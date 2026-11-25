/* 版权所有 (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * 本程序是自由软件：你可以依据自由软件基金会发布的 GNU 通用公共许可证
 * （第 2 版，或你选择的任何更高版本）的条款，重新分发和/或修改本程序。
 *
 * 分发本程序是希望它有用，但不作任何担保，甚至不担保适销性或
 * 适用于特定用途。详见 GNU 通用公共许可证。
 *
 * 你应当已经随本程序收到一份 GNU 通用公共许可证的副本；
 * 如果没有，见 <http://gnu.org/licenses/>。
 */

#include <SDL3/SDL.h>
#include "gfx/gl.h"
#include <cglm/cglm.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/utfsjis.h"

#include "gfx/gfx.h"
#include "gfx/private.h"
#include "icon.h"
#include "xsystem4.h"
#include "preview3d_vk.h"

struct sdl_private sdl;

/* [D517k] 渲染后端开关：`XSYS4_RENDER_BACKEND=vulkan` 时整帧走 SDL_GPU 的
 * Vulkan 驱动（src/gfx_vk.c），GL 的 gfx 绘制全部跳过。默认仍是 GL。
 * 互斥原因：SDL 窗口只能属于一个图形 API（OPENGL 或 VULKAN）。 */
static bool vk_mode;

bool gfx_vk_mode(void)
{
	return vk_mode;
}

static const GLchar glsl_preamble[] =
#ifdef USE_GLES
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n";
#else
	"#version 140\n";
#endif

/*
 * 从窗口坐标系变换到裁剪空间。
 *
 * 窗口坐标系：x= 0..w，y= 0..h（+y 向下）
 * 裁剪空间：  x=-1..1，y=-1..1（+y 向上）
 *
 * +-                 -+
 * | 2/w   0    0   -1 |
 * |  0   -2/h  0    1 |
 * |  0    0    2   -1 |
 * |  0    0    0    1 |
 * +-                 -+
 */
static mat4 world_view_transform = MAT4(
	0, 0, 0, -1,
	0, 0, 0, -1,
	0, 0, 1, -1,
	0, 0, 0,  1);

static struct shader default_shader;

static GLuint main_surface_fb;
static struct texture main_surface;
static struct texture *view = &main_surface;
/* [D521b] Vulkan 上屏用：GL 离屏窗口/context 与回读缓冲。 */
static SDL_Window *vk_gfx_win;
static SDL_GLContext vk_gfx_ctx;
static unsigned char *vk_rgb, *vk_rgba;
static int vk_rw, vk_rh;
static GLint max_texture_size;
static SDL_Color clear_color = { 0, 0, 0, 255 };
static float frame_rate;
static bool wait_vsync = false;

static GLchar *read_shader_file(const char *path)
{
	GLchar *source = SDL_LoadFile(path, NULL);
	if (!source) {
		char full_path[PATH_MAX];
		snprintf(full_path, PATH_MAX, "%s/%s", xsys4_data_dir(), path);
		source = SDL_LoadFile(full_path, NULL);
		if (!source)
			ERROR("Failed to load shader file %s", full_path, strerror(errno));
	}
	return source;
}

GLuint gfx_load_shader_file(const char *path, GLenum type, const char *defines)
{
	GLint shader_compiled;
	GLuint shader;
	const GLchar *source[3] = {
		glsl_preamble,
		defines ? defines : "",
		read_shader_file(path)
	};
	shader = glCreateShader(type);
	glShaderSource(shader, 3, source, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &shader_compiled);
	if (!shader_compiled) {
		GLint len;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		char *infolog = xmalloc(len + 1);
		glGetShaderInfoLog(shader, len, NULL, infolog);
		ERROR("Failed to compile shader %s: %s", path, infolog);
	}
	SDL_free((char*)source[2]);
	return shader;
}

void gfx_load_shader(struct shader *dst, const char *vertex_shader_path, const char *fragment_shader_path)
{
	GLuint program = glCreateProgram();
	GLuint vertex_shader = gfx_load_shader_file(vertex_shader_path, GL_VERTEX_SHADER, NULL);
	GLuint fragment_shader = gfx_load_shader_file(fragment_shader_path, GL_FRAGMENT_SHADER, NULL);

	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);

	GLint link_success;
	glGetProgramiv(program, GL_LINK_STATUS, &link_success);
	if (!link_success)
		ERROR("Failed to link shader: %s, %s", vertex_shader_path, fragment_shader_path);

	dst->program = program;
	dst->world_transform = glGetUniformLocation(program, "world_transform");
	dst->view_transform = glGetUniformLocation(program, "view_transform");
	dst->texture = glGetUniformLocation(program, "tex");
	dst->vertex_pos = glGetAttribLocation(program, "vertex_pos");
	dst->vertex_uv = glGetAttribLocation(program, "vertex_uv");
	dst->prepare = NULL;
}

static int gl_initialize(void)
{
	gfx_load_shader(&default_shader, "shaders/render.v.glsl", "shaders/render.f.glsl");

	const struct gfx_vertex vertex_data[] = {
		//  x,   y,   z,   w,   u,   v
		{ 0.f, 0.f, 0.f, 1.f, 0.f, 0.f },
		{ 1.f, 0.f, 0.f, 1.f, 1.f, 0.f },
		{ 0.f, 1.f, 0.f, 1.f, 0.f, 1.f },
		{ 1.f, 1.f, 0.f, 1.f, 1.f, 1.f }
	};

	const GLuint rect_index_data[] = { 0, 1, 3, 2 };
	const GLuint line_index_data[] = { 0, 3 };

	glGenVertexArrays(1, &sdl.gl.vao);
	glBindVertexArray(sdl.gl.vao);

	glGenBuffers(1, &sdl.gl.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data, GL_STATIC_DRAW);
	glGenBuffers(1, &sdl.gl.quad_vbo);

	glGenBuffers(1, &sdl.gl.rect_ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.rect_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 4 * sizeof(GLuint), rect_index_data, GL_STATIC_DRAW);

	glGenBuffers(1, &sdl.gl.line_ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.line_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 2 * sizeof(GLuint), line_index_data, GL_STATIC_DRAW);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glBindVertexArray(0);

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);

	return 0;
}

static void set_window_title(void)
{
	char title[1024] = { [1023] = 0 };
	char *game_name = sjis2utf(config.game_name, 0);
	snprintf(title, 1023, "%s - XSystem4", game_name);
	free(game_name);

	SDL_SetWindowTitle(sdl.window, title);
}

static bool gfx_initialized = false;

/*
 * 显示器分辨率比游戏分辨率小时，
 * 就缩小窗口尺寸。
 */
static void init_window_size(void)
{
#ifndef __ANDROID__
	int display_id = SDL_GetWindowDisplayIndex(sdl.window);
	if (display_id < 0)
		return;

	SDL_Rect bounds;
	if (SDL_GetDisplayUsableBounds(display_id, &bounds) < 0)
		return;

	int w, h;
	SDL_GetWindowSize(sdl.window, &w, &h);
	if (bounds.w > w && bounds.h > h)
		return;

	int border_top, border_bot;
	if (SDL_GetWindowBordersSize(sdl.window, &border_top, NULL, &border_bot, NULL) < 0)
		return;

	int scaled_h = bounds.h - (border_top + border_bot);
	int scaled_w = ((float)scaled_h / h) * w;
	int scaled_x = bounds.w / 2 - scaled_w / 2;

	SDL_SetWindowSize(sdl.window, scaled_w, scaled_h);
	SDL_SetWindowPosition(sdl.window, bounds.x + scaled_x, bounds.y + border_top);
#endif
}

int gfx_init(void)
{
	if (gfx_initialized)
		return true;

	uint32_t flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO;
	if (config.joypad)
		flags |= SDL_INIT_GAMECONTROLLER;
	if (SDL_Init(flags) < 0)
		ERROR("SDL_Init failed: %s", SDL_GetError());

	{
		const char *bk = getenv("XSYS4_RENDER_BACKEND");
		vk_mode = bk && (strstr(bk, "vulkan") || strcmp(bk, "vk") == 0);
	}

	/* Vulkan 模式下**不要**设 GL 上下文属性：SDL 内部会因此偏向 GL，
	 * `SDL_CreateGPUDevice` 直接报 "No supported SDL_GPU backend found"
	 * （d695 实测；同样的代码在裸测试程序里能拿到 driver=vulkan）。 */
	if (!vk_mode) {
#ifdef USE_GLES
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
	}

	/* [D329] SDL3 没有 SDL_AllocFormat：格式描述由 SDL 全局持有，
	 * 用 SDL_GetPixelFormatDetails 取只读指针，且**不得释放**。 */
	sdl.format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
	/* [D329] SDL3 的 SDL_CreateWindow 去掉了 x/y（6 参 -> 4 参），
	 * 也没有 SDL_WINDOW_SHOWN（建出来就是可见的）。 */
	/* [D517k] Vulkan 探测：**先建一个隐藏窗口试出来**，拿不到 SDL_GPU
	 * 的 vulkan 设备就回退 GL。不能先建 VULKAN 窗口再失败——SDL 窗口
	 * 只能属于一个图形 API，回退必须重建。
	 * （本机实测：SDL 只报 driver[0]=vulkan，但 CreateGPUDevice 在引擎里
	 * 返回 "unsupported"，而裸测试程序同环境能成功 —— 待续排查，见
	 * docs/RENDER-BACKEND.md。） */
	if (vk_mode) {
		/* [D521] 探测与正式初始化分离：probe 不建窗口不 claim——
		 * 此前 probe 窗口 claim 后销毁，销毁事件触发
		 * VULKAN_INTERNAL_OnWindowResize 解引用已失效 swapchain，
		 * 核心转储（d810）。 */
		if (!gfx_vk_probe()) {
			NOTICE("[vk] Vulkan 后端不可用，回退 GL（%s）", SDL_GetError());
			vk_mode = false;
		} else {
			/* [D521c] 不带 SDL_WINDOW_VULKAN：SDL3 会在带该 flag 的
			 * 窗口创建期自动挂 GPU watcher（wayland 下
			 * Wayland_CreateWindow 内部发 SIZE_CHANGED →
			 * OnWindowResize → FetchWindowData 为 NULL → 崩，
			 * d821 gdb 栈实证）。claim 不依赖此 flag。 */
			sdl.window = SDL_CreateWindow("XSystem4",
					config.view_width, config.view_height,
					SDL_WINDOW_RESIZABLE);
			if (!sdl.window)
				ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
			max_texture_size = 8192;
			/* [D521b] GL 离屏合成：2D 全部照旧走 GL（main_surface
			 * FBO），Vulkan 只做上屏。隐藏 GL 窗口 + context。 */
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
					    SDL_GL_CONTEXT_PROFILE_CORE);
			vk_gfx_win = SDL_CreateWindow("gfx-offscreen", 8, 8,
					SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
			vk_gfx_ctx = vk_gfx_win
					     ? SDL_GL_CreateContext(vk_gfx_win)
					     : NULL;
			if (!vk_gfx_win || !vk_gfx_ctx)
				ERROR("GL 离屏窗口创建失败：%s", SDL_GetError());
			if (!SDL_GL_MakeCurrent(vk_gfx_win, vk_gfx_ctx))
				ERROR("GL MakeCurrent 失败：%s", SDL_GetError());
			{
				const unsigned char *glv = glGetString(GL_VERSION);
				const unsigned char *glr = glGetString(GL_RENDERER);
				NOTICE("[vk] offscreen GL %s on %s",
				       glv ? (const char *)glv : "(null)",
				       glr ? (const char *)glr : "(null)");
			}
			set_window_title();
			if (!gfx_vk_init(sdl.window))
				ERROR("Vulkan 探测通过但正式窗口初始化失败：%s",
				      SDL_GetError());
			gfx_initialized = true;
			return 0;
		}
	}

	sdl.window = SDL_CreateWindow("XSystem4",
				      config.view_width,
				      config.view_height,
				      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!sdl.window)
		ERROR("SDL_CreateWindow failed: %s", SDL_GetError());

	set_window_title();

	sdl.gl.context = SDL_GL_CreateContext(sdl.window);
	if (!sdl.gl.context)
		ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());

#ifndef USE_GLES
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK && err != GLEW_ERROR_NO_GLX_DISPLAY)
		ERROR("glewInit failed");
#endif

	/* [D249] 打实际 GL renderer —— 「有没有用上硬件加速」的唯一硬证据。
	 * llvmpipe/softpipe = 软件；`Mesa Intel(R) Graphics` = 硬件。
	 * 之前跑测被脚本写死成 `LIBGL_ALWAYS_SOFTWARE=1` + `GALLIUM_DRIVER=llvmpipe`，
	 * 13.5fps 的瓶颈有一半在这里，而没有这行日志就看不出来。 */
	if (getenv("XSYS4_GL_DBG")) {
		const GLubyte *gv = glGetString(GL_VENDOR);
		const GLubyte *gr = glGetString(GL_RENDERER);
		const GLubyte *gs = glGetString(GL_VERSION);
		NOTICE("[gl] vendor=%s renderer=%s version=%s",
		       gv ? (const char *)gv : "?", gr ? (const char *)gr : "?",
		       gs ? (const char *)gs : "?");
	}

	SDL_GL_SetSwapInterval(wait_vsync ? 1 : 0);
	gl_initialize();
	gfx_draw_init();
	gfx_set_window_logical_size(config.view_width, config.view_height);
	/* [D377] 启动即真全屏（无窗口边框、无 WM 标题栏）。
	 *
	 * 为什么需要这一条：引擎本来只有 F11（`gfx_toggle_fullscreen`）和游戏内
	 * `SystemService.ChangeFullScreen` 两个入口，**启动时一律窗口化** ——
	 * 于是在桌面环境里顶上永远挂着一条标题栏（"<游戏名> - XSystem4"），
	 * 不是玩家要的全屏。现在 `.xsys4rc` 写 `fullscreen = 1` 就能默认全屏，
	 * 临时试用则 `XSYS4_FULLSCREEN=1`（`=0` 强制窗口化，覆盖配置文件）。
	 *
	 * 全屏时跳过 `init_window_size()`：它按「可用桌面区域」缩窗口并挪位置，
	 * 与全屏互相打架（会看到一帧窗口化的画面再被全屏盖住）。 */
	bool want_fullscreen = config.fullscreen;
	const char *fs_env = getenv("XSYS4_FULLSCREEN");
	if (fs_env && *fs_env)
		want_fullscreen = strcmp(fs_env, "0") != 0;
	if (want_fullscreen)
		gfx_set_fullscreen(true);
	else
		init_window_size();
	atexit(gfx_fini);
	gfx_clear();
	icon_init();
	gfx_initialized = true;
	return 0;
}

void gfx_fini(void)
{
	if (vk_mode) {
		gfx_vk_fini();
		SDL_DestroyWindow(sdl.window);
		SDL_Quit();
		return;
	}
	glDeleteProgram(default_shader.program);
	SDL_DestroyWindow(sdl.window);
	/* [D329] SDL3 的格式描述归 SDL 所有，不能释放。 */
	SDL_Quit();
}

Texture *gfx_main_surface(void)
{
	return &main_surface;
}

static void main_surface_init(int w, int h)
{
	gfx_delete_texture(&main_surface);

	glGenTextures(1, &main_surface.handle);
	glBindTexture(GL_TEXTURE_2D, main_surface.handle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	main_surface.w = w;
	main_surface.h = h;

	glGenFramebuffers(1, &main_surface_fb);
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, main_surface.handle, 0);
}

void gfx_set_window_logical_size(int w, int h)
{
	sdl.w = w;
	sdl.h = h;
	world_view_transform[0][0] = 2.0 / w;
	world_view_transform[1][1] = 2.0 / h;
	gfx_update_screen_scale();
	main_surface_init(w, h);
}

/* [D377] 全屏状态自己记一份，不信 SDL 的 flags。
 *
 * 实测（Xvfb `:90`，无窗口管理器）：`SDL_SetWindowFullscreen(SDL_WINDOW_FULLSCREEN)`
 * 返回 **0（成功）**，但紧接着 `SDL_GetWindowFlags()` 里没有 SDL_WINDOW_FULLSCREEN
 * —— 因为 X11 后端的全屏靠 `_NET_WM_STATE_FULLSCREEN`，没有 WM 就没人执行它。
 * 所以判据必须是「回读 flags」，flags 没变就走**无边框铺满**这条等价路线。 */
static bool fullscreen_active;
/* 进全屏前的窗口尺寸：退出全屏要还原，否则会留下一块铺满屏幕的窗口。 */
static int pre_fs_w, pre_fs_h;

/* 无边框铺满：去掉 WM 装饰 + 挪到 (0,0) + 撑到显示器尺寸。
 * 这条路线在任何环境下都有效（连没有 WM 的 Xvfb 也一样），
 * 效果与真全屏在视觉上一致 —— 顶上不再有标题栏。 */
static void window_fill_display(bool on)
{
	SDL_DisplayID display = SDL_GetDisplayForWindow(sdl.window);
	SDL_Rect bounds = { 0, 0, 0, 0 };
	if (display)
		SDL_GetDisplayBounds(display, &bounds);

	SDL_SetWindowBordered(sdl.window, on ? false : true);
	if (on) {
		if (bounds.w > 0 && bounds.h > 0) {
			SDL_SetWindowPosition(sdl.window, bounds.x, bounds.y);
			SDL_SetWindowSize(sdl.window, bounds.w, bounds.h);
		}
	}
	gfx_update_screen_scale();
}

bool gfx_set_fullscreen(bool enable)
{
	/* [D329] SDL3 删除了 SDL_WINDOW_FULLSCREEN_DESKTOP：SDL_WINDOW_FULLSCREEN
	 * 在 SDL3 里就是「桌面全屏」（不再改显示模式）。 */
	if (SDL_SetWindowFullscreen(sdl.window,
				    enable ? SDL_WINDOW_FULLSCREEN : 0) < 0)
		WARNING("Failed to set fullscreen mode: %s", SDL_GetError());

	if (enable && !fullscreen_active) {
		pre_fs_w = pre_fs_h = 0;
		SDL_GetWindowSize(sdl.window, &pre_fs_w, &pre_fs_h);
	}

	bool sdl_fs = (SDL_GetWindowFlags(sdl.window) & SDL_WINDOW_FULLSCREEN) != 0;
	/* [D517p] 成功路径也要留痕：SDL 生效时是静默的，实测没法区分
	 * 「生效了」与「压根没被调」（d731 首测的教训）。 */
	NOTICE("[gfx] set_fullscreen(%d) -> SDL flag %s", enable,
	       sdl_fs ? "ON" : "off");
	if (enable && !sdl_fs) {
		/* SDL 的全屏没生效（见上面的说明）→ 退回无边框铺满。 */
		window_fill_display(true);
		fullscreen_active = true;
		int dw = 0, dh = 0;
		SDL_GetWindowSizeInPixels(sdl.window, &dw, &dh);
		NOTICE("[gfx] fullscreen on (SDL 未生效 → 无边框铺满) drawable=%dx%d",
		       dw, dh);
		return true;
	}
	if (!enable) {
		window_fill_display(false);
		/* 还原进全屏前的尺寸（只在确实被放大过时才还原）。 */
		if (pre_fs_w > 0 && pre_fs_h > 0) {
			int cw = 0, ch = 0;
			SDL_GetWindowSize(sdl.window, &cw, &ch);
			if (cw * ch > pre_fs_w * pre_fs_h)
				SDL_SetWindowSize(sdl.window, pre_fs_w, pre_fs_h);
			gfx_update_screen_scale();
		}
	}

	fullscreen_active = sdl_fs;
	int dw = 0, dh = 0;
	SDL_GetWindowSizeInPixels(sdl.window, &dw, &dh);
	NOTICE("[gfx] fullscreen %s -> flags_fs=%d drawable=%dx%d",
	       enable ? "on" : "off", sdl_fs ? 1 : 0, dw, dh);
	return true;
}

bool gfx_is_fullscreen(void)
{
	return fullscreen_active;
}

void gfx_toggle_fullscreen(void)
{
	gfx_set_fullscreen(!gfx_is_fullscreen());
}

/* [D517u] 窗口页「SYS_初始化窗口位置和大小」：
 * 窗口还原到基准尺寸（视图分辨率原尺寸）并居中。
 * RECORD_POS_SIZE（SetWindowSetting type 3）决定跨会话是否记住，这里只管本次复位。
 * 全屏中调用是无效操作（原版语义：先退出全屏才有窗口可复位），直接返回真。 */
bool gfx_init_main_window_pos_size(void)
{
	if (!sdl.window)
		return false;
	if (fullscreen_active) {
		NOTICE("[gfx] init_main_window_pos_size: 全屏中忽略");
		return true;
	}
	SDL_SetWindowSize(sdl.window, config.view_width, config.view_height);
	SDL_SetWindowPosition(sdl.window, SDL_WINDOWPOS_CENTERED,
			      SDL_WINDOWPOS_CENTERED);
	gfx_update_screen_scale();
	NOTICE("[gfx] init_main_window_pos_size -> %dx%d centered",
	       config.view_width, config.view_height);
	return true;
}

void gfx_update_screen_scale(void)
{
	int display_w, display_h;
	/* [D329] SDL_GL_GetDrawableSize -> SDL_GetWindowSizeInPixels（同语义）。 */
	SDL_GetWindowSizeInPixels(sdl.window, &display_w, &display_h);

	if (display_w * sdl.h == display_h * sdl.w) {
		// 宽高比一样，按比例缩放即可。
		sdl.viewport.x = 0;
		sdl.viewport.y = 0;
		sdl.viewport.w = display_w;
		sdl.viewport.h = display_h;
		return;
	}

	double logical_aspect = (double)sdl.w / sdl.h;
	double display_aspect = (double)display_w / display_h;
	if (logical_aspect > display_aspect) {
		// letterbox
		sdl.viewport.w = display_w;
		sdl.viewport.h = sdl.h * display_w / sdl.w;
		sdl.viewport.x = 0;
		sdl.viewport.y = (display_h - sdl.viewport.h) / 2;
	} else {
		// pillarbox (side bars)
		sdl.viewport.w = sdl.w * display_h / sdl.h;
		sdl.viewport.h = display_h;
		sdl.viewport.x = (display_w - sdl.viewport.w) / 2;
		sdl.viewport.y = 0;
	}
}

void gfx_set_wait_vsync(bool wait)
{
	wait_vsync = wait;
	if (gfx_initialized)
		SDL_GL_SetSwapInterval(wait ? 1 : 0);
}

int gfx_view_width(void)
{
	return sdl.w;
}

int gfx_view_height(void)
{
	return sdl.h;
}

void gfx_set_clear_color(int r, int g, int b, int a)
{
	clear_color.r = max(0, min(255, r));
	clear_color.g = max(0, min(255, g));
	clear_color.b = max(0, min(255, b));
	clear_color.a = max(0, min(255, a));
}

void gfx_clear(void)
{
	if (vk_mode)
		return; /* 清屏由 gfx_vk 的 render pass 完成 */
	glClearColor(clear_color.r / 255.f, clear_color.g / 255.f, clear_color.b / 255.f, clear_color.a / 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
}

static mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;

void gfx_set_view_offset(int x, int y)
{
	glm_mat4_identity(mw_transform);
	if (!x && !y)
		return;
	vec3 off = { (float)x / config.view_width, (float)y / config.view_height, 0 };
	glm_translate(mw_transform, off);
}

float gfx_get_frame_rate(void)
{
	return frame_rate;
}

static void gfx_update_frame_rate_counter(void)
{
	static uint64_t timestamp;
	static int frame_count;

	frame_count++;
	uint64_t current_time = SDL_GetTicks64();
	if (current_time > timestamp + 1000) {
		frame_rate = frame_count * 1000.f / (current_time - timestamp);
		timestamp = current_time;
		frame_count = 0;
	}
}

void gfx_swap(void)
{
	if (vk_mode) {
		/* [D521b] GL 合成结果回读 → RGBA → Vulkan 上屏。
		 * present 已在 gfx_vk_blit_frame 的 SubmitGPUCommandBuffer
		 * 里完成。 */
		glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
		if (vk_rw != sdl.w || vk_rh != sdl.h || !vk_rgb) {
			free(vk_rgb);
			free(vk_rgba);
			vk_rw = sdl.w;
			vk_rh = sdl.h;
			vk_rgb = malloc((size_t)vk_rw * vk_rh * 3);
			vk_rgba = malloc((size_t)vk_rw * vk_rh * 4);
		}
		if (vk_rgb && vk_rgba) {
			glReadPixels(0, 0, vk_rw, vk_rh, GL_RGB,
				     GL_UNSIGNED_BYTE, vk_rgb);
			/* RGB → RGBA；行序翻转（GL FBO 下原点 → 上原点） */
			for (int y = 0; y < vk_rh; y++) {
				const unsigned char *src =
					vk_rgb + (size_t)(vk_rh - 1 - y) * vk_rw * 3;
				unsigned char *dst =
					vk_rgba + (size_t)y * vk_rw * 4;
				for (int x = 0; x < vk_rw; x++) {
					dst[x * 4 + 0] = src[x * 3 + 0];
					dst[x * 4 + 1] = src[x * 3 + 1];
					dst[x * 4 + 2] = src[x * 3 + 2];
					dst[x * 4 + 3] = 255;
				}
			}
			gfx_vk_blit_frame(vk_rgba, vk_rw, vk_rh);
		}
		gfx_update_frame_rate_counter();
		return;
	}
	/* [D112] 首帧探针：XSYS4_SWAP_LOG=1 会记录前几次交换，
	 * 这样启动器可以在第一帧呈现之后立刻冻结进程。 */
	if (getenv("XSYS4_SWAP_LOG")) {
		static int n = 0;
		if (n < 5)
			NOTICE("[swap] %d", ++n);
	}
	/* XSYS4_DUMP_FRAME=<dir>: 每 N 次 swap 把 main_surface 存成 PNG。
	 * 真实桌面是 Wayland/XWayland，外部截图工具拿不到合成后的画面
	 * （x11grab 全黑、import 只拿到过期的 backing pixmap），只能由引擎
	 * 自己导出帧缓冲作为“到底画出了什么”的判据。
	 * XSYS4_DUMP_EVERY 控制间隔（默认 200 次 swap）。 */
	if (getenv("XSYS4_DUMP_FRAME")) {
		static int n;
		static int every = -1;
		if (every < 0) {
			const char *e = getenv("XSYS4_DUMP_EVERY");
			every = e && *e ? atoi(e) : 200;
		}
		if (++n % every == 0) {
			char path[PATH_MAX];
			snprintf(path, sizeof path, "%s/frame_%06d.png",
				 getenv("XSYS4_DUMP_FRAME"), n);
			gfx_save_texture(gfx_main_surface(), path, ALCG_PNG);
			NOTICE("[frame] dumped %s (swap #%d)", path, n);
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(sdl.viewport.x, sdl.viewport.y, sdl.viewport.w, sdl.viewport.h);
	gfx_clear();

	static mat4 wv_transform = MAT4(
		2,  0, 0, -1,
		0, -2, 0,  1,
		0,  0, 1,  0,
		0,  0, 0,  1);
	struct gfx_render_job job = {
		.shader = &default_shader,
		.shape = GFX_RECTANGLE,
		.texture = view->handle,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = view
	};
	gfx_render(&job);

	SDL_GL_SwapWindow(sdl.window);
	glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);
	glViewport(0, 0, sdl.w, sdl.h);

	gfx_update_frame_rate_counter();
}

void gfx_set_view(struct texture *t)
{
	view = t;
}

void gfx_reset_view(void)
{
	view = &main_surface;
}

/*
 * 设置必需的着色器参数。
 */
/* [D517j] 填单位正方形到 quad_vbo 并 prepare。
 * 3D 预览用：顶点固定 (0,0)-(1,1)，几何全部由 world_transform 表达
 * （model 矩阵），view_transform 传透视矩阵（与 draw.c 的 copy_rotate_y
 * 同一套用法：gl_Position = view * world * vertex_pos）。
 *
 * **顶点顺序必须匹配 `rect_index_data = {0,1,3,2}`**（TRIANGLE_FAN）：
 * 它要求「左上 / 右上 / 左下 / 右下」这种 Z 序，而不是环形序
 * (0,0)(1,0)(1,1)(0,1)。用环形序会把四边形拼成蝴蝶结 ——
 * 两个三角形交叉、对角两侧缺失，看上去就是「只画了半个」，
 * d693 的背景板正是这个现象。 */
void gfx_prepare_unit_quad(struct gfx_render_job *job)
{
	static const struct gfx_vertex unit[4] = {
		{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }, /* v0 左上 */
		{ 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f }, /* v1 右上 */
		{ 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f }, /* v2 左下 */
		{ 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f }, /* v3 右下 */
	};
	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.quad_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(unit), unit, GL_DYNAMIC_DRAW);
	job->shape = GFX_QUADRILATERAL;
	gfx_prepare_job(job);
}

void gfx_prepare_job(struct gfx_render_job *job)
{	glUseProgram(job->shader->program);

	glUniformMatrix4fv(job->shader->world_transform, 1, GL_FALSE, job->world_transform);
	glUniformMatrix4fv(job->shader->view_transform, 1, GL_FALSE, job->view_transform);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, job->texture);
	glUniform1i(job->shader->texture, 0);

	if (job->shader->prepare)
		job->shader->prepare(job, job->data);
}

/*
 * 执行之前用 gfx_prepare_job 建好的渲染任务。
 */
void gfx_run_job(struct gfx_render_job *job)
{
	glBindVertexArray(sdl.gl.vao);
	glEnableVertexAttribArray(job->shader->vertex_pos);
	glEnableVertexAttribArray(job->shader->vertex_uv);

	glBindBuffer(GL_ARRAY_BUFFER, job->shape == GFX_QUADRILATERAL ? sdl.gl.quad_vbo : sdl.gl.vbo);
	glVertexAttribPointer(job->shader->vertex_pos, 4, GL_FLOAT, GL_FALSE, sizeof(struct gfx_vertex), NULL);
	glVertexAttribPointer(job->shader->vertex_uv, 2, GL_FLOAT, GL_FALSE, sizeof(struct gfx_vertex), (void*)offsetof(struct gfx_vertex, u));

	switch (job->shape) {
	case GFX_RECTANGLE:
	case GFX_QUADRILATERAL:
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.rect_ibo);
		glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, NULL);
		break;
	case GFX_LINE:
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.gl.line_ibo);
		glDrawElements(GL_LINES, 2, GL_UNSIGNED_INT, NULL);
		break;
	}

	glDisableVertexAttribArray(job->shader->vertex_pos);
	glDisableVertexAttribArray(job->shader->vertex_uv);
	glBindVertexArray(0);
	glUseProgram(0);
}

void gfx_render(struct gfx_render_job *job)
{
	if (!job->shader) {
		job->shader = &default_shader;
		gfx_prepare_job(job);
		gfx_run_job(job);
		job->shader = NULL;
	} else {
		gfx_prepare_job(job);
		gfx_run_job(job);
	}
}

void _gfx_render_texture(struct shader *s, struct texture *t, Rectangle *r, void *data)
{
	if (!t->handle) {
		WARNING("Attempted to render uninitialized texture");
		return;
	}

	mat4 world_transform = WORLD_TRANSFORM(t->w, t->h, r ? r->x : 0, r ? r->y : 0);

	struct gfx_render_job job = {
		.shader = s,
		.shape = GFX_RECTANGLE,
		.texture = t->handle,
		.world_transform = world_transform[0],
		.view_transform = world_view_transform[0],
		.data = data
	};
	gfx_render(&job);
}

void gfx_render_texture(struct texture *t, Rectangle *r)
{
	_gfx_render_texture(&default_shader, t, r, t);
}

void gfx_render_quadrilateral(struct texture *t, struct gfx_vertex vertices[4])
{
	if (!t->handle) {
		WARNING("Attempted to render uninitialized texture");
		return;
	}

	// 把纹理坐标归一化到 [0, 1]
	struct gfx_vertex buf[4];
	memcpy(buf, vertices, sizeof(struct gfx_vertex) * 4);
	for (int i = 0; i < 4; i++) {
		buf[i].u /= t->w;
		buf[i].v /= t->h;
	}

	glBindBuffer(GL_ARRAY_BUFFER, sdl.gl.quad_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(buf), buf, GL_DYNAMIC_DRAW);

	mat4 world_transform = GLM_MAT4_IDENTITY_INIT;
	struct gfx_render_job job = {
		.shader = &default_shader,
		.shape = GFX_QUADRILATERAL,
		.texture = t->handle,
		.world_transform = world_transform[0],
		.view_transform = world_view_transform[0],
		.data = t
	};
	gfx_render(&job);
}

/* [MEM] [mem]（XSYS4_MEM_STAT）的生命周期计数：我们一共建过多少
 * GL 纹理、上传过多少像素。纹理数在涨而活对象数不变，
 * 就说明泄漏发生在 VM 堆之外。 */
size_t gfx_texture_count = 0;
size_t gfx_texture_pixels = 0;

static void init_texture(struct texture *t, int w, int h)
{
	if (vk_mode) {
		/* Vulkan 路径不建 GL 纹理：handle=0 让所有 GL 绘制 early-return
		 * （gfx_delete_texture / _gfx_render_texture 都判 handle）。 */
		t->w = w;
		t->h = h;
		t->handle = 0;
		return;
	}
	gfx_texture_count++;
	gfx_texture_pixels += (size_t)(w > 0 ? w : 0) * (size_t)(h > 0 ? h : 0);
	glGenTextures(1, &t->handle);
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	t->w = w;
	t->h = h;
}

void gfx_init_texture_with_pixels(struct texture *t, int w, int h, void *pixels)
{

	init_texture(t, w, h);
	if (vk_mode)
		return;
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void gfx_update_texture_with_pixels(struct texture *t, void *pixels)
{
	glBindTexture(GL_TEXTURE_2D, t->handle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void gfx_init_texture_with_cg(struct texture *t, struct cg *cg)
{

	gfx_init_texture_with_pixels(t, cg->metrics.w, cg->metrics.h, cg->pixels);
}

void gfx_init_texture_rgba(struct texture *t, int w, int h, SDL_Color color)
{

	if (w > max_texture_size) {
		WARNING("Texture width %d exceeds maximum texture size %d", w, max_texture_size);
		w = max_texture_size;
	}
	if (h > max_texture_size) {
		WARNING("Texture height %d exceeds maximum texture size %d", h, max_texture_size);
		h = max_texture_size;
	}
	gfx_init_texture_blank(t, w, h);
	if (w <= 0 || h <= 0)
		return;
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, t, 0, 0, w, h);
	glClearColor(color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

void gfx_init_texture_rgb(struct texture *t, int w, int h, SDL_Color color)
{

	if (w > max_texture_size) {
		WARNING("Texture width %d exceeds maximum texture size %d", w, max_texture_size);
		w = max_texture_size;
	}
	if (h > max_texture_size) {
		WARNING("Texture height %d exceeds maximum texture size %d", h, max_texture_size);
		h = max_texture_size;
	}
	init_texture(t, w, h);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	if (w <= 0 || h <= 0)
		return;
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, t, 0, 0, w, h);
	glClearColor(color.r / 255.f, color.g / 255.f, color.b / 255.f, 255.f);
	glClear(GL_COLOR_BUFFER_BIT);
	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
}

void gfx_init_texture_amap(struct texture *t, int w, int h, uint8_t *amap, SDL_Color color)
{

	uint32_t *pixels = xmalloc(sizeof(uint32_t) * w * h);
	for (int i = 0; i < w*h; i++) {
		/* [D329] SDL3 的 SDL_MapRGBA 多了 palette 参数（无调色板传 NULL）。 */
		uint32_t c = SDL_MapRGBA(sdl.format, NULL, color.r, color.g, color.b, amap[i]);
		pixels[i] = c;
	}

	gfx_init_texture_with_pixels(t, w, h, pixels);
	free(pixels);
}

void gfx_init_texture_rmap(struct texture *t, int w, int h, uint8_t *rmap)
{

	init_texture(t, w, h);
	/* rmap 是单通道字形位图（字体用）。展开成 RGBA 再上传：parts / sprite
	 * 这类通用着色器按 vec4 采样，GL_R8 在它们眼里 rgb 恒 0、alpha 恒 1，
	 * 结果文字要么整块黑、要么整块透明——实测标题菜单的字形完全不出画。
	 * 展开后 r=g=b=a=覆盖率，读 .r 的专用着色器（blend_rmap_color /
	 * dilate）行为不变，通用着色器也能正常混合。 */
	uint32_t *px = xmalloc(sizeof(uint32_t) * w * h);
	for (int i = 0; i < w * h; i++) {
		uint32_t a = rmap[i];
		px[i] = a | (a << 8) | (a << 16) | (a << 24);
	}
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	free(px);
}

void gfx_init_texture_blank(struct texture *t, int w, int h)
{

	gfx_init_texture_with_pixels(t, w, h, NULL);
}

void gfx_copy_main_surface(struct texture *dst)
{
	init_texture(dst, main_surface.w, main_surface.h);
	glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, main_surface.w, main_surface.h, 0);
}

void gfx_delete_texture(struct texture *t)
{
	if (t->handle)
		glDeleteTextures(1, &t->handle);
	t->handle = 0;
}

GLuint gfx_set_framebuffer(GLenum target, Texture *t, int x, int y, int w, int h)
{

	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(target, fbo);
	glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->handle, 0);
	glViewport(x, y, w, h);

	GLenum st = glCheckFramebufferStatus(target);
	if (st != GL_FRAMEBUFFER_COMPLETE) {
		/* [D521c] 打码诊断：status + 纹理信息；不退出（继续跑，
		 * 让后续错误流可见）。 */
		static int nfb;
		if (nfb++ < 10)
			WARNING("Incomplete framebuffer: 0x%x tex=%u %dx%d",
				st, t->handle, t->w, t->h);
	}
	return fbo;
}

void gfx_reset_framebuffer(GLenum target, GLuint fbo)
{

	glBindFramebuffer(target, main_surface_fb);
	glDeleteFramebuffers(1, &fbo);
	/* [L3] main_surface_fb 是按 VIEW 尺寸（config.view_width/height）建的，
	 * 不是按窗口尺寸。把视口恢复成 sdl.w/h 会让 viewport != FBO，
	 * 只要窗口不是正好 1920x1080（例如 1440x900）就会出问题。 */
	glViewport(0, 0, config.view_width, config.view_height);
}

SDL_Color gfx_get_pixel(Texture *t, int x, int y)
{
	GLuint fbo = gfx_set_framebuffer(GL_READ_FRAMEBUFFER, t, 0, 0, t->w, t->h);

	uint8_t pixel[4];
	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

	gfx_reset_framebuffer(GL_READ_FRAMEBUFFER, fbo);

	return (SDL_Color) {
		.r = pixel[0],
		.g = pixel[1],
		.b = pixel[2],
		.a = pixel[3],
	};
}

void *gfx_get_pixels(Texture *t)
{
	GLuint fbo = gfx_set_framebuffer(GL_READ_FRAMEBUFFER, t, 0, 0, t->w, t->h);
	void *pixels = xmalloc(t->w * t->h * 4);
	glReadPixels(0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	gfx_reset_framebuffer(GL_READ_FRAMEBUFFER, fbo);
	return pixels;
}

int gfx_save_texture(Texture *t, const char *path, enum cg_type format)
{
	void *pixels = gfx_get_pixels(t);
	struct cg cg = {
		.type = ALCG_UNKNOWN,
		.metrics = {
			.w = t->w,
			.h = t->h,
			.bpp = 24,
			.has_pixel = true,
			.has_alpha = true,
			.pixel_pitch = t->w * 3,
			.alpha_pitch = 1
		},
		.pixels = pixels
	};
	FILE *fp = file_open_utf8(path, "wb");
	if (!fp) {
		WARNING("Failed to open %s: %s", display_utf0(path), strerror(errno));
		free(pixels);
		return 0;
	}
	int r = cg_write(&cg, format, fp);
	fclose(fp);
	free(pixels);
	return r;
}
