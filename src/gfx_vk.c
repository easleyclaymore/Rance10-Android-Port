/* [D517k] 3D 预览的 Vulkan 渲染路径（经 SDL_GPU，底层驱动 = Vulkan）。
 *
 * 为什么不直接写原生 Vulkan：本仓库的 2D 渲染是**裸 GL**（111 个 gfx API、
 * 932 行 video.c、离屏 framebuffer、87 处混合模式设置），全量迁移是
 * 3000+ 行新代码。先用 SDL_GPU 把「3D 预览」这条路径打通，验证 Vulkan
 * 能起、能画、能截图；2D 的迁移作为后续分期（见 docs/RENDER-BACKEND.md）。
 *
 * 与 GL 路径的关系：**互斥**。SDL 窗口只能是 OPENGL 或 VULKAN 之一，
 * 所以 `XSYS4_RENDER_BACKEND=vulkan` 时整帧由本文件负责，GL 的 gfx
 * 绘制全部跳过（画面只有预览 + 清屏色）。默认仍是 GL。
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "xsystem4.h"
#include "preview3d_vk.h"
#include "preview3d_vk_spv.h"

static struct {
	bool active;
	SDL_GPUDevice *dev;
	SDL_Window *win;
	SDL_GPUGraphicsPipeline *pipe;
	SDL_GPUBuffer *vbo;
	SDL_GPUTransferBuffer *xfer;
	SDL_GPUTextureFormat fmt;
	int vbo_cap;      /* 字节容量 */
} vk;

/* 每面一次 push uniform：world + view */
struct vk_ubo {
	mat4 world;
	mat4 view;
};

bool gfx_vk_active(void)
{
	return vk.active;
}

bool gfx_vk_init(SDL_Window *win)
{
	if (vk.active)
		return true;
	if (!win)
		return false;
	if (!SDL_IsMainThread())
		return false;

	/* 诊断：当前 video driver 是什么。SDL_GPU 的 vulkan bootstrap 需要
	 * video 驱动提供 Vulkan surface 扩展回调 —— 若落到 dummy/offscreen，
	 * 窗口能"创建"但没有扩展，bootstrap 直接返回 unsupported。 */
	NOTICE("[vk] current video driver = '%s'",
	       SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(null)");

	/* 诊断：SDL 能不能拿到 Vulkan 实例扩展（bootstrap 的第一道门）。
	 * 返回 0 = video 驱动没提供 surface 扩展回调 → bootstrap 必然失败。 */
	{
		unsigned int n = 0;
		if (SDL_Vulkan_GetInstanceExtensions(&n))
			NOTICE("[vk] instance extensions = %u", n);
		else
			NOTICE("[vk] SDL_Vulkan_GetInstanceExtensions 失败：%s",
			       SDL_GetError());
	}

	/* 诊断：先列出 SDL 认为可用的 GPU 驱动。 */
	if (getenv("XSYS4_VK_DBG")) {
		int nd = SDL_GetNumGPUDrivers();
		NOTICE("[vk] SDL 可用 GPU 驱动数=%d", nd);
		for (int i = 0; i < nd; i++)
			NOTICE("[vk]   driver[%d]=%s", i, SDL_GetGPUDriver(i));
	}

	/* [D521] 分层诊断：PrepareDriver 的四个早退条件逐个验证——
	 * ① props SHADERS_SPIRV（三参包装必设）② video 驱动
	 * Vulkan_CreateSurface 钩子（无公开查询 API，用 LoadLibrary 间接验）
	 * ③ SDL_Vulkan_LoadLibrary ④ INTERNAL_PrepareVulkan。 */
	if (!SDL_Vulkan_LoadLibrary(NULL))
		NOTICE("[vk] D521 SDL_Vulkan_LoadLibrary 失败：%s",
		       SDL_GetError());
	else
		NOTICE("[vk] D521 SDL_Vulkan_LoadLibrary OK");

	/* [D521] PrepareDriver 的早退②=video 驱动 Vulkan_CreateSurface==NULL。
	 * 用公开 API 对探测窗口直接建 surface——失败会带具体错误消息，
	 * 成功则证明钩子在（随后销毁 surface，不影响后续流程）。 */
	{
		Uint32 extn = 0;
		const char *const *extv = SDL_Vulkan_GetInstanceExtensions(&extn);
		NOTICE("[vk] D521 extensions=%u", extn);
		if (extv && extn > 0) {
			for (Uint32 qi = 0; qi < extn; qi++)
				NOTICE("[vk] D521   ext[%u]=%s", qi, extv[qi]);
		}
	}

	/* 只要 SPIR-V（Vulkan 用），不要 DXBC/MSL：本机是 Linux。 */
	/* [D521] 强制 SDL 全级别日志（VERBOSE 属性 + LogPriorities）——
	 * PrepareVulkan 内部失败路径有 SDL_LogWarn，此前从未出现在日志里，
	 * 用它分辨「Prepare 没跑」还是「跑了但日志被吞」。 */
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_PropertiesID vprops = SDL_CreateProperties();
	SDL_SetBooleanProperty(vprops, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
	SDL_SetBooleanProperty(vprops, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);
	SDL_SetBooleanProperty(vprops, SDL_PROP_GPU_DEVICE_CREATE_VERBOSE_BOOLEAN, true);
	SDL_GPUDevice *dev = SDL_CreateGPUDeviceWithProperties(vprops);
	SDL_DestroyProperties(vprops);
	if (!dev) {
		NOTICE("[vk] SDL_CreateGPUDeviceWithProperties 失败：%s",
		       SDL_GetError());
		/* 再点名要 vulkan，区分「没有驱动」与「自动选择失败」。 */
		dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, "vulkan");
		if (!dev) {
			NOTICE("[vk] 点名 vulkan 也失败：%s（放弃）", SDL_GetError());
			return false;
		}
	}
	const char *drv = SDL_GetGPUDeviceDriver(dev);
	if (!drv || SDL_strcasecmp(drv, "vulkan") != 0) {
		NOTICE("[vk] 拿到的驱动是 '%s' 而不是 vulkan，放弃（回退 GL）",
		       drv ? drv : "?");
		SDL_DestroyGPUDevice(dev);
		return false;
	}
	if (!SDL_ClaimWindowForGPUDevice(dev, win)) {
		NOTICE("[vk] ClaimWindow 失败：%s", SDL_GetError());
		SDL_DestroyGPUDevice(dev);
		return false;
	}

	vk.dev = dev;
	vk.win = win;
	vk.fmt = SDL_GetGPUSwapchainTextureFormat(dev, win);

	/* ---- 着色器 ---- */
	SDL_GPUShader *vs = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
		.code = preview3d_vk_vert_spv,
		.code_size = preview3d_vk_vert_spv_size,
		.entrypoint = "main",
		.format = SDL_GPU_SHADERFORMAT_SPIRV,
		.stage = SDL_GPU_SHADERSTAGE_VERTEX,
		.num_uniform_buffers = 1,
	});
	SDL_GPUShader *fs = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
		.code = preview3d_vk_frag_spv,
		.code_size = preview3d_vk_frag_spv_size,
		.entrypoint = "main",
		.format = SDL_GPU_SHADERFORMAT_SPIRV,
		.stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
		.num_uniform_buffers = 0,
	});
	if (!vs || !fs) {
		NOTICE("[vk] 着色器创建失败：%s", SDL_GetError());
		goto fail;
	}

	/* ---- 管线 ---- */
	SDL_GPUVertexBufferDescription vbd = {
		.slot = 0,
		.pitch = sizeof(float) * 6, /* pos(3) + color(3) */
		.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
	};
	SDL_GPUVertexAttribute attrs[2] = {
		{ .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
		  .offset = 0 },
		{ .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
		  .offset = sizeof(float) * 3 },
	};
	SDL_GPUColorTargetDescription ct = {
		.format = vk.fmt,
		.blend_state = {
			.enable_blend = true,
			.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.color_blend_op = SDL_GPU_BLENDOP_ADD,
			.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
			.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.alpha_blend_op = SDL_GPU_BLENDOP_ADD,
			.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
					    SDL_GPU_COLORCOMPONENT_G |
					    SDL_GPU_COLORCOMPONENT_B |
					    SDL_GPU_COLORCOMPONENT_A,
		},
	};
	SDL_GPUGraphicsPipelineCreateInfo pci = {
		.vertex_shader = vs,
		.fragment_shader = fs,
		.vertex_input_state = {
			.vertex_buffer_descriptions = &vbd,
			.num_vertex_buffers = 1,
			.vertex_attributes = attrs,
			.num_vertex_attributes = 2,
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP,
		.rasterizer_state = {
			.fill_mode = SDL_GPU_FILLMODE_FILL,
			.cull_mode = SDL_GPU_CULLMODE_NONE,
			.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
		},
		.target_info = {
			.color_target_descriptions = &ct,
			.num_color_targets = 1,
		},
	};
	vk.pipe = SDL_CreateGPUGraphicsPipeline(dev, &pci);
	SDL_ReleaseGPUShader(dev, vs);
	SDL_ReleaseGPUShader(dev, fs);
	if (!vk.pipe) {
		NOTICE("[vk] 管线创建失败：%s", SDL_GetError());
		goto fail;
	}

	/* 顶点缓冲：6 面 × 4 顶点 × 6 float，一次建够。 */
	vk.vbo_cap = 6 * 4 * 6 * (int)sizeof(float);
	vk.vbo = SDL_CreateGPUBuffer(dev, &(SDL_GPUBufferCreateInfo){
		.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
		.size = (Uint32)vk.vbo_cap,
	});
	vk.xfer = SDL_CreateGPUTransferBuffer(dev, &(SDL_GPUTransferBufferCreateInfo){
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = (Uint32)vk.vbo_cap,
	});
	if (!vk.vbo || !vk.xfer) {
		NOTICE("[vk] 顶点缓冲创建失败：%s", SDL_GetError());
		goto fail;
	}

	vk.active = true;
	NOTICE("[vk] Vulkan 路径就绪：driver=%s swapchain_fmt=%d", drv, (int)vk.fmt);
	return true;

fail:
	if (vk.xfer) { SDL_ReleaseGPUTransferBuffer(dev, vk.xfer); vk.xfer = NULL; }
	if (vk.vbo) { SDL_ReleaseGPUBuffer(dev, vk.vbo); vk.vbo = NULL; }
	if (vk.pipe) { SDL_ReleaseGPUGraphicsPipeline(dev, vk.pipe); vk.pipe = NULL; }
	SDL_ReleaseWindowFromGPUDevice(dev, win);
	SDL_DestroyGPUDevice(dev);
	vk.dev = NULL;
	return false;
}

void gfx_vk_fini(void)
{
	if (!vk.active)
		return;
	if (vk.xfer)
		SDL_ReleaseGPUTransferBuffer(vk.dev, vk.xfer);
	if (vk.vbo)
		SDL_ReleaseGPUBuffer(vk.dev, vk.vbo);
	if (vk.pipe)
		SDL_ReleaseGPUGraphicsPipeline(vk.dev, vk.pipe);
	SDL_ReleaseWindowFromGPUDevice(vk.dev, vk.win);
	SDL_DestroyGPUDevice(vk.dev);
	memset(&vk, 0, sizeof(vk));
}

/* 只清屏（3D 页未激活时的 Vulkan 帧，避免黑屏无反馈）。 */
void gfx_vk_clear_screen(void)
{
	if (!vk.active)
		return;
	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(vk.dev);
	if (!cmd)
		return;
	SDL_GPUTexture *swap = NULL;
	Uint32 sw = 0, sh = 0;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, vk.win, &swap, &sw, &sh) || !swap) {
		SDL_SubmitGPUCommandBuffer(cmd);
		return;
	}
	SDL_GPUColorTargetInfo ct = {
		.texture = swap,
		.clear_color = { 0.06f, 0.06f, 0.08f, 1.0f },
		.load_op = SDL_GPU_LOADOP_CLEAR,
		.store_op = SDL_GPU_STOREOP_STORE,
	};
	SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
	SDL_EndGPURenderPass(rp);
	SDL_SubmitGPUCommandBuffer(cmd);
}

/* 画 N 个面：每面 4 顶点（单位方形）+ 自己的 world 矩阵 + 顶点色。
 * 坐标约定与 GL 路径一致：world 把 (0,0)-(1,1) 映射到该面。 */
bool gfx_vk_draw_faces(const float (*world)[16], const float (*colors)[3],
		       int n, const float view[16])
{
	if (!vk.active || n <= 0 || n > 6)
		return false;

	/* 上传顶点：每面 4 个单位方形顶点 + 该面颜色。 */
	float *map = SDL_MapGPUTransferBuffer(vk.dev, vk.xfer, true);
	if (!map)
		return false;
	const float quad[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
	for (int f = 0; f < n; f++) {
		for (int v = 0; v < 4; v++) {
			float *d = map + (f * 4 + v) * 6;
			d[0] = quad[v][0];
			d[1] = quad[v][1];
			d[2] = 0.0f;
			d[3] = colors[f][0];
			d[4] = colors[f][1];
			d[5] = colors[f][2];
		}
	}
	SDL_UnmapGPUTransferBuffer(vk.dev, vk.xfer);

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(vk.dev);
	if (!cmd)
		return false;

	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){
			.transfer_buffer = vk.xfer, .offset = 0 },
		&(SDL_GPUBufferRegion){ .buffer = vk.vbo, .offset = 0,
			.size = (Uint32)(n * 4 * 6 * sizeof(float)) }, true);
	SDL_EndGPUCopyPass(cp);

	SDL_GPUTexture *swap = NULL;
	Uint32 sw = 0, sh = 0;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, vk.win, &swap, &sw, &sh) || !swap) {
		SDL_SubmitGPUCommandBuffer(cmd);
		return false;
	}

	/* 清屏色 = 预览背景（与 GL 路径的 bg 一致）。 */
	SDL_GPUColorTargetInfo ct = {
		.texture = swap,
		.clear_color = { 20 / 255.0f, 22 / 255.0f, 28 / 255.0f, 1.0f },
		.load_op = SDL_GPU_LOADOP_CLEAR,
		.store_op = SDL_GPU_STOREOP_STORE,
	};
	SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
	SDL_BindGPUGraphicsPipeline(rp, vk.pipe);
	SDL_BindGPUVertexBuffers(rp, 0, &(SDL_GPUBufferBinding){
		.buffer = vk.vbo, .offset = 0 }, 1);

	for (int f = 0; f < n; f++) {
		struct vk_ubo ubo;
		memcpy(ubo.world, world[f], sizeof(ubo.world));
		memcpy(ubo.view, view, sizeof(ubo.view));
		SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(ubo));
		SDL_DrawGPUPrimitives(rp, 4, 1, (Uint32)(f * 4), 0);
	}

	SDL_EndGPURenderPass(rp);
	SDL_SubmitGPUCommandBuffer(cmd);
	return true;
}
