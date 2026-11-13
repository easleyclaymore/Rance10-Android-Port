/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#ifndef SYSTEM4_PARTS_INTERNAL_H
#define SYSTEM4_PARTS_INTERNAL_H

#include <cglm/types.h>
#include "gfx/gfx.h"
#include "gfx/font.h"
#include "queue.h"
#include "scene.h"
#include "swf.h"

typedef struct cJSON cJSON;
struct string;
struct hash_table;

// NOTE: actual value is +1
enum parts_state_type {
	PARTS_STATE_DEFAULT = 0,
	PARTS_STATE_HOVERED = 1,
	PARTS_STATE_CLICKED = 2,
#define PARTS_NR_STATES 3
};

enum parts_motion_type {
	PARTS_MOTION_POS,
	PARTS_MOTION_ALPHA,
	PARTS_MOTION_CG,
	PARTS_MOTION_HGAUGE_RATE,
	PARTS_MOTION_VGAUGE_RATE,
	PARTS_MOTION_NUMERAL_NUMBER,
	PARTS_MOTION_MAG_X,
	PARTS_MOTION_MAG_Y,
	PARTS_MOTION_ROTATE_X,
	PARTS_MOTION_ROTATE_Y,
	PARTS_MOTION_ROTATE_Z,
	PARTS_MOTION_VIBRATION_SIZE
#define PARTS_NR_MOTION_TYPES (PARTS_MOTION_VIBRATION_SIZE+1)
};

union parts_motion_param {
	int i;
	float f;
	struct {
		int x;
		int y;
	};
};

struct parts_motion {
	TAILQ_ENTRY(parts_motion) entry;
	enum parts_motion_type type;
	union parts_motion_param begin;
	union parts_motion_param end;
	int begin_time;
	int end_time;
};

TAILQ_HEAD(parts_motion_list, parts_motion);

struct sound_motion {
	TAILQ_ENTRY(sound_motion) entry;
	int begin_time;
	int sound_no;
	bool played;
};

struct parts_text_char {
	Texture t;
	char ch[4];
	int advance;
	Point off;
};

struct parts_text_line {
	struct parts_text_char *chars;
	int nr_chars;
	unsigned height;
	float width;
};

enum parts_type {
	PARTS_UNINITIALIZED,
	PARTS_CG,
	PARTS_TEXT,
	PARTS_ANIMATION,
	PARTS_NUMERAL,
	PARTS_HGAUGE,
	PARTS_VGAUGE,
	PARTS_CONSTRUCTION_PROCESS,
	PARTS_FLASH,
	PARTS_FLAT,
	PARTS_MOVIE,
	PARTS_RECT_DETECTION,
	PARTS_LAYOUT_BOX,
	PARTS_3DLAYER,
#define PARTS_NR_TYPES (PARTS_3DLAYER+1)
};

struct parts_common {
	Texture texture;
	int w, h;
	Point origin_offset;
	Rectangle hitbox;
	Rectangle surface_area;
};

struct parts_cg {
	struct parts_common common;
	int no;
	struct string *name;
};

struct parts_text {
	struct parts_common common;
	unsigned nr_lines;
	struct parts_text_line *lines;
	int line_space;
	struct { float x; int y; } cursor;
	struct text_style ts;
};

struct parts_animation {
	struct parts_common common;
	struct string *cg_name;
	unsigned start_no;
	unsigned nr_frames;
	Texture *frames;
	unsigned frame_time;
	unsigned elapsed;
	unsigned current_frame;
};

enum parts_numeral_font_type {
	// each digit has a separate CG (only first CG number stored in `cg_no` member)
	PARTS_NUMERAL_FONT_SEPARATE = 0,
	// digits packed into single CG
	PARTS_NUMERAL_FONT_COMBINED = 1,
	// each digit has a separate CG (CG numbers stored in `width` member)
	PARTS_NUMERAL_FONT_SEPARATE2 = 2,
};

struct parts_numeral_font {
	enum parts_numeral_font_type type;
	int cg_no;
	int width[12];
	Texture cg[12];
};

struct parts_numeral {
	struct parts_common common;
	bool have_num;
	int num;
	int space;
	int show_comma;
	int length;
	int font_no;
};

struct parts_gauge {
	struct parts_common common;
	Texture cg;
	int cg_no;
	float rate;
};

// Serialized in save data. Do not reorder.
enum parts_cp_op_type {
	PARTS_CP_CREATE,
	PARTS_CP_CREATE_PIXEL_ONLY,
	PARTS_CP_CG,
	PARTS_CP_FILL,
	PARTS_CP_FILL_ALPHA_COLOR,
	PARTS_CP_FILL_AMAP,
	PARTS_CP_DRAW_RECT,
	PARTS_CP_DRAW_CUT_CG,
	PARTS_CP_COPY_CUT_CG,
	PARTS_CP_DRAW_TEXT,
	PARTS_CP_COPY_TEXT,
	PARTS_CP_GRAY_FILTER,
	PARTS_CP_FILL_WITH_ALPHA,
#define PARTS_NR_CP_TYPES (PARTS_CP_FILL_WITH_ALPHA+1)
};

struct parts_cp_create {
	int w;
	int h;
};

struct parts_cp_cg {
	struct string *name;
};

struct parts_cp_fill {
	int x, y, w, h;
	int r, g, b, a;
};

struct parts_cp_cut_cg {
	struct string *cg_name;
	int dx, dy, dw, dh;
	int sx, sy, sw, sh;
	int interp_type;
};

struct parts_cp_text {
	struct string *text;
	int x, y;
	int line_space;
	struct text_style style;
};

struct parts_cp_filter {
	int x, y, w, h;
	bool full_size;
};

struct parts_cp_op {
	TAILQ_ENTRY(parts_cp_op) entry;
	enum parts_cp_op_type type;
	union {
		struct parts_cp_create create;
		struct parts_cp_cg cg;
		struct parts_cp_fill fill;
		struct parts_cp_cut_cg cut_cg;
		struct parts_cp_text text;
		struct parts_cp_filter filter;
	};
};

struct parts_construction_process {
	struct parts_common common;
	TAILQ_HEAD(, parts_cp_op) ops;
};

enum parts_flash_blend_mode {
	PARTS_FLASH_BLEND_NORMAL0    = 0,
	PARTS_FLASH_BLEND_NORMAL1    = 1,
	PARTS_FLASH_BLEND_LAYER      = 2,
	PARTS_FLASH_BLEND_MULTIPLY   = 3,
	PARTS_FLASH_BLEND_SCREEN     = 4,
	PARTS_FLASH_BLEND_LIGHTEN    = 5,
	PARTS_FLASH_BLEND_DARKEN     = 6,
	PARTS_FLASH_BLEND_DIFFERENCE = 7,
	PARTS_FLASH_BLEND_ADD        = 8,
	PARTS_FLASH_BLEND_SUBTRACT   = 9,
	PARTS_FLASH_BLEND_INVERT     = 10,
	PARTS_FLASH_BLEND_ALPHA      = 11,
	PARTS_FLASH_BLEND_ERASE      = 12,
	PARTS_FLASH_BLEND_OVERLAY    = 13,
	PARTS_FLASH_BLEND_HARDLIGHT  = 14,
};

enum parts_draw_filter {
	PARTS_DRAW_FILTER_NORMAL   = 0,
	PARTS_DRAW_FILTER_ADDITIVE = 1,
	PARTS_DRAW_FILTER_MULTIPLY = 2,
	PARTS_DRAW_FILTER_SCREEN   = 3,
};

struct parts_flash_object {
	TAILQ_ENTRY(parts_flash_object) entry;
	uint16_t depth;
	uint16_t character_id;
	mat4 matrix;
	struct swf_cxform_with_alpha color_transform;
	enum parts_flash_blend_mode blend_mode;
};

struct parts_flash {
	struct parts_common common;
	struct string *name;
	struct swf *swf;

	struct swf_tag *tag;
	bool has_ended;
	bool stopped;
	unsigned elapsed;
	int current_frame;
	struct hash_table *dictionary;
	struct hash_table *bitmaps;  // bitmap character id -> struct texture *
	struct hash_table *sprites;  // sprite character id -> struct parts_flash_object *
	TAILQ_HEAD(, parts_flash_object) display_list;
};

struct flat_layer_state {
	int current_frame;
	bool stopped;
	bool suppress_advance;
	int jump_target;  // pending jump frame, -1 = none
	// Per-timeline: last matched script key index for change detection
	// (-2 = uninitialized, -1 = no match, >= 0 = key index)
	int *last_script_keys;
	// Per-timeline: child layer state (for TIMELINE libs), NULL otherwise
	struct flat_layer_state **children;
	size_t nr_timelines;
};

// Per-frame CG list for a STOP_MOTION library. lib_indices[k] is
// the library index of the CG to display at frame k.
struct flat_stop_motion_frames {
	int *lib_indices;
	int count;
};

struct parts_flat {
	struct parts_common common;
	struct string *name;
	struct flat *flat;
	bool stopped;
	bool needs_advance;
	unsigned elapsed;
	int end_frame;
	int pending_seek_delta;
	struct flat_layer_state *root_state;
	size_t nr_libraries;
	Texture *textures;  // indexed by library index (only CG libs have valid textures)
	// Indexed by library index. Only entries for STOP_MOTION libraries have lib_indices populated.
	struct flat_stop_motion_frames *stop_motion_frames;
};

struct parts_movie {
	struct parts_common common;
	int sprite_no;  // SACT sprite number used as movie render target
};

enum parts_layout_type {
	PARTS_LAYOUT_FREE       = 0,  // no automatic layout
	PARTS_LAYOUT_VERTICAL   = 1,
	PARTS_LAYOUT_HORIZONTAL = 2,
};

// In AliceSoft's PartsEngine implementation, LayoutBox is a component type
// that is NOT per-state. We store it per-state for uniformity with other
// component types, but parts_get_layout_box() and all code in layoutbox.c
// operate on states[0] only. This is safe as long as game scripts never
// convert a LayoutBox parts to/from another component type.
struct parts_layout_box {
	struct parts_common common;
	enum parts_layout_type layout_type;
	bool wrap;
	int wrap_size;
	int align;
	int padding_top;
	int padding_bottom;
	int padding_left;
	int padding_right;
};

struct parts_3dlayer {
	struct parts_common common;
	int plugin;    // ReignEngine plugin handle (-1 = none)
	int sprite_no; // SACT sprite used as render target
};

struct parts_state {
	enum parts_type type;
	/* Rance10 component-type number last passed to PE_SetComponentType(),
	 * or -1 when the state was created implicitly (then it is derived from
	 * `type`). Kept per state because PE_GetComponentType() must round-trip
	 * exactly what the game set: the AIN Activity layer switches on this
	 * number to pick the wrapper class. */
	int component_type;
	union {
		struct parts_common common;
		struct parts_cg cg;
		struct parts_text text;
		struct parts_animation anim;
		struct parts_numeral num;
		struct parts_gauge gauge;
		struct parts_construction_process cproc;
		struct parts_flash flash;
		struct parts_flat flat;
		struct parts_movie movie;
		struct parts_layout_box layout_box;
		struct parts_3dlayer layer3d;
	};
};

TAILQ_HEAD(parts_list, parts);

struct parts_params {
	int z;
	Point pos;
	bool show;
	uint8_t alpha;
	struct { float x, y; } scale;
	struct { float x, y, z; } rotation;
	SDL_Color add_color;
	SDL_Color multiply_color;
	int alpha_clipper_parts_no;
};

struct parts {
	struct sprite sp;
	enum parts_state_type state;
	struct parts_state states[PARTS_NR_STATES];
	TAILQ_ENTRY(parts) parts_list_entry;
	TAILQ_ENTRY(parts) child_list_entry;
	TAILQ_ENTRY(parts) dirty_list_entry;
	struct parts_list children;
	struct parts_params local;
	struct parts_params global;
	struct parts *parent;
	int dirty;
	int no;
	int delegate_index;
	int sprite_deform;
	bool clickable;
	bool pass_cursor;
	bool lock_input_state;
	bool want_save;
	bool want_save_back_scene;
	bool draggable;
	int on_cursor_sound;
	int on_click_sound;
	int origin_mode;
	int pending_parent;
	int linked_to;
	int linked_from;
	bool is_hovered;
	int hover_time;
	int draw_filter;
	bool message_window;
	int margin_top;
	int margin_bottom;
	int margin_left;
	int margin_right;
	struct parts_motion_list motion;
	int controller_no;

	/* 用户组件 (component type 15)。
	 * EX 的 数据 = ['オブジェクト名','TextButton','参数','标题,从头开始'] 声明
	 * 的是“引擎内建控件”，不是游戏自绘的图。activity 层把控件种类和显示文字
	 * 填进来，绘制由 parts_render_user_component() 负责。
	 * 三个字段都是 GBK 字节串；uc_text 是首次绘制时构建的字形缓存。 */
	char *uc_object;
	char *uc_label;
	struct parts_text uc_text;
	bool uc_text_valid;
	/* 用户组件的键值数据（EX 数据 = [k,v,k,v,...] 原样留存）。
	 * 游戏侧通过 PartsEngine.GetUserComponentData(no,"参数") 之类读回去，
	 * 控件真正的行为（点击、绘制）由游戏的 AIN 代码实现。 */
	char **uc_data;
	int uc_data_n;
};

#define PARTS_LIST_FOREACH(iter) TAILQ_FOREACH(iter, &parts_list, parts_list_entry)
#define PARTS_LIST_FOREACH_REVERSE(iter) TAILQ_FOREACH_REVERSE(iter, &parts_list, parts_list, parts_list_entry)
#define PARTS_FOREACH_CHILD(iter, parent) TAILQ_FOREACH(iter, &parent->children, child_list_entry)

// parts.c
extern struct parts_list parts_list;

// A "controller" is stored internally by its position in the stack (0 = bottom).
// AIN, however, distinguishes a stable layer *ID* from a stack *index*:
//   GetLayerID(index) / GetLayerIndex(id)  (PartsFunc cases 4 / 3)
// SceneStack remembers the ID returned by AddLayer and compares it with
// GetActiveLayer() in IsActive. If IDs were just indices, removing a layer
// from the middle would shift every higher index and the comparison would
// fail forever -> SceneStack@Join never finishes (D129 / DIALOG-STUCK).
// So the stack owns id[index] (monotonically increasing, never reused) and
// everything crossing the HLL boundary is converted between ID and index.
#define PARTS_CONTROLLER_STACK_MAX 10000
#define PARTS_CONTROLLER_SYSTEM_OVERLAY PARTS_CONTROLLER_STACK_MAX

struct parts_controller_stack {
	int nr_controllers;
	int active;  // stack INDEX (internal) or PARTS_CONTROLLER_SYSTEM_OVERLAY
	int id[PARTS_CONTROLLER_STACK_MAX];  // stable AIN-visible layer id per index
	int next_id;
	/* [D128] 每个 controller 是一个"图层"，AIN 用 SetComponentShow(图层号)
	 * 把整层一起显示/隐藏（SceneStack@IsShow）。show 为 false 时该层的
	 * 全部部件都不画、也不参与命中。 */
	/* [D128] 该图层曾经出现"铺满视图且不透明"的部件 —— 用它判定视觉上下关系。
	 * 不能每帧只按当前帧算：背景淡入的瞬间 alpha<255，遮挡层值会来回跳，
	 * 旧场景就一闪一闪地透出来。所以一旦认定就记住，只在该层被弹出时清掉。 */
	bool opaque[PARTS_CONTROLLER_STACK_MAX];
	bool show[PARTS_CONTROLLER_STACK_MAX];
};
extern struct parts_controller_stack ctrl_stack;
extern bool parts_multi_controller;

/* [D128] 图层遮挡的下界：这一层往下（含本层）不画。
 * 由"最上面那个铺满视图且不透明的部件所在图层"决定，每帧重算。 */
extern int parts_cover_layer;

/* [D128] 编号 < 100000000 的一律是图层号（部件号从 1000001000 起分配）。 */
static inline bool parts_no_is_layer(int no)
{
	return no >= 0 && no < 100000000;
}
/* 图层 + 部件自身都可见才画。 */
static inline bool parts_is_visible(const struct parts *parts)
{
	if (!parts->global.show)
		return false;
	int c = parts->controller_no;
	if (c < 0 || c >= PARTS_CONTROLLER_STACK_MAX)
		return true;
	if (!ctrl_stack.show[c])
		return false;
	return c >= parts_cover_layer;
}

struct parts *parts_try_get(int parts_no);
struct parts *parts_get(int parts_no);
/* [D128] 场景图层诊断：controller 分布 + 当前显示部件快照。 */
void PE_DebugControllerHistogram(const char *tag);
void PE_DebugDumpParts(const char *tag);
struct parts_cg *parts_get_cg(struct parts *parts, int state);
struct parts_text *parts_get_text(struct parts *parts, int state);
struct parts_animation *parts_get_animation(struct parts *parts, int state);
struct parts_numeral *parts_get_numeral(struct parts *parts, int state);
struct parts_gauge *parts_get_hgauge(struct parts *parts, int state);
struct parts_gauge *parts_get_vgauge(struct parts *parts, int state);
struct parts_construction_process *parts_get_construction_process(struct parts *parts, int state);
struct parts_flash *parts_get_flash(struct parts *parts, int state);
struct parts_flat *parts_get_flat(struct parts *parts, int state);
struct parts_movie *parts_get_movie(struct parts *parts, int state);
struct parts_layout_box *parts_get_layout_box(struct parts *parts);
struct parts_3dlayer *parts_get_3dlayer(struct parts *parts, int state);
void parts_set_pos(struct parts *parts, Point pos);
void parts_set_global_pos(Point pos);
void parts_set_dims(struct parts *parts, struct parts_common *common, int w, int h);
void parts_set_scale_x(struct parts *parts, float mag);
void parts_set_scale_y(struct parts *parts, float mag);
void parts_set_rotation_z(struct parts *parts, float rot);
void parts_set_alpha(struct parts *parts, int alpha);
void parts_set_state(struct parts *parts, enum parts_state_type state);
void parts_release(int parts_no);
void parts_release_all(void);
void parts_set_surface_area(struct parts *parts, struct parts_common *common, int x, int y, int w, int h);
extern bool parts_message_window_show;

extern struct parts_numeral_font *parts_numeral_fonts;
extern int parts_nr_numeral_fonts;

// for save.c
void parts_list_resort(struct parts *parts);
void parts_component_dirty(struct parts *parts);
void parts_recalculate_hitbox(struct parts *parts);
void parts_state_reset(struct parts_state *state, enum parts_type type);
bool parts_cg_set(struct parts *parts, struct parts_cg *cg, struct string *cg_name);
bool parts_cg_set_by_index(struct parts *parts, struct parts_cg *cg, int cg_no);
void parts_text_append(struct parts *parts, struct parts_text *t, struct string *text);
bool parts_animation_set_cg_by_index(struct parts *parts, struct parts_animation *anim,
		int cg_no, int nr_frames, int frame_time);
bool parts_animation_set_cg(struct parts *parts, struct parts_animation *anim,
		struct string *cg_name, int start_no, int nr_frames, int frame_time);
void parts_numeral_font_init(struct parts_numeral_font *font);
bool parts_numeral_set_number(struct parts *parts, struct parts_numeral *num, int n);
bool parts_gauge_set_cg(struct parts *parts, struct parts_gauge *g, struct string *cg_name);
bool parts_gauge_set_cg_by_index(struct parts *parts, struct parts_gauge *g, int cg_no);
void parts_hgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate);
void parts_vgauge_set_rate(struct parts *parts, struct parts_gauge *g, float rate);
bool parts_3dlayer_create_plugin(struct parts_3dlayer *l);
struct text_style *parts_default_text_style(void);

/* 用户组件 (component type 15)：登记控件种类与显示文字。
 * 两者都是 GBK 字节串，函数内部自行 strdup。 */
void parts_set_user_component(struct parts *parts, const char *object, const char *label);
void parts_set_user_component_data(struct parts *parts, const char *key, const char *value);
const char *parts_get_user_component_data(struct parts *parts, const char *key);

// text.c
void parts_text_free(struct parts_text *t);
struct string *parts_text_line_get(struct parts_text_line *line);
struct string *parts_text_get(struct parts_text *t);
void parts_uc_text_build(struct parts_text *t, const char *label, float size, SDL_Color color);
int parts_uc_text_width(struct parts_text *t);
int parts_uc_text_height(struct parts_text *t);

// render.c
void parts_render_init(void);
void parts_render_update(void);
/* 在 update 阶段（渲染之外）准备用户组件的文字纹理。 */
void parts_uc_text_prepare(struct parts *parts);
void parts_engine_dirty(void);
void parts_engine_clean(void);
void parts_dirty(struct parts *parts);
void parts_sprite_render(struct sprite *sp);
void parts_render(struct parts *parts);
void parts_render_family(struct parts *parts);

// motion.c
void parts_clear_motion(struct parts *parts);
void parts_add_motion(struct parts *parts, struct parts_motion *motion);

// input.c
extern bool parts_began_click;
void parts_input_reset_drag(struct parts *parts);

// message.c
enum parts_message_type {
	PARTS_MSG_MOUSE_ENTER    = 1,
	PARTS_MSG_MOUSE_MOVE     = 2,
	PARTS_MSG_MOUSE_LEAVE    = 3,
	PARTS_MSG_MOUSE_WHEEL    = 4,
	PARTS_MSG_MOUSE_CLICK    = 5,
	PARTS_MSG_MOUSE_ON       = 6,
	PARTS_MSG_DRAG_BEGIN     = 7,
	PARTS_MSG_DRAGGING       = 8,
	PARTS_MSG_DRAG_END       = 9,
	PARTS_MSG_DROP_ENTER     = 10,
	PARTS_MSG_DROP_ON        = 11,
	PARTS_MSG_DROPPED        = 12,
	PARTS_MSG_DROP_LEAVE     = 13,
	PARTS_MSG_KEY_TRIGGER    = 14,
	PARTS_MSG_KEY_DOWN       = 15,
	PARTS_MSG_KEY_UP         = 17,
	/* [D115] AIN 侧 CPartsMessageManager@CallDelegate 的 switch 编号与本枚举
	 * 不一致（实测 4=左键点击、5=双击、6=OnCursor）。消息类型最终由 AIN 解释，
	 * 所以发消息时用 AIN 的编号；本枚举只用于 C 侧语义。
	 * 目前只验证了左键点击，其余类型保持原值，遇到再逐个校准。 */
	PARTS_MSG_MOUSE_CLICK_AIN = 4,
};

void parts_msg_push(struct parts* parts, int type, const char *fmt, ...);

// construction.c
void parts_cp_op_free(struct parts_cp_op *op);
void parts_add_cp_op(struct parts_construction_process *cproc, struct parts_cp_op *op);
bool parts_build_construction_process(struct parts *parts,
		struct parts_construction_process *cproc);
bool parts_clear_construction_process(struct parts_construction_process *cproc);

// flash.c
void parts_flash_free(struct parts_flash *f);
bool parts_flash_load(struct parts *parts, struct parts_flash *f, struct string *filename);
bool parts_flash_update(struct parts_flash *f, int passed_time);
bool parts_flash_seek(struct parts_flash *f, int frame);

// flat.c
void parts_flat_free(struct parts_flat *f);
bool parts_flat_load(struct parts *parts, struct parts_flat *f, struct string *filename);
bool parts_flat_update(struct parts_flat *f, int passed_time);
int parts_flat_find_library(struct flat *fl, const char *name);
int parts_flat_stop_motion_get_cg_lib(struct parts_flat *f, int sm_lib_idx, int local);

struct flat_emitter;
struct flat_key_data_graphic;

struct flat_emitter_particle {
	vec2 pos;          // emitter-space position (pixels)
	vec2 scale;
	vec3 rot;          // degrees (x, y, z)
	float fade_alpha;  // 0-1.0
	int cg_lib_idx;    // CG library index of the texture for this particle
};

// Per-key emitter properties after applying the emitter's inherit_* flags.
struct flat_emitter_layer_effective {
	vec2 pos;
	bool reverse_lr, reverse_tb;
	float alpha;
	vec3 add_color;
	vec3 mul_color;
	int draw_filter;
	bool use_scale;
	bool use_rotation;
	bool use_origin;
};

#define FLAT_MAX_ANCESTOR_DEPTH 32
struct flat_key_stack {
	const struct flat_key_data_graphic *keys[FLAT_MAX_ANCESTOR_DEPTH];
	int count;
};

typedef void (*flat_emitter_particle_fn)(const struct flat_emitter_particle *p,
		void *ud);
void parts_flat_align_offset(int align, int w, int h, vec2 out);
bool parts_flat_emitter_get_align_offset(struct parts_flat *f, int emitter_lib_idx, vec2 out);
void parts_flat_foreach_emitter_particle(struct parts_flat *f, int emitter_lib_idx,
		const struct flat_key_data_graphic *keys,
		int birth_frame, int age, int frame_count,
		flat_emitter_particle_fn fn, void *ud);
void parts_flat_build_layer_matrix(const struct flat_key_data_graphic *key,
		vec2 pos,
		bool use_rotation, bool use_scale, bool use_origin,
		bool reverse_lr, bool reverse_tb,
		mat4 out);
void parts_flat_build_emitter_base_matrix(const struct flat_emitter *em,
		const struct flat_key_stack *stack, mat4 out);
void parts_flat_emitter_resolve_layer(
		const struct flat_emitter *em,
		const struct flat_key_data_graphic *key,
		float parts_alpha, float layer_alpha,
		struct flat_emitter_layer_effective *out);

// layoutbox.c
void parts_do_layout(struct parts *parts);

// debug.c
struct sprite;
void parts_debug_init(void);
cJSON *parts_engine_to_json(struct sprite *sp, bool verbose);
cJSON *parts_sprite_to_json(struct sprite *sp, bool verbose);

static inline bool parts_state_valid(int state)
{
	return state >= 0 && state <= 2;
}

#endif /* SYSTEM4_PARTS_INTERNAL_H */
