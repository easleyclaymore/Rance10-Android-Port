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

#include <limits.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "system4/hashtable.h"
#include "system4/string.h"
#include "system4/flat.h"

#include "gfx/gfx.h"
#include "sact.h"
#include "scene.h"
#include "sprite.h"
#include "xsystem4.h"

#include "parts_internal.h"

static struct {
	struct shader shader;
	GLint blend_rate;
	GLint bot_left;
	GLint top_right;
	GLint add_color;
	GLint multiply_color;
	GLint draw_filter;
	GLint use_clipper;
	GLint clipper_tex;
	GLint inv_clipper_transform;
} parts_shader;

static void set_draw_filter_blend_func(int draw_filter)
{
	switch (draw_filter) {
	case PARTS_DRAW_FILTER_ADDITIVE:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
		break;
	case PARTS_DRAW_FILTER_MULTIPLY:
		glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
		break;
	case PARTS_DRAW_FILTER_SCREEN:
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
		break;
	default:
		break;
	}
}

static void parts_render_texture(struct texture *texture, mat4 mw_transform, Rectangle *rect, float blend_rate, vec3 add_color, vec3 multiply_color, int draw_filter, int alpha_clipper)
{
	mat4 wv_transform = WV_TRANSFORM(config.view_width, config.view_height);

	struct gfx_render_job job = {
		.shader = &parts_shader.shader,
		.shape = GFX_RECTANGLE,
		.texture = texture->handle,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = texture,
	};

	gfx_prepare_job(&job);
	glUniform1f(parts_shader.blend_rate, blend_rate);
	glUniform2f(parts_shader.bot_left, rect->x, rect->y);
	glUniform2f(parts_shader.top_right, rect->x + rect->w, rect->y + rect->h);
	glUniform3fv(parts_shader.add_color, 1, add_color);
	glUniform3fv(parts_shader.multiply_color, 1, multiply_color);
	glUniform1i(parts_shader.draw_filter, draw_filter);

	struct parts *clipper = alpha_clipper ? parts_try_get(alpha_clipper) : NULL;
	if (clipper) {
		struct parts_common *c_common = &clipper->states[clipper->state].common;

		// 计算裁剪器世界矩阵的逆矩阵。
		mat4 clip_mw = GLM_MAT4_IDENTITY_INIT;
		glm_translate(clip_mw, (vec3) { clipper->global.pos.x, clipper->global.pos.y, 0 });
		glm_rotate_z(clip_mw, glm_rad(clipper->local.rotation.z), clip_mw);
		glm_scale(clip_mw, (vec3){ clipper->global.scale.x, clipper->global.scale.y, 1.0 });
		glm_translate(clip_mw, (vec3){ c_common->origin_offset.x, c_common->origin_offset.y, 0 });
		glm_scale(clip_mw, (vec3){ c_common->w, c_common->h, 1.0 });
		mat4 clip_inv;
		glm_mat4_inv(clip_mw, clip_inv);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, c_common->texture.handle);
		glActiveTexture(GL_TEXTURE0);

		glUniform1i(parts_shader.use_clipper, 1);
		glUniform1i(parts_shader.clipper_tex, 1);
		glUniformMatrix4fv(parts_shader.inv_clipper_transform, 1, GL_FALSE, clip_inv[0]);
	} else {
		glUniform1i(parts_shader.use_clipper, 0);
	}

	gfx_run_job(&job);
}

static struct texture uc_white;
static bool uc_white_ready;

/* [D141] 标题菜单里每个标签被画了两遍：一遍来自引擎内建 UC 控件
 * （uc_text，common.w/h 未设置），另一遍来自 EX 里独立的 PARTS_TEXT parts
 * （实测 common.w = 字数*16、common.h = 16，位置在按钮下方 100/42px）。
 * 后者是原版编辑器留下的预览文字，运行时应由内建控件接管，所以只要
 * 当前帧存在可见 UC 部件就跳过它。XSYS4_TITLE_UI_FIX=0 回退。 */
static bool uc_skip_dup_text(struct parts *parts, struct parts_text *t)
{
	static int fix = -1;
	if (fix < 0) {
		const char *e = getenv("XSYS4_TITLE_UI_FIX");
		fix = (e && *e == '0') ? 0 : 1;
	}
	if (!fix || parts->uc_object)
		return false;
	if (!parts->text_plain || !*parts->text_plain)
		return false;
	/* 只跳过"内容与某个可见内建控件标签完全相同"的那一层。
	 * 早先用 t->common.h == 16 当判据，结果把任务面板正文
	 * （同样是 16px）也吞掉了 —— 表现为"事件有框没字"。 */
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		if (p->uc_object && p->global.show && p->uc_label && *p->uc_label &&
		    strcmp(p->uc_label, parts->text_plain) == 0)
			return true;
	}
	return false;
}

/* [PROBE-GATED] XSYS4_TEXT_RENDER_DBG 的文字层探针。
 *
 * 原来用「全局前 80 条」限流：地图上 5 个文字部件每帧各打一条，80 条
 * 瞬间吃光，消息窗口（9000003xx）一条都打不出来 —— 「没有输出」被误读成
 * 「正文没有文字层」。改成按部件号分流（每号最多 3 条），并允许用
 * XSYS4_TEXT_RENDER_DBG=<部件号下限> 只盯动态号段；=1 等价于不过滤。
 * 顺带把 parts_render() 的三条准入条件一起打出来，一次区分「没到渲染」
 * 与「画了但不可见」。默认关闭，不改行为。 */
static void text_render_probe(struct parts *parts, struct parts_text *t)
{
	static struct { int no, n; } seen[64];
	static int rr;
	const char *e = getenv("XSYS4_TEXT_RENDER_DBG");
	if (!e || !*e || parts->uc_object || !t->nr_lines)
		return;
	int floor = atoi(e);
	if (floor < 0 || parts->no < floor)
		return;
	int slot = -1;
	for (int i = 0; i < 64; i++) {
		if (seen[i].no == parts->no) { slot = i; break; }
	}
	if (slot < 0) {
		slot = rr++ % 64;
		seen[slot].no = parts->no;
		seen[slot].n = 0;
	}
	if (seen[slot].n++ >= 3)
		return;
	NOTICE("[txtr] no=%d pos=(%d,%d) off=(%d,%d) area=(%d,%d) start=(%d,%d) "
	       "wh=%dx%d lines=%d "
	       "state=%d type=%d show=%d a=%d vis=%d mw=%d mwshow=%d uc=-",
	       parts->no, parts->global.pos.x, parts->global.pos.y,
	       t->common.origin_offset.x, t->common.origin_offset.y,
	       t->area_x, t->area_y,
	       parts->global.pos.x + t->common.origin_offset.x + t->area_x,
	       parts->global.pos.y + t->common.origin_offset.y + t->area_y,
	       t->common.w, t->common.h, t->nr_lines,
	       parts->state, parts->states[parts->state].type,
	       parts->global.show, (int)parts->global.alpha,
	       parts_is_visible(parts) ? 1 : 0,
	       parts->message_window ? 1 : 0,
	       parts_message_window_show ? 1 : 0);
}

static void parts_render_text(struct parts *parts, struct parts_text *t)
{
	text_render_probe(parts, t);
	/* [D301] 逐字符几何探针：正文「嗯……」看起来是两个字叠在一起，
	 * 需要区分「advance 太小」还是「同一段被画了两遍」。 */
	if (getenv("XSYS4_TEXT_CHAR_DBG")) {
		/* 每个部件只打一次（最多 8 个部件），用来确认「同一时刻有几个
		 * 文本件在同一位置画字」——正文那两个字明显是两段叠在一起。 */
		static int seen[8], nseen, total;
		if (t->nr_lines && parts->no >= 900000300 && total < 40) {
			int known = 0;
			for (int k = 0; k < nseen; k++)
				if (seen[k] == parts->no) { known = 1; break; }
			if (!known && nseen < 8) { seen[nseen++] = parts->no; }
			if (known) goto skip_char_dump;
			total++;
			for (unsigned i = 0; i < t->nr_lines && i < 4; i++) {
				struct parts_text_line *ln = &t->lines[i];
				NOTICE("[txtc] no=%d line=%u nr=%u h=%d space=%d",
				       parts->no, i, ln->nr_chars, ln->height, t->line_space);
				for (unsigned j = 0; j < ln->nr_chars && j < 8; j++) {
					struct parts_text_char *c = &ln->chars[j];
					NOTICE("[txtc]   j=%u ch=%02x%02x%02x%02x adv=%d t=%dx%d",
					       j, (unsigned char)c->ch[0], (unsigned char)c->ch[1],
					       (unsigned char)c->ch[2], (unsigned char)c->ch[3],
					       c->advance, c->t.w, c->t.h);
				}
			}
		}
		skip_char_dump: ;
	}
	if (uc_skip_dup_text(parts, t))
		return;
	int x = parts->global.pos.x + t->common.origin_offset.x + t->area_x;
	int y = parts->global.pos.y + t->common.origin_offset.y + t->area_y;
	for (int i = 0; i < t->nr_lines; i++) {
		struct parts_text_line *line = &t->lines[i];
		for (int j = 0; j < line->nr_chars; j++) {
			struct parts_text_char *ch = &line->chars[j];
			/* 用精灵路径贴字形，而不是 parts 着色器。
			 *
			 * parts 着色器采样字形纹理时一个像素都不出——把纹理换成已知
			 * 可用的 1×1 白纹理、矩阵换成和底板完全一样的写法也照样不出，
			 * 而同一张白纹理走 render_uc_rect() 就能画（见 TECH_DEBT
			 * [UC-TEXSHADER]，未定位）。精灵路径稳定，先走这条。 */
			Rectangle pr = RECT(x, y, ch->t.w, ch->t.h);
			gfx_render_texture(&ch->t, &pr);
			x += ch->advance;
		}
		x = parts->global.pos.x + t->common.origin_offset.x + t->area_x;
		y += line->height + t->line_space;
	}
}

/* 用户组件 (component type 15) 的绘制。
 *
 * EX 里声明的是“引擎内建控件”（数据 = ['オブジェクト名','TextButton',...]），
 * 游戏本身不提供图；按钮的底板与文字都得引擎自己画。目前只实现 TextButton：
 * 半透明深色底 + 1px 浅色边框 + 居中白字，hover 时底板和边框提亮。
 *
 * 底板用一张 1×1 白色纹理拉伸得到，避免引入美术资源依赖。 */

/* 把单位方框缩放铺到 (x,y,w,h)。颜色走 multiply_color——这张纹理是纯白的，
 * 乘上去才是最终颜色（此前只传了 alpha，红色底板画出来是白的）。 */
static void render_uc_rect(int x, int y, int w, int h, SDL_Color c)
{
	if (w <= 0 || h <= 0)
		return;
	mat4 mw = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw, (vec3){ x, y, 0 });
	glm_scale(mw, (vec3){ w, h, 1.0f });
	Rectangle r = { 0, 0, 1, 1 };
	vec3 add_color = { 0.0f, 0.0f, 0.0f };
	vec3 multiply_color = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
	parts_render_texture(&uc_white, mw, &r, c.a / 255.0f, add_color,
			multiply_color, 0, 0);
}

/* [D141] 官方说明书 game/Manual/img/rm02.jpg 里的启动界面按钮：
 *   「标题」样式 = 红色圆形按钮 + 金色圆环边框（直径约 180px）
 *   其余样式   = 金色圆角小按钮 + 浅金边框（196x40，与命中框同尺寸）
 * EX 只有 データ = ['オブジェクト名','TextButton','参数','标题,从头开始']，
 * 没有任何 CG 名 —— 按钮确实是引擎内建绘制，不存在"未加载的 CG"。
 * 这里程序化生成两张 RGBA 纹理并缓存，每帧每个按钮只贴一次。 */
#define UC_BTN_ROUND_D 180
#define UC_BTN_FLAT_W  196
#define UC_BTN_FLAT_H  40
static struct texture uc_btn_round, uc_btn_flat;
static bool uc_btn_ready;

static void uc_btn_build(void)
{
	if (uc_btn_ready)
		return;
	uc_btn_ready = true;

	int D = UC_BTN_ROUND_D, R = D / 2 - 2;
	uint8_t *px = calloc((size_t)D * D, 4);
	for (int y = 0; y < D; y++) {
		for (int x = 0; x < D; x++) {
			float dx = x - D / 2.0f + 0.5f, dy = y - D / 2.0f + 0.5f;
			float d = sqrtf(dx * dx + dy * dy);
			uint8_t *p = px + ((size_t)y * D + x) * 4;
			if (d > R)
				continue;                       /* 圆外透明 */
			if (d > R - 7) {                 /* 金色外环 */
				p[0] = 226; p[1] = 188; p[2] = 98; p[3] = 255;
			} else if (d > R - 10) {         /* 内圈暗边 */
				p[0] = 88; p[1] = 22; p[2] = 24; p[3] = 255;
			} else {                         /* 红色渐变 + 上方高光 */
				float t = (float)y / D;
				float hd = sqrtf(dx * dx + (dy + 20) * (dy + 20)) / (float)R;
				float hi = 1.0f - fminf(1.0f, hd);
				p[0] = (uint8_t)fminf(255, 116 + 62 * (1 - t) + 50 * hi);
				p[1] = (uint8_t)fminf(255,  20 + 20 * (1 - t) + 34 * hi);
				p[2] = (uint8_t)fminf(255,  24 + 16 * (1 - t) + 28 * hi);
				p[3] = 255;
			}
		}
	}
	gfx_init_texture_with_pixels(&uc_btn_round, D, D, px);
	free(px);

	int W = UC_BTN_FLAT_W, H = UC_BTN_FLAT_H, rad = 9;
	px = calloc((size_t)W * H, 4);
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			int cx = x < rad ? rad : (x >= W - rad ? W - 1 - rad : x);
			int cy = y < rad ? rad : (y >= H - rad ? H - 1 - rad : y);
			float d = sqrtf((float)((x-cx)*(x-cx) + (y-cy)*(y-cy)));
			uint8_t *p = px + ((size_t)y * W + x) * 4;
			if (d > rad)
				continue;
			float t = (float)y / H;
			if (x < 2 || y < 2 || x >= W-2 || y >= H-2 || d > rad-2) {
				p[0] = 228; p[1] = 198; p[2] = 124; p[3] = 255;
			} else {
				p[0] = (uint8_t)(152 - 46 * t);
				p[1] = (uint8_t)(120 - 38 * t);
				p[2] = (uint8_t)( 56 - 22 * t);
				p[3] = 255;
			}
		}
	}
	gfx_init_texture_with_pixels(&uc_btn_flat, W, H, px);
	free(px);
}

static void render_uc_button(int x, int y, int w, int h, bool round, bool hover)
{
	struct texture *tex = round ? &uc_btn_round : &uc_btn_flat;
	int dx = round ? (w - tex->w) / 2 : 0;
	int dy = round ? (h - tex->h) / 2 : 0;
	mat4 mw = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw, (vec3){ x + dx, y + dy, 0 });
	glm_scale(mw, (vec3){ tex->w, tex->h, 1.0f });
	/* parts_render_texture 的 rect 是纹理内的**像素**矩形（见
	 * parts_render_cg 用 surface_area / texture.w,h）。1x1 白底板能写
	 * {0,0,1,1} 只是因为它确实只有 1 像素；这里必须用整张纹理尺寸，
	 * 否则只采样左上角那个透明像素，按钮整个消失。 */
	Rectangle r = { 0, 0, tex->w, tex->h };
	vec3 add = { 0.0f, 0.0f, 0.0f };
	float k = hover ? 1.25f : 1.0f;
	vec3 mul = { k, k, k };
	parts_render_texture(tex, mw, &r, 1.0f, add, mul, 0, 0);
}

/* 用户组件的文字纹理必须在渲染之外创建。
 *
 * gfx_init_texture_rgba()/gfx_draw_glyph() 会切 FBO 并把 viewport 重置成窗口
 * 尺寸；在 parts_render_user_component() 里顺手建文字，等于在一帧画到一半时
 * 换渲染目标——实测结果是字形纹理全是空的（gfx_get_pixel 采样 0,0,0,0），
 * 后面的绘制也跟着错位。放到每帧的 update 阶段（parts_update_animation）
 * 来建，和游戏自己的 PARTS_TEXT 一个时机。 */
void parts_uc_text_prepare(struct parts *parts)
{
	if (!parts || !parts->uc_object || parts->uc_text_valid)
		return;
	if (!parts->uc_label || !*parts->uc_label)
		return;
	struct parts_common *common = &parts->states[parts->state].common;
	if (common->w <= 0 || common->h <= 0)
		return;
	float size = common->h * 0.55f;
	if (size < 10.0f)
		size = 10.0f;
	parts_uc_text_build(&parts->uc_text, parts->uc_label, size,
			(SDL_Color){ 236, 234, 226, 255 });
	parts->uc_text_valid = true;
}

static void parts_render_user_component(struct parts *parts)
{
	if (!uc_white_ready) {
		gfx_init_texture_rgba(&uc_white, 1, 1, (SDL_Color){255, 255, 255, 255});
		uc_white_ready = true;
	}

	struct parts_common *common = &parts->states[parts->state].common;
	int w = common->w;
	int h = common->h;
	if (getenv("XSYS4_UC_RENDER_DBG")) {
		static int n = 0;
		if (n < 24) {
			n++;
			NOTICE("[ucr] no=%d state=%d type=%d w=%d h=%d pos=(%d,%d) "
			       "alpha=%d z=%d clickable=%d pass=%d di=%d label=%s",
			       parts->no, parts->state,
			       parts->states[parts->state].type, w, h,
			       parts->global.pos.x, parts->global.pos.y,
			       parts->global.alpha, parts->global.z,
			       parts->clickable, parts->pass_cursor, parts->delegate_index,
			       parts->uc_label ? display_sjis0(parts->uc_label) : "(null)");
		}
	}
	if (w <= 0 || h <= 0)
		return;

	/* 文字纹理由 parts_uc_text_prepare() 在 update 阶段建好，这里只画。 */

	set_draw_filter_blend_func(parts->draw_filter);
	int x = parts->global.pos.x;
	int y = parts->global.pos.y;
	bool hover = parts->is_hovered;

	/* [D141] 按 EX 样式绘制官方外观，取代原先的蓝色占位矩形。
	 * 圆形按钮比命中框大，但圆心与框中心重合，文字仍居中在圆心。
	 * XSYS4_TITLE_UI_FIX=0 回退到旧的蓝色矩形（用于 A/B）。 */
	static int ui_fix = -1;
	if (ui_fix < 0) {
		const char *e = getenv("XSYS4_TITLE_UI_FIX");
		ui_fix = (e && *e == '0') ? 0 : 1;
	}
	if (ui_fix) {
		const char *style = parts_get_user_component_data(parts, "__style");
		bool round = style && strcmp(style, "round") == 0;
		uc_btn_build();
		render_uc_button(x, y, w, h, round, hover);
	} else if (hover) {
		render_uc_rect(x, y, w, h, (SDL_Color){ 96, 108, 140, 235 });
		render_uc_rect(x, y, w, 3, (SDL_Color){ 255, 246, 214, 255 });
		render_uc_rect(x, y + h - 3, w, 3, (SDL_Color){ 255, 246, 214, 255 });
		render_uc_rect(x, y, 3, h, (SDL_Color){ 255, 246, 214, 255 });
		render_uc_rect(x + w - 3, y, 3, h, (SDL_Color){ 255, 246, 214, 255 });
	} else {
		render_uc_rect(x, y, w, h, (SDL_Color){ 70, 80, 105, 255 });
		render_uc_rect(x, y, w, 3, (SDL_Color){ 208, 200, 180, 255 });
		render_uc_rect(x, y + h - 3, w, 3, (SDL_Color){ 208, 200, 180, 255 });
		render_uc_rect(x, y, 3, h, (SDL_Color){ 208, 200, 180, 255 });
		render_uc_rect(x + w - 3, y, 3, h, (SDL_Color){ 208, 200, 180, 255 });
	}

	if (parts->uc_text_valid) {
		int tw = parts_uc_text_width(&parts->uc_text);
		int th = parts_uc_text_height(&parts->uc_text);
		parts->uc_text.common.origin_offset.x = (w - tw) / 2;
		parts->uc_text.common.origin_offset.y = (h - th) / 2;
		parts_render_text(parts, &parts->uc_text);
	}

	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

/* [D224] 同帧 CG 绘制日志。每次绘制只写内存（无 I/O、不改帧率），
 * 仅在 XSYS4_SHOT_DIR + Alt+S 的那一刻由 PE_DebugCGRenderLog() 打印。
 * 用来回答 H037：黑屏帧里 parts_render_cg() 到底有没有被调用、调用时
 * alpha / multiply_color / scale 是什么。since_last=0 表示两次截图之间
 * 完全没有 CG 绘制（scene_render 没跑或 sprite_list 空），是本轮关键判据。 */
#define CGLOG_N 12
struct cglog_entry {
	int no;
	unsigned tex;
	int alpha, mul[3], add[3], w, h, tex_w, tex_h, pos_x, pos_y;
	float scale_x, scale_y;
};
static struct cglog_entry cglog[CGLOG_N];
static unsigned cglog_seq, cglog_at_dump;

static bool cglog_enabled(void)
{
	static int v = -1;
	if (v < 0)
		v = getenv("XSYS4_SHOT_PARTS_DUMP") ? 1 : 0;
	return v != 0;
}

void PE_DebugCGRenderLog(const char *tag)
{
	unsigned n = cglog_seq;
	NOTICE("[cglog] %s total=%u since_last=%u", tag, n, n - cglog_at_dump);
	cglog_at_dump = n;
	int lim = CGLOG_N < (int)n ? CGLOG_N : (int)n;
	for (int i = 0; i < lim; i++) {
		struct cglog_entry *e = &cglog[(n - 1 - i) % CGLOG_N];
		NOTICE("[cglog]  -%d no=%d tex=%u a=%d mul=(%d,%d,%d) add=(%d,%d,%d) "
		       "scale=(%.2f,%.2f) %dx%d texwh=%dx%d pos=(%d,%d)",
		       i, e->no, e->tex, e->alpha, e->mul[0], e->mul[1], e->mul[2],
		       e->add[0], e->add[1], e->add[2], e->scale_x, e->scale_y,
		       e->w, e->h, e->tex_w, e->tex_h, e->pos_x, e->pos_y);
	}
}

static void parts_render_cg(struct parts *parts, struct parts_common *common){
	set_draw_filter_blend_func(parts->draw_filter);

	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw_transform, (vec3) { parts->global.pos.x, parts->global.pos.y, 0 });
	// FIXME：3D 旋转需要真正的透视投影
	//glm_rotate_x(mw_transform, parts->rotation.x, mw_transform);
	//glm_rotate_y(mw_transform, parts->rotation.y, mw_transform);
	glm_rotate_z(mw_transform, glm_rad(parts->local.rotation.z), mw_transform);
	glm_scale(mw_transform, (vec3){ parts->global.scale.x, parts->global.scale.y, 1.0 });
	glm_translate(mw_transform, (vec3){ common->origin_offset.x, common->origin_offset.y, 0 });

	switch (parts->sprite_deform) {
	// 水平翻转
	case 1:
		glm_translate(mw_transform, (vec3){ common->w, 0.0f, 0.0f });
		glm_scale(mw_transform, (vec3){ -common->w, common->h, 1.0f });
		break;
	// 垂直翻转
	case 2:
		glm_translate(mw_transform, (vec3){ 0.0f, common->h, 0.0f });
		glm_scale(mw_transform, (vec3){ common->w, -common->h, 1.0f });
		break;
	default:
		WARNING("Invalid sprite_deform: %d", parts->sprite_deform);
	// 不做形变
	case 0:
		glm_scale(mw_transform, (vec3){ common->w, common->h, 1.0 });
		break;
	}

	Rectangle r = common->surface_area;
	if (!r.w && !r.h) {
		r = (Rectangle) { 0, 0, common->texture.w, common->texture.h };
	}

	/* [D292] 立绘只画出一部分（用户实测「希露只有头发出现」）的探针。
	 * 判据不是“画没画”，而是“画的区域和纹理能不能对上”：
	 *   - surface_area 只箦上一个维度（w=0,h>0）时，上面的 `!r.w && !r.h`
	 *     两个都为 0 才回退，于是 r={0,0,0,h} → 采样出一根竖条；
	 *   - surface_area 超出纹理（例如拿 1920x1080 去采样一张小图）→
	 *     超出的部分采样到钳制的边缘像素，表现为整块色带。
	 * 只对面积够大的部件打（立绘 1920x1080 量级），默认关。 */
	if (getenv("XSYS4_CG_AREA_DBG") && common->texture.handle &&
	    (common->texture.w >= 400 || common->texture.h >= 400)) {
		static int area_logged;
		/* [D296] 名额从 80 提到 2000：80 个会被地图的大 CG 先占满，
		 * 消息窗口底板（1920x330）根本轮不到打印 —— 上一轮据此
		 * 误判成「parts_render_cg 没被调用」。 */
		if (area_logged < 2000) {
			area_logged++;
			/* [D297] glIsTexture：handle 非 0 不等于纹理有效
			 * （号复用后旧对象可能已被 delete）。 */
			NOTICE("[cgarea] no=%d spz=%d/%d ctrl=%d mc=%d glis=%d deform=%d tex=%dx%d "
			       "w/h=%dx%d area=(%d,%d %dx%d) r=(%d,%d %dx%d)%s",
			       parts->no, parts->sp.z, parts->sp.z2,
			       parts->controller_no, parts_multi_controller,
			       common->texture.handle ?
					(int)glIsTexture(common->texture.handle) : -1,
			       parts->sprite_deform,
			       common->texture.w, common->texture.h,
			       common->w, common->h,
			       common->surface_area.x, common->surface_area.y,
			       common->surface_area.w, common->surface_area.h,
			       r.x, r.y, r.w, r.h,
			       (r.w <= 0 || r.h <= 0) ? "  <<< DEGENERATE" : "");
		}
	}

	vec3 add_color = {
		parts->global.add_color.r / 255.0f,
		parts->global.add_color.g / 255.0f,
		parts->global.add_color.b / 255.0f
	};
	vec3 multiply_color = {
		parts->global.multiply_color.r / 255.0f,
		parts->global.multiply_color.g / 255.0f,
		parts->global.multiply_color.b / 255.0f,
	};
	/* [D299] parts 着色器路径对某些纹理「提交了但不出像素」
	 * （`[UC-TEXSHADER]`；STATUS 契约 19 已记录文字必须走精灵路径）。
	 * 消息窗口底板 `シス／ＡＤＶ／事件／提示` 正好中招：s92–s99 七项全排除
	 * （纹理有效 glIsTexture=1、矩形 r=(0,0,1920,330)、vis=1、a=255、
	 * gz=4901 无遮挡、deform=0、被调 302 次）却看不见。
	 * 这里提供精灵路径兜底：仅在「无缩放无旋转」时启用（底板/立绘都是），
	 * 有变换的仍走原路径以免破坏既有画面。XSYS4_CG_SPRITE_PATH=1 开启。 */
	/* 只卡缩放/旋转：`draw_filter` 与 `alpha` 不该成为判据
	 * （底板 alpha=255 但 draw_filter 可能非 0，上一版因此整条没进这条路径）。 */
	/* 范围必须收紧：一刀切让**所有** CG 走精灵路径会把地图大 CG 画黑
	 * （`s102` 就是），因为原路径还负责 blend/draw_filter。
	 * 只处理「我们为消息窗口新建的底板」——特征：`di=-1`（不属于任何
	 * activity）+ 全宽 + 高 250..600（1920x330 / 1920x417 两种）。 */
	if (getenv("XSYS4_CG_SPRITE_PATH") &&
	    r.w >= 1900 && r.h >= 250 && r.h <= 600 &&
	    parts->global.scale.x == 1.0f && parts->global.scale.y == 1.0f &&
	    parts->local.rotation.z == 0.0f) {
		if (getenv("XSYS4_CG_SPRITE_DBG")) {
			static int n;
			/* 只打底板级（宽 >=1900）的大矩形：名额按到达顺序分配时，
			 * 一堆小 CG 会先把 40 条占满，目标部件永远打不出来。 */
			if (r.w >= 1900 && n++ < 500)
				NOTICE("[cgsp] no=%d di=%d pr=(%d,%d %dx%d) filt=%d a=%d",
				       parts->no, parts->delegate_index,
				       (int)(parts->global.pos.x + common->origin_offset.x),
				       (int)(parts->global.pos.y + common->origin_offset.y),
				       r.w, r.h, parts->draw_filter,
				       (int)parts->global.alpha);
		}
		Rectangle pr = {
			(int)(parts->global.pos.x + common->origin_offset.x),
			(int)(parts->global.pos.y + common->origin_offset.y),
			r.w, r.h,
		};
		gfx_render_texture(&common->texture, &pr);
		return;
	}
	parts_render_texture(&common->texture, mw_transform, &r, parts->global.alpha / 255.0,
			add_color, multiply_color, parts->draw_filter,
			parts->global.alpha_clipper_parts_no);

	/* [D224] 记录这次绘制的实际输入（只写内存、默认关闭，不打印）。 */
	if (cglog_enabled()) {
		struct cglog_entry *e = &cglog[cglog_seq % CGLOG_N];
		e->no = parts->no;
		e->tex = common->texture.handle;
		e->alpha = parts->global.alpha;
		e->mul[0] = parts->global.multiply_color.r;
		e->mul[1] = parts->global.multiply_color.g;
		e->mul[2] = parts->global.multiply_color.b;
		e->add[0] = parts->global.add_color.r;
		e->add[1] = parts->global.add_color.g;
		e->add[2] = parts->global.add_color.b;
		e->w = common->w; e->h = common->h;
		e->tex_w = common->texture.w; e->tex_h = common->texture.h;
		e->scale_x = parts->global.scale.x; e->scale_y = parts->global.scale.y;
		e->pos_x = parts->global.pos.x; e->pos_y = parts->global.pos.y;
		cglog_seq++;
	}

	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

struct emitter_render_ud {
	struct parts_flat *f;
	mat4 transform;       // root * base * layer for this emitter+birth_frame
	float parent_alpha;
	vec2 align;
	int alpha_clipper;
	int draw_filter;
	vec3 add_color;
	vec3 mul_color;
};

static void render_emitter_particle_cb(const struct flat_emitter_particle *p,
		void *ud)
{
	struct emitter_render_ud *d = ud;
	if (p->cg_lib_idx < 0 || (size_t)p->cg_lib_idx >= d->f->nr_libraries)
		return;
	Texture *tex = &d->f->textures[p->cg_lib_idx];
	if (!tex->handle)
		return;

	mat4 m = GLM_MAT4_IDENTITY_INIT;
	glm_translate(m, (vec3){ p->pos[0], p->pos[1], 0 });
	if (p->rot[2] != 0)
		glm_rotate_z(m, glm_rad(p->rot[2]), m);
	if (p->rot[0] != 0)
		glm_rotate_x(m, glm_rad(-p->rot[0]), m);
	if (p->rot[1] != 0)
		glm_rotate_y(m, glm_rad(p->rot[1]), m);
	glm_scale(m, (vec3){ p->scale[0], p->scale[1], 1.0f });
	glm_translate(m, (vec3){ d->align[0], d->align[1], 0 });

	// 先把粒子换到屏幕空间，再把输出的 Z 行清零，
	// 让 clip_z 永远停在近平面。
	glm_mat4_mul(d->transform, m, m);
	m[0][2] = m[1][2] = m[2][2] = m[3][2] = 0.0f;
	glm_scale(m, (vec3){ tex->w, tex->h, 1.0f });

	if (d->draw_filter != PARTS_DRAW_FILTER_NORMAL)
		set_draw_filter_blend_func(d->draw_filter);

	float blend_rate = d->parent_alpha * p->fade_alpha;
	Rectangle rect = { 0, 0, tex->w, tex->h };
	parts_render_texture(tex, m, &rect, blend_rate,
			d->add_color, d->mul_color, d->draw_filter, d->alpha_clipper);

	if (d->draw_filter != PARTS_DRAW_FILTER_NORMAL)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

static void render_flat_emitter(struct parts *parts, struct parts_flat *f,
		int emitter_lib_idx, int local, int frame_count,
		struct flat_key_data_graphic *keys,
		mat4 root, float parent_alpha,
		struct flat_key_stack *key_stack)
{
	vec2 align;
	if (!parts_flat_emitter_get_align_offset(f, emitter_lib_idx, align))
		return;

	struct flat_emitter *em = &f->flat->libraries[emitter_lib_idx].emitter;
	int active_frames = max(1, frame_count - em->particle_lifetime + 1);
	float parts_alpha = parts->global.alpha / 255.0f;

	// TODO：key_stack 反映的是当前渲染帧，但每个粒子
	// 应该看到的是它出生那一帧的祖先关键帧。
	// 现在这个发射器的所有粒子共用同一份祖先姿态，
	// 只有在兰斯9 的 猿玉／取得済みマーク.flat 上才看得出来。
	// 要修的话，得在 parts_flat_update 里按发射器记录
	// 每个出生帧的祖先关键帧；base 随之要按 birth_frame 算。
	mat4 base;
	parts_flat_build_emitter_base_matrix(em, key_stack, base);

	int min_birth = max(0, local - em->particle_lifetime + 1);
	int max_birth = min(active_frames - 1, local);
	for (int birth_frame = min_birth; birth_frame <= max_birth; birth_frame++) {
		int age = local - birth_frame;

		// 用出生帧的关键帧变换，让粒子在发射器移动时
		// 保持在自己的出生位置上。
		struct flat_key_data_graphic *birth_key = &keys[birth_frame];
		float layer_alpha = parent_alpha * birth_key->alpha / 255.0f;

		struct flat_emitter_layer_effective eff;
		parts_flat_emitter_resolve_layer(em, birth_key,
				parts_alpha, layer_alpha, &eff);

		// 用 birth_key 和发射器的 inherit_* 开关，
		// 构造「按出生帧」的图层矩阵。
		mat4 layer_m;
		parts_flat_build_layer_matrix(birth_key, eff.pos,
				eff.use_rotation, eff.use_scale, eff.use_origin,
				eff.reverse_lr, eff.reverse_tb,
				layer_m);

		struct emitter_render_ud ud = {
			.f = f,
			.parent_alpha = eff.alpha,
			.alpha_clipper = parts->global.alpha_clipper_parts_no,
			.draw_filter = eff.draw_filter,
		};
		glm_vec2_copy(align, ud.align);
		glm_vec3_copy(eff.add_color, ud.add_color);
		glm_vec3_copy(eff.mul_color, ud.mul_color);
		glm_mat4_mul(base, layer_m, ud.transform);
		glm_mat4_mul(root, ud.transform, ud.transform);
		parts_flat_foreach_emitter_particle(f, emitter_lib_idx, keys,
				birth_frame, age, frame_count,
				render_emitter_particle_cb, &ud);
	}
}

struct flat_draw_ctx {
	mat4 matrix;
	float alpha;
	vec3 add_color;
	vec3 mul_color;
	int draw_filter;
};

static void render_flat_layer(struct parts *parts, struct parts_flat *f,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines,
		struct flat_draw_ctx *ctx, mat4 root,
		struct flat_key_stack *key_stack);

static void render_flat_cg(struct parts *parts, Texture *tex,
		struct flat_key_data_graphic *key, struct flat_draw_ctx *ctx)
{
	if (!tex->handle)
		return;

	set_draw_filter_blend_func(ctx->draw_filter);

	Rectangle rect;
	if (key->area_width && key->area_height) {
		rect = (Rectangle){ key->area_x, key->area_y, key->area_width, key->area_height };
	} else {
		rect = (Rectangle){ 0, 0, tex->w, tex->h };
	}

	vec2 align;
	parts_flat_align_offset(key->align, rect.w, rect.h, align);

	mat4 render_m;
	glm_mat4_copy(ctx->matrix, render_m);
	// 把输出的 Z 行清零，让 clip_z 钉在近平面，
	// 避免 3D 旋转过的精灵被近/远平面裁掉。
	render_m[0][2] = render_m[1][2] = render_m[2][2] = render_m[3][2] = 0.0f;
	glm_translate(render_m, (vec3){ align[0], align[1], 0.0f });
	// area_x/area_y 只选纹理图集里的子矩形，
	// 不应该改变屏幕上的位置。这次平移正好抵消
	// 子矩形左上角本来会引入的偏移。
	glm_translate(render_m, (vec3){ -(float)key->area_x, -(float)key->area_y, 0.0f });
	glm_scale(render_m, (vec3){ tex->w, tex->h, 1.0f });

	parts_render_texture(tex, render_m, &rect, ctx->alpha, ctx->add_color, ctx->mul_color,
			ctx->draw_filter, parts->global.alpha_clipper_parts_no);

	if (ctx->draw_filter != PARTS_DRAW_FILTER_NORMAL)
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
}

static void render_flat_item(struct parts *parts, struct parts_flat *f,
		struct flat_layer_state *state, size_t tl_idx,
		struct flat_timeline *tl, int local,
		struct flat_key_data_graphic *key,
		struct flat_draw_ctx *parent, mat4 root,
		struct flat_key_stack *key_stack)
{
	int lib_idx = parts_flat_find_library(f->flat, tl->library_name->text);
	bool has_lib = lib_idx >= 0 && (size_t)lib_idx < f->flat->nr_libraries;
	struct flat_library *lib = has_lib ? &f->flat->libraries[lib_idx] : NULL;
	if (lib && lib->type == FLAT_LIB_EMITTER) {
		render_flat_emitter(parts, f, lib_idx, local, tl->frame_count,
				tl->graphic.keys,
				root, parent->alpha, key_stack);
		return;
	}

	mat4 layer_m;
	vec2 pos = { key->pos_x, key->pos_y };
	parts_flat_build_layer_matrix(key, pos,
			true, true, true,
			key->reverse_lr, key->reverse_tb,
			layer_m);

	struct flat_draw_ctx ctx;
	glm_mat4_mul(parent->matrix, layer_m, ctx.matrix);
	ctx.alpha = parent->alpha * key->alpha / 255.0f;
	vec3 key_add = { key->add_r / 255.0f, key->add_g / 255.0f, key->add_b / 255.0f };
	vec3 key_mul = { key->mul_r / 255.0f, key->mul_g / 255.0f, key->mul_b / 255.0f };
	glm_vec3_add(parent->add_color, key_add, ctx.add_color);
	glm_vec3_mul(parent->mul_color, key_mul, ctx.mul_color);
	ctx.draw_filter = key->draw_filter != PARTS_DRAW_FILTER_NORMAL
			? key->draw_filter : parent->draw_filter;

	/* [D200] LIBL 段为空、图形在 TALT 段的 QNT 图集里：回退到追加的那张
	 * 纹理。render_flat_cg 会用 key->area_x/y/w/h 从图集裁剪。 */
	if (!has_lib) {
		if (f->nr_libraries > f->flat->nr_libraries &&
		    f->flat->nr_talt_entries > 0) {
			Texture *tex = &f->textures[f->flat->nr_libraries];
			struct talt_entry *e = &f->flat->talt_entries[0];
			/* 第 N 条 GRAPHIC timeline 对应第 N 个 metadata 区域。
			 * 不裁剪就会把整张 QNT 图集画出来（编队界面曾因此盖上一大块深色）。 */
			int gidx = 0;
			for (size_t q = 0; q < tl_idx; q++)
				if (f->flat->timelines[q].type == FLAT_TIMELINE_GRAPHIC)
					gidx++;
			if (tex->handle && gidx < (int)e->nr_meta) {
				struct talt_metadata *m = &e->metadata[gidx];
				struct flat_key_data_graphic k = *key;
				k.area_x = m->unknown2;
				k.area_y = m->unknown3;
				k.area_width = m->unknown4;
				k.area_height = m->unknown5;
				render_flat_cg(parts, tex, &k, &ctx);
			}
		}
		return;
	}

	switch (lib->type) {
	case FLAT_LIB_CG:
		render_flat_cg(parts, &f->textures[lib_idx], key, &ctx);
		break;
	case FLAT_LIB_TIMELINE: {
		struct flat_layer_state *child = state->children[tl_idx];
		if (child && key_stack->count < FLAT_MAX_ANCESTOR_DEPTH) {
			key_stack->keys[key_stack->count++] = key;
			render_flat_layer(parts, f, child,
					lib->timeline.timelines, lib->timeline.nr_timelines,
					&ctx, root, key_stack);
			key_stack->count--;
		}
		break;
	}
	case FLAT_LIB_STOP_MOTION: {
		int cg_idx = parts_flat_stop_motion_get_cg_lib(f, lib_idx, local);
		if (cg_idx >= 0 && (size_t)cg_idx < f->nr_libraries)
			render_flat_cg(parts, &f->textures[cg_idx], key, &ctx);
		break;
	}
	case FLAT_LIB_EMITTER:
		// 不可能发生，上面已经处理
		break;
	case FLAT_LIB_MEMORY:
		// 尚未实现
		break;
	}
}

static void render_flat_layer(struct parts *parts, struct parts_flat *f,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines,
		struct flat_draw_ctx *ctx, mat4 root,
		struct flat_key_stack *key_stack)
{
	// 倒序排，才能得到正确的 z 序
	for (size_t i = nr_timelines; i-- > 0;) {
		struct flat_timeline *tl = &timelines[i];
		if (tl->type != FLAT_TIMELINE_GRAPHIC)
			continue;
		int local = state->current_frame - tl->begin_frame;
		if (local < 0 || local >= tl->frame_count)
			continue;

		/* flat v15 每帧存的是图形关键帧的列表
		 * （每个 SWF 图层一个）；更早的版本每帧只存一个关键帧，
		 * 放在 `count`/`keys` 里。兰斯10 的 UI 是 v15+，
		 * 所以只处理扁平数组会让每个 flat 部件 —— 整个网络
		 * 确认对话框和标题画面 —— 拿到空的关键帧列表，
		 * 于是什么都不画。 */
		if (tl->graphic.frames) {
			struct flat_key_frame_graphic *fr = &tl->graphic.frames[local];
			for (uint32_t k = 0; k < fr->count; k++)
				render_flat_item(parts, f, state, i, tl, local,
						 &fr->keys[k], ctx, root, key_stack);
			continue;
		}

		if (local >= (int)tl->graphic.count)
			continue;

		render_flat_item(parts, f, state, i, tl, local,
				 &tl->graphic.keys[local], ctx, root, key_stack);
	}
}

static void parts_render_flat(struct parts *parts, struct parts_flat *f)
{
	/* [D192] flat 部件「有部件、有 z、a=255，但一个像素不画」只有两个原因：
	 * flat 没加载成功（`f->flat == NULL`）或没有 root 状态。
	 * 任务地图的「確認／放棄」按钮就卡在这里，尺寸还是 0x0。
	 * 每个部件号只打一次：地图层每帧重复渲染同一批部件，全局计数会被
	 * 最先出现的那几个吃满（[PROBE-GATED]）。 */
	if (getenv("XSYS4_FLAT_DBG")) {
		/* [D199] 每号最多打 8 次：只打一次会把「容器滑入过程中的旧坐标」
		 * 当成稳态值。305 的 pos 走 0→585→-73→0，首次快照恰好落在 585，
		 * 于是 306 被记成 1920，误导了 D195–D199 好几轮。 */
		static int cnt[64], ids[64], nr;
		int slot = -1;
		for (int i = 0; i < nr; i++)
			if (ids[i] == parts->no) { slot = i; break; }
		if (slot < 0 && nr < 64) { slot = nr++; ids[slot] = parts->no; }
		if (slot >= 0 && cnt[slot] < 8) {
			cnt[slot]++;
			int cg = 0;
			for (size_t i = 0; i < f->nr_libraries; i++)
				if (f->textures[i].handle)
					cg++;
			NOTICE("[flat] render no=%d flat=%p root=%d a=%d pos=(%d,%d) "
			       "hdr=%dx%d wh=%dx%d cg=%zu/%zu fps=%d tl=%zu adv=%d stop=%d",
			       parts->no, (void*)f->flat, f->root_state,
			       parts->global.alpha, parts->global.pos.x, parts->global.pos.y,
			       f->flat ? f->flat->hdr.width : -1,
			       f->flat ? f->flat->hdr.height : -1,
			       f->common.w, f->common.h, (size_t)cg, f->nr_libraries,
			       f->flat ? f->flat->hdr.fps : -1,
			       f->flat ? f->flat->nr_timelines : (size_t)0,
			       f->needs_advance, f->stopped);
		}
	}
	if (!f->flat || !f->root_state)
		return;

	struct flat_draw_ctx ctx;
	glm_mat4_identity(ctx.matrix);
	glm_translate(ctx.matrix, (vec3){ parts->global.pos.x, parts->global.pos.y, 0 });
	glm_rotate_z(ctx.matrix, glm_rad(parts->local.rotation.z), ctx.matrix);
	glm_scale(ctx.matrix, (vec3){ parts->global.scale.x, parts->global.scale.y, 1.0f });
	ctx.alpha = parts->global.alpha / 255.0f;
	glm_vec3_zero(ctx.add_color);
	glm_vec3_one(ctx.mul_color);
	ctx.draw_filter = PARTS_DRAW_FILTER_NORMAL;

	struct flat_key_stack key_stack = { .count = 0 };
	render_flat_layer(parts, f, f->root_state,
			f->flat->timelines, f->flat->nr_timelines,
			&ctx, ctx.matrix, &key_stack);
}

static void parts_render_3dlayer(struct parts *parts, struct parts_3dlayer *l)
{
	if (l->sprite_no < 0)
		return;
	struct sact_sprite *sp = sact_try_get_sprite(l->sprite_no);
	if (!sp)
		return;
	struct texture *tex = sprite_get_texture(sp);
	if (!tex->handle)
		return;
	// 绑定精灵的纹理由 RE_set_viewport 重建，
	// 所以每帧都要刷新 parts_common 里那份视图。
	l->common.texture = *tex;
	l->common.w = tex->w;
	l->common.h = tex->h;
	parts_render_cg(parts, &l->common);
}

static void parts_render_flash_shape(struct parts *parts, struct parts_flash *f, struct parts_flash_object *obj, struct swf_tag_define_shape *tag)
{
	struct texture *src = ht_get_int(f->bitmaps, tag->fill_style.bitmap_id, NULL);
	if (!src)
		ERROR("undefined bitmap id %d", tag->fill_style.bitmap_id);

	mat4 mw_transform = GLM_MAT4_IDENTITY_INIT;
	glm_translate(mw_transform, (vec3) { parts->global.pos.x, parts->global.pos.y, 0 });
	glm_rotate_z(mw_transform, parts->local.rotation.z, mw_transform);
	glm_scale(mw_transform, (vec3){ parts->global.scale.x, parts->global.scale.y, 1.0 });
	glm_translate(mw_transform, (vec3){ f->common.origin_offset.x, f->common.origin_offset.y, 0 });

	glm_mul(mw_transform, obj->matrix, mw_transform);

	glm_translate(mw_transform, (vec3){
		twips_to_float(tag->bounds.x_min),
		twips_to_float(tag->bounds.y_min),
		0});
	glm_scale(mw_transform, (vec3){
		twips_to_float(tag->bounds.x_max - tag->bounds.x_min),
		twips_to_float(tag->bounds.y_max - tag->bounds.y_min),
		1.0});

	Rectangle r = { 0, 0, src->w, src->h };

	float blend_rate = (parts->global.alpha / 255.0f) * fixed16_to_float(obj->color_transform.mult_terms[3]);
	vec3 add_color = {
		(parts->global.add_color.r + obj->color_transform.add_terms[0]) / 255.0f,
		(parts->global.add_color.g + obj->color_transform.add_terms[1]) / 255.0f,
		(parts->global.add_color.b + obj->color_transform.add_terms[2]) / 255.0f
	};
	vec3 multiply_color = {
		(parts->global.multiply_color.r / 255.0f) * fixed16_to_float(obj->color_transform.mult_terms[0]),
		(parts->global.multiply_color.g / 255.0f) * fixed16_to_float(obj->color_transform.mult_terms[1]),
		(parts->global.multiply_color.b / 255.0f) * fixed16_to_float(obj->color_transform.mult_terms[2])
	};
	parts_render_texture(src, mw_transform, &r, blend_rate, add_color, multiply_color,
			0, parts->global.alpha_clipper_parts_no);
}

static void parts_render_flash_sprite(struct parts *parts, struct parts_flash *f, struct parts_flash_object *obj, struct swf_tag_define_sprite *tag)
{
	struct parts_flash_object *obj2 = ht_get_int(f->sprites, tag->sprite_id, NULL);
	if (!obj2)
		return;  // sprite has no visual elements.

	struct swf_tag *child = ht_get_int(f->dictionary, obj2->character_id, NULL);
	if (!child)
		ERROR("character %d is not defined", obj2->character_id);
	if (child->type != TAG_DEFINE_SHAPE)
		ERROR("unexpected child type %d", child->type);

	struct parts_flash_object transform;
	glm_mul(obj->matrix, obj2->matrix, transform.matrix);
	for (int i = 0; i < 4; i++) {
		transform.color_transform.add_terms[i] =
			obj->color_transform.add_terms[i] + obj2->color_transform.add_terms[i];
	}
	for (int i = 0; i < 4; i++) {
		transform.color_transform.mult_terms[i] =
			fixed16_mul(obj->color_transform.mult_terms[i], obj2->color_transform.mult_terms[i]);
	}
	parts_render_flash_shape(parts, f, &transform, (struct swf_tag_define_shape*)child);
}

static void parts_render_flash(struct parts *parts, struct parts_flash *f)
{
	struct parts_flash_object *obj;
	TAILQ_FOREACH(obj, &f->display_list, entry) {
		struct swf_tag *tag = ht_get_int(f->dictionary, obj->character_id, NULL);
		if (!tag) {
			WARNING("character %d is not defined", obj->character_id);
			continue;
		}
		// 设置混合模式
		switch (obj->blend_mode) {
		case PARTS_FLASH_BLEND_MULTIPLY:
			glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
			break;
		case PARTS_FLASH_BLEND_SCREEN:
			glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
			break;
		case PARTS_FLASH_BLEND_ADD:
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
			break;
		default:
			break;
		}
		// 渲染对象
		switch (tag->type) {
		case TAG_DEFINE_SHAPE:
			parts_render_flash_shape(parts, f, obj, (struct swf_tag_define_shape*)tag);
			break;
		case TAG_DEFINE_SPRITE:
			parts_render_flash_sprite(parts, f, obj, (struct swf_tag_define_sprite*)tag);
			break;
		default:
			ERROR("unknown tag %d", tag->type);
		}
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
	}
}

void parts_render(struct parts *parts)
{
	/* [D217 诊断] 消息窗口底板 CG 是 1920x330 / 1920x417 的部件。
	 * XSYS4_MSGWIN_FRAME_DBG=1 时打印它有没有被 parts_render 调用、
	 * origin_offset 与各项准入；用来区分「没到渲染」与「画了看不见」。 */
	if (getenv("XSYS4_MSGWIN_FRAME_DBG")) {
		static int fn;
		struct parts_state *fst = &parts->states[parts->state];
		if (fst->type == PARTS_CG && fst->common.w >= 1900 &&
		    fst->common.h >= 300 && fst->common.h <= 500 && fn++ < 12)
			NOTICE("[frame] no=%d tex=%u(%dx%d) pos=(%d,%d) off=(%d,%d) "
			       "%dx%d vis=%d mw=%d mwshow=%d link=%d a=%d",
			       parts->no, fst->common.texture.handle,
			       fst->common.texture.w, fst->common.texture.h,
			       parts->global.pos.x, parts->global.pos.y,
			       fst->common.origin_offset.x,
			       fst->common.origin_offset.y,
			       fst->common.w, fst->common.h,
			       parts_is_visible(parts) ? 1 : 0,
			       parts->message_window ? 1 : 0,
			       parts_message_window_show ? 1 : 0,
			       parts->linked_to, (int)parts->global.alpha);
	}
	/* [D128] 图层整体隐藏时（AIN 的 SceneStack@IsShow）该层所有部件都不画。 */
	if (!parts_is_visible(parts))
		return;
	if (parts->message_window && !parts_message_window_show)
		return;
	if (parts->linked_to >= 0) {
		struct parts *link_parts = parts_get(parts->linked_to);
		if (!link_parts->is_hovered)
			return;
	}

	// render
	struct parts_state *state = &parts->states[parts->state];
	if (parts->uc_object) {
		parts_render_user_component(parts);
		return;
	}
	switch (state->type) {
	case PARTS_UNINITIALIZED:
	case PARTS_RECT_DETECTION:
	case PARTS_LAYOUT_BOX:
		break;
	case PARTS_CG:
	case PARTS_ANIMATION:
	case PARTS_NUMERAL:
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
	case PARTS_CONSTRUCTION_PROCESS:
	case PARTS_MOVIE:
		/* [D187] 到没到渲染、画没画，是两件事。 */
		if (state->type == PARTS_CONSTRUCTION_PROCESS && getenv("XSYS4_CONSTRUCT_DBG")) {
			static int n;
			/* 只打卡片号段：前面的地图部件每帧重复，会把名额吃光。 */
			/* [D189] 亮度差异只能来自这些渲染输入之一，一次全打出来。 */
			if (parts->no >= 900000700 && parts->no < 900000800 && n++ < 40)
				NOTICE("[cproc] render parts=%d tex=%u a=%d show=%d pos=(%d,%d) %dx%d "
				       "scale=(%.2f,%.2f) mul=(%d,%d,%d) add=(%d,%d,%d) df=%d clip=%d sa=(%d,%d,%d,%d)",
				       parts->no, state->common.texture.handle,
				       (int)parts->global.alpha, parts->global.show,
				       parts->global.pos.x, parts->global.pos.y,
				       state->common.w, state->common.h,
				       parts->global.scale.x, parts->global.scale.y,
				       parts->global.multiply_color.r, parts->global.multiply_color.g,
				       parts->global.multiply_color.b,
				       parts->global.add_color.r, parts->global.add_color.g,
				       parts->global.add_color.b,
				       parts->draw_filter, parts->global.alpha_clipper_parts_no,
				       state->common.surface_area.x, state->common.surface_area.y,
				       state->common.surface_area.w, state->common.surface_area.h);
		}
		if (state->common.texture.handle)
			parts_render_cg(parts, &state->common);
		break;
	case PARTS_TEXT:
		parts_render_text(parts, &state->text);
		break;
	case PARTS_FLASH:
		parts_render_flash(parts, &state->flash);
		break;
	case PARTS_FLAT:
		parts_render_flat(parts, &state->flat);
		break;
	case PARTS_3DLAYER:
		parts_render_3dlayer(parts, &state->layer3d);
		break;
	}
}

void parts_render_family(struct parts *parts)
{
	parts_render(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_render_family(child);
	}
}

void parts_engine_render(possibly_unused struct sprite *_)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_render(parts);
	}
}

void parts_sprite_render(struct sprite *sp)
{
	parts_render((struct parts*)sp);
}

static bool pe_dirty = false;

void parts_render_update(void)
{
	/* [D244] 帧率探针。D92 的 hack（每帧把全场部件标脏）在 llvmpipe 下
	 * 只有 3~8 fps，而战斗演出 / instantTimer / flat 推进全按墙钟走，
	 * 低帧率会把「点 15 次才到 AP 球」这类现象误判成逻辑卡死。
	 * 先量化再决定要不要动渲染路径。 */
	if (getenv("XSYS4_FPS_DBG")) {
		static unsigned long long n;
		static time_t last;
		time_t now = time(NULL);
		if (now != last) {
			if (last)
				NOTICE("[fps] %llu", n);
			last = now;
			n = 0;
		}
		n++;
	}
	/* [HACK-POC D92] 每次更新都把每个部件精灵标脏，而不是
	 * 只在 pe_dirty 置位时标。parts_engine_dirty() 的调用点极少，
	 * 于是场景只渲染了那一次（启动时，精灵列表还是空的），
	 * gfx_swap() 再没呈现过部件 —— 窗口一直是黑的。
	 * 真正的修法：让每一处部件改动都去调 parts_engine_dirty()。 */
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		scene_sprite_dirty(&p->sp);
	}
	pe_dirty = false;
}

void parts_engine_dirty(void)
{
	pe_dirty = true;
}

void parts_engine_clean(void)
{
	pe_dirty = false;
}

void parts_dirty(possibly_unused struct parts *parts)
{
	pe_dirty = true;
}

void parts_render_init(void)
{
	gfx_load_shader(&parts_shader.shader, "shaders/parts.v.glsl", "shaders/parts.f.glsl");
	parts_shader.blend_rate = glGetUniformLocation(parts_shader.shader.program, "blend_rate");
	parts_shader.bot_left = glGetUniformLocation(parts_shader.shader.program, "bot_left");
	parts_shader.top_right = glGetUniformLocation(parts_shader.shader.program, "top_right");
	parts_shader.add_color = glGetUniformLocation(parts_shader.shader.program, "add_color");
	parts_shader.multiply_color = glGetUniformLocation(parts_shader.shader.program, "multiply_color");
	parts_shader.draw_filter = glGetUniformLocation(parts_shader.shader.program, "draw_filter");
	parts_shader.use_clipper = glGetUniformLocation(parts_shader.shader.program, "use_clipper");
	parts_shader.clipper_tex = glGetUniformLocation(parts_shader.shader.program, "clipper_tex");
	parts_shader.inv_clipper_transform = glGetUniformLocation(parts_shader.shader.program, "inv_clipper_transform");
}
