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

// 注意：实际值要 +1
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
	/* [D294] EX `文本エリア = (x, y, 0, 0)` 的文本起始偏移。
	 * 消息窗口的文本件自身高度是 0，`parts_render_text()` 原来只用
	 * `global.pos + origin_offset` 起画 —— 于是正文从 y=1080（屏幕底边）
	 * 开始画，只露最底一行（`s88` 的「希露：肯来囚的日记。」贴在最底部）。
	 * EX 明明给了 (337,177) 的排版原点，却从没人用过。 */
	int area_x, area_y;
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
	// 每个数字一张独立 CG（`cg_no` 成员里只存第一个 CG 号）
	PARTS_NUMERAL_FONT_SEPARATE = 0,
	// 数字打包进一张 CG
	PARTS_NUMERAL_FONT_COMBINED = 1,
	// 每个数字一张独立 CG（CG 号存在 `width` 成员里）
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

// 会序列化进存档。不要重排顺序。
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
	/* [D383] 线段：兰斯10 的「回合播报 / 世界地图战线」全靠它。
	 * 追加在**末尾**（这个枚举会序列化进存档，只能追加、不能重排）。 */
	PARTS_CP_DRAW_LINE,
#define PARTS_NR_CP_TYPES (PARTS_CP_DRAW_LINE+1)
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

/* [D383] 一条线段（端点 + RGBA）。 */
struct parts_cp_line {
	int x0, y0, x1, y1;
	int r, g, b, a;
};

struct parts_cp_op {
	TAILQ_ENTRY(parts_cp_op) entry;
	enum parts_cp_op_type type;
	union {
		struct parts_cp_create create;
		struct parts_cp_cg cg;
		struct parts_cp_fill fill;
		struct parts_cp_line line;
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
	// 每条时间线：上次匹配到的脚本关键帧下标，用于检测变化
	// （-2 = 未初始化，-1 = 没匹配上，>= 0 = 关键帧下标）
	int *last_script_keys;
	// 每条时间线：子图层的状态（TIMELINE 库用），否则是 NULL
	struct flat_layer_state **children;
	size_t nr_timelines;
};

// STOP_MOTION 库里每帧的 CG 列表。lib_indices[k] 是
// 第 k 帧要显示的 CG 的库下标。
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
	// 按库下标索引。只有 STOP_MOTION 库的条目才填了 lib_indices。
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

// 在 AliceSoft 的 PartsEngine 实现里，LayoutBox 是一种
// 不按状态区分的组件类型。这里为了和其他组件类型一致
// 也按状态存，但 parts_get_layout_box() 和 layoutbox.c 里
// 所有代码都只用 states[0]。只要游戏脚本从不把 LayoutBox
// 部件转成/转出别的组件类型，这就是安全的。
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
	/* 最后一次传给 PE_SetComponentType() 的兰斯10 组件类型号，
	 * 或 -1（表示该状态是隐式创建的，此时从 `type` 推出来）。
	 * 按状态存是因为 PE_GetComponentType() 必须原样往返游戏设过的值：
	 * AIN 的 Activity 层就是靠这个号来选包装类。
	 * 存成 int 而不是枚举，是因为要能存下 -1。 */
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

/* [D396] 控件（UI widget）状态。
 *
 * 兰斯10 的组件类型 0..15 是一族**控件**：按钮 / 复选框 / 两种滚动条 /
 * 单行文本输入框 / 列表框 / 组合框 / 多行文本输入框 / 布局盒 / 单选按钮组 /
 * 消息窗口 / 微调框 / 面板 / 表单。
 *
 * 这些控件的属性**不在 EX 里**：EX 只给位置和尺寸，属性由 AIN 在运行期
 * 用 `Set*` 一族写进来，再用 `Get*` 读回去。最常见的用法是**存档前后往返**
 * （存前逐个 Get 出来、读后逐个 Set 回去），所以 getter 必须原样返回
 * setter 写进去的值 —— 返回"安全默认值"会让存档里的样式静默丢失。
 *
 * 过去这些 setter 是空实现、getter 写死 0，于是「设进去再读出来」永远是默认值。
 * 这里按部件存一份真状态。
 *
 * 存在 `struct parts` 上（不是旁挂数组）：生命周期跟着部件走，部件释放时一起
 * free，不需要手动 drop，也不会出现「环形数组被复用后串号」。
 *
 * 注意：字体不走这里。字体存在 `struct parts_text.ts`（`text_style`），
 * 已有 `PE_SetPartsFont*` 一族真实现，控件只是转发。
 */
/* [D494] Parts_SetSoundName 的槽号上限（实证只用到 2/3，留裕量到 4） */
#define PARTS_SOUND_NAME_SLOTS 4

struct parts_widget {
	/* --- 通用：底图 / 覆盖图 / flat 名（都是 GBK 字节串） --- */
	struct string *cg_name;
	struct string *override_cg_name;
	struct string *override_checked_cg_name;
	struct string *flat_name;
	struct string *override_flat_name;
	struct string *override_checked_flat_name;

	/* --- 文本 --- */
	struct string *text;
	int char_space;
	int line_space;
	int text_origin_pos_mode;

	/* --- 四组颜色（每组 RGB；AIN 的签名里没有 alpha） ---
	 * 用 `*_set` 区分「没设过」与「设成 (0,0,0)」：
	 * 前者应当回落到 EX/主题色，后者是玩家真的选了黑色。 */
	int bg_r, bg_g, bg_b;          bool bg_set;
	int ro_bg_r, ro_bg_g, ro_bg_b; bool ro_bg_set;
	int frame_r, frame_g, frame_b; bool frame_set;
	int select_r, select_g, select_b; bool select_set;

	/* --- 单行 / 多行文本输入框、微调框共用 --- */
	int max_text_length;
	bool readonly;
	/* 多行文本框的滚动条开关与四边内边距（`SetMultiTextBox*`）。 */
	bool vscroll_show, hscroll_show;
	int pad_top, pad_bottom, pad_left, pad_right;

	/* --- 列表框 --- */
	struct string **items;
	int nr_items, cap_items;
	int select_index;
	int scroll_pos;
	int line_height;
	int margin_w, margin_h;
	/* 引擎侧 hover 时写：鼠标停在第几项（`GetListBoxOnCursorItem*` 读它）。
	 * 由 parts 的 hover 判定更新，AIN 从不设。 */
	int on_cursor_index;

	/* --- 组合框：一个显示框 + 一个可展开的内嵌列表框 --- */
	int cb_text_margin_w, cb_text_margin_h;
	int cb_lb_height, cb_lb_line_height;
	int cb_lb_margin_w, cb_lb_margin_h;
	int cb_lb_scroll_pos;
	int cb_lb_select_index;
	int cb_lb_char_space;
	struct string **cb_items;
	int nr_cb_items, cap_cb_items;
	bool cb_open;

	/* --- 微调框 ---
	 * value/default/max/min/increment 是 **float**：证据是 AIN 包装类的
	 * `GetSpinBox{Value,DefaultValue,MaxValue,MinValue,IncrementalValue}`
	 * 默认返回字节码都是 `F_PUSH 0`（浮点 0），而
	 * `GetSpinBoxDecimalPlace` 是 `PUSH 0`（整数）。 */
	float spin_value, spin_default, spin_max, spin_min, spin_increment;
	int spin_decimal_place;

	/* --- 复选框 --- */
	bool checked, enabled;
	bool button_mode;
	bool drag;              /* `SetCheckBoxDrag`：按下拖动也能切换（AIN 侧语义） */
	int button_width, button_height;
	/* [D517] AIN 调过 SetCheckBoxSize/Color 族 = 该部件按 checkbox 组件
	 * 程序化外观绘制（EX 无 フラット名，flat 空库）。渲染层的钩子判据。 */
	bool is_checkbox;

	/* --- 部件音效名（`Parts_SetSoundName`/`Parts_GetSoundName`，[D494]）---
	 * 下标 = AIN 的槽号参数。反汇编实证只见过 2/3（AIN 包装 7651/7653 的
 * 立即数，配对 getter 7652/7654 同槽），容量按 4 留裕量；
 * 超界槽忽略并告警。名字不需要持久化（跟复选框状态同理）。
 * 播放接线（[D494] 补全）：`PE_Parts_PlaySound` 按槽名经
 * `audio_play_by_name` 走匿名通道；AIN 侧 live 链见 parts.c 该函数注释。 */
	struct string *sound_name[PARTS_SOUND_NAME_SLOTS];

	/* --- flat 音效变更表（FlatSound 家族，[D495]）---
	 * FlatSound 族 HLL（NumofFlatSound 755 / GetFlatSoundName 756 /
	 * Parts_ChangeFlatSound 757 / GetProxyFlatSound 758 /
	 * ReturnChangeFlatSound 759 / ClearChangeFlatSound 760）维护的
	 * 「每部件每状态已变更音效」表：`Parts_ChangeFlatSound(no, old, new,
	 * state)` 把 old→new 记一条；`GetProxyFlatSound` 查 old 当前的替身；
	 * `ReturnChangeFlatSound` 撤一条；`ClearChangeFlatSound` 全撤。
	 * flat 档案本身没有 sound 段（libsys4 flat.c 解析器无此字段），所以
	 * 变更表初始为空、`NumofFlatSound` 只数变更条目——AIN 包装
	 * Ｐ＿フラットＣＧ変更リスト取得（fno=7510）正是拿 755/756 遍历
	 * 变更列表。注：本作 AIN 里 CFlatParts@SoundNames 一族 0 静态调用者
	 * （ain_xref call 9586..9590 实测），这是死链保真实现。 */
	struct flat_sound_change {
		struct string *old_name;   /* 变更前音效名（AIN 的 OldName） */
		struct string *new_name;   /* 变更后音效名 */
	} *flat_sound_changes[PARTS_SOUND_NAME_SLOTS];
	int nr_flat_sound_changes[PARTS_SOUND_NAME_SLOTS];

	/* --- 按键等待指示（キー待ち表示，[D496]）---
	 * 消息窗一族：AIN `_CMsgKeyWait@_PreCommand` → show(0)（打字中）、
	 * `_PostCommand` → show(1)（等按键，实测 693 次/章）、6189 → show(0)+清
	 * 委托（消息窗销毁）。CG 名是**连番动画**（start_no/numof/time_per_cg，
	 * 来自 SetKeyWaitCGName 的实参布局 = GetKeyWaitCGName 的四个出参）；
	 * 也可以是 flat。运行期 SetKeyWaitCGName/FlatName/Pos **0 次命中**
	 * （hllhot 实测）——图形/位置走引擎默认，素材与闪烁节奏不可知，
	 * 渲染挂钩待证据；先把状态存真、getter 读回。 */
	struct string *keywait_cg;
	int keywait_cg_start_no, keywait_cg_numof, keywait_cg_time;
	struct string *keywait_flat;
	int keywait_x, keywait_y, keywait_z;
	bool keywait_show;

	/* --- 单选按钮组：成员是子部件号 --- */
	int *radio_children;
	int nr_radio_children, cap_radio_children;

	/* --- 滚动条（组件类型 2 纵向 / 3 横向）---
	 * 两者接口完全对称（Up/Down 对 Left/Right），所以共用一份字段。
	 * `scroll_pos` / `scroll_rate` 是 **float**（0..1 的比率），
	 * `total` / `view` / `move_by_button` 是整数尺寸。 */
	struct {
		int head_size, tail_size;      /* Up/Down 或 Left/Right 的按钮尺寸 */
		int total_size, view_size;
		int scroll_pos;        /* 像素位置（`GetVScrollbarScrollPos` 默认返回 PUSH 0） */
		float scroll_rate;     /* 0..1 比率（默认返回 F_PUSH 0） */
		int move_size_by_button;
		/* [D500] 用户正在值条上按下/拖动：拖动期间 BeginUpdateEvent 的
		 * 「config 值→部件 rate」同步必须跳过，否则每帧开头的覆盖会把
		 * 点击换算出的 rate 冲回旧值（死锁在旧值，点击永远不生效）。 */
		bool user_active;
		struct string *cg_name, *flat_name;
		struct string *ov_head_cg, *ov_tail_cg, *ov_bar_cg, *ov_bg_cg;
		struct string *ov_head_flat, *ov_tail_flat, *ov_bar_flat, *ov_bg_flat;
	} scroll;

	/* --- 消息窗口 ---
	 * 文本走通用的 `text`；`CGName` / `FlatName` 也复用通用字段。
	 * 下面这四个是 `SetMessageWindowText` 的「台词位置指纹」：AIN 在设文本时
	 * 一并传进来，存档后靠它定位「读到哪一句」。AIN 侧没有对应的 getter，
	 * 所以只存不读，但必须存 —— 否则会退化成「存档后从头开始」。 */
	int msg_num;
	struct string *msg_func_name;
	int msg_ver, msg_step;
	bool msg_active;
	int msg_flat_wait_frame;
	int msg_area_x, msg_area_y, msg_area_w, msg_area_h;
	int msg_text_speed;
	int msg_space_x, msg_space_y;
	int msg_inactive_r, msg_inactive_g, msg_inactive_b;
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
	/* [D311] 创建序号：部件号会被复用，「同一个号在不同时刻是不同部件」
	 * 已经坑过好几轮（D277 的 alpha 序列、本轮的 show 判定）。
	 * 序号只在创建时递增，永远不复用，用来在日志里区分实例。 */
	unsigned serial;
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
	/* [D514] flat/CG 件的 EX `文本` 标签。
	 *
	 * 单选组的选项就是这种件：`SYS_画质设定一般` 的 EX `文本` = "画质优先"，
	 * 而它本身是 flat（60x60 检查图标）。`parts_state` 是 union，flat 与
	 * text 不能共存（`PE_SetText` 会把 state 重置成 PARTS_TEXT，圆圈就没了），
	 * 所以标签单独缓存，渲染时叠在 flat 之上。
	 * 此前只对 `ACT_COMP_TEXT(19)` 读 `文本`，这批标签全被丢弃 ——
	 * 表现正是「设置项只有圆圈、没有文字」。 */
	struct parts_text caption_text;
	bool caption_valid;
	/* [D515] 独立保存的布局盒数据。
	 *
	 * 装载时 `PE_SetComponentType` 把 state 0 设成 PARTS_LAYOUT_BOX，
	 * 但 AIN 之后会调 SetPartsCG / SetComponentType 把同一个 state 改成
	 * CG。`parts_do_layout()` 只看 states[0]，一旦被覆盖就再也不排版 ——
	 * 3D 设置页 21 个容器（`被写界深度特效页面` 等）就是这样全叠在 (0,0)。
	 * 这里把 EX 声明的布局参数单独存一份，state 变化不影响它。 */
	struct parts_layout_box layout_box_data;
	bool has_layout_box;
	/* 用户组件的键值数据（EX 数据 = [k,v,k,v,...] 原样留存）。
	 * 游戏侧通过 PartsEngine.GetUserComponentData(no,"参数") 之类读回去，
	 * 控件真正的行为（点击、绘制）由游戏的 AIN 代码实现。 */
	char **uc_data;
	int uc_data_n;
	/* [D143] 最近一次 PE_SetText/SetMessageWindowText 写入的原文（GBK）。
	 * 渲染层用它判断"这条 EX 文字是不是某个内建 UC 控件的重复标签"，
	 * 只跳过真正重复的那一层 —— 早先用"16px 高"当判据，把任务面板的
	 * 正文（也是 16px）一起吞掉了。 */
	char *text_plain;
	/* [D396] 控件状态；只有控件类组件才有，其余为 NULL。 */
	struct parts_widget *widget;
	/* [D396] 像素级判定：命中要求该像素不透明（`SetPartsPixelDecide`）。
	 * 加在 `widget` 之后，同样不参与 `parts/save.c` 的顺序序列化。 */
	bool pixel_decide;
	/* [D496] 输入处理参与开关（AIN `SetEnableInputProcess`）。
	 * 契约（离线 xref）：AIN 侧只写不读——detail::Set(8255) 的调用点
	 * 全是类方法转发、转发自身 0 静态调用点；IsInputPartsActive(7243)
	 * 按类型 {4,7,11} 过滤也不读它 ⇒ 消费方是原版引擎 C++ 输入处理器。
	 * 引擎侧消费：input.c 对**组件类型显式 ∈ {4,7,11}**（文本框/组合框/
	 * 微调框一族）的部件生效；不碰 clickable（D165 教训：输入处理开关
	 * ≠ 可点击，全局映射会把活动文件点击許可打开的按钮全关掉）。
	 * 默认 true：AIN 初始化时统一先关（D165 实测 enable=0 占 238/242），
	 * "新建即关"会把没被 AIN 显式关过的件也一并关掉。
	 * 加在 pixel_decide 之后，同样不参与顺序序列化；无需持久化
	 * （场景重建时 AIN 会重设）。 */
	bool enable_input_process;
};

#define PARTS_LIST_FOREACH(iter) TAILQ_FOREACH(iter, &parts_list, parts_list_entry)
#define PARTS_LIST_FOREACH_REVERSE(iter) TAILQ_FOREACH_REVERSE(iter, &parts_list, parts_list, parts_list_entry)
#define PARTS_FOREACH_CHILD(iter, parent) TAILQ_FOREACH(iter, &parent->children, child_list_entry)

// parts.c
extern struct parts_list parts_list;

// 内部是按它在栈里的位置存 controller 的（0 = 栈底）。
// 但 AIN 区分「稳定的图层 ID」和「栈下标」：
//   GetLayerID(index) / GetLayerIndex(id)（PartsFunc 的 case 4 / 3）
// SceneStack 记住 AddLayer 返回的 ID，并在 IsActive 里
// 和 GetActiveLayer() 比较。如果 ID 就是下标，从中间
// 移除一层会让上面所有下标平移，比较就永远不成立
// -> SceneStack@Join 永远结束不了（D129 / DIALOG-STUCK）。
// 所以栈自己维护 id[index]（单调递增、绝不复用），
// 跨界传出去时在 ID 和下标之间转换。
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
/* [D481] 图层 + 部件自身都可见才画。
 *
 * **这里曾经读写用不同的键**：`PE_SetShow` 的图层分支写的是
 * `ctrl_stack.show[PE_get_controller_index(id)]`（**栈下标**），
 * 而这里直接用 `parts->controller_no`（**稳定 ID**）当下标去读。
 * 只要 `ctrl_stack.id[i] != i`（AddLayer 之后必然如此），两者就错位：
 * `EraseLayer(id=3)` 关掉的是 `show[1]`，部件却去看 `show[2]`，
 * 于是「层关了、部件仍 vis=1」—— 表现就是老场景的按钮还留在画面上、
 * 按 z 抢新场景的点击（`[LAYER-LEAK]` / H033 的精确形态）。
 *
 * `XSYS4_VIS_IDX=1` 打开「先把 controller_no 映射成栈下标再查」，
 * 用于验证这条不一致是不是残留层的成因。 */
extern bool parts_vis_use_index;
int PE_get_controller_index(int id);   /* [D481] 定义在 parts.c */
int PE_get_active_controller_index(void);  /* [D498] 活动层栈下标 */

/* [D483] 把部件的 `controller_no`（**稳定 ID**）映射成 `ctrl_stack` **下标**。
 *
 * `ctrl_stack.show[]` / `opaque[]` 都是按**下标**索引的：
 *   - `PE_SetShow` 的图层分支写 `show[PE_get_controller_index(id)]`；
 *   - `parts_update_cover_layer` 的 `opaque[c]`、`top = c` 也都是下标。
 * 只有读端（`parts_is_visible` / `parts_update_cover_layer`）拿
 * `parts->controller_no` 直接当下标 —— 于是 `id[i] != i` 时全错位。
 * 这里统一成一个映射函数，读端一律先过它。
 *
 * 映射失败（返回 -1）按「不参与图层过滤」处理：宁可多画一件，
 * 也不要因为映射失败把整层藏掉（那是整屏黑）。 */
static inline int parts_ctrl_index(const struct parts *parts)
{
	int c = parts->controller_no;
	if (c < 0 || c >= PARTS_CONTROLLER_STACK_MAX)
		return -1;
	if (!parts_vis_use_index)
		return c;          /* A/B 关掉时保持旧行为 */
	/* 系统 overlay 是哨兵值，不参与映射。 */
	if (c == PARTS_CONTROLLER_SYSTEM_OVERLAY)
		return c;
	return PE_get_controller_index(c);
}

static inline bool parts_is_visible(const struct parts *parts)
{
	if (!parts->global.show)
		return false;
	int c = parts->controller_no;
	if (c < 0 || c >= PARTS_CONTROLLER_STACK_MAX)
		return true;
	int idx = parts_ctrl_index(parts);
	if (idx >= 0) {
		if (!ctrl_stack.show[idx])
			return false;
		return idx >= parts_cover_layer;
	}
	/* 映射失败：不过滤（安全侧）。 */
	if (!ctrl_stack.show[c])
		return false;
	return c >= parts_cover_layer;
}

struct parts *parts_try_get(int parts_no);
struct parts *parts_get(int parts_no);
/* [D128] 场景图层诊断：controller 分布 + 当前显示部件快照。 */
void PE_DebugControllerHistogram(const char *tag);
void PE_DebugDumpParts(const char *tag);
void PE_debug_force_hide(void);
void PE_debug_track_parts(void);
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
/* [D309] 光标当前指向的部件号（0 = 没有）。供 `PartsEngine.GetActiveParts` 使用。 */
int parts_input_active_parts(void);

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
/* [D226] flat 在当前帧最多能画出多大 alpha（0..255）：key alpha × 纹理可得性。 */
int parts_flat_max_paint_alpha(struct parts_flat *f);

/* [D231] XSYS4_FLAT_TREE=<部件号>：转储该 flat 的时间线树（诊断用）。 */
void parts_flat_dump_tree(int parts_no, struct parts_flat *f);
bool parts_flat_update(struct parts_flat *f, int passed_time, int parts_no);
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

// 应用发射器 inherit_* 开关之后，每帧的发射器属性。
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
void parts_layout_veto_static_children(int parts_no);
void parts_layout_enable_runtime_children(int parts_no);

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
