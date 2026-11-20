/* 版权所有 (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/string.h"

#include "xsystem4.h"   /* [D183] display_sjis0：合成探针要打 CG 名 */
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "parts.h"
#include "asset_manager.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "hll.h"

/* [D110c] D89 手写桩的触达探针。
 * 这类桩 .fun 非 NULL，走的是正常 HLL_EXPORT，所以既不会触发 VM_ERROR，
 * 也不会出现在 D110 的软桩清单里 —— 它们静默返回 0，最容易查漏。
 * XSYS4_PE_DBG=1 时每次触达打一行。 */
#define PE_HACK_HIT(fname) do { \
	if (getenv("XSYS4_PE_DBG")) \
		NOTICE("[pe-hack] %s", fname); \
} while (0)

/* [D216] 消息窗口配置族取证探针。
 * 这批 setter 都是 noop（[HACK-POC D85]），静默吞掉参数，最容易查漏；
 * 「消息框不画、正文不出」很可能正是它们负责的区域/CG 没落到位。
 * XSYS4_MSGWIN_DBG=1 时打印名字与实参，每个函数各自限流 20 条，
 * 默认关闭、不改变行为。 */
#define MSGWIN_HIT(fmt, ...) do { \
	if (getenv("XSYS4_MSGWIN_DBG")) { \
		static int _n; \
		if (_n++ < 20) NOTICE("[msgwin] " fmt, ##__VA_ARGS__); \
	} \
} while (0)

static void PartsEngine_ModuleInit(void)
{
	PE_Init();
}

static void PartsEngine_ModuleFini(void)
{
	PE_Reset();
}

static void PartsEngine_Update(int passed_time, bool is_skip, bool message_window_show)
{
	PE_Update(passed_time, message_window_show);
}

static void PartsEngine_Update_Pascha3PC(struct string *xxx1, struct string *xxx2, int passed_time, bool is_skip, bool message_window_show)
{
	PE_Update(passed_time, message_window_show);
}

// 《Oyako Rankan》版本
static bool PartsEngine_AddDrawCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// 《Oyako Rankan》版本
static bool PartsEngine_AddCopyCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// 兰斯9 起：实参类型由 int 改为 float
static void PartsEngine_SetComponentPos(int parts_no, float x, float y)
{
	PE_SetPos(parts_no, x, y);
}

// 兰斯9 起：返回类型由 int 改为 float
static float PartsEngine_Parts_GetPartsUpperLeftPosX(int parts_no, int state)
{
	return PE_GetPartsUpperLeftPosX(parts_no, state);
}

static float PartsEngine_Parts_GetPartsUpperLeftPosY(int parts_no, int state)
{
	return PE_GetPartsUpperLeftPosY(parts_no, state);
}

// 兰斯9 起：返回类型由 int 改为 float
static float PartsEngine_Parts_GetComponentPosX(int parts_no)
{
	return PE_GetPartsX(parts_no);
}

static float PartsEngine_GetComponentPosY(int parts_no)
{
	return PE_GetPartsY(parts_no);
}

// 兰斯9 起：X/Y 坐标类型由 int 改为 float
static void PartsEngine_AddComponentMotionPos(int parts_no, float begin_x, float begin_y,
		float end_x, float end_y, int begin_t, int end_t,
		struct string *curve_name)
{
	PE_AddMotionPos_curve(parts_no, begin_x, begin_y, end_x, end_y,
			begin_t, end_t, curve_name);
}

static void PartsEngine_add_construction_process(union vm_value *ints,
		union vm_value *floats, union vm_value *strings)
{
	int parts_no    = ints[0].i;
	int state       = ints[1].i;
	int command     = ints[2].i;
	int interp_type = ints[3].i;
	int sx          = ints[4].i;
	int sy          = ints[5].i;
	int sw          = ints[6].i;
	int sh          = ints[7].i;
	int dx          = ints[8].i;
	int dy          = ints[9].i;
	int dw          = ints[12].i;
	int dh          = ints[13].i;
	int r           = ints[14].i;
	int g           = ints[15].i;
	int b           = ints[16].i;
	int a           = ints[17].i;
	// int r2       = ints[18].i;
	// int g2       = ints[19].i;
	// int b2       = ints[20].i;
	int char_space  = ints[21].i;
	int line_space  = ints[22].i;
	int font_type   = ints[23].i;
	int font_size   = ints[24].i;
	int font_r      = ints[25].i;
	int font_g      = ints[26].i;
	int font_b      = ints[27].i;
	int edge_r      = ints[28].i;
	int edge_g      = ints[29].i;
	int edge_b      = ints[30].i;
	int full_size   = ints[31].i;
	float bold_weight = floats[0].f;
	float edge_weight = floats[1].f;
	struct string *text    = heap_get_string(strings[0].i);
	struct string *cg_name = heap_get_string(strings[1].i);

	/* [D183] `XSYS4_CONSTRUCT_DBG=1`：合成操作的唯一观测点。卡片正面到底是
	 * 走这里（PartsFunc case 162）还是走空桩 776，光看导出表看不出来。 */
	if (getenv("XSYS4_CONSTRUCT_DBG"))
		NOTICE("[cproc] flat parts=%d state=%d cmd=%d d=%d,%d,%d,%d s0=%d s1=%d cg='%s' text='%.20s'",
		       parts_no, state, command, dx, dy, dw, dh,
		       strings[0].i, strings[1].i,
		       cg_name ? display_sjis0(cg_name->text) : "(null)",
		       text ? display_sjis0(text->text) : "");

	switch (command) {
	case 0:  // CASConstructionProcess::SetCreate
		PE_AddCreateToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case 1:  // CASConstructionProcess::SetCreatePixelOnly
		PE_AddCreatePixelOnlyToPartsConstructionProcess(parts_no, dw, dh, state);
		break;
	case 2:  // CASConstructionProcess::SetCreateCG
		PE_AddCreateCGToProcess(parts_no, cg_name, state);
		break;
	case 3:  // CASConstructionProcess::SetFill
		PE_AddFillToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, state);
		break;
	case 4:  // CASConstructionProcess::SetFillAlphaColor
		PE_AddFillAlphaColorToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, a, state);
		break;
	case 5:  // CASConstructionProcess::SetFillAMap
		PE_AddFillAMapToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, a, state);
		break;
	case 6:  // CASConstructionProcess::SetFillWithAlpha
		PE_AddFillWithAlphaToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, a, state);
		break;
	case 7:  // CASConstructionProcess::SetDrawText
		PE_AddDrawTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 8:  // CASConstructionProcess::SetCopyText
		PE_AddCopyTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 9:  // CASConstructionProcess::SetFillGradationHorizon
		WARNING("AddConstructProcess: FillGradationHorizon unimplemented");
		break;
	case 10:  // CASConstructionProcess::SetDrawRect
		PE_AddDrawRectToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, r, g, b, state);
		break;
	case 11:  // CASConstructionProcess::SetCutCGBlend (equal scale)
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, dw, dh,
				interp_type, state);
		break;
	case 12:  // CASConstructionProcess::SetCutCGCopy (equal scale)
		PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, dw, dh,
				interp_type, state);
		break;
	case 13:  // CASConstructionProcess::SetCutCGScaleBlend
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, state);
		break;
	case 14:  // CASConstructionProcess::SetCutCGScaleCopy
		PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, state);
		break;
	case 15:  // CASConstructionProcess::SetGrayFilter
		PE_AddGrayFilterToPartsConstructionProcess(parts_no,
				dx, dy, dw, dh, full_size, state);
		break;
	case 16:  // CASConstructionProcess::SetAddFilter
		WARNING("AddConstructProcess: AddFilter unimplemented");
		break;
	case 17:  // CASConstructionProcess::SetMulFilter
		WARNING("AddConstructProcess: MulFilter unimplemented");
		break;
	case 18:  // CASConstructionProcess::SetDrawLine
		/* [D383] 端点取 (sx,sy)→(dx,dy)（与其它命令的「源/目标」字段一致），
		 * 颜色取 r,g,b,a。第一版靠实机截图验收：回合播报屏应当出现势力战线。 */
		if (getenv("XSYS4_CONSTRUCT_DBG")) {
			static int n;
			if (n++ < 20)
				NOTICE("[cproc] draw_line parts=%d (%d,%d)-(%d,%d) rgb=%d,%d,%d a=%d",
				       parts_no, sx, sy, dx, dy, r, g, b, a);
		}
		PE_AddDrawLineToPartsConstructionProcess(parts_no, sx, sy, dx, dy,
				r, g, b, a, state);
		break;
	/* [D184] cmd=29 是 Rance10 的「按 CG 原尺寸绘制」：`d=dx,dy,0,0` 且带
	 * CG 名（`カード／ランス／基本` 等），尺寸在 CG 本身。实测 171 次，全部
	 * 落在卡片部件上——卡片立绘不出的直接原因就是它被当 unknown 丢掉。
	 * 传 dw/dh=0，由 `build_draw_cut_cg` 回退到 texture 尺寸。 */
	case 29:  // SetDrawCG (natural size at dx,dy)
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, 0, 0, 0, 0, 0, 0, interp_type, state);
		break;
	default:
		WARNING("AddConstructProcess: unknown command %d", command);
		break;
	}
}

// 兰斯9 比 func_id 3-5 里的图层函数更早。以后来 PartsEngine 版本的
// 编号为准，把兰斯9 的 id 调整过来。
static bool parts_func_uses_rance9_ids;

// PartsEngine 操作的通用分发函数。
// func_id 选择操作；实参与返回值都通过三个带类型的数组传递
// （int/bool、float、string）。
//
// 调用约定（来自游戏脚本）：
//   1. 把输入值压进对应的数组。
//   2. 为每个输出槽压一个占位值（0 或 空串）。
//   3. 调用 PartsFunc；输出会写回那些占位槽。
static int PartsEngine_PartsFunc(int func_id, struct page **array_int,
		struct page **array_float, struct page **array_string)
{
	int nr_ints = (array_int && *array_int) ? (*array_int)->nr_vars : 0;
	union vm_value *ints = nr_ints ? (*array_int)->values : NULL;
	int nr_floats = (array_float && *array_float) ? (*array_float)->nr_vars : 0;
	union vm_value *floats = nr_floats ? (*array_float)->values : NULL;
	int nr_strings = (array_string && *array_string) ? (*array_string)->nr_vars : 0;
	union vm_value *strings = nr_strings ? (*array_string)->values : NULL;

#define REQUIRE_INTS(n) \
	if (nr_ints != (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d ints, got %d", func_id, (n), nr_ints)
#define REQUIRE_FLOATS(n) \
	if (nr_floats < (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d floats, got %d", func_id, (n), nr_floats)
#define REQUIRE_STRINGS(n) \
	if (nr_strings < (n)) VM_ERROR("Invalid arguments for PartsFunc %d: expected %d strings, got %d", func_id, (n), nr_strings)

	int canonical_func_id = func_id;
	if (parts_func_uses_rance9_ids && canonical_func_id > 2)
		canonical_func_id += 3;

	switch (canonical_func_id) {
	case 0:  // void SetActiveLayer(int layer)
		REQUIRE_INTS(1);
		PE_set_active_controller(ints[0].i);
		return 1;
	case 1:  // int GetActiveLayer()
		REQUIRE_INTS(1);
		ints[0].i = PE_get_active_controller();
		return 1;
	case 2:  // int GetSystemOverlayLayer()
		REQUIRE_INTS(1);
		ints[0].i = PE_get_system_controller();
		return 1;
	// [D129] AIN 明确区分稳定图层 ID 与栈下标：中间层被弹掉后下标会左移，
	// 恒等映射会让 SceneStack.LayerPartsNumber 与 GetActiveLayer() 永远对不上。
	case 3:  // int GetLayerIndex(int layer_id)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_controller_index(ints[0].i);
		return 1;
	case 4:  // int GetLayerID(int layer_index)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_controller_id(ints[0].i);
		return 1;
	case 5:  // int NumofLayer()
		REQUIRE_INTS(1);
		ints[0].i = PE_get_nr_controllers();
		return 1;
	case 6:  // void PauseMotion(bool pause)
		REQUIRE_INTS(1);
		PE_PauseMotion(!!ints[0].i);
		return 1;
	case 7:  // void SetWantSave(int parts_no, bool want_save)
		REQUIRE_INTS(2);
		PE_parts_set_want_save(ints[0].i, !!ints[1].i);
		return 1;
	case 9:  // bool SaveThumbnail(string filename, int reduction_factor)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		ints[1].i = PE_save_thumbnail(heap_get_string(strings[0].i), ints[0].i);
		return 1;
	case 43:  // float PARTS_GetAbsoluteX(int number)
		REQUIRE_INTS(1);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_parts_get_absolute_x(ints[0].i);
		return 1;
	case 44:  // float PARTS_GetAbsoluteY(int number)
		REQUIRE_INTS(1);
		REQUIRE_FLOATS(1);
		floats[0].f = PE_parts_get_absolute_y(ints[0].i);
		return 1;
	case 45:  // int PARTS_GetAbsoluteZ(int number)
		REQUIRE_INTS(2);
		ints[1].i = PE_parts_get_absolute_z(ints[0].i);
		return 1;
	case 48:  // void PARTS_SetLockInputState(int number, bool lock)
		REQUIRE_INTS(2);
		PE_parts_set_lock_input_state(ints[0].i, !!ints[1].i);
		return 1;
	case 60:  // void AppendChild(int number, int child_number)
		REQUIRE_INTS(2);
		PE_SetParentPartsNumber(ints[1].i, ints[0].i);
		return 1;
	case 94:  // void SetLayoutBoxPadding(int parts_no, int top, int bottom, int left, int right)
		REQUIRE_INTS(5);
		PE_set_layoutbox_padding(ints[0].i, ints[1].i, ints[2].i, ints[3].i, ints[4].i);
		return 1;
	case 95:  // int GetLayoutBoxPaddingTop(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_top(ints[0].i);
		return 1;
	case 96:  // int GetLayoutBoxPaddingBottom(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_bottom(ints[0].i);
		return 1;
	case 97:  // int GetLayoutBoxPaddingLeft(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_left(ints[0].i);
		return 1;
	case 98:  // int GetLayoutBoxPaddingRight(int parts_no)
		REQUIRE_INTS(2);
		ints[1].i = PE_get_layoutbox_padding_right(ints[0].i);
		return 1;
	case 106:  // void GetPartsCGSurfaceArea(int parts_no, int *x, int *y, int *w, int *h, int state)
		REQUIRE_INTS(6);
		PE_GetPartsCGSurfaceArea(ints[0].i, &ints[1].i, &ints[2].i, &ints[3].i, &ints[4].i, ints[5].i);
		return 1;
	case 112: {  // string GetText(int parts_no, int state)
		REQUIRE_INTS(2);
		REQUIRE_STRINGS(1);
		struct string *text = PE_GetText(ints[0].i, ints[1].i);
		if (text) {
			heap_string_assign(strings[0].i, text);
			free_string(text);
		}
		return 1;
	}
	case 162:  // AddConstructProcess(ArrayInt[32], ArrayFloat[2], ArrayString[2])
		REQUIRE_INTS(32);
		REQUIRE_FLOATS(2);
		REQUIRE_STRINGS(2);
		PartsEngine_add_construction_process(ints, floats, strings);
		return 1;
	case 165:  // bool InitPartsMovie(int parts_no, int width, int height, int bg_r, int bg_g, int bg_b, int state)
		REQUIRE_INTS(8);
		ints[7].i = PE_init_parts_movie(ints[0].i, ints[1].i, ints[2].i, ints[3].i, ints[4].i, ints[5].i, ints[6].i);
		return 1;
	case 166:  // int GetMovieSprite(int parts_no, int state)
		REQUIRE_INTS(3);
		ints[2].i = PE_get_movie_sprite(ints[0].i, ints[1].i);
		return 1;
	default:
		WARNING("Unknown func_id: %d", func_id);
		return 0;
	}
#undef REQUIRE_INTS
}

HLL_WARN_UNIMPLEMENTED(, void, PartsEngine, StopSoundWithoutSystemSound);

static void PartsEngine_PreLink(void);

/* Parts_SetComment(int, string) / Parts_GetComment(int) -> string：批注读写。
 * 挂在部件对象上的调试批注（.pactex 里的「批注」字段）。
 * 部件后端没有批注槽，所以这里单独维护一张小表：
 * 编辑器和调试视图确实会读回来（f[178]）。 */
#define PE_COMMENT_MAX 64
static struct { int parts_no; char *text; } pe_comments[PE_COMMENT_MAX];

static void PartsEngine_SetComment(int parts_no, struct string *comment)
{
	for (int i = 0; i < PE_COMMENT_MAX; i++) {
		if (pe_comments[i].text && pe_comments[i].parts_no == parts_no) {
			free(pe_comments[i].text);
			pe_comments[i].text = strdup(comment ? comment->text : "");
			return;
		}
	}
	for (int i = 0; i < PE_COMMENT_MAX; i++) {
		if (!pe_comments[i].text) {
			pe_comments[i].parts_no = parts_no;
			pe_comments[i].text = strdup(comment ? comment->text : "");
			return;
		}
	}
}

static struct string *PartsEngine_GetComment(int parts_no)
{
	for (int i = 0; i < PE_COMMENT_MAX; i++) {
		if (pe_comments[i].text && pe_comments[i].parts_no == parts_no)
			return cstr_to_string(pe_comments[i].text);
	}
	return cstr_to_string("");
}

/* AIN f[70]：void SetWantSaveBackScene(int parts_no, bool want)。
 * AIN_BOOL 按 ffi_type_sint32 传递，所以 C 侧接的是 int。 */
static void PE_SetWantSaveBackScene(int parts_no, int want)
{
	/* [D310] XSYS4_BACKSCENE_DBG=1：看 AIN 到底把哪些部件标成「背景场景」。
	 * 剧情里从世界地图切到战舰内部时，地图那几层（z≥4200 的 CG）应该被
	 * 收起来，只留 ADV 自己的背景件（900000413）。 */
	if (getenv("XSYS4_BACKSCENE_DBG")) {
		static int n;
		if (n++ < 300)
			NOTICE("[bs] want_save no=%d want=%d", parts_no, !!want);
	}
	PE_parts_set_want_save_back_scene(parts_no, !!want);
}

/* AIN f[776]：void AddPartsConstructionProcess(int, ref array, ref array,
 * ref array, int)。兰斯10 的调试 FPS 曲线用它来搭柱子。
 * case 162 的分发器收到的是扁平的 vm_value 联合体数组，
 * 所以「数组 -> 操作」那套映射还得移植过来（D37）。 */
static void PE_AddPartsConstructionProcess(int parts_no, struct page **ints,
        struct page **floats, struct page **strings, int state)
{
	/* [D183] 探针：776 是库表里独立于 PartsFunc 的批量入口，当前实现是
	 * 空桩。先量它是否真的被卡片合成调用（数组长度决定能否直接映射到
	 * flat 版的 32 槽布局）。 */
	if (getenv("XSYS4_CONSTRUCT_DBG")) {
		int ni = (ints && *ints) ? (*ints)->nr_vars : -1;
		int nf = (floats && *floats) ? (*floats)->nr_vars : -1;
		int ns = (strings && *strings) ? (*strings)->nr_vars : -1;
		/* 关键判据：flat 版读 ints[0]=parts_no / ints[1]=state /
		 * ints[2]=command。若这里 ints[0..1] 与形参一致，776 的数组
		 * 就是同一套 32 槽布局，可以直接转交 flat 版。 */
#define IV(k) ((ni > (k)) ? (*ints)->values[k].i : -1)
		const char *s0 = "", *s1 = "";
		if (ns > 0) {
			struct string *t = heap_get_string((*strings)->values[0].i);
			s0 = (t && t->text) ? display_sjis0(t->text) : "";
		}
		if (ns > 1) {
			struct string *t = heap_get_string((*strings)->values[1].i);
			s1 = (t && t->text) ? display_sjis0(t->text) : "";
		}
		NOTICE("[cproc] f776 parts=%d state=%d nr=%d/%d/%d i0..3=%d,%d,%d,%d s0='%s' s1='%s'",
		       parts_no, state, ni, nf, ns, IV(0), IV(1), IV(2), IV(3), s0, s1);
#undef IV
	}
	/* [D183] 卡片正面合成就走这里：实测 468 次调用，而 `PartsFunc` 的
	 * case 162（flat 版）**0 次**。数组是 32/2/2，但**不含** parts_no/state
	 * —— `ArrayInt[0]` 是 command（实测 0/6/18），flat 版却读
	 * `[0]=parts_no, [1]=state, [2]=command`。所以把形参前插两槽再转交，
	 * 其余槽位整体偏移 2。
	 * `XSYS4_CONSTRUCT_776=0` 回退空桩做 A/B。 */
	static int cp776_enable = -1;
	if (cp776_enable < 0) {
		const char *e = getenv("XSYS4_CONSTRUCT_776");
		cp776_enable = !(e && *e == '0');
	}
	if (!cp776_enable || !ints || !*ints || !floats || !*floats ||
	    !strings || !*strings)
		return;
	struct page *ip = *ints, *fp = *floats, *sp = *strings;
	if (ip->nr_vars < 1 || fp->nr_vars < 2 || sp->nr_vars < 2)
		return;
	if (ip->values[0].i < 0 || ip->values[0].i > 31) {
		WARNING("[cproc] f776 unexpected command %d (parts %d)",
		        ip->values[0].i, parts_no);
		return;
	}
	union vm_value tmp[34];
	memset(tmp, 0, sizeof(tmp));
	tmp[0].i = parts_no;
	tmp[1].i = state;
	int n = ip->nr_vars < 32 ? ip->nr_vars : 32;
	for (int k = 0; k < n && k + 2 < 34; k++)
		tmp[k + 2] = ip->values[k];
	PartsEngine_add_construction_process(tmp, fp->values, sp->values);
}

/* AIN f[12]：void UpdateComponent(int, int, bool, float, float)。
 * 原来的绑定指到了 3 实参的 PartsEngine_Update（D62）。 */
/* [D203] 库表 f[12] UpdateComponent 的签名是
 * (PassedTime, ScaledPassedTime, MessageWindowShow, MulColorRate, AlphaRate)，
 * 这是 AIN parts::detail::Update(8292) 每帧调用的**主更新入口**，不是
 * 「更新某个部件」。原实现（连带注册在库表里不存在的 `Update` 名上的
 * PartsEngine_Update）让它一直是空桩：PE_UpdateComponent / 动画时钟 /
 * PE_UpdateInputState / parts_render_update 全部不跑 → 进入 ADV 后
 * 背景 CG 停在 alpha=0 / show=0，整屏黑只剩 MENU。
 * 接上后同一序列截图从 10.9KB（纯黑）变成 680KB（世界地图背景出图）。
 * MulColorRate / AlphaRate 目前未使用（消息窗口整体调色/透明），见台账。 */
static void PartsEngine_UpdateComponent(int passed_time, int scaled_time,
		int message_window_show, float mul_color_rate, float alpha_rate)
{
	/* [D228 诊断] XSYS4_UPD_DBG=1：每 120 次打一行真实时间实参。
	 * 起因：`PartsMotionTask`/`CAS` 任务只在开局跑 4 次就再也不推进（H040），
	 * 而 AIN 仍在帧循环里。若这里 `st=0`，说明"缩放时间"恒 0 —— 所有靠
	 * scaled_time 推进的 motion 任务都会冻在 from 值上。 */
	if (getenv("XSYS4_UPD_DBG")) {
		static unsigned n;
		if (n++ % 120 == 0)
			NOTICE("[upd] t=%d st=%d mw=%d mul=%.2f alpha=%.2f n=%u",
			       passed_time, scaled_time, message_window_show,
			       mul_color_rate, alpha_rate, n);
	}
	(void)scaled_time; (void)mul_color_rate; (void)alpha_rate;
	PE_Update(passed_time, message_window_show != 0);
}
/* AIN PartsEngine.Parts_GetParentPartsNumber。
 *
 * [D312] 原来是恒 0 的桩（D80/D81 的"HACK-POC 仅探针"）。但父链正是 AIN 算
 * **层号**的输入：`AFL_Parts_GetLayerIDByParts`(7260) 沿父链上溯、遇 0 结束。
 * 恒 0 ⇒ 所有部件都被判成根 ⇒ 层号与真实归属不符 ⇒ AIN 隐藏的分支落空
 * （`[BG-LAYER]` 的形状）。运行期实测被调 18 次，属"冷但关键"。
 * `XSYS4_PARENT_REAL=0` 回退到旧桩。 */
static int PartsEngine_Parts_GetParentPartsNumber(uintptr_t parts_no)
{
	static int off = -1;
	if (off < 0) {
		const char *e = getenv("XSYS4_PARENT_REAL");
		off = (e && !strcmp(e, "0"));
	}
	if (off)
		return 0;
	return PE_parts_get_parent_parts_number((int)parts_no);
}

/* AIN PartsEngine.CreateUserPartsNumber：分配一个新的用户部件号。
 * 由 Parts@AllocatePartsNumber（函数号 29534）调用，返回值直接当作
 * 部件号存起来，所以它必须唯一且可用。
 * [HACK-POC D82] 复用系统的空闲号分配器（1000001000+）；
 * 用户部件那一段的语义还没验证过。 */
static int PartsEngine_CreateUserPartsNumber(void)
{
	return PE_GetFreeNumber();
}
/* AIN f[66]：void SetEnableInputProcess(int parts_no, bool enable)。
 *
 * [D165] 仍然是空桩 —— 这是**试过实现之后退回**的结论，不是偷懒。
 *
 * 一开始把它映射成 `PE_SetClickable(no, enable)`，理由是 input.c 的
 * `click_eligible = parts->clickable || !parts->pass_cursor`。结果整局
 * 只加载 27 个活动、停在任务地图：`XSYS4_DBG_EIP=1` 计数 242 次调用里
 * **238 次是 enable=0**（活动初始化时统一"先关一遍"），于是活动文件里
 * 靠 `点击許可` 打开的按钮全被关掉，连编队界面都进不去。
 *
 * 结论：`输入处理开关` ≠ `可点击`。前者是"AIN 自己要不要派发"，后者是
 * "这个部件在活动里声明为按钮"。在没有弄清 AIN 侧怎么用这个开关之前，
 * 保持空桩比猜一个映射安全。
 *
 * 已登记 TECH_DEBT `[EIP-NOT-CLICKABLE]`。 */
static void PartsEngine_SetEnableInputProcess(int parts_no, int enable)
{
	if (getenv("XSYS4_DBG_EIP"))
		NOTICE("[eip] no=%d enable=%d", parts_no, enable);
	/* 故意不写 parts->clickable，见上面的说明。 */
	(void)parts_no;
	(void)enable;
}

/* AIN f：void Parts_SetPartsTextCountReturn。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_Parts_SetPartsTextCountReturn(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void Parts_SetPartsTextHighlight。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_Parts_SetPartsTextHighlight(int a0, int a1, int a2, int a3, int a4, int a5, int a6)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;
	(void)a6;
}

/* AIN f：void SetButtonColor。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetButtonColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN f：void SetButtonEnable。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetButtonEnable(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetButtonSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetButtonSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SuspendTexture(int PartsNumber) / void ResumeTexture(int)。
 *
 * [D144] 这两个原来是"表里根本没有"，到 `SceneQuestMap@OpenParty`（打开部队
 * 编成界面的那一步）就 `Unimplemented HLL function` 直接中止 —— 编成界面
 * 因此从来进不去。语义是"切场景时先别刷新这批部件的贴图，回来再恢复"，
 * 纯性能优化：不挂起只是多刷几帧，不影响任何可见状态。所以先按 no-op 接上，
 * 让主线能过这一关；真要做可以给 parts 加 texture_suspended 位并在
 * texture 提交处跳过。 */
static void PE_Noop_SuspendTexture(int a0)
{
	(void)a0;
}

static void PE_Noop_ResumeTexture(int a0)
{
	(void)a0;
}

/* AIN f：void SetCheckBoxColor。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetCheckBoxColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN f：void SetCheckBoxSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetCheckBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SetComboBoxSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetComboBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SetComboBoxTextMargin。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetComboBoxTextMargin(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SetComponentMipmap。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetComponentMipmap(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetComponentTextureFilterType。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetComponentTextureFilterType(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetFocus。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetFocus(int a0)
{
	(void)a0;
}

/* AIN f：void SetHScrollbarLeftWidth。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetHScrollbarLeftWidth(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetHScrollbarMoveSizeByButton。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetHScrollbarMoveSizeByButton(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetHScrollbarRightWidth。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetHScrollbarRightWidth(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetHScrollbarScrollPos。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetHScrollbarScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetHScrollbarSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetHScrollbarSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SetHScrollbarTotalSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetHScrollbarTotalSize(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetHScrollbarViewSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetHScrollbarViewSize(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetListBoxLineHeight。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetListBoxLineHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetListBoxMargin。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetListBoxMargin(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SetListBoxScrollPos。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetListBoxScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetListBoxSelectIndex。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetListBoxSelectIndex(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetListBoxSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetListBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SetMultiTextBoxMaxTextLength。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetMultiTextBoxMaxTextLength(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetMultiTextBoxSelectColor。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetMultiTextBoxSelectColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN f：void SetMultiTextBoxSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetMultiTextBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SetTextBoxMaxTextLength。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetTextBoxMaxTextLength(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetTextBoxSelectColor。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetTextBoxSelectColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN f：void SetTextBoxSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetTextBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SetVScrollbarDownHeight。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetVScrollbarDownHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetVScrollbarMoveSizeByButton。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetVScrollbarMoveSizeByButton(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetVScrollbarScrollPos。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetVScrollbarScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetVScrollbarSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetVScrollbarSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SetVScrollbarTotalSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetVScrollbarTotalSize(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetVScrollbarUpHeight。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetVScrollbarUpHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetVScrollbarViewSize。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetVScrollbarViewSize(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetButtonCGName。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetButtonCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetButtonText。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetButtonText(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetCheckBoxCGName。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetCheckBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetCheckBoxText。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetCheckBoxText(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetComboBoxCGName。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetComboBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetComboBoxText。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetComboBoxText(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetHScrollbarCGName。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetHScrollbarCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetListBoxCGName。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetListBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetListBoxItem。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetListBoxItem(int a0, int a1, struct string * a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f：void SetMultiTextBoxCGName。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetMultiTextBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetMultiTextBoxText。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetMultiTextBoxText(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetTextBoxCGName。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetTextBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetTextBoxText。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetTextBoxText(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f：void SetVScrollbarCGName。[HACK-POC D85] 空实现（setter 占位），不保存状态。 */
static void PE_Noop_SetVScrollbarCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN Parts_SetAnchor：void Parts_SetAnchor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetAnchor(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN Parts_SetFlatBeginSeekFrame：void Parts_SetFlatBeginSeekFrame。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetFlatBeginSeekFrame(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetFlatEnableLayer：void Parts_SetFlatEnableLayer。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetFlatEnableLayer(int a0, struct string * a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN Parts_SetHGaugeReverse：void Parts_SetHGaugeReverse。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetHGaugeReverse(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetLockUpdate：void Parts_SetLockUpdate。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetLockUpdate(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN Parts_SetPosForRate：void Parts_SetPosForRate。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetPosForRate(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN Parts_SetRubyCharSpace：void Parts_SetRubyCharSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetRubyCharSpace(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetSoundName：void Parts_SetSoundName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetSoundName(int a0, struct string * a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetSwipeType：void Parts_SetSwipeType。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetSwipeType(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN Parts_SetTextEnableTag：void Parts_SetTextEnableTag。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetTextEnableTag(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetTextPos：void Parts_SetTextPos。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetTextPos(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetTextShowTime：void Parts_SetTextShowTime。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetTextShowTime(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetUpdateLookAtPos：void Parts_SetUpdateLookAtPos。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetUpdateLookAtPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN Parts_SetVGaugeReverse：void Parts_SetVGaugeReverse。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetVGaugeReverse(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetWheelable：void Parts_SetWheelable。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_Parts_SetWheelable(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetActiveController：void SetActiveController。
 * [D312] 原来空实现（D85 占位）。实测运行期 **5970 次**，且它决定新部件落在
 * 哪一层（`parts->controller_no = ctrl_stack.active`），空实现会让 AIN 的
 * "往低层放东西/隐藏低层"全部落空。见 parts.c 的 PE_SetActiveController。 */

/* AIN SetButtonCharSpace：void SetButtonCharSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetButtonCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetButtonFlatName：void SetButtonFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetButtonFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetButtonLineSpace：void SetButtonLineSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetButtonLineSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetButtonTextOriginPosMode：void SetButtonTextOriginPosMode。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetButtonTextOriginPosMode(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxButtonMode：void SetCheckBoxButtonMode。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetCheckBoxButtonMode(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxCharSpace：void SetCheckBoxCharSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetCheckBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxEnable：void SetCheckBoxEnable。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetCheckBoxEnable(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxFlatName：void SetCheckBoxFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetCheckBoxFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxLineSpace：void SetCheckBoxLineSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetCheckBoxLineSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxOverrideCGName：void SetCheckBoxOverrideCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetCheckBoxOverrideCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxOverrideCheckedCGName：void SetCheckBoxOverrideCheckedCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetCheckBoxOverrideCheckedCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxOverrideCheckedFlatName：void SetCheckBoxOverrideCheckedFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetCheckBoxOverrideCheckedFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxOverrideFlatName：void SetCheckBoxOverrideFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetCheckBoxOverrideFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxTextOriginPosMode：void SetCheckBoxTextOriginPosMode。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetCheckBoxTextOriginPosMode(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetClickMissSoundName：void SetClickMissSoundName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetClickMissSoundName(struct string * a0)
{
	(void)a0;
}

/* AIN SetComboBoxCharSpace：void SetComboBoxCharSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComboBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComboBoxListBoxCharSpace：void SetComboBoxListBoxCharSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComboBoxListBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComboBoxListBoxHeight：void SetComboBoxListBoxHeight。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComboBoxListBoxHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComboBoxListBoxItem：void SetComboBoxListBoxItem。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComboBoxListBoxItem(int a0, int a1, struct string * a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetComboBoxListBoxLineHeight：void SetComboBoxListBoxLineHeight。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComboBoxListBoxLineHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComboBoxListBoxMargin：void SetComboBoxListBoxMargin。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComboBoxListBoxMargin(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetComboBoxListBoxScrollPos：void SetComboBoxListBoxScrollPos。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComboBoxListBoxScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComboBoxListBoxSelectIndex：void SetComboBoxListBoxSelectIndex。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComboBoxListBoxSelectIndex(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentCheckBoxShowLinkNumber：void SetComponentCheckBoxShowLinkNumber。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentCheckBoxShowLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentClipArea：void SetComponentClipArea。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentClipArea(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetComponentEnableClipArea：void SetComponentEnableClipArea。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentEnableClipArea(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentMessageWindowEffectLink：void SetComponentMessageWindowEffectLink。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentMessageWindowEffectLink(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentReverseLR：void SetComponentReverseLR。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentReverseLR(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentReverseTB：void SetComponentReverseTB。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentReverseTB(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentScrollAlphaLinkNumber：void SetComponentScrollAlphaLinkNumber。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentScrollAlphaLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentScrollPosXLinkNumber：void SetComponentScrollPosXLinkNumber。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentScrollPosXLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentScrollPosYLinkNumber：void SetComponentScrollPosYLinkNumber。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentScrollPosYLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentShowEditor：void SetComponentShowEditor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentShowEditor(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentTextureAddressType：void SetComponentTextureAddressType。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetComponentTextureAddressType(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetEnableSpeedUp：void SetEnableSpeedUp。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetEnableSpeedUp(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFontColorForBackScene：void SetFontColorForBackScene。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFontColorForBackScene(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetFormCaptionColor：void SetFormCaptionColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormCaptionColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetFormCaptionPlusHeight：void SetFormCaptionPlusHeight。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormCaptionPlusHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormClientColor：void SetFormClientColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormClientColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetFormEnableCloseButton：void SetFormEnableCloseButton。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormEnableCloseButton(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormFold：void SetFormFold。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormFold(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormGroupViewSpan：void SetFormGroupViewSpan。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormGroupViewSpan(int a0, int a1, int a2, int a3, int a4, int a5)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;
}

/* AIN SetFormHScrollPos：void SetFormHScrollPos。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormHScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormIcon：void SetFormIcon。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormIcon(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormMaxSize：void SetFormMaxSize。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormMaxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetFormMinSize：void SetFormMinSize。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormMinSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetFormResizable：void SetFormResizable。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormResizable(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormSize：void SetFormSize。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetFormTitle：void SetFormTitle。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormTitle(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormVScrollPos：void SetFormVScrollPos。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetFormVScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollTotalSizeLinkNumber：void SetHScrollTotalSizeLinkNumber。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollTotalSizeLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollViewSizeLinkNumber：void SetHScrollViewSizeLinkNumber。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollViewSizeLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarFlatName：void SetHScrollbarFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollbarFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideBGCGName：void SetHScrollbarOverrideBGCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollbarOverrideBGCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideBGFlatName：void SetHScrollbarOverrideBGFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollbarOverrideBGFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideBarCGName：void SetHScrollbarOverrideBarCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollbarOverrideBarCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideBarFlatName：void SetHScrollbarOverrideBarFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollbarOverrideBarFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideLeftCGName：void SetHScrollbarOverrideLeftCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollbarOverrideLeftCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideLeftFlatName：void SetHScrollbarOverrideLeftFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollbarOverrideLeftFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideRightCGName：void SetHScrollbarOverrideRightCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollbarOverrideRightCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideRightFlatName：void SetHScrollbarOverrideRightFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetHScrollbarOverrideRightFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetKeyWaitCGName：void SetKeyWaitCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetKeyWaitCGName(int a0, struct string * a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetKeyWaitFlatName：void SetKeyWaitFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetKeyWaitFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetKeyWaitPos：void SetKeyWaitPos。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetKeyWaitPos(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetKeyWaitShow：void SetKeyWaitShow。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetKeyWaitShow(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetLayoutBoxPadding：void SetLayoutBoxPadding。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetLayoutBoxPadding(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetLayoutBoxReturnSizeForRate：void SetLayoutBoxReturnSizeForRate。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetLayoutBoxReturnSizeForRate(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetListBoxCharSpace：void SetListBoxCharSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetListBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetLockInputState：void SetLockInputState。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetLockInputState(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowActive：void SetMessageWindowActive。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMessageWindowActive(int a0, int a1)
{
	MSGWIN_HIT("SetMessageWindowActive no=%d active=%d", a0, a1);
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowCGName：void SetMessageWindowCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMessageWindowCGName(int a0, struct string * a1)
{
	MSGWIN_HIT("SetMessageWindowCGName no=%d len=%d", a0, a1 ? (int)strlen(a1->text) : -1);
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowFlatName：void SetMessageWindowFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMessageWindowFlatName(int a0, struct string * a1)
{
	MSGWIN_HIT("SetMessageWindowFlatName no=%d len=%d", a0, a1 ? (int)strlen(a1->text) : -1);
	(void)a0;
	(void)a1;
}

/* [D202] 消息等待点击时 AIN 会轮询「当前活动部件号」，再交给
 * `_CMsgKeyWait@IsClickableParts` 判断能否点。引擎没有编辑器侧的
 * active-parts 概念，且 IsClickableParts(0) 直接返回 false，所以返回 0
 * 是安全默认：点击推进仍由已实现的 GetClickNumber / WaitForClick 负责。 */
static int PE_GetActiveParts(void)
{
	/* [D309] 补成真实语义（原来是恒 0 的桩）。
	 *
	 * AIN 侧：`parts::detail::GetActivePartsType`(8308) = 本函数返回 0 则
	 * **-1**，否则 `GetComponentType(parts, 1)`；`parts::IsInputPartsActive`(7243)
	 * 与 `parts::detail::WaitForClick`(8309) 全靠它判断「这次点击算不算数」。
	 * 恒返回 0 ⇒ 类型恒 -1 ⇒ `TextButton@Motion`/`IsClickable` 一次都不被调用
	 * ⇒ 开场回想对话的「過後查看」点不动（`s141`–`s174` 全链路取证，见
	 * `[TEXTBUTTON-CLICK]`）。
	 *
	 * `XSYS4_ACTIVE_PARTS=0` 回退到旧桩，供 A/B。 */
	return parts_input_active_parts();
}

/* [D202] 背景快照辅助：引擎始终按 SaveBackScene 的完整快照工作，
 * 不需要按部件标记是否要保存，Clear 也无需单独动作（Load 会重建整树）。 */
static void PE_ClearBackScene(void) { }
static bool PE_IsWantSaveBackScene(int no)
{
	/* [D310] 原来是恒 false 的桩。`SetWantSaveBackScene(no, want)` 明明
	 * 一直在被调用（实测 30 次），说明 AIN 指望用这个标记决定「哪些部件
	 * 属于背景场景、可以被 SaveBackScene 收起」。恒 false 的结果就是：
	 * 标记写了、没人读，剧情从世界地图切到战舰内部时，地图那几层 CG
	 * 一直留在屏幕上，盖住 ADV 自己的背景件 `900000413`。
	 * **默认仍关**：实测打开后这条剧情链的画面没有变化（地图仍在），
	 * 说明它不是直接原因，不能当作已验证的修复发出去。
	 * `XSYS4_BACKSCENE_WANT=1` 才生效，留作后续 A/B。 */
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("XSYS4_BACKSCENE_WANT");
		on = (e && !strcmp(e, "1"));
	}
	if (!on)
		return false;
	return PE_parts_get_want_save_back_scene(no);
}

static struct string **msgwin_text_slot(int no);

/* [D202] 消息窗口配置 getter（SaveAdvMessageWindowData 用于保存/恢复文本样式）。
 * 对应的 setter 目前都是 noop（引擎用 PE_SetText 同步排版），所以 getter
 * 把 ref 出参写成安全默认值即可：AIN 只把它们存进快照，不参与排版判定。
 * 以前这些名字根本没注册，AIN 一调就 VM_ERROR，第一章剧情刚显示第一行
 * 文本就断在 CMessageTextView@GetTextFontColorList。
 * 签名与库表 f[522]/[526]/[530] 一致（ref int/float 在 C 侧是普通指针）。 */
static struct string *PE_GetMessageWindowText(int no)
{
	struct string **slot = msgwin_text_slot(no);
	return *slot ? string_ref(*slot) : string_dup(&EMPTY_STRING);
}

static void PE_GetMessageWindowTextFont(int no, int *type, int *size, int *r,
		int *g, int *b, float *bold_weight, int *edge_r, int *edge_g,
		int *edge_b, float *edge_weight)
{
	(void)no;
	if (type) *type = 0;
	if (size) *size = 0;
	if (r) *r = 255;
	if (g) *g = 255;
	if (b) *b = 255;
	if (bold_weight) *bold_weight = 0.0f;
	if (edge_r) *edge_r = 0;
	if (edge_g) *edge_g = 0;
	if (edge_b) *edge_b = 0;
	if (edge_weight) *edge_weight = 0.0f;
}

static void PE_GetMessageWindowTextArea(int no, int *x, int *y, int *w, int *h)
{
	(void)no;
	if (x) *x = 0;
	if (y) *y = 0;
	if (w) *w = 0;
	if (h) *h = 0;
}

static void PE_GetMessageWindowTextSpace(int no, int *letter, int *line)
{
	(void)no;
	if (letter) *letter = 0;
	if (line) *line = 0;
}

static void PE_GetKeyWaitCGName(int no, struct string **name, int *start_no,
		int *numof_cg, int *time_per_cg)
{
	(void)no;
	if (name) *name = string_dup(&EMPTY_STRING);
	if (start_no) *start_no = 0;
	if (numof_cg) *numof_cg = 0;
	if (time_per_cg) *time_per_cg = 0;
}

static int PE_GetMessageWindowTextOriginPosMode(int no) { (void)no; return 0; }
static int PE_GetMessageWindowTextSpeed(int no) { (void)no; return 0; }
static int PE_GetMessageWindowInactiveMultipleColorR(int no) { (void)no; return 255; }
static int PE_GetMessageWindowInactiveMultipleColorG(int no) { (void)no; return 255; }
static int PE_GetMessageWindowInactiveMultipleColorB(int no) { (void)no; return 255; }
static int PE_GetKeyWaitPosX(int no) { (void)no; return 0; }
static int PE_GetKeyWaitPosY(int no) { (void)no; return 0; }
static int PE_GetKeyWaitPosZ(int no) { (void)no; return 0; }
static bool PE_IsKeyWaitShow(int no) { (void)no; return false; }
static bool PE_IsComponentMessageWindowEffectLink(int no) { (void)no; return false; }

/* [D202] 消息窗口 flat 相关 getter。
 * SetMessageWindowFlatName/CGName 目前是 noop setter（引擎用 PE_SetText
 * 同步排版普通文本窗口，没有 flat 动画），所以 getter 返回空串与之配对：
 * `CMessageWindow@IsFlat` 用 String.Empty(FlatName) 取反判断，空串 => false
 * => 走普通消息窗口路径，不再等 flat 动画。以前 getter 根本没注册，
 * AIN 一调就 VM_ERROR，第一章剧情刚进 ADV 就断在这里。 */
static struct string *PE_GetMessageWindowFlatName(int no)
{
	(void)no;
	return string_dup(&EMPTY_STRING);
}

static struct string *PE_GetMessageWindowCGName(int no)
{
	(void)no;
	return string_dup(&EMPTY_STRING);
}

static struct string *PE_GetKeyWaitFlatName(int no)
{
	(void)no;
	return string_dup(&EMPTY_STRING);
}

static int PE_GetMessageWindowFlatShowWaitFrameNumber(int no)
{
	(void)no;
	return 0;
}

static bool PE_IsOverMessageWindowFlatShowWaitFrame(int no)
{
	(void)no;
	return true;   /* 没有 flat 动画，等待帧视为已过 */
}

static bool PE_BackMessageWindowFlatBeginFrame(int no)
{
	(void)no;
	return false;
}

static bool PE_StepMessageWindowFlatFinalFrame(int no)
{
	(void)no;
	return false;
}

/* AIN SetMessageWindowFlatShowWaitFrameNumber：void SetMessageWindowFlatShowWaitFrameNumber。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMessageWindowFlatShowWaitFrameNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowInactiveMultipleColor：void SetMessageWindowInactiveMultipleColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMessageWindowInactiveMultipleColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* [D142] 消息窗口正文必须落到 parts 的文本层。
 *
 * 之前这三个函数是空桩/未导出，结果是：事件场景能起来（立绘、PHASE 指示器、
 * 消息框都在），但框里**一个字都没有** —— 看起来就像"剧情被跳过"。
 * 实测（后门进第一章地图 → 点任务标记）确认场景确实进入 ADV 流程。
 *
 * AIN 签名：SetMessageWindowText(Number, Text, MsgNum, FuncName, Ver, Step)
 * MsgNum/FuncName/Ver/Step 是语音与已读管理的参数，引擎侧暂不需要。 */
/* [D202] GetMessageWindowText 必须能读回 Set 过的文本：
 * `CMessageTextView@CreateDrawChar`(6152) 用 Get 取当前串、S_PLUSA2 逐字追加、
 * 再 Set 回去实现逐字显示。返回空串会让正文永远生不出来（窗口装载了、
 * 剧情脚本 `０２／ＳＴＡＲＴ` 也执行了，画面却全黑）。
 * 部件号是大数（900000xxx），用旁路表按号存，避免给 struct parts 加字段
 * （save.c 顺序序列化，插字段会错位存档）。 */
#define MSGWIN_TEXT_SLOTS 64
static struct {
	int no;
	struct string *text;
} msgwin_text[MSGWIN_TEXT_SLOTS];

static struct string **msgwin_text_slot(int no)
{
	static int probe;
	for (int i = 0; i < MSGWIN_TEXT_SLOTS; i++)
		if (msgwin_text[i].no == no)
			return &msgwin_text[i].text;
	int i = (probe++) % MSGWIN_TEXT_SLOTS;
	if (msgwin_text[i].text)
		free_string(msgwin_text[i].text);
	msgwin_text[i].text = NULL;
	msgwin_text[i].no = no;
	return &msgwin_text[i].text;
}

/* AIN 签名：void SetMessageWindowTextFont(int Number, int Type, int Size,
 *   int R, int G, int B, float BoldWeight, int EdgeR, int EdgeG, int EdgeB,
 *   float EdgeWeight)
 *
 * [D301] 之前未注册 → `Unimplemented library function`，消息窗口正文用的是
 * 默认字体（face/size/edge 全没设），而名牌走另一条路径（EX 的字体字段），
 * 于是同一屏里名牌字大而正文又小又糊。它本质是 6 个 SetPartsFont* 的
 * 组合，直接转发即可；状态固定 1（这几个 API 的 state 是 1-based）。
 */
static void PE_SetMessageWindowTextFont(int no, int type, int size,
		int r, int g, int b, float bold_weight,
		int edge_r, int edge_g, int edge_b, float edge_weight)
{
	MSGWIN_HIT("SetMessageWindowTextFont no=%d type=%d size=%d color=(%d,%d,%d) "
		   "bold=%.2f edge=(%d,%d,%d) ew=%.2f",
		   no, type, size, r, g, b, bold_weight, edge_r, edge_g, edge_b,
		   edge_weight);
	PE_SetPartsFontType(no, type, 1);
	PE_SetPartsFontSize(no, size, 1);
	PE_SetPartsFontColor(no, r, g, b, 1);
	PE_SetPartsFontBoldWeight(no, bold_weight, 1);
	PE_SetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_SetPartsFontEdgeWeight(no, edge_weight, 1);
}

static void PE_SetMessageWindowText(int no, struct string *text, int msg_num,
		struct string *func_name, int ver, int step)
{
	(void)msg_num;
	(void)func_name;
	(void)ver;
	(void)step;
	MSGWIN_HIT("SetMessageWindowText no=%d len=%d", no, text ? (int)strlen(text->text) : -1);
	/* [D319] 台词进度探针：`CMessageTextView@CreateDrawChar` 是**逐字**调
	 * `SetMessageWindowText` 的（取串 → 追加一个字 → 设回），所以不能逐次打，
	 * 否则一行 20 字就是 20 条。这里只在「长度从有到接近 0」时认作新行起点，
	 * 打一个累计行号 —— 它同时是「剧情推进到第几句」的唯一可量化指标，
	 * 也是判断「是不是卡住不动了」的判据。 */
	if (getenv("XSYS4_MSG_LINES")) {
		static int last_len, lines;
		int len = text ? (int)strlen(text->text) : 0;
		if (len <= 2 && last_len > 2) {
			lines++;
			NOTICE("[msgline] %d no=%d", lines, no);
		}
		last_len = len;
	}
	/* [D303] 打印原始字节：说话态首行渲染出碎片，需要确认是「字符本身」
	 * 还是「解码/字形缺失」的问题。 */
	if (text && getenv("XSYS4_MSGWIN_DBG")) {
		static int hn;
		if (hn++ < 6) {
			char hex[3 * 17];
			int n = (int)strlen(text->text);
			if (n > 16) n = 16;
			for (int i = 0; i < n; i++)
				snprintf(hex + i * 3, 4, "%02x ", (unsigned char)text->text[i]);
			hex[n ? n * 3 : 0] = '\0';
			NOTICE("[msghex] no=%d n=%d bytes=%s", no, (int)strlen(text->text), hex);
		}
	}
	if (!text)
		return;
	struct string **slot = msgwin_text_slot(no);
	if (*slot)
		free_string(*slot);
	*slot = string_dup(text);
	PE_SetText(no, text, 1);
}

/* PE_SetText 是同步排版的，Fix 没有额外工作；但 AIN 会先 Set 再 Fix，
 * 有些脚本还会轮询 IsFixedMessageWindowText 决定是否继续。 */
static void PE_FixMessageWindowText(int no)
{
	(void)no;
}

static bool PE_IsFixedMessageWindowText(int no)
{
	(void)no;
	return true;
}

/* AIN SetMessageWindowTextArea：void SetMessageWindowTextArea。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMessageWindowTextArea(int a0, int a1, int a2, int a3, int a4)
{
	MSGWIN_HIT("SetMessageWindowTextArea no=%d x=%d y=%d w=%d h=%d", a0, a1, a2, a3, a4);
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetMessageWindowTextOriginPosMode：void SetMessageWindowTextOriginPosMode。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMessageWindowTextOriginPosMode(int a0, int a1)
{
	MSGWIN_HIT("SetMessageWindowTextOriginPosMode no=%d mode=%d", a0, a1);
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowTextSpace：void SetMessageWindowTextSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMessageWindowTextSpace(int a0, int a1, int a2)
{
	MSGWIN_HIT("SetMessageWindowTextSpace no=%d letter=%d line=%d", a0, a1, a2);
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetMessageWindowTextSpeed：void SetMessageWindowTextSpeed。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMessageWindowTextSpeed(int a0, int a1)
{
	MSGWIN_HIT("SetMessageWindowTextSpeed no=%d speed=%d", a0, a1);
	(void)a0;
	(void)a1;
}

/* AIN SetMotionData：void SetMotionData。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMotionData(int a0, struct string * a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetMovieTime：void SetMovieTime。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMovieTime(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetMultiTextBoxBGColor：void SetMultiTextBoxBGColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMultiTextBoxBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetMultiTextBoxCharSpace：void SetMultiTextBoxCharSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMultiTextBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMultiTextBoxFrameColor：void SetMultiTextBoxFrameColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMultiTextBoxFrameColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetMultiTextBoxHScrollShow：void SetMultiTextBoxHScrollShow。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMultiTextBoxHScrollShow(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMultiTextBoxLineSpace：void SetMultiTextBoxLineSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMultiTextBoxLineSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMultiTextBoxPadding：void SetMultiTextBoxPadding。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMultiTextBoxPadding(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetMultiTextBoxReadOnly：void SetMultiTextBoxReadOnly。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMultiTextBoxReadOnly(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMultiTextBoxReadOnlyBGColor：void SetMultiTextBoxReadOnlyBGColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMultiTextBoxReadOnlyBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetMultiTextBoxVScrollShow：void SetMultiTextBoxVScrollShow。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetMultiTextBoxVScrollShow(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetNumeralFullPitch：void SetNumeralFullPitch。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetNumeralFullPitch(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetNumeralShowPadding：void SetNumeralShowPadding。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetNumeralShowPadding(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetNumeralShowType：void SetNumeralShowType。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetNumeralShowType(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetOpenTextBoxIME：void SetOpenTextBoxIME。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetOpenTextBoxIME(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetPanelAlphaGradationBottom：void SetPanelAlphaGradationBottom。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetPanelAlphaGradationBottom(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetPanelAlphaGradationLeft：void SetPanelAlphaGradationLeft。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetPanelAlphaGradationLeft(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetPanelAlphaGradationRight：void SetPanelAlphaGradationRight。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetPanelAlphaGradationRight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetPanelAlphaGradationTop：void SetPanelAlphaGradationTop。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetPanelAlphaGradationTop(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetPanelColor：void SetPanelColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetPanelColor(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetPanelSize：void SetPanelSize。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetPanelSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetRectangleDetectionPoint：void SetRectangleDetectionPoint。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetRectangleDetectionPoint(int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;
	(void)a6;
	(void)a7;
	(void)a8;
	(void)a9;
}

/* AIN SetShowForBackScene(int parts_no, bool show)。
 *
 * [D248] 不再是纯空桩：Rance10 战斗里全屏背景 `BattleInfoView.Flat`
 * (900000705) 的 EX 是 `点击許可=0 光标选中透過=0`，而需要接收点击的
 * 技能按钮是 `点击許可=0 光标选中透過=1` —— **两者 clickable 都是 0**，
 * 唯一区别是透过位。705 按 ctrl=2 + `gz=1<<30+1125` 排第一，于是点
 * (1845,818)（「防御 1Ap」按钮）命中的是 705（`s47` 实测），消息打空炮，
 * 战斗停在第一回合。`XSYS4_CLICK_SKIP_NONCLICKABLE=1` 过滤掉 clickable=0
 * 之后 `EndInput` 直接变 0（`s48`）—— 说明**不能**用 clickable 区分，
 * 也说明这个「按场景隐藏背景层」的 setter 就是 AIN 本来要用的手段。
 *
 * 默认仍走空桩（保持既有已验收路径不变），`XSYS4_BACKSCENE_SHOW=1` 才生效，
 * 便于 A/B。 */
static void PE_SetShowForBackScene(int a0, int a1)
{
	if (!getenv("XSYS4_BACKSCENE_SHOW"))
		return;
	static int n;
	if (n++ < 200)
		NOTICE("[backscene] no=%d show=%d", a0, a1);
	PE_SetShow(a0, a1);
}

/* AIN SetSpinBoxBGColor：void SetSpinBoxBGColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetSpinBoxBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetSpinBoxCGName：void SetSpinBoxCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetSpinBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetSpinBoxCharSpace：void SetSpinBoxCharSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetSpinBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetSpinBoxDecimalPlace：void SetSpinBoxDecimalPlace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetSpinBoxDecimalPlace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetSpinBoxFrameColor：void SetSpinBoxFrameColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetSpinBoxFrameColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetSpinBoxReadOnly：void SetSpinBoxReadOnly。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetSpinBoxReadOnly(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetSpinBoxReadOnlyBGColor：void SetSpinBoxReadOnlyBGColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetSpinBoxReadOnlyBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetSpinBoxSelectColor：void SetSpinBoxSelectColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetSpinBoxSelectColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetSpinBoxSize：void SetSpinBoxSize。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetSpinBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetTextBoxBGColor：void SetTextBoxBGColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetTextBoxBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetTextBoxCharSpace：void SetTextBoxCharSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetTextBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetTextBoxFrameColor：void SetTextBoxFrameColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetTextBoxFrameColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetTextBoxReadOnly：void SetTextBoxReadOnly。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetTextBoxReadOnly(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetTextBoxReadOnlyBGColor：void SetTextBoxReadOnlyBGColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetTextBoxReadOnlyBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* 用户组件 (component type 15)。引擎侧只存"名字 + 键值袋"，
 * TextButton 之类的行为与绘制由游戏的 AIN 代码实现：
 *   活动创建用户组件事件的入口（CallUserComponentCreateEventWithChild）
 *   → 引擎按部件号取用户组件名（GetUserComponentName）
 *   → 交给活动的用户组件事件委托。
 * 数据表就是 EX 的 数据 = ['オブジェクト名','TextButton','参数','标题,从头开始']。 */
static struct string *PE_GetUserComponentName_str(int parts_no)
{
	const char *name = PE_GetUserComponentName(parts_no);
	if (!name)
		return NULL;
	return cstr_to_string(name);
}

static void PE_SetUserComponentName_str(int parts_no, struct string *name)
{
	PE_SetUserComponentName(parts_no, name ? name->text : NULL);
}

static void PE_SetUserComponentData_str(int parts_no, struct string *key, struct string *value)
{
	PE_SetUserComponentData(parts_no, key ? key->text : NULL, value ? value->text : NULL);
}

static struct string *PE_GetUserComponentData_str(int parts_no, struct string *key)
{
	const char *v = PE_GetUserComponentData(parts_no, key ? key->text : NULL);
	if (!v)
		return NULL;
	return cstr_to_string(v);
}

/* AIN SetVScrollTotalSizeLinkNumber：void SetVScrollTotalSizeLinkNumber。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollTotalSizeLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollViewSizeLinkNumber：void SetVScrollViewSizeLinkNumber。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollViewSizeLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarFlatName：void SetVScrollbarFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollbarFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideBGCGName：void SetVScrollbarOverrideBGCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollbarOverrideBGCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideBGFlatName：void SetVScrollbarOverrideBGFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollbarOverrideBGFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideBarCGName：void SetVScrollbarOverrideBarCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollbarOverrideBarCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideBarFlatName：void SetVScrollbarOverrideBarFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollbarOverrideBarFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideDownCGName：void SetVScrollbarOverrideDownCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollbarOverrideDownCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideDownFlatName：void SetVScrollbarOverrideDownFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollbarOverrideDownFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideUpCGName：void SetVScrollbarOverrideUpCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollbarOverrideUpCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideUpFlatName：void SetVScrollbarOverrideUpFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_Noop_SetVScrollbarOverrideUpFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* ==================================================================
 * D89：活动子系统（AIN f[22..52]）
 *
 * 原先这里每个函数都是 [HACK-POC D89] 空桩，静默返回 0，
 * 于是 ReadActivityFile() 什么也没加载，部件表始终是空的，
 * 画面上什么都没有。
 *
 * 真正干活的是 src/parts/activity.c：解析 .pactex（System4 EX 树），
 * 实例化出 `struct parts` 树，并维护一张活动表。这里的包装很薄，
 * 但它们必须用一模一样的 HLL 名字存在：
 * link_static_library() 是按名字把 AIN 函数绑到 C 符号的，
 * 不看表里的位置。
 *
 * 驱动这套设计的结构事实（见
 * Rance10_AIN底层研究-20261110.md）：
 *  - 活动树永远有一个叫 ルートパーツ（GBK）的根部件。
 *    如果 GetActivityPartsNumber(name, ルートパーツ) == 0，
 *    activity::detail::CActivity@LoadActivity 会直接中止。
 *  - AIN 从不自己搭部件树：整个脚本里 Parts_SetPartsCG 只有
 *    三个调用点。
 * ================================================================== */

static int PartsEngine_IsExistActivity(struct string *name)
{
	if (!name || !name->text)
		return 0;
	bool exists = activity_exists(name->text);
	if (getenv("XSYS4_PE_DBG"))
		NOTICE("[pe-act] IsExistActivity('%s') = %d", name->text, exists);
	return exists;
}

static int PE_Act_CreateActivity(struct string *name)
{
	if (!name || !name->text)
		return 0;
	activity_create(name->text);
	return 1;
}

static int PE_Act_ReleaseActivity(struct string *name, struct page **erase_number_list)
{
	if (!name || !name->text)
		return 0;
	/* [D270] EraseNumberList 是出参，不能丢：见 activity_release_ex() 的
	 * 注释。AIN 用它去销掉该活动各部件登记的委托下标。 */
	return activity_release_ex(name->text, erase_number_list);
}

/* AIN f[25] SaveActivityEXText：仅供编辑器，游戏流程用不到。 */
static int PE_Act_SaveActivityEXText(struct page **a0, struct string *a1)
{
	(void)a0;
	(void)a1;
	return 0;
}

/* AIN f[26] LoadActivityEXText：从 EX 文本加载活动，而不是
 * 从 .pactex 读。启动画面走的是文件路径那条，所以这里用不上；
 * 返回 0 会让调用方回退到文件加载。 */
static int PE_Act_LoadActivityEXText(struct string *name, struct string *text, int flag)
{
	(void)name;
	(void)text;
	(void)flag;
	return 0;
}

static int PE_Act_ReadActivityFile(struct string *name, struct string *file, int edit)
{
	(void)edit;
	bool ok = activity_read(name, file);
	if (getenv("XSYS4_PE_DBG"))
		NOTICE("[pe-act] ReadActivityFile(name='%s', file='%s') = %d",
		       name && name->text ? name->text : "(null)",
		       file && file->text ? file->text : "(null)", ok);
	return ok;
}

static int PE_Act_WriteActivityFile(struct string *name, struct string *file)
{
	(void)name;
	(void)file;
	return 0;
}

static int PE_Act_IsExistActivityFile(struct string *file)
{
	if (!file || !file->text)
		return 0;
	bool exists = activity_is_exist_file(file->text);
	if (getenv("XSYS4_PE_DBG"))
		NOTICE("[pe-act] IsExistActivityFile('%s') = %d", file->text, exists);
	return exists;
}

static int PE_Act_AddActivityParts(struct string *name, struct string *parts_name, int parts_no)
{
	return activity_add_parts(name, parts_name, parts_no);
}

static int PE_Act_RemoveActivityParts(struct string *name, struct string *parts_name)
{
	return activity_remove_parts(name, parts_name);
}

static int PE_Act_RemoveAllActivityParts(struct string *name)
{
	activity_remove_all_parts(name);
	return 1;
}

static int PE_Act_NumofActivityParts(struct string *name)
{
	struct activity *act = name ? activity_get(name->text) : NULL;
	int n = activity_nr_parts(act);
	/* [D119 探针] AIN Activity@GetList 靠它确定部件列表的长度。
	 * 返回 0 会导致那些按钮对象根本不被创建。 */
	if (getenv("XSYS4_ACT_DBG"))
		NOTICE("[act] Numof('%s') = %d", name && name->text ? name->text : "(null)", n);
	return n;
}

/* AIN f[34] GetActivityParts(index, name, ref string out_name, ref int out_no)。
 *
 * ffi 把 ref 形参当指针交过来，指向堆槽 / 页里的单元格，
 * 所以 C 类型写成 `struct string **` 和 `union vm_value *`。
 *
 * Activity@CreateParts 靠这个函数按下标遍历活动；
 * 一旦它返回 0，调用方就会沿用上一次的（空）名字，
 * 然后每轮迭代报一次 PARTS_INVALID_NUMBER ——
 * 这正是启动画面以前连报七次的那条警告。 */
static int PE_Act_GetActivityParts(int index, struct string *name,
				   struct string **out_name, union vm_value *out_no)
{
	struct activity *act = name ? activity_get(name->text) : NULL;
	if (!act || index < 0 || index >= activity_nr_parts(act)) {
		if (getenv("XSYS4_ACT_DBG"))
			NOTICE("[act] GetParts(%d,'%s') MISS", index,
			       name && name->text ? name->text : "(null)");
		return 0;
	}
	if (getenv("XSYS4_ACT_DBG"))
		NOTICE("[act] GetParts(%d,'%s') -> name='%s' no=%d", index,
		       name->text, activity_part_name_at(act, index),
		       activity_part_no_at(act, index));
	if (out_name) {
		if (*out_name)
			free_string(*out_name);
		*out_name = cstr_to_string(activity_part_name_at(act, index));
	}
	if (out_no)
		out_no->i = activity_part_no_at(act, index);
	return 1;
}

static int PE_Act_IsExistActivityPartsByName(struct string *name, struct string *parts_name)
{
	struct activity *act = name ? activity_get(name->text) : NULL;
	if (!act || !parts_name || !parts_name->text)
		return 0;
	return activity_find_parts(act, parts_name->text) != NULL;
}

static int PE_Act_IsExistActivityPartsByNumber(struct string *name, int parts_no)
{
	struct activity *act = name ? activity_get(name->text) : NULL;
	if (!act)
		return 0;
	for (int i = 0; i < activity_nr_parts(act); i++) {
		if (activity_part_no_at(act, i) == parts_no)
			return 1;
	}
	return 0;
}

static int PE_Act_GetActivityPartsNumber(struct string *name, struct string *parts_name)
{
	struct activity *act = name ? activity_get(name->text) : NULL;
	if (!act || !parts_name || !parts_name->text)
		return 0;
	if (!parts_name->text[0] && getenv("XSYS4_PE_DBG")) {
		NOTICE("[pe-act] GetActivityPartsNumber('%s', <empty>)", name->text);
		vm_stack_trace();
	}
	int no = activity_find_parts_no(act, parts_name->text);
	if (getenv("XSYS4_PE_DBG"))
		NOTICE("[pe-act] GetActivityPartsNumber('%s', '%s') = %d",
		       name->text, parts_name->text, no);
	/* [D360] `XSYS4_ACT_GEOM=1`：查到的部件号顺手把**画面矩形**打出来。
	 * 起因：技能卡的绘制位置与部件矩形长期对不上（D348），
	 * 而 `AFL_Activity_GetPartsNumber` 正是「按名字找部件」的唯一入口，
	 * 在这里打几何可以一次拿到「这张卡在屏幕上的真实位置」。 */
	if (no > 0 && getenv("XSYS4_ACT_GEOM"))
		PE_DebugPartsInfo(no);
	return no;
}

static struct string *PE_Act_GetActivityPartsName(struct string *name, int parts_no)
{
	struct activity *act = name ? activity_get(name->text) : NULL;
	if (!act)
		return cstr_to_string("");
	for (int i = 0; i < activity_nr_parts(act); i++) {
		if (activity_part_no_at(act, i) == parts_no)
			return cstr_to_string(activity_part_name_at(act, i));
	}
	return cstr_to_string("");
}

static int PE_Act_GetActivityEXID(struct string *name)
{
	(void)name;
	return 0;
}

/* AIN f[40..43]：EX 文本 / 背景覆盖（编辑器用）。 */
static void PE_Act_SetActivityEXText(struct string *name, struct string *text)
{
	(void)name;
	(void)text;
}

static struct string *PE_Act_GetActivityEXText(struct string *name)
{
	(void)name;
	return cstr_to_string("");
}

static void PE_Act_SetActivityBG(struct string *name, struct string *bg)
{
	(void)name;
	(void)bg;
}

static struct string *PE_Act_GetActivityBG(struct string *name)
{
	struct activity *act = name ? activity_get(name->text) : NULL;
	return cstr_to_string(activity_background(act));
}

/* AIN f[44..47]：关闭/锁定的部件列表（输入处理用）。 */
static void PE_Act_AddActivityCloseParts(struct string *name, struct string *parts_name)
{
	(void)name;
	(void)parts_name;
}

static int PE_Act_IsExistActivityCloseParts(struct string *name, struct string *parts_name)
{
	return PE_Act_IsExistActivityPartsByName(name, parts_name);
}

static void PE_Act_AddActivityLockedParts(struct string *name, struct string *parts_name)
{
	(void)name;
	(void)parts_name;
}

static int PE_Act_IsExistActivityLockedParts(struct string *name, struct string *parts_name)
{
	return PE_Act_IsExistActivityPartsByName(name, parts_name);
}

/* AIN f[48..52]：结束键。这个列表在部分活动文件的 block 1 里；
 * 首帧用不到，所以暂时一律报「没有」。 */
static void PE_Act_SetActivityEndKey(struct string *name, int key)
{
	(void)name;
	(void)key;
}

static void PE_Act_EraseActivityEndKey(struct string *name, int key)
{
	(void)name;
	(void)key;
}

static int PE_Act_IsExistActivityEndKey(struct string *name, int key)
{
	(void)name;
	(void)key;
	return 0;
}

static int PE_Act_NumofActivityEndKey(struct string *name)
{
	(void)name;
	return 0;
}

static int PE_Act_GetActivityEndKey(struct string *name, int index)
{
	(void)name;
	(void)index;
	return -1;
}

/* AIN v12 PartsEngine.GetControllerLength（库 27 的第 10 号函数）：
 * parts::detail::NumofLayer 遍历时用的计数。这个移植里「图层」就是
 * 控制器下标，所以它就是控制器个数（见 PartsEngine_PartsFunc 的 case 5）。 */
static int PE_GetControllerLength(void)
{
	return PE_get_nr_controllers();
}

/* [D129] GetControllerID(index) 返回稳定 ID（不再是恒等映射）。
 * AIN 的 parts::detail::AddLayer 拿这个返回值存进 SceneStack，
 * 之后 GetActiveLayer() 必须返回同一个值，IsActive 才成立。 */
static int PE_GetControllerID(int index)
{
	return PE_get_controller_id(index);
}

/* PartsEngine.SetFlatFPSRate(parts_no, rate, state)，AIN 库27 的 f[761]。
 * [HACK][D119-FLATFPS] 用一个全局 rate 代替按部件/按状态的存储；
 * 足够让「未实现的 HLL」报错不再挡住标题画面。
 * 待重构：把 rate 存到部件状态上，并在
 * parts_update_animation() 里使用。已登记进技术债。 */
static float parts_flat_fps_rate = 1.0f;

static void PE_SetFlatFPSRate(int parts_no, float rate, int state)
{
	(void)parts_no;
	(void)state;
	parts_flat_fps_rate = rate > 0.0f ? rate : 1.0f;
}

/* [D341] `PartsEngine.GetFlatFPS`：技能卡放完之后 AIN 要读当前 flat 的帧率
 * （`sk14` 在 `SceneBattle@ProcessAttack` 之后撞上「Unimplemented HLL」，
 * 直接打断战斗、画面回标题）。数据本来就在 `flat->hdr.fps`，
 * 再乘上 `SetFlatFPSRate` 记的倍率即可。
 *
 * 签名按 `SetFlatFPSRate(parts_no, rate, state)` 的对应关系取
 * `(parts_no, state) -> float`；参数个数若与实际不符，
 * `XSYS4_HLL_TRACE_FILTER=FlatFPS` 的 `nr_args` 会立刻暴露。 */
static float PE_GetFlatFPS(int parts_no, int state)
{
	return (float)PE_GetPartsFlatFPS(parts_no, state) * parts_flat_fps_rate;
}

/* [D341] `Parts_GetFlatFrame`：技能演算里按帧号推进（`sk15` 在
 * `SceneBattle@ProcessAttack` 之后撞上它）。与 `GetPartsFlatCurrentFrameNumber`
 * 同义，只是 AIN 用的是另一个导出名。 */
static int PE_Parts_GetFlatFrame(int parts_no, int state)
{
	return PE_GetPartsFlatCurrentFrameNumber(parts_no, state);
}

/* AIN v12 声明的是 Parts_GetPartsCGName(parts_no, state) -> string，
 * 而上游绑定是出参形式
 * (parts_no, struct string **out, state)。libffi 把 state 传到了
 * 本该是出参指针的位置，于是 HLL 通过一个小整数写内存，
 * 并把 0x1 当成「字符串」返回（第一个格式化它的 %s 就崩了）。 */
static struct string *PE_GetPartsCGName_v12(int parts_no, int state)
{
	struct string *name = NULL;
	PE_GetPartsCGName(parts_no, &name, state);
	return name;
}

HLL_LIBRARY(PartsEngine,
	    HLL_EXPORT(_PreLink, PartsEngine_PreLink),
	    // 给没有 PartsEngine.Init 的版本用
	    HLL_EXPORT(_ModuleInit, PartsEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, PartsEngine_ModuleFini),
	    // 《Oyako Rankan》版本
	    HLL_EXPORT(SetWantSaveBackScene, PE_SetWantSaveBackScene),
	    HLL_EXPORT(GetActiveController, PE_get_active_controller),
	    HLL_EXPORT(GetActiveParts, PE_GetActiveParts),
	    HLL_EXPORT(SaveBackScene, PE_SaveBackScene),
	    HLL_EXPORT(LoadBackScene, PE_LoadBackScene),
	    HLL_EXPORT(ClearBackScene, PE_ClearBackScene),
	    HLL_EXPORT(IsWantSaveBackScene, PE_IsWantSaveBackScene),
	    HLL_EXPORT(GetMessageWindowText, PE_GetMessageWindowText),
	    HLL_EXPORT(GetMessageWindowTextFont, PE_GetMessageWindowTextFont),
	    HLL_EXPORT(GetMessageWindowTextArea, PE_GetMessageWindowTextArea),
	    HLL_EXPORT(GetMessageWindowTextSpace, PE_GetMessageWindowTextSpace),
	    HLL_EXPORT(GetMessageWindowTextOriginPosMode, PE_GetMessageWindowTextOriginPosMode),
	    HLL_EXPORT(GetMessageWindowTextSpeed, PE_GetMessageWindowTextSpeed),
	    HLL_EXPORT(GetKeyWaitCGName, PE_GetKeyWaitCGName),
	    HLL_EXPORT(GetKeyWaitPosX, PE_GetKeyWaitPosX),
	    HLL_EXPORT(GetKeyWaitPosY, PE_GetKeyWaitPosY),
	    HLL_EXPORT(GetKeyWaitPosZ, PE_GetKeyWaitPosZ),
	    HLL_EXPORT(IsKeyWaitShow, PE_IsKeyWaitShow),
	    HLL_EXPORT(GetMessageWindowInactiveMultipleColorR, PE_GetMessageWindowInactiveMultipleColorR),
	    HLL_EXPORT(GetMessageWindowInactiveMultipleColorG, PE_GetMessageWindowInactiveMultipleColorG),
	    HLL_EXPORT(GetMessageWindowInactiveMultipleColorB, PE_GetMessageWindowInactiveMultipleColorB),
	    HLL_EXPORT(IsComponentMessageWindowEffectLink, PE_IsComponentMessageWindowEffectLink),
	    HLL_EXPORT(AddPartsConstructionProcess, PE_AddPartsConstructionProcess),
	    HLL_EXPORT(Init, PE_Init),
	    HLL_EXPORT(Update, PartsEngine_Update),
	    HLL_TODO_EXPORT(UpdateGUIController, PartsEngine_UpdateGUIController),
	    HLL_EXPORT(GetFreeSystemPartsNumber, PE_GetFreeNumber),
	    // FIXME：这两个到底有什么区别？
	    HLL_EXPORT(GetFreeSystemPartsNumberNotSaved, PE_GetFreeNumber),
	    HLL_EXPORT(IsExistParts, PE_IsExist),
	    HLL_EXPORT(SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    /* [D385] 读 CG 的绘制区域。签名对齐 AIN 的 `ref int` 出参：
	     * `ffi.c` 会把 `AIN_REF_INT` 实参转成 `&page->values[varno]`，
	     * 所以 C 侧直接收 `int *` 即可（和 `SetPartsCGSurfaceArea` 是同一族）。 */
	    HLL_EXPORT(GetPartsCGSurfaceArea, PE_GetPartsCGSurfaceArea),
	    HLL_EXPORT(SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(SetText, PE_SetText),
	    HLL_EXPORT(AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(DeletePartsTopTextLine, PartsEngine_DeletePartsTopTextLine),
	    HLL_EXPORT(SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_TODO_EXPORT(SetPartsTextHighlight, PartsEngine_SetPartsTextHighlight),
	    HLL_TODO_EXPORT(AddPartsTextHighlight, PartsEngine_AddPartsTextHighlight),
	    HLL_TODO_EXPORT(ClearPartsTextHighlight, PartsEngine_ClearPartsTextHighlight),
	    HLL_TODO_EXPORT(SetPartsTextCountReturn, PartsEngine_SetPartsTextCountReturn),
	    HLL_TODO_EXPORT(GetPartsTextCountReturn, PartsEngine_GetPartsTextCountReturn),
	    HLL_EXPORT(SetFont, PE_SetFont),
	    HLL_EXPORT(SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(SetHGaugeRate, PE_SetHGaugeRate_int),
	    HLL_EXPORT(SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(SetVGaugeRate, PE_SetVGaugeRate_int),
	    HLL_EXPORT(SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    /* [D164] 这一族在 AIN 里的真名带 `Parts_` 前缀
	     * （见 libs.txt：`Parts_SetNumeralNumber` / `Parts_GetNumeralNumber` …），
	     * 原来注册成不带前缀的 `SetNumeralNumber` 之类，strcmp 一个都不等，
	     * 于是"以为实现了"的函数全是从没被调用过的死条目，真正被调用的
	     * 那几个一直在 595 条未实现清单里。任务地图的 box 视图第一次真
	     * 被建出来时才暴露：一读数字就 ERROR。
	     * 旧名字保留（别处可能还有别名调用），新名字并列。 */
	    HLL_EXPORT(SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(Parts_SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_EXPORT(Parts_SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_TODO_EXPORT(SetNumeralFont, PartsEngine_SetNumeralFont),
	    HLL_EXPORT(SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(Parts_SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(GetNumeralNumber, PE_GetNumeralNumber),
	    HLL_EXPORT(Parts_GetNumeralNumber, PE_GetNumeralNumber),
	    HLL_EXPORT(SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(Parts_SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(Parts_SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(Parts_SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_EXPORT(SetPartsRectangleDetectionSurfaceArea, PE_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(SetPartsCGDetectionSurfaceArea, PartsEngine_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(SetPartsFlash, PE_SetPartsFlash),
	    HLL_EXPORT(IsPartsFlashEnd, PE_IsPartsFlashEnd),
	    HLL_EXPORT(GetPartsFlashCurrentFrameNumber, PE_GetPartsFlashCurrentFrameNumber),
	    HLL_EXPORT(BackPartsFlashBeginFrame, PE_BackPartsFlashBeginFrame),
	    HLL_EXPORT(StepPartsFlashFinalFrame, PE_StepPartsFlashFinalFrame),
	    HLL_TODO_EXPORT(SetPartsFlashSurfaceArea, PE_SetPartsFlashSurfaceArea),
	    HLL_EXPORT(SetPartsFlashAndStop, PE_SetPartsFlashAndStop),
	    HLL_EXPORT(StopPartsFlash, PE_StopPartsFlash),
	    HLL_EXPORT(StartPartsFlash, PE_StartPartsFlash),
	    HLL_EXPORT(GoFramePartsFlash, PE_GoFramePartsFlash),
	    HLL_EXPORT(GetPartsFlashEndFrame, PE_GetPartsFlashEndFrame),
	    HLL_EXPORT(ExistsFlashFile, PE_ExistsFlashFile),
	    HLL_EXPORT(ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddFillGradationHorizonToPartsConstructionProcess, PartsEngine_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(AddDrawCutCGToPartsConstructionProcess, PartsEngine_AddDrawCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddCopyCutCGToPartsConstructionProcess, PartsEngine_AddCopyCutCGToPartsConstructionProcess_old),
	    HLL_EXPORT(AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddAddFilterToPartsConstructionProcess, PartsEngine_AddAddFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(AddMulFilterToPartsConstructionProcess, PartsEngine_AddMulFilterToPartsConstructionProcess),
	    HLL_EXPORT(BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_EXPORT(ReleaseParts, PE_ReleaseParts),
	    HLL_EXPORT(ReleaseAllParts, PE_ReleaseAllParts),
	    HLL_EXPORT(ReleaseAllPartsWithoutSystem, PE_ReleaseAllPartsWithoutSystem),
	    HLL_EXPORT(SetPos, PE_SetPos),
	    HLL_EXPORT(SetZ, PE_SetZ),
	    HLL_EXPORT(SetShow, PE_SetShow),
	    HLL_EXPORT(SetAlpha, PE_SetAlpha),
	    HLL_EXPORT(SetPartsDrawFilter, PE_SetPartsDrawFilter),
	    HLL_EXPORT(SetAddColor, PE_SetAddColor),
	    HLL_EXPORT(SetMultiplyColor, PE_SetMultiplyColor),
	    HLL_EXPORT(SetPassCursor, PE_SetPassCursor),
	    HLL_EXPORT(SetClickable, PE_SetClickable),
	    HLL_EXPORT(SetSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(SetResetTimerByChangeInputStatus, PartsEngine_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsX, PE_GetPartsX),
	    HLL_EXPORT(GetPartsY, PE_GetPartsY),
	    HLL_EXPORT(GetPartsZ, PE_GetPartsZ),
	    HLL_EXPORT(GetPartsShow, PE_GetPartsShow),
	    HLL_EXPORT(GetPartsAlpha, PE_GetPartsAlpha),
	    HLL_TODO_EXPORT(GetAddColor, PartsEngine_GetAddColor),
	    HLL_TODO_EXPORT(GetMultiplyColor, PartsEngine_GetMultiplyColor),
	    HLL_EXPORT(GetPartsPassCursor, PE_GetPartsPassCursor),
	    HLL_EXPORT(GetPartsClickable, PE_GetPartsClickable),
	    HLL_TODO_EXPORT(GetPartsSpeedupRateByMessageSkip, PartsEngine_GetPartsSpeedupRateByMessageSkip),
	    HLL_TODO_EXPORT(GetResetTimerByChangeInputStatus, PartsEngine_GetResetTimerByChangeInputStatus),
	    HLL_EXPORT(GetPartsUpperLeftPosX, PE_GetPartsUpperLeftPosX),
	    HLL_EXPORT(GetPartsUpperLeftPosY, PE_GetPartsUpperLeftPosY),
	    HLL_EXPORT(GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(SetInputState, PE_SetInputState),
	    HLL_EXPORT(GetInputState, PE_GetInputState),
	    HLL_EXPORT(SetPartsOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetPartsOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_EXPORT(SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(SetPartsGroupNumber, PE_SetPartsGroupNumber),
	    HLL_EXPORT(SetPartsGroupDecideOnCursor, PE_SetPartsGroupDecideOnCursor),
	    HLL_EXPORT(SetPartsGroupDecideClick, PE_SetPartsGroupDecideClick),
	    HLL_EXPORT(SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_EXPORT(SetPartsMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(GetPartsMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(AddMotionPos, PE_AddMotionPos_curve),
	    HLL_EXPORT(AddMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_TODO_EXPORT(AddMotionCG, PartsEngine_AddMotionCG),
	    HLL_EXPORT(AddMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(AddMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(AddMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(AddMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(AddMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(AddMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(AddMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(AddMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(AddMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_EXPORT(AddWholeMotionVibrationSize, PE_AddWholeMotionVibrationSize),
	    HLL_EXPORT(AddMotionSound, PE_AddMotionSound),
	    HLL_TODO_EXPORT(SetSoundNumber, PartsEngine_SetSoundNumber),
	    HLL_TODO_EXPORT(GetSoundNumber, PartsEngine_GetSoundNumber),
	    HLL_EXPORT(SetClickMissSoundNumber, PE_SetClickMissSoundNumber),
	    HLL_TODO_EXPORT(GetClickMissSoundNumber, PartsEngine_GetClickMissSoundNumber),
	    HLL_EXPORT(BeginMotion, PE_BeginMotion),
	    HLL_EXPORT(EndMotion, PE_EndMotion),
	    HLL_EXPORT(IsMotion, PE_IsMotion),
	    HLL_EXPORT(SeekEndMotion, PE_SeekEndMotion),
	    HLL_EXPORT(UpdateMotionTime, PE_UpdateMotionTime),
	    HLL_EXPORT(BeginInput, PE_BeginInput),
	    HLL_EXPORT(EndInput, PE_EndInput),
	    HLL_EXPORT(GetClickPartsNumber, PE_GetClickPartsNumber),
	    HLL_TODO_EXPORT(GetFocusPartsNumber, PartsEngine_GetFocusPartsNumber),
	    HLL_TODO_EXPORT(SetFocusPartsNumber, PartsEngine_SetFocusPartsNumber),
	    HLL_TODO_EXPORT(PushGUIController, PartsEngine_PushGUIController),
	    HLL_TODO_EXPORT(PopGUIController, PartsEngine_PopGUIController),
	    HLL_EXPORT(SetPartsMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetPartsMagY, PE_SetPartsMagY),
	    HLL_EXPORT(SetPartsRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(SetPartsRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(SetPartsRotateZ, PE_SetPartsRotateZ),
	    HLL_EXPORT(SetPartsAlphaClipperPartsNumber, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_EXPORT(SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(IsCursorIn, PE_IsCursorIn),
	    HLL_EXPORT(SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(Save, PE_Save),
	    HLL_EXPORT(SaveWithoutHideParts, PE_SaveWithoutHideParts),
/* [D177] `PartsEngine.SaveThumbnail` 是库表里的**独立导出**（libs.txt:438），
 * 不是 PartsFunc 的 func_id。原来只注册了 PartsFunc，于是
 * `SceneQuestMap@AutoSave → gamesave::SaveThumbnail` 一进来就
 * Unimplemented。PE_save_thumbnail() 本来就有实现，只差这一行。 */
	    HLL_EXPORT(SaveThumbnail, PE_save_thumbnail),
	    HLL_EXPORT(Load, PE_Load),
	    // 兰斯9
	    HLL_EXPORT(PartsFunc, PartsEngine_PartsFunc),
	    HLL_EXPORT(Release, PE_ReleaseParts),
	    HLL_TODO_EXPORT(ReleaseAll, PartsEngine_ReleaseAll),
	    HLL_EXPORT(ReleaseAllWithoutSystem, PE_ReleaseAllWithoutSystem),
	    HLL_EXPORT(GetFreeNumber, PE_GetFreeNumber),
	    HLL_EXPORT(IsExist, PE_IsExist),
	    HLL_EXPORT(GetControllerLength, PE_GetControllerLength),
	    HLL_EXPORT(GetControllerID, PE_GetControllerID),
	    HLL_EXPORT(AddController, PE_AddController),
	    HLL_EXPORT(RemoveController, PE_RemoveController),
	    HLL_EXPORT(UpdateComponent, PartsEngine_UpdateComponent),
	    HLL_EXPORT(Parts_GetParentPartsNumber, PartsEngine_Parts_GetParentPartsNumber),
	    HLL_EXPORT(Parts_SetThumbnailReductionSize, PE_SetThumbnailReductionSize),
	    HLL_EXPORT(Parts_SetThumbnailMode, PE_SetThumbnailMode),
	    HLL_EXPORT(GetClickNumber, PE_GetClickPartsNumber),
	    HLL_EXPORT(StopSoundWithoutSystemSound, PartsEngine_StopSoundWithoutSystemSound),
	    /* Rance10.ain f[177] = Parts_SetComment。这张表是按函数号索引的，
	     * 不是按名字；见技术债 D26（表待重建）。 */
	    HLL_EXPORT(Parts_SetComment, PartsEngine_SetComment),
	    HLL_EXPORT(Parts_GetComment, PartsEngine_GetComment),
	    HLL_TODO_EXPORT(CrateActivityBinary, PartsEngine_CrateActivityBinary),
	    HLL_TODO_EXPORT(ReadActivityBinary, PartsEngine_ReadActivityBinary),
	    HLL_EXPORT(ReleaseMessage, PE_ReleaseMessage),
	    HLL_EXPORT(PopMessage, PE_PopMessage),
	    HLL_EXPORT(GetMessagePartsNumber, PE_GetMessagePartsNumber),
	    HLL_EXPORT(GetMessageDelegateIndex, PE_GetMessageDelegateIndex),
	    HLL_EXPORT(GetDelegateIndex, PE_GetDelegateIndex),
	    HLL_EXPORT(GetMessageType, PE_GetMessageType),
	    HLL_EXPORT(GetMessageVariableCount, PE_GetMessageVariableCount),
	    HLL_EXPORT(GetMessageVariableType, PE_GetMessageVariableType),
	    HLL_EXPORT(GetMessageVariableInt, PE_GetMessageVariableInt),
	    HLL_EXPORT(GetMessageVariableFloat, PE_GetMessageVariableFloat),
	    HLL_EXPORT(GetMessageVariableBool, PE_GetMessageVariableBool),
	    HLL_EXPORT(GetMessageVariableString, PE_GetMessageVariableString),
	    HLL_EXPORT(SetDelegateIndex, PE_SetDelegateIndex),
	    HLL_EXPORT(SetFocus, PE_Noop_SetFocus),
	    HLL_TODO_EXPORT(IsFocus, PartsEngine_IsFocus),
	    HLL_EXPORT(IsExistChild, PE_IsExistChild),
	    HLL_EXPORT(ClearChild, PE_ClearChild),
	    HLL_EXPORT(AddChild, PE_AddChild),
	    HLL_EXPORT(InsertChild, PE_InsertChild),
	    HLL_EXPORT(RemoveChild, PE_RemoveChild),
	    HLL_EXPORT(NumofChild, PE_NumofChild),
	    HLL_EXPORT(GetChildIndex, PE_GetChildIndex),
	    HLL_EXPORT(GetChild, PE_GetChild),
	    HLL_EXPORT(SetComponentType, PE_SetComponentType),
	    HLL_EXPORT(GetComponentType, PE_GetComponentType),
	    HLL_EXPORT(SetComponentPos, PartsEngine_SetComponentPos),
	    HLL_EXPORT(SetComponentPosZ, PE_SetZ),
	    HLL_EXPORT(GetComponentPosX, PartsEngine_Parts_GetComponentPosX),
	    HLL_EXPORT(GetComponentPosY, PartsEngine_GetComponentPosY),
	    HLL_EXPORT(GetComponentPosZ, PE_GetPartsZ),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosX, PartsEngine_Parts_GetPartsUpperLeftPosX),
	    HLL_EXPORT(Parts_GetPartsUpperLeftPosY, PartsEngine_Parts_GetPartsUpperLeftPosY),
	    HLL_EXPORT(SetComponentOriginPosMode, PE_SetPartsOriginPosMode),
	    HLL_EXPORT(GetComponentOriginPosMode, PE_GetPartsOriginPosMode),
	    HLL_TODO_EXPORT(GetComponentWidth, PartsEngine_GetComponentWidth),
	    HLL_TODO_EXPORT(GetComponentHeight, PartsEngine_GetComponentHeight),
	    HLL_EXPORT(Parts_GetPartsWidth, PE_GetPartsWidth),
	    HLL_EXPORT(Parts_GetPartsHeight, PE_GetPartsHeight),
	    HLL_EXPORT(SetComponentShow, PE_SetShow),
	    HLL_EXPORT(IsComponentShow, PE_GetPartsShow),
	    HLL_EXPORT(SetComponentMessageWindowShowLink, PE_SetPartsMessageWindowShowLink),
	    HLL_EXPORT(IsComponentMessageWindowShowLink, PE_GetPartsMessageWindowShowLink),
	    HLL_EXPORT(SetComponentAlpha, PE_SetAlpha),
	    HLL_EXPORT(GetComponentAlpha, PE_GetPartsAlpha),
	    HLL_EXPORT(SetComponentAddColor, PE_SetAddColor),
	    HLL_TODO_EXPORT(GetComponentAddColorR, PartsEngine_GetComponentAddColorR),
	    HLL_TODO_EXPORT(GetComponentAddColorG, PartsEngine_GetComponentAddColorG),
	    HLL_TODO_EXPORT(GetComponentAddColorB, PartsEngine_GetComponentAddColorB),
	    HLL_EXPORT(SetComponentMulColor, PE_SetMultiplyColor),
	    HLL_TODO_EXPORT(GetComponentMulColorR, PartsEngine_GetComponentMulColorR),
	    HLL_TODO_EXPORT(GetComponentMulColorG, PartsEngine_GetComponentMulColorG),
	    HLL_TODO_EXPORT(GetComponentMulColorB, PartsEngine_GetComponentMulColorB),
	    HLL_EXPORT(SetComponentDrawFilter, PE_SetPartsDrawFilter),
	    HLL_TODO_EXPORT(GetComponentDrawFilter, PartsEngine_GetComponentDrawFilter),
	    HLL_EXPORT(SetComponentMagX, PE_SetPartsMagX),
	    HLL_EXPORT(SetComponentMagY, PE_SetPartsMagY),
	    /* [D341] 补上 getter：`SceneBattle@RunSelectedSkill` 放完技能要读
	     * 组件的缩放值做表现（`sk13` 在这里 VM_ERROR 直接打断战斗）。
	     * 数据本来就在 `parts->local.scale` 里，`PE_GetPartsMagX/Y` 早就有了，
	     * 只是库表还挂着 TODO 桩。 */
	    HLL_EXPORT(GetComponentMagX, PE_GetPartsMagX),
	    HLL_EXPORT(GetComponentMagY, PE_GetPartsMagY),
	    HLL_EXPORT(SetComponentRotateX, PE_SetPartsRotateX),
	    HLL_EXPORT(SetComponentRotateY, PE_SetPartsRotateY),
	    HLL_EXPORT(SetComponentRotateZ, PE_SetPartsRotateZ),
	    HLL_TODO_EXPORT(GetComponentRotateX, PartsEngine_GetComponentRotateX),
	    HLL_TODO_EXPORT(GetComponentRotateY, PartsEngine_GetComponentRotateY),
	    HLL_EXPORT(GetComponentRotateZ, PE_GetPartsRotateZ),
	    HLL_EXPORT(SetComponentMargin, PE_SetComponentMargin),
	    HLL_EXPORT(GetComponentMarginTop, PE_GetComponentMarginTop),
	    HLL_EXPORT(GetComponentMarginBottom, PE_GetComponentMarginBottom),
	    HLL_EXPORT(GetComponentMarginLeft, PE_GetComponentMarginLeft),
	    HLL_EXPORT(GetComponentMarginRight, PE_GetComponentMarginRight),
	    HLL_EXPORT(SetComponentAlphaClipper, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_TODO_EXPORT(GetComponentAlphaClipper, PartsEngine_GetComponentAlphaClipper),
	    HLL_EXPORT(SetComponentTextureFilterType, PE_Noop_SetComponentTextureFilterType),
	    HLL_TODO_EXPORT(GetComponentTextureFilterType, PartsEngine_GetComponentTextureFilterType),
	    HLL_EXPORT(SetComponentMipmap, PE_Noop_SetComponentMipmap),
	    HLL_TODO_EXPORT(IsComponentMipmap, PartsEngine_IsComponentMipmap),
	    HLL_EXPORT(SetComponentSpeedupRateByMessageSkip, PE_SetSpeedupRateByMessageSkip),
	    HLL_EXPORT(GetComponentSpeedupRateByMessageSkip, PE_GetSpeedupRateByMessageSkip),
	    HLL_EXPORT(AddComponentMotionPos, PartsEngine_AddComponentMotionPos),
	    HLL_EXPORT(AddComponentMotionAlpha, PE_AddMotionAlpha_curve),
	    HLL_TODO_EXPORT(AddComponentMotionCG, PartsEngine_AddComponentMotionCG),
	    HLL_TODO_EXPORT(AddComponentMotionCGTermination, PartsEngine_AddComponentMotionCGTermination),
	    HLL_EXPORT(AddComponentMotionHGaugeRate, PE_AddMotionHGaugeRate_curve),
	    HLL_EXPORT(AddComponentMotionVGaugeRate, PE_AddMotionVGaugeRate_curve),
	    HLL_EXPORT(AddComponentMotionNumeralNumber, PE_AddMotionNumeralNumber_curve),
	    HLL_EXPORT(AddComponentMotionMagX, PE_AddMotionMagX_curve),
	    HLL_EXPORT(AddComponentMotionMagY, PE_AddMotionMagY_curve),
	    HLL_EXPORT(AddComponentMotionRotateX, PE_AddMotionRotateX_curve),
	    HLL_EXPORT(AddComponentMotionRotateY, PE_AddMotionRotateY_curve),
	    HLL_EXPORT(AddComponentMotionRotateZ, PE_AddMotionRotateZ_curve),
	    HLL_EXPORT(AddComponentMotionVibrationSize, PE_AddMotionVibrationSize),
	    HLL_TODO_EXPORT(SuspendBuildView, PartsEngine_SuspendBuildView),
	    HLL_EXPORT(SuspendTexture, PE_Noop_SuspendTexture),
	    HLL_EXPORT(ResumeTexture, PE_Noop_ResumeTexture),
	    HLL_TODO_EXPORT(SuspendBuildViewAt, PartsEngine_SuspendBuildViewAt),
	    HLL_TODO_EXPORT(ResumeBuildView, PartsEngine_ResumeBuildView),
	    HLL_EXPORT(SetButtonSize, PE_Noop_SetButtonSize),
	    HLL_TODO_EXPORT(SetButtonDrag, PartsEngine_SetButtonDrag),
	    HLL_TODO_EXPORT(IsButtonDrag, PartsEngine_IsButtonDrag),
	    HLL_EXPORT(SetButtonEnable, PE_Noop_SetButtonEnable),
	    HLL_TODO_EXPORT(IsButtonEnable, PartsEngine_IsButtonEnable),
	    HLL_TODO_EXPORT(SetButtonPixelDecide, PartsEngine_SetButtonPixelDecide),
	    HLL_TODO_EXPORT(IsButtonPixelDecide, PartsEngine_IsButtonPixelDecide),
	    HLL_EXPORT(SetButtonColor, PE_Noop_SetButtonColor),
	    HLL_TODO_EXPORT(GetButtonR, PartsEngine_GetButtonR),
	    HLL_TODO_EXPORT(GetButtonG, PartsEngine_GetButtonG),
	    HLL_TODO_EXPORT(GetButtonB, PartsEngine_GetButtonB),
	    HLL_TODO_EXPORT(SetButtonFontProperty, PartsEngine_SetButtonFontProperty),
	    HLL_TODO_EXPORT(GetButtonFontProperty, PartsEngine_GetButtonFontProperty),
	    HLL_TODO_EXPORT(SetButtonOnCursorSoundNumber, PartsEngine_SetButtonOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetButtonClickSoundNumber, PartsEngine_SetButtonClickSoundNumber),
	    HLL_TODO_EXPORT(GetButtonOnCursorSoundNumber, PartsEngine_GetButtonOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetButtonClickSoundNumber, PartsEngine_GetButtonClickSoundNumber),
	    HLL_EXPORT(SetButtonCGName, PE_Noop_SetButtonCGName),
	    HLL_TODO_EXPORT(GetButtonCGName, PartsEngine_GetButtonCGName),
	    HLL_EXPORT(SetButtonText, PE_Noop_SetButtonText),
	    HLL_TODO_EXPORT(GetButtonText, PartsEngine_GetButtonText),
	    HLL_EXPORT(SetCheckBoxSize, PE_Noop_SetCheckBoxSize),
	    HLL_TODO_EXPORT(SetCheckBoxDrag, PartsEngine_SetCheckBoxDrag),
	    HLL_TODO_EXPORT(IsCheckBoxDrag, PartsEngine_IsCheckBoxDrag),
	    /* [D174] 这一族原来是 TODO/空桩，`CommonMenu@SetCheckBoxState`
	     * 一进来就撞 Unimplemented。改成旁路表实现（见 parts.c）。 */
	    HLL_EXPORT(CheckBoxChecked, PE_CheckBoxChecked),
	    HLL_EXPORT(IsCheckBoxChecked, PE_IsCheckBoxChecked),
	    HLL_EXPORT(SetCheckBoxEnable, PE_SetCheckBoxEnable),
	    HLL_EXPORT(IsCheckBoxEnable, PE_IsCheckBoxEnable),
	    HLL_EXPORT(SetCheckBoxColor, PE_Noop_SetCheckBoxColor),
	    HLL_TODO_EXPORT(GetCheckBoxR, PartsEngine_GetCheckBoxR),
	    HLL_TODO_EXPORT(GetCheckBoxG, PartsEngine_GetCheckBoxG),
	    HLL_TODO_EXPORT(GetCheckBoxB, PartsEngine_GetCheckBoxB),
	    HLL_TODO_EXPORT(SetCheckBoxFontProperty, PartsEngine_SetCheckBoxFontProperty),
	    HLL_TODO_EXPORT(GetCheckBoxFontProperty, PartsEngine_GetCheckBoxFontProperty),
	    HLL_TODO_EXPORT(SetCheckBoxOnCursorSoundNumber, PartsEngine_SetCheckBoxOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetCheckBoxClickSoundNumber, PartsEngine_SetCheckBoxClickSoundNumber),
	    HLL_TODO_EXPORT(GetCheckBoxOnCursorSoundNumber, PartsEngine_GetCheckBoxOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetCheckBoxClickSoundNumber, PartsEngine_GetCheckBoxClickSoundNumber),
	    HLL_EXPORT(SetCheckBoxCGName, PE_Noop_SetCheckBoxCGName),
	    HLL_TODO_EXPORT(GetCheckBoxCGName, PartsEngine_GetCheckBoxCGName),
	    HLL_EXPORT(SetCheckBoxText, PE_Noop_SetCheckBoxText),
	    HLL_TODO_EXPORT(GetCheckBoxText, PartsEngine_GetCheckBoxText),
	    HLL_TODO_EXPORT(SetVScrollbarOnCursorSoundNumber, PartsEngine_SetVScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetVScrollbarClickSoundNumber, PartsEngine_SetVScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(GetVScrollbarOnCursorSoundNumber, PartsEngine_GetVScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetVScrollbarClickSoundNumber, PartsEngine_GetVScrollbarClickSoundNumber),
	    HLL_EXPORT(SetVScrollbarSize, PE_Noop_SetVScrollbarSize),
	    HLL_EXPORT(SetVScrollbarUpHeight, PE_Noop_SetVScrollbarUpHeight),
	    HLL_EXPORT(SetVScrollbarDownHeight, PE_Noop_SetVScrollbarDownHeight),
	    HLL_TODO_EXPORT(GetVScrollbarUpHeight, PartsEngine_GetVScrollbarUpHeight),
	    HLL_TODO_EXPORT(GetVScrollbarDownHeight, PartsEngine_GetVScrollbarDownHeight),
	    HLL_EXPORT(SetVScrollbarTotalSize, PE_Noop_SetVScrollbarTotalSize),
	    HLL_EXPORT(SetVScrollbarViewSize, PE_Noop_SetVScrollbarViewSize),
	    HLL_EXPORT(SetVScrollbarScrollPos, PE_Noop_SetVScrollbarScrollPos),
	    HLL_TODO_EXPORT(SetVScrollbarScrollRate, PartsEngine_SetVScrollbarScrollRate),
	    HLL_EXPORT(SetVScrollbarMoveSizeByButton, PE_Noop_SetVScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(GetVScrollbarTotalSize, PartsEngine_GetVScrollbarTotalSize),
	    HLL_TODO_EXPORT(GetVScrollbarViewSize, PartsEngine_GetVScrollbarViewSize),
	    HLL_TODO_EXPORT(GetVScrollbarScrollPos, PartsEngine_GetVScrollbarScrollPos),
	    HLL_TODO_EXPORT(GetVScrollbarScrollRate, PartsEngine_GetVScrollbarScrollRate),
	    HLL_TODO_EXPORT(GetVScrollbarMoveSizeByButton, PartsEngine_GetVScrollbarMoveSizeByButton),
	    HLL_EXPORT(SetVScrollbarCGName, PE_Noop_SetVScrollbarCGName),
	    HLL_TODO_EXPORT(GetVScrollbarCGName, PartsEngine_GetVScrollbarCGName),
	    HLL_TODO_EXPORT(SetHScrollbarOnCursorSoundNumber, PartsEngine_SetHScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(SetHScrollbarClickSoundNumber, PartsEngine_SetHScrollbarClickSoundNumber),
	    HLL_TODO_EXPORT(GetHScrollbarOnCursorSoundNumber, PartsEngine_GetHScrollbarOnCursorSoundNumber),
	    HLL_TODO_EXPORT(GetHScrollbarClickSoundNumber, PartsEngine_GetHScrollbarClickSoundNumber),
	    HLL_EXPORT(SetHScrollbarSize, PE_Noop_SetHScrollbarSize),
	    HLL_EXPORT(SetHScrollbarLeftWidth, PE_Noop_SetHScrollbarLeftWidth),
	    HLL_EXPORT(SetHScrollbarRightWidth, PE_Noop_SetHScrollbarRightWidth),
	    HLL_TODO_EXPORT(GetHScrollbarLeftWidth, PartsEngine_GetHScrollbarLeftWidth),
	    HLL_TODO_EXPORT(GetHScrollbarRightWidth, PartsEngine_GetHScrollbarRightWidth),
	    HLL_EXPORT(SetHScrollbarTotalSize, PE_Noop_SetHScrollbarTotalSize),
	    HLL_EXPORT(SetHScrollbarViewSize, PE_Noop_SetHScrollbarViewSize),
	    HLL_EXPORT(SetHScrollbarScrollPos, PE_Noop_SetHScrollbarScrollPos),
	    HLL_TODO_EXPORT(SetHScrollbarScrollRate, PartsEngine_SetHScrollbarScrollRate),
	    HLL_EXPORT(SetHScrollbarMoveSizeByButton, PE_Noop_SetHScrollbarMoveSizeByButton),
	    HLL_TODO_EXPORT(GetHScrollbarTotalSize, PartsEngine_GetHScrollbarTotalSize),
	    HLL_TODO_EXPORT(GetHScrollbarViewSize, PartsEngine_GetHScrollbarViewSize),
	    HLL_TODO_EXPORT(GetHScrollbarScrollPos, PartsEngine_GetHScrollbarScrollPos),
	    HLL_TODO_EXPORT(GetHScrollbarScrollRate, PartsEngine_GetHScrollbarScrollRate),
	    HLL_TODO_EXPORT(GetHScrollbarMoveSizeByButton, PartsEngine_GetHScrollbarMoveSizeByButton),
	    HLL_EXPORT(SetHScrollbarCGName, PE_Noop_SetHScrollbarCGName),
	    HLL_TODO_EXPORT(GetHScrollbarCGName, PartsEngine_GetHScrollbarCGName),
	    HLL_EXPORT(SetTextBoxSize, PE_Noop_SetTextBoxSize),
	    HLL_TODO_EXPORT(SetTextBoxFontProperty, PartsEngine_SetTextBoxFontProperty),
	    HLL_TODO_EXPORT(GetTextBoxFontProperty, PartsEngine_GetTextBoxFontProperty),
	    HLL_EXPORT(SetTextBoxText, PE_Noop_SetTextBoxText),
	    HLL_TODO_EXPORT(GetTextBoxText, PartsEngine_GetTextBoxText),
	    HLL_EXPORT(SetTextBoxMaxTextLength, PE_Noop_SetTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(GetTextBoxMaxTextLength, PartsEngine_GetTextBoxMaxTextLength),
	    HLL_EXPORT(SetTextBoxSelectColor, PE_Noop_SetTextBoxSelectColor),
	    HLL_TODO_EXPORT(GetTextBoxSelectR, PartsEngine_GetTextBoxSelectR),
	    HLL_TODO_EXPORT(GetTextBoxSelectG, PartsEngine_GetTextBoxSelectG),
	    HLL_TODO_EXPORT(GetTextBoxSelectB, PartsEngine_GetTextBoxSelectB),
	    HLL_EXPORT(SetTextBoxCGName, PE_Noop_SetTextBoxCGName),
	    HLL_TODO_EXPORT(GetTextBoxCGName, PartsEngine_GetTextBoxCGName),
	    HLL_TODO_EXPORT(OpenTextBoxIME, PartsEngine_OpenTextBoxIME),
	    HLL_TODO_EXPORT(CloseTextBoxIME, PartsEngine_CloseTextBoxIME),
	    HLL_EXPORT(SetListBoxSize, PE_Noop_SetListBoxSize),
	    HLL_EXPORT(SetListBoxLineHeight, PE_Noop_SetListBoxLineHeight),
	    HLL_TODO_EXPORT(GetListBoxLineHeight, PartsEngine_GetListBoxLineHeight),
	    HLL_EXPORT(SetListBoxMargin, PE_Noop_SetListBoxMargin),
	    HLL_TODO_EXPORT(GetListBoxWidthMargin, PartsEngine_GetListBoxWidthMargin),
	    HLL_TODO_EXPORT(GetListBoxHeightMargin, PartsEngine_GetListBoxHeightMargin),
	    HLL_EXPORT(SetListBoxCGName, PE_Noop_SetListBoxCGName),
	    HLL_TODO_EXPORT(GetListBoxCGName, PartsEngine_GetListBoxCGName),
	    HLL_EXPORT(SetListBoxScrollPos, PE_Noop_SetListBoxScrollPos),
	    HLL_TODO_EXPORT(GetListBoxScrollPos, PartsEngine_GetListBoxScrollPos),
	    HLL_TODO_EXPORT(AddListBoxItem, PartsEngine_AddListBoxItem),
	    HLL_TODO_EXPORT(InsertListBoxItem, PartsEngine_InsertListBoxItem),
	    HLL_EXPORT(SetListBoxItem, PE_Noop_SetListBoxItem),
	    HLL_TODO_EXPORT(GetListBoxItemCount, PartsEngine_GetListBoxItemCount),
	    HLL_TODO_EXPORT(GetListBoxItem, PartsEngine_GetListBoxItem),
	    HLL_TODO_EXPORT(EraseListBoxItem, PartsEngine_EraseListBoxItem),
	    HLL_TODO_EXPORT(ClearListBoxItem, PartsEngine_ClearListBoxItem),
	    HLL_TODO_EXPORT(GetListBoxOnCursorItemIndex, PartsEngine_GetListBoxOnCursorItemIndex),
	    HLL_TODO_EXPORT(GetListBoxOnCursorItem, PartsEngine_GetListBoxOnCursorItem),
	    HLL_TODO_EXPORT(SetListBoxFontProperty, PartsEngine_SetListBoxFontProperty),
	    HLL_TODO_EXPORT(GetListBoxFontProperty, PartsEngine_GetListBoxFontProperty),
	    HLL_EXPORT(SetListBoxSelectIndex, PE_Noop_SetListBoxSelectIndex),
	    HLL_TODO_EXPORT(GetListBoxSelectIndex, PartsEngine_GetListBoxSelectIndex),
	    HLL_EXPORT(SetComboBoxSize, PE_Noop_SetComboBoxSize),
	    HLL_EXPORT(SetComboBoxTextMargin, PE_Noop_SetComboBoxTextMargin),
	    HLL_TODO_EXPORT(GetComboBoxTextWidthMargin, PartsEngine_GetComboBoxTextWidthMargin),
	    HLL_TODO_EXPORT(GetComboBoxTextHeightMargin, PartsEngine_GetComboBoxTextHeightMargin),
	    HLL_EXPORT(SetComboBoxCGName, PE_Noop_SetComboBoxCGName),
	    HLL_TODO_EXPORT(GetComboBoxCGName, PartsEngine_GetComboBoxCGName),
	    HLL_EXPORT(SetComboBoxText, PE_Noop_SetComboBoxText),
	    HLL_TODO_EXPORT(GetComboBoxText, PartsEngine_GetComboBoxText),
	    HLL_TODO_EXPORT(SetComboBoxFontProperty, PartsEngine_SetComboBoxFontProperty),
	    HLL_TODO_EXPORT(GetComboBoxFontProperty, PartsEngine_GetComboBoxFontProperty),
	    HLL_EXPORT(SetMultiTextBoxSize, PE_Noop_SetMultiTextBoxSize),
	    HLL_TODO_EXPORT(SetMultiTextBoxFontProperty, PartsEngine_SetMultiTextBoxFontProperty),
	    HLL_TODO_EXPORT(GetMultiTextBoxFontProperty, PartsEngine_GetMultiTextBoxFontProperty),
	    HLL_EXPORT(SetMultiTextBoxText, PE_Noop_SetMultiTextBoxText),
	    HLL_TODO_EXPORT(GetMultiTextBoxText, PartsEngine_GetMultiTextBoxText),
	    HLL_EXPORT(SetMultiTextBoxMaxTextLength, PE_Noop_SetMultiTextBoxMaxTextLength),
	    HLL_TODO_EXPORT(GetMultiTextBoxMaxTextLength, PartsEngine_GetMultiTextBoxMaxTextLength),
	    HLL_EXPORT(SetMultiTextBoxSelectColor, PE_Noop_SetMultiTextBoxSelectColor),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectR, PartsEngine_GetMultiTextBoxSelectR),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectG, PartsEngine_GetMultiTextBoxSelectG),
	    HLL_TODO_EXPORT(GetMultiTextBoxSelectB, PartsEngine_GetMultiTextBoxSelectB),
	    HLL_EXPORT(SetMultiTextBoxCGName, PE_Noop_SetMultiTextBoxCGName),
	    HLL_TODO_EXPORT(GetMultiTextBoxCGName, PartsEngine_GetMultiTextBoxCGName),
	    HLL_EXPORT(SetLayoutBoxLayoutType, PE_SetLayoutBoxLayoutType),
	    HLL_EXPORT(GetLayoutBoxLayoutType, PE_GetLayoutBoxLayoutType),
	    HLL_EXPORT(SetLayoutBoxReturn, PE_SetLayoutBoxReturn),
	    HLL_EXPORT(IsLayoutBoxReturn, PE_IsLayoutBoxReturn),
	    HLL_EXPORT(GetLayoutBoxReturnSize, PE_GetLayoutBoxReturnSize),
	    HLL_EXPORT(SetLayoutBoxAlign, PE_SetLayoutBoxAlign),
	    HLL_EXPORT(GetLayoutBoxAlign, PE_GetLayoutBoxAlign),
	    /* [D385] 这四个 getter 与 SetLayoutBoxPadding 之前只挂在 CALLHLL 的
	     * `case 94..98` 分派上，而兰斯10 走的是**静态链接（按名字）**，
	     * 于是运行期一直报 `Unimplemented library function:
	     * PartsEngine.GetLayoutBoxPaddingTop`，setter 则绑到了空实现
	     * （`PE_Noop_SetLayoutBoxPadding`）—— 布局盒的 padding 恒为 0，
	     * 子控件全挤在父容器原点（战况报告屏三行数字叠在左上就是这么来的）。 */
	    HLL_EXPORT(SetLayoutBoxPadding, PE_set_layoutbox_padding),
	    HLL_EXPORT(GetLayoutBoxPaddingTop, PE_get_layoutbox_padding_top),
	    HLL_EXPORT(GetLayoutBoxPaddingBottom, PE_get_layoutbox_padding_bottom),
	    HLL_EXPORT(GetLayoutBoxPaddingLeft, PE_get_layoutbox_padding_left),
	    HLL_EXPORT(GetLayoutBoxPaddingRight, PE_get_layoutbox_padding_right),
	    HLL_EXPORT(Parts_SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(Parts_SetPartsCGThread, PE_SetPartsCGThread),
	    HLL_EXPORT(Parts_IsThreadLoading, PE_IsThreadLoading),
	    HLL_EXPORT(Parts_GetPartsCGName, PE_GetPartsCGName_v12),
	    HLL_EXPORT(Parts_SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
	    HLL_EXPORT(Parts_SetLoopCG, PE_SetLoopCG),
	    HLL_EXPORT(Parts_SetLoopCGSurfaceArea, PE_SetLoopCGSurfaceArea),
	    HLL_EXPORT(Parts_SetText, PE_SetText),
	    HLL_EXPORT(Parts_AddPartsText, PE_AddPartsText),
	    HLL_TODO_EXPORT(Parts_DeletePartsTopTextLine, PartsEngine_Parts_DeletePartsTopTextLine),
	    HLL_EXPORT(Parts_SetPartsTextSurfaceArea, PE_SetPartsTextSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsTextHighlight, PE_Noop_Parts_SetPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_AddPartsTextHighlight, PartsEngine_Parts_AddPartsTextHighlight),
	    HLL_TODO_EXPORT(Parts_ClearPartsTextHighlight, PartsEngine_Parts_ClearPartsTextHighlight),
	    HLL_EXPORT(Parts_SetPartsTextCountReturn, PE_Noop_Parts_SetPartsTextCountReturn),
	    HLL_TODO_EXPORT(Parts_GetPartsTextCountReturn, PartsEngine_Parts_GetPartsTextCountReturn),
	    HLL_EXPORT(Parts_SetFont, PE_SetFont),
	    HLL_EXPORT(Parts_SetPartsFontType, PE_SetPartsFontType),
	    HLL_EXPORT(Parts_SetPartsFontSize, PE_SetPartsFontSize),
	    HLL_EXPORT(Parts_SetPartsFontColor, PE_SetPartsFontColor),
	    HLL_EXPORT(Parts_SetPartsFontBoldWeight, PE_SetPartsFontBoldWeight),
	    HLL_EXPORT(Parts_SetPartsFontEdgeColor, PE_SetPartsFontEdgeColor),
	    HLL_EXPORT(Parts_SetPartsFontEdgeWeight, PE_SetPartsFontEdgeWeight),
	    HLL_EXPORT(Parts_SetTextCharSpace, PE_SetTextCharSpace),
	    HLL_EXPORT(Parts_GetTextCharSpace, PE_GetTextCharSpace),
	    HLL_EXPORT(Parts_GetTextLineSpace, PE_GetTextLineSpace),
	    HLL_EXPORT(Parts_SetTextLineSpace, PE_SetTextLineSpace),
	    HLL_EXPORT(Parts_SetHGaugeCG, PE_SetHGaugeCG),
	    HLL_EXPORT(Parts_SetHGaugeRate, PE_SetHGaugeRate),
	    HLL_EXPORT(Parts_SetVGaugeCG, PE_SetVGaugeCG),
	    HLL_EXPORT(Parts_SetVGaugeRate, PE_SetVGaugeRate),
	    HLL_EXPORT(Parts_SetHGaugeSurfaceArea, PE_SetHGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetVGaugeSurfaceArea, PE_SetVGaugeSurfaceArea),
	    HLL_EXPORT(Parts_SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(Parts_SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_TODO_EXPORT(Parts_SetNumeralFont, PartsEngine_Parts_SetNumeralFont),
	    HLL_EXPORT(Parts_SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(Parts_SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(Parts_SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(Parts_SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(Parts_SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_EXPORT(Parts_SetPartsRectangleDetectionSurfaceArea, PE_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(Parts_SetPartsCGDetectionSurfaceArea, PartsEngine_Parts_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlat, PE_SetPartsFlat),
	    HLL_EXPORT(Parts_SetPartsFlatThread, PE_SetPartsFlatThread),
	    HLL_EXPORT(Parts_GetPartsFlatName, PE_GetPartsFlatName),
	    HLL_EXPORT(Parts_IsPartsFlatEnd, PE_IsPartsFlatEnd),
	    HLL_EXPORT(Parts_GetPartsFlatCurrentFrameNumber, PE_GetPartsFlatCurrentFrameNumber),
	    HLL_EXPORT(Parts_BackPartsFlatBeginFrame, PE_BackPartsFlatBeginFrame),
	    HLL_EXPORT(Parts_StepPartsFlatFinalFrame, PE_StepPartsFlatFinalFrame),
	    HLL_EXPORT(Parts_SetPartsFlatSurfaceArea, PE_SetPartsFlatSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlatAndStop, PE_SetPartsFlatAndStop),
	    HLL_EXPORT(Parts_StopPartsFlat, PE_StopPartsFlat),
	    HLL_EXPORT(Parts_StartPartsFlat, PE_StartPartsFlat),
	    HLL_EXPORT(Parts_GoFramePartsFlat, PE_GoFramePartsFlat),
	    HLL_EXPORT(Parts_GetPartsFlatEndFrame, PE_GetPartsFlatEndFrame),
	    HLL_EXPORT(Parts_ExistsFlatFile, PE_ExistsFlatFile),
	    HLL_TODO_EXPORT(Parts_GetPartsFlatDataInfo, PartsEngine_Parts_GetPartsFlatDataInfo),
	    /* [D342] 技能卡换图（`d342` 在点角色卡后撞上它，VM_ERROR 直接
	     * 把技能列表打断）。语义与 `SetPartsFlat` 一致：
	     * `(parts_no, cg_name, state)` 换掉该状态的 flat。 */
	    HLL_EXPORT(Parts_ChangeFlatCG, PE_SetPartsFlat),
	    HLL_TODO_EXPORT(Parts_ChangeFlatSound, PartsEngine_Parts_ChangeFlatSound),
	    HLL_EXPORT(Parts_ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateToPartsConstructionProcess, PE_AddCreateToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreatePixelOnlyToPartsConstructionProcess, PE_AddCreatePixelOnlyToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCreateCGToProcess, PE_AddCreateCGToProcess),
	    HLL_EXPORT(Parts_AddFillToPartsConstructionProcess, PE_AddFillToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAlphaColorToPartsConstructionProcess, PE_AddFillAlphaColorToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillAMapToPartsConstructionProcess, PE_AddFillAMapToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddFillWithAlphaToPartsConstructionProcess, PE_AddFillWithAlphaToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddFillGradationHorizonToPartsConstructionProcess, PartsEngine_Parts_AddFillGradationHorizonToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawRectToPartsConstructionProcess, PE_AddDrawRectToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddDrawCutCGToPartsConstructionProcess, PE_AddDrawCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyCutCGToPartsConstructionProcess, PE_AddCopyCutCGToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddGrayFilterToPartsConstructionProcess, PE_AddGrayFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddAddFilterToPartsConstructionProcess, PartsEngine_Parts_AddAddFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddMulFilterToPartsConstructionProcess, PartsEngine_Parts_AddMulFilterToPartsConstructionProcess),
	    HLL_TODO_EXPORT(Parts_AddDrawLineToPartsConstructionProcess, PartsEngine_Parts_AddDrawLineToPartsConstructionProcess),
	    HLL_EXPORT(Parts_BuildPartsConstructionProcess, PE_BuildPartsConstructionProcess),
	    HLL_EXPORT(Parts_BuildPartsConstructionProcessThread, PE_BuildPartsConstructionProcessThread),
	    HLL_EXPORT(Parts_ClearPartsConstructionProcess, PE_ClearPartsConstructionProcess),
	    HLL_EXPORT(Parts_GetPartsConstructionProcessCount, PE_GetPartsConstructionProcessCount),
	    HLL_EXPORT(Parts_AddDrawTextToPartsConstructionProcess, PE_AddDrawTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_AddCopyTextToPartsConstructionProcess, PE_AddCopyTextToPartsConstructionProcess),
	    HLL_EXPORT(Parts_SetPartsConstructionSurfaceArea, PE_SetPartsConstructionSurfaceArea),
	    HLL_EXPORT(Parts_CreateParts3DLayerPluginID, PE_CreateParts3DLayerPluginID),
	    HLL_EXPORT(Parts_GetParts3DLayerPluginID, PE_GetParts3DLayerPluginID),
	    HLL_EXPORT(Parts_ReleaseParts3DLayerPluginID, PE_ReleaseParts3DLayerPluginID),
	    HLL_EXPORT(Parts_SetPassCursor, PE_SetPassCursor),
	    HLL_EXPORT(Parts_SetClickable, PE_SetClickable),
	    HLL_TODO_EXPORT(Parts_SetResetTimerByChangeInputStatus, PartsEngine_Parts_SetResetTimerByChangeInputStatus),
	    HLL_EXPORT(Parts_SetDrag, PE_SetDrag),
	    HLL_EXPORT(Parts_SetParentPartsNumber, PE_SetParentPartsNumber),
	    HLL_EXPORT(Parts_SetInputState, PE_SetInputState),
	    HLL_EXPORT(Parts_SetOnCursorShowLinkPartsNumber, PE_SetOnCursorShowLinkPartsNumber),
	    HLL_TODO_EXPORT(Parts_SetSoundNumber, PartsEngine_Parts_SetSoundNumber),
	    HLL_EXPORT(Parts_SetPartsPixelDecide, PE_SetPartsPixelDecide),
	    HLL_EXPORT(Parts_GetPartsPassCursor, PE_GetPartsPassCursor),
	    HLL_EXPORT(Parts_GetPartsClickable, PE_GetPartsClickable),
	    HLL_TODO_EXPORT(Parts_GetResetTimerByChangeInputStatus, PartsEngine_Parts_GetResetTimerByChangeInputStatus),
	    HLL_TODO_EXPORT(Parts_GetPartsDrag, PartsEngine_Parts_GetPartsDrag),
	    /* [D312] 删除同名重复登记：本行原为 `HLL_TODO_EXPORT(Parts_GetParentPartsNumber, …)`，
	    /* 与上方 HLL_EXPORT 重复，覆盖率把它算两次、工具误报 TODO。 */
	    HLL_EXPORT(Parts_GetInputState, PE_GetInputState),
	    HLL_TODO_EXPORT(Parts_GetOnCursorShowLinkPartsNumber, PartsEngine_Parts_GetOnCursorShowLinkPartsNumber),
	    HLL_TODO_EXPORT(Parts_GetSoundNumber, PartsEngine_Parts_GetSoundNumber),
	    HLL_TODO_EXPORT(Parts_IsPartsPixelDecide, PartsEngine_Parts_IsPartsPixelDecide),
	    HLL_EXPORT(Parts_IsCursorIn, PE_IsCursorIn),
	    HLL_TODO_EXPORT(SaveParts, PartsEngine_SaveParts),
	    HLL_TODO_EXPORT(LoadParts, PartsEngine_LoadParts),
	    HLL_EXPORT(CreateUserPartsNumber, PartsEngine_CreateUserPartsNumber),
	    HLL_EXPORT(SetEnableInputProcess, PartsEngine_SetEnableInputProcess),
	    HLL_EXPORT(Parts_SetAnchor, PE_Noop_Parts_SetAnchor),
	    HLL_EXPORT(Parts_SetFlatBeginSeekFrame, PE_Noop_Parts_SetFlatBeginSeekFrame),
	    HLL_EXPORT(Parts_SetFlatEnableLayer, PE_Noop_Parts_SetFlatEnableLayer),
	    HLL_EXPORT(Parts_SetHGaugeReverse, PE_Noop_Parts_SetHGaugeReverse),
	    HLL_EXPORT(Parts_SetLockUpdate, PE_Noop_Parts_SetLockUpdate),
	    HLL_EXPORT(Parts_SetPosForRate, PE_Noop_Parts_SetPosForRate),
	    HLL_EXPORT(Parts_SetRubyCharSpace, PE_Noop_Parts_SetRubyCharSpace),
	    HLL_EXPORT(Parts_SetSoundName, PE_Noop_Parts_SetSoundName),
	    HLL_EXPORT(Parts_SetSwipeType, PE_Noop_Parts_SetSwipeType),
	    HLL_EXPORT(Parts_SetTextEnableTag, PE_Noop_Parts_SetTextEnableTag),
	    HLL_EXPORT(Parts_SetTextPos, PE_Noop_Parts_SetTextPos),
	    HLL_EXPORT(Parts_SetTextShowTime, PE_Noop_Parts_SetTextShowTime),
	    HLL_EXPORT(Parts_SetUpdateLookAtPos, PE_Noop_Parts_SetUpdateLookAtPos),
	    HLL_EXPORT(Parts_SetVGaugeReverse, PE_Noop_Parts_SetVGaugeReverse),
	    HLL_EXPORT(Parts_SetWheelable, PE_Noop_Parts_SetWheelable),
	    HLL_EXPORT(SetActiveController, PE_SetActiveController),
	    HLL_EXPORT(SetButtonCharSpace, PE_Noop_SetButtonCharSpace),
	    HLL_EXPORT(SetButtonFlatName, PE_Noop_SetButtonFlatName),
	    HLL_EXPORT(SetButtonLineSpace, PE_Noop_SetButtonLineSpace),
	    HLL_EXPORT(SetButtonTextOriginPosMode, PE_Noop_SetButtonTextOriginPosMode),
	    HLL_EXPORT(SetCheckBoxButtonMode, PE_Noop_SetCheckBoxButtonMode),
	    HLL_EXPORT(SetCheckBoxCharSpace, PE_Noop_SetCheckBoxCharSpace),
	    HLL_EXPORT(SetCheckBoxFlatName, PE_Noop_SetCheckBoxFlatName),
	    HLL_EXPORT(SetCheckBoxLineSpace, PE_Noop_SetCheckBoxLineSpace),
	    HLL_EXPORT(SetCheckBoxOverrideCGName, PE_Noop_SetCheckBoxOverrideCGName),
	    HLL_EXPORT(SetCheckBoxOverrideCheckedCGName, PE_Noop_SetCheckBoxOverrideCheckedCGName),
	    HLL_EXPORT(SetCheckBoxOverrideCheckedFlatName, PE_Noop_SetCheckBoxOverrideCheckedFlatName),
	    HLL_EXPORT(SetCheckBoxOverrideFlatName, PE_Noop_SetCheckBoxOverrideFlatName),
	    HLL_EXPORT(SetCheckBoxTextOriginPosMode, PE_Noop_SetCheckBoxTextOriginPosMode),
	    HLL_EXPORT(SetClickMissSoundName, PE_Noop_SetClickMissSoundName),
	    HLL_EXPORT(SetComboBoxCharSpace, PE_Noop_SetComboBoxCharSpace),
	    HLL_EXPORT(SetComboBoxListBoxCharSpace, PE_Noop_SetComboBoxListBoxCharSpace),
	    HLL_EXPORT(SetComboBoxListBoxHeight, PE_Noop_SetComboBoxListBoxHeight),
	    HLL_EXPORT(SetComboBoxListBoxItem, PE_Noop_SetComboBoxListBoxItem),
	    HLL_EXPORT(SetComboBoxListBoxLineHeight, PE_Noop_SetComboBoxListBoxLineHeight),
	    HLL_EXPORT(SetComboBoxListBoxMargin, PE_Noop_SetComboBoxListBoxMargin),
	    HLL_EXPORT(SetComboBoxListBoxScrollPos, PE_Noop_SetComboBoxListBoxScrollPos),
	    HLL_EXPORT(SetComboBoxListBoxSelectIndex, PE_Noop_SetComboBoxListBoxSelectIndex),
	    HLL_EXPORT(SetComponentCheckBoxShowLinkNumber, PE_Noop_SetComponentCheckBoxShowLinkNumber),
	    HLL_EXPORT(SetComponentClipArea, PE_Noop_SetComponentClipArea),
	    HLL_EXPORT(SetComponentEnableClipArea, PE_Noop_SetComponentEnableClipArea),
	    HLL_EXPORT(SetComponentMessageWindowEffectLink, PE_Noop_SetComponentMessageWindowEffectLink),
	    HLL_EXPORT(SetComponentReverseLR, PE_Noop_SetComponentReverseLR),
	    HLL_EXPORT(SetComponentReverseTB, PE_Noop_SetComponentReverseTB),
	    HLL_EXPORT(SetComponentScrollAlphaLinkNumber, PE_Noop_SetComponentScrollAlphaLinkNumber),
	    HLL_EXPORT(SetComponentScrollPosXLinkNumber, PE_Noop_SetComponentScrollPosXLinkNumber),
	    HLL_EXPORT(SetComponentScrollPosYLinkNumber, PE_Noop_SetComponentScrollPosYLinkNumber),
	    HLL_EXPORT(SetComponentShowEditor, PE_Noop_SetComponentShowEditor),
	    HLL_EXPORT(SetComponentTextureAddressType, PE_Noop_SetComponentTextureAddressType),
	    HLL_EXPORT(SetEnableSpeedUp, PE_Noop_SetEnableSpeedUp),
	    HLL_EXPORT(SetFontColorForBackScene, PE_Noop_SetFontColorForBackScene),
	    HLL_EXPORT(SetFormCaptionColor, PE_Noop_SetFormCaptionColor),
	    HLL_EXPORT(SetFormCaptionPlusHeight, PE_Noop_SetFormCaptionPlusHeight),
	    HLL_EXPORT(SetFormClientColor, PE_Noop_SetFormClientColor),
	    HLL_EXPORT(SetFormEnableCloseButton, PE_Noop_SetFormEnableCloseButton),
	    HLL_EXPORT(SetFormFold, PE_Noop_SetFormFold),
	    HLL_EXPORT(SetFormGroupViewSpan, PE_Noop_SetFormGroupViewSpan),
	    HLL_EXPORT(SetFormHScrollPos, PE_Noop_SetFormHScrollPos),
	    HLL_EXPORT(SetFormIcon, PE_Noop_SetFormIcon),
	    HLL_EXPORT(SetFormMaxSize, PE_Noop_SetFormMaxSize),
	    HLL_EXPORT(SetFormMinSize, PE_Noop_SetFormMinSize),
	    HLL_EXPORT(SetFormResizable, PE_Noop_SetFormResizable),
	    HLL_EXPORT(SetFormSize, PE_Noop_SetFormSize),
	    HLL_EXPORT(SetFormTitle, PE_Noop_SetFormTitle),
	    HLL_EXPORT(SetFormVScrollPos, PE_Noop_SetFormVScrollPos),
	    HLL_EXPORT(SetHScrollTotalSizeLinkNumber, PE_Noop_SetHScrollTotalSizeLinkNumber),
	    HLL_EXPORT(SetHScrollViewSizeLinkNumber, PE_Noop_SetHScrollViewSizeLinkNumber),
	    HLL_EXPORT(SetHScrollbarFlatName, PE_Noop_SetHScrollbarFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideBGCGName, PE_Noop_SetHScrollbarOverrideBGCGName),
	    HLL_EXPORT(SetHScrollbarOverrideBGFlatName, PE_Noop_SetHScrollbarOverrideBGFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideBarCGName, PE_Noop_SetHScrollbarOverrideBarCGName),
	    HLL_EXPORT(SetHScrollbarOverrideBarFlatName, PE_Noop_SetHScrollbarOverrideBarFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideLeftCGName, PE_Noop_SetHScrollbarOverrideLeftCGName),
	    HLL_EXPORT(SetHScrollbarOverrideLeftFlatName, PE_Noop_SetHScrollbarOverrideLeftFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideRightCGName, PE_Noop_SetHScrollbarOverrideRightCGName),
	    HLL_EXPORT(SetHScrollbarOverrideRightFlatName, PE_Noop_SetHScrollbarOverrideRightFlatName),
	    HLL_EXPORT(SetKeyWaitCGName, PE_Noop_SetKeyWaitCGName),
	    HLL_EXPORT(SetKeyWaitFlatName, PE_Noop_SetKeyWaitFlatName),
	    HLL_EXPORT(SetKeyWaitPos, PE_Noop_SetKeyWaitPos),
	    HLL_EXPORT(SetKeyWaitShow, PE_Noop_SetKeyWaitShow),
	    HLL_EXPORT(SetLayoutBoxReturnSizeForRate, PE_Noop_SetLayoutBoxReturnSizeForRate),
	    HLL_EXPORT(SetListBoxCharSpace, PE_Noop_SetListBoxCharSpace),
	    HLL_EXPORT(SetLockInputState, PE_Noop_SetLockInputState),
	    HLL_EXPORT(SetMessageWindowActive, PE_Noop_SetMessageWindowActive),
	    HLL_EXPORT(SetMessageWindowCGName, PE_Noop_SetMessageWindowCGName),
	    HLL_EXPORT(SetMessageWindowFlatName, PE_Noop_SetMessageWindowFlatName),
	    HLL_EXPORT(GetMessageWindowFlatName, PE_GetMessageWindowFlatName),
	    HLL_EXPORT(GetMessageWindowCGName, PE_GetMessageWindowCGName),
	    HLL_EXPORT(GetKeyWaitFlatName, PE_GetKeyWaitFlatName),
	    HLL_EXPORT(GetMessageWindowFlatShowWaitFrameNumber, PE_GetMessageWindowFlatShowWaitFrameNumber),
	    HLL_EXPORT(IsOverMessageWindowFlatShowWaitFrame, PE_IsOverMessageWindowFlatShowWaitFrame),
	    HLL_EXPORT(BackMessageWindowFlatBeginFrame, PE_BackMessageWindowFlatBeginFrame),
	    HLL_EXPORT(StepMessageWindowFlatFinalFrame, PE_StepMessageWindowFlatFinalFrame),
	    HLL_EXPORT(SetMessageWindowFlatShowWaitFrameNumber, PE_Noop_SetMessageWindowFlatShowWaitFrameNumber),
	    HLL_EXPORT(SetMessageWindowInactiveMultipleColor, PE_Noop_SetMessageWindowInactiveMultipleColor),
	    HLL_EXPORT(SetMessageWindowText, PE_SetMessageWindowText),
	    HLL_EXPORT(SetMessageWindowTextFont, PE_SetMessageWindowTextFont),
	    HLL_EXPORT(FixMessageWindowText, PE_FixMessageWindowText),
	    HLL_EXPORT(IsFixedMessageWindowText, PE_IsFixedMessageWindowText),
	    HLL_EXPORT(SetMessageWindowTextArea, PE_Noop_SetMessageWindowTextArea),
	    HLL_EXPORT(SetMessageWindowTextOriginPosMode, PE_Noop_SetMessageWindowTextOriginPosMode),
	    HLL_EXPORT(SetMessageWindowTextSpace, PE_Noop_SetMessageWindowTextSpace),
	    HLL_EXPORT(SetMessageWindowTextSpeed, PE_Noop_SetMessageWindowTextSpeed),
	    HLL_EXPORT(SetMotionData, PE_Noop_SetMotionData),
	    HLL_EXPORT(SetMovieTime, PE_Noop_SetMovieTime),
	    HLL_EXPORT(SetMultiTextBoxBGColor, PE_Noop_SetMultiTextBoxBGColor),
	    HLL_EXPORT(SetMultiTextBoxCharSpace, PE_Noop_SetMultiTextBoxCharSpace),
	    HLL_EXPORT(SetMultiTextBoxFrameColor, PE_Noop_SetMultiTextBoxFrameColor),
	    HLL_EXPORT(SetMultiTextBoxHScrollShow, PE_Noop_SetMultiTextBoxHScrollShow),
	    HLL_EXPORT(SetMultiTextBoxLineSpace, PE_Noop_SetMultiTextBoxLineSpace),
	    HLL_EXPORT(SetMultiTextBoxPadding, PE_Noop_SetMultiTextBoxPadding),
	    HLL_EXPORT(SetMultiTextBoxReadOnly, PE_Noop_SetMultiTextBoxReadOnly),
	    HLL_EXPORT(SetMultiTextBoxReadOnlyBGColor, PE_Noop_SetMultiTextBoxReadOnlyBGColor),
	    HLL_EXPORT(SetMultiTextBoxVScrollShow, PE_Noop_SetMultiTextBoxVScrollShow),
	    HLL_EXPORT(SetNumeralFullPitch, PE_Noop_SetNumeralFullPitch),
	    HLL_EXPORT(SetNumeralShowPadding, PE_Noop_SetNumeralShowPadding),
	    HLL_EXPORT(SetNumeralShowType, PE_Noop_SetNumeralShowType),
	    HLL_EXPORT(SetOpenTextBoxIME, PE_Noop_SetOpenTextBoxIME),
	    HLL_EXPORT(SetPanelAlphaGradationBottom, PE_Noop_SetPanelAlphaGradationBottom),
	    HLL_EXPORT(SetPanelAlphaGradationLeft, PE_Noop_SetPanelAlphaGradationLeft),
	    HLL_EXPORT(SetPanelAlphaGradationRight, PE_Noop_SetPanelAlphaGradationRight),
	    HLL_EXPORT(SetPanelAlphaGradationTop, PE_Noop_SetPanelAlphaGradationTop),
	    HLL_EXPORT(SetPanelColor, PE_Noop_SetPanelColor),
	    HLL_EXPORT(SetPanelSize, PE_Noop_SetPanelSize),
	    HLL_EXPORT(SetRectangleDetectionPoint, PE_Noop_SetRectangleDetectionPoint),
	    HLL_EXPORT(SetShowForBackScene, PE_SetShowForBackScene),
	    HLL_EXPORT(SetSpinBoxBGColor, PE_Noop_SetSpinBoxBGColor),
	    HLL_EXPORT(SetSpinBoxCGName, PE_Noop_SetSpinBoxCGName),
	    HLL_EXPORT(SetSpinBoxCharSpace, PE_Noop_SetSpinBoxCharSpace),
	    HLL_EXPORT(SetSpinBoxDecimalPlace, PE_Noop_SetSpinBoxDecimalPlace),
	    HLL_EXPORT(SetSpinBoxFrameColor, PE_Noop_SetSpinBoxFrameColor),
	    HLL_EXPORT(SetSpinBoxReadOnly, PE_Noop_SetSpinBoxReadOnly),
	    HLL_EXPORT(SetSpinBoxReadOnlyBGColor, PE_Noop_SetSpinBoxReadOnlyBGColor),
	    HLL_EXPORT(SetSpinBoxSelectColor, PE_Noop_SetSpinBoxSelectColor),
	    HLL_EXPORT(SetSpinBoxSize, PE_Noop_SetSpinBoxSize),
	    HLL_EXPORT(SetTextBoxBGColor, PE_Noop_SetTextBoxBGColor),
	    HLL_EXPORT(SetTextBoxCharSpace, PE_Noop_SetTextBoxCharSpace),
	    HLL_EXPORT(SetTextBoxFrameColor, PE_Noop_SetTextBoxFrameColor),
	    HLL_EXPORT(SetTextBoxReadOnly, PE_Noop_SetTextBoxReadOnly),
	    HLL_EXPORT(SetTextBoxReadOnlyBGColor, PE_Noop_SetTextBoxReadOnlyBGColor),
	    HLL_EXPORT(SetUserComponentData, PE_SetUserComponentData_str),
	    HLL_EXPORT(SetUserComponentName, PE_SetUserComponentName_str),
	    HLL_EXPORT(GetUserComponentData, PE_GetUserComponentData_str),
	    HLL_EXPORT(GetUserComponentName, PE_GetUserComponentName_str),
	    HLL_EXPORT(SetVScrollTotalSizeLinkNumber, PE_Noop_SetVScrollTotalSizeLinkNumber),
	    HLL_EXPORT(SetVScrollViewSizeLinkNumber, PE_Noop_SetVScrollViewSizeLinkNumber),
	    HLL_EXPORT(SetVScrollbarFlatName, PE_Noop_SetVScrollbarFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideBGCGName, PE_Noop_SetVScrollbarOverrideBGCGName),
	    HLL_EXPORT(SetVScrollbarOverrideBGFlatName, PE_Noop_SetVScrollbarOverrideBGFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideBarCGName, PE_Noop_SetVScrollbarOverrideBarCGName),
	    HLL_EXPORT(SetVScrollbarOverrideBarFlatName, PE_Noop_SetVScrollbarOverrideBarFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideDownCGName, PE_Noop_SetVScrollbarOverrideDownCGName),
	    HLL_EXPORT(SetVScrollbarOverrideDownFlatName, PE_Noop_SetVScrollbarOverrideDownFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideUpCGName, PE_Noop_SetVScrollbarOverrideUpCGName),
	    HLL_EXPORT(SetVScrollbarOverrideUpFlatName, PE_Noop_SetVScrollbarOverrideUpFlatName),
	    HLL_EXPORT(IsExistActivity, PartsEngine_IsExistActivity),
	    HLL_EXPORT(CreateActivity, PE_Act_CreateActivity),
	    HLL_EXPORT(ReleaseActivity, PE_Act_ReleaseActivity),
	    HLL_EXPORT(SaveActivityEXText, PE_Act_SaveActivityEXText),
	    HLL_EXPORT(LoadActivityEXText, PE_Act_LoadActivityEXText),
	    HLL_EXPORT(ReadActivityFile, PE_Act_ReadActivityFile),
	    HLL_EXPORT(WriteActivityFile, PE_Act_WriteActivityFile),
	    HLL_EXPORT(IsExistActivityFile, PE_Act_IsExistActivityFile),
	    HLL_EXPORT(AddActivityParts, PE_Act_AddActivityParts),
	    HLL_EXPORT(RemoveActivityParts, PE_Act_RemoveActivityParts),
	    HLL_EXPORT(RemoveAllActivityParts, PE_Act_RemoveAllActivityParts),
	    HLL_EXPORT(NumofActivityParts, PE_Act_NumofActivityParts),
	    HLL_EXPORT(IsExistActivityPartsByName, PE_Act_IsExistActivityPartsByName),
	    HLL_EXPORT(IsExistActivityPartsByNumber, PE_Act_IsExistActivityPartsByNumber),
	    HLL_EXPORT(GetActivityParts, PE_Act_GetActivityParts),
	    HLL_EXPORT(GetActivityPartsNumber, PE_Act_GetActivityPartsNumber),
	    HLL_EXPORT(GetActivityPartsName, PE_Act_GetActivityPartsName),
	    HLL_EXPORT(GetActivityEXID, PE_Act_GetActivityEXID),
	    HLL_EXPORT(SetActivityEXText, PE_Act_SetActivityEXText),
	    HLL_EXPORT(GetActivityEXText, PE_Act_GetActivityEXText),
	    HLL_EXPORT(SetActivityBG, PE_Act_SetActivityBG),
	    HLL_EXPORT(GetActivityBG, PE_Act_GetActivityBG),
	    HLL_EXPORT(AddActivityCloseParts, PE_Act_AddActivityCloseParts),
	    HLL_EXPORT(IsExistActivityCloseParts, PE_Act_IsExistActivityCloseParts),
	    HLL_EXPORT(AddActivityLockedParts, PE_Act_AddActivityLockedParts),
	    HLL_EXPORT(IsExistActivityLockedParts, PE_Act_IsExistActivityLockedParts),
	    HLL_EXPORT(SetActivityEndKey, PE_Act_SetActivityEndKey),
	    HLL_EXPORT(EraseActivityEndKey, PE_Act_EraseActivityEndKey),
	    HLL_EXPORT(IsExistActivityEndKey, PE_Act_IsExistActivityEndKey),
	    HLL_EXPORT(NumofActivityEndKey, PE_Act_NumofActivityEndKey),
	    HLL_EXPORT(GetActivityEndKey, PE_Act_GetActivityEndKey),
	    HLL_EXPORT(SetFlatFPSRate, PE_SetFlatFPSRate),
	    HLL_EXPORT(GetFlatFPS, PE_GetFlatFPS),
	    HLL_EXPORT(Parts_GetFlatFrame, PE_Parts_GetFlatFrame),
	    /* 战斗演出读绝对坐标/尺寸；`Absolute` 走 global，别用 local 的
	     * `PE_GetPartsX/Y/Z` 顶替（会整体偏移）。 */
	    HLL_EXPORT(GetComponentAbsolutePosX, PE_GetPartsAbsoluteX),
	    HLL_EXPORT(GetComponentAbsolutePosY, PE_GetPartsAbsoluteY),
	    HLL_EXPORT(GetComponentAbsolutePosZ, PE_GetPartsAbsoluteZ),
	    HLL_EXPORT(GetComponentWidth, PE_GetPartsWidth),
	    HLL_EXPORT(GetComponentHeight, PE_GetPartsHeight),
	    HLL_EXPORT(Parts_GetHGaugeNumerator, PE_GetPartsHGaugeNumerator),
	    HLL_EXPORT(Parts_GetHGaugeDenominator, PE_GetPartsHGaugeDenominator),
	    HLL_EXPORT(Parts_GetVGaugeNumerator, PE_GetPartsVGaugeNumerator),
	    HLL_EXPORT(Parts_GetVGaugeDenominator, PE_GetPartsVGaugeDenominator)
	    );

static struct ain_hll_function *get_fun(int libno, const char *name)
{
	int fno = ain_get_library_function(ain, libno, name);
	return fno >= 0 ? &ain->libraries[libno].functions[fno] : NULL;
}

static void PartsEngine_PreLink(void)
{
	struct ain_hll_function *fun;
	int libno = ain_get_library(ain, "PartsEngine");
	assert(libno >= 0);

	fun = get_fun(libno, "AddDrawCutCGToPartsConstructionProcess");
	if (fun && fun->nr_arguments == 12) {
		static_library_replace(&lib_PartsEngine, "AddDrawCutCGToPartsConstructionProcess",
				PE_AddDrawCutCGToPartsConstructionProcess);
	}
	fun = get_fun(libno, "AddCopyCutCGToPartsConstructionProcess");
	if (fun && fun->nr_arguments == 12) {
		static_library_replace(&lib_PartsEngine, "AddCopyCutCGToPartsConstructionProcess",
				PE_AddCopyCutCGToPartsConstructionProcess);
	}
	fun = get_fun(libno, "Update");
	if (fun && fun->nr_arguments == 5) {
		static_library_replace(&lib_PartsEngine, "Update",
				PartsEngine_Update_Pascha3PC);
	}
	if (get_fun(libno, "AddController")) {
		PE_enable_multi_controller();
	}
	parts_func_uses_rance9_ids = ain_get_function(ain, "PARTS_GetLayerIndex") < 0;
}
