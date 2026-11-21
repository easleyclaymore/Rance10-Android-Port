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

#include <assert.h>

#include "system4.h"
#include "system4/cg.h"
#include "system4/flat.h"
#include "system4/hashtable.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "audio.h"
#include "vm/page.h"
#include "xsystem4.h"
#include "gfx/gfx.h"
#include "sact.h"
#include "sprite.h"
#include "parts.h"
#include "parts_internal.h"
#include "reign.h"

struct parts_list parts_list = TAILQ_HEAD_INITIALIZER(parts_list);
static struct parts_list dirty_list = TAILQ_HEAD_INITIALIZER(dirty_list);
static struct hash_table *parts_table = NULL;

#define PARTS_PARAMS_INITIALIZER(Z) (struct parts_params) { \
	.z = Z, \
	.pos = { 0, 0 }, \
	.show = true, \
	.alpha = 255, \
	.scale = { 1.0f, 1.0f }, \
	.rotation = { 0.0f, 0.0f, 0.0f }, \
	.add_color = { 0, 0, 0, 0 }, \
	.multiply_color = { 255, 255, 255, 255 }, \
	.alpha_clipper_parts_no = 0 \
}

static struct parts_params root_params = PARTS_PARAMS_INITIALIZER(0);

struct parts_controller_stack ctrl_stack;
bool parts_multi_controller;

static void ctrl_stack_init(void);

static void parts_init(struct parts *parts)
{
	parts->sp.z = 1;
	parts->sp.has_pixel = true;
	parts->sp.has_alpha = true;
	parts->sp.render = parts_sprite_render;
	/* [HACK-POC D93] 部件是由 parts_sprite_render() 动态绘制的，
	 * 所以没有静态像素缓冲 —— 但 scene_sprite_dirty() 只在
	 * has_pixel 置位时才调 scene_register_sprite()。没有这个标志，
	 * 每个部件精灵都进不了场景，scene_render() 于是只清屏（黑窗口）。
	 * 真正的修法：让场景接受「只渲染型」精灵，
	 * 或者在 parts_dirty() 里置 has_pixel 时一并置脏标志。 */
	parts->sp.has_pixel = true;
	parts->sp.to_json = parts_sprite_to_json;
	parts->local = PARTS_PARAMS_INITIALIZER(1);
	parts->global = PARTS_PARAMS_INITIALIZER(1);
	parts->delegate_index = -1;
	parts->want_save = true;
	parts->want_save_back_scene = false;
	/* [D311] 见 parts_internal.h：创建序号，永不复用。 */
	{ static unsigned next_serial = 1; parts->serial = next_serial++; }
	parts->on_cursor_sound = -1;
	parts->on_click_sound = -1;
	parts->origin_mode = 1;
	parts->pending_parent = -1;
	parts->linked_to = -1;
	parts->linked_from = -1;
	TAILQ_INIT(&parts->children);
	TAILQ_INIT(&parts->motion);
}

static struct parts *parts_alloc(void)
{
	struct parts *parts = xcalloc(1, sizeof(struct parts));
	parts_init(parts);
	return parts;
}

void parts_component_dirty(struct parts *parts)
{
	if (parts->dirty)
		return;
	parts->dirty = true;
	TAILQ_INSERT_TAIL(&dirty_list, parts, dirty_list_entry);
}

static void dirty_list_remove(struct parts *parts)
{
	if (parts->dirty)
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
}

static int parts_get_sprite_z(struct parts *parts)
{
	if (!parts_multi_controller)
		return parts->global.z;
	// 系统覆盖层的控制器排在栈内所有控制器之上。
	return parts->controller_no;
}

static int parts_get_sprite_z2(struct parts *parts)
{
	if (!parts_multi_controller)
		return 0;
	return parts->global.z;
}

static void parts_list_insert(struct parts *parts)
{
	int z = parts_get_sprite_z(parts);
	int z2 = parts_get_sprite_z2(parts);
	parts->sp.z = z;
	parts->sp.z2 = z2;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		int pz = parts_get_sprite_z(p);
		int pz2 = parts_get_sprite_z2(p);
		if (pz > z || (pz == z && pz2 > z2)) {
			TAILQ_INSERT_BEFORE(p, parts, parts_list_entry);
			goto done;
		}
	}
	TAILQ_INSERT_TAIL(&parts_list, parts, parts_list_entry);
done:
	parts_engine_dirty();
	scene_register_sprite(&parts->sp);
}

static void parts_list_remove(struct parts *parts)
{
	TAILQ_REMOVE(&parts_list, parts, parts_list_entry);
	scene_unregister_sprite(&parts->sp);
}

void parts_list_resort(struct parts *parts)
{
	// TODO：这里可以优化
	parts_list_remove(parts);
	parts_list_insert(parts);
}

struct parts *parts_try_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;
	return NULL;
}

/* [D174] 定义在下方（parts_exists 之前），parts_get 里要先用。 */
void parts_cb_drop(int parts_no);

static void ctrl_pending_add(int parts_no);

/* [D335] 「部件还没建就设 delegate_index」的补绑定表。
 *
 * 实测（`dg4`，回想对话框「過後查看」）：
 *   `[dgidx] get parts=900000574 -> -1`      ← 此刻部件不存在
 *   `[dgidx] set parts=900000574 di=404`     ← 但要绑委托
 * `SetDelegateIndex` 用的是 `parts_get()`，它**按需创建**：于是 AIN 这一句
 * 造出一个空壳部件，`di` 写在空壳上；随后活动装载/重建把空壳换掉，
 * 真正的 574 拿到的是默认 `di`，点击派发自然打空。
 *
 * 处置与 `pending_parent` 同思路：部件不在就先记账，等它真的建出来再补。
 * 容量 64 足够（同一时刻待绑的按钮不会更多）；满了就丢最旧的并留痕，
 * 不静默溢出。
 */
#define PE_PENDING_DI_MAX 64
static struct {
	int no;
	int di;
} pe_pending_di[PE_PENDING_DI_MAX];
static int pe_pending_di_n;

static void pe_pending_di_remember(int parts_no, int di)
{
	for (int i = 0; i < pe_pending_di_n; i++) {
		if (pe_pending_di[i].no == parts_no) {
			pe_pending_di[i].di = di;
			return;
		}
	}
	if (pe_pending_di_n == PE_PENDING_DI_MAX) {
		memmove(&pe_pending_di[0], &pe_pending_di[1],
			sizeof(pe_pending_di[0]) * (PE_PENDING_DI_MAX - 1));
		pe_pending_di_n--;
		if (getenv("XSYS4_DGIDX_DBG"))
			NOTICE("[dgidx] pending 表满，丢弃最旧一条");
	}
	pe_pending_di[pe_pending_di_n].no = parts_no;
	pe_pending_di[pe_pending_di_n].di = di;
	pe_pending_di_n++;
}

/* 部件刚建出来时调用：把欠它的 delegate_index 补上。 */
static void pe_pending_di_apply(struct parts *parts)
{
	for (int i = 0; i < pe_pending_di_n; i++) {
		if (pe_pending_di[i].no != parts->no)
			continue;
		parts->delegate_index = pe_pending_di[i].di;
		if (getenv("XSYS4_DGIDX_DBG"))
			NOTICE("[dgidx] 补绑定 parts=%d di=%d", parts->no,
			       parts->delegate_index);
		pe_pending_di[i] = pe_pending_di[--pe_pending_di_n];
		return;
	}
}

struct parts *parts_get(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (slot->value)
		return slot->value;

	struct parts *parts = parts_alloc();
	parts->no = parts_no;
	parts->controller_no = ctrl_stack.active;
	/* [D320] 部件生命周期探针：`XSYS4_PART_LIFE=1` 时打印 900000560..900000600
	 * 这一段（回想对话框）的**创建**。起因：AIN 给按钮 574 绑了
	 * `SetDelegateIndex(574, 404)`，之后 `GetDelegateIndex(574)` 却读回 -1 ——
	 * 只有「部件被释放后重建」能造成这种结果，而重建会丢掉全部绑定。 */
	if (getenv("XSYS4_PART_LIFE") && parts_no >= 900000560 && parts_no <= 900000600)
		NOTICE("[plife] create no=%d ctrl=%d", parts_no, parts->controller_no);
	pe_pending_di_apply(parts);
	/* [D289] Load 期间建的部件先记下来，AddLayer 时迁到新层。 */
	ctrl_pending_add(parts_no);
	slot->value = parts;
	parts_list_insert(parts);
	/* [D174] 部件号会被回收复用。复选框状态存在旁路表里（见下方实现），
	 * 新建部件时必须清掉该号的旧记录，否则新菜单的复选框会继承上一次
	 * 的勾选状态。 */
	parts_cb_drop(parts_no);
	/* [D128] 部件分布变了，覆盖推断要跟着更新（不放在每帧钩子上：
	 * parts_render_update() 并不是所有等待路径都会走到）。 */
	return parts;
}


/* [D174] 复选框状态：勾选 / 可用。
 *
 * 为什么不加进 `struct parts`：
 * `save.c` 按**顺序** `iarray_write` / `iarray_read` 序列化部件字段
 * （`clickable`、`draggable` 都在其中），中间插字段会让存档格式错位 ——
 * 为一个 UI 勾选状态付这个代价不值得。而且这个状态本来也不需要持久化：
 * 菜单重建时 AIN 会把勾选状态重新设一遍。
 *
 * 所以用固定容量的旁路表。容量 64 远大于任何一屏的复选框数量；满了按
 * 环形覆盖。部件创建时（parts_get）清掉该号旧记录，避免号复用后串味。
 *
 * 这一族原来全是空桩或 TODO：
 *   SetCheckBoxSize / SetCheckBoxColor / SetCheckBoxEnable -> 空桩
 *   CheckBoxChecked / IsCheckBoxChecked                   -> TODO
 * 于是 `CommonMenu@SetCheckBoxState` 一进来就撞 Unimplemented。 */
#define PARTS_CB_MAX 64
static struct parts_cb_state {
	int no;            /* 0 = 空槽（部件号从 1 开始） */
	bool checked;
	bool enabled;
} parts_cb_table[PARTS_CB_MAX];
static unsigned parts_cb_cursor;

static struct parts_cb_state *parts_cb_slot(int parts_no)
{
	for (unsigned i = 0; i < PARTS_CB_MAX; i++)
		if (parts_cb_table[i].no == parts_no)
			return &parts_cb_table[i];
	struct parts_cb_state *e = &parts_cb_table[parts_cb_cursor++ % PARTS_CB_MAX];
	e->no = parts_no;
	e->checked = false;
	e->enabled = false;
	return e;
}

/* 只读查询：不创建条目，查不到就是"默认未勾选/未启用"。 */
static bool parts_cb_lookup(int parts_no, bool *checked, bool *enabled)
{
	for (unsigned i = 0; i < PARTS_CB_MAX; i++)
		if (parts_cb_table[i].no == parts_no) {
			*checked = parts_cb_table[i].checked;
			*enabled = parts_cb_table[i].enabled;
			return true;
		}
	return false;
}

void parts_cb_drop(int parts_no)
{
	for (unsigned i = 0; i < PARTS_CB_MAX; i++)
		if (parts_cb_table[i].no == parts_no) {
			parts_cb_table[i].no = 0;
			parts_cb_table[i].checked = false;
			parts_cb_table[i].enabled = false;
		}
}

/* [D396] 勾选状态迁到 `parts_widget`（跟着部件生命周期走）。
 *
 * D174 当初用旁挂表，理由是「往 `struct parts` 中间插字段会让存档格式错位」。
 * 现在状态挂在 `parts->widget`（**末尾的指针**，不参与 `parts/save.c` 的顺序
 * 序列化），既不动存档格式，也不用再在 `parts_get` 里手动清旧记录。
 *
 * 旧表保留为空壳不再写入 —— 读不到就走 widget，行为与之前一致。 */
void PE_CheckBoxChecked(int parts_no, bool check)
{
	struct parts_widget *w = PE_widget(parts_no);
	if (w)
		w->checked = check;
}

bool PE_IsCheckBoxChecked(int parts_no)
{
	struct parts_widget *w = PE_widget_peek(parts_no);
	return w ? w->checked : false;
}

void PE_SetCheckBoxEnable(int parts_no, bool enable)
{
	struct parts_widget *w = PE_widget(parts_no);
	if (w)
		w->enabled = enable;
}

bool PE_IsCheckBoxEnable(int parts_no)
{
	struct parts_widget *w = PE_widget_peek(parts_no);
	return w ? w->enabled : false;
}

bool parts_exists(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

static void parts_state_free(struct parts_state *state)
{
	switch (state->type) {
	case PARTS_UNINITIALIZED:
	case PARTS_RECT_DETECTION:
	case PARTS_LAYOUT_BOX:
		break;
	case PARTS_CG:
		gfx_delete_texture(&state->common.texture);
		if (state->cg.name)
			free_string(state->cg.name);
		break;
	case PARTS_TEXT:
		parts_text_free(&state->text);
		break;
	case PARTS_ANIMATION:
		for (unsigned i = 0; i < state->anim.nr_frames; i++) {
			gfx_delete_texture(&state->anim.frames[i]);
		}
		free(state->anim.frames);
		if (state->anim.cg_name)
			free_string(state->anim.cg_name);
		break;
	case PARTS_NUMERAL:
		gfx_delete_texture(&state->common.texture);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		gfx_delete_texture(&state->common.texture);
		gfx_delete_texture(&state->gauge.cg);
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		gfx_delete_texture(&state->common.texture);
		parts_clear_construction_process(&state->cproc);
		break;
	case PARTS_FLASH:
		parts_flash_free(&state->flash);
		break;
	case PARTS_FLAT:
		parts_flat_free(&state->flat);
		break;
	case PARTS_MOVIE:
		// 纹理的所有权归 SACT 精灵。
		state->common.texture.handle = 0;
		if (state->movie.sprite_no >= 0)
			sact_SP_Delete(state->movie.sprite_no);
		break;
	case PARTS_3DLAYER:
		state->common.texture.handle = 0;
		if (state->layer3d.plugin >= 0)
			ReignEngine_ReleasePlugin(state->layer3d.plugin);
		if (state->layer3d.sprite_no >= 0)
			sact_SP_Delete(state->layer3d.sprite_no);
		break;
	}
	memset(state, 0, sizeof(struct parts_state));
}

static struct text_style default_text_style = {
	.face = FONT_GOTHIC,
	.size = 16.0f,
	.bold_width = 0.0f,
	.weight = FW_NORMAL,
	.edge_left = 0.0f,
	.edge_up = 0.0f,
	.edge_right = 0.0f,
	.edge_down = 0.0f,
	.color = { .r = 255, .g = 255, .b = 255, .a = 255 },
	.edge_color = { .r = 0, .g = 0, .b = 0, .a = 255 },
	.scale_x = 1.0f,
	.space_scale_x = 1.0f,
	.font_spacing = 0.0f,
	.font_size = NULL
};

struct text_style *parts_default_text_style(void)
{
	return &default_text_style;
}

void parts_state_reset(struct parts_state *state, enum parts_type type)
{
	parts_state_free(state);
	state->type = type;
	state->component_type = -1;
	switch (type) {
	case PARTS_TEXT:
		state->text.ts = default_text_style;
		break;
	case PARTS_NUMERAL:
		state->num.length = 1;
		state->num.font_no = -1;
		break;
	case PARTS_CONSTRUCTION_PROCESS:
		TAILQ_INIT(&state->cproc.ops);
		break;
	case PARTS_HGAUGE:
	case PARTS_VGAUGE:
		state->gauge.cg_no = -1;
		state->gauge.rate = 0.0f;
		break;
	case PARTS_3DLAYER:
		state->layer3d.plugin = -1;
		state->layer3d.sprite_no = -1;
		break;
	case PARTS_UNINITIALIZED:
	case PARTS_CG:
	case PARTS_ANIMATION:
	case PARTS_FLASH:
	case PARTS_FLAT:
	case PARTS_RECT_DETECTION:
		break;
	case PARTS_MOVIE:
		state->movie.sprite_no = -1;
		break;
	case PARTS_LAYOUT_BOX:
		state->layout_box.layout_type = PARTS_LAYOUT_VERTICAL;
		state->layout_box.align = 1;
		break;
	}
}

struct parts_cg *parts_get_cg(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_CG) {
		parts_state_reset(&parts->states[state], PARTS_CG);
	}
	return &parts->states[state].cg;
}

struct parts_text *parts_get_text(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_TEXT) {
		parts_state_reset(&parts->states[state], PARTS_TEXT);
	}
	return &parts->states[state].text;
}

struct parts_animation *parts_get_animation(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_ANIMATION) {
		parts_state_reset(&parts->states[state], PARTS_ANIMATION);
	}
	return &parts->states[state].anim;
}

struct parts_numeral *parts_get_numeral(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_NUMERAL) {
		parts_state_reset(&parts->states[state], PARTS_NUMERAL);
	}
	return &parts->states[state].num;
}

struct parts_gauge *parts_get_hgauge(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_HGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_HGAUGE);
	}
	return &parts->states[state].gauge;
}

struct parts_gauge *parts_get_vgauge(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_VGAUGE) {
		parts_state_reset(&parts->states[state], PARTS_VGAUGE);
	}
	return &parts->states[state].gauge;
}

struct parts_construction_process *parts_get_construction_process(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_CONSTRUCTION_PROCESS) {
		parts_state_reset(&parts->states[state], PARTS_CONSTRUCTION_PROCESS);
	}
	return &parts->states[state].cproc;
}

struct parts_flash *parts_get_flash(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_FLASH) {
		parts_state_reset(&parts->states[state], PARTS_FLASH);
	}
	return &parts->states[state].flash;
}

struct parts_flat *parts_get_flat(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_FLAT) {
		parts_state_reset(&parts->states[state], PARTS_FLAT);
	}
	return &parts->states[state].flat;
}

struct parts_movie *parts_get_movie(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_MOVIE) {
		parts_state_reset(&parts->states[state], PARTS_MOVIE);
	}
	return &parts->states[state].movie;
}

struct parts_layout_box *parts_get_layout_box(struct parts *parts)
{
	if (parts->states[0].type != PARTS_LAYOUT_BOX) {
		parts_state_reset(&parts->states[0], PARTS_LAYOUT_BOX);
	}
	return &parts->states[0].layout_box;
}

struct parts_3dlayer *parts_get_3dlayer(struct parts *parts, int state)
{
	if (parts->states[state].type != PARTS_3DLAYER) {
		parts_state_reset(&parts->states[state], PARTS_3DLAYER);
	}
	return &parts->states[state].layer3d;
}

static Point calculate_offset(int mode, int w, int h)
{
	switch (mode) {
	case 1:  return (Point) {    0, 0    }; // top-left
	case 2:  return (Point) { -w/2, 0    }; // top-center
	case 3:  return (Point) {   -w, 0    }; // top-right
	case 4:  return (Point) {    0, -h/2 }; // middle-left
	case 5:  return (Point) { -w/2, -h/2 }; // middle-center
	case 6:  return (Point) {   -w, -h/2 }; // middle-right
	case 7:  return (Point) {    0, -h   }; // bottom-left
	case 8:  return (Point) { -w/2, -h   }; // bottom-center
	case 9:  return (Point) {   -w, -h   }; // bottom-right
	/* [D328] 上游 `77f7f02`（2026-10-02）把 mode 0 / 未映射模式改成了**左上角
	 * `{0,0}`**，理由是「`{mode,(3*h)/4}` 是从 `StoatSpriteEngine.c` 抄来的，
	 * 而 GoatGUIEngine / GUIEngine / PartsEngine 里 mode 0 就是左上角」。
	 *
	 * 我们这两行还是抄来的旧逻辑：mode 0 会把部件往下推 `3h/4`（1080 高的部件
	 * 就是 +810px），并且模式 >9 时把 `mode` 本身当 x 偏移 —— 两者都会让
	 * origin_offset / hitbox 落错位置。
	 *
	 * `XSYS4_ORIGIN_MODE0_TL=1` 切到上游语义做 A/B（默认先保持现状，
	 * 因为我们的 activity 加载器可能已经自己补偿过）。 */
	default:
		if (getenv("XSYS4_ORIGIN_MODE0_TL"))
			return (Point) { 0, 0 };
		return (Point) { mode, (3*h)/4 }; // why...
	}
}

/*
 * 应当在下列情况调用：
 *   - 位置（parts->pos）变化时
 *   - 宽度或高度变化时
 *   - 原点模式变化时
 */
static void parts_common_recalculate_hitbox(struct parts *parts, struct parts_common *common)
{

	if (common->surface_area.w || common->surface_area.h) {
		/* [D192] 尺寸未知时，AIN 的「表示領域」是这个部件唯一的尺寸来源。
		 * 任务地图選択肢按钮的 flat 是空 flat（`hdr=0x0`、`nr_libraries=0`），
		 * `parts_flat_load` 于是不设 `common.w/h`；这里再拿 `{0,0,0,0}` 与
		 * `surface_area` 求交就得到空 hitbox —— 按钮既画不出来也点不到，
		 * 回合流程永远推不动。用 surface_area 的下边界补齐尺寸，
		 * 求交结果正好还是 surface_area 本身。 */
		if (common->w <= 0 && common->h <= 0) {
			common->w = common->surface_area.x + common->surface_area.w;
			common->h = common->surface_area.y + common->surface_area.h;
		}
		common->origin_offset = calculate_offset(parts->origin_mode,
				common->surface_area.w, common->surface_area.h);
		Rectangle r = { 0, 0, common->w, common->h };
		SDL_IntersectRect(&r, &common->surface_area, &common->hitbox);
		common->origin_offset.x -= common->surface_area.x;
		common->origin_offset.y -= common->surface_area.y;
		common->hitbox.x += parts->local.pos.x + common->origin_offset.x;
		common->hitbox.y += parts->local.pos.y + common->origin_offset.y;
	} else {
		common->origin_offset = calculate_offset(parts->origin_mode, common->w, common->h);
		common->hitbox = (Rectangle) {
			.x = parts->local.pos.x + common->origin_offset.x,
			.y = parts->local.pos.y + common->origin_offset.y,
			.w = common->w,
			.h = common->h,
		};
	}
}

/* [D397] 部件的 (类型, w, h, surface_area w/h)，排查「命中盒 0×0」用。
 * 不做任何推断，只把 `states[0].common` 原样交出来 —— 判据要能
 * 区分「EX 没给尺寸」「AIN 没设尺寸」「设了但被覆盖」三种情形。 */
bool PE_PartsBrief(int parts_no, int *type, int *w, int *h, int *sa_w, int *sa_h)
{
	if (parts_no <= 0)
		return false;
	struct parts *p = parts_get(parts_no);
	if (!p)
		return false;
	struct parts_common *c = &p->states[PARTS_STATE_DEFAULT].common;
	if (type) *type = (int)p->state;
	if (w) *w = c->w;
	if (h) *h = c->h;
	if (sa_w) *sa_w = c->surface_area.w;
	if (sa_h) *sa_h = c->surface_area.h;
	return true;
}

/* [D397] 用户组件标记 + 父号。`[btn]` 要能区分「EX 没声明成用户组件」
 * 和「声明了但 uc_label 空」—— 这两种都会让 prepare 早退，但修法完全不同。 */
int PE_PartsUcFlags(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p)
		return -1;
	return (p->uc_object ? 1 : 0) | ((p->uc_label && *p->uc_label) ? 2 : 0) |
	       (p->uc_data_n > 0 ? 4 : 0);
}

int PE_PartsParent(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	return (p && p->parent) ? p->parent->no : 0;
}

void parts_recalculate_hitbox(struct parts *parts)
{
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_common_recalculate_hitbox(parts, &parts->states[i].common);
	}
}

static void parts_combine_params(struct parts_params *parent, struct parts_params *child,
		struct parts_params *out);

static void parts_refresh_subtree(struct parts *parts, struct parts_params *parent)
{
	parts_combine_params(parent, &parts->local, &parts->global);
	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_refresh_subtree(child, &parts->global);
	}
}

/* 把 pending_parent 落到 children 链表（只挂树，不跑 layout）。
 * 与 PE_UpdateComponent() 里那段逻辑一致，抽出来是为了能"只建树不排布"。 */
static void parts_bind_pending_parent(struct parts *parts)
{
	if (parts->pending_parent < 0)
		return;
	struct parts *parent = parts_try_get(parts->pending_parent);
	if (parent) {
		if (parts->parent)
			TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
		parts->parent = parent;
		TAILQ_INSERT_TAIL(&parent->children, parts, child_list_entry);
		if (parent->states[0].type == PARTS_LAYOUT_BOX)
			parts_component_dirty(parent);
	}
	parts->pending_parent = -1;
}

/* 从根参数出发重算整棵树的 global 参数。
 *
 * 为什么需要：部件被创建时还没有父节点，PE_SetPos() 只能算出
 * global = local；随后 PE_SetParentPartsNumber() 只是登记 pending_parent，
 * 真正的合成发生在 PE_UpdateComponent() 里。而 PE_UpdateComponent() 会跳过
 * "父节点还脏" 的子部件（它们已经被从脏链表里弹掉，不会自动回来），
 * 布局框又会改写子部件的 local.pos 而不回流通 global ——
 * 结果子部件的 global.pos 永远停在建树瞬间的值。
 * 渲染（parts_render_*）与命中测试都读 global.pos，
 * 于是整棵子树都画在错误的坐标上（Rance10 标题菜单画到屏幕外的根因）。
 *
 * 注意：这里只重算参数，**不触发 layout**。layout 会按布局框自己的原点
 * 覆盖子部件的 local.pos，一旦提前跑，就会把 EX 里给好的绝对位置冲掉
 * （NetworkConfirmation 的 OK 按钮被排到左上角就是这么来的）。
 *
 * 建树完成后调一次即可；幂等。 */
void PE_RefreshGlobalTree(void)
{
	struct parts *parts;
	/* 两趟：先把所有 pending_parent 挂完，再从根往下合成参数。 */
	PARTS_LIST_FOREACH(parts) {
		parts_bind_pending_parent(parts);
	}
	PARTS_LIST_FOREACH(parts) {
		if (!parts->parent)
			parts_refresh_subtree(parts, &root_params);
	}
	parts_engine_dirty();
}

static void parts_update_global_pos(struct parts *parts, Point parent_pos)
{
	parts->global.pos = (Point) {
		parent_pos.x + parts->local.pos.x,
		parent_pos.y + parts->local.pos.y
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_pos(child, parts->global.pos);
	}
}

void parts_set_pos(struct parts *parts, Point pos)
{
	parts->local.pos.x = pos.x;
	parts->local.pos.y = pos.y;
	parts_recalculate_hitbox(parts);
	parts_update_global_pos(parts, parts->parent ? parts->parent->global.pos : root_params.pos);
	parts_dirty(parts);
}

void parts_set_global_pos(Point pos)
{
	root_params.pos = pos;
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_update_global_pos(parts, root_params.pos);
	}
	parts_engine_dirty();
}

static void parts_update_global_z(struct parts *parts, int parent_z)
{
	parts->global.z = parent_z + parts->local.z;
	parts_list_resort(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_z(child, parts->global.z);
	}
}

void parts_set_z(struct parts *parts, int z)
{
	if (parts->local.z == z)
		return;

	parts->local.z = z;
	parts_update_global_z(parts, parts->parent ? parts->parent->global.z : 0);
	parts_dirty(parts);
}

static void parts_update_global_show(struct parts *parts, bool parent_show)
{
	parts->global.show = parent_show && parts->local.show;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_show(child, parts->global.show);
	}
}

void parts_set_show(struct parts *parts, bool show)
{
	if (parts->local.show == show)
		return;

	parts->local.show = show;
	parts_update_global_show(parts, parts->parent ? parts->parent->global.show : true);
	parts_dirty(parts);
}

static void parts_update_global_alpha(struct parts *parts, int parent_alpha)
{
	parts->global.alpha = parent_alpha * (parts->local.alpha / 255.0f);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_alpha(child, parts->global.alpha);
	}
}

void parts_set_alpha(struct parts *parts, int alpha)
{
	parts->local.alpha = max(0, min(255, alpha));
	parts_update_global_alpha(parts, parts->parent ? parts->parent->global.alpha : 255);
	parts_dirty(parts);
}

static void parts_update_global_add_color(struct parts *parts, SDL_Color parent_color)
{
	parts->global.add_color = (SDL_Color) {
		parent_color.r * (parts->local.add_color.r / 255.0f),
		parent_color.g * (parts->local.add_color.g / 255.0f),
		parent_color.b * (parts->local.add_color.b / 255.0f),
		0
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_add_color(child, parts->global.add_color);
	}
}

void parts_set_add_color(struct parts *parts, SDL_Color color)
{
	parts->local.add_color = color;
	parts_update_global_add_color(parts, parts->parent ? parts->parent->global.add_color
			: (SDL_Color){0,0,0,0});
	parts_dirty(parts);
}

static void parts_update_global_multiply_color(struct parts *parts, SDL_Color parent_color)
{
	parts->global.multiply_color = (SDL_Color) {
		parent_color.r * (parts->local.multiply_color.r / 255.0f),
		parent_color.g * (parts->local.multiply_color.g / 255.0f),
		parent_color.b * (parts->local.multiply_color.b / 255.0f),
		255
	};

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_multiply_color(child, parts->global.multiply_color);
	}
}

void parts_set_multiply_color(struct parts *parts, SDL_Color color)
{
	parts->local.multiply_color = color;
	parts_update_global_multiply_color(parts, parts->parent ? parts->parent->global.multiply_color
			: (SDL_Color){255,255,255,255});
	parts_dirty(parts);
}

void parts_set_origin_mode(struct parts *parts, int origin_mode)
{
	parts->origin_mode = origin_mode;
	parts_recalculate_hitbox(parts);
	parts_dirty(parts);
}

static void parts_update_global_scale_x(struct parts *parts, float parent_scale_x)
{
	parts->global.scale.x = parent_scale_x * parts->local.scale.x;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_scale_x(child, parts->global.scale.x);
	}
}

void parts_set_scale_x(struct parts *parts, float mag)
{
	parts->local.scale.x = mag;
	parts_recalculate_hitbox(parts);
	parts_update_global_scale_x(parts, parts->parent ? parts->parent->global.scale.x : 1.0f);
	parts_dirty(parts);
}

static void parts_update_global_scale_y(struct parts *parts, float parent_scale_y)
{
	parts->global.scale.y = parent_scale_y * parts->local.scale.y;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_scale_y(child, parts->global.scale.y);
	}
}

void parts_set_scale_y(struct parts *parts, float mag)
{
	parts->local.scale.y = mag;
	parts_recalculate_hitbox(parts);
	parts_update_global_scale_y(parts, parts->parent ? parts->parent->global.scale.y : 1.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_x(struct parts *parts, float parent_rot_x)
{
	parts->global.rotation.x = parent_rot_x + parts->local.rotation.x;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_x(child, parts->global.rotation.x);
	}
}

void parts_set_rotation_x(struct parts *parts, float rot)
{
	parts->local.rotation.x = rot;
	parts_update_global_rotate_x(parts, parts->parent ? parts->parent->global.rotation.x : 0.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_y(struct parts *parts, float parent_rot_y)
{
	parts->global.rotation.y = parent_rot_y + parts->local.rotation.y;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_y(child, parts->global.rotation.y);
	}
}

void parts_set_rotation_y(struct parts *parts, float rot)
{
	parts->local.rotation.y = rot;
	parts_update_global_rotate_y(parts, parts->parent ? parts->parent->global.rotation.y : 0.0f);
	parts_dirty(parts);
}

static void parts_update_global_rotate_z(struct parts *parts, float parent_rot_z)
{
	parts->global.rotation.z = parent_rot_z + parts->local.rotation.z;

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_global_rotate_z(child, parts->global.rotation.z);
	}
}

void parts_set_rotation_z(struct parts *parts, float rot)
{
	parts->local.rotation.z = rot;
	parts_update_global_rotate_z(parts, parts->parent ? parts->parent->global.rotation.z : 0.0f);
	parts_dirty(parts);
}

void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h)
{
	common->w = w;
	common->h = h;
	parts_common_recalculate_hitbox(parts, common);
}

bool _parts_cg_set(struct parts *parts, struct parts_cg *parts_cg, struct cg *cg, int cg_no,
		struct string *name)
{
	if (!cg)
		return false;
	gfx_delete_texture(&parts_cg->common.texture);
	gfx_init_texture_with_cg(&parts_cg->common.texture, cg);
	/* [D300] 把刚上传的 CG 纹理另存成 PNG —— 判断「纹理内容到底有没有图」
	 * 的唯一决定性证据。s92–s99 已排除几何/层级/可见性/GL 有效性，
	 * 若这张 PNG 是空的，问题在解码；若有图，问题在绘制。
	 * 只在 XSYS4_DUMP_CG_NO 指定的部件上触发一次。 */
	/* 按尺寸触发而不是按部件号：号段会复用（[PROBE-GATED] 教训），
	 * 而「全宽 + 高 250..600」正是消息窗口底板/立绘量级。 */
	if (getenv("XSYS4_DUMP_CG")) {
		static int dumped;
		int want = getenv("XSYS4_DUMP_CG_SIZE") ? atoi(getenv("XSYS4_DUMP_CG_SIZE")) : 0;
		int hit = want ? (cg->metrics.w == want)
			: (cg->metrics.w >= 1900 && cg->metrics.h >= 250 &&
			   cg->metrics.h <= 600);
		if (dumped < 8 && hit) {
			dumped++;
			typedef struct texture Texture;
			extern int gfx_save_texture(Texture *, const char *, enum cg_type);
			char path[256];
			snprintf(path, sizeof(path), "/tmp/texdump_%d_%dx%d.png",
				 parts->no, cg->metrics.w, cg->metrics.h);
			int ok = gfx_save_texture(&parts_cg->common.texture, path, ALCG_PNG);
			NOTICE("[texdump] no=%d %dx%d save=%d path=%s", parts->no,
			       cg->metrics.w, cg->metrics.h, ok, path);
		}
	}
	parts_set_dims(parts, &parts_cg->common, cg->metrics.w, cg->metrics.h);
	parts_cg->no = cg_no;
	if (parts_cg->name)
		free_string(parts_cg->name);
	parts_cg->name = name;
	parts_dirty(parts);
	cg_free(cg);
	return true;
}

bool parts_cg_set_by_index(struct parts *parts, struct parts_cg *cg, int cg_no)
{
	assert(cg_no);
	return _parts_cg_set(parts, cg, asset_cg_load(cg_no), cg_no, NULL);
}

bool parts_cg_set(struct parts *parts, struct parts_cg *parts_cg, struct string *cg_name)
{
	assert(cg_name && *(cg_name->text) != '\0');
	int no;
	struct cg *cg;
	if (!memcmp(cg_name->text, "<save>", 6)) {
		char *path = savedir_path(cg_name->text + 6);
		cg = cg_load_file(path);
		no = 0;
		free(path);
	} else {
		cg = asset_cg_load_by_name(cg_name->text, &no);
	}
	return _parts_cg_set(parts, parts_cg, cg, no, string_dup(cg_name));
}

struct parts_numeral_font *parts_numeral_fonts = NULL;
int parts_nr_numeral_fonts = 0;

void parts_numeral_font_init(struct parts_numeral_font *font)
{
	if (font->type == PARTS_NUMERAL_FONT_SEPARATE) {
		for (int i = 0; i < 12; i++) {
			struct cg *cg = asset_cg_load(font->cg_no + i);
			if (!cg) {
				font->cg[i].handle = 0;
				continue;
			}
			gfx_init_texture_with_cg(&font->cg[i], cg);
			cg_free(cg);
		}
	} else if (font->type == PARTS_NUMERAL_FONT_SEPARATE2) {
		for (int i = 0; i < 12; i++) {
			if (font->width[i] < 0)
				continue;
			struct cg *cg = asset_cg_load(font->width[i]);
			if (!cg) {
				font->cg[i].handle = 0;
				continue;
			}
			gfx_init_texture_with_cg(&font->cg[i], cg);
			cg_free(cg);
		}
	} else if (font->type == PARTS_NUMERAL_FONT_COMBINED) {
		int x = 0;
		Texture t = {0};
		struct cg *cg = asset_cg_load(font->cg_no);
		gfx_init_texture_with_cg(&t, cg);
		for (int i = 0; i < 12; i++) {
			if (font->width[i] <= 0)
				continue;
			gfx_init_texture_blank(&font->cg[i], font->width[i], t.h);
			gfx_copy_with_alpha_map(&font->cg[i], 0, 0, &t, x, 0, font->width[i], t.h);
			x += font->width[i];
		}
		gfx_delete_texture(&t);
		cg_free(cg);
	}
}

static int parts_load_numeral_font_separate(int cg_no)
{
	// 找已有的字体
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		if (parts_numeral_fonts[i].type == PARTS_NUMERAL_FONT_SEPARATE
				&& parts_numeral_fonts[i].cg_no == cg_no)
			return i;
	}

	// 加载新字体
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = cg_no;
	font->type = PARTS_NUMERAL_FONT_SEPARATE;
	parts_numeral_font_init(font);
	return font_no;
}

static int parts_load_numeral_font_separate_string(struct string *cg_name)
{
	// 把名字转成 CG 下标
	int indices[12];
	for (int i = 0; i < 12; i++) {
		struct string *name = string_format(cg_name, (union vm_value){.i = i}, STRFMT_INT);
		if (!asset_exists_by_name(ASSET_CG, name->text, &indices[i])) {
			WARNING("numeral cg doesn't exist: %s", display_sjis0(name->text));
			indices[i] = -1;
		}
		free_string(name);
	}

	// 找已有的字体
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_SEPARATE2)
			continue;
		bool not_match = false;
		for (int d = 0; d < 12; d++) {
			if (font->width[d] != indices[d]) {
				not_match = true;
				break;
			}
		}
		if (not_match)
			continue;
		return i;
	}

	// 加载新字体
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = indices[0];
	font->type = PARTS_NUMERAL_FONT_SEPARATE2;
	memcpy(font->width, indices, sizeof(int) * 12);
	parts_numeral_font_init(font);
	return font_no;
}

static int parts_load_numeral_font_combined(int cg_no, int w[12])
{
	// 找已有的字体
	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		if (font->type != PARTS_NUMERAL_FONT_COMBINED || font->cg_no != cg_no)
			continue;
		for (int i = 0; i < 12; i++) {
			if (w[i] != font->width[i])
				continue;
		}
		return i;
	}

	// 加载新字体
	int font_no = parts_nr_numeral_fonts++;
	parts_numeral_fonts = xrealloc_array(parts_numeral_fonts, font_no, font_no + 1,
			sizeof(struct parts_numeral_font));
	struct parts_numeral_font *font = &parts_numeral_fonts[font_no];
	font->cg_no = cg_no;
	font->type = PARTS_NUMERAL_FONT_COMBINED;
	memcpy(font->width, w, sizeof(int) * 12);
	parts_numeral_font_init(font);
	return font_no;
}

static bool parts_numeral_update(struct parts *parts, struct parts_numeral *num)
{
	// XXX：号码还没设过时不要生成纹理
	if (!num->have_num || num->font_no < 0)
		return true;
	int_least64_t n = num->num;
	bool negative = n < 0;
	if (negative)
		n *= -1;

	// 拆出各位数字
	int digits;
	uint8_t d[32];
	for (digits = 0; n; digits++) {
		d[digits] = n % 10;
		n /= 10;
	}

	// 把数字编码成纹理下标
	int nr_chars = 0;
	uint8_t chars[32];
	if (negative) {
		chars[nr_chars++] = 10;
	}
	for (int i = 0; i < digits; i++) {
		if (num->show_comma && i > 0 && i % 3 == 0) {
			chars[nr_chars++] = 11;
		}
		chars[nr_chars++] = d[i];
	}

	for (int i = digits; i < num->length; i++) {
		chars[nr_chars++] = 0;
	}

	struct parts_numeral_font *font = &parts_numeral_fonts[num->font_no];

	// 算出输出尺寸
	int w = 0, h = 0;
	for (int i = nr_chars-1; i >= 0; i--) {
		if (!font->cg[chars[i]].handle)
			continue;
		w += font->cg[chars[i]].w;
		h = max(h, font->cg[chars[i]].h);
	}
	w += (nr_chars-1) * num->space;

	// 把字符拷进纹理
	gfx_delete_texture(&num->common.texture);
	gfx_init_texture_rgba(&num->common.texture, w, h, (SDL_Color){0, 0, 0, 255});

	int x = 0;
	for (int i = nr_chars-1; i>= 0; i--) {
		Texture *ch = &font->cg[chars[i]];
		if (!ch->handle)
			continue;
		gfx_copy_with_alpha_map(&num->common.texture, x, 0, ch, 0, 0, ch->w, ch->h);
		x += ch->w + num->space;
	}

	parts_set_dims(parts, &num->common, w, h);

	parts_dirty(parts);
	return true;

}

bool parts_numeral_set_number(struct parts *parts, struct parts_numeral *num, int n)
{
	num->have_num = true;
	num->num = n;
	return parts_numeral_update(parts, num);
}

void parts_set_state(struct parts *parts, enum parts_state_type state)
{
	if (parts->lock_input_state)
		return;
	while (state > PARTS_STATE_DEFAULT && parts->states[state].type == PARTS_UNINITIALIZED)
		state--;
	if (parts->state != state) {
		parts->state = state;
		parts_dirty(parts);
	}
}

void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h)
{
	if (x < 0 || y < 0)
		return;
	common->surface_area = (Rectangle) { x, y, w, h };
	parts_common_recalculate_hitbox(parts, common);
}

static bool parts_animation_update(struct parts_animation *anim, int passed_time)
{
	if (passed_time <= 0 || !anim->nr_frames)
		return false;

	const unsigned elapsed = anim->elapsed + passed_time;
	const unsigned frame_diff = elapsed / anim->frame_time;
	const unsigned remainder = elapsed % anim->frame_time;

	if (frame_diff > 0) {
		anim->elapsed = remainder;
		anim->current_frame = (anim->current_frame + frame_diff) % anim->nr_frames;
		anim->common.texture = anim->frames[anim->current_frame];
		return true;
	} else {
		anim->elapsed = elapsed;
		return false;
	}
}

static void parts_update_loop(struct parts *parts, int passed_time)
{
	bool dirty = false;
	switch (parts->states[parts->state].type) {
	case PARTS_ANIMATION:
		dirty = parts_animation_update(&parts->states[parts->state].anim, passed_time);
		break;
	case PARTS_FLASH:
		dirty = parts_flash_update(&parts->states[parts->state].flash, passed_time);
		break;
	case PARTS_FLAT: {
		struct parts_flat *pf = &parts->states[parts->state].flat;
		dirty = parts_flat_update(pf, passed_time, parts->no);
		/* [D243] flat 帧探针。PARTS_DUMP 一次打全场几百行、把帧率拖到
		 * 听不见，所以单独给一条只打帧号的轻量通路：
		 *   XSYS4_FLAT_FRAME_DBG=1      所有 flat
		 *   XSYS4_FLAT_FRAME_DBG=<no>   只跟这个部件号
		 * 判据：`frame` 要能自己涨到 `end`；停在 2/15 就是没推进。 */
		const char *ffd = getenv("XSYS4_FLAT_FRAME_DBG");
		if (ffd && *ffd && pf->flat && pf->root_state) {
			int want = atoi(ffd);
			static unsigned long long tick;
			if (want <= 1 || want == parts->no) {
				if ((tick++ % 90) == 0)
					NOTICE("[flatf] no=%d ctrl=%d frame=%d stopped=%d fps=%d pt=%d",
					       parts->no, parts->controller_no,
					       pf->root_state->current_frame,
					       pf->root_state->stopped,
					       pf->flat->hdr.fps, passed_time);
			}
		}
		parts_flat_dump_tree(parts->no, pf);
		break;
	}
	default:
		break;
	}
	if (dirty)
		parts_dirty(parts);
}

/* 用户组件 (component type 15)：登记控件种类与显示文字。
 * 入参都是游戏自己的编码（GBK / SJIS）字节串，原样留存，绘制时交给
 * parts_render_user_component()。重复设置会丢弃旧的文字缓存。 */
void parts_set_user_component(struct parts *parts, const char *object, const char *label)
{
	if (!parts)
		return;
	free(parts->uc_object);
	free(parts->uc_label);
	parts->uc_object = object ? strdup(object) : NULL;
	parts->uc_label = label ? strdup(label) : NULL;
	/* 文字缓存要跟着失效，否则改标签后画的还是旧字。 */
	if (parts->uc_text_valid) {
		parts_text_free(&parts->uc_text);
		parts->uc_text_valid = false;
	}
	parts_engine_dirty();
}

/* 用户组件的键值数据。EX 的 数据 就是一个 [键,值,键,值,...] 列表，
 * 原样存下来，游戏用 GetUserComponentData(no, "参数") 之类读回去。 */
void parts_set_user_component_data(struct parts *parts, const char *key, const char *value)
{
	if (!parts || !key)
		return;
	for (int i = 0; i + 1 < parts->uc_data_n; i += 2) {
		if (strcmp(parts->uc_data[i], key) != 0)
			continue;
		free(parts->uc_data[i + 1]);
		parts->uc_data[i + 1] = value ? strdup(value) : NULL;
		return;
	}
	parts->uc_data = xrealloc_array(parts->uc_data, parts->uc_data_n,
			parts->uc_data_n + 2, sizeof(char *));
	parts->uc_data[parts->uc_data_n++] = strdup(key);
	parts->uc_data[parts->uc_data_n++] = value ? strdup(value) : NULL;
}

const char *parts_get_user_component_data(struct parts *parts, const char *key)
{
	if (!parts || !key)
		return NULL;
	for (int i = 0; i + 1 < parts->uc_data_n; i += 2) {
		if (strcmp(parts->uc_data[i], key) == 0)
			return parts->uc_data[i + 1];
	}
	return NULL;
}

static void parts_update_animation(int passed_time)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_update_loop(parts, passed_time);
		/* 用户组件的文字纹理在这里建（渲染之外），见 render.c。 */
		parts_uc_text_prepare(parts);
	}
}

/* [D396] 控件状态。存在 parts 上，生命周期跟部件走。 */
struct parts_widget *PE_widget_peek(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->widget : NULL;
}

struct parts_widget *PE_widget(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return NULL;
	if (!parts->widget)
		parts->widget = xcalloc(1, sizeof(struct parts_widget));
	return parts->widget;
}

void PE_widget_free(struct parts_widget *w)
{
	if (!w)
		return;
	/* `free_string()` 不接受 NULL（它直接读 `str->ref`）——
	 * 控件状态里大部分字段是「没设过 = NULL」，必须逐个判空。 */
	if (w->cg_name)
		free_string(w->cg_name);
	if (w->override_cg_name)
		free_string(w->override_cg_name);
	if (w->override_checked_cg_name)
		free_string(w->override_checked_cg_name);
	if (w->flat_name)
		free_string(w->flat_name);
	if (w->override_flat_name)
		free_string(w->override_flat_name);
	if (w->override_checked_flat_name)
		free_string(w->override_checked_flat_name);
	if (w->text)
		free_string(w->text);
	if (w->msg_func_name)
		free_string(w->msg_func_name);
	{
		struct string *sl[] = {
			w->scroll.cg_name, w->scroll.flat_name,
			w->scroll.ov_head_cg, w->scroll.ov_tail_cg,
			w->scroll.ov_bar_cg, w->scroll.ov_bg_cg,
			w->scroll.ov_head_flat, w->scroll.ov_tail_flat,
			w->scroll.ov_bar_flat, w->scroll.ov_bg_flat,
		};
		for (unsigned i = 0; i < sizeof sl / sizeof sl[0]; i++)
			if (sl[i])
				free_string(sl[i]);
	}
	for (int i = 0; i < w->nr_items; i++)
		if (w->items[i])
			free_string(w->items[i]);
	free(w->items);
	for (int i = 0; i < w->nr_cb_items; i++)
		if (w->cb_items[i])
			free_string(w->cb_items[i]);
	free(w->cb_items);
	free(w->radio_children);
	free(w);
}

void parts_release(int parts_no)
{
	struct ht_slot *slot = ht_put_int(parts_table, parts_no, NULL);
	if (!slot->value)
		return;

	struct parts *parts = slot->value;
	if (getenv("XSYS4_PART_LIFE") && parts_no >= 900000560 && parts_no <= 900000600)
		NOTICE("[plife] release no=%d di=%d", parts_no, parts->delegate_index);
	parts_input_reset_drag(parts);
	parts_clear_motion(parts);
	for (int i = 0; i < PARTS_NR_STATES; i++) {
		parts_state_free(&parts->states[i]);
	}
	if (parts->uc_text_valid)
		parts_text_free(&parts->uc_text);
	free(parts->uc_object);
	free(parts->uc_label);
	free(parts->text_plain);
	PE_widget_free(parts->widget);
	parts->widget = NULL;
	for (int i = 0; i < parts->uc_data_n; i++)
		free(parts->uc_data[i]);
	free(parts->uc_data);

	// 断开父子关系
	while (!TAILQ_EMPTY(&parts->children)) {
		struct parts *child = TAILQ_FIRST(&parts->children);
		TAILQ_REMOVE(&parts->children, child, child_list_entry);
		child->parent = NULL;
		parts_component_dirty(child);
	}
	if (parts->parent) {
		TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
		parts->parent = NULL;
	}

	parts_list_remove(parts);
	dirty_list_remove(parts);
	free(parts);
	slot->value = NULL;
	parts_engine_dirty();
}

void parts_release_all(void)
{
	while (!TAILQ_EMPTY(&parts_list)) {
		struct parts *parts = TAILQ_FIRST(&parts_list);
		parts_release(parts->no);
	}

	for (int i = 0; i < parts_nr_numeral_fonts; i++) {
		struct parts_numeral_font *font = &parts_numeral_fonts[i];
		for (int i = 0; i < 12; i++) {
			if (font->cg[i].handle)
				gfx_delete_texture(&font->cg[i]);
		}
	}

	free(parts_numeral_fonts);
	parts_numeral_fonts = NULL;
	parts_nr_numeral_fonts = 0;
}

static bool parts_engine_initialized = false;

void PE_enable_multi_controller(void)
{
	if (parts_multi_controller)
		return;
	assert(!parts_engine_initialized);
	parts_multi_controller = true;
}

bool PE_Init(void)
{
	if (parts_engine_initialized)
		return true;
	// XXX：《Oyako Rankan》不调用 ChipmunkSpriteEngine.Init
	sact_init(16, CHIPMUNK_SPRITE_ENGINE);
	parts_table = ht_create(1024);
	parts_render_init();
	parts_debug_init();
	ctrl_stack_init();
	parts_engine_initialized = true;
	return true;
}

void PE_Reset(void)
{
	PE_ReleaseAllParts();
	PE_ReleaseMessage();
	ctrl_stack_init();
	sact_ModuleFini();
}

static bool parts_has_dirty_parent(struct parts *parts)
{
	for (struct parts *parent = parts->parent; parent; parent = parent->parent) {
		if (parent->dirty)
			return true;
	}
	return false;
}

static void parts_combine_params(struct parts_params *parent, struct parts_params *child,
		struct parts_params *out)
{
	out->z = parent->z + child->z;
	out->pos = (Point) { parent->pos.x + child->pos.x, parent->pos.y + child->pos.y };
	out->show = parent->show && child->show;
	out->alpha = parent->alpha * (child->alpha / 255.0f);
	out->scale.x = parent->scale.x * child->scale.x;
	out->scale.y = parent->scale.y * child->scale.y;
	out->rotation.x = parent->rotation.x + child->rotation.x;
	out->rotation.y = parent->rotation.y + child->rotation.y;
	out->rotation.z = parent->rotation.z + child->rotation.z;
	out->add_color.r = parent->add_color.r * (child->add_color.r / 255.0f);
	out->add_color.g = parent->add_color.g * (child->add_color.g / 255.0f);
	out->add_color.b = parent->add_color.b * (child->add_color.b / 255.0f);
	out->multiply_color.r = parent->multiply_color.r * (child->multiply_color.r / 255.0f);
	out->multiply_color.g = parent->multiply_color.g * (child->multiply_color.g / 255.0f);
	out->multiply_color.b = parent->multiply_color.b * (child->multiply_color.b / 255.0f);
	out->alpha_clipper_parts_no = child->alpha_clipper_parts_no
			? child->alpha_clipper_parts_no : parent->alpha_clipper_parts_no;
}

static void parts_update_component(struct parts *parts)
{
	parts_combine_params(parts->parent ? &parts->parent->global : &root_params,
			&parts->local, &parts->global);
	if (parts_get_sprite_z(parts) != parts->sp.z
			|| parts_get_sprite_z2(parts) != parts->sp.z2) {
		parts_list_resort(parts);
	}

	if (parts->dirty) {
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
		parts->dirty = false;
	}

	parts_do_layout(parts);

	struct parts *child;
	PARTS_FOREACH_CHILD(child, parts) {
		parts_update_component(child);
	}
}

void PE_UpdateComponent(possibly_unused int passed_time)
{
	// 读档之后，游戏脚本可能算出一个负的时间增量：
	// 因为它在存档里存的是绝对的 system.GetTime() 值，
	// 而那个值跨进程重启是没有意义的。
	if (passed_time < 0)
		passed_time = 0;
	while (!TAILQ_EMPTY(&dirty_list)) {
		// 从脏表里弹出部件对象
		struct parts *parts = TAILQ_FIRST(&dirty_list);
		TAILQ_REMOVE(&dirty_list, parts, dirty_list_entry);
		parts->dirty = false;

		// 更新父部件
		struct parts *parent;
		if (parts->pending_parent >= 0 && (parent = parts_try_get(parts->pending_parent))) {
			if (parts->parent) {
				TAILQ_REMOVE(&parts->parent->children, parts, child_list_entry);
			}
			parts->parent = parent;
			TAILQ_INSERT_TAIL(&parent->children, parts, child_list_entry);

			// 父部件若是布局盒，就把它标脏，好让它重排子部件
			if (parent->states[0].type == PARTS_LAYOUT_BOX)
				parts_component_dirty(parent);
		}
		// TODO：如果子部件已经有父部件、却给了一个非法的父号码，
		//       应该把它变成孤儿吗？
		parts->pending_parent = -1;

		// 父部件是脏的，这里就什么也不做
		if (parts_has_dirty_parent(parts))
			continue;

		// 更新子部件参数
		parts_update_component(parts);
	}
}

bool parts_message_window_show = true;

/* [D128] 图层遮挡：一层里出现"铺满整个视图且不透明"的部件时，它下面的
 * 所有图层都不该再画。
 *
 * 为什么不能简单地"新压入的图层盖住旧图层"：对话框也走 AddController，
 * 于是一弹对话框整张地图就黑掉。真实机靠 SceneStack 析构里的 EraseLayer
 * 把旧场景连部件一起带走，而我们的图层/部件在泄漏（见 [LAYER-LEAK]），
 * 旧场景还活着——所以只能从"能不能盖住屏幕"反推视觉上的上下关系：
 *   标题层有全屏背景  → 盖住底下的场景层   ✓
 *   任务地图有全屏底图 → 盖住标题层        ✓
 *   对话框只有 862x226 的面板 → 不盖地图   ✓
 * 每帧重算，代价是遍历一次部件表。XSYS4_NO_LAYER_COVER=1 可关闭。 */
int parts_cover_layer;

static void parts_update_cover_layer(void)
{
	static int disabled = -1;
	if (disabled < 0)
		disabled = getenv("XSYS4_NO_LAYER_COVER") ? 1 : 0;
	if (disabled) {
		parts_cover_layer = 0;
		return;
	}

	int vw = gfx_view_width(), vh = gfx_view_height();
	int top = 0;
	/* [D190] `opaque` 是**本帧的统计量**，不是持久状态。原来只在
	 * `RemoveController` 里清，于是「编队层曾经有不透明全屏部件」这件事
	 * 一旦发生就永远成立：Close 退回任务地图后 cover_layer 仍卡在 2，
	 * ctrl 0/1 被 `parts_is_visible()` 全部滤掉 → 整屏黑。
	 * 判据：`XSYS4_CTRL_DBG=1` 下 cover_layer 应随编队层隐藏回落到 0/1。 */
	memset(ctrl_stack.opaque, 0, sizeof(ctrl_stack.opaque));
	struct parts *p;
	TAILQ_FOREACH(p, &parts_list, parts_list_entry) {
		if (!p->global.show)
			continue;
		int c = p->controller_no;
		if (c < 0 || c >= PARTS_CONTROLLER_STACK_MAX)
			continue;
		if (!ctrl_stack.show[c])
			continue;
		if (p->global.alpha < 250)
			continue;
		struct parts_state *st = &p->states[p->state];
		if (st->common.w < vw || st->common.h < vh)
			continue;
		/* [D342] **纯矩形检测件 / 未初始化件不绘制，不能当"视觉地板"。**
		 *
		 * `nc1` 实测：战斗里 ctrl=4 的 `900000405`/`406` 是 `ck=1` 的
		 * 1920×1080 整屏**点击接收器**（`PARTS_RECT_DETECTION`，一个像素都不画），
		 * 却被算成 opaque，于是 `parts_cover_layer=4`，把 ctrl=3 整层
		 * （敌方面板、技能卡、立绘）全部判为不可见 ——
		 * 关掉 cover（`XSYS4_NO_LAYER_COVER=1`）后这些部件立刻全部显示。
		 *
		 * 判据：只有"能画出不透明像素"的件才算地板。矩形检测件按定义不画，
		 * 未初始化件更没有图形。flat 仍走下面那段 `paint_alpha` 检查。 */
		if (st->type == PARTS_RECT_DETECTION ||
		    st->type == PARTS_UNINITIALIZED ||
		    /* [D343] 布局盒也是纯排版容器，自身不绘制。`d342` 的
		     * `floor by` 统计里 `900000568 st=12 1920x1080 a=255` 出现 2 次 ——
		     * 它每次出现都会把 cover_layer 顶到它那一层，让下面整层闪一下。
		     * 真正的视觉地板应该是 CG / flat 这类能画不透明像素的件。 */
		    st->type == PARTS_LAYOUT_BOX)
			continue;
		/* [D226] 名义 alpha ≠ 画出来的 alpha。flat 的实际不透明度由内部
		 * key 的 alpha 决定（`ctx.alpha = global.alpha * key.alpha`），
		 * 而且纹理缺失时一个像素都不出。光看 global.alpha 会把「已经淡到
		 * 0 的整屏遮罩」当成不透明地板，把下面所有层滤掉 —— 第二次点击
		 * 进战斗回合时整屏变黑就是这个形状（H038/H039）。 */
		if (st->type == PARTS_FLAT) {
			int a = parts_flat_max_paint_alpha(&st->flat);
			if ((int)p->global.alpha * a / 255 < 250)
				continue;
		}
		ctrl_stack.opaque[c] = true;
	}
	for (int c = 0; c < ctrl_stack.nr_controllers; c++) {
		/* [D128] 不看 show：AIN 弹对话框时会把地图那层 show=0（淡出遮罩），
		 * 此时地图仍然定义了"视觉地板"，它下面的旧场景不该因为地图淡出而
		 * 重新露出来。 */
		if (ctrl_stack.opaque[c])
			top = c;
	}
	/* [D343] **单帧滞回**：`d343b` 的 `floor by` 统计显示 ctrl=7 的
	 * `900000644`/`900000631`（`st=1` CG、1920×1080、a=255）会偶发出现，
	 * 让 `cover_layer` 在 4 与 7 之间逐帧跳 —— 7 时 ctrl 3/4/5/6 全不画，
	 * 于是敌方面板/背景整层闪。它们多半是短命的全屏叠加特效，
	 * 但真实的全屏不透明件（战斗背景 `900000412`）是**持续**的。
	 *
	 * 判据：同一个值连续 3 帧才算生效，单帧尖峰被抹掉。
	 * 代价是切换延迟约 50ms，人眼不可见。
	 * `XSYS4_COVER_HYST=0` 关闭做 A/B。 */
	static int hyst = -1;
	if (hyst < 0) {
		const char *e = getenv("XSYS4_COVER_HYST");
		hyst = !(e && !strcmp(e, "0"));
	}
	if (hyst) {
		static int stable_top = -1, pend = -1, pend_n;
		if (stable_top < 0)
			stable_top = top;
		if (top != stable_top) {
			if (top == pend) {
				pend_n++;
			} else {
				pend = top;
				pend_n = 1;
			}
			if (pend_n >= 3) {
				stable_top = top;
				pend_n = 0;
			} else {
				top = stable_top;
			}
		} else {
			pend = top;
			pend_n = 0;
		}
	}
	parts_cover_layer = top;
	if (getenv("XSYS4_CTRL_DBG")) {
		static int last = -1;
		if (top != last) {
			last = top;
			char buf[256];
			int n = snprintf(buf, sizeof(buf), "[ctrl] cover_layer=%d (view %dx%d)",
					 top, vw, vh);
			for (int c = 0; c < ctrl_stack.nr_controllers && n < (int)sizeof(buf) - 20; c++)
				n += snprintf(buf + n, sizeof(buf) - n, " L%d[o%d s%d]",
					      c, ctrl_stack.opaque[c], ctrl_stack.show[c]);
			NOTICE("%s", buf);
			/* [D226] 谁把地板抬上去的：只列抬升层里铺满视图的那几个部件。
			 * 没有这一行，"cover 从 7 跳到 10"只能靠翻全量部件表去猜。 */
			int shown = 0;
			struct parts *q;
			TAILQ_FOREACH(q, &parts_list, parts_list_entry) {
				if (shown >= 3 || q->controller_no != top || !q->global.show)
					continue;
				struct parts_state *qs = &q->states[q->state];
				if (q->global.alpha < 250 || qs->common.w < vw || qs->common.h < vh)
					continue;
				NOTICE("[ctrl]   floor by no=%d st=%d %dx%d pos=(%d,%d) a=%d flatmax=%d",
				       q->no, (int)qs->type, qs->common.w, qs->common.h,
				       q->global.pos.x, q->global.pos.y, (int)q->global.alpha,
				       qs->type == PARTS_FLAT
					       ? parts_flat_max_paint_alpha(&qs->flat) : -1);
				if (qs->type == PARTS_FLAT) {
					size_t cgn = 0;
					for (size_t i = 0; i < qs->flat.nr_libraries; i++)
						if (qs->flat.textures[i].handle)
							cgn++;
					NOTICE("[ctrl]   floor flat loaded=%d root=%d hdr=%dx%d libs=%zu cg=%zu",
					       qs->flat.flat != NULL, qs->flat.root_state,
					       qs->flat.flat ? qs->flat.flat->hdr.width : -1,
					       qs->flat.flat ? qs->flat.flat->hdr.height : -1,
					       qs->flat.nr_libraries, cgn);
				}
				shown++;
			}
		}
	}
}

void PE_Update(int passed_time, bool message_window_show)
{
	if (getenv("XSYS4_UPD_DBG")) {
		static int n = 0;
		if (n++ < 12) NOTICE("[upd] PE_Update t=%d", passed_time);
	}
	parts_message_window_show = message_window_show;
	PE_UpdateComponent(passed_time);
	audio_update();
	parts_update_animation(passed_time);
	PE_UpdateInputState(passed_time);
	parts_render_update();
}

/* 把每个部件的动画时钟（flat / animation / flash）推进
 * `passed_time` 毫秒。
 *
 * 注意这里只推时钟：PE_Update() 还会重跑
 * PE_UpdateComponent() 和 parts_render_update()，
 * 而那会让场景渲染器已经准备好的精灵失效。
 * 只需要「时间在走」的调用方（VM 帧泵，见 vm_frame_pump）用这个。 */
void parts_advance_animation(int passed_time)
{
	/* [D128] 图层遮挡的判定跟着这里重算：AIN 在 WaitForClick 里自旋时根本
	 * 到不了 PartsEngine.Update，而遮挡必须在每次绘制前是最新的。 */
	parts_update_cover_layer();
	if (passed_time > 0)
		parts_update_animation(passed_time);
}

void PE_UpdateParts(int passed_time, possibly_unused bool is_skip, bool message_window_show)
{
	if (getenv("XSYS4_UPD_DBG")) {
		static int n = 0;
		if (n++ < 12) NOTICE("[upd] PE_UpdateParts t=%d", passed_time);
	}
	parts_message_window_show = message_window_show;
	audio_update();
	parts_update_animation(passed_time);
	parts_render_update();
}

void PE_SetDelegateIndex(int parts_no, int delegate_index)
{
	/* [D320] 部件 → 委托索引的绑定点了名才能查。
	 * `ReplayMemoDialog` 的「過後查看」(900000574) 点击**捕获成功**
	 * （`[inp] EndInput clicked=900000574`）却毫无反应，而派发侧的
	 * `CPartsFunctionSet` 里 `m1=parts` 与 `m7=<MouseLClickEvent>` 都是空的。
	 * 这条日志回答「AIN 到底有没有给 574 绑过 delegate_index」。 */
	if (getenv("XSYS4_DGIDX_DBG"))
		NOTICE("[dgidx] set parts=%d di=%d", parts_no, delegate_index);
	/* [D334] 只对回想对话框段打 AIN 调用栈，回答「是谁在配置这些按钮」。
	 * 限 3 次：一次跑测里这段会被设很多遍，栈会把日志冲爆。 */
	if (getenv("XSYS4_DGIDX_DBG") && parts_no >= 900000560 &&
	    parts_no <= 900000600) {
		static int nstack;
		if (nstack++ < 3)
			vm_stack_trace();
	}
	/* [D335] 部件不在就记账，别用 parts_get 造空壳（见 pe_pending_di_remember）。 */
	struct parts *p = parts_try_get(parts_no);
	if (p) {
		p->delegate_index = delegate_index;
	} else {
		pe_pending_di_remember(parts_no, delegate_index);
		if (getenv("XSYS4_DGIDX_DBG"))
			NOTICE("[dgidx] 部件 %d 尚不存在，记账待补 di=%d",
			       parts_no, delegate_index);
	}
}

/* [D341] 部件 `parts_no` 的命中盒是否覆盖 (x,y)（已经补上父的 global 偏移）。
 *
 * 用途：`LargeSkillButton` 的卡片点击区不参与引擎命中（`ck=0`、`pc=1`），
 * 所以只能用坐标反查「点在哪个技能卡里」。与 `PE_FindClickableAt` 用同一套
 * 几何口径 —— 两者不一致时会出"改派到了别的件"这种诡异结果。 */
bool PE_PartsCovers(int parts_no, int x, int y)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p)
		return false;
	Rectangle hb = p->states[PARTS_STATE_DEFAULT].common.hitbox;
	if (p->parent) {
		hb.x += p->parent->global.pos.x;
		hb.y += p->parent->global.pos.y;
	}
	if (hb.w <= 0 || hb.h <= 0)
		return false;
	return x >= hb.x && x < hb.x + hb.w && y >= hb.y && y < hb.y + hb.h;
}

/* [D341] `root_no` 的**子树**里有没有部件覆盖 (x,y)。
 *
 * 技能卡的按钮对象（`LargeSkillButton.m8`）本身是 0×0 的布局盒
 * （`sk12` 实测 `st=12 w=0 h=0 hb=(740,888,0,0)`），真正有几何的是它的子件，
 * 所以「点没点中这张卡」只能按子树判断。
 * 遍历全表但在上溯之前先用便宜的矩形判定过滤，覆盖点通常只有几十个。 */
bool PE_SubtreeCovers(int root_no, int x, int y)
{
	if (root_no <= 0)
		return false;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		if (!PE_PartsCovers(p->no, x, y))
			continue;
		if (PE_PartsIsUnder(p->no, root_no))
			return true;
	}
	return false;
}

/* [D434] `root_no` 子树里所有「有图形尺寸」的件的**并集包围盒**。
 *
 * 为什么需要：技能卡的 `m7`/`m8` 是 0x0 的布局锚点（实测 `hb=(740,884,0,0)`），
 * 光看它们不知道卡片画在哪、更不知道该点哪个像素。`PE_SubtreeCovers` 只能回答
 * 「这个点算不算在卡里」，回答不了「卡在哪」。点不中时缺的正是后者。
 * 与 `PE_PartsCovers` 用同一套坐标口径（补父的 global 偏移）。 */
bool PE_SubtreeBounds(int root_no, int *x, int *y, int *w, int *h, int *n)
{
	if (root_no <= 0)
		return false;
	int x0 = 0x7fffffff, y0 = 0x7fffffff;
	int x1 = -0x7fffffff, y1 = -0x7fffffff, cnt = 0;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		if (!PE_PartsIsUnder(p->no, root_no))
			continue;
		Rectangle hb = p->states[PARTS_STATE_DEFAULT].common.hitbox;
		if (p->parent) {
			hb.x += p->parent->global.pos.x;
			hb.y += p->parent->global.pos.y;
		}
		if (hb.w <= 0 || hb.h <= 0)
			continue;
		cnt++;
		if (hb.x < x0)
			x0 = hb.x;
		if (hb.y < y0)
			y0 = hb.y;
		if (hb.x + hb.w > x1)
			x1 = hb.x + hb.w;
		if (hb.y + hb.h > y1)
			y1 = hb.y + hb.h;
	}
	if (!cnt)
		return false;
	if (x) *x = x0;
	if (y) *y = y0;
	if (w) *w = x1 - x0;
	if (h) *h = y1 - y0;
	if (n) *n = cnt;
	return true;
}

/* [D347 诊断] 打印所有覆盖 (x,y) 且有图形尺寸的部件（不限可见性）。
 * 技能卡点不中时用它看"到底有哪些件压在这个点上"。 */
void PE_DebugCovers(int x, int y)
{
	struct parts *p;
	int n = 0;
	PARTS_LIST_FOREACH(p) {
		if (p->states[PARTS_STATE_DEFAULT].common.w <= 0 ||
		    p->states[PARTS_STATE_DEFAULT].common.h <= 0)
			continue;
		if (!PE_PartsCovers(p->no, x, y))
			continue;
		if (n++ >= 16)
			break;
		NOTICE("[cov] no=%d ctrl=%d z=%d a=%d vis=%d ck=%d pc=%d uc=%s",
		       p->no, p->controller_no, p->global.z,
		       (int)p->global.alpha, parts_is_visible(p) ? 1 : 0,
		       p->clickable, p->pass_cursor,
		       p->uc_object ? p->uc_object : "-");
	}
	if (!n)
		NOTICE("[cov] (%d,%d) 没有任何有尺寸的件覆盖", x, y);
}

/* [D338 诊断] 打印单个部件的输入相关字段（`[pev]` 用）。 */
/* [D471] 收集 (x,y) 处 z 最高的 N 个可见件。
 *
 * 「点不中」在兰斯10 里几乎总是同一个形状：真正想要的按钮 z=2010，
 * 而某个面板/遮罩 z=20121、甚至 z=0x40000000 盖在上面且 `pass_cursor=0`
 * 吃掉光标。以前只能靠 `XSYS4_CAP_DBG` 看一堆部件号猜谁是谁 ——
 * 这个接口直接把「最顶的 5 个」连同祖先链打出来。
 * 坐标口径与 `PE_PartsCovers` 完全一致（hitbox + 父 global 偏移）。 */
void PE_DebugTopAt(int x, int y, int *nos, int *zs, int *n)
{
	int cnt = 0;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		Rectangle hb = p->states[PARTS_STATE_DEFAULT].common.hitbox;
		if (p->parent) {
			hb.x += p->parent->global.pos.x;
			hb.y += p->parent->global.pos.y;
		}
		if (hb.w <= 0 || hb.h <= 0)
			continue;
		if (x < hb.x || x >= hb.x + hb.w || y < hb.y || y >= hb.y + hb.h)
			continue;
		if (!parts_is_visible(p))
			continue;
		if (p->global.alpha <= 0)
			continue;
		/* 插入排序，只保留前 5 个（z 降序）。 */
		int z = p->global.z;
		int i = cnt < 5 ? cnt : 4;
		if (cnt < 5)
			cnt++;
		else if (z <= zs[4])
			continue;
		for (; i > 0 && zs[i - 1] < z; i--) {
			zs[i] = zs[i - 1];
			nos[i] = nos[i - 1];
		}
		zs[i] = z;
		nos[i] = p->no;
	}
	*n = cnt;
}

void PE_DebugPartsInfo(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p) {
		NOTICE("[pev]   no=%d <不存在>", parts_no);
		return;
	}
	const struct parts_state *st = &p->states[PARTS_STATE_DEFAULT];
	Rectangle hb = st->common.hitbox;
	if (p->parent) {
		hb.x += p->parent->global.pos.x;
		hb.y += p->parent->global.pos.y;
	}
	/* [D469] 同时打出 local.pos / global.pos 与**未加父偏移**的原始 hitbox。
	 *
	 * 起因：`ButtonTurnEnd` 与一个 z=20246 的菜单按钮报出几乎完全重叠的
	 * hb，导致坐标点击永远先落到菜单上。要判断「到底是不是真重叠」，
	 * 必须先确定 hitbox 的坐标系：
	 *   - `global.pos` 在 `parts_update_global_pos()` 里是
	 *     `parent.global.pos + local.pos`，即**已经是绝对坐标**；
	 *   - 而 `PE_PartsCovers` / 这里都在 hitbox 上再加一次
	 *     `p->parent->global.pos` —— 两者不可能同时成立。
	 * 只有把 raw hitbox 与 gpos 并排打出来，才能判断哪一边是对的。 */
	NOTICE("[pev]   no=%d st=%d w=%d h=%d a=%d show=%d ck=%d pc=%d "
	       "z=%d ctrl=%d vis=%d hb=(%d,%d,%d,%d) raw=(%d,%d) "
	       "gpos=(%d,%d) lpos=(%d,%d) par=%d",
	       parts_no, (int)st->type, st->common.w, st->common.h,
	       (int)p->global.alpha, p->global.show, p->clickable, p->pass_cursor,
	       p->global.z, p->controller_no, parts_is_visible(p) ? 1 : 0,
	       hb.x, hb.y, hb.w, hb.h,
	       st->common.hitbox.x, st->common.hitbox.y,
	       p->global.pos.x, p->global.pos.y,
	       p->local.pos.x, p->local.pos.y,
	       p->parent ? p->parent->no : -1);
	/* [D470] 祖先链：判断「这个件属于哪个活动/是不是上个场景的残留」。
	 * 部件号段会复用，只看自己的号分不清；沿 parent 打到根，
	 * 配合 z 与 show 才能定性。 */
	{
		struct parts *a = p->parent;
		for (int d = 0; a && d < 6; d++, a = a->parent)
			NOTICE("[pev]     ↑%d par=%d z=%d show=%d vis=%d lpos=(%d,%d) "
			       "gpos=(%d,%d) w=%d h=%d",
			       d + 1, a->no, a->global.z, a->global.show,
			       parts_is_visible(a) ? 1 : 0,
			       a->local.pos.x, a->local.pos.y,
			       a->global.pos.x, a->global.pos.y,
			       a->states[PARTS_STATE_DEFAULT].common.w,
			       a->states[PARTS_STATE_DEFAULT].common.h);
	}
}

/* [D338] 覆盖 (x,y) 的**最上层局部可点击件**；没有则返回 0。
 *
 * 用途：战斗里点 (740,886) 时命中记录的是 `900000818`（角色卡内部一个
 * 全透明装饰层，`ck=0`），而角色卡本身 `900000813` 是 `ck=1` 的矩形检测件、
 * 与它几何重叠。命中判据是全局的、影响面大（一改就波及标题/ADV），
 * 所以不动命中，只在**派发端**按坐标补一次"找真正可点的那个件"。
 *
 * 规则：`clickable=1`、可见、命中盒覆盖 (x,y)、且 hb 不是整屏（整屏是
 * "点空白处"的兜底层），取 `global.z` 最大的那个。 */
int PE_FindClickableAt(int x, int y)
{
	struct parts *p, *best = NULL;
	PARTS_LIST_FOREACH(p) {
		/* [D339] 候选条件放宽：原来只认 `clickable=1`，但战斗的技能卡
		 * 并不是 `Parts_SetClickable` 设过的（`bt10` 的部件快照里那块
		 * 区域 ck 全是 0），于是兜底恒返回 0、点了没反应。
		 * 现在接受「`clickable=1` 或不透光标（`pass_cursor=0`）」——
		 * 前者是显式按钮，后者是游戏自己标成"吃掉光标"的可交互件。
		 * 两者都要求**有图形尺寸**，避免又选中纯检测框/空壳。 */
		if (!p->clickable && p->pass_cursor)
			continue;
		if (p->states[PARTS_STATE_DEFAULT].common.w <= 0 ||
		    p->states[PARTS_STATE_DEFAULT].common.h <= 0)
			continue;
		if (!parts_is_visible(p))
			continue;
		/* [D341] 完全透明的件不能当兜底目标。
		 * `bt10`/`sk8` 实测：点技能卡 (1102,871) 时兜底选中了
		 * `900000569` —— 那是**回想对话框的面板**（862x226 @ (960,761)），
		 * 对话早已关闭、`global.alpha=0`（一个像素不画），
		 * 但 `parts_is_visible()` 只看 show/layer/cover，不看 alpha，
		 * 于是它挡住了真正的技能卡，派发到它身上后没有任何按钮对象。 */
		if (p->global.alpha <= 0)
			continue;
		Rectangle hb = p->states[PARTS_STATE_DEFAULT].common.hitbox;
		if (p->parent) {
			hb.x += p->parent->global.pos.x;
			hb.y += p->parent->global.pos.y;
		}
		if (x < hb.x || x >= hb.x + hb.w || y < hb.y || y >= hb.y + hb.h)
			continue;
		if (hb.w >= gfx_view_width() && hb.h >= gfx_view_height())
			continue;
		if (!best || p->global.z > best->global.z)
			best = p;
	}
	return best ? best->no : 0;
}

/* [D337] `child_no` 是不是 `ancestor_no` 自己或它的子孙部件。
 *
 * 用途：按钮部件常常 `光标选中透過=1`（点击穿过它落到父容器），于是引擎
 * 捕获到的是**容器**的部件号而不是按钮自己的。派发时要把「点在容器上」
 * 也算作点中了容器里的按钮。深度上限 12 防环。 */
bool PE_PartsIsUnder(int child_no, int ancestor_no)
{
	if (child_no <= 0 || ancestor_no <= 0)
		return false;
	struct parts *p = parts_try_get(child_no);
	for (int d = 0; p && d < 12; d++) {
		if (p->no == ancestor_no)
			return true;
		p = p->parent;
	}
	return false;
}

int PE_GetDelegateIndex(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	if (getenv("XSYS4_DGIDX_DBG") && parts_no >= 900000560)
		NOTICE("[dgidx] get parts=%d -> %d", parts_no,
		       parts ? parts->delegate_index : -1);
	return parts ? parts->delegate_index : -1;
}

/* [D317] 名字前缀比较：AIN 的 CG 名是 SJIS，`display_sjis0` 转 UTF-8 后再比。
 * 注意它用**单缓冲**（每次调用会 free 上一次），所以只能立即用完就丢。 */
static bool pe_cg_name_prefix(struct parts_cg *cg, const char *utf8_prefix)
{
	if (!cg || !cg->name || !cg->name->text || !*cg->name->text)
		return false;
	const char *u = display_sjis0(cg->name->text);
	if (!u)
		return false;
	return strncmp(u, utf8_prefix, strlen(utf8_prefix)) == 0;
}

/* [D317] 背景换图时，收起同层的「情报地图」覆盖件。
 *
 * 事实链（`D316` 追踪 + `cgset` 调用栈）：
 * ① `400/408/410`（`地图／世界地图基本`、`地图／赫尔曼基本`、`シス／地图框基本`）
 *    由 AIN 的 `AdvInfoMapCg@InitParts` 创建，是**全屏**情报地图；
 * ② 与之同时，`413`（`背景／宇航船／通道`）由 `AdvBackground@Create` 创建并显示；
 * ③ 之后 16 段剧情里，AIN **对 400/408 一次 `SetShow` 都没调**，也不换图、
 *    不释放、不迁层（`[track]` 只有创建那一条），只反复开关地图层（id=7）——
 *    而这两件在 ADV 层（id=9），关不到；
 * ④ `XSYS4_FORCE_HIDE=900000400,900000408` 一隐藏，画面立刻是走廊 + 对话框。
 *
 * 也就是说：**背景换到新的 `背景／*` 时，旧的全屏地图覆盖件必须收起**，而这条
 * 收起在 AIN 侧不存在（它依赖「整层收起」，但两者被放进了同一层）。这里补上
 * 这条收敛：只在「根级 `背景／*` 部件换图」时触发，只动**同层、可见、
 * `地图／*` 前缀**的部件，别的什么都不碰。
 *
 * `XSYS4_BG_SWEEP_MAP=0` 关闭。 */
static void pe_sweep_map_overlay(struct parts *bg)
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("XSYS4_BG_SWEEP_MAP");
		on = !(e && strcmp(e, "0") == 0);
	}
	if (!on || !bg || bg->parent)
		return;
	if (!parts_state_valid(0) ||
	    bg->states[0].type != PARTS_CG ||
	    !pe_cg_name_prefix(&bg->states[0].cg, "背景"))
		return;
	/* 收起的是命中部件的**根**，不是它自己。
	 *
	 * 实测（`bgT`）：只把 `地图／*` 那 4 个置 0 后画面**仍是地图** —— 地图图像
	 * 挂在根部件 `900000390` 上（`地图／ベース`，CG 按**序号**设置，`cg.name`
	 * 为空，名字前缀匹配不到它）。同一层里有两个独立根：
	 * `391`(z=1500) → 任务信息窗（393..399），`390`(z=4200) → 地图组
	 * （400/401/…/410/411）。所以按「根」收起，既覆盖到无名的基板，也
	 * 一点不碰信息窗。 */
	int hidden = 0;
	struct parts *p;
	TAILQ_FOREACH(p, &parts_list, parts_list_entry) {
		if (p == bg || p->controller_no != bg->controller_no)
			continue;
		if (!p->local.show || p->states[0].type != PARTS_CG)
			continue;
		if (!pe_cg_name_prefix(&p->states[0].cg, "地图"))
			continue;
		struct parts *root = p;
		while (root->parent)
			root = root->parent;
		if (root == bg || !root->local.show)
			continue;
		parts_set_show(root, false);
		hidden++;
	}
	if (hidden && getenv("XSYS4_CTRL_DBG"))
		NOTICE("[bg] 背景换图 -> 收起同层地图覆盖件 %d 个（bg=%d）",
		       hidden, bg->no);
}

bool PE_SetPartsCG(int parts_no, struct string *cg_name, int sprite_deform, int state)
{
	/* [D315] 全量探针：回答「AIN 到底有没有给某个部件换过图」。
	 *
	 * [BG-LAYER] 已确认：`FORCE_HIDE=900000400,900000408` 一隐藏画面就对，
	 * 说明这两张地图基底 CG 从未被 AIN 收起。剩下的问题是 —— AIN 是**想换图
	 * 但没换成**，还是**根本没碰过它们**。前者查 `parts_cg_set` 的前置条件，
	 * 后者要回到活动卸载路径。这两条路的修法完全不同，不能靠猜。 */
	if (getenv("XSYS4_CGSET_DBG")) {
		static int n;
		if (n++ < 600)
			NOTICE("[cgset] no=%d st=%d name='%s'", parts_no, state,
			       cg_name && cg_name->text ? display_sjis0(cg_name->text)
			                                : "(null)");
		/* [D316] 背景组（390..430）额外打 AIN 调用栈：要回答「切背景那一步
		 * 是哪个 AIN 函数做的、它前后还调了什么」。只打这一段，否则 213 条
		 * 每条一栈会把日志冲爆。 */
		if (parts_no >= 900000390 && parts_no <= 900000430)
			vm_stack_trace();
	}
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts->sprite_deform = sprite_deform;
	if (!cg_name || *(cg_name->text) == '\0') {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct parts_cg *cg = parts_get_cg(parts, state);
	bool ok = parts_cg_set(parts, cg, cg_name);
	if (ok)
		pe_sweep_map_overlay(parts);
	return ok;
}

/* AIN PartsEngine：bool SetPartsCGThread(int Number, string CGName, int State)。
 *
 * 异步设定 CG 的入口。参数比同步版少一个 sprite_deform：原版异步路径只
 * 按名字换图，形变沿用部件当前值。我们不做后台线程（见
 * PE_BuildPartsConstructionProcessThread 的理由），直接走同步版，形变显式
 * 取当前值，避免顺手把已设好的 deform 清成 0。
 *
 * [D164] 谓词版 Array.Find 修好后，QuestMapBoxView@InitBoxLight 第一次
 * 真的走到这里；这个导出之前完全没注册，一进就是 Unimplemented HLL
 * ERROR，把启动画面卡在 WaitForClick，整局只加载 19 个活动。 */
bool PE_SetPartsCGThread(int parts_no, struct string *cg_name, int state)
{
	struct parts *p = parts_try_get(parts_no);
	return PE_SetPartsCG(parts_no, cg_name, p ? p->sprite_deform : 0, state);
}

/* AIN PartsEngine：bool IsThreadLoading(int Number, int State)。
 *
 * 所有 *Thread 导出我们都同步做完再返回（见上面两处注释），所以"还在
 * 后台加载"永远为假。返回 false 让调用方的轮询循环第一轮就退出。 */
bool PE_IsThreadLoading(int parts_no, int state)
{
	return false;
}

bool PE_SetPartsCG_by_index(int parts_no, int cg_no, int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts->sprite_deform = sprite_deform;
	if (!cg_no) {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct parts_cg *cg = parts_get_cg(parts, state);
	return parts_cg_set_by_index(parts, cg, cg_no);
}

// XXX：《兰斯 任务》（Rance Quest）
bool PE_SetPartsCG_by_string_index(int parts_no, struct string *cg_name,
		int sprite_deform, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	parts->sprite_deform = sprite_deform;
	if (!cg_name) {
		parts_state_reset(&parts->states[state], PARTS_CG);
		parts_dirty(parts);
		return true;
	}

	struct parts_cg *cg = parts_get_cg(parts, state);
	int cg_no = atoi(cg_name->text);
	if (cg_no) {
		if (!parts_cg_set_by_index(parts, cg, atoi(cg_name->text)))
			return false;
		cg->name = string_ref(cg_name);
		return true;
	} else if (!memcmp(cg_name->text, "<save>SaveData\\", 15)) {
		char *path = savedir_path(cg_name->text + 15);
		bool result = _parts_cg_set(parts, cg, cg_load_file(path), 0, string_ref(cg_name));
		free(path);
		return result;
	} else {
		VM_ERROR("Invalid CG name: %s", display_sjis0(cg_name->text));
	}
}

void PE_GetPartsCGName(int parts_no, struct string **cg_name, int state)
{
	if (!parts_state_valid(--state))
		return;
	struct parts_cg *cg = parts_get_cg(parts_get(parts_no), state);
	if (cg->name) {
		if (*cg_name)
			free_string(*cg_name);
		*cg_name = string_dup(cg->name);
	}
}

bool PE_SetPartsCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_cg *cg = parts_get_cg(parts, state);
	parts_set_surface_area(parts, &cg->common, x, y, w, h);
	return true;
}

void PE_GetPartsCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
{
	if (!parts_state_valid(--state))
		return;

	struct parts_cg *cg = parts_get_cg(parts_get(parts_no), state);
	*x = cg->common.surface_area.x;
	*y = cg->common.surface_area.y;
	*w = cg->common.surface_area.w;
	*h = cg->common.surface_area.h;
}

int PE_GetPartsCGNumber(int parts_no, int state)
{
	if (!parts_state_valid(--state)) {
		return false;
	}

	return parts_get_cg(parts_get(parts_no), state)->no;
}

static bool _parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		int start_no, int nr_frames, int frame_time,
		struct cg *(*load_cg)(int no, void *data), void *data)
{
	int w = 0, h = 0;
	Texture *frames = xcalloc(nr_frames, sizeof(Texture));
	for (int i = 0; i < nr_frames; i++) {
		struct cg *cg = load_cg(start_no + i, data);
		if (!cg) {
			for (int j = 0; j < i; j++) {
				gfx_delete_texture(&frames[j]);
			}
			free(frames);
			return false;
		}
		gfx_init_texture_with_cg(&frames[i], cg);
		w = max(w, cg->metrics.w);
		h = max(h, cg->metrics.h);
		cg_free(cg);
	}

	parts_set_dims(parts, &anim->common, w, h);
	free(anim->frames);
	anim->start_no = start_no;
	anim->frames = frames;
	anim->nr_frames = nr_frames;
	anim->frame_time = frame_time;
	anim->elapsed = 0;
	anim->current_frame = 0;
	anim->common.texture = frames[0];
	return true;
}

static struct cg *load_loop_cg_by_index(int no, void *_)
{
	return asset_cg_load(no);
}

bool parts_animation_set_cg_by_index(struct parts *parts, struct parts_animation *anim,
		int cg_no, int nr_frames, int frame_time)
{
	return _parts_animation_set_cg(parts, anim, cg_no, nr_frames, frame_time,
			load_loop_cg_by_index, NULL);
}

bool PE_SetLoopCG_by_index(int parts_no, int cg_no, int nr_frames, int frame_time, int state)
{
	if (!parts_state_valid(--state))
		return false;
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	return parts_animation_set_cg_by_index(parts, anim, cg_no, nr_frames, frame_time);
}

static struct cg *load_loop_cg_by_name(int no, void *data)
{
	int unused_no;
	struct string *cg_name = string_format((struct string*)data, (union vm_value){.i=no}, STRFMT_INT);
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &unused_no);
	free_string(cg_name);
	return cg;
}

bool parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		struct string *cg_name, int start_no, int nr_frames, int frame_time)
{
	bool r = _parts_animation_set_cg(parts, anim, start_no, nr_frames, frame_time,
			load_loop_cg_by_name, cg_name);
	if (r) {
		if (anim->cg_name)
			free_string(anim->cg_name);
		anim->cg_name = string_dup(cg_name);
	}
	return r;
}

bool PE_SetLoopCG(int parts_no, struct string *cg_name, int start_no, int nr_frames,
	int frame_time, int state)
{
	if (!parts_state_valid(--state))
		return false;
	if (nr_frames <= 0 || nr_frames > 10000) {
		WARNING("Invalid frame count: %d", nr_frames);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	return parts_animation_set_cg(parts, anim, cg_name, start_no, nr_frames, frame_time);
}

bool PE_SetLoopCGSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state)) {
		WARNING("Invalid parts state: %d", state);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	struct parts_animation *anim = parts_get_animation(parts, state);
	parts_set_surface_area(parts, &anim->common, x, y, w, h);
	return true;
}

static void _parts_set_gauge_cg(struct parts *parts, struct parts_gauge *g, struct cg *cg)
{
	gfx_init_texture_with_cg(&g->cg, cg);
	gfx_init_texture_with_cg(&g->common.texture, cg);

	gfx_init_texture_rgba(&g->common.texture, g->cg.w, g->cg.h, (SDL_Color){0,0,0,255});
	gfx_copy_with_alpha_map(&g->common.texture, 0, 0, &g->cg, 0, 0, g->cg.w, g->cg.h);

	parts_set_dims(parts, &g->common, g->cg.w, g->cg.h);

	parts_dirty(parts);
}

bool parts_gauge_set_cg(struct parts *parts, struct parts_gauge *g, struct string *cg_name)
{
	int cg_no;
	struct cg *cg = asset_cg_load_by_name(cg_name->text, &cg_no);
	if (!cg)
		return false;
	_parts_set_gauge_cg(parts, g, cg);
	cg_free(cg);
	g->cg_no = cg_no;
	return true;
}

bool parts_gauge_set_cg_by_index(struct parts *parts, struct parts_gauge *g, int cg_no)
{
	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return false;
	_parts_set_gauge_cg(parts, g, cg);
	cg_free(cg);
	g->cg_no = cg_no;
	return true;
}

bool PE_SetHGaugeCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	return parts_gauge_set_cg(parts, g, cg_name);
}

bool PE_SetHGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	return parts_gauge_set_cg_by_index(parts, g, cg_no);
}

bool PE_SetVGaugeCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	return parts_gauge_set_cg(parts, g, cg_name);
}

bool PE_SetVGaugeCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	return parts_gauge_set_cg_by_index(parts, g, cg_no);
}

void parts_hgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate)
{
	if (!g->common.texture.handle) {
		WARNING("HGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.w;
	gfx_copy_with_alpha_map(&g->common.texture, 0, 0, &g->cg, 0, 0, pixels, g->cg.h);
	gfx_fill_amap(&g->common.texture, pixels, 0, g->cg.w - pixels, g->cg.h, 0);
	parts_dirty(parts);
	g->rate = rate;
}

void parts_vgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate)
{
	if (!g->common.texture.handle) {
		WARNING("VGauge texture uninitialized");
		return;
	}
	int pixels = rate * g->cg.h;
	gfx_copy_with_alpha_map(&g->common.texture, 0, pixels, &g->cg, 0, pixels, g->cg.w, g->cg.h - pixels);
	gfx_fill_amap(&g->common.texture, 0, 0, g->cg.w, pixels, 0);
	parts_dirty(parts);
	g->rate = rate;
}

bool PE_SetHGaugeRate(int parts_no, float numerator, float denominator, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	parts_hgauge_set_rate(parts, g, numerator/denominator);
	return true;
}

/* [D341] HP/VP 条的分子/分母 getter。
 *
 * `struct parts_gauge` 只存了 `rate`（0..1 浮点），原始的分子分母没保存过
 * （`PE_SetHGaugeRate` 一进来就做除法）。所以这里用固定分母 1000 反推分子：
 * 比值是对的，绝对值是约定值。AIN 拿它做「条要画多长 / 是否归零」——
 * 两者都只看比例，不依赖真实 HP 数字（HP 另有 `HpBarEnemy` 那套数据）。
 * 若将来发现它被用来显示"1200/1200"这种文本，就得给 gauge 补真实字段。 */
int PE_GetPartsHGaugeNumerator(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *p = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(p, state);
	return (int)(g->rate * 1000.0f + 0.5f);
}

int PE_GetPartsHGaugeDenominator(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	return 1000;
}

int PE_GetPartsVGaugeNumerator(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *p = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(p, state);
	return (int)(g->rate * 1000.0f + 0.5f);
}

int PE_GetPartsVGaugeDenominator(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	return 1000;
}

bool PE_SetHGaugeRate_int(int parts_no, int numerator, int denominator, int state)
{
	return PE_SetHGaugeRate(parts_no, numerator, denominator, state);
}

bool PE_SetVGaugeRate(int parts_no, float numerator, float denominator, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	parts_vgauge_set_rate(parts, g, (float)numerator/(float)denominator);
	return true;
}

bool PE_SetVGaugeRate_int(int parts_no, int numerator, int denominator, int state)
{
	return PE_SetVGaugeRate(parts_no, numerator, denominator, state);
}

bool PE_SetHGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_hgauge(parts, state);
	parts_set_surface_area(parts, &g->common, x, y, w, h);
	return true;
}

bool PE_SetVGaugeSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_gauge *g = parts_get_vgauge(parts, state);
	parts_set_surface_area(parts, &g->common, x, y, w, h);
	return true;
}

bool PE_SetNumeralCG(int parts_no, struct string *cg_name, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_separate_string(cg_name);
	return true;
}

bool PE_SetNumeralCG_by_index(int parts_no, int cg_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_separate(cg_no);
	return true;
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList_by_index(int parts_no, int cg_no,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	if (!asset_exists(ASSET_CG, cg_no))
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_combined(cg_no, w);
	return true;
}

bool PE_SetNumeralLinkedCGNumberWidthWidthList(int parts_no, struct string *cg_name,
		int w0, int w1, int w2, int w3, int w4, int w5, int w6, int w7, int w8,
		int w9, int w_minus, int w_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	int no;
	if (!asset_exists_by_name(ASSET_CG, cg_name->text, &no))
		return false;

	int w[12] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w_minus, w_comma };
	struct parts_numeral *n = parts_get_numeral(parts_get(parts_no), state);
	n->font_no = parts_load_numeral_font_combined(no, w);
	return true;
}

bool PE_SetNumeralNumber(int parts_no, int n, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *numeral = parts_get_numeral(parts, state);
	return parts_numeral_set_number(parts, numeral, n);
}

/* AIN PartsEngine：int GetNumeralNumber(int Number, int State)。
 *
 * 读回 SetNumeralNumber 设进去的值。只读，不改状态：状态还不是 numeral
 * 时返回 0，而不是像 parts_get_numeral 那样顺手把该状态重建成 numeral
 * （getter 不该有副作用，否则"查询"会把一个 CG 状态换成数字状态）。
 *
 * [D164] 谓词版 Array.Find 修好后，任务地图的 box 视图第一次真的被建
 * 出来；QuestMapBoxView@InitBox 系列要读数字贴图的当前值来排布。 */
int PE_GetNumeralNumber(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_NUMERAL)
		return 0;
	return parts->states[state].num.have_num ? parts->states[state].num.num : 0;
}

bool PE_SetNumeralShowComma(int parts_no, bool show_comma, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->show_comma == show_comma)
		return true;

	num->show_comma = show_comma;
	parts_numeral_update(parts, num);
	return true;
}

bool PE_SetNumeralSpace(int parts_no, int space, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->space == space)
		return true;

	num->space = space;
	parts_numeral_update(parts, num);
	return true;
}

bool PE_SetNumeralLength(int parts_no, int length, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *num = parts_get_numeral(parts, state);
	if (num->length == length)
		return true;

	num->length = max(1, length);
	num->have_num = true;
	parts_numeral_update(parts, num);
	return true;
}

bool PE_SetNumeralSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_numeral *n = parts_get_numeral(parts, state);
	parts_set_surface_area(parts, &n->common, x, y, w, h);
	return true;
}

/* [D219] 收集部件树的全部子孙号。parts_release() 原来只把子部件摘链
 * （child->parent = NULL）而不释放，它们仍留在 parts_list 里继续被渲染 ——
 * 对话框关掉后文字层残留（H025 / [LAYER-LEAK]）。先把号收齐再逐个释放，
 * 避免边遍历边摘链造成悬空指针。上限只是保护，正常部件树远小于此。 */
#define RELEASE_SUBTREE_MAX 512
static void parts_collect_descendants(struct parts *p, int *nos, int *n, int cap)
{
	struct parts *child;
	PARTS_FOREACH_CHILD(child, p) {
		if (*n >= cap)
			return;
		nos[(*n)++] = child->no;
		parts_collect_descendants(child, nos, n, cap);
	}
}

void PE_ReleaseParts(int parts_no)
{
	if (getenv("XSYS4_CTRL_DBG")) {
		static int n;
		if (n++ < 300)
			NOTICE("[ctrl] Release parts=%d", parts_no);
	}
	/* 默认生效、=0 回退做 A/B。只作用于 AIN 显式 ReleaseParts 的路径：
	 * PE_RemoveController 自己会遍历整表按 controller 释放，不走这里，
	 * 因此不会破坏它「先存 next 再释放」的遍历。 */
	static int recursive = -1;
	if (recursive < 0) {
		const char *e = getenv("XSYS4_RELEASE_RECURSIVE");
		recursive = e && !strcmp(e, "0") ? 0 : 1;
	}
	if (recursive) {
		struct parts *root = parts_try_get(parts_no);
		if (root) {
			int nos[RELEASE_SUBTREE_MAX];
			int n = 0;
			parts_collect_descendants(root, nos, &n, RELEASE_SUBTREE_MAX);
			parts_release(parts_no);
			for (int i = 0; i < n; i++)
				parts_release(nos[i]);
			if (getenv("XSYS4_CTRL_DBG"))
				NOTICE("[ctrl] Release subtree root=%d descendants=%d",
				       parts_no, n);
			return;
		}
	}
	parts_release(parts_no);
}

void PE_ReleaseAllParts(void)
{
	parts_release_all();
}

void PE_ReleaseAllPartsWithoutSystem(void)
{
	// FIXME：这两者有什么区别？
	parts_release_all();
}

void PE_ReleaseAllWithoutSystem(struct page **erase_number_list)
{
	// 释放所有不属于系统覆盖层控制器的部件
	struct parts *parts = TAILQ_FIRST(&parts_list);
	while (parts) {
		struct parts *next = TAILQ_NEXT(parts, parts_list_entry);
		if (parts->controller_no != PARTS_CONTROLLER_SYSTEM_OVERLAY) {
			*erase_number_list = array_pushback(*erase_number_list,
					(union vm_value){.i = parts->no}, AIN_ARRAY_INT, -1);
			parts_release(parts->no);
		}
		parts = next;
	}

	// 丢掉所有普通控制器，再加一个新的默认控制器。
	ctrl_stack.nr_controllers = 0;
	PE_AddController(-1);
}

void PE_SetPos(int parts_no, int x, int y)
{
	/* [D193] 選択肢按钮的 `SetPos(1438,362)` 调用了但部件仍在 (0,0)，
	 * 需要看清它设的是哪个号 —— 号对不上就说明 `Activity@GetParts`
	 * 返回的不是视觉主体那个部件。 */
	if (getenv("XSYS4_ACT_PART_DBG") && parts_no >= 900000300)
		NOTICE("[pos] SetPos no=%d (%d,%d)", parts_no, x, y);
	parts_set_pos(parts_get(parts_no), (Point){ x, y });
}

void PE_SetZ(int parts_no, int z)
{
	/* [D193] 選択肢按钮画在 (1438,362)、能点、流程能推进，但 global z 只有
	 * 103，被 `QuestMapFrame`(237) 盖住 ⇒ 看不见。要分清是 AIN 就给这么低，
	 * 还是我们的 z 合成算错。 */
	if (getenv("XSYS4_ACT_PART_DBG") && parts_no >= 900000300)
		NOTICE("[z] SetZ no=%d z=%d", parts_no, z);
	parts_set_z(parts_get(parts_no), z);
}

void PE_SetShow(int parts_no, bool show)
{
	/* [D128] AIN 的 SceneStack 用 SetComponentShow(图层号, show) 整层显示/
	 * 隐藏（parts::detail 的"ﾚｲﾔｰ表示設定"就是 CALLHLL 27 99）。以前这里
	 * 一律当部件号处理，parts_get() 还会为图层号凭空造一个部件——于是
	 * 切场景时旧场景整层都留在画面上，新背景又盖不住它。 */
	if (parts_no_is_layer(parts_no)) {
		/* [D129] 小整数是图层 ID，不是栈下标。 */
		int c = PE_get_controller_index(parts_no);
		if (c < 0 || c == PARTS_CONTROLLER_SYSTEM_OVERLAY)
			return;
		ctrl_stack.show[c] = show;
		if (getenv("XSYS4_CTRL_DBG")) {
			/* [D311] 这层里到底有哪些部件。
			 * 起因：剧情要从世界地图切到战舰内部时，AIN 关掉的是
			 * `idx=3`，可地图那几张 CG（`地图／世界地图基本`、`地图／赫尔曼基本`）
			 * 实测挂在 `ctrl=4` —— 关的那层和画图的那层不是同一层。
			 * 只打层号看不出这一点，必须把同层部件号一起打出来。 */
			char buf[400] = "";
			size_t off = 0;
			int n_in_layer = 0, shown_in_layer = 0;
			struct parts *q;
			TAILQ_FOREACH(q, &parts_list, parts_list_entry) {
				if (q->controller_no != c)
					continue;
				n_in_layer++;
				if (q->global.show)
					shown_in_layer++;
				if (off < sizeof(buf) - 24 && n_in_layer <= 40)
					off += snprintf(buf + off, sizeof(buf) - off,
							" %d", q->no);
			}
			NOTICE("[ctrl] layer id=%d idx=%d show=%d fno=%d nr=%d shown_in=%d parts:%s",
			       parts_no, c, show, hll_current_fno(), n_in_layer,
			       shown_in_layer, buf);
		}
		if (getenv("XSYS4_LAYER_TRACE"))
			vm_stack_trace();
		scene_dirty();
		return;
	}
	/* 只记"隐藏"：这是场景切换时图层挂起的动作，量少且正是要看的东西。
	 * 创建期的 show=1 每部件一条，全打会把日志冲掉。 */
	/* [D311] 900000390..900000430 这一段是 ADV 的合成层（地图 CG / 背景 / 立绘 /
	 * 消息框），show 与 hide 都要记：只记 hide 会漏掉「谁又把它显示回来」，
	 * 而本轮正是卡在「hide 过之后画面里 vis 仍是 1」。 */
	if (parts_no >= 900000390 && parts_no <= 900000430 &&
	    getenv("XSYS4_CTRL_DBG")) {
		static int n2;
		struct parts *sp2 = parts_try_get(parts_no);
		if (n2++ < 2000)
			NOTICE("[ctrl] setshow parts=%d show=%d serial=%u", parts_no,
			       (int)show, sp2 ? sp2->serial : 0u);
	}
	if (!show && getenv("XSYS4_CTRL_DBG")) {
		/* [D243] 限流从 200 提到 3000：场景切换阶段本身就能打出 200 条以上，
		 * 旧上限会在战斗装载前耗尽，导致「战斗部件没 hide」这类结论是假阴性。 */
		static int n;
		if (n++ < 3000) {
			/* [D311] 带创建序号：号会被复用，只有序号能说明「这次 hide 的
			 * 是不是后来在画面上那个」。 */
			struct parts *hp = parts_try_get(parts_no);
			NOTICE("[ctrl] hide parts=%d serial=%u", parts_no,
			       hp ? hp->serial : 0u);
		}
		else if (n == 3001)
			NOTICE("[ctrl] hide parts=... (3000 条上限已到，后续不再打印)");
	}
	parts_set_show(parts_get(parts_no), show);
}

void PE_SetAlpha(int parts_no, int alpha)
{
	/* [D187] 探针：卡片合成完是 a=255，dump 时却 a=0 —— 要回答
	 * 「谁把它设成 0、之后靠什么恢复」。 */
	/* [D277] 补 fno：不带调用者时，同一个部件号被不同活动复用时
	 * 完全分不清这次 0 是谁设的。查 `900000439` 的 `la` 为什么是 0，
	 * 三次 `alpha=0` 分别来自 L982/L1268/L1274 三个不同活动，
	 * 而部件号是复用的 —— 没有 fno 就只能靠猜。 */
	/* [D277] 补 fno 与调用栈。**注意 `hll_current_fno()` 在这里不可靠**：
	 * 它返回的是 `hll_call_fno`，只在 `hll_call()` 入口赋值、返回后不复原，
	 * 于是残留的是上一个**无关** HLL 的值（实测打出 `fno=107`
	 * = `CASConstructionProcess@SetSrcX`，它根本不调 SetAlpha）。
	 * 所以 fno 只能当参考，定位靠 `XSYS4_ALPHA_STACK=1` 打 AIN 调用栈。 */
	if (getenv("XSYS4_ALPHA_DBG")) {
		/* [D285] 必须同时打 `par=`（直接父部件号）。
		 *
		 * 起因：`900000439` 的 alpha 序列一直没法判读 —— 同一个号在不同时刻
		 * 属于**不同部件**（`LargeSkillButton` 活动根 `par=-1`；活动释放后号
		 * 被回收，又被 `EnemyInformationView` 拿去当子部件 `par=900000393`）。
		 * 光看号，`0 → 191 → 255` 到底属于哪个部件全靠猜，D283/D284 两轮就
		 * 是这么被带偏的。父部件号在同一时刻内唯一，能把两类事件分开。 */
		struct parts *dbg_p = parts_try_get(parts_no);
		NOTICE("[alpha] set parts=%d alpha=%d par=%d fno~%d", parts_no, alpha,
		       dbg_p && dbg_p->parent ? dbg_p->parent->no : -1, hll_current_fno());
		if (getenv("XSYS4_ALPHA_STACK"))
			vm_stack_trace();
	}

	struct parts *p = parts_get(parts_no);
	/* [D188] 合成类部件（construction process）拿到 alpha=0 时**不能照单全收**。
	 *
	 * 实测（`logs/live_cp23.log`）：卡片部件只在初始化时被设 0 三次，
	 * 之后既不设 255、也不加任何 motion——而库表里 PartsEngine 根本没有
	 * `AddMotion*`（只有 SetMotionData / GetMotionBeginFrame / GetMotionEndFrame），
	 * 所以 AIN 无法靠淡入把它恢复。结果就是：合成结果完全正确
	 * （`/tmp/cproc_0.png` 是一张完整卡片），部件 `a=0` 永远不可见。
	 *
	 * 「移除卡片」走的是别的路径（`RemoveCardFromFormations`，不再设 0），
	 * 因此把 0 提升为 255 不影响隐藏逻辑。`XSYS4_NO_CP_ALPHA_FIX=1` 回退。
	 *
	 * [D224] 但这个提升**不能无条件做**：Rance10 的 VS 演出收尾有一个
	 * 同为 `PARTS_CONSTRUCTION_PROCESS` 的全屏淡出遮罩（`no=900000613`，
	 * 2x2 单色纹理 × `scale=(960,540)` 铺满 1920x1080、z=INT_MAX、
	 * 最后绘制）。AIN 在淡入结束时把它设 `alpha=0` 隐藏，无条件提升把
	 * 它抬回 255 → 遮罩永久不透明、战斗 UI 被整片盖住（H037：引擎自截图
	 * 10579B 全黑；`XSYS4_NO_CP_ALPHA_FIX=1` 回退后 1207952B 有画面）。
	 * 卡片合成纹理不会是 2x2，故只排除单色小纹理，卡片行为不变。 */
	static int cp_alpha_fix = -1;
	if (cp_alpha_fix < 0) {
		const char *e = getenv("XSYS4_NO_CP_ALPHA_FIX");
		cp_alpha_fix = (e && *e == '1') ? 0 : 1;
	}
	struct parts_common *cp_common = &p->states[p->state].common;
	if (cp_alpha_fix && alpha <= 0 &&
	    p->states[p->state].type == PARTS_CONSTRUCTION_PROCESS &&
	    cp_common->w >= 8 && cp_common->h >= 8 &&
	    cp_common->texture.handle) {
		if (getenv("XSYS4_ALPHA_DBG"))
			NOTICE("[alpha] keep 255 on construction-process parts=%d",
			       parts_no);
		alpha = 255;
	}
	parts_set_alpha(p, alpha);
}

void PE_SetPartsDrawFilter(int parts_no, int draw_filter)
{
	parts_get(parts_no)->draw_filter = draw_filter;
}

void PE_SetAddColor(int parts_no, int r, int g, int b)
{
	SDL_Color add_color = {
		min(255, max(0, r)),
		min(255, max(0, g)),
		min(255, max(0, b)),
		255
	};
	parts_set_add_color(parts_get(parts_no), add_color);
}

void PE_SetMultiplyColor(int parts_no, int r, int g, int b)
{
	SDL_Color multiply_color = {
		min(255, max(0, r)),
		min(255, max(0, g)),
		min(255, max(0, b)),
		255
	};
	parts_set_multiply_color(parts_get(parts_no), multiply_color);
}

/* [D341] 别名：AIN 的 `GetComponentAbsolutePos*` 指的是**含父偏移**的全局坐标，
 * 而 `PE_GetPartsX/Y/Z` 返回的是 `local.pos`。两者不能混用 ——
 * 战斗中敌人/技能卡都挂在多层容器下，拿 local 去算会整体偏移。 */
/* [D455] 颜色 getter 族：**setter 早就有，getter 一直是 TODO**。
 *
 * 这一族是典型「配对缺口」：`SetComponentAddColor` / `SetComponentMulColor`
 * 都实现了（写进 `local.add_color` / `local.multiply_color`），
 * 但 6 个分量 getter 全在未实现清单里。调用方（技能演出、受击闪白）
 * 设完颜色再读回来做插值时，拿到的是桩返回的 0，颜色就永远停在第一次。
 *
 * 口径与 `PE_GetPartsAlpha` 一致：**返回 local（本件自己的值），不是 global**。
 * global 是叠加父件之后的合成值，读它会把父件的颜色也算进来 ——
 * 而 setter 写的是 local，getter 必须回读同一个量。 */
int PE_GetAddColorR(int parts_no) { return parts_get(parts_no)->local.add_color.r; }
int PE_GetAddColorG(int parts_no) { return parts_get(parts_no)->local.add_color.g; }
int PE_GetAddColorB(int parts_no) { return parts_get(parts_no)->local.add_color.b; }
int PE_GetMultiplyColorR(int parts_no) { return parts_get(parts_no)->local.multiply_color.r; }
int PE_GetMultiplyColorG(int parts_no) { return parts_get(parts_no)->local.multiply_color.g; }
int PE_GetMultiplyColorB(int parts_no) { return parts_get(parts_no)->local.multiply_color.b; }

int PE_GetPartsAbsoluteX(int parts_no) { return parts_get(parts_no)->global.pos.x; }
int PE_GetPartsAbsoluteY(int parts_no) { return parts_get(parts_no)->global.pos.y; }
int PE_GetPartsAbsoluteZ(int parts_no) { return parts_get(parts_no)->global.z; }

int PE_GetPartsX(int parts_no)
{
	return parts_get(parts_no)->local.pos.x;
}

int PE_GetPartsY(int parts_no)
{
	return parts_get(parts_no)->local.pos.y;
}

/* 视图绝对坐标（已含父链与 root 偏移）。AIN 侧只有 GetPartsX/Y（局部），
 * 但渲染与命中用的都是 global.pos，排障时必须有地方读它。 */
int PE_GetPartsGlobalX(int parts_no)
{
	return parts_get(parts_no)->global.pos.x;
}

int PE_GetPartsGlobalY(int parts_no)
{
	return parts_get(parts_no)->global.pos.y;
}

int PE_GetPartsWidth(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	return parts_get(parts_no)->states[state].common.w;
}

int PE_GetPartsHeight(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	return parts_get(parts_no)->states[state].common.h;
}

int PE_GetPartsUpperLeftPosX(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	int x = parts->states[state].common.hitbox.x;
	if (parts->parent)
		x += parts->parent->global.pos.x;
	return x;
}

int PE_GetPartsUpperLeftPosY(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;
	struct parts *parts = parts_get(parts_no);
	int y = parts->states[state].common.hitbox.y;
	if (parts->parent)
		y += parts->parent->global.pos.y;
	return y;
}

int PE_GetPartsZ(int parts_no)
{
	return parts_get(parts_no)->local.z;
}

bool PE_GetPartsShow(int parts_no)
{
	/* [D128] 图层号查询的是整层的显示状态（SceneStack@IsShow 走这条路）。 */
	if (parts_no_is_layer(parts_no)) {
		int c = PE_get_controller_index(parts_no);
		if (c < 0 || c == PARTS_CONTROLLER_SYSTEM_OVERLAY)
			return true;
		return ctrl_stack.show[c];
	}
	return parts_get(parts_no)->local.show;
}

int PE_GetPartsAlpha(int parts_no)
{
	return parts_get(parts_no)->local.alpha;
}

int PE_GetPartsDrawFilter(int parts_no)
{
	return parts_get(parts_no)->draw_filter;
}

void PE_GetAddColor(int parts_no, int *r, int *g, int *b)
{
	struct parts *parts = parts_get(parts_no);
	*r = parts->local.add_color.r;
	*g = parts->local.add_color.g;
	*b = parts->local.add_color.b;
}

void PE_GetMultiplyColor(int parts_no, int *r, int *g, int *b)
{
	struct parts *parts = parts_get(parts_no);
	*r = parts->local.multiply_color.r;
	*g = parts->local.multiply_color.g;
	*b = parts->local.multiply_color.b;
}

void PE_SetPartsOriginPosMode(int parts_no, int origin_pos_mode)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_origin_mode(parts, origin_pos_mode);
	parts_dirty(parts);
}

int PE_GetPartsOriginPosMode(int parts_no)
{
	return parts_get(parts_no)->origin_mode;
}

/* [D312] 真实的父部件号查询。
 *
 * `PartsEngine.Parts_GetParentPartsNumber` 一直是「恒返回 0」的桩（D80/D81），
 * 理由是「第一阶段不做父部件追踪」。但父链是 AIN 算**层号**的输入：
 * `AFL_Parts_GetLayerIDByParts`(7260) 就是沿父链往上找、遇到 0 结束。
 * 恒返回 0 ⇒ 每个部件都被判成「根」⇒ 算出的层号与实际归属不符，
 * 于是 AIN 去隐藏 A 层、而部件其实在 B 层（`[BG-LAYER]` 的形状）。
 *
 * `parent_parts_no` 的 0 表示「没有父」，与 AIN 的终止约定一致。 */
int PE_parts_get_parent_parts_number(int parts_no)
{
	struct parts *p = parts_try_get(parts_no);
	if (!p)
		return 0;
	if (p->parent)
		return p->parent->no;
	/* 挂父是延迟生效的（`pending_parent` 等下次 RefreshGlobalTree 才落地），
	 * 这期间也要报出「将来会挂到谁」，否则刚 SetParent 完就问的情况全错。 */
	if (p->pending_parent >= 0)
		return p->pending_parent;
	return 0;
}

void PE_SetParentPartsNumber(int parts_no, int parent_parts_no)
{
	/* [D194] 選択肢按钮的根 `par=-1`、z=100，而兄弟面板 z=22000。
	 * 若按钮本该挂在面板下，global z 应是 22100 —— 先证明 AIN 有没有调挂父。 */
	if (getenv("XSYS4_ACT_PART_DBG") &&
	    (parts_no >= 900000300 || parent_parts_no >= 900000300))
		NOTICE("[parent] SetParent no=%d parent=%d", parts_no, parent_parts_no);
	struct parts *parts = parts_get(parts_no);
	parts->pending_parent = parent_parts_no;
	parts_component_dirty(parts);
	/* [D194] 运行时挂父要立刻重算整树。
	 *
	 * `activity_load` 末尾只调一次 `PE_RefreshGlobalTree()`；而選択肢按钮是
	 * AIN 在运行时 `Activity@Create` 出来的，它的根 `SetParent(325 → 323)`
	 * 发生在那次刷新之后。`PE_UpdateComponent` 虽然会把父挂上，但只重算
	 * **该部件自己**，不动子树 —— 于是按钮的 `global.z` 永远停在建树瞬间的
	 * 100，被面板的 22000 盖住，画面上一片空白（点得到、流程也能推进）。
	 *
	 * 只在「从无父变有父」时触发，避免建造活动时几十次 SetParent 各刷一遍。
	 * `PE_RefreshGlobalTree()` 只挂父链 + 重算参数，不触发 layout，幂等。 */
	if (!parts->parent && parent_parts_no >= 0 && parts_try_get(parent_parts_no))
		PE_RefreshGlobalTree();
}

int PE_GetParentPartsNumber(int parts_no)
{
	struct parts *parts = parts_get(parts_no);
	if (parts->parent)
		return parts->parent->no;
	return -1;
}

/* --- D89：显式的子树 -----------------------------------------------
 * AIN 通过 PartsEngine f[165..172] 访问部件树
 * （IsExistChild / ClearChild / AddChild / InsertChild / RemoveChild /
 * NumofChild / GetChildIndex / GetChild）。活动装载器已经用
 * PE_SetParentPartsNumber 设过父部件，但 AIN 会立刻来问树结构，
 * 所以这些函数直接操作活的 TAILQ，
 * 而不是等脏表那一轮。 */

static void parts_detach(struct parts *child)
{
	if (!child->parent)
		return;
	TAILQ_REMOVE(&child->parent->children, child, child_list_entry);
	child->parent = NULL;
}

static void parts_attach_after(struct parts *parent, struct parts *child, struct parts *after)
{
	parts_detach(child);
	child->parent = parent;
	child->pending_parent = -1;
	if (after)
		TAILQ_INSERT_AFTER(&parent->children, after, child, child_list_entry);
	else
		TAILQ_INSERT_TAIL(&parent->children, child, child_list_entry);
	parts_component_dirty(child);
	parts_dirty(parent);
}

void PE_AddChild(int parts_no, int child_no)
{
	struct parts *parent = parts_get(parts_no);
	struct parts *child = parts_get(child_no);
	parts_attach_after(parent, child, NULL);
}

void PE_InsertChild(int parts_no, int child_no, int index)
{
	struct parts *parent = parts_get(parts_no);
	struct parts *child = parts_get(child_no);
	struct parts *after = NULL;
	int i = 0;
	struct parts *c;
	PARTS_FOREACH_CHILD(c, parent) {
		if (i++ == index - 1) {
			after = c;
			break;
		}
	}
	if (index <= 0)
		parts_attach_after(parent, child, NULL);
	else
		parts_attach_after(parent, child, after);
}

void PE_RemoveChild(int parts_no, int child_no)
{
	struct parts *parent = parts_get(parts_no);
	struct parts *child = parts_try_get(child_no);
	if (!parent || !child || child->parent != parent)
		return;
	parts_detach(child);
	parts_dirty(parent);
}

void PE_ClearChild(int parts_no)
{
	struct parts *parent = parts_get(parts_no);
	struct parts *c = TAILQ_FIRST(&parent->children);
	while (c) {
		struct parts *next = TAILQ_NEXT(c, child_list_entry);
		TAILQ_REMOVE(&parent->children, c, child_list_entry);
		c->parent = NULL;
		c = next;
	}
	parts_dirty(parent);
}

int PE_NumofChild(int parts_no)
{
	struct parts *parent = parts_try_get(parts_no);
	if (!parent)
		return 0;
	int n = 0;
	struct parts *c;
	PARTS_FOREACH_CHILD(c, parent)
		n++;
	return n;
}

int PE_GetChild(int parts_no, int index)
{
	struct parts *parent = parts_try_get(parts_no);
	if (!parent || index < 0)
		return -1;
	int i = 0;
	struct parts *c;
	PARTS_FOREACH_CHILD(c, parent) {
		if (i++ == index)
			return c->no;
	}
	return -1;
}

int PE_GetChildIndex(int parts_no, int child_no)
{
	struct parts *parent = parts_try_get(parts_no);
	if (!parent)
		return -1;
	int i = 0;
	struct parts *c;
	PARTS_FOREACH_CHILD(c, parent) {
		if (c->no == child_no)
			return i;
		i++;
	}
	return -1;
}

int PE_IsExistChild(int parts_no, int child_no)
{
	return PE_GetChildIndex(parts_no, child_no) >= 0;
}

bool PE_SetPartsGroupNumber(possibly_unused int PartsNumber, possibly_unused int GroupNumber)
{
	UNIMPLEMENTED("(%d, %d)", PartsNumber, GroupNumber);
	return true;
}

void PE_SetPartsMessageWindowShowLink(possibly_unused int parts_no, bool message_window_show_link)
{
	struct parts *parts = parts_get(parts_no);
	parts->message_window = message_window_show_link;
}

bool PE_GetPartsMessageWindowShowLink(int parts_no)
{
	return parts_get(parts_no)->message_window;
}

void PE_SetPartsMagX(int parts_no, float scale_x)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_scale_x(parts, scale_x);
}

float PE_GetPartsMagX(int parts_no)
{
	return parts_get(parts_no)->local.scale.x;
}

void PE_SetPartsMagY(int parts_no, float scale_y)
{
	struct parts *parts = parts_get(parts_no);
	parts_set_scale_y(parts, scale_y);
}

float PE_GetPartsMagY(int parts_no)
{
	return parts_get(parts_no)->local.scale.y;
}

void PE_SetPartsRotateX(int parts_no, float rot_x)
{
	parts_set_rotation_x(parts_get(parts_no), rot_x);
}

/* [D456] 与 setter 配对的 getter（同样是"setter 有、getter 缺"）。
 * 口径统一：回读 `local.*`（本件自己的值），不是叠加父件后的 `global.*`。 */
float PE_GetPartsRotateX(int parts_no)
{
	return parts_get(parts_no)->local.rotation.x;
}

float PE_GetPartsRotateY(int parts_no)
{
	return parts_get(parts_no)->local.rotation.y;
}

int PE_GetPartsAlphaClipperPartsNo(int parts_no)
{
	return parts_get(parts_no)->local.alpha_clipper_parts_no;
}

/* `GetNumeralLength` 在 AIN 里只有 (partsNo) 一个参数，没有 state ——
 * 而 setter 是 (partsNo, length, state)。所以读**默认态 (state 0)** 的长度：
 * 数字件几乎只用默认态，带 state 的那些是逐帧动画用的，读错态会得到上一帧的值。 */
int PE_GetNumeralLength(int parts_no)
{
	return parts_get_numeral(parts_get(parts_no), PARTS_STATE_DEFAULT)->length;
}

void PE_SetPartsRotateY(int parts_no, float rot_y)
{
	parts_set_rotation_y(parts_get(parts_no), rot_y);
}

void PE_SetPartsRotateZ(int parts_no, float rot_z)
{
	parts_set_rotation_z(parts_get(parts_no), rot_z);
}

float PE_GetPartsRotateZ(int parts_no)
{
	return parts_get(parts_no)->local.rotation.z;
}

void PE_SetPartsAlphaClipperPartsNumber(int parts_no, int alpha_clipper_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	/* [D302] 立绘溢出金框：要用「父件的 alpha 当遮罩」把它裁进圆孔里。
	 * 先确认 AIN 到底有没有调这个接口。 */
	if (getenv("XSYS4_ACT_PART_DBG")) {
		static int n;
		if (n++ < 20)
			NOTICE("[clip] SetPartsAlphaClipper no=%d clip=%d",
			       parts_no, alpha_clipper_parts_no);
	}
	parts->local.alpha_clipper_parts_no = alpha_clipper_parts_no;
	parts_component_dirty(parts);
	parts_dirty(parts);
}

void PE_SetPartsPixelDecide(int parts_no, bool pixel_decide)
{
	/* [D396] 原来是空实现（连日志都被注释掉了）。语义是「命中判定要求
	 * 该像素不透明」—— 存下来，命中路径后续读它做 alpha 采样。 */
	struct parts *parts = parts_try_get(parts_no);
	if (parts)
		parts->pixel_decide = pixel_decide;
}

bool PE_GetPartsPixelDecide(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->pixel_decide : false;
}

bool PE_SetThumbnailReductionSize(int reduction_size)
{
	UNIMPLEMENTED("(%d)", reduction_size);
	return true;
}

bool PE_SetThumbnailMode(bool mode)
{
	UNIMPLEMENTED("(%s)", mode ? "true" : "false");
	return true;
}

bool PE_save_thumbnail(struct string *filename, int reduction_factor)
{
	if (reduction_factor < 1)
		reduction_factor = 1;

	Texture *src = gfx_main_surface();
	int w = src->w / reduction_factor;
	int h = src->h / reduction_factor;

	// 反复折半缩小，直到距离目标尺寸在两倍以内，
	// 再做最后一次拉伸。这样能避免
	// 单次双线性缩小产生的走样。
	Texture tmp, *cur = src;
	bool have_tmp = false;
	while (cur->w / 2 > w && cur->h / 2 > h) {
		Texture next;
		gfx_init_texture_blank(&next, cur->w / 2, cur->h / 2);
		gfx_copy_stretch_with_alpha_map(&next, 0, 0, next.w, next.h, cur, 0, 0, cur->w, cur->h);
		if (have_tmp)
			gfx_delete_texture(&tmp);
		tmp = next;
		cur = &tmp;
		have_tmp = true;
	}

	Texture dst;
	gfx_init_texture_blank(&dst, w, h);
	gfx_copy_stretch_with_alpha_map(&dst, 0, 0, w, h, cur, 0, 0, cur->w, cur->h);
	if (have_tmp)
		gfx_delete_texture(&tmp);

	char *path = savedir_path(filename->text);
	int r = gfx_save_texture(&dst, path, ALCG_QNT);
	free(path);
	gfx_delete_texture(&dst);
	return !!r;
}

void PE_SetInputState(int parts_no, int state)
{
	if (!parts_state_valid(--state)) {
		WARNING("invalid input state: %d", state);
		return;
	}
	parts_set_state(parts_get(parts_no), state);
}

int PE_GetInputState(int parts_no)
{
	return parts_get(parts_no)->state + 1;
}

/* 兰斯10 的组件类型号 -> 引擎的绘制类型。
 *
 * 这些号是游戏自己用的（从
 * activityeditor::detail::CBatchRenameStack@GetPartsTypeName 的 AIN switch #31 还原），
 * 和上游 xsystem4 的编号不一样；对应关系见 activity.c 里的枚举。
 * 控件类组件（button、checkbox、textbox…）从一张 flat 图绘制，
 * 容器变成布局盒，其余槽位
 * 映射到各自的引擎渲染器。 */
static enum parts_type component_type_to_parts_type(int type)
{
	switch (type) {
	case 0:  /* 按钮 Button */
	case 1:  /* 检查ＢＯＸ CheckBox */
	case 2:  /* 纵向滚动条栏 VScrollBar */
	case 3:  /* 横向滚动条栏 HScrollBar */
	case 4:  /* 文本ＢＯＸ TextBox */
	case 5:  /* リストＢＯＸ ListBox */
	case 6:  /* ＣＯＭＢＯＢＯＸ ComboBox */
	case 7:  /* ＭＬ文本ＢＯＸ MultiLineTextBox */
	case 9:  /* ラジオ按钮ＢＯＸ */
	case 10: /* 消息窗口 MessageWindow */
	case 11: /* スピンＢＯＸ SpinBox */
		return PARTS_FLAT;
	case 8:  /* レイアウトＢＯＸ */
	case 12: /* 面板 Panel */
	case 13: /* フォーム Form */
	case 14: /* フォームグ分组 */
		return PARTS_LAYOUT_BOX;
	case 15: /* 用户组件 UserComponent */
		/* 控件本身不画任何东西：它的内容来自 EX 负载里
		 * 指定的内建控件
		 * (数据 = ['オブジェクト名','TextButton','参数','...']).
		 * 把它当成 PARTS_CG 会得到一个没有纹理、尺寸为 0 的 CG，
		 * 这就是为什么兰斯10 的标题菜单（13 个部件里有 8 个是
		 * 用户组件）看不见。在控件层存在之前，
		 * 它就是个容器。 */
		return PARTS_LAYOUT_BOX;
	case 17: /* ＣＧ */
		return PARTS_CG;
	case 18: /* 循环ＣＧ */
		return PARTS_ANIMATION;
	case 19: /* 文本 */
		return PARTS_TEXT;
	case 20: /* 横向计量 */
		return PARTS_HGAUGE;
	case 21: /* 纵向计量 */
		return PARTS_VGAUGE;
	case 22: /* 数字 */
		return PARTS_NUMERAL;
	case 23: /* 矩形 */
		return PARTS_RECT_DETECTION;
	case 24: /* 構築 */
		return PARTS_CONSTRUCTION_PROCESS;
	case 25: /* ＣＧ判定 */
		return PARTS_RECT_DETECTION;
	case 26: /* フラット */
		return PARTS_FLAT;
	case 27: /* ３Ｄ层 */
		return PARTS_3DLAYER;
	case 28: /* 动画 */
		return PARTS_MOVIE;
	default:
		return PARTS_UNINITIALIZED;
	}
}

void PE_SetComponentType(int parts_no, int type, int state)
{
	if (!parts_state_valid(--state))
		return;
	struct parts *parts = parts_get(parts_no);
	enum parts_type pt = component_type_to_parts_type(type);
	if (pt == PARTS_UNINITIALIZED) {
		VM_ERROR("unknown component type %d", type);
		return;
	}
	if (parts->states[state].type != pt)
		parts_state_reset(&parts->states[state], pt);
	parts->states[state].component_type = type;
}

int PE_GetComponentType(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return -1;

	/* 这个状态上游戏设过什么，就原样存回去。隐式创建的状态
	 * （例如 PE_SetPartsCG 建的）没有自己的组件类型，
	 * 所以从绘制类型反推一个合理的值。 */
	if (parts->states[state].component_type >= 0)
		return parts->states[state].component_type;
	switch (parts->states[state].type) {
	case PARTS_UNINITIALIZED:
	case PARTS_CG:                  return 17;
	case PARTS_LAYOUT_BOX:          return 8;
	case PARTS_ANIMATION:           return 18;
	case PARTS_TEXT:                return 19;
	case PARTS_HGAUGE:              return 20;
	case PARTS_VGAUGE:              return 21;
	case PARTS_NUMERAL:             return 22;
	case PARTS_RECT_DETECTION:      return 25;
	case PARTS_CONSTRUCTION_PROCESS:return 24;
	case PARTS_FLAT:                return 26;
	case PARTS_3DLAYER:             return 27;
	case PARTS_MOVIE:               return 28;
	case PARTS_FLASH:               return 19;
	}
	VM_ERROR("unsupported component type %d", parts->states[state].type);
	return -1;
}

/* 给部件一个尺寸（也就是命中盒），但不改它的状态类型。
 *
 * PE_SetPartsRectangleDetectionSize() 会把状态重置成 PARTS_RECT_DETECTION，
 * 那会把部件已有的东西全丢掉。用户组件需要在保住自己状态的
 * 同时拥有命中盒（它们是 LAYOUT_BOX 容器，
 * 内容由控件层绘制）。 */
bool PE_SetPartsSize(int parts_no, int w, int h, int state)
{
	/* [D375] 与 `SetRectangleDetectionSize` 共用同一个探针开关：
	 * 「这个件的尺寸到底是谁设的」只能靠两条路都留痕才回答得了。 */
	if (getenv("XSYS4_FLAT_DBG"))
		NOTICE("[size] SetPartsSize no=%d %dx%d state=%d", parts_no, w, h, state);
	/* [D375] AIN 自己传 1×1 时把调用栈打出来 —— 「尺寸是谁算的」只能这样回答。 */
	if (getenv("XSYS4_SIZE_1X1_TRACE") && w <= 1 && h <= 1)
		vm_stack_trace();
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;
	parts_set_dims(parts, &parts->states[state].common, w, h);
	return true;
}

void PE_SetUserComponent(int parts_no, const char *object, const char *label)
{
	parts_set_user_component(parts_try_get(parts_no), object, label);
}

/* 用户组件的 4 个 HLL 入口（PartsEngine.Set/GetUserComponentName/Data）。
 * 引擎侧只是个"名字 + 键值袋"，控件行为由游戏的 AIN 代码实现。 */
void PE_SetUserComponentName(int parts_no, const char *name)
{
	parts_set_user_component(parts_try_get(parts_no), name, NULL);
}

const char *PE_GetUserComponentName(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->uc_object : NULL;
}

void PE_SetUserComponentData(int parts_no, const char *key, const char *value)
{
	parts_set_user_component_data(parts_try_get(parts_no), key, value);
}

const char *PE_GetUserComponentData(int parts_no, const char *key)
{
	return parts_get_user_component_data(parts_try_get(parts_no), key);
}

bool PE_SetPartsRectangleDetectionSize(int parts_no, int w, int h, int state)
{
	/* [D192] 「按钮没有几何」要分清是 AIN 没传、还是传了没生效。 */
	if (getenv("XSYS4_FLAT_DBG"))
		NOTICE("[rect] SetRectangleDetectionSize no=%d %dx%d state=%d", parts_no, w, h, state);
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;
	if (parts->states[state].type != PARTS_RECT_DETECTION)
		parts_state_reset(&parts->states[state], PARTS_RECT_DETECTION);
	parts_set_dims(parts, &parts->states[state].common, w, h);
	return true;
}

bool PE_SetPartsCGDetectionSize(int parts_no, struct string *cg_name, int state)
{
	UNIMPLEMENTED("(%d, %s, %d)", parts_no, display_sjis0(cg_name->text), state);
	return false;
}

int PE_GetFreeNumber(void)
{
	/* [D161] 起始号是**版本相关**的，不能照抄 Rance 01。
	 *
	 * 旧值 1000001000 来自 "per GUIEngine.dll (Rance 01)"。但兰斯10 的 AIN
	 * 在 `Parts@Release` 里明确校验 `1 ～ 999999999`，实测单局打出 213 条
	 * "指定されたパーツ番号「1000002473」はシステムが使用できない番号です"，
	 * 每次释放都失败 —— 每次都换一个新号，213 个号各不相同。
	 *
	 * A/B（同序列）：旧 1000001000 → 213 条范围报错、err=0、acts=107；
	 * 新 900000000 → 范围报错 0、err=0、acts=107，基线 32/0/0 不变。
	 * ⇒ 默认改为 900000000。`XSYS4_PARTS_NO_BASE=1000001000` 可回退。
	 *
	 * 注意：改基址后**旧日志里的 100000xxxx 部件号全部作废**，
	 * 历史文档/脚本里那些号不能再拿去 grep。 */
	static int first_free = -1;
	if (first_free < 0) {
		const char *e = getenv("XSYS4_PARTS_NO_BASE");
		first_free = (e && *e) ? atoi(e) : 900000000;
	}
	while (PE_IsExist(first_free)) {
		first_free++;
	}
	// XXX：即使部件没创建成功，这个 ID 也会自增
	return first_free++;
}

bool PE_IsExist(int parts_no)
{
	return !!ht_get_int(parts_table, parts_no, NULL);
}

static void ctrl_stack_init(void)
{
	memset(&ctrl_stack, 0, sizeof(ctrl_stack));
	/* [D128] 图层默认可见。 */
	for (int i = 0; i < PARTS_CONTROLLER_STACK_MAX; i++)
		ctrl_stack.show[i] = true;
	// 加上初始的默认控制器
	PE_AddController(-1);
}

/* [D129] ID/index 边界：内部一律用栈下标，AIN 一律用稳定 ID。 */
int PE_get_controller_id(int index)
{
	if (index == PARTS_CONTROLLER_SYSTEM_OVERLAY)
		return PARTS_CONTROLLER_SYSTEM_OVERLAY;
	if (index < 0 || index >= ctrl_stack.nr_controllers)
		return -1;
	return ctrl_stack.id[index];
}

int PE_get_controller_index(int id)
{
	if (id == PARTS_CONTROLLER_SYSTEM_OVERLAY)
		return PARTS_CONTROLLER_SYSTEM_OVERLAY;
	if (id < 0)
		return -1;
	for (int i = 0; i < ctrl_stack.nr_controllers; i++) {
		if (ctrl_stack.id[i] == id)
			return i;
	}
	return -1;
}

// 往栈上加一个新控制器并激活它。`index` 参数指定
// 新控制器插在栈里的位置；-1 表示「插在当前活动控制器
// 之后」。实际游戏里只会传 -1，
// 而那一刻活动控制器总是栈顶，
// 所以这里退化成
// 一次简单的入栈。
/* [D128] 场景切换后旧图层仍在画面上：需要知道"谁挂在哪个 controller 上"，
 * 因为渲染顺序是 (controller_no, global.z)，旧场景只要挂在低位 controller
 * 上就永远画在新场景下面，而 RemoveController 又带不走它。
 * 只在诊断开关下打印，一次一行，输出量可控。 */
void PE_DebugControllerHistogram(const char *tag)
{
	int cnt[PARTS_CONTROLLER_STACK_MAX + 2] = { 0 };
	int shown[PARTS_CONTROLLER_STACK_MAX + 2] = { 0 };
	struct parts *p;
	int total = 0, nr_shown = 0, max_c = -1;

	TAILQ_FOREACH(p, &parts_list, parts_list_entry) {
		int c = p->controller_no;
		if (c < 0 || c > PARTS_CONTROLLER_STACK_MAX)
			c = PARTS_CONTROLLER_STACK_MAX + 1;
		cnt[c]++;
		total++;
		if (p->global.show) {
			shown[c]++;
			nr_shown++;
		}
		if (c > max_c && c <= PARTS_CONTROLLER_STACK_MAX)
			max_c = c;
	}

	char buf[512];
	int n = snprintf(buf, sizeof(buf), "[ctrl] %s total=%d shown=%d active=%d nctrl=%d hist=",
			 tag, total, nr_shown, ctrl_stack.active, ctrl_stack.nr_controllers);
	for (int c = 0; c <= max_c && n < (int)sizeof(buf) - 24; c++) {
		if (!cnt[c])
			continue;
		n += snprintf(buf + n, sizeof(buf) - n, "%d:%d/%d ", c, shown[c], cnt[c]);
	}
	if (cnt[PARTS_CONTROLLER_STACK_MAX + 1] && n < (int)sizeof(buf) - 24)
		n += snprintf(buf + n, sizeof(buf) - n, "sys:%d/%d ",
			      shown[PARTS_CONTROLLER_STACK_MAX + 1],
			      cnt[PARTS_CONTROLLER_STACK_MAX + 1]);
	NOTICE("%s", buf);

	/* [D175] 上一行的 `shown`/`cnt` 统计的是**部件自己的 global.show**，
	 * 而 `parts_render()` 真正的闸门是
	 *   `parts_is_visible() = global.show && ctrl_stack.show[c] && c >= parts_cover_layer`
	 * 后两个条件（图层开关、遮挡下界）从来没被打印过 —— 于是
	 * "controller 活跃"（`active=4`）被误当成"这一层要画"，但那是**数量**，
	 * 不是开关位。把开关位和遮挡下界一次打全。 */
	char lbuf[256];
	int ln = snprintf(lbuf, sizeof(lbuf), "[ctrl] %s layer_show=", tag);
	int lim = max_c < PARTS_CONTROLLER_STACK_MAX ? max_c : PARTS_CONTROLLER_STACK_MAX - 1;
	for (int c = 0; c <= lim && ln < (int)sizeof(lbuf) - 16; c++)
		ln += snprintf(lbuf + ln, sizeof(lbuf) - ln, "%c", ctrl_stack.show[c] ? '1' : '0');
	snprintf(lbuf + ln, sizeof(lbuf) - ln, " cover=%d", parts_cover_layer);
	NOTICE("%s", lbuf);
}

/* [D128] 一次性部件快照：把"此刻还在显示"的部件连同所属 controller、z、
 * 尺寸、UC 名一起打出来。诊断"场景切走了但旧图层还画着"时靠它定位。
 * XSYS4_PARTS_DUMP=1 时在每次活动装载后调用。
 *
 * [D167] 补 `a=`(alpha) `st=`(内容类型) `tex=`(有没有纹理)。
 * 起因：任务地图的菜单 `MenuContext:7..13` 明明装载了、坐标也正常
 * （`(0,608) 512x91`），画面上却一个像素都没有。原格式只有 w/h，
 * "有尺寸但没画"的三种原因（alpha=0 / 内容类型不是 CG / 纹理没加载）
 * 一个都区分不出来。 */
/* [D316] 部件归属追踪：`XSYS4_TRACK_PARTS=900000400,900000408`。
 *
 * 只在 (ctrl / local.show / global.show / z / parent) **发生变化**时打一行。
 * `[BG-LAYER]` 需要回答的是「这个部件一生中归属变了哪些次」，而 `setshow`
 * 探针只覆盖 show、`pending-move` 只覆盖迁层 —— 两处都可能漏掉「创建时就
 * 在某层」这一事实。这个探针是唯一能给出完整变迁史的。 */
void PE_debug_track_parts(void)
{
	static int nos[16], n, inited;
	static struct { int ctrl, lshow, gshow, z, parent; } last[16];
	if (!inited) {
		inited = 1;
		const char *e = getenv("XSYS4_TRACK_PARTS");
		if (e && *e) {
			const char *q = e;
			while (*q && n < 16) {
				nos[n++] = atoi(q);
				while (*q && *q != ',') q++;
				if (*q == ',') q++;
			}
		}
		for (int i = 0; i < n; i++)
			last[i].ctrl = -999;
	}
	for (int i = 0; i < n; i++) {
		struct parts *p = parts_try_get(nos[i]);
		int ctrl = p ? p->controller_no : -1;
		int lshow = p ? p->local.show : -1;
		int gshow = p ? p->global.show : -1;
		int z = p ? p->global.z : 0;
		int par = (p && p->parent) ? p->parent->no : 0;
		if (ctrl == last[i].ctrl && lshow == last[i].lshow &&
		    gshow == last[i].gshow && z == last[i].z && par == last[i].parent)
			continue;
		NOTICE("[track] no=%d ctrl=%d(id=%d) lshow=%d gshow=%d z=%d parent=%d",
		       nos[i], ctrl,
		       (ctrl >= 0 && ctrl < ctrl_stack.nr_controllers) ? ctrl_stack.id[ctrl] : -1,
		       lshow, gshow, z, par);
		last[i].ctrl = ctrl; last[i].lshow = lshow; last[i].gshow = gshow;
		last[i].z = z; last[i].parent = par;
	}
}

/* [D314] `XSYS4_FORCE_HIDE=900000400,900000408`：每帧强制隐藏这些部件。
 *
 * 唯一用途是**判定性实验**：把「地图 CG 盖住新背景」缩成一句
 * 「只要隐藏这几个号，画面是否立刻变成新背景」。
 * 变了 ⇒ 根因是「AIN 从未 hide 它们」，去查切换逻辑；
 * 没变 ⇒ 还有别的东西在盖，回到合成顺序。默认关。 */
void PE_debug_force_hide(void)
{
	static int nos[16], n_nos, inited;
	if (!inited) {
		inited = 1;
		const char *e = getenv("XSYS4_FORCE_HIDE");
		if (e && *e) {
			const char *q = e;
			while (*q && n_nos < 16) {
				nos[n_nos++] = atoi(q);
				while (*q && *q != ',') q++;
				if (*q == ',') q++;
			}
		}
	}
	for (int i = 0; i < n_nos; i++) {
		struct parts *p = parts_try_get(nos[i]);
		if (p && p->local.show)
			parts_set_show(p, false);
	}

	/* [D315] `XSYS4_FORCE_MOVE=900000400:3,900000408:3`：把部件强制改到指定层。
	 *
	 * 判定「400/408 是不是**创建时落错了层**」：它们现在在 ADV 层（idx=4），
	 * 而 AIN 隐藏的是地图层（idx=3）。若把它们挪到 3，AIN 的
	 * `layer show=0` 就会自动把它们藏住 —— 画面立刻正确即证明归属错了。
	 * 同样是判定性实验，不是修复。 */
	{
		static int inited2, n2;
		static int mv_no[8], mv_idx[8];
		if (!inited2) {
			inited2 = 1;
			const char *e = getenv("XSYS4_FORCE_MOVE");
			const char *q = e;
			while (q && *q && n2 < 8) {
				mv_no[n2] = atoi(q);
				while (*q && *q != ':') q++;
				if (*q == ':') q++;
				mv_idx[n2] = atoi(q);
				while (*q && *q != ',') q++;
				if (*q == ',') q++;
				n2++;
			}
		}
		for (int i = 0; i < n2; i++) {
			struct parts *p = parts_try_get(mv_no[i]);
			if (p && p->controller_no != mv_idx[i])
				p->controller_no = mv_idx[i];
		}
	}
}

void PE_DebugDumpParts(const char *tag)
{
	struct parts *p;
	int n = 0;
	/* [D175] 先打一行图层状态：`vis=` 的两个非部件条件都在这里。
	 * 没有这一行，"部件全正常但一个都不画"就只能靠猜。 */
	{
		char lbuf[256];
		int ln = snprintf(lbuf, sizeof(lbuf), "[parts] %s layer_show=", tag);
		for (int c = 0; c < PARTS_CONTROLLER_STACK_MAX && ln < (int)sizeof(lbuf) - 16; c++)
			ln += snprintf(lbuf + ln, sizeof(lbuf) - ln, "%c", ctrl_stack.show[c] ? '1' : '0');
		snprintf(lbuf + ln, sizeof(lbuf) - ln, " cover=%d active=%d nctrl=%d",
			 parts_cover_layer, ctrl_stack.active, ctrl_stack.nr_controllers);
		NOTICE("%s", lbuf);
	}
	/* [D314] 「孤儿层」统计：`controller_no` 指向**已经不存在的层**的部件。
	 *
	 * `parts_is_visible()` 的上界是 `PARTS_CONTROLLER_STACK_MAX`，不是
	 * `nr_controllers`，所以层被 `RemoveController` 删掉后，留在旧号上的部件
	 * 仍然读到 `ctrl_stack.show[c]` 的**陈旧值 true** —— 永远可见。
	 * 这正是「地图 CG 一直盖住新背景、AIN 却从没 hide 过它」的形状：
	 * 它根本不在 AIN 认为的那一层里。 */
	{
		int orphan = 0;
		char obuf[200] = "";
		size_t oo = 0;
		struct parts *q;
		TAILQ_FOREACH(q, &parts_list, parts_list_entry) {
			if (q->controller_no < 0 ||
			    q->controller_no == PARTS_CONTROLLER_SYSTEM_OVERLAY ||
			    q->controller_no < ctrl_stack.nr_controllers)
				continue;
			orphan++;
			if (oo < sizeof(obuf) - 24 && orphan <= 10)
				oo += snprintf(obuf + oo, sizeof(obuf) - oo, " %d(c%d)",
					       q->no, q->controller_no);
		}
		if (orphan)
			NOTICE("[parts] %s ORPHAN-LAYER n=%d parts:%s", tag, orphan, obuf);
	}
	/* [D216] 部件表按插入顺序遍历，地图（9000000xx）会先把
	 * XSYS4_PARTS_DUMP_MAX 的名额吃光，动态号段（9000003xx）一条都打不出
	 * —— 又一次 [PROBE-GATED]。XSYS4_PARTS_DUMP_MIN=<号> 只盯目标号段。 */
	static int dump_min = -1;
	if (dump_min < 0) {
		const char *e = getenv("XSYS4_PARTS_DUMP_MIN");
		dump_min = (e && *e) ? atoi(e) : 0;
	}
	TAILQ_FOREACH(p, &parts_list, parts_list_entry) {
		if (!p->global.show || p->no < dump_min)
			continue;
		/* [D167] 必须打**当前状态**（`p->state`），不是 DEFAULT。
		 * 绘制读的是 `states[p->state]`：一个 DEFAULT 有 CG 的部件，
		 * 在 HOVERED/CLICKED 状态里可能是空的 —— 那才是"show=1、
		 * alpha=255、坐标正常却不显示"的第四种可能。 */
		struct parts_state *st = &p->states[p->state];
		/* [D175] `vis=` 是 `parts_render()` 真正的准入判断
		 * （`parts_is_visible()` = global.show && ctrl_stack.show[c] &&
		 *   c >= parts_cover_layer）。前面几个字段都是"部件自己长什么样"，
		 * 只有这一列说明"绘制时会怎么做" —— 前者全正常而 vis=0 时，
		 * 问题就在图层，不在部件。 */
		/* [D178] 补 `ck=`(clickable) / `pc=`(pass_cursor)：判断「这个部件能不能点」
		 * 靠的就是这两个标志，之前只有几何和渲染字段，找卡片坐标得靠猜。
		 * 另外每段 400 行的硬截断实测把编队界面 73 段全切了（卡片在后面），
		 * 上限改成可调，默认仍是 400 以免默认日志膨胀。 */
		NOTICE("[parts] %s no=%d ctrl=%d z=%d pos=(%d,%d) %dx%d a=%d la=%d par=%d ps=%d st=%d tex=%d vis=%d ck=%d pc=%d uc=%s",
		       tag, p->no, p->controller_no, p->global.z,
		       p->global.pos.x, p->global.pos.y,
		       st->common.w, st->common.h,
		       (int)p->global.alpha, (int)p->local.alpha,
		       p->parent ? p->parent->no : -1,
		       (int)p->state, (int)st->type,
		       st->common.texture.handle != 0,
		       parts_is_visible(p) ? 1 : 0,
		       p->clickable ? 1 : 0, p->pass_cursor ? 1 : 0,
		       p->uc_object ? p->uc_object : "-");
		if (p->serial)
			NOTICE("[parts] %s   serial=%u", tag, p->serial);
		/* [D310] CG 部件把**当前挂的 CG 名**打出来。
		 *
		 * 起因：剧情里旁白已经说「巨型战舰内部」，画面却还停在世界地图。
		 * 背景部件 `900000413` 是 `1920x1080 → 1798x680`、`vis=1 a=255 tex=1`，
		 * 从快照看一切正常 —— 但「正常」也可能是**旧图没被换掉**。
		 * 尺寸相同的新旧背景（`背景／任务／宇航船／通道` 与 `背景／宇航船／通道`
		 * 都是 1798x680）光看尺寸分不出来，必须看名字。 */
		if (st->type == PARTS_CG) {
			NOTICE("[parts] %s   cg no=%d name='%s' no#=%d",
			       tag, p->no,
			       st->cg.name && st->cg.name->text
			               ? display_sjis0(st->cg.name->text) : "(null)",
			       st->cg.no);
		}
		/* [D226] flat 部件的 `tex=` 列永远是 0（flat 不写 common.texture），
		 * 光看主行分不清「加载成功、只是没有单张纹理」和「flat 根本没装载」
		 * —— 后者 parts_render_flat() 直接 return，一个像素不画，却仍会被
		 * parts_update_cover_layer() 当成"铺满全屏的不透明地板"，把下面
		 * 所有层挡掉（H039）。补一行只打 flat 的加载事实。 */
		if (st->type == PARTS_FLAT) {
			size_t cgn = 0;
			for (size_t i = 0; i < st->flat.nr_libraries; i++)
				if (st->flat.textures[i].handle)
					cgn++;
			NOTICE("[parts] %s   flat no=%d loaded=%d root=%d hdr=%dx%d fps=%d tl=%zu libs=%zu cg=%zu frame=%d end=%d adv=%d stop=%d",
			       tag, p->no, st->flat.flat != NULL, st->flat.root_state,
			       st->flat.flat ? st->flat.flat->hdr.width : -1,
			       st->flat.flat ? st->flat.flat->hdr.height : -1,
			       st->flat.flat ? st->flat.flat->hdr.fps : -1,
			       st->flat.flat ? st->flat.flat->nr_timelines : (size_t)0,
			       st->flat.nr_libraries, cgn,
			       st->flat.root_state ? st->flat.root_state->current_frame : -1,
			       st->flat.end_frame, st->flat.needs_advance, st->flat.stopped);
		}
		static int dump_max = -1;
		if (dump_max < 0) {
			const char *e = getenv("XSYS4_PARTS_DUMP_MAX");
			dump_max = (e && *e) ? atoi(e) : 400;
			if (dump_max <= 0)
				dump_max = 400;
		}
		if (++n > dump_max) {
			NOTICE("[parts] %s ... truncated", tag);
			break;
		}
	}
}

/* [D289] `Activity@Load` 建出的部件该归到「即将创建的层」。
 *
 * 背景见 include/parts.h 的说明。这里只做一件事：Load 期间把新部件号记进
 * 待迁移表；`PE_AddController` 建新层成功后，把表里的部件 `controller_no`
 * 改成新层下标并清表。表和层级都是很小的量（一次 Load 最多几百个部件），
 * 用固定上限即可，超了只告警不崩。 */
#define CTRL_PENDING_MAX 4096
static int ctrl_pending_nos[CTRL_PENDING_MAX];
static int ctrl_pending_n = 0;
static int ctrl_pending_begin_active = -1;   /* Load 开始时快照的 active */
static int ctrl_pending_on = -1;

/* [D316] 层身份口径 A/B：`XSYS4_LAYER_IDX=1` 用**栈下标**（上游语义），
 * 默认用**稳定 id**（D129）。
 *
 * 为什么现在要试：`[BG-LAYER]` 的追踪证明 400/408（世界地图基底）创建在
 * `ctrl=4`(id=9)，而 AIN 收起的是 `ctrl=3`(id=7)。也就是说 AIN 创建它们时
 * 认为「当前层就是地图层」，而我们给它的当前层是 ADV 层。
 *
 * 上游 `PE_AddController` 返回的是**栈下标**（`return no`），`SetActive` /
 * `Remove` 也都按下标解释、越界时回退到 active；D129 把返回值改成稳定 id，
 * 之后 AIN 存的值、`GetActiveLayer` 的返回值都跟着换了口径。两条口径各自
 * 自洽，但 AIN 侧某些地方（例如它自己维护的层列表）可能只对其中一种成立。
 * 这个开关让两种口径都能跑，用画面做判据。 */
static bool layer_idx_mode(void)
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("XSYS4_LAYER_IDX");
		on = (e && strcmp(e, "0") != 0);
	}
	return on;
}

static bool ctrl_pending_enabled(void)
{
	if (ctrl_pending_on < 0) {
		const char *e = getenv("XSYS4_CTRL_PENDING");
		ctrl_pending_on = (e && !strcmp(e, "0")) ? 0 : 1;
	}
	return ctrl_pending_on;
}

void PE_ControllerPendingBegin(void)
{
	if (!ctrl_pending_enabled())
		return;
	/* 窗口开到**下一个 AddLayer 消费为止**，不是 Load 返回就关。
	 * 实测 `SceneStartScreen@0` 里 Activity@Load 之后还有 3 层调用栈才
	 * 走到 SceneStack@AddLayer（FadeIn → lambda → ...），立即关窗口会
	 * 把全部部件退回「归上一层」的旧行为（`pending-end 未迁移 3 个`）。 */
	if (ctrl_pending_n && getenv("XSYS4_CTRL_DBG"))
		NOTICE("[ctrl] pending-begin 丢弃上一次未消费的 %d 个", ctrl_pending_n);
	ctrl_pending_n = 0;
	ctrl_pending_begin_active = ctrl_stack.active;
	if (getenv("XSYS4_CTRL_DBG"))
		NOTICE("[ctrl] pending-begin active=%d", ctrl_pending_begin_active);
}

void PE_ControllerPendingEnd(void)
{
	if (!ctrl_pending_enabled())
		return;
	/* 不在这里关窗口：真正的消费点是 PE_AddController。 */
}

/* 部件创建时调用：Load 期间创建的就记下来，等 AddLayer 迁移。 */
static void ctrl_pending_add(int parts_no)
{
	if (!ctrl_pending_enabled() || ctrl_pending_begin_active < 0)
		return;
	if (ctrl_pending_n >= CTRL_PENDING_MAX) {
		static int warned;
		if (!warned) {
			warned = 1;
			WARNING("[ctrl] pending 表满（%d），后续部件不迁移", CTRL_PENDING_MAX);
		}
		return;
	}
	ctrl_pending_nos[ctrl_pending_n++] = parts_no;
}

/* [D312] 真正设置「活动层」。
 *
 * `PartsEngine.SetActiveController` 一直是 `PE_Noop_SetActiveController` 空实现
 * （D85 的占位），实测运行期被调用 **5970 次** —— 每建一层、每次切场景都在调。
 * 它决定 `parts->controller_no = ctrl_stack.active`（新部件落在哪层），
 * 也是 AIN 判断「这一层现在是不是前台」的依据。空实现 ⇒ 活动层永远是栈顶，
 * 于是 AIN 想往低层放/隐藏的东西全被放到栈顶，隐藏因此不生效。
 *
 * `XSYS4_ACTIVE_CTRL_SET=0` 可回退到空实现做 A/B。 */
void PE_SetActiveController(int no)
{
	static int off = -1;
	if (off < 0) {
		const char *e = getenv("XSYS4_ACTIVE_CTRL_SET");
		off = (e && !strcmp(e, "0"));
	}
	if (getenv("XSYS4_CTRL_DBG"))
		NOTICE("[ctrl] SetActive no=%d (idx 范围 0..%d, 原 active=%d)%s",
		       no, ctrl_stack.nr_controllers - 1, ctrl_stack.active,
		       off ? "  [已禁用]" : "");
	if (off)
		return;
	/* 参数是**稳定 id**，不是栈下标。
	 *
	 * 实测（`s/h2`）：`SetActive no=2` 而当时只有 `0..1` 两层、`no=9` 而只有
	 * `0..4` —— 用下标判定会 6739/6741 全部越界丢弃。与 `PE_get_active_controller`
	 * 对称（它 `return PE_get_controller_id(active)`，D129 起就是 id），
	 * 所以这里必须按 id 反查下标。 */
	if (layer_idx_mode()) {
		/* 上游语义：参数就是栈下标。 */
		if (no >= 0 && no < ctrl_stack.nr_controllers) {
			if (getenv("XSYS4_CTRL_DBG") && no != ctrl_stack.active)
				NOTICE("[ctrl] SetActive idx=%d (直取)", no);
			ctrl_stack.active = no;
		} else if (getenv("XSYS4_CTRL_DBG")) {
			NOTICE("[ctrl] SetActive idx=%d 越界（0..%d），忽略", no,
			       ctrl_stack.nr_controllers - 1);
		}
		return;
	}
	for (int i = 0; i < ctrl_stack.nr_controllers; i++) {
		if (ctrl_stack.id[i] == no) {
			if (getenv("XSYS4_CTRL_DBG") && i != ctrl_stack.active)
				NOTICE("[ctrl] SetActive id=%d -> idx=%d", no, i);
			ctrl_stack.active = i;
			return;
		}
	}
	if (getenv("XSYS4_CTRL_DBG"))
		NOTICE("[ctrl] SetActive 未知 id=%d（现有 %d 层），忽略",
		       no, ctrl_stack.nr_controllers);
}

/* [D316] 建层前的活动层快照，供 `EraseLayer` 决定「活动层回到哪」。
 *
 * 上游 `PE_RemoveController` 弹出后一律 `active = nr-1`（回到栈顶）。但
 * `[BG-LAYER]` 的追踪显示：AIN 在 `EraseLayer(10)` 之后创建「情报地图」部件时，
 * 期望活动层是**地图层(7)**，而栈顶已经是我们这边的 ADV 层(9) —— 两边从此错开，
 * 地图件落进 ADV 层，AIN 之后只开关地图层，永远收不掉它。
 *
 * `XSYS4_CTRL_ACTIVE_RESTORE=1` 改成「回到建层前的那一层」做 A/B。 */
static int ctrl_restore[PARTS_CONTROLLER_STACK_MAX];
static bool ctrl_active_restore(void)
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("XSYS4_CTRL_ACTIVE_RESTORE");
		on = (e && strcmp(e, "0") != 0);
	}
	return on;
}

int PE_AddController(int index)
{
	if (index != -1)
		VM_ERROR("index != -1 not supported (got %d)", index);
	/* [D312] 这条断言原本假设「活动层永远是栈顶」—— 因为当年
	 * `SetActiveController` 是空实现，active 只可能在 `PE_AddController`
	 * 里被改，自然恒等于栈顶。
	 *
	 * 现在 setter 真的生效了（AIN 的 `parts::detail::SetActiveLayer` 会把
	 * 活动层切到低层），紧接着再 `AddLayer` 就会命中这里。实测：
	 * `NetworkConfirmation` 装载后 `AddLayer` 直接 VM_ERROR 退出。
	 *
	 * `AddLayer` 自身的契约是「压一层并让它成为活动层」—— 函数末尾本来就会
	 * `ctrl_stack.active = no`。所以这里不再当作致命错误，只留痕并继续，
	 * 由函数末尾把 active 归位到新栈顶。 */
	if (ctrl_stack.nr_controllers > 0 &&
			ctrl_stack.active != ctrl_stack.nr_controllers - 1) {
		if (getenv("XSYS4_CTRL_DBG"))
			NOTICE("[ctrl] AddLayer 前 active=%d 不在栈顶(%d)，按压栈语义归位",
			       ctrl_stack.active, ctrl_stack.nr_controllers - 1);
	}
	if (ctrl_stack.nr_controllers >= PARTS_CONTROLLER_STACK_MAX)
		VM_ERROR("controller stack overflow");

	int no = ctrl_stack.nr_controllers++;
	ctrl_restore[no] = ctrl_stack.active;
	ctrl_stack.opaque[no] = false;
	ctrl_stack.show[no] = true;
	/* [D129] 返回稳定 ID，不是栈下标。AIN 会把它存进
	 * SceneStack.LayerPartsNumber，之后与 GetActiveLayer() 比较。 */
	ctrl_stack.id[no] = ctrl_stack.next_id++;
	ctrl_stack.active = no;
	/* [D289] 把 Load 期间建出的部件迁到刚建好的这一层。
	 * 这正是 AIN `Activity@Load → SceneStack@AddLayer` 顺序下想要的归属：
	 * 部件属于「即将用它的那一层」，而不是「上一层」。
	 * 之后 AIN 按这层的 LayerPartsNumber 去 EraseLayer 才能真正释放掉。 */
	if (ctrl_pending_n > 0) {
		int moved = 0;
		for (int i = 0; i < ctrl_pending_n; i++) {
			struct parts *pp = parts_try_get(ctrl_pending_nos[i]);
			if (!pp)
				continue;
			/* [D315] 段内明细：`[BG-LAYER]` 要回答「400 是不是被搬过来的」。
			 * 只看总数（moved=3/6/1）分不出是谁，必须把 390..430 段点名。 */
			if (getenv("XSYS4_CTRL_DBG") &&
			    pp->no >= 900000390 && pp->no <= 900000430)
				NOTICE("[ctrl] pending-move detail no=%d -> idx=%d", pp->no, no);
			pp->controller_no = no;
			moved++;
		}
		if (getenv("XSYS4_CTRL_DBG"))
			NOTICE("[ctrl] pending-move %d 个部件 -> idx=%d id=%d",
			       moved, no, ctrl_stack.id[no]);
		ctrl_pending_n = 0;
	}
	if (getenv("XSYS4_CTRL_DBG")) {
		/* [D268] 把「栈下标 ↔ 稳定 id」成对打出来。之前只有直方图，
		 * 看不出 AIN 手里记的 id 到底对应哪个下标 —— 而本轮的根因正是
		 * 「建部件时的 active」和「AIN 后来 Erase 的 id」不是同一个层。 */
		NOTICE("[ctrl] add idx=%d id=%d", no, ctrl_stack.id[no]);
		PE_DebugControllerHistogram("add-controller");
	}
	/* [D316] 上游返回栈下标；D129 返回稳定 id。见 layer_idx_mode()。 */
	return layer_idx_mode() ? no : ctrl_stack.id[no];
}

// 从栈上移除位置 `index` 处的控制器，释放属于它的所有
// 部件，并把它们的部件号写进 `erase_number_list`。
// `index == -1` 表示「移除当前活动控制器」。
// 实际游戏里只会传 -1，
// 而那一刻活动控制器总是栈顶，
// 所以这里退化成一次简单的出栈。
void PE_RemoveController(struct page **erase_number_list, int index)
{
	/* index == -1 表示「活动控制器」；兰斯10 也会从
	 * SceneStack@EraseLayer 传一个明确的控制器号（例如 0）。
	 * 两者都接受；下面的部件收集是按 ctrl_no 索引的。（D50） */
	int nr = ctrl_stack.nr_controllers;
	if (nr == 0)
		return;
	int ctrl_no;
	if (index == -1) {
		ctrl_no = ctrl_stack.active;
	} else {
		/* [D129] AIN 传的是 AddLayer 返回的稳定 ID，不是栈下标。
		 * [D316] `XSYS4_LAYER_IDX=1` 时改按上游语义：参数是下标，
		 * 越界回退到 active（上游 `PE_RemoveController` 就是这么写的 ——
		 * `controller_id` 不合法时用 `ctrl_stack.active`）。 */
		if (layer_idx_mode())
			ctrl_no = (index >= 0 && index < nr) ? index : ctrl_stack.active;
		else
			ctrl_no = PE_get_controller_index(index);
		if (ctrl_no < 0 || ctrl_no == PARTS_CONTROLLER_SYSTEM_OVERLAY ||
		    ctrl_no >= nr) {
			/* 已经弹过的层再 Erase 一次是正常路径（SceneStack@1 会重复调），
			 * 不该杀进程。 */
			if (getenv("XSYS4_CTRL_DBG"))
				NOTICE("[ctrl] RemoveController stale id=%d nctrl=%d", index, nr);
			return;
		}
	}

	// 收集并释放属于这个控制器的部件
	int released = 0;
	if (getenv("XSYS4_CTRL_DBG"))
		PE_DebugControllerHistogram("remove-pre");
	struct parts *p = TAILQ_FIRST(&parts_list);
	while (p) {
		struct parts *next = TAILQ_NEXT(p, parts_list_entry);
		if (p->controller_no == ctrl_no) {
			*erase_number_list = array_pushback(*erase_number_list,
					(union vm_value){.i = p->delegate_index}, AIN_ARRAY_INT, -1);
			parts_release(p->no);
			released++;
		}
		p = next;
	}
	if (getenv("XSYS4_CTRL_DBG"))
		NOTICE("[ctrl] RemoveController index=%d -> ctrl=%d nctrl=%d released=%d",
		       index, ctrl_no, nr, released);

	ctrl_stack.nr_controllers--;
	/* 弹出后上面的下标整体左移：显示状态、稳定 ID 都要跟着搬。
	 * [D129] 部件内部记的是下标，也必须同步左移，否则渲染分层错位。 */
	for (int i = ctrl_no; i < ctrl_stack.nr_controllers; i++) {
		ctrl_stack.show[i] = ctrl_stack.show[i + 1];
		ctrl_stack.opaque[i] = ctrl_stack.opaque[i + 1];
		ctrl_stack.id[i] = ctrl_stack.id[i + 1];
		ctrl_restore[i] = ctrl_restore[i + 1];
	}
	ctrl_stack.show[ctrl_stack.nr_controllers] = true;
	ctrl_stack.opaque[ctrl_stack.nr_controllers] = false;
	ctrl_stack.id[ctrl_stack.nr_controllers] = -1;

	struct parts *pp;
	PARTS_LIST_FOREACH(pp) {
		if (pp->controller_no > ctrl_no &&
		    pp->controller_no != PARTS_CONTROLLER_SYSTEM_OVERLAY)
			pp->controller_no--;
	}

	if (ctrl_stack.nr_controllers == 0) {
		PE_AddController(-1);
	} else if (ctrl_active_restore()) {
		/* [D316] 回到「建这一层之前活动的那一层」。越界就退回栈顶。 */
		int r = ctrl_restore[ctrl_no];
		if (r < 0 || r >= ctrl_stack.nr_controllers)
			r = ctrl_stack.nr_controllers - 1;
		if (getenv("XSYS4_CTRL_DBG"))
			NOTICE("[ctrl] Erase 后恢复 active=%d（栈顶 %d）",
			       r, ctrl_stack.nr_controllers - 1);
		ctrl_stack.active = r;
	} else {
		ctrl_stack.active = ctrl_stack.nr_controllers - 1;
	}
}

void PE_set_active_controller(int controller_no)
{
	/* [D129] AIN 的 SetActiveLayer 传稳定 ID，内部 active 存下标。 */
	int idx = controller_no == PARTS_CONTROLLER_SYSTEM_OVERLAY
		? PARTS_CONTROLLER_SYSTEM_OVERLAY
		: PE_get_controller_index(controller_no);
	if (idx == PARTS_CONTROLLER_SYSTEM_OVERLAY ||
			(idx >= 0 && idx < ctrl_stack.nr_controllers)) {
		if (getenv("XSYS4_CTRL_DBG"))
			NOTICE("[ctrl] setactive id=%d -> idx=%d", controller_no, idx);
		ctrl_stack.active = idx;
	}
	else if (getenv("XSYS4_CTRL_DBG"))
		/* 场景重建后 AIN 可能拿旧 ID 来 SetActiveLayer；这是正常的迟到
		 * 事件，不该 VM_ERROR(_Noreturn) 直接杀进程。 */
		NOTICE("[ctrl] SetActiveLayer stale id=%d nctrl=%d", controller_no,
		       ctrl_stack.nr_controllers);
}

int PE_get_active_controller(void)
{
	/* [D115] XSYS4_CTRL_DBG=1：AIN 的 parts::detail::GetActiveLayer 走
	 * PartsEngine f[6]，若这里安静而 PE_Init 被打，说明 lib27 表与 AIN 索引错位。 */
	if (getenv("XSYS4_CTRL_DBG")) {
		/* 只在值变化时打印：IsActive 每帧都问，全打会把日志冲爆，
		 * 而这里要的正是"这一次返回了什么"。 */
		static int n, la = -99, ln = -99;
		int a = ctrl_stack.active, c = PE_get_nr_controllers();
		if (n++ < 5 || a != la || c != ln)
			NOTICE("[ctrl] #%d active=%d nctrl=%d", n, a, c);
		la = a; ln = c;
	}
	/* [D316] 与 AddController 的返回口径必须一致，否则 AIN 存的值对不上。 */
	return layer_idx_mode() ? ctrl_stack.active
	                        : PE_get_controller_id(ctrl_stack.active);
}

int PE_get_system_controller(void)
{
	return PARTS_CONTROLLER_SYSTEM_OVERLAY;
}

int PE_get_nr_controllers(void)
{
	return ctrl_stack.nr_controllers;
}

void PE_parts_set_want_save(int parts_no, bool want_save)
{
	parts_get(parts_no)->want_save = want_save;
}

/* [D310] `IsWantSaveBackScene` 的读取端。见 PartsEngine.c 里的说明：
 * 恒返回 false 会让 AIN 以为「没有任何部件属于背景场景」，于是它既不会
 * 在 SaveBackScene 之后把这些部件收起来，剧情切到新背景时旧画面就留在
 * 上面（世界地图盖住战舰通道）。 */
bool PE_parts_get_want_save_back_scene(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->want_save_back_scene : false;
}

void PE_parts_set_want_save_back_scene(int parts_no, bool want)
{
	parts_get(parts_no)->want_save_back_scene = want;
}

float PE_parts_get_absolute_x(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? (float)parts->global.pos.x : 0.0f;
}

float PE_parts_get_absolute_y(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? (float)parts->global.pos.y : 0.0f;
}

int PE_parts_get_absolute_z(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->global.z : 0;
}

void PE_parts_set_lock_input_state(int parts_no, bool lock)
{
	parts_get(parts_no)->lock_input_state = lock;
}

bool PE_init_parts_movie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_movie *movie = parts_get_movie(parts, state);

	int sp_no = sact_SP_GetUnuseNum(0);
	struct sact_sprite *sp = sact_create_sprite(sp_no, width, height, bg_r, bg_g, bg_b, 255);
	if (!sp)
		return false;
	// 动画帧由部件引擎通过 parts_render() 合成，
	// 所以把绑定的精灵从场景里藏起来，避免画两遍。
	sprite_set_show(sp, false);

	struct texture *tex = sprite_get_texture(sp);
	movie->sprite_no = sp_no;
	movie->common.texture = *tex; // XXX: textures normally shouldn't be copied like this...
	parts_set_dims(parts, &movie->common, width, height);
	parts_dirty(parts);
	return true;
}

int PE_get_movie_sprite(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_MOVIE)
		return -1;

	return parts->states[state].movie.sprite_no;
}

bool parts_3dlayer_create_plugin(struct parts_3dlayer *l)
{
	if (l->plugin >= 0)
		return false;

	int handle = ReignEngine_create_plugin(RE_SEAL_PLUGIN);
	if (handle < 0)
		return false;

	int sp_no = sact_SP_GetUnuseNum(0);
	struct sact_sprite *sp = sact_create_sprite(sp_no, 1, 1, 0, 0, 0, 255);
	if (!sp) {
		ReignEngine_ReleasePlugin(handle);
		return false;
	}

	if (!ReignEngine_BindPlugin(handle, sp_no)) {
		sact_SP_Delete(sp_no);
		ReignEngine_ReleasePlugin(handle);
		return false;
	}
	// 3D 内容由部件引擎通过 parts_render() 合成，所以
	// 把绑定的精灵从场景里藏起来，避免画两遍。
	sprite_set_show(sp, false);

	l->plugin = handle;
	l->sprite_no = sp_no;
	return true;
}

bool PE_CreateParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_get(parts_no);
	return parts_3dlayer_create_plugin(parts_get_3dlayer(parts, state));
}

int PE_GetParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return -1;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_3DLAYER)
		return -1;
	return parts->states[state].layer3d.plugin;
}

bool PE_ReleaseParts3DLayerPluginID(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts || parts->states[state].type != PARTS_3DLAYER)
		return false;
	struct parts_3dlayer *l = &parts->states[state].layer3d;
	if (l->plugin < 0)
		return false;
	ReignEngine_ReleasePlugin(l->plugin);
	l->plugin = -1;
	if (l->sprite_no >= 0) {
		sact_SP_Delete(l->sprite_no);
		l->sprite_no = -1;
	}
	return true;
}
