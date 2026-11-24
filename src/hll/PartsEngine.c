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
#include "parts/parts_internal.h"   /* [D396] struct parts_widget */
#include "audio.h"                   /* [D498] wav_stop_all */
#include "asset_manager.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "movie.h"                  /* [D497] PartsMovie 族 */
#include "sact.h"                   /* [D497] 部件 movie 精灵按帧重绘 */
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
	/* [D496] cmd=19 SetCutCGAlphaBlend。判据：FUNC 60(SetCutCGBlend) 与
	 * FUNC 74(SetCutCGAlphaBlend) 反汇编逐指令对照 —— 成员写入序列完全
	 * 一致（m0=命令号、strings[1]=CG 名、m6/m7/m2/m3/m4/m5/m10/m11/m1
	 * 全部同 local 同序），唯一差异是写进 m0 的命令常量 11 vs 19。
	 * 所以字段布局与 cmd 11 相同，执行语义取「带 alpha 混合的等比
	 * cut-CG 绘制」= 与 case 11 同实现。11/19 在原版引擎若还有别的
	 * 差别，判据只能来自跑测对照（贴图错色/透明度不对时再查）。
	 * 实测 323 次（旧日志 270+53），全部来自战斗卡片绘制路径。 */
	case 19:  // CASConstructionProcess::SetCutCGAlphaBlend (equal scale)
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, dw, dh,
				interp_type, state);
		break;
	/* [D497] cmd=20 SetCutCGScaleAlphaBlend。判据与 19 同法：FUNC 62
	 * (SetCutCGScaleBlend) vs FUNC 75 逐指令 diff，成员写入完全一致、
	 * 唯一差异是常量 13 vs 20 → 与 case 13 同实现（缩放 cut-CG 混合）。
	 * 21/22（OnlyAlpha 族）同法验证字段也一致，但 OnlyAlpha 的渲染语义
	 * （只取 alpha 通道做遮罩）与混合绘制不同，无渲染机制证据，不猜。 */
	case 20:  // CASConstructionProcess::SetCutCGScaleAlphaBlend
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, dw, dh, sx, sy, sw, sh,
				interp_type, state);
		break;
	/* [D184] cmd=29 是 Rance10 的「按 CG 原尺寸绘制」：`d=dx,dy,0,0` 且带
	 * CG 名（`カード／ランス／基本` 等），尺寸在 CG 本身。实测 171 次，全部
	 * 落在卡片部件上——卡片立绘不出的直接原因就是它被当 unknown 丢掉。
	 * 传 dw/dh=0，由 `build_draw_cut_cg` 回退到 texture 尺寸。 */
	/* [D436] 19..28 的编号是扫出来的，不是猜的：对每个
	 * `CASConstructionProcess@SetXxx`(fno 48..77) 反汇编，取它写进
	 * `ints[2]` 的那个常量，得到完整映射
	 *   19 CutCGAlphaBlend   20 CutCGScaleAlphaBlend
	 *   21 CutCGOnlyAlpha    22 CutCGScaleOnlyAlpha
	 *   23 AlphaBlendText    24 OnlyAlphaText
	 *   25 MulAMapGradationHorizon  26 MulAMapGradationVertical
	 *   27 HBlurFilter        28 VBlurFilter
	 * 其中 23 是战斗里每次点技能都刷 `unknown command 23` 的那个。
	 *
	 * `SetAlphaBlendText` 语义上就是「混合模式画字」，与 case 7 的
	 * `SetDrawText` 同实现（`build_draw_text` 用 `gfx_render_text(…, true)`
	 * 混合；`SetCopyText` 才是覆盖）。所以直接复用。 */
	case 23:  // CASConstructionProcess::SetAlphaBlendText
		PE_AddDrawTextToPartsConstructionProcess(parts_no,
				dx, dy, text, font_type, font_size,
				font_r, font_g, font_b, bold_weight,
				edge_r, edge_g, edge_b, edge_weight,
				char_space, line_space, state);
		break;
	case 29:  // SetDrawCG (natural size at dx,dy)
		PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name,
				dx, dy, 0, 0, 0, 0, 0, 0, interp_type, state);
		break;
	default: {
		/* [D436] 带上名字再报。只报数字时，「unknown command 23」既不能
		 * 搜、也不能判断该不该实现 —— 光为了解这一个数字就得再扫一遍
		 * 全部 setter 的常量。 */
		static const char *const cproc_names[30] = {
			"SetCreate", "SetCreatePixelOnly", "SetCreateCG", "SetFill",
			"SetFillAlphaColor", "SetFillAMap", "SetFillWithAlpha",
			"SetDrawText", "SetCopyText", "SetFillGradationHorizon",
			"SetDrawRect", "SetCutCGBlend", "SetCutCGCopy",
			"SetCutCGScaleBlend", "SetCutCGScaleCopy", "SetGrayFilter",
			"SetAddFilter", "SetMulFilter", "SetDrawLine",
			"SetCutCGAlphaBlend", "SetCutCGScaleAlphaBlend",
			"SetCutCGOnlyAlpha", "SetCutCGScaleOnlyAlpha",
			"SetAlphaBlendText", "SetOnlyAlphaText",
			"SetMulAMapGradationHorizon", "SetMulAMapGradationVertical",
			"SetHBlurFilter", "SetVBlurFilter", "SetCGBlend",
		};
		WARNING("AddConstructProcess: unknown command %d (%s)", command,
		        (command >= 0 && command < 30) ? cproc_names[command]
		                                       : "?");
		break;
	}
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

/* [D498] 停掉 PartsEngine 管的音效且**不播**「系统声」。
 * 语义出处：设置界面打开时 AIN 调它（实测 cf 系列每次都打 Unimplemented）。
 * 原来是 warn 桩，UI 音效在设置打开后继续响。 */
static void PartsEngine_StopSoundWithoutSystemSound(void)
{
	int stopped = wav_stop_all();
	if (getenv("XSYS4_FLAT_DBG"))
		NOTICE("[snd] StopSoundWithoutSystemSound: stopped %d", stopped);
}

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
 * [D496] 离线 xref 把契约钉到了"AIN 侧只写不读"：
 * `parts::detail::SetEnableInputProcess(8255)` 的静态调用点只有 3 处，
 * 全是 Parts 类方法转发（7124 / 12792 一族），而这些转发自身**没有任何
 * 静态调用点**（虚表派发）；`parts::IsInputPartsActive(7243)`（AIN 侧的
 * 输入组件过滤）按类型 {4,7,11} 判定、根本不读这个 flag。⇒ 开关的消费
 * 方在**原版引擎的 C++ 输入处理器**里，AIN 只是转存。
 *
 * 修复方向（[D496] 已实现）：flag 存 `parts->enable_input_process`
 * （默认 true——AIN 初始化时统一先关），input.c 对**组件类型显式
 * ∈ {4,7,11}** 的部件门控 hover/click——输入组件族之外一律不碰，
 * clickable 语义原样保留。`XSYS4_EIP_GATE=0` 回退。 */
static void PartsEngine_SetEnableInputProcess(int parts_no, int enable)
{
	PE_set_enable_input_process(parts_no, enable != 0);
}

/* [D496] `bool IsEnableInputProcess(int parts_no)`（dump-hll：
 * args=1 / ret bool）。AIN 侧同样只是类方法转发，配对 getter
 * 原样回读；部件不存在时返回 true（与"新建即开"默认一致）。 */
static bool PartsEngine_IsEnableInputProcess(int parts_no)
{
	return PE_get_enable_input_process(parts_no);
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

/* [D494] Parts_SetSoundName / Parts_GetSoundName：转发 parts.c 的
 * parts_widget 音效名存储（见那里 [D494] 的签名判据与槽号语义）。 */
static void PE_Parts_SetSoundName_fwd(int no, struct string *name, int slot)
{
	PE_Parts_SetSoundName(no, name, slot);
}

static struct string *PE_Parts_GetSoundName_hll(int no, int slot)
{
	return PE_Parts_GetSoundName(no, slot);
}

/* [D494] `int PartsEngine.GetSystemOverlayController()`（dump-hll：
 * args=0 / ret int）。AIN 包装 8297 是纯转发，语义与 PartsFunc
 * GetSystemOverlayLayer（case 2）相同：返回系统 overlay 层的稳定 ID。 */
static int PE_GetSystemOverlayController(void)
{
	return PE_get_system_controller();
}

/* [D494] `void PartsEngine.CancelFormClose(int parts)`（dump-hll：
 * args=1 / ret void）。AIN 调用点在 `parts::detail::CPartsFunctionSet`
 * 的类型 SWITCH 里，只对部件类型 2（FORM）触发。本引擎没有实现 FORM
 * 关闭 machinery（SetFormCaptionColor 一族同为 [D85] 占位），"取消一次
 * 已排定的表单关闭"在此前提下是正确的空操作；XSYS4_PE_DBG=1 时留触达痕。 */
static void PE_CancelFormClose(int parts)
{
	(void)parts;
	PE_HACK_HIT("CancelFormClose");
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

/* [D494] 全局点击 miss 音（SetClickMissSoundName/GetClickMissSoundName），
 * 转发 parts.c 的全局存储。getter 在 AIN 里静态 0 调用（ain_xref hlltop），
 * setter 1 个调用点；补 getter 只为消掉链接期告警。 */
static void PE_SetClickMissSoundName_fwd(struct string * a0)
{
	PE_SetClickMissSoundName(a0);
}

static struct string *PE_GetClickMissSoundName_fwd(void)
{
	return PE_GetClickMissSoundName();
}

/* [D494] Parts_PlaySound：按槽号播部件音效名，转发 parts.c（live 链
 * 与槽号语义见那里）。 */
static void PE_Parts_PlaySound_fwd(int no, int state)
{
	PE_Parts_PlaySound(no, state);
}

/* [D495] flat 音效变更表（FlatSound 家族），转发 parts.c。证据链：
 * AIN 包装 Ｐ＿フラットＣＧ変更系（fno=7510..7514）→ 本族 HLL 755..760；
 * 但包装本身 0 静态调用者（ain_xref call 9586..9590 实测）＝死链保真，
 * 实现按 parts_internal.h [D495] 注释的语义走。 */
static int PE_NumofFlatSound_fwd(int no, int state)
{
	return PE_NumofFlatSound(no, state);
}

static struct string *PE_GetFlatSoundName_fwd(int no, int index, int state)
{
	return PE_GetFlatSoundName(no, index, state);
}

static void PE_Parts_ChangeFlatSound_fwd(int no, struct string *old_name,
		struct string *new_name, int state)
{
	PE_Parts_ChangeFlatSound(no, old_name, new_name, state);
}

static struct string *PE_GetProxyFlatSound_fwd(int no, struct string *old_name,
		int state)
{
	return PE_GetProxyFlatSound(no, old_name, state);
}

static void PE_ReturnChangeFlatSound_fwd(int no, struct string *old_name,
		int state)
{
	PE_ReturnChangeFlatSound(no, old_name, state);
}

static void PE_ClearChangeFlatSound_fwd(int no, int state)
{
	PE_ClearChangeFlatSound(no, state);
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

/* [D496] 按键等待指示（キー待ち表示）一族：状态存 parts_widget
 * （见那里 [D496] 注释的调用方证据）。运行期只有 SetKeyWaitShow 被调
 * （693 次/章），CG/Flat/Pos 三个配置 setter 0 次命中——但照实存，
 * getter 读回；渲染挂钩待引擎默认图形证据（素材/闪烁节奏不可知，
 * 不猜）。 */
static void PE_SetKeyWaitCGName(int no, struct string *name, int start_no,
		int numof_cg, int time_per_cg)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	if (w->keywait_cg)
		free_string(w->keywait_cg);
	w->keywait_cg = name ? string_dup(name) : NULL;
	w->keywait_cg_start_no = start_no;
	w->keywait_cg_numof = numof_cg;
	w->keywait_cg_time = time_per_cg;
}

static void PE_SetKeyWaitFlatName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	if (w->keywait_flat)
		free_string(w->keywait_flat);
	w->keywait_flat = name ? string_dup(name) : NULL;
}

static void PE_SetKeyWaitPos(int no, int x, int y, int z)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	w->keywait_x = x;
	w->keywait_y = y;
	w->keywait_z = z;
}

static void PE_SetKeyWaitShow(int no, int show)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	w->keywait_show = show != 0;
}

/* AIN SetLayoutBoxReturnSizeForRate：void SetLayoutBoxReturnSizeForRate。
 * [D517l] 仍是占位：AIN 有调用点（0x1d9d06）但运行期 0 命中（d662 全量
 * trace 实证），语义 = wrap_size 按比率设置，比率基数（盒宽？父宽？）
 * 未考证 —— 盲目实现有引入错误行为的风险，等真实命中再考。 */
static void PE_Noop_SetLayoutBoxReturnSizeForRate(int a0, int a1)
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


/* [D396] 控件字符串槽的读写（定义在下方 TextBox 段）。 */
static void widget_set_string(struct string **slot, struct string *v);
static struct string *widget_get_string(struct string **slot);

/* ===== [D396] 控件：多行文本输入框（组件类型 7） =====
 *
 * 单行文本框的超集：多出「纵向/横向滚动条开关」与「四边内边距」。
 * 文本、长度上限、只读、四组颜色、字体、字距、行距与 TextBox 同义，
 * 直接复用 `parts_widget` 的同一批字段 —— 它们是同一个部件的不同形态，
 * AIN 也不会同时按两种控件访问同一个部件。
 */
static void PE_SetMultiTextBoxSize(int no, int w, int h)
{
	PE_SetPartsSize(no, w, h, 1);
}

static void PE_SetMultiTextBoxFontProperty(int no, int type, int size, int r,
		int g, int b, float bold_weight, int edge_r, int edge_g, int edge_b,
		float edge_weight)
{
	PE_SetPartsFontType(no, type, 1);
	PE_SetPartsFontSize(no, size, 1);
	PE_SetPartsFontColor(no, r, g, b, 1);
	PE_SetPartsFontBoldWeight(no, bold_weight, 1);
	PE_SetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_SetPartsFontEdgeWeight(no, edge_weight, 1);
}
static void PE_GetMultiTextBoxFontProperty(int no, int *type, int *size, int *r,
		int *g, int *b, float *bold_weight, int *edge_r, int *edge_g,
		int *edge_b, float *edge_weight)
{
	PE_GetPartsFontType(no, type, 1);
	PE_GetPartsFontSize(no, size, 1);
	PE_GetPartsFontColor(no, r, g, b, 1);
	PE_GetPartsFontBoldWeight(no, bold_weight, 1);
	PE_GetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_GetPartsFontEdgeWeight(no, edge_weight, 1);
}

static void PE_SetMultiTextBoxReadOnly(int no, bool ro)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->readonly = ro;
}
static bool PE_IsMultiTextBoxReadOnly(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->readonly : false;
}

static void PE_SetMultiTextBoxText(int no, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->text, text);
}
static struct string *PE_GetMultiTextBoxText(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->text) : string_dup(&EMPTY_STRING);
}

static void PE_SetMultiTextBoxMaxTextLength(int no, int len)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->max_text_length = len;
}
static int PE_GetMultiTextBoxMaxTextLength(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->max_text_length : 0;
}

static void PE_SetMultiTextBoxBGColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->bg_r = r; w->bg_g = g; w->bg_b = b; w->bg_set = true; }
}
static int PE_GetMultiTextBoxBGR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_r : 255;
}
static int PE_GetMultiTextBoxBGG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_g : 255;
}
static int PE_GetMultiTextBoxBGB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_b : 255;
}

static void PE_SetMultiTextBoxReadOnlyBGColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->ro_bg_r = r; w->ro_bg_g = g; w->ro_bg_b = b; w->ro_bg_set = true; }
}
static int PE_GetMultiTextBoxReadOnlyBGR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->ro_bg_set) ? w->ro_bg_r : 192;
}
static int PE_GetMultiTextBoxReadOnlyBGG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->ro_bg_set) ? w->ro_bg_g : 192;
}
static int PE_GetMultiTextBoxReadOnlyBGB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->ro_bg_set) ? w->ro_bg_b : 192;
}

static void PE_SetMultiTextBoxFrameColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->frame_r = r; w->frame_g = g; w->frame_b = b; w->frame_set = true; }
}
static int PE_GetMultiTextBoxFrameR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->frame_set) ? w->frame_r : 0;
}
static int PE_GetMultiTextBoxFrameG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->frame_set) ? w->frame_g : 0;
}
static int PE_GetMultiTextBoxFrameB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->frame_set) ? w->frame_b : 0;
}

static void PE_SetMultiTextBoxSelectColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->select_r = r; w->select_g = g; w->select_b = b; w->select_set = true; }
}
static int PE_GetMultiTextBoxSelectR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->select_set) ? w->select_r : 0;
}
static int PE_GetMultiTextBoxSelectG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->select_set) ? w->select_g : 0;
}
static int PE_GetMultiTextBoxSelectB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->select_set) ? w->select_b : 0;
}

static void PE_SetMultiTextBoxVScrollShow(int no, bool show)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->vscroll_show = show;
}
static void PE_SetMultiTextBoxHScrollShow(int no, bool show)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->hscroll_show = show;
}
static bool PE_IsMultiTextBoxVScrollShow(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->vscroll_show : false;
}
static bool PE_IsMultiTextBoxHScrollShow(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->hscroll_show : false;
}

static void PE_SetMultiTextBoxCGName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->cg_name, name);
}
static struct string *PE_GetMultiTextBoxCGName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->cg_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetMultiTextBoxCharSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->char_space = space;
}
static int PE_GetMultiTextBoxCharSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->char_space : 0;
}

static void PE_SetMultiTextBoxLineSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->line_space = space;
}
static int PE_GetMultiTextBoxLineSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->line_space : 0;
}

static void PE_SetMultiTextBoxPadding(int no, int top, int bottom, int left,
		int right)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	w->pad_top = top;
	w->pad_bottom = bottom;
	w->pad_left = left;
	w->pad_right = right;
}
static int PE_GetMultiTextBoxPaddingTop(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->pad_top : 0;
}
static int PE_GetMultiTextBoxPaddingBottom(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->pad_bottom : 0;
}
static int PE_GetMultiTextBoxPaddingLeft(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->pad_left : 0;
}
static int PE_GetMultiTextBoxPaddingRight(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->pad_right : 0;
}

/* ===== [D396] 控件：滚动条（组件类型 2 纵向 / 3 横向） =====
 *
 * 纵向与横向的接口**完全对称**（Up/Down ↔ Left/Right），所以字段共用一份、
 * 函数体用宏生成两套 —— 手抄 70 个几乎相同的函数只会引入拼写错误。
 *
 * 类型依据（AIN 包装类的默认返回字节码）：
 *   `Get*ScrollPos`  → `PUSH 0`   ⇒ int（像素位置）
 *   `Get*ScrollRate` → `F_PUSH 0` ⇒ float（0..1 比率）
 */
#define DEFINE_SCROLLBAR(PREFIX, HEAD, TAIL, UNITS) \
static void PE_Set##PREFIX##Size(int no, int w, int h) \
{ \
	PE_SetPartsSize(no, w, h, 1); \
} \
static void PE_Set##PREFIX##HEAD##UNITS(int no, int v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		w->scroll.head_size = v; \
} \
static void PE_Set##PREFIX##TAIL##UNITS(int no, int v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		w->scroll.tail_size = v; \
} \
static int PE_Get##PREFIX##HEAD##UNITS(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? w->scroll.head_size : 0; \
} \
static int PE_Get##PREFIX##TAIL##UNITS(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? w->scroll.tail_size : 0; \
} \
static void PE_Set##PREFIX##TotalSize(int no, int v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		w->scroll.total_size = v; \
} \
static void PE_Set##PREFIX##ViewSize(int no, int v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		w->scroll.view_size = v; \
} \
static void PE_Set##PREFIX##ScrollPos(int no, int v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		w->scroll.scroll_pos = v; \
} \
static void PE_Set##PREFIX##ScrollRate(int no, float v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	/* [D500] 用户正在值条上按下/拖动时，BeginUpdateEvent 的「config 值→
	 * 部件 rate」同步不得覆盖点击换算出的 rate —— 否则死锁在旧值。 */ \
	if (w && !w->scroll.user_active) \
		w->scroll.scroll_rate = v; \
} \
static void PE_Set##PREFIX##MoveSizeByButton(int no, int v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		w->scroll.move_size_by_button = v; \
} \
static int PE_Get##PREFIX##TotalSize(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? w->scroll.total_size : 0; \
} \
static int PE_Get##PREFIX##ViewSize(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? w->scroll.view_size : 0; \
} \
static int PE_Get##PREFIX##ScrollPos(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? w->scroll.scroll_pos : 0; \
} \
static float PE_Get##PREFIX##ScrollRate(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? w->scroll.scroll_rate : 0.0f; \
} \
static int PE_Get##PREFIX##MoveSizeByButton(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? w->scroll.move_size_by_button : 0; \
} \
static void PE_Set##PREFIX##CGName(int no, struct string *v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		widget_set_string(&w->scroll.cg_name, v); \
} \
static struct string *PE_Get##PREFIX##CGName(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? widget_get_string(&w->scroll.cg_name) : string_dup(&EMPTY_STRING); \
} \
static void PE_Set##PREFIX##FlatName(int no, struct string *v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		widget_set_string(&w->scroll.flat_name, v); \
} \
static struct string *PE_Get##PREFIX##FlatName(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? widget_get_string(&w->scroll.flat_name) : string_dup(&EMPTY_STRING); \
} \
static void PE_Set##PREFIX##Override##HEAD##CGName(int no, struct string *v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		widget_set_string(&w->scroll.ov_head_cg, v); \
} \
static struct string *PE_Get##PREFIX##Override##HEAD##CGName(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? widget_get_string(&w->scroll.ov_head_cg) : string_dup(&EMPTY_STRING); \
} \
static void PE_Set##PREFIX##Override##TAIL##CGName(int no, struct string *v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		widget_set_string(&w->scroll.ov_tail_cg, v); \
} \
static struct string *PE_Get##PREFIX##Override##TAIL##CGName(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? widget_get_string(&w->scroll.ov_tail_cg) : string_dup(&EMPTY_STRING); \
} \
static void PE_Set##PREFIX##OverrideBarCGName(int no, struct string *v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		widget_set_string(&w->scroll.ov_bar_cg, v); \
} \
static struct string *PE_Get##PREFIX##OverrideBarCGName(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? widget_get_string(&w->scroll.ov_bar_cg) : string_dup(&EMPTY_STRING); \
} \
static void PE_Set##PREFIX##OverrideBGCGName(int no, struct string *v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		widget_set_string(&w->scroll.ov_bg_cg, v); \
} \
static struct string *PE_Get##PREFIX##OverrideBGCGName(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? widget_get_string(&w->scroll.ov_bg_cg) : string_dup(&EMPTY_STRING); \
} \
static void PE_Set##PREFIX##Override##HEAD##FlatName(int no, struct string *v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		widget_set_string(&w->scroll.ov_head_flat, v); \
} \
static struct string *PE_Get##PREFIX##Override##HEAD##FlatName(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? widget_get_string(&w->scroll.ov_head_flat) : string_dup(&EMPTY_STRING); \
} \
static void PE_Set##PREFIX##Override##TAIL##FlatName(int no, struct string *v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		widget_set_string(&w->scroll.ov_tail_flat, v); \
} \
static struct string *PE_Get##PREFIX##Override##TAIL##FlatName(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? widget_get_string(&w->scroll.ov_tail_flat) : string_dup(&EMPTY_STRING); \
} \
static void PE_Set##PREFIX##OverrideBarFlatName(int no, struct string *v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		widget_set_string(&w->scroll.ov_bar_flat, v); \
} \
static struct string *PE_Get##PREFIX##OverrideBarFlatName(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? widget_get_string(&w->scroll.ov_bar_flat) : string_dup(&EMPTY_STRING); \
} \
static void PE_Set##PREFIX##OverrideBGFlatName(int no, struct string *v) \
{ \
	struct parts_widget *w = PE_widget(no); \
	if (w) \
		widget_set_string(&w->scroll.ov_bg_flat, v); \
} \
static struct string *PE_Get##PREFIX##OverrideBGFlatName(int no) \
{ \
	struct parts_widget *w = PE_widget_peek(no); \
	return w ? widget_get_string(&w->scroll.ov_bg_flat) : string_dup(&EMPTY_STRING); \
}

#define DEFINE_SCROLLBAR_SOUND(PREFIX) \
static void PE_Set##PREFIX##OnCursorSoundNumber(int no, int n) \
{ \
	PE_SetPartsOnCursorSoundNumber(no, n); \
} \
static void PE_Set##PREFIX##ClickSoundNumber(int no, int n) \
{ \
	PE_SetPartsClickSoundNumber(no, n); \
} \
static int PE_Get##PREFIX##OnCursorSoundNumber(int no) \
{ \
	return PE_GetPartsOnCursorSoundNumber(no); \
} \
static int PE_Get##PREFIX##ClickSoundNumber(int no) \
{ \
	return PE_GetPartsClickSoundNumber(no); \
}

DEFINE_SCROLLBAR_SOUND(VScrollbar)
DEFINE_SCROLLBAR_SOUND(HScrollbar)

DEFINE_SCROLLBAR(VScrollbar, Up, Down, Height)
DEFINE_SCROLLBAR(HScrollbar, Left, Right, Width)


/* AIN SetMessageWindowActive：void SetMessageWindowActive。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_SetMessageWindowActive(int no, bool active)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->msg_active = active;
	MSGWIN_HIT("SetMessageWindowActive no=%d active=%d", no, active);
}

/* AIN SetMessageWindowCGName：void SetMessageWindowCGName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_SetMessageWindowCGName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->cg_name, name);
	MSGWIN_HIT("SetMessageWindowCGName no=%d len=%d", no,
		   name ? (int)strlen(name->text) : -1);
}

/* AIN SetMessageWindowFlatName：void SetMessageWindowFlatName。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_SetMessageWindowFlatName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->flat_name, name);
	MSGWIN_HIT("SetMessageWindowFlatName no=%d len=%d", no,
		   name ? (int)strlen(name->text) : -1);
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

/* ===== [D396] 控件：单行文本输入框（组件类型 4） =====
 *
 * 语义（由成对的 Set/Get 签名确定，arity 来自 `ain_hllarity`）：
 *   SetTextBoxText(no, text)          / GetTextBoxText(no) -> string
 *   SetTextBoxMaxTextLength(no, len)  / GetTextBoxMaxTextLength(no) -> int
 *   SetTextBoxReadOnly(no, bool)      / IsTextBoxReadOnly(no) -> bool
 *   SetTextBoxCGName(no, name)        / GetTextBoxCGName(no) -> string
 *   SetTextBoxCharSpace(no, n)        / GetTextBoxCharSpace(no) -> int
 *   SetTextBox{BG,ReadOnlyBG,Frame,Select}Color(no,r,g,b)
 *                                     / GetTextBox{...}R/G/B(no) -> int ×3
 *   SetTextBoxFontProperty(no, ...10) / GetTextBoxFontProperty(no, ...10)
 *   SetTextBoxSize(no, w, h)
 *
 * 这些是**控件自身的属性**，不是 EX 内容：EX 只给位置和尺寸，属性由 AIN
 * 在运行期设进来。AIN 在存档前后会把它们逐个读出来再写回去，所以 getter
 * 必须原样返回 setter 写进去的值。
 *
 * 状态存在 `struct parts_widget`（挂在该部件上，随部件释放），
 * 见 `parts_internal.h` 的说明。没设过时按 EX/主题默认值返回，
 * 并用 `*_set` 区分「没设过」与「设成 0」。
 */
static void widget_set_string(struct string **slot, struct string *v)
{
	if (*slot)
		free_string(*slot);
	*slot = v ? string_ref(v) : NULL;
}

static struct string *widget_get_string(struct string **slot)
{
	return *slot ? string_ref(*slot) : string_dup(&EMPTY_STRING);
}

static void PE_SetTextBoxSize(int no, int w, int h)
{
	PE_SetPartsSize(no, w, h, 1);
}

static void PE_SetTextBoxText(int no, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->text, text);
}

static struct string *PE_GetTextBoxText(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->text) : string_dup(&EMPTY_STRING);
}

static void PE_SetTextBoxMaxTextLength(int no, int len)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->max_text_length = len;
}

static int PE_GetTextBoxMaxTextLength(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->max_text_length : 0;
}

static void PE_SetTextBoxReadOnly(int no, bool ro)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->readonly = ro;
}

static bool PE_IsTextBoxReadOnly(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->readonly : false;
}

static void PE_SetTextBoxCGName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->cg_name, name);
}

static struct string *PE_GetTextBoxCGName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->cg_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetTextBoxCharSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->char_space = space;
}

static int PE_GetTextBoxCharSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->char_space : 0;
}

/* 四组颜色。TextBox 的四组（BG / ReadOnlyBG / Frame / Select）
 * 是同一形状，只有存的字段不同 —— 逐个写清楚，而不是用一个宏藏起来。 */
static void PE_SetTextBoxBGColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->bg_r = r; w->bg_g = g; w->bg_b = b; w->bg_set = true; }
}
static int PE_GetTextBoxBGR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_r : 255;
}
static int PE_GetTextBoxBGG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_g : 255;
}
static int PE_GetTextBoxBGB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_b : 255;
}

static void PE_SetTextBoxReadOnlyBGColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->ro_bg_r = r; w->ro_bg_g = g; w->ro_bg_b = b; w->ro_bg_set = true; }
}
static int PE_GetTextBoxReadOnlyBGR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->ro_bg_set) ? w->ro_bg_r : 192;
}
static int PE_GetTextBoxReadOnlyBGG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->ro_bg_set) ? w->ro_bg_g : 192;
}
static int PE_GetTextBoxReadOnlyBGB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->ro_bg_set) ? w->ro_bg_b : 192;
}

static void PE_SetTextBoxFrameColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->frame_r = r; w->frame_g = g; w->frame_b = b; w->frame_set = true; }
}
static int PE_GetTextBoxFrameR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->frame_set) ? w->frame_r : 0;
}
static int PE_GetTextBoxFrameG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->frame_set) ? w->frame_g : 0;
}
static int PE_GetTextBoxFrameB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->frame_set) ? w->frame_b : 0;
}

static void PE_SetTextBoxSelectColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->select_r = r; w->select_g = g; w->select_b = b; w->select_set = true; }
}
static int PE_GetTextBoxSelectR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->select_set) ? w->select_r : 0;
}
static int PE_GetTextBoxSelectG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->select_set) ? w->select_g : 0;
}
static int PE_GetTextBoxSelectB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->select_set) ? w->select_b : 0;
}

/* 字体：转发已有的 `PE_SetPartsFont*` / 新增的 `PE_GetPartsFont*`。
 * 11 个实参里 float 只有 bold_weight 和 edge_weight（与
 * `SetMessageWindowTextFont` 的签名一致）。 */
static void PE_SetTextBoxFontProperty(int no, int type, int size, int r, int g,
		int b, float bold_weight, int edge_r, int edge_g, int edge_b,
		float edge_weight)
{
	PE_SetPartsFontType(no, type, 1);
	PE_SetPartsFontSize(no, size, 1);
	PE_SetPartsFontColor(no, r, g, b, 1);
	PE_SetPartsFontBoldWeight(no, bold_weight, 1);
	PE_SetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_SetPartsFontEdgeWeight(no, edge_weight, 1);
}

static void PE_GetTextBoxFontProperty(int no, int *type, int *size, int *r,
		int *g, int *b, float *bold_weight, int *edge_r, int *edge_g,
		int *edge_b, float *edge_weight)
{
	PE_GetPartsFontType(no, type, 1);
	PE_GetPartsFontSize(no, size, 1);
	PE_GetPartsFontColor(no, r, g, b, 1);
	PE_GetPartsFontBoldWeight(no, bold_weight, 1);
	PE_GetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_GetPartsFontEdgeWeight(no, edge_weight, 1);
}

/* ===== [D396] 控件：列表框（组件类型 5） =====
 *
 * 语义（arity 来自 `ain_hllarity`，索引基数见下）：
 *   AddListBoxItem(no, text)            追加一项
 *   InsertListBoxItem(no, index, text)  在第 index 项**之前**插入
 *   SetListBoxItem(no, index, text)     改写第 index 项
 *   GetListBoxItem(no, index) -> string 取第 index 项
 *   EraseListBoxItem(no, index)         删第 index 项
 *   ClearListBoxItem(no)                清空
 *   GetListBoxItemCount(no) -> int      项数
 *   SetListBoxSelectIndex(no, index) / GetListBoxSelectIndex(no)
 *   GetListBoxOnCursorItemIndex(no) / GetListBoxOnCursorItem(no)
 *   SetListBox{Size,Margin,LineHeight,ScrollPos,CGName,CharSpace,FontProperty}
 *
 * **索引基数：0-based。** 依据（不是猜）：
 * ① System4 的数组与列表一律 0-based，`GetListBoxItemCount()` 的唯一
 *    自洽配法是 `for (i = 0; i < count; i++)`；
 * ② 反汇编 `fno=7683`（`列表框にテキストリストを追記`）里遍历源数组
 *    用的是 `Index = 0; while (Index < Count) { …; Index++ }`；
 * ③ `SelectIndex` 用 `-1` 表示「没有选中项」—— 只有 0-based 才需要 -1
 *    （1-based 的「无选中」会用 0）。
 * 越界访问一律安全返回：`GetListBoxItem` 返回空串，`Set/Erase` 忽略。
 *
 * `on_cursor_index` 是**引擎侧**状态（鼠标停在第几项），AIN 从不设；
 * 由 parts 的 hover 判定更新（尚未接），默认 -1。
 */
static void lb_ensure(struct parts_widget *w, int need)
{
	if (need <= w->cap_items)
		return;
	int cap = w->cap_items ? w->cap_items : 8;
	while (cap < need)
		cap *= 2;
	w->items = xrealloc(w->items, cap * sizeof(*w->items));
	memset(w->items + w->cap_items, 0, (cap - w->cap_items) * sizeof(*w->items));
	w->cap_items = cap;
}

static void PE_AddListBoxItem(int no, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	lb_ensure(w, w->nr_items + 1);
	w->items[w->nr_items++] = text ? string_ref(text) : string_dup(&EMPTY_STRING);
}

static void PE_InsertListBoxItem(int no, int index, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	if (index < 0 || index > w->nr_items)
		index = w->nr_items;
	lb_ensure(w, w->nr_items + 1);
	memmove(&w->items[index + 1], &w->items[index],
		(w->nr_items - index) * sizeof(*w->items));
	w->items[index] = text ? string_ref(text) : string_dup(&EMPTY_STRING);
	w->nr_items++;
}

static void PE_SetListBoxItem(int no, int index, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (!w || index < 0 || index >= w->nr_items)
		return;
	if (w->items[index])
		free_string(w->items[index]);
	w->items[index] = text ? string_ref(text) : string_dup(&EMPTY_STRING);
}

static struct string *PE_GetListBoxItem(int no, int index)
{
	struct parts_widget *w = PE_widget_peek(no);
	if (!w || index < 0 || index >= w->nr_items || !w->items[index])
		return string_dup(&EMPTY_STRING);
	return string_ref(w->items[index]);
}

static int PE_GetListBoxItemCount(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->nr_items : 0;
}

static void PE_EraseListBoxItem(int no, int index)
{
	struct parts_widget *w = PE_widget(no);
	if (!w || index < 0 || index >= w->nr_items)
		return;
	if (w->items[index])
		free_string(w->items[index]);
	memmove(&w->items[index], &w->items[index + 1],
		(w->nr_items - index - 1) * sizeof(*w->items));
	w->nr_items--;
	if (w->select_index == index)
		w->select_index = -1;
	else if (w->select_index > index)
		w->select_index--;
}

static void PE_ClearListBoxItem(int no)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	for (int i = 0; i < w->nr_items; i++)
		if (w->items[i])
			free_string(w->items[i]);
	w->nr_items = 0;
	w->select_index = -1;
}

static void PE_SetListBoxSelectIndex(int no, int index)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->select_index = index;
}

static int PE_GetListBoxSelectIndex(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->select_index : 0;
}

static int PE_GetListBoxOnCursorItemIndex(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->on_cursor_index : -1;
}

static struct string *PE_GetListBoxOnCursorItem(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	if (!w || w->on_cursor_index < 0 || w->on_cursor_index >= w->nr_items ||
	    !w->items[w->on_cursor_index])
		return string_dup(&EMPTY_STRING);
	return string_ref(w->items[w->on_cursor_index]);
}

static void PE_SetListBoxSize(int no, int w, int h)
{
	PE_SetPartsSize(no, w, h, 1);
}

static void PE_SetListBoxLineHeight(int no, int h)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->line_height = h;
}

static int PE_GetListBoxLineHeight(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->line_height : 0;
}

static void PE_SetListBoxMargin(int no, int mw, int mh)
{
	struct parts_widget *w = PE_widget(no);
	if (w) {
		w->margin_w = mw;
		w->margin_h = mh;
	}
}

static int PE_GetListBoxWidthMargin(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->margin_w : 0;
}

static int PE_GetListBoxHeightMargin(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->margin_h : 0;
}

static void PE_SetListBoxScrollPos(int no, int pos)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->scroll_pos = pos;
}

static int PE_GetListBoxScrollPos(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->scroll_pos : 0;
}

static void PE_SetListBoxCGName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->cg_name, name);
}

static struct string *PE_GetListBoxCGName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->cg_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetListBoxCharSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->char_space = space;
}

static int PE_GetListBoxCharSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->char_space : 0;
}

static void PE_SetListBoxFontProperty(int no, int type, int size, int r, int g,
		int b, float bold_weight, int edge_r, int edge_g, int edge_b,
		float edge_weight)
{
	PE_SetPartsFontType(no, type, 1);
	PE_SetPartsFontSize(no, size, 1);
	PE_SetPartsFontColor(no, r, g, b, 1);
	PE_SetPartsFontBoldWeight(no, bold_weight, 1);
	PE_SetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_SetPartsFontEdgeWeight(no, edge_weight, 1);
}

static void PE_GetListBoxFontProperty(int no, int *type, int *size, int *r,
		int *g, int *b, float *bold_weight, int *edge_r, int *edge_g,
		int *edge_b, float *edge_weight)
{
	PE_GetPartsFontType(no, type, 1);
	PE_GetPartsFontSize(no, size, 1);
	PE_GetPartsFontColor(no, r, g, b, 1);
	PE_GetPartsFontBoldWeight(no, bold_weight, 1);
	PE_GetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_GetPartsFontEdgeWeight(no, edge_weight, 1);
}

/* ===== [D396] 控件：组合框（组件类型 6） =====
 *
 * 组合框 = **一个显示框 + 一个可展开的内嵌列表框**。所以它的接口分两层：
 *   `SetComboBoxText` / `GetComboBoxText`     显示框里的文字
 *   `*ComboBoxListBox*`                        内嵌列表框（项、选中、滚动、行高…）
 * 两层各自有字体与字距 —— 这不是重复，是 System4 的原始设计。
 *
 * `IsComboBoxOpenListBox` 是**只读**的展开状态：整张接口表里没有对应的
 * `Set/Open` 函数，说明展开/收起由引擎在点击箭头时自己完成，AIN 只查询。
 * 我们目前不实现展开交互，所以恒 false（如实反映「没展开」）。
 */
static void cb_ensure(struct parts_widget *w, int need)
{
	if (need <= w->cap_cb_items)
		return;
	int cap = w->cap_cb_items ? w->cap_cb_items : 8;
	while (cap < need)
		cap *= 2;
	w->cb_items = xrealloc(w->cb_items, cap * sizeof(*w->cb_items));
	memset(w->cb_items + w->cap_cb_items, 0,
	       (cap - w->cap_cb_items) * sizeof(*w->cb_items));
	w->cap_cb_items = cap;
}

static void PE_SetComboBoxSize(int no, int w, int h)
{
	PE_SetPartsSize(no, w, h, 1);
}

static void PE_SetComboBoxTextMargin(int no, int mw, int mh)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->cb_text_margin_w = mw; w->cb_text_margin_h = mh; }
}
static int PE_GetComboBoxTextWidthMargin(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->cb_text_margin_w : 0;
}
static int PE_GetComboBoxTextHeightMargin(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->cb_text_margin_h : 0;
}

static void PE_SetComboBoxCGName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->cg_name, name);
}
static struct string *PE_GetComboBoxCGName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->cg_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetComboBoxText(int no, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->text, text);
}
static struct string *PE_GetComboBoxText(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->text) : string_dup(&EMPTY_STRING);
}

static void PE_SetComboBoxFontProperty(int no, int type, int size, int r, int g,
		int b, float bold_weight, int edge_r, int edge_g, int edge_b,
		float edge_weight)
{
	PE_SetPartsFontType(no, type, 1);
	PE_SetPartsFontSize(no, size, 1);
	PE_SetPartsFontColor(no, r, g, b, 1);
	PE_SetPartsFontBoldWeight(no, bold_weight, 1);
	PE_SetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_SetPartsFontEdgeWeight(no, edge_weight, 1);
}
static void PE_GetComboBoxFontProperty(int no, int *type, int *size, int *r,
		int *g, int *b, float *bold_weight, int *edge_r, int *edge_g,
		int *edge_b, float *edge_weight)
{
	PE_GetPartsFontType(no, type, 1);
	PE_GetPartsFontSize(no, size, 1);
	PE_GetPartsFontColor(no, r, g, b, 1);
	PE_GetPartsFontBoldWeight(no, bold_weight, 1);
	PE_GetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_GetPartsFontEdgeWeight(no, edge_weight, 1);
}

static void PE_SetComboBoxCharSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->char_space = space;
}
static int PE_GetComboBoxCharSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->char_space : 0;
}

/* --- 内嵌列表框 --- */
static void PE_SetComboBoxListBoxHeight(int no, int h)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->cb_lb_height = h;
}
static int PE_GetComboBoxListBoxHeight(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->cb_lb_height : 0;
}

static void PE_SetComboBoxListBoxLineHeight(int no, int h)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->cb_lb_line_height = h;
}
static int PE_GetComboBoxListBoxLineHeight(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->cb_lb_line_height : 0;
}

static void PE_SetComboBoxListBoxMargin(int no, int mw, int mh)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->cb_lb_margin_w = mw; w->cb_lb_margin_h = mh; }
}
static int PE_GetComboBoxListBoxWidthMargin(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->cb_lb_margin_w : 0;
}
static int PE_GetComboBoxListBoxHeightMargin(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->cb_lb_margin_h : 0;
}

static void PE_SetComboBoxListBoxScrollPos(int no, int pos)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->cb_lb_scroll_pos = pos;
}
static int PE_GetComboBoxListBoxScrollPos(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->cb_lb_scroll_pos : 0;
}

static void PE_AddComboBoxListBoxItem(int no, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	cb_ensure(w, w->nr_cb_items + 1);
	w->cb_items[w->nr_cb_items++] = text ? string_ref(text)
					    : string_dup(&EMPTY_STRING);
}

static void PE_InsertComboBoxListBoxItem(int no, int index, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	if (index < 0 || index > w->nr_cb_items)
		index = w->nr_cb_items;
	cb_ensure(w, w->nr_cb_items + 1);
	memmove(&w->cb_items[index + 1], &w->cb_items[index],
		(w->nr_cb_items - index) * sizeof(*w->cb_items));
	w->cb_items[index] = text ? string_ref(text) : string_dup(&EMPTY_STRING);
	w->nr_cb_items++;
}

static void PE_SetComboBoxListBoxItem(int no, int index, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (!w || index < 0 || index >= w->nr_cb_items)
		return;
	if (w->cb_items[index])
		free_string(w->cb_items[index]);
	w->cb_items[index] = text ? string_ref(text) : string_dup(&EMPTY_STRING);
}

static struct string *PE_GetComboBoxListBoxItem(int no, int index)
{
	struct parts_widget *w = PE_widget_peek(no);
	if (!w || index < 0 || index >= w->nr_cb_items || !w->cb_items[index])
		return string_dup(&EMPTY_STRING);
	return string_ref(w->cb_items[index]);
}

static int PE_GetComboBoxListBoxItemCount(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->nr_cb_items : 0;
}

static void PE_EraseComboBoxListBoxItem(int no, int index)
{
	struct parts_widget *w = PE_widget(no);
	if (!w || index < 0 || index >= w->nr_cb_items)
		return;
	if (w->cb_items[index])
		free_string(w->cb_items[index]);
	memmove(&w->cb_items[index], &w->cb_items[index + 1],
		(w->nr_cb_items - index - 1) * sizeof(*w->cb_items));
	w->nr_cb_items--;
	if (w->cb_lb_select_index == index)
		w->cb_lb_select_index = -1;
	else if (w->cb_lb_select_index > index)
		w->cb_lb_select_index--;
}

static void PE_ClearComboBoxListBoxItem(int no)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	for (int i = 0; i < w->nr_cb_items; i++)
		if (w->cb_items[i])
			free_string(w->cb_items[i]);
	w->nr_cb_items = 0;
	w->cb_lb_select_index = -1;
}

static void PE_SetComboBoxListBoxFontProperty(int no, int type, int size,
		int r, int g, int b, float bold_weight, int edge_r, int edge_g,
		int edge_b, float edge_weight)
{
	/* 内嵌列表框与显示框共用部件的 text 状态（同一个 parts_no），
	 * 所以字体只能存一份 —— 这与「组合框里外字体一致」的观感相符。 */
	PE_SetComboBoxFontProperty(no, type, size, r, g, b, bold_weight,
				   edge_r, edge_g, edge_b, edge_weight);
}
static void PE_GetComboBoxListBoxFontProperty(int no, int *type, int *size,
		int *r, int *g, int *b, float *bold_weight, int *edge_r, int *edge_g,
		int *edge_b, float *edge_weight)
{
	PE_GetComboBoxFontProperty(no, type, size, r, g, b, bold_weight,
				   edge_r, edge_g, edge_b, edge_weight);
}

static void PE_SetComboBoxListBoxSelectIndex(int no, int index)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->cb_lb_select_index = index;
}
static int PE_GetComboBoxListBoxSelectIndex(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->cb_lb_select_index : 0;
}

static void PE_SetComboBoxListBoxCharSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->cb_lb_char_space = space;
}
static int PE_GetComboBoxListBoxCharSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->cb_lb_char_space : 0;
}

static bool PE_IsComboBoxOpenListBox(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->cb_open : false;
}

/* ===== [D396] 控件：微调框（组件类型 11） =====
 *
 * 数值属性全部是 **float**（证据见 `parts_widget` 里 `spin_*` 的注释）；
 * `DecimalPlace` 是整数（小数位数）。颜色四组与 TextBox 同形。
 */
static void PE_SetSpinBoxSize(int no, int w, int h)
{
	PE_SetPartsSize(no, w, h, 1);
}

static void PE_SetSpinBoxFontProperty(int no, int type, int size, int r, int g,
		int b, float bold_weight, int edge_r, int edge_g, int edge_b,
		float edge_weight)
{
	PE_SetPartsFontType(no, type, 1);
	PE_SetPartsFontSize(no, size, 1);
	PE_SetPartsFontColor(no, r, g, b, 1);
	PE_SetPartsFontBoldWeight(no, bold_weight, 1);
	PE_SetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_SetPartsFontEdgeWeight(no, edge_weight, 1);
}
static void PE_GetSpinBoxFontProperty(int no, int *type, int *size, int *r,
		int *g, int *b, float *bold_weight, int *edge_r, int *edge_g,
		int *edge_b, float *edge_weight)
{
	PE_GetPartsFontType(no, type, 1);
	PE_GetPartsFontSize(no, size, 1);
	PE_GetPartsFontColor(no, r, g, b, 1);
	PE_GetPartsFontBoldWeight(no, bold_weight, 1);
	PE_GetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_GetPartsFontEdgeWeight(no, edge_weight, 1);
}

static void PE_SetSpinBoxReadOnly(int no, bool ro)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->readonly = ro;
}
static bool PE_IsSpinBoxReadOnly(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->readonly : false;
}

static void PE_SetSpinBoxValue(int no, float v)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->spin_value = v;
}
static float PE_GetSpinBoxValue(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->spin_value : 0.0f;
}

static void PE_SetSpinBoxDefaultValue(int no, float v)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->spin_default = v;
}
static float PE_GetSpinBoxDefaultValue(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->spin_default : 0.0f;
}

static void PE_SetSpinBoxMaxValue(int no, float v)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->spin_max = v;
}
static float PE_GetSpinBoxMaxValue(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->spin_max : 0.0f;
}

static void PE_SetSpinBoxMinValue(int no, float v)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->spin_min = v;
}
static float PE_GetSpinBoxMinValue(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->spin_min : 0.0f;
}

static void PE_SetSpinBoxIncrementalValue(int no, float v)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->spin_increment = v;
}
static float PE_GetSpinBoxIncrementalValue(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->spin_increment : 0.0f;
}

static void PE_SetSpinBoxDecimalPlace(int no, int v)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->spin_decimal_place = v;
}
static int PE_GetSpinBoxDecimalPlace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->spin_decimal_place : 0;
}

static void PE_SetSpinBoxBGColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->bg_r = r; w->bg_g = g; w->bg_b = b; w->bg_set = true; }
}
static int PE_GetSpinBoxBGR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_r : 255;
}
static int PE_GetSpinBoxBGG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_g : 255;
}
static int PE_GetSpinBoxBGB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_b : 255;
}

static void PE_SetSpinBoxReadOnlyBGColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->ro_bg_r = r; w->ro_bg_g = g; w->ro_bg_b = b; w->ro_bg_set = true; }
}
static int PE_GetSpinBoxReadOnlyBGR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->ro_bg_set) ? w->ro_bg_r : 192;
}
static int PE_GetSpinBoxReadOnlyBGG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->ro_bg_set) ? w->ro_bg_g : 192;
}
static int PE_GetSpinBoxReadOnlyBGB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->ro_bg_set) ? w->ro_bg_b : 192;
}

static void PE_SetSpinBoxFrameColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->frame_r = r; w->frame_g = g; w->frame_b = b; w->frame_set = true; }
}
static int PE_GetSpinBoxFrameR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->frame_set) ? w->frame_r : 0;
}
static int PE_GetSpinBoxFrameG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->frame_set) ? w->frame_g : 0;
}
static int PE_GetSpinBoxFrameB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->frame_set) ? w->frame_b : 0;
}

static void PE_SetSpinBoxSelectColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->select_r = r; w->select_g = g; w->select_b = b; w->select_set = true; }
}
static int PE_GetSpinBoxSelectR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->select_set) ? w->select_r : 0;
}
static int PE_GetSpinBoxSelectG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->select_set) ? w->select_g : 0;
}
static int PE_GetSpinBoxSelectB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->select_set) ? w->select_b : 0;
}

static void PE_SetSpinBoxCGName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->cg_name, name);
}
static struct string *PE_GetSpinBoxCGName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->cg_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetSpinBoxCharSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w) w->char_space = space;
}
static int PE_GetSpinBoxCharSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->char_space : 0;
}

/* ===== [D396] 控件：按钮（组件类型 0） =====
 *
 * 按钮是「底图 + 文字」，属性集与 TextBox 的颜色组不同：
 * 只有一组前景色（`SetButtonColor` → `GetButtonR/G/B`），没有 BG/Frame/Select。
 * `SetButtonEnable` / `IsButtonEnable` 决定是否响应输入。
 */
static void PE_SetButtonSize(int no, int w, int h)
{
	PE_SetPartsSize(no, w, h, 1);
}

static void PE_SetButtonEnable(int no, bool enable)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->enabled = enable;
}
static bool PE_IsButtonEnable(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->enabled : false;
}

static void PE_SetButtonColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->bg_r = r; w->bg_g = g; w->bg_b = b; w->bg_set = true; }
}
static int PE_GetButtonR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_r : 0;
}
static int PE_GetButtonG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_g : 0;
}
static int PE_GetButtonB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_b : 0;
}

static void PE_SetButtonFontProperty(int no, int type, int size, int r, int g,
		int b, float bold_weight, int edge_r, int edge_g, int edge_b,
		float edge_weight)
{
	PE_SetPartsFontType(no, type, 1);
	PE_SetPartsFontSize(no, size, 1);
	PE_SetPartsFontColor(no, r, g, b, 1);
	PE_SetPartsFontBoldWeight(no, bold_weight, 1);
	PE_SetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_SetPartsFontEdgeWeight(no, edge_weight, 1);
}
static void PE_GetButtonFontProperty(int no, int *type, int *size, int *r,
		int *g, int *b, float *bold_weight, int *edge_r, int *edge_g,
		int *edge_b, float *edge_weight)
{
	PE_GetPartsFontType(no, type, 1);
	PE_GetPartsFontSize(no, size, 1);
	PE_GetPartsFontColor(no, r, g, b, 1);
	PE_GetPartsFontBoldWeight(no, bold_weight, 1);
	PE_GetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_GetPartsFontEdgeWeight(no, edge_weight, 1);
}

static void PE_SetButtonCGName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->cg_name, name);
}
static struct string *PE_GetButtonCGName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->cg_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetButtonFlatName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->flat_name, name);
}
static struct string *PE_GetButtonFlatName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->flat_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetButtonText(int no, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->text, text);
}
static struct string *PE_GetButtonText(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->text) : string_dup(&EMPTY_STRING);
}

static void PE_SetButtonTextOriginPosMode(int no, int mode)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->text_origin_pos_mode = mode;
}
static int PE_GetButtonTextOriginPosMode(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->text_origin_pos_mode : 0;
}

static void PE_SetButtonCharSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->char_space = space;
}
static int PE_GetButtonCharSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->char_space : 0;
}

static void PE_SetButtonLineSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->line_space = space;
}
static int PE_GetButtonLineSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->line_space : 0;
}

/* 音效编号：直接转 parts 层（那里会校验 sound 资源是否存在）。 */
/* 拖动触发 / 像素级判定：两者都是「命中策略」的开关，
 * 与 CheckBox 的同名属性语义一致。PixelDecide 转 parts 层的真字段。 */
static void PE_SetButtonDrag(int no, bool drag)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->drag = drag;
}
static bool PE_IsButtonDrag(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->drag : false;
}

static void PE_SetButtonPixelDecide(int no, bool on)
{
	PE_SetPartsPixelDecide(no, on);
}
static bool PE_IsButtonPixelDecide(int no)
{
	return PE_GetPartsPixelDecide(no);
}

static void PE_SetButtonOnCursorSoundNumber(int no, int n)
{
	PE_SetPartsOnCursorSoundNumber(no, n);
}
static void PE_SetButtonClickSoundNumber(int no, int n)
{
	PE_SetPartsClickSoundNumber(no, n);
}
static int PE_GetButtonOnCursorSoundNumber(int no)
{
	return PE_GetPartsOnCursorSoundNumber(no);
}
static int PE_GetButtonClickSoundNumber(int no)
{
	return PE_GetPartsClickSoundNumber(no);
}

/* ===== [D396] 控件：复选框（组件类型 1） =====
 *
 * 勾选 / 可用两个状态在 parts.c 里（`PE_CheckBoxChecked` 一族，[D396] 已迁到
 * `parts_widget`）；这里补的是**外观与文字**那部分：
 *   - `CGName` / `FlatName` 与 `Override*` 三兄弟：普通态 / 勾选态 / 覆盖态
 *     各自的底图名。Override 是「鼠标悬停时临时换图」，Checked 是勾上之后。
 *   - `ButtonMode`：勾选框外观按「按钮」样式画（整块可点），不是小方框。
 *   - `ButtonWidth/Height`：小方框（或按钮区）的尺寸。
 *   - `Drag`：按下后拖动手指移出也能切换。
 */
static void PE_SetCheckBoxSize(int no, int w, int h)
{
	/* [D517] AIN 配置 checkbox 外观 = 该部件走程序化 checkbox 渲染
	 * （EX 无 フラット名 的检查件，flat 空库）。渲染层据此画色板。 */
	static int n;
	if (getenv("XSYS4_CB_DBG") && n++ < 24)
		NOTICE("[cb] SetCheckBoxSize no=%d %dx%d", no, w, h);
	struct parts_widget *wgt = PE_widget(no);
	if (wgt)
		wgt->is_checkbox = true;
	PE_SetPartsSize(no, w, h, 1);
}

static int PE_GetCheckBoxButtonWidth(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->button_width : 0;
}
static int PE_GetCheckBoxButtonHeight(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->button_height : 0;
}

static void PE_SetCheckBoxColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) { w->bg_r = r; w->bg_g = g; w->bg_b = b; w->bg_set = true; }
}
static int PE_GetCheckBoxR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_r : 0;
}
static int PE_GetCheckBoxG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_g : 0;
}
static int PE_GetCheckBoxB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return (w && w->bg_set) ? w->bg_b : 0;
}

static void PE_SetCheckBoxFontProperty(int no, int type, int size, int r, int g,
		int b, float bold_weight, int edge_r, int edge_g, int edge_b,
		float edge_weight)
{
	PE_SetPartsFontType(no, type, 1);
	PE_SetPartsFontSize(no, size, 1);
	PE_SetPartsFontColor(no, r, g, b, 1);
	PE_SetPartsFontBoldWeight(no, bold_weight, 1);
	PE_SetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_SetPartsFontEdgeWeight(no, edge_weight, 1);
}
static void PE_GetCheckBoxFontProperty(int no, int *type, int *size, int *r,
		int *g, int *b, float *bold_weight, int *edge_r, int *edge_g,
		int *edge_b, float *edge_weight)
{
	PE_GetPartsFontType(no, type, 1);
	PE_GetPartsFontSize(no, size, 1);
	PE_GetPartsFontColor(no, r, g, b, 1);
	PE_GetPartsFontBoldWeight(no, bold_weight, 1);
	PE_GetPartsFontEdgeColor(no, edge_r, edge_g, edge_b, 1);
	PE_GetPartsFontEdgeWeight(no, edge_weight, 1);
}

static void PE_SetCheckBoxCGName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->cg_name, name);
}
static struct string *PE_GetCheckBoxCGName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->cg_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetCheckBoxOverrideCGName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->override_cg_name, name);
}
static struct string *PE_GetCheckBoxOverrideCGName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->override_cg_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetCheckBoxOverrideCheckedCGName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->override_checked_cg_name, name);
}
static struct string *PE_GetCheckBoxOverrideCheckedCGName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->override_checked_cg_name)
		 : string_dup(&EMPTY_STRING);
}

static void PE_SetCheckBoxFlatName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->flat_name, name);
}
static struct string *PE_GetCheckBoxFlatName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->flat_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetCheckBoxOverrideFlatName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->override_flat_name, name);
}
static struct string *PE_GetCheckBoxOverrideFlatName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->override_flat_name) : string_dup(&EMPTY_STRING);
}

static void PE_SetCheckBoxOverrideCheckedFlatName(int no, struct string *name)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->override_checked_flat_name, name);
}
static struct string *PE_GetCheckBoxOverrideCheckedFlatName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->override_checked_flat_name)
		 : string_dup(&EMPTY_STRING);
}

static void PE_SetCheckBoxText(int no, struct string *text)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		widget_set_string(&w->text, text);
}
static struct string *PE_GetCheckBoxText(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->text) : string_dup(&EMPTY_STRING);
}

static void PE_SetCheckBoxTextOriginPosMode(int no, int mode)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->text_origin_pos_mode = mode;
}
static int PE_GetCheckBoxTextOriginPosMode(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->text_origin_pos_mode : 0;
}

static void PE_SetCheckBoxButtonMode(int no, bool mode)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->button_mode = mode;
}
static bool PE_IsCheckBoxButtonMode(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->button_mode : false;
}

static void PE_SetCheckBoxCharSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->char_space = space;
}
static int PE_GetCheckBoxCharSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->char_space : 0;
}

static void PE_SetCheckBoxLineSpace(int no, int space)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->line_space = space;
}
static int PE_GetCheckBoxLineSpace(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->line_space : 0;
}

static void PE_SetCheckBoxDrag(int no, bool drag)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->drag = drag;
}
static bool PE_IsCheckBoxDrag(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->drag : false;
}

static void PE_SetCheckBoxOnCursorSoundNumber(int no, int n)
{
	PE_SetPartsOnCursorSoundNumber(no, n);
}
static void PE_SetCheckBoxClickSoundNumber(int no, int n)
{
	PE_SetPartsClickSoundNumber(no, n);
}
static int PE_GetCheckBoxOnCursorSoundNumber(int no)
{
	return PE_GetPartsOnCursorSoundNumber(no);
}
static int PE_GetCheckBoxClickSoundNumber(int no)
{
	return PE_GetPartsClickSoundNumber(no);
}

/* ===== [D396] 控件：单选按钮组（组件类型 9） =====
 *
 * 单选按钮组本身不画东西，它维护**成员列表**（子部件号）并保证互斥：
 *   AddRadioButtonBoxChild(no, child)    加入成员
 *   RemoveRadioButtonBoxChild(no, child) 移出
 *   NumofRadioButtonBoxChild(no)         成员数
 *   GetRadioButtonBoxChild(no, index)    第 index 个成员（0-based）
 *   ClearRadioButtonBoxChild(no)         清空
 *   IsRadioButtonBoxExistGUI(no, child)  某子件是否属于本组
 */
static void rb_ensure(struct parts_widget *w, int need)
{
	if (need <= w->cap_radio_children)
		return;
	int cap = w->cap_radio_children ? w->cap_radio_children : 4;
	while (cap < need)
		cap *= 2;
	w->radio_children = xrealloc(w->radio_children, cap * sizeof(int));
	w->cap_radio_children = cap;
}

static void PE_AddRadioButtonBoxChild(int no, int child)
{
	struct parts_widget *w = PE_widget(no);
	if (!w || child <= 0)
		return;
	for (int i = 0; i < w->nr_radio_children; i++)
		if (w->radio_children[i] == child)
			return;
	rb_ensure(w, w->nr_radio_children + 1);
	w->radio_children[w->nr_radio_children++] = child;
}

static void PE_RemoveRadioButtonBoxChild(int no, int child)
{
	struct parts_widget *w = PE_widget(no);
	if (!w)
		return;
	for (int i = 0; i < w->nr_radio_children; i++) {
		if (w->radio_children[i] != child)
			continue;
		memmove(&w->radio_children[i], &w->radio_children[i + 1],
			(w->nr_radio_children - i - 1) * sizeof(int));
		w->nr_radio_children--;
		return;
	}
}

static int PE_NumofRadioButtonBoxChild(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->nr_radio_children : 0;
}

static int PE_GetRadioButtonBoxChild(int no, int index)
{
	struct parts_widget *w = PE_widget_peek(no);
	if (!w || index < 0 || index >= w->nr_radio_children)
		return 0;
	return w->radio_children[index];
}

static void PE_ClearRadioButtonBoxChild(int no)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->nr_radio_children = 0;
}

static bool PE_IsRadioButtonBoxExistGUI(int no, int child)
{
	struct parts_widget *w = PE_widget_peek(no);
	if (!w)
		return false;
	for (int i = 0; i < w->nr_radio_children; i++)
		if (w->radio_children[i] == child)
			return true;
	return false;
}



/* [D202] 消息窗口配置 getter（SaveAdvMessageWindowData 用于保存/恢复文本样式）。
 * 对应的 setter 目前都是 noop（引擎用 PE_SetText 同步排版），所以 getter
 * 把 ref 出参写成安全默认值即可：AIN 只把它们存进快照，不参与排版判定。
 * 以前这些名字根本没注册，AIN 一调就 VM_ERROR，第一章剧情刚显示第一行
 * 文本就断在 CMessageTextView@GetTextFontColorList。
 * 签名与库表 f[522]/[526]/[530] 一致（ref int/float 在 C 侧是普通指针）。 */
static struct string *PE_GetMessageWindowText(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->text) : string_dup(&EMPTY_STRING);
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
	struct parts_widget *wp = PE_widget_peek(no);
	if (x) *x = wp ? wp->msg_area_x : 0;
	if (y) *y = wp ? wp->msg_area_y : 0;
	if (w) *w = wp ? wp->msg_area_w : 0;
	if (h) *h = wp ? wp->msg_area_h : 0;
}

static void PE_GetMessageWindowTextSpace(int no, int *letter, int *line)
{
	struct parts_widget *w = PE_widget_peek(no);
	if (letter) *letter = w ? w->msg_space_x : 0;
	if (line) *line = w ? w->msg_space_y : 0;
}

static void PE_GetKeyWaitCGName(int no, struct string **name, int *start_no,
		int *numof_cg, int *time_per_cg)
{
	/* [D496] 读真值：与 PE_SetKeyWaitCGName 对称。 */
	struct parts_widget *w = PE_widget_peek(no);
	if (name)
		*name = (w && w->keywait_cg)
			? string_dup(w->keywait_cg)
			: string_dup(&EMPTY_STRING);
	if (start_no) *start_no = w ? w->keywait_cg_start_no : 0;
	if (numof_cg) *numof_cg = w ? w->keywait_cg_numof : 0;
	if (time_per_cg) *time_per_cg = w ? w->keywait_cg_time : 0;
}

static int PE_GetMessageWindowTextOriginPosMode(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->text_origin_pos_mode : 0;
}
static int PE_GetMessageWindowTextSpeed(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->msg_text_speed : 0;
}
static int PE_GetMessageWindowInactiveMultipleColorR(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->msg_inactive_r : 255;
}
static int PE_GetMessageWindowInactiveMultipleColorG(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->msg_inactive_g : 255;
}
static int PE_GetMessageWindowInactiveMultipleColorB(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->msg_inactive_b : 255;
}
static int PE_GetKeyWaitPosX(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->keywait_x : 0;
}
static int PE_GetKeyWaitPosY(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->keywait_y : 0;
}
static int PE_GetKeyWaitPosZ(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->keywait_z : 0;
}
/* [D496] 读真值：与 PE_SetKeyWaitShow 对称。 */
static bool PE_IsKeyWaitShow(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->keywait_show : false;
}
static bool PE_IsComponentMessageWindowEffectLink(int no) { (void)no; return false; }

/* [D396] 消息窗口的底图 / flat 名 getter。
 *
 * D202 时这两个是「恒返回空串」的桩，注释写得很清楚：`CMessageWindow@IsFlat`
 * 用 `String.Empty(FlatName)` 取反判断，空串 ⇒ 走普通消息窗口路径，
 * 从而绕开我们没实现的 flat 动画。
 *
 * 现在 setter 真的存了值，getter 也必须读真值 —— 否则「设进去读出来是空」
 * 会让 AIN 的存档往返静默丢样式。**代价**：AIN 一旦设了 FlatName，
 * `IsFlat` 就会为真、走 flat 动画路径，而那条路径尚未实现。
 * 这是必须暴露的真实缺口（不是靠 getter 撒谎掩盖），判据是
 * `smoke full` 的 ADV 段是否仍然推进；真卡住时要做的是补 flat 消息窗口，
 * 而不是把 getter 改回空串。 */
static struct string *PE_GetMessageWindowFlatName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->flat_name) : string_dup(&EMPTY_STRING);
}

static struct string *PE_GetMessageWindowCGName(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? widget_get_string(&w->cg_name) : string_dup(&EMPTY_STRING);
}

static struct string *PE_GetKeyWaitFlatName(int no)
{
	/* [D496] 读真值：与 PE_SetKeyWaitFlatName 对称。 */
	struct parts_widget *w = PE_widget_peek(no);
	if (w && w->keywait_flat)
		return string_dup(w->keywait_flat);
	return string_dup(&EMPTY_STRING);
}

static int PE_GetMessageWindowFlatShowWaitFrameNumber(int no)
{
	struct parts_widget *w = PE_widget_peek(no);
	return w ? w->msg_flat_wait_frame : 0;
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
static void PE_SetMessageWindowFlatShowWaitFrameNumber(int no, int frames)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->msg_flat_wait_frame = frames;
}

/* AIN SetMessageWindowInactiveMultipleColor：void SetMessageWindowInactiveMultipleColor。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_SetMessageWindowInactiveMultipleColor(int no, int r, int g, int b)
{
	struct parts_widget *w = PE_widget(no);
	if (w) {
		w->msg_inactive_r = r;
		w->msg_inactive_g = g;
		w->msg_inactive_b = b;
	}
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
	/* [D396] 文本与「台词位置指纹」存进 `parts_widget`（原来是旁挂的
	 * `msgwin_text` 环形表）。指纹（msg_num / func_name / ver / step）是
	 * AIN 用来在存档后定位「读到哪一句」的；AIN 侧没有对应 getter，
	 * 所以只存不读 —— 但必须存，否则存档会退化成从头开始。 */
	struct parts_widget *w = PE_widget(no);
	if (w) {
		widget_set_string(&w->text, text);
		w->msg_num = msg_num;
		widget_set_string(&w->msg_func_name, func_name);
		w->msg_ver = ver;
		w->msg_step = step;
	}
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
static void PE_SetMessageWindowTextArea(int no, int x, int y, int w, int h)
{
	struct parts_widget *wp = PE_widget(no);
	if (wp) {
		wp->msg_area_x = x;
		wp->msg_area_y = y;
		wp->msg_area_w = w;
		wp->msg_area_h = h;
	}
}

/* AIN SetMessageWindowTextOriginPosMode：void SetMessageWindowTextOriginPosMode。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_SetMessageWindowTextOriginPosMode(int no, int mode)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->text_origin_pos_mode = mode;
	MSGWIN_HIT("SetMessageWindowTextOriginPosMode no=%d mode=%d", no, mode);
}

/* AIN SetMessageWindowTextSpace：void SetMessageWindowTextSpace。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_SetMessageWindowTextSpace(int no, int letter, int line)
{
	struct parts_widget *w = PE_widget(no);
	if (w) {
		w->msg_space_x = letter;
		w->msg_space_y = line;
	}
	MSGWIN_HIT("SetMessageWindowTextSpace no=%d letter=%d line=%d", no, letter, line);
}

/* AIN SetMessageWindowTextSpeed：void SetMessageWindowTextSpeed。[HACK-POC D85] 空实现（setter 占位）。 */
static void PE_SetMessageWindowTextSpeed(int no, int speed)
{
	struct parts_widget *w = PE_widget(no);
	if (w)
		w->msg_text_speed = speed;
	MSGWIN_HIT("SetMessageWindowTextSpeed no=%d speed=%d", no, speed);
}

/* AIN SetMotionData：void SetMotionData。[HACK-POC D85] 空实现（setter 占位）。
 *
 * [D486] **这条桩是「窗口淡出不动」的头号嫌疑**：
 * `BattleInfoView@Hide` 走 `Parts@Motion → PartsMotionTask@SetAlpha(1,0,…)`
 * —— 这些全是 **AIN 类**（`SetKey` 只是 `Array.PushBack` 存 key），
 * AIN 侧每帧算好插值后调这个 HLL 把值写到部件上。桩什么都不做，
 * alpha 就停在 1，战况窗口淡出永远不发生。
 * 探针（`XSYS4_MOTION_DBG=1`）确认它到底被调多勤、参数长什么样。 */
static void PE_Noop_SetMotionData(int a0, struct string * a1, int a2, int a3)
{
	if (getenv("XSYS4_MOTION_DBG")) {
		static int n;
		if (n++ < 200)
			NOTICE("[motion] SetMotionData a0=%d a1='%s' a2=%d a3=%d",
			       a0, a1 ? a1->text : "(null)", a2, a3);
	}
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetMovieTime：void SetMovieTime(int, int, int)。[D85] 占位——
 * [D497] 仍未接：movie API 没有 seek；AIN 签名 (no, ?, ?) 的两个 int
 * 语义（目标时刻 / 帧号？）无运行期证据。 */
static void PE_Noop_SetMovieTime(int a0, int a1, int a2)
{
	if (getenv("XSYS4_MOVIE_DBG"))
		NOTICE("[pmov] SetMovieTime no=%d a1=%d a2=%d", a0, a1, a2);
	(void)a0;
	(void)a1;
	(void)a2;
}

/* ==========================================================================
 * [D497] PartsMovie 族：部件内嵌视频。
 *
 * 来源：69 个 Scene*@Run 全量入口扫 reach_hll（D496 修完主流程 10 条后
 * 的复扫），4 条新可达未实现全在这族——消费方
 * `movie::detail::CDrawMovie`（SceneMovie 的 OP 播放路径）。
 *
 * AIN 签名（--dump-hll 类型码，D493 列）：
 *   bool CreatePartsMovie (int no, string file, int×6)
 *   bool ReleasePartsMovie(int no, int state)
 *   bool PlayPartsMovie   (int no, int state, int ?)
 *   bool IsEndPartsMovie  (int no, int state)
 *   int  GetPartsMovieEndTime  (int no, int state)
 *   int  GetPartsMovieCurrentTime(int no, int state)
 *
 * 证据链：
 *  - `CDrawMovie@CreateMovieParts`(6843) 反汇编：先 `Release(m0)` 再
 *    `m0 = GetFreeSystemPartsNumber()`，然后 CreatePartsMovie(m0, file,
 *    1000, local1, m4, m5, m6, 1)。成员名（dump-structs）：m4/m5/m6 =
 *    `m_nBackColorY/Cb/Cr` —— **YCbCr 背景色**（视频信箱边的底色）。
 *    尾参 1 = state（与 PE_init_parts_movie 的 1-based state 一致）；
 *    1000/local1 按 (x, y) 处理——位置即使判错也只是 cosmetic。
 *  - 创建后 AIN 立刻 `Math.Max(ViewWidth, Parts_GetWidth)` —— 部件尺寸
 *    由**视频自身尺寸**定（movie_get_size），AIN 只做"至少铺满视图"。
 *  - Play 循环里 AIN 每帧调 `IsEndPartsMovie` 轮询 ⇒ 引擎在轮询点
 *    `movie_play`（推进）+ `movie_draw`（画进部件的隐藏精灵，
 *    `PE_get_movie_sprite`），这与 DrawMovie 的 Load/Run/Draw 节奏同构。
 *  - `PlayPartsMovie` 返回 false 的 AIN 路径是**优雅跳过**（隐藏光标、
 *    return 0，字节码 0x1c886e）——所以 load 失败不会挂死场景。
 *
 * 引擎内建 YCbCr→RGB 用 BT.601（与视频解码同一惯例）。
 * 判据（待跑测）：SceneMovie 的 OP 实际播放且结束时场景正常推进；
 * `XSYS4_MOVIE_DBG=1` 打创建/轮询轨迹。 */
#define PARTS_MOVIE_SLOTS 4
struct parts_movie_slot {
	int no;                /* 0 = 空槽（部件号从 1 开始） */
	int state;
	struct movie_context *mc;
};
static struct parts_movie_slot parts_movie_slots[PARTS_MOVIE_SLOTS];

static struct movie_context **parts_movie_slot(int no, int state)
{
	struct parts_movie_slot *e = NULL;
	for (int i = 0; i < PARTS_MOVIE_SLOTS; i++) {
		if (parts_movie_slots[i].no == no) {
			e = &parts_movie_slots[i];
			break;
		}
		if (!e && parts_movie_slots[i].no == 0)
			e = &parts_movie_slots[i];
	}
	if (!e)
		return NULL;
	e->no = no;
	e->state = state;
	return &e->mc;
}

static void parts_movie_close(int no)
{
	for (int i = 0; i < PARTS_MOVIE_SLOTS; i++) {
		if (parts_movie_slots[i].no == no) {
			if (parts_movie_slots[i].mc)
				movie_free(parts_movie_slots[i].mc);
			parts_movie_slots[i].mc = NULL;
			parts_movie_slots[i].no = 0;
		}
	}
}

/* ITU-R BT.601：Y (16..219), Cb/Cr (-128..127) → RGB。
 * 视频信箱底色，精度要求不高，截断即可。 */
static int ycc_clamp8(int v)
{
	return v < 0 ? 0 : (v > 255 ? 255 : v);
}
static void ycbcr_to_rgb(int y, int cb, int cr, int *r, int *g, int *b)
{
	int C = y - 16, D = cb - 128, E = cr - 128;
	*r = ycc_clamp8((298 * C + 409 * E + 128) >> 8);
	*g = ycc_clamp8((298 * C - 100 * D - 208 * E + 128) >> 8);
	*b = ycc_clamp8((298 * C + 516 * D + 128) >> 8);
}

static bool PE_CreatePartsMovie(int no, struct string *file, int x, int y,
		int bg_y, int bg_cb, int bg_cr, int state)
{
	if (!file)
		return false;
	parts_movie_close(no);

	struct movie_context *mc = movie_load(file->text);
	if (!mc) {
		WARNING("[pmov] CreatePartsMovie: movie_load(\"%s\") failed",
			display_utf0(file->text));
		return false;
	}
	int mw, mh;
	movie_get_size(mc, &mw, &mh);
	if (mw <= 0 || mh <= 0) {
		/* 视频尺寸拿不到：退到视图大小，保证部件至少铺满可播。 */
		mw = gfx_view_width();
		mh = gfx_view_height();
	}
	int r, g, b;
	ycbcr_to_rgb(bg_y, bg_cb, bg_cr, &r, &g, &b);
	if (getenv("XSYS4_MOVIE_DBG"))
		NOTICE("[pmov] create no=%d file='%s' %dx%d pos=(%d,%d) bg=(%d,%d,%d) state=%d",
		       no, display_utf0(file->text), mw, mh, x, y, r, g, b, state);

	if (!PE_init_parts_movie(no, mw, mh, r, g, b, state)) {
		movie_free(mc);
		return false;
	}
	/* x/y：AIN 侧证据是实参直传；引擎按部件坐标落位。 */
	PE_SetPos(no, x, y);

	struct movie_context **slot = parts_movie_slot(no, state);
	if (!slot) {
		WARNING("[pmov] movie slot table full (%d)", PARTS_MOVIE_SLOTS);
		movie_free(mc);
		return false;
	}
	*slot = mc;
	return true;
}

static bool PE_ReleasePartsMovie(int no, int state)
{
	if (getenv("XSYS4_MOVIE_DBG"))
		NOTICE("[pmov] release no=%d state=%d", no, state);
	parts_movie_close(no);
	return true;
}

static bool PE_PlayPartsMovie(int no, int state, int a2)
{
	struct movie_context **slot = parts_movie_slot(no, state);
	if (!slot || !*slot) {
		if (getenv("XSYS4_MOVIE_DBG"))
			NOTICE("[pmov] play no=%d state=%d: no context (create failed?)",
			       no, state);
		return false;   /* AIN 侧走"优雅跳过"路径（0x1c886e） */
	}
	if (getenv("XSYS4_MOVIE_DBG"))
		NOTICE("[pmov] play no=%d state=%d a2=%d", no, state, a2);
	return movie_play(*slot);
}

static bool PE_IsEndPartsMovie(int no, int state)
{
	struct movie_context **slot = parts_movie_slot(no, state);
	if (!slot || !*slot)
		return true;   /* 没在播 = 视为已结束，让轮询退出 */
	/* AIN 每帧来这里轮询（0x1c8854 之后是轮询循环）⇒ 推进 + 重绘。
	 * 精灵是 PE_init_parts_movie 建的隐藏渲染目标，部件引擎每帧把它
	 * 合成进部件（parts_render 走 movie 分支）。 */
	struct sact_sprite *sp = sact_try_get_sprite(PE_get_movie_sprite(no, state));
	if (sp)
		movie_draw(*slot, sp);
	return movie_is_end(*slot);
}

static int PE_GetPartsMovieEndTime(int no, int state)
{
	struct movie_context **slot = parts_movie_slot(no, state);
	return (slot && *slot) ? movie_get_duration(*slot) : 0;
}

static int PE_GetPartsMovieCurrentTime(int no, int state)
{
	struct movie_context **slot = parts_movie_slot(no, state);
	return (slot && *slot) ? movie_get_position(*slot) : 0;
}

/* [D497] `void GetPartsConstructionProcess(int no, int state, ref array a,
 * ref array b, ref array c, int d)`（dump-hll：ret void、三 ref array）。
 *
 * AIN 消费方：`parts::GetConstructProcess(7)` → activity 的 CompParts
 * 重放路径（FUNC 297/298）。要把 ops 队列（TAILQ）按 AIN 侧的数组编码
 * 写回——**编码未知**（每个 op 的参数布局 = AIN 侧 op 枚举，没有证据），
 * 猜了就是编造。当前回空数组 = "没有合成数据"：该路径在全部 679 份
 * 历史日志中 0 次命中，从不影响已观察流程；真被走到时以
 * `XSYS4_CPROC_DBG=1` 的轨迹定性，再按 AIN 枚举补编码。 */
static void PE_GetPartsConstructionProcess(int no, int state,
		struct page **a, struct page **b, struct page **c, int d)
{
	if (getenv("XSYS4_CPROC_DBG"))
		NOTICE("[cproc] GetConstructionProcess no=%d state=%d d=%d "
		       "(ops readback not implemented, returning empty)",
		       no, state, d);
	(void)a;
	(void)b;
	(void)c;
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

/* 用户组件 (component type 15)。引擎侧只存"名字 + 键值袋"，
 * TextButton 之类的行为与绘制由游戏的 AIN 代码实现：
 *   活动创建用户组件事件的入口（CallUserComponentCreateEventWithChild）
 *   → 引擎按部件号取用户组件名（GetUserComponentName）
 *   → 交给活动的用户组件事件委托。
 * 数据表就是 EX 的 数据 = ['オブジェクト名','TextButton','参数','标题,从头开始']。 */
static struct string *PE_GetUserComponentName_str(int parts_no)
{
	/* [D441] 声明返回 `string` 的导出**不能返回 NULL**。
	 *
	 * AIN 侧没有"空指针字符串"这个值：`string` 是堆上的页，NULL 会被 FFI
	 * 层报成 `returned (nil) where a string was declared`，调用方拿到的是
	 * 一个无法比较、无法拼接的野值。同族的 `PE_Act_GetActivityPartsName`
	 * 一直是 `cstr_to_string("")` —— 这里跟它对齐。
	 *
	 * 触发场景：战斗里每次点技能都会刷 2 条该告警
	 * （`bat3`/`chain1` 日志），说明确实有部件没有用户组件名。 */
	const char *name = PE_GetUserComponentName(parts_no);
	return cstr_to_string(name ? name : "");
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
	/* [D441] 同上：声明 `string` 返回就必须给一个真字符串。
	 * 部件没登记过这个 key 是**正常情况**（不是错误），返回空串。 */
	const char *v = PE_GetUserComponentData(parts_no, key ? key->text : NULL);
	return cstr_to_string(v ? v : "");
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
	/* [D498] 文件兜底只对**不带实例号**的路径做：`….pact:N` 是
	 * GetFreeName 的实例枚举，basename 匹配会让任意 N 都命中 → 死循环。 */
	if (!exists && !strchr(name->text, ':')) {
		/* [D498] AIN 也会用**文件路径**问（Asra 设置页面
		 * `Asra\设置／０１／消息速度页面.pact`）——注册表没有时按
		 * 档案存在性兜底（activity_is_exist_file 内含 .pact→.pactex
		 * 扩展名解析）。返回 0 会让设置界面整页跳过装载。 */
		exists = activity_is_exist_file(name->text);
	}
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
/* [D497] AIN 声明 (ref string path, string name)——原 (page**, string)
 * 形状是错的（ref string 到达形式是 struct string **）。桩返回 0 =
 * 调用方回退到文件加载（见下方 LoadActivityEXText 注释），形状对齐
 * 后语义不变。 */
static int PE_Act_SaveActivityEXText(struct string **path, struct string *name)
{
	(void)path;
	(void)name;
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
	int found = activity_find_parts(act, parts_name->text) != NULL;
	/* [D498] 设置界面标签空白排查：SetTextForParts 先在这里早退。
	 * 看它拿的活动名/部件名与注册名是否错位；顺带打延迟文本三旗标
	 * （IsPartsDelay = global<28> && global<29>，为真时文本进队列
	 * 不进 Parts_SetText）。 */
	if (getenv("XSYS4_ACT_DBG")) {
		static int n;
		if (n++ < 3000)
			NOTICE("[act] IsExistPartsByName('%s','%s') = %d | g28=%d g29=%d",
			       name->text, parts_name->text, found,
			       global_get(28).i, global_get(29).i);
		/* [D498] MISS 的前 6 次打 AIN 调用栈：回答「谁在枚举 SYS_* 值件、
		 * 查完之后走哪条创建路径」。 */
		static int nm;
		if (!found && nm < 6) {
			nm++;
			NOTICE("[act] MISS 栈 #%d：", nm);
			vm_stack_trace();
		}
	}
	return found;
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
 * 同义，只是 AIN 用的是另一个导出名。
 * [D497] AIN 声明 3 参 (int no, string name, int state)——中间的 string
 * 是 flat 名；引擎里一个部件状态只有一份 flat，名字不参与判定
 * （arity 审计揪出：原 2 参实现会把 string 指针当 state 用）。 */
static int PE_Parts_GetFlatFrame(int parts_no, struct string *name, int state)
{
	(void)name;
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
	    HLL_EXPORT(GetNumeralLength, PE_GetNumeralLength),   /* [D456] */
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
	    /* [D389] 这两个一直在未实现清单里（`[TURNSTALL]` 相关）：
	     * `PartsMotionTask@IsFinish` → `CASTask@IsEndTask` 要判「到没到结束帧」，
	     * 而 AIN 侧拿帧号的入口就是它们。我们只有一个全局 motion 时钟，
	     * 起点恒 0、终点是 `motion_end_t`。 */
	    HLL_EXPORT(GetMotionBeginFrame, PE_GetMotionBeginFrame),
	    HLL_EXPORT(GetMotionEndFrame, PE_GetMotionEndTime),
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
	    HLL_EXPORT(GetComponentAddColorR, PE_GetAddColorR),
	    HLL_EXPORT(GetComponentAddColorG, PE_GetAddColorG),
	    HLL_EXPORT(GetComponentAddColorB, PE_GetAddColorB),
	    HLL_EXPORT(SetComponentMulColor, PE_SetMultiplyColor),
	    HLL_EXPORT(GetComponentMulColorR, PE_GetMultiplyColorR),
	    HLL_EXPORT(GetComponentMulColorG, PE_GetMultiplyColorG),
	    HLL_EXPORT(GetComponentMulColorB, PE_GetMultiplyColorB),
	    HLL_EXPORT(SetComponentDrawFilter, PE_SetPartsDrawFilter),
	    HLL_EXPORT(GetComponentDrawFilter, PE_GetPartsDrawFilter),
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
	    HLL_EXPORT(GetComponentRotateX, PE_GetPartsRotateX),
	    HLL_EXPORT(GetComponentRotateY, PE_GetPartsRotateY),
	    HLL_EXPORT(GetComponentRotateZ, PE_GetPartsRotateZ),
	    HLL_EXPORT(SetComponentMargin, PE_SetComponentMargin),
	    HLL_EXPORT(GetComponentMarginTop, PE_GetComponentMarginTop),
	    HLL_EXPORT(GetComponentMarginBottom, PE_GetComponentMarginBottom),
	    HLL_EXPORT(GetComponentMarginLeft, PE_GetComponentMarginLeft),
	    HLL_EXPORT(GetComponentMarginRight, PE_GetComponentMarginRight),
	    HLL_EXPORT(SetComponentAlphaClipper, PE_SetPartsAlphaClipperPartsNumber),
	    HLL_EXPORT(GetComponentAlphaClipper, PE_GetPartsAlphaClipperPartsNo),
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
	    HLL_EXPORT(SetButtonSize, PE_SetButtonSize),
	    HLL_EXPORT(SetButtonDrag, PE_SetButtonDrag),
	    HLL_EXPORT(IsButtonDrag, PE_IsButtonDrag),
	    HLL_EXPORT(SetButtonEnable, PE_SetButtonEnable),
	    HLL_EXPORT(IsButtonEnable, PE_IsButtonEnable),
	    HLL_EXPORT(SetButtonPixelDecide, PE_SetButtonPixelDecide),
	    HLL_EXPORT(IsButtonPixelDecide, PE_IsButtonPixelDecide),
	    HLL_EXPORT(SetButtonColor, PE_SetButtonColor),
	    HLL_EXPORT(GetButtonR, PE_GetButtonR),
	    HLL_EXPORT(GetButtonG, PE_GetButtonG),
	    HLL_EXPORT(GetButtonB, PE_GetButtonB),
	    HLL_EXPORT(SetButtonFontProperty, PE_SetButtonFontProperty),
	    HLL_EXPORT(GetButtonFontProperty, PE_GetButtonFontProperty),
	    HLL_EXPORT(SetButtonOnCursorSoundNumber, PE_SetButtonOnCursorSoundNumber),
	    HLL_EXPORT(SetButtonClickSoundNumber, PE_SetButtonClickSoundNumber),
	    HLL_EXPORT(GetButtonOnCursorSoundNumber, PE_GetButtonOnCursorSoundNumber),
	    HLL_EXPORT(GetButtonClickSoundNumber, PE_GetButtonClickSoundNumber),
	    HLL_EXPORT(SetButtonCGName, PE_SetButtonCGName),
	    HLL_EXPORT(GetButtonCGName, PE_GetButtonCGName),
	    HLL_EXPORT(SetButtonText, PE_SetButtonText),
	    HLL_EXPORT(GetButtonText, PE_GetButtonText),
	    HLL_EXPORT(SetCheckBoxSize, PE_SetCheckBoxSize),
	    HLL_EXPORT(SetCheckBoxDrag, PE_SetCheckBoxDrag),
	    HLL_EXPORT(IsCheckBoxDrag, PE_IsCheckBoxDrag),
	    /* [D174] 这一族原来是 TODO/空桩，`CommonMenu@SetCheckBoxState`
	     * 一进来就撞 Unimplemented。改成旁路表实现（见 parts.c）。 */
	    HLL_EXPORT(CheckBoxChecked, PE_CheckBoxChecked),
	    /* [D517r] 轮询读走即清 click_pending 保护门（引擎内部读用
	     * PE_IsCheckBoxChecked，不清门 —— 见 PE_PollCheckBoxChecked）。 */
	    HLL_EXPORT(IsCheckBoxChecked, PE_PollCheckBoxChecked),
	    HLL_EXPORT(SetCheckBoxEnable, PE_SetCheckBoxEnable),
	    HLL_EXPORT(IsCheckBoxEnable, PE_IsCheckBoxEnable),
	    HLL_EXPORT(SetCheckBoxColor, PE_SetCheckBoxColor),
	    HLL_EXPORT(GetCheckBoxR, PE_GetCheckBoxR),
	    HLL_EXPORT(GetCheckBoxG, PE_GetCheckBoxG),
	    HLL_EXPORT(GetCheckBoxB, PE_GetCheckBoxB),
	    HLL_EXPORT(SetCheckBoxFontProperty, PE_SetCheckBoxFontProperty),
	    HLL_EXPORT(GetCheckBoxFontProperty, PE_GetCheckBoxFontProperty),
	    HLL_EXPORT(SetCheckBoxOnCursorSoundNumber, PE_SetCheckBoxOnCursorSoundNumber),
	    HLL_EXPORT(SetCheckBoxClickSoundNumber, PE_SetCheckBoxClickSoundNumber),
	    HLL_EXPORT(GetCheckBoxOnCursorSoundNumber, PE_GetCheckBoxOnCursorSoundNumber),
	    HLL_EXPORT(GetCheckBoxClickSoundNumber, PE_GetCheckBoxClickSoundNumber),
	    HLL_EXPORT(SetCheckBoxCGName, PE_SetCheckBoxCGName),
	    HLL_EXPORT(GetCheckBoxCGName, PE_GetCheckBoxCGName),
	    HLL_EXPORT(SetCheckBoxText, PE_SetCheckBoxText),
	    HLL_EXPORT(GetCheckBoxText, PE_GetCheckBoxText),
	    HLL_EXPORT(AddRadioButtonBoxChild, PE_AddRadioButtonBoxChild),
	    HLL_EXPORT(ClearRadioButtonBoxChild, PE_ClearRadioButtonBoxChild),
	    HLL_EXPORT(GetButtonCharSpace, PE_GetButtonCharSpace),
	    HLL_EXPORT(GetButtonFlatName, PE_GetButtonFlatName),
	    HLL_EXPORT(GetButtonLineSpace, PE_GetButtonLineSpace),
	    HLL_EXPORT(GetButtonTextOriginPosMode, PE_GetButtonTextOriginPosMode),
	    HLL_EXPORT(GetCheckBoxButtonHeight, PE_GetCheckBoxButtonHeight),
	    HLL_EXPORT(GetCheckBoxButtonWidth, PE_GetCheckBoxButtonWidth),
	    HLL_EXPORT(GetCheckBoxCharSpace, PE_GetCheckBoxCharSpace),
	    HLL_EXPORT(GetCheckBoxFlatName, PE_GetCheckBoxFlatName),
	    HLL_EXPORT(GetCheckBoxLineSpace, PE_GetCheckBoxLineSpace),
	    HLL_EXPORT(GetCheckBoxOverrideCGName, PE_GetCheckBoxOverrideCGName),
	    HLL_EXPORT(GetCheckBoxOverrideCheckedCGName, PE_GetCheckBoxOverrideCheckedCGName),
	    HLL_EXPORT(GetCheckBoxOverrideCheckedFlatName, PE_GetCheckBoxOverrideCheckedFlatName),
	    HLL_EXPORT(GetCheckBoxOverrideFlatName, PE_GetCheckBoxOverrideFlatName),
	    HLL_EXPORT(GetCheckBoxTextOriginPosMode, PE_GetCheckBoxTextOriginPosMode),
	    HLL_EXPORT(GetRadioButtonBoxChild, PE_GetRadioButtonBoxChild),
	    HLL_EXPORT(IsCheckBoxButtonMode, PE_IsCheckBoxButtonMode),
	    HLL_EXPORT(IsRadioButtonBoxExistGUI, PE_IsRadioButtonBoxExistGUI),
	    HLL_EXPORT(NumofRadioButtonBoxChild, PE_NumofRadioButtonBoxChild),
	    HLL_EXPORT(RemoveRadioButtonBoxChild, PE_RemoveRadioButtonBoxChild),
	    HLL_EXPORT(SetVScrollbarOnCursorSoundNumber, PE_SetVScrollbarOnCursorSoundNumber),
	    HLL_EXPORT(SetVScrollbarClickSoundNumber, PE_SetVScrollbarClickSoundNumber),
	    HLL_EXPORT(GetVScrollbarOnCursorSoundNumber, PE_GetVScrollbarOnCursorSoundNumber),
	    HLL_EXPORT(GetVScrollbarClickSoundNumber, PE_GetVScrollbarClickSoundNumber),
	    HLL_EXPORT(SetVScrollbarSize, PE_SetVScrollbarSize),
	    HLL_EXPORT(SetVScrollbarUpHeight, PE_SetVScrollbarUpHeight),
	    HLL_EXPORT(SetVScrollbarDownHeight, PE_SetVScrollbarDownHeight),
	    HLL_EXPORT(GetVScrollbarUpHeight, PE_GetVScrollbarUpHeight),
	    HLL_EXPORT(GetVScrollbarDownHeight, PE_GetVScrollbarDownHeight),
	    HLL_EXPORT(SetVScrollbarTotalSize, PE_SetVScrollbarTotalSize),
	    HLL_EXPORT(SetVScrollbarViewSize, PE_SetVScrollbarViewSize),
	    HLL_EXPORT(SetVScrollbarScrollPos, PE_SetVScrollbarScrollPos),
	    HLL_EXPORT(SetVScrollbarScrollRate, PE_SetVScrollbarScrollRate),
	    HLL_EXPORT(SetVScrollbarMoveSizeByButton, PE_SetVScrollbarMoveSizeByButton),
	    HLL_EXPORT(GetVScrollbarTotalSize, PE_GetVScrollbarTotalSize),
	    HLL_EXPORT(GetVScrollbarViewSize, PE_GetVScrollbarViewSize),
	    HLL_EXPORT(GetVScrollbarScrollPos, PE_GetVScrollbarScrollPos),
	    HLL_EXPORT(GetVScrollbarScrollRate, PE_GetVScrollbarScrollRate),
	    HLL_EXPORT(GetVScrollbarMoveSizeByButton, PE_GetVScrollbarMoveSizeByButton),
	    HLL_EXPORT(SetVScrollbarCGName, PE_SetVScrollbarCGName),
	    HLL_EXPORT(GetVScrollbarCGName, PE_GetVScrollbarCGName),
	    HLL_EXPORT(SetHScrollbarOnCursorSoundNumber, PE_SetHScrollbarOnCursorSoundNumber),
	    HLL_EXPORT(SetHScrollbarClickSoundNumber, PE_SetHScrollbarClickSoundNumber),
	    HLL_EXPORT(GetHScrollbarOnCursorSoundNumber, PE_GetHScrollbarOnCursorSoundNumber),
	    HLL_EXPORT(GetHScrollbarClickSoundNumber, PE_GetHScrollbarClickSoundNumber),
	    HLL_EXPORT(SetHScrollbarSize, PE_SetHScrollbarSize),
	    HLL_EXPORT(SetHScrollbarLeftWidth, PE_SetHScrollbarLeftWidth),
	    HLL_EXPORT(SetHScrollbarRightWidth, PE_SetHScrollbarRightWidth),
	    HLL_EXPORT(GetHScrollbarLeftWidth, PE_GetHScrollbarLeftWidth),
	    HLL_EXPORT(GetHScrollbarRightWidth, PE_GetHScrollbarRightWidth),
	    HLL_EXPORT(SetHScrollbarTotalSize, PE_SetHScrollbarTotalSize),
	    HLL_EXPORT(SetHScrollbarViewSize, PE_SetHScrollbarViewSize),
	    HLL_EXPORT(SetHScrollbarScrollPos, PE_SetHScrollbarScrollPos),
	    HLL_EXPORT(SetHScrollbarScrollRate, PE_SetHScrollbarScrollRate),
	    HLL_EXPORT(SetHScrollbarMoveSizeByButton, PE_SetHScrollbarMoveSizeByButton),
	    HLL_EXPORT(GetHScrollbarTotalSize, PE_GetHScrollbarTotalSize),
	    HLL_EXPORT(GetHScrollbarViewSize, PE_GetHScrollbarViewSize),
	    HLL_EXPORT(GetHScrollbarScrollPos, PE_GetHScrollbarScrollPos),
	    HLL_EXPORT(GetHScrollbarScrollRate, PE_GetHScrollbarScrollRate),
	    HLL_EXPORT(GetHScrollbarMoveSizeByButton, PE_GetHScrollbarMoveSizeByButton),
	    HLL_EXPORT(SetHScrollbarCGName, PE_SetHScrollbarCGName),
	    HLL_EXPORT(GetHScrollbarCGName, PE_GetHScrollbarCGName),
	    HLL_EXPORT(SetTextBoxSize, PE_SetTextBoxSize),
	    HLL_EXPORT(SetTextBoxFontProperty, PE_SetTextBoxFontProperty),
	    HLL_EXPORT(GetTextBoxFontProperty, PE_GetTextBoxFontProperty),
	    HLL_EXPORT(SetTextBoxText, PE_SetTextBoxText),
	    HLL_EXPORT(GetTextBoxText, PE_GetTextBoxText),
	    HLL_EXPORT(SetTextBoxMaxTextLength, PE_SetTextBoxMaxTextLength),
	    HLL_EXPORT(GetTextBoxMaxTextLength, PE_GetTextBoxMaxTextLength),
	    HLL_EXPORT(SetTextBoxSelectColor, PE_SetTextBoxSelectColor),
	    HLL_EXPORT(GetTextBoxSelectR, PE_GetTextBoxSelectR),
	    HLL_EXPORT(GetTextBoxSelectG, PE_GetTextBoxSelectG),
	    HLL_EXPORT(GetTextBoxSelectB, PE_GetTextBoxSelectB),
	    HLL_EXPORT(SetTextBoxCGName, PE_SetTextBoxCGName),
	    HLL_EXPORT(GetTextBoxCGName, PE_GetTextBoxCGName),
	    HLL_EXPORT(SetTextBoxReadOnly, PE_SetTextBoxReadOnly),
	    HLL_EXPORT(IsTextBoxReadOnly, PE_IsTextBoxReadOnly),
	    HLL_EXPORT(GetTextBoxBGR, PE_GetTextBoxBGR),
	    HLL_EXPORT(GetTextBoxBGG, PE_GetTextBoxBGG),
	    HLL_EXPORT(GetTextBoxBGB, PE_GetTextBoxBGB),
	    HLL_EXPORT(GetTextBoxReadOnlyBGR, PE_GetTextBoxReadOnlyBGR),
	    HLL_EXPORT(GetTextBoxReadOnlyBGG, PE_GetTextBoxReadOnlyBGG),
	    HLL_EXPORT(GetTextBoxReadOnlyBGB, PE_GetTextBoxReadOnlyBGB),
	    HLL_EXPORT(GetTextBoxFrameR, PE_GetTextBoxFrameR),
	    HLL_EXPORT(GetTextBoxFrameG, PE_GetTextBoxFrameG),
	    HLL_EXPORT(GetTextBoxFrameB, PE_GetTextBoxFrameB),
	    HLL_EXPORT(GetTextBoxCharSpace, PE_GetTextBoxCharSpace),
	    HLL_TODO_EXPORT(OpenTextBoxIME, PartsEngine_OpenTextBoxIME),
	    HLL_TODO_EXPORT(CloseTextBoxIME, PartsEngine_CloseTextBoxIME),
	    HLL_EXPORT(SetListBoxSize, PE_SetListBoxSize),
	    HLL_EXPORT(SetListBoxLineHeight, PE_SetListBoxLineHeight),
	    HLL_EXPORT(GetListBoxLineHeight, PE_GetListBoxLineHeight),
	    HLL_EXPORT(SetListBoxMargin, PE_SetListBoxMargin),
	    HLL_EXPORT(GetListBoxWidthMargin, PE_GetListBoxWidthMargin),
	    HLL_EXPORT(GetListBoxHeightMargin, PE_GetListBoxHeightMargin),
	    HLL_EXPORT(SetListBoxCGName, PE_SetListBoxCGName),
	    HLL_EXPORT(GetListBoxCGName, PE_GetListBoxCGName),
	    HLL_EXPORT(SetListBoxScrollPos, PE_SetListBoxScrollPos),
	    HLL_EXPORT(GetListBoxScrollPos, PE_GetListBoxScrollPos),
	    HLL_EXPORT(AddListBoxItem, PE_AddListBoxItem),
	    HLL_EXPORT(InsertListBoxItem, PE_InsertListBoxItem),
	    HLL_EXPORT(SetListBoxItem, PE_SetListBoxItem),
	    HLL_EXPORT(GetListBoxItemCount, PE_GetListBoxItemCount),
	    HLL_EXPORT(GetListBoxItem, PE_GetListBoxItem),
	    HLL_EXPORT(EraseListBoxItem, PE_EraseListBoxItem),
	    HLL_EXPORT(ClearListBoxItem, PE_ClearListBoxItem),
	    HLL_EXPORT(GetListBoxOnCursorItemIndex, PE_GetListBoxOnCursorItemIndex),
	    HLL_EXPORT(GetListBoxOnCursorItem, PE_GetListBoxOnCursorItem),
	    HLL_EXPORT(SetListBoxFontProperty, PE_SetListBoxFontProperty),
	    HLL_EXPORT(GetListBoxFontProperty, PE_GetListBoxFontProperty),
	    HLL_EXPORT(SetListBoxSelectIndex, PE_SetListBoxSelectIndex),
	    HLL_EXPORT(GetListBoxSelectIndex, PE_GetListBoxSelectIndex),
	    HLL_EXPORT(SetListBoxCharSpace, PE_SetListBoxCharSpace),
	    HLL_EXPORT(GetListBoxCharSpace, PE_GetListBoxCharSpace),
	    HLL_EXPORT(SetComboBoxSize, PE_SetComboBoxSize),
	    HLL_EXPORT(SetComboBoxTextMargin, PE_SetComboBoxTextMargin),
	    HLL_EXPORT(GetComboBoxTextWidthMargin, PE_GetComboBoxTextWidthMargin),
	    HLL_EXPORT(GetComboBoxTextHeightMargin, PE_GetComboBoxTextHeightMargin),
	    HLL_EXPORT(SetComboBoxCGName, PE_SetComboBoxCGName),
	    HLL_EXPORT(GetComboBoxCGName, PE_GetComboBoxCGName),
	    HLL_EXPORT(SetComboBoxText, PE_SetComboBoxText),
	    HLL_EXPORT(GetComboBoxText, PE_GetComboBoxText),
	    HLL_EXPORT(AddComboBoxListBoxItem, PE_AddComboBoxListBoxItem),
	    HLL_EXPORT(ClearComboBoxListBoxItem, PE_ClearComboBoxListBoxItem),
	    HLL_EXPORT(EraseComboBoxListBoxItem, PE_EraseComboBoxListBoxItem),
	    HLL_EXPORT(GetComboBoxCharSpace, PE_GetComboBoxCharSpace),
	    HLL_EXPORT(GetComboBoxListBoxCharSpace, PE_GetComboBoxListBoxCharSpace),
	    HLL_EXPORT(GetComboBoxListBoxFontProperty, PE_GetComboBoxListBoxFontProperty),
	    HLL_EXPORT(GetComboBoxListBoxHeight, PE_GetComboBoxListBoxHeight),
	    HLL_EXPORT(GetComboBoxListBoxHeightMargin, PE_GetComboBoxListBoxHeightMargin),
	    HLL_EXPORT(GetComboBoxListBoxItem, PE_GetComboBoxListBoxItem),
	    HLL_EXPORT(GetComboBoxListBoxItemCount, PE_GetComboBoxListBoxItemCount),
	    HLL_EXPORT(GetComboBoxListBoxLineHeight, PE_GetComboBoxListBoxLineHeight),
	    HLL_EXPORT(GetComboBoxListBoxScrollPos, PE_GetComboBoxListBoxScrollPos),
	    HLL_EXPORT(GetComboBoxListBoxSelectIndex, PE_GetComboBoxListBoxSelectIndex),
	    HLL_EXPORT(GetComboBoxListBoxWidthMargin, PE_GetComboBoxListBoxWidthMargin),
	    HLL_EXPORT(GetSpinBoxBGB, PE_GetSpinBoxBGB),
	    HLL_EXPORT(GetSpinBoxBGG, PE_GetSpinBoxBGG),
	    HLL_EXPORT(GetSpinBoxBGR, PE_GetSpinBoxBGR),
	    HLL_EXPORT(GetSpinBoxCGName, PE_GetSpinBoxCGName),
	    HLL_EXPORT(GetSpinBoxCharSpace, PE_GetSpinBoxCharSpace),
	    HLL_EXPORT(GetSpinBoxDecimalPlace, PE_GetSpinBoxDecimalPlace),
	    HLL_EXPORT(GetSpinBoxDefaultValue, PE_GetSpinBoxDefaultValue),
	    HLL_EXPORT(GetSpinBoxFontProperty, PE_GetSpinBoxFontProperty),
	    HLL_EXPORT(GetSpinBoxFrameB, PE_GetSpinBoxFrameB),
	    HLL_EXPORT(GetSpinBoxFrameG, PE_GetSpinBoxFrameG),
	    HLL_EXPORT(GetSpinBoxFrameR, PE_GetSpinBoxFrameR),
	    HLL_EXPORT(GetSpinBoxIncrementalValue, PE_GetSpinBoxIncrementalValue),
	    HLL_EXPORT(GetSpinBoxMaxValue, PE_GetSpinBoxMaxValue),
	    HLL_EXPORT(GetSpinBoxMinValue, PE_GetSpinBoxMinValue),
	    HLL_EXPORT(GetSpinBoxReadOnlyBGB, PE_GetSpinBoxReadOnlyBGB),
	    HLL_EXPORT(GetSpinBoxReadOnlyBGG, PE_GetSpinBoxReadOnlyBGG),
	    HLL_EXPORT(GetSpinBoxReadOnlyBGR, PE_GetSpinBoxReadOnlyBGR),
	    HLL_EXPORT(GetSpinBoxSelectB, PE_GetSpinBoxSelectB),
	    HLL_EXPORT(GetSpinBoxSelectG, PE_GetSpinBoxSelectG),
	    HLL_EXPORT(GetSpinBoxSelectR, PE_GetSpinBoxSelectR),
	    HLL_EXPORT(GetSpinBoxValue, PE_GetSpinBoxValue),
	    HLL_EXPORT(InsertComboBoxListBoxItem, PE_InsertComboBoxListBoxItem),
	    HLL_EXPORT(IsComboBoxOpenListBox, PE_IsComboBoxOpenListBox),
	    HLL_EXPORT(IsSpinBoxReadOnly, PE_IsSpinBoxReadOnly),
	    HLL_EXPORT(SetComboBoxListBoxFontProperty, PE_SetComboBoxListBoxFontProperty),
	    HLL_EXPORT(SetSpinBoxDefaultValue, PE_SetSpinBoxDefaultValue),
	    HLL_EXPORT(SetSpinBoxFontProperty, PE_SetSpinBoxFontProperty),
	    HLL_EXPORT(SetSpinBoxIncrementalValue, PE_SetSpinBoxIncrementalValue),
	    HLL_EXPORT(SetSpinBoxMaxValue, PE_SetSpinBoxMaxValue),
	    HLL_EXPORT(SetSpinBoxMinValue, PE_SetSpinBoxMinValue),
	    HLL_EXPORT(SetSpinBoxValue, PE_SetSpinBoxValue),
	    HLL_EXPORT(SetComboBoxFontProperty, PE_SetComboBoxFontProperty),
	    HLL_EXPORT(GetComboBoxFontProperty, PE_GetComboBoxFontProperty),
	    HLL_EXPORT(SetMultiTextBoxSize, PE_SetMultiTextBoxSize),
	    HLL_EXPORT(GetMultiTextBoxBGB, PE_GetMultiTextBoxBGB),
	    HLL_EXPORT(GetMultiTextBoxBGG, PE_GetMultiTextBoxBGG),
	    HLL_EXPORT(GetMultiTextBoxBGR, PE_GetMultiTextBoxBGR),
	    HLL_EXPORT(GetMultiTextBoxCharSpace, PE_GetMultiTextBoxCharSpace),
	    HLL_EXPORT(GetMultiTextBoxFrameB, PE_GetMultiTextBoxFrameB),
	    HLL_EXPORT(GetMultiTextBoxFrameG, PE_GetMultiTextBoxFrameG),
	    HLL_EXPORT(GetMultiTextBoxFrameR, PE_GetMultiTextBoxFrameR),
	    HLL_EXPORT(GetMultiTextBoxLineSpace, PE_GetMultiTextBoxLineSpace),
	    HLL_EXPORT(GetMultiTextBoxPaddingBottom, PE_GetMultiTextBoxPaddingBottom),
	    HLL_EXPORT(GetMultiTextBoxPaddingLeft, PE_GetMultiTextBoxPaddingLeft),
	    HLL_EXPORT(GetMultiTextBoxPaddingRight, PE_GetMultiTextBoxPaddingRight),
	    HLL_EXPORT(GetMultiTextBoxPaddingTop, PE_GetMultiTextBoxPaddingTop),
	    HLL_EXPORT(GetMultiTextBoxReadOnlyBGB, PE_GetMultiTextBoxReadOnlyBGB),
	    HLL_EXPORT(GetMultiTextBoxReadOnlyBGG, PE_GetMultiTextBoxReadOnlyBGG),
	    HLL_EXPORT(GetMultiTextBoxReadOnlyBGR, PE_GetMultiTextBoxReadOnlyBGR),
	    HLL_EXPORT(IsMultiTextBoxHScrollShow, PE_IsMultiTextBoxHScrollShow),
	    HLL_EXPORT(IsMultiTextBoxReadOnly, PE_IsMultiTextBoxReadOnly),
	    HLL_EXPORT(IsMultiTextBoxVScrollShow, PE_IsMultiTextBoxVScrollShow),
	    HLL_EXPORT(SetMultiTextBoxFontProperty, PE_SetMultiTextBoxFontProperty),
	    HLL_EXPORT(GetMultiTextBoxFontProperty, PE_GetMultiTextBoxFontProperty),
	    HLL_EXPORT(SetMultiTextBoxText, PE_SetMultiTextBoxText),
	    HLL_EXPORT(GetMultiTextBoxText, PE_GetMultiTextBoxText),
	    HLL_EXPORT(SetMultiTextBoxMaxTextLength, PE_SetMultiTextBoxMaxTextLength),
	    HLL_EXPORT(GetMultiTextBoxMaxTextLength, PE_GetMultiTextBoxMaxTextLength),
	    HLL_EXPORT(SetMultiTextBoxSelectColor, PE_SetMultiTextBoxSelectColor),
	    HLL_EXPORT(GetMultiTextBoxSelectR, PE_GetMultiTextBoxSelectR),
	    HLL_EXPORT(GetMultiTextBoxSelectG, PE_GetMultiTextBoxSelectG),
	    HLL_EXPORT(GetMultiTextBoxSelectB, PE_GetMultiTextBoxSelectB),
	    HLL_EXPORT(SetMultiTextBoxCGName, PE_SetMultiTextBoxCGName),
	    HLL_EXPORT(GetMultiTextBoxCGName, PE_GetMultiTextBoxCGName),
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
	     * 子控件全挤在父容器原点（战况报告屏三行数字叠在左上就是这么来的）。
	     * [D517l] 补充核实：AIN 的 padding 包装方法（0x1d9fca 一带）运行期
	     * **零调用**（d662 全量 trace），220 个 pact 的 EX `パディング`
	     * 也全零（编辑器默认值）—— 这条链当前没有真实消费者。 */
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
	    HLL_EXPORT(Parts_ChangeFlatSound, PE_Parts_ChangeFlatSound_fwd),
	    HLL_EXPORT(NumofFlatSound, PE_NumofFlatSound_fwd),
	    HLL_EXPORT(GetFlatSoundName, PE_GetFlatSoundName_fwd),
	    HLL_EXPORT(GetProxyFlatSound, PE_GetProxyFlatSound_fwd),
	    HLL_EXPORT(ReturnChangeFlatSound, PE_ReturnChangeFlatSound_fwd),
	    HLL_EXPORT(ClearChangeFlatSound, PE_ClearChangeFlatSound_fwd),
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
	    HLL_EXPORT(GetPartsConstructionProcess, PE_GetPartsConstructionProcess),
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
	    HLL_EXPORT(IsEnableInputProcess, PartsEngine_IsEnableInputProcess),
	    HLL_EXPORT(Parts_SetAnchor, PE_Noop_Parts_SetAnchor),
	    HLL_EXPORT(Parts_SetFlatBeginSeekFrame, PE_Noop_Parts_SetFlatBeginSeekFrame),
	    HLL_EXPORT(Parts_SetFlatEnableLayer, PE_Noop_Parts_SetFlatEnableLayer),
	    HLL_EXPORT(Parts_SetHGaugeReverse, PE_Noop_Parts_SetHGaugeReverse),
	    HLL_EXPORT(Parts_SetLockUpdate, PE_Noop_Parts_SetLockUpdate),
	    HLL_EXPORT(Parts_SetPosForRate, PE_Noop_Parts_SetPosForRate),
	    HLL_EXPORT(Parts_SetRubyCharSpace, PE_Noop_Parts_SetRubyCharSpace),
	    HLL_EXPORT(Parts_SetSoundName, PE_Parts_SetSoundName_fwd),
	    HLL_EXPORT(Parts_GetSoundName, PE_Parts_GetSoundName_hll),
	    HLL_EXPORT(Parts_PlaySound, PE_Parts_PlaySound_fwd),
	    HLL_EXPORT(GetSystemOverlayController, PE_GetSystemOverlayController),
	    HLL_EXPORT(CancelFormClose, PE_CancelFormClose),
	    HLL_EXPORT(Parts_SetSwipeType, PE_Noop_Parts_SetSwipeType),
	    HLL_EXPORT(Parts_SetTextEnableTag, PE_Noop_Parts_SetTextEnableTag),
	    HLL_EXPORT(Parts_SetTextPos, PE_Noop_Parts_SetTextPos),
	    HLL_EXPORT(Parts_SetTextShowTime, PE_Noop_Parts_SetTextShowTime),
	    HLL_EXPORT(Parts_SetUpdateLookAtPos, PE_Noop_Parts_SetUpdateLookAtPos),
	    HLL_EXPORT(Parts_SetVGaugeReverse, PE_Noop_Parts_SetVGaugeReverse),
	    HLL_EXPORT(Parts_SetWheelable, PE_Noop_Parts_SetWheelable),
	    HLL_EXPORT(SetActiveController, PE_SetActiveController),
	    HLL_EXPORT(SetButtonCharSpace, PE_SetButtonCharSpace),
	    HLL_EXPORT(SetButtonFlatName, PE_SetButtonFlatName),
	    HLL_EXPORT(SetButtonLineSpace, PE_SetButtonLineSpace),
	    HLL_EXPORT(SetButtonTextOriginPosMode, PE_SetButtonTextOriginPosMode),
	    HLL_EXPORT(SetCheckBoxButtonMode, PE_SetCheckBoxButtonMode),
	    HLL_EXPORT(SetCheckBoxCharSpace, PE_SetCheckBoxCharSpace),
	    HLL_EXPORT(SetCheckBoxFlatName, PE_SetCheckBoxFlatName),
	    HLL_EXPORT(SetCheckBoxLineSpace, PE_SetCheckBoxLineSpace),
	    HLL_EXPORT(SetCheckBoxOverrideCGName, PE_SetCheckBoxOverrideCGName),
	    HLL_EXPORT(SetCheckBoxOverrideCheckedCGName, PE_SetCheckBoxOverrideCheckedCGName),
	    HLL_EXPORT(SetCheckBoxOverrideCheckedFlatName, PE_SetCheckBoxOverrideCheckedFlatName),
	    HLL_EXPORT(SetCheckBoxOverrideFlatName, PE_SetCheckBoxOverrideFlatName),
	    HLL_EXPORT(SetCheckBoxTextOriginPosMode, PE_SetCheckBoxTextOriginPosMode),
	    HLL_EXPORT(SetClickMissSoundName, PE_SetClickMissSoundName_fwd),
	    HLL_EXPORT(GetClickMissSoundName, PE_GetClickMissSoundName_fwd),
	    HLL_EXPORT(SetComboBoxCharSpace, PE_SetComboBoxCharSpace),
	    HLL_EXPORT(SetComboBoxListBoxCharSpace, PE_SetComboBoxListBoxCharSpace),
	    HLL_EXPORT(SetComboBoxListBoxHeight, PE_SetComboBoxListBoxHeight),
	    HLL_EXPORT(SetComboBoxListBoxItem, PE_SetComboBoxListBoxItem),
	    HLL_EXPORT(SetComboBoxListBoxLineHeight, PE_SetComboBoxListBoxLineHeight),
	    HLL_EXPORT(SetComboBoxListBoxMargin, PE_SetComboBoxListBoxMargin),
	    HLL_EXPORT(SetComboBoxListBoxScrollPos, PE_SetComboBoxListBoxScrollPos),
	    HLL_EXPORT(SetComboBoxListBoxSelectIndex, PE_SetComboBoxListBoxSelectIndex),
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
	    HLL_EXPORT(SetHScrollbarFlatName, PE_SetHScrollbarFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideBGCGName, PE_SetHScrollbarOverrideBGCGName),
	    HLL_EXPORT(SetHScrollbarOverrideBGFlatName, PE_SetHScrollbarOverrideBGFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideBarCGName, PE_SetHScrollbarOverrideBarCGName),
	    HLL_EXPORT(SetHScrollbarOverrideBarFlatName, PE_SetHScrollbarOverrideBarFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideLeftCGName, PE_SetHScrollbarOverrideLeftCGName),
	    HLL_EXPORT(SetHScrollbarOverrideLeftFlatName, PE_SetHScrollbarOverrideLeftFlatName),
	    HLL_EXPORT(SetHScrollbarOverrideRightCGName, PE_SetHScrollbarOverrideRightCGName),
	    HLL_EXPORT(SetHScrollbarOverrideRightFlatName, PE_SetHScrollbarOverrideRightFlatName),
	    HLL_EXPORT(SetKeyWaitCGName, PE_SetKeyWaitCGName),
	    HLL_EXPORT(SetKeyWaitFlatName, PE_SetKeyWaitFlatName),
	    HLL_EXPORT(SetKeyWaitPos, PE_SetKeyWaitPos),
	    HLL_EXPORT(SetKeyWaitShow, PE_SetKeyWaitShow),
	    HLL_EXPORT(SetLayoutBoxReturnSizeForRate, PE_Noop_SetLayoutBoxReturnSizeForRate),
	    HLL_EXPORT(SetLockInputState, PE_Noop_SetLockInputState),
	    HLL_EXPORT(SetMessageWindowActive, PE_SetMessageWindowActive),
	    HLL_EXPORT(SetMessageWindowCGName, PE_SetMessageWindowCGName),
	    HLL_EXPORT(SetMessageWindowFlatName, PE_SetMessageWindowFlatName),
	    HLL_EXPORT(GetMessageWindowFlatName, PE_GetMessageWindowFlatName),
	    HLL_EXPORT(GetMessageWindowCGName, PE_GetMessageWindowCGName),
	    HLL_EXPORT(GetKeyWaitFlatName, PE_GetKeyWaitFlatName),
	    HLL_EXPORT(GetMessageWindowFlatShowWaitFrameNumber, PE_GetMessageWindowFlatShowWaitFrameNumber),
	    HLL_EXPORT(IsOverMessageWindowFlatShowWaitFrame, PE_IsOverMessageWindowFlatShowWaitFrame),
	    HLL_EXPORT(BackMessageWindowFlatBeginFrame, PE_BackMessageWindowFlatBeginFrame),
	    HLL_EXPORT(StepMessageWindowFlatFinalFrame, PE_StepMessageWindowFlatFinalFrame),
	    HLL_EXPORT(SetMessageWindowFlatShowWaitFrameNumber, PE_SetMessageWindowFlatShowWaitFrameNumber),
	    HLL_EXPORT(SetMessageWindowInactiveMultipleColor, PE_SetMessageWindowInactiveMultipleColor),
	    HLL_EXPORT(SetMessageWindowText, PE_SetMessageWindowText),
	    HLL_EXPORT(SetMessageWindowTextFont, PE_SetMessageWindowTextFont),
	    HLL_EXPORT(FixMessageWindowText, PE_FixMessageWindowText),
	    HLL_EXPORT(IsFixedMessageWindowText, PE_IsFixedMessageWindowText),
	    HLL_EXPORT(SetMessageWindowTextArea, PE_SetMessageWindowTextArea),
	    HLL_EXPORT(SetMessageWindowTextOriginPosMode, PE_SetMessageWindowTextOriginPosMode),
	    HLL_EXPORT(SetMessageWindowTextSpace, PE_SetMessageWindowTextSpace),
	    HLL_EXPORT(SetMessageWindowTextSpeed, PE_SetMessageWindowTextSpeed),
	    HLL_EXPORT(SetMotionData, PE_Noop_SetMotionData),
	    HLL_EXPORT(CreatePartsMovie, PE_CreatePartsMovie),
	    HLL_EXPORT(ReleasePartsMovie, PE_ReleasePartsMovie),
	    HLL_EXPORT(PlayPartsMovie, PE_PlayPartsMovie),
	    HLL_EXPORT(IsEndPartsMovie, PE_IsEndPartsMovie),
	    HLL_EXPORT(GetPartsMovieEndTime, PE_GetPartsMovieEndTime),
	    HLL_EXPORT(GetPartsMovieCurrentTime, PE_GetPartsMovieCurrentTime),
	    HLL_EXPORT(SetMovieTime, PE_Noop_SetMovieTime),
	    HLL_EXPORT(SetMultiTextBoxBGColor, PE_SetMultiTextBoxBGColor),
	    HLL_EXPORT(SetMultiTextBoxCharSpace, PE_SetMultiTextBoxCharSpace),
	    HLL_EXPORT(SetMultiTextBoxFrameColor, PE_SetMultiTextBoxFrameColor),
	    HLL_EXPORT(SetMultiTextBoxHScrollShow, PE_SetMultiTextBoxHScrollShow),
	    HLL_EXPORT(SetMultiTextBoxLineSpace, PE_SetMultiTextBoxLineSpace),
	    HLL_EXPORT(SetMultiTextBoxPadding, PE_SetMultiTextBoxPadding),
	    HLL_EXPORT(SetMultiTextBoxReadOnly, PE_SetMultiTextBoxReadOnly),
	    HLL_EXPORT(SetMultiTextBoxReadOnlyBGColor, PE_SetMultiTextBoxReadOnlyBGColor),
	    HLL_EXPORT(SetMultiTextBoxVScrollShow, PE_SetMultiTextBoxVScrollShow),
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
	    HLL_EXPORT(SetSpinBoxBGColor, PE_SetSpinBoxBGColor),
	    HLL_EXPORT(SetSpinBoxCGName, PE_SetSpinBoxCGName),
	    HLL_EXPORT(SetSpinBoxCharSpace, PE_SetSpinBoxCharSpace),
	    HLL_EXPORT(SetSpinBoxDecimalPlace, PE_SetSpinBoxDecimalPlace),
	    HLL_EXPORT(SetSpinBoxFrameColor, PE_SetSpinBoxFrameColor),
	    HLL_EXPORT(SetSpinBoxReadOnly, PE_SetSpinBoxReadOnly),
	    HLL_EXPORT(SetSpinBoxReadOnlyBGColor, PE_SetSpinBoxReadOnlyBGColor),
	    HLL_EXPORT(SetSpinBoxSelectColor, PE_SetSpinBoxSelectColor),
	    HLL_EXPORT(SetSpinBoxSize, PE_SetSpinBoxSize),
	    HLL_EXPORT(SetTextBoxBGColor, PE_SetTextBoxBGColor),
	    HLL_EXPORT(SetTextBoxCharSpace, PE_SetTextBoxCharSpace),
	    HLL_EXPORT(SetTextBoxFrameColor, PE_SetTextBoxFrameColor),
	    HLL_EXPORT(SetTextBoxReadOnly, PE_SetTextBoxReadOnly),
	    HLL_EXPORT(SetTextBoxReadOnlyBGColor, PE_SetTextBoxReadOnlyBGColor),
	    HLL_EXPORT(SetUserComponentData, PE_SetUserComponentData_str),
	    HLL_EXPORT(SetUserComponentName, PE_SetUserComponentName_str),
	    HLL_EXPORT(GetUserComponentData, PE_GetUserComponentData_str),
	    HLL_EXPORT(GetUserComponentName, PE_GetUserComponentName_str),
	    HLL_EXPORT(SetVScrollTotalSizeLinkNumber, PE_Noop_SetVScrollTotalSizeLinkNumber),
	    HLL_EXPORT(SetVScrollViewSizeLinkNumber, PE_Noop_SetVScrollViewSizeLinkNumber),
	    HLL_EXPORT(SetVScrollbarFlatName, PE_SetVScrollbarFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideBGCGName, PE_SetVScrollbarOverrideBGCGName),
	    HLL_EXPORT(SetVScrollbarOverrideBGFlatName, PE_SetVScrollbarOverrideBGFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideBarCGName, PE_SetVScrollbarOverrideBarCGName),
	    HLL_EXPORT(SetVScrollbarOverrideBarFlatName, PE_SetVScrollbarOverrideBarFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideDownCGName, PE_SetVScrollbarOverrideDownCGName),
	    HLL_EXPORT(SetVScrollbarOverrideDownFlatName, PE_SetVScrollbarOverrideDownFlatName),
	    HLL_EXPORT(SetVScrollbarOverrideUpCGName, PE_SetVScrollbarOverrideUpCGName),
	    HLL_EXPORT(SetVScrollbarOverrideUpFlatName, PE_SetVScrollbarOverrideUpFlatName),
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

/* [D396] 控件状态自检：`XSYS4_WIDGET_SELFTEST=1` 时启动后跑一次。
 *
 * 为什么需要它：填桩的验收不能只是「日志里不再报 Unimplemented」——
 * 那只能证明函数被注册了。真正的语义是**往返一致**：AIN 存档前 Get 出来、
 * 读档后 Set 回去，值必须一个字节都不差。这里对每个属性做一次
 * 「set 一个非默认值 → get 回来比对」，失败项计数返回。
 *
 * 用一个不存在的部件号造件（`parts_get` 按需创建），测完释放。
 */
#define SELFTEST_NO 0x7F000001
int PartsEngine_WidgetSelfTest(void)
{
	int fail = 0;
#define CHECK(cond, what) do { \
	if (!(cond)) { NOTICE("[wtest] FAIL %s", what); fail++; } \
} while (0)
#define CHECK_STR(got, want, what) do { \
	struct string *g_ = (got); \
	CHECK(g_ && !strcmp(g_->text, want), what); \
	if (g_) free_string(g_); \
} while (0)

	parts_get(SELFTEST_NO);

	/* --- TextBox --- */
	struct string *v = cstr_to_string("\x82\xa0\x82\xa2");
	PE_SetTextBoxText(SELFTEST_NO, v);
	CHECK_STR(PE_GetTextBoxText(SELFTEST_NO), "\x82\xa0\x82\xa2", "TextBox.Text");
	free_string(v);

	PE_SetTextBoxMaxTextLength(SELFTEST_NO, 42);
	CHECK(PE_GetTextBoxMaxTextLength(SELFTEST_NO) == 42, "TextBox.MaxTextLength");

	PE_SetTextBoxReadOnly(SELFTEST_NO, true);
	CHECK(PE_IsTextBoxReadOnly(SELFTEST_NO), "TextBox.ReadOnly");

	PE_SetTextBoxBGColor(SELFTEST_NO, 1, 2, 3);
	CHECK(PE_GetTextBoxBGR(SELFTEST_NO) == 1 && PE_GetTextBoxBGG(SELFTEST_NO) == 2 &&
	      PE_GetTextBoxBGB(SELFTEST_NO) == 3, "TextBox.BGColor");

	PE_SetTextBoxReadOnlyBGColor(SELFTEST_NO, 4, 5, 6);
	CHECK(PE_GetTextBoxReadOnlyBGR(SELFTEST_NO) == 4 &&
	      PE_GetTextBoxReadOnlyBGG(SELFTEST_NO) == 5 &&
	      PE_GetTextBoxReadOnlyBGB(SELFTEST_NO) == 6, "TextBox.ReadOnlyBGColor");

	PE_SetTextBoxFrameColor(SELFTEST_NO, 7, 8, 9);
	CHECK(PE_GetTextBoxFrameR(SELFTEST_NO) == 7 && PE_GetTextBoxFrameG(SELFTEST_NO) == 8 &&
	      PE_GetTextBoxFrameB(SELFTEST_NO) == 9, "TextBox.FrameColor");

	PE_SetTextBoxSelectColor(SELFTEST_NO, 10, 11, 12);
	CHECK(PE_GetTextBoxSelectR(SELFTEST_NO) == 10 &&
	      PE_GetTextBoxSelectG(SELFTEST_NO) == 11 &&
	      PE_GetTextBoxSelectB(SELFTEST_NO) == 12, "TextBox.SelectColor");

	PE_SetTextBoxCharSpace(SELFTEST_NO, -3);
	CHECK(PE_GetTextBoxCharSpace(SELFTEST_NO) == -3, "TextBox.CharSpace");

	v = cstr_to_string("bg.cg");
	PE_SetTextBoxCGName(SELFTEST_NO, v);
	CHECK_STR(PE_GetTextBoxCGName(SELFTEST_NO), "bg.cg", "TextBox.CGName");
	free_string(v);

	/* 字体走 parts_text，需要该状态是 TEXT 才有意义 —— 造一个文本状态。 */
	parts_get_text(parts_get(SELFTEST_NO), 0);
	PE_SetTextBoxFontProperty(SELFTEST_NO, 3, 24, 100, 110, 120, 0.5f, 1, 2, 3, 1.5f);
	int ft = -1, fs = -1, fr = -1, fg = -1, fb = -1, er = -1, eg = -1, eb = -1;
	float bw = -1, ew = -1;
	PE_GetTextBoxFontProperty(SELFTEST_NO, &ft, &fs, &fr, &fg, &fb, &bw,
				  &er, &eg, &eb, &ew);
	CHECK(ft == 3 && fs == 24 && fr == 100 && fg == 110 && fb == 120,
	      "TextBox.Font(type/size/color)");
	CHECK(er == 1 && eg == 2 && eb == 3, "TextBox.Font(edge color)");
	CHECK(bw == 0.5f && ew == 1.5f, "TextBox.Font(weights)");

	/* --- ListBox：结构 + 索引语义（0-based） --- */
	struct string *it;
	it = cstr_to_string("item0"); PE_AddListBoxItem(SELFTEST_NO, it); free_string(it);
	it = cstr_to_string("item1"); PE_AddListBoxItem(SELFTEST_NO, it); free_string(it);
	it = cstr_to_string("item2"); PE_AddListBoxItem(SELFTEST_NO, it); free_string(it);
	CHECK(PE_GetListBoxItemCount(SELFTEST_NO) == 3, "ListBox.Count after 3 Add");
	CHECK_STR(PE_GetListBoxItem(SELFTEST_NO, 0), "item0", "ListBox.Item[0]");
	CHECK_STR(PE_GetListBoxItem(SELFTEST_NO, 2), "item2", "ListBox.Item[2]");
	CHECK_STR(PE_GetListBoxItem(SELFTEST_NO, 3), "", "ListBox.Item[3] 越界为空");
	CHECK_STR(PE_GetListBoxItem(SELFTEST_NO, -1), "", "ListBox.Item[-1] 越界为空");

	it = cstr_to_string("ins"); PE_InsertListBoxItem(SELFTEST_NO, 1, it); free_string(it);
	CHECK(PE_GetListBoxItemCount(SELFTEST_NO) == 4, "ListBox.Count after Insert");
	CHECK_STR(PE_GetListBoxItem(SELFTEST_NO, 1), "ins", "ListBox.Insert@1");
	CHECK_STR(PE_GetListBoxItem(SELFTEST_NO, 2), "item1", "ListBox.Insert 后移");

	it = cstr_to_string("rep"); PE_SetListBoxItem(SELFTEST_NO, 0, it); free_string(it);
	CHECK_STR(PE_GetListBoxItem(SELFTEST_NO, 0), "rep", "ListBox.Set@0");

	PE_EraseListBoxItem(SELFTEST_NO, 1);
	CHECK(PE_GetListBoxItemCount(SELFTEST_NO) == 3, "ListBox.Count after Erase");
	CHECK_STR(PE_GetListBoxItem(SELFTEST_NO, 1), "item1", "ListBox.Erase@1");

	PE_SetListBoxSelectIndex(SELFTEST_NO, 2);
	CHECK(PE_GetListBoxSelectIndex(SELFTEST_NO) == 2, "ListBox.SelectIndex");
	PE_EraseListBoxItem(SELFTEST_NO, 0);
	CHECK(PE_GetListBoxSelectIndex(SELFTEST_NO) == 1, "ListBox.SelectIndex 随删前移");

	PE_SetListBoxLineHeight(SELFTEST_NO, 27);
	PE_SetListBoxMargin(SELFTEST_NO, 5, 6);
	PE_SetListBoxScrollPos(SELFTEST_NO, 3);
	PE_SetListBoxCharSpace(SELFTEST_NO, 2);
	CHECK(PE_GetListBoxLineHeight(SELFTEST_NO) == 27, "ListBox.LineHeight");
	CHECK(PE_GetListBoxWidthMargin(SELFTEST_NO) == 5 &&
	      PE_GetListBoxHeightMargin(SELFTEST_NO) == 6, "ListBox.Margin");
	CHECK(PE_GetListBoxScrollPos(SELFTEST_NO) == 3, "ListBox.ScrollPos");
	CHECK(PE_GetListBoxCharSpace(SELFTEST_NO) == 2, "ListBox.CharSpace");

	PE_ClearListBoxItem(SELFTEST_NO);
	CHECK(PE_GetListBoxItemCount(SELFTEST_NO) == 0, "ListBox.Count after Clear");

	/* --- ComboBox：显示框 + 内嵌列表框 --- */
	it = cstr_to_string("chosen"); PE_SetComboBoxText(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetComboBoxText(SELFTEST_NO), "chosen", "ComboBox.Text");

	it = cstr_to_string("c0"); PE_AddComboBoxListBoxItem(SELFTEST_NO, it); free_string(it);
	it = cstr_to_string("c1"); PE_AddComboBoxListBoxItem(SELFTEST_NO, it); free_string(it);
	CHECK(PE_GetComboBoxListBoxItemCount(SELFTEST_NO) == 2, "ComboBox.LB.Count");
	CHECK_STR(PE_GetComboBoxListBoxItem(SELFTEST_NO, 1), "c1", "ComboBox.LB.Item[1]");
	it = cstr_to_string("c0x");
	PE_SetComboBoxListBoxItem(SELFTEST_NO, 0, it); free_string(it);
	CHECK_STR(PE_GetComboBoxListBoxItem(SELFTEST_NO, 0), "c0x", "ComboBox.LB.Set@0");

	PE_SetComboBoxTextMargin(SELFTEST_NO, 4, 5);
	PE_SetComboBoxListBoxHeight(SELFTEST_NO, 120);
	PE_SetComboBoxListBoxLineHeight(SELFTEST_NO, 22);
	PE_SetComboBoxListBoxMargin(SELFTEST_NO, 7, 8);
	PE_SetComboBoxListBoxScrollPos(SELFTEST_NO, 2);
	PE_SetComboBoxListBoxSelectIndex(SELFTEST_NO, 1);
	PE_SetComboBoxCharSpace(SELFTEST_NO, 3);
	PE_SetComboBoxListBoxCharSpace(SELFTEST_NO, 4);
	CHECK(PE_GetComboBoxTextWidthMargin(SELFTEST_NO) == 4 &&
	      PE_GetComboBoxTextHeightMargin(SELFTEST_NO) == 5, "ComboBox.TextMargin");
	CHECK(PE_GetComboBoxListBoxHeight(SELFTEST_NO) == 120, "ComboBox.LB.Height");
	CHECK(PE_GetComboBoxListBoxLineHeight(SELFTEST_NO) == 22, "ComboBox.LB.LineHeight");
	CHECK(PE_GetComboBoxListBoxWidthMargin(SELFTEST_NO) == 7 &&
	      PE_GetComboBoxListBoxHeightMargin(SELFTEST_NO) == 8, "ComboBox.LB.Margin");
	CHECK(PE_GetComboBoxListBoxScrollPos(SELFTEST_NO) == 2, "ComboBox.LB.ScrollPos");
	CHECK(PE_GetComboBoxListBoxSelectIndex(SELFTEST_NO) == 1, "ComboBox.LB.SelectIndex");
	CHECK(PE_GetComboBoxCharSpace(SELFTEST_NO) == 3, "ComboBox.CharSpace");
	CHECK(PE_GetComboBoxListBoxCharSpace(SELFTEST_NO) == 4, "ComboBox.LB.CharSpace");
	CHECK(!PE_IsComboBoxOpenListBox(SELFTEST_NO), "ComboBox.OpenListBox 应为 false");

	/* --- SpinBox（数值是 float，见 spin_* 注释） --- */
	PE_SetSpinBoxValue(SELFTEST_NO, 1.5f);
	PE_SetSpinBoxDefaultValue(SELFTEST_NO, 2.25f);
	PE_SetSpinBoxMinValue(SELFTEST_NO, -3.75f);
	PE_SetSpinBoxMaxValue(SELFTEST_NO, 99.5f);
	PE_SetSpinBoxIncrementalValue(SELFTEST_NO, 0.5f);
	PE_SetSpinBoxDecimalPlace(SELFTEST_NO, 2);
	PE_SetSpinBoxReadOnly(SELFTEST_NO, true);
	CHECK(PE_GetSpinBoxValue(SELFTEST_NO) == 1.5f, "SpinBox.Value");
	CHECK(PE_GetSpinBoxDefaultValue(SELFTEST_NO) == 2.25f, "SpinBox.DefaultValue");
	CHECK(PE_GetSpinBoxMinValue(SELFTEST_NO) == -3.75f, "SpinBox.MinValue");
	CHECK(PE_GetSpinBoxMaxValue(SELFTEST_NO) == 99.5f, "SpinBox.MaxValue");
	CHECK(PE_GetSpinBoxIncrementalValue(SELFTEST_NO) == 0.5f, "SpinBox.IncrementalValue");
	CHECK(PE_GetSpinBoxDecimalPlace(SELFTEST_NO) == 2, "SpinBox.DecimalPlace");
	CHECK(PE_IsSpinBoxReadOnly(SELFTEST_NO), "SpinBox.ReadOnly");
	PE_SetSpinBoxBGColor(SELFTEST_NO, 21, 22, 23);
	CHECK(PE_GetSpinBoxBGR(SELFTEST_NO) == 21 && PE_GetSpinBoxBGG(SELFTEST_NO) == 22 &&
	      PE_GetSpinBoxBGB(SELFTEST_NO) == 23, "SpinBox.BGColor");

	/* --- Button --- */
	it = cstr_to_string("OK"); PE_SetButtonText(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetButtonText(SELFTEST_NO), "OK", "Button.Text");
	PE_SetButtonColor(SELFTEST_NO, 31, 32, 33);
	CHECK(PE_GetButtonR(SELFTEST_NO) == 31 && PE_GetButtonG(SELFTEST_NO) == 32 &&
	      PE_GetButtonB(SELFTEST_NO) == 33, "Button.Color");
	PE_SetButtonEnable(SELFTEST_NO, true);
	CHECK(PE_IsButtonEnable(SELFTEST_NO), "Button.Enable");
	PE_SetButtonCharSpace(SELFTEST_NO, 1);
	PE_SetButtonLineSpace(SELFTEST_NO, 2);
	PE_SetButtonTextOriginPosMode(SELFTEST_NO, 3);
	CHECK(PE_GetButtonCharSpace(SELFTEST_NO) == 1, "Button.CharSpace");
	CHECK(PE_GetButtonLineSpace(SELFTEST_NO) == 2, "Button.LineSpace");
	CHECK(PE_GetButtonTextOriginPosMode(SELFTEST_NO) == 3, "Button.TextOriginPosMode");
	it = cstr_to_string("btn.cg"); PE_SetButtonCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetButtonCGName(SELFTEST_NO), "btn.cg", "Button.CGName");
	it = cstr_to_string("btn.fl"); PE_SetButtonFlatName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetButtonFlatName(SELFTEST_NO), "btn.fl", "Button.FlatName");
	PE_SetButtonDrag(SELFTEST_NO, true);
	CHECK(PE_IsButtonDrag(SELFTEST_NO), "Button.Drag");
	PE_SetButtonPixelDecide(SELFTEST_NO, true);
	CHECK(PE_IsButtonPixelDecide(SELFTEST_NO), "Button.PixelDecide");

	/* --- CheckBox --- */
	PE_CheckBoxChecked(SELFTEST_NO, true);
	CHECK(PE_IsCheckBoxChecked(SELFTEST_NO), "CheckBox.Checked");
	PE_SetCheckBoxEnable(SELFTEST_NO, true);
	CHECK(PE_IsCheckBoxEnable(SELFTEST_NO), "CheckBox.Enable");
	PE_SetCheckBoxColor(SELFTEST_NO, 41, 42, 43);
	CHECK(PE_GetCheckBoxR(SELFTEST_NO) == 41 && PE_GetCheckBoxG(SELFTEST_NO) == 42 &&
	      PE_GetCheckBoxB(SELFTEST_NO) == 43, "CheckBox.Color");
	it = cstr_to_string("cb.cg"); PE_SetCheckBoxCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetCheckBoxCGName(SELFTEST_NO), "cb.cg", "CheckBox.CGName");
	it = cstr_to_string("cb.ov"); PE_SetCheckBoxOverrideCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetCheckBoxOverrideCGName(SELFTEST_NO), "cb.ov", "CheckBox.OverrideCGName");
	it = cstr_to_string("cb.ovc");
	PE_SetCheckBoxOverrideCheckedCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetCheckBoxOverrideCheckedCGName(SELFTEST_NO), "cb.ovc",
		  "CheckBox.OverrideCheckedCGName");
	it = cstr_to_string("cb.fl"); PE_SetCheckBoxFlatName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetCheckBoxFlatName(SELFTEST_NO), "cb.fl", "CheckBox.FlatName");
	it = cstr_to_string("cb.ofl"); PE_SetCheckBoxOverrideFlatName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetCheckBoxOverrideFlatName(SELFTEST_NO), "cb.ofl",
		  "CheckBox.OverrideFlatName");
	it = cstr_to_string("cb.oFl");
	PE_SetCheckBoxOverrideCheckedFlatName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetCheckBoxOverrideCheckedFlatName(SELFTEST_NO), "cb.oFl",
		  "CheckBox.OverrideCheckedFlatName");
	it = cstr_to_string("label"); PE_SetCheckBoxText(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetCheckBoxText(SELFTEST_NO), "label", "CheckBox.Text");
	PE_SetCheckBoxTextOriginPosMode(SELFTEST_NO, 2);
	CHECK(PE_GetCheckBoxTextOriginPosMode(SELFTEST_NO) == 2, "CheckBox.TextOriginPosMode");
	PE_SetCheckBoxButtonMode(SELFTEST_NO, true);
	CHECK(PE_IsCheckBoxButtonMode(SELFTEST_NO), "CheckBox.ButtonMode");
	PE_SetCheckBoxCharSpace(SELFTEST_NO, 4);
	PE_SetCheckBoxLineSpace(SELFTEST_NO, 5);
	CHECK(PE_GetCheckBoxCharSpace(SELFTEST_NO) == 4, "CheckBox.CharSpace");
	CHECK(PE_GetCheckBoxLineSpace(SELFTEST_NO) == 5, "CheckBox.LineSpace");
	PE_SetCheckBoxDrag(SELFTEST_NO, true);
	CHECK(PE_IsCheckBoxDrag(SELFTEST_NO), "CheckBox.Drag");

	/* --- RadioButtonBox：成员表 --- */
	PE_AddRadioButtonBoxChild(SELFTEST_NO, 111);
	PE_AddRadioButtonBoxChild(SELFTEST_NO, 222);
	PE_AddRadioButtonBoxChild(SELFTEST_NO, 333);
	PE_AddRadioButtonBoxChild(SELFTEST_NO, 222);   /* 重复不应增加 */
	CHECK(PE_NumofRadioButtonBoxChild(SELFTEST_NO) == 3, "Radio.Count（去重）");
	CHECK(PE_GetRadioButtonBoxChild(SELFTEST_NO, 1) == 222, "Radio.Child[1]");
	CHECK(PE_IsRadioButtonBoxExistGUI(SELFTEST_NO, 333), "Radio.ExistGUI(333)");
	CHECK(!PE_IsRadioButtonBoxExistGUI(SELFTEST_NO, 444), "Radio.ExistGUI(444)=false");
	PE_RemoveRadioButtonBoxChild(SELFTEST_NO, 111);
	CHECK(PE_NumofRadioButtonBoxChild(SELFTEST_NO) == 2 &&
	      PE_GetRadioButtonBoxChild(SELFTEST_NO, 0) == 222, "Radio.Remove(111)");
	PE_ClearRadioButtonBoxChild(SELFTEST_NO);
	CHECK(PE_NumofRadioButtonBoxChild(SELFTEST_NO) == 0, "Radio.Clear");

	/* --- MessageWindow --- */
	PE_SetMessageWindowActive(SELFTEST_NO, true);
	it = cstr_to_string("mw.cg"); PE_SetMessageWindowCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetMessageWindowCGName(SELFTEST_NO), "mw.cg", "MsgWin.CGName");
	it = cstr_to_string("mw.fl"); PE_SetMessageWindowFlatName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetMessageWindowFlatName(SELFTEST_NO), "mw.fl", "MsgWin.FlatName");
	PE_SetMessageWindowFlatShowWaitFrameNumber(SELFTEST_NO, 9);
	CHECK(PE_GetMessageWindowFlatShowWaitFrameNumber(SELFTEST_NO) == 9,
	      "MsgWin.FlatShowWaitFrameNumber");
	PE_SetMessageWindowTextArea(SELFTEST_NO, 11, 12, 800, 200);
	int ax = 0, ay = 0, aw = 0, ah = 0;
	PE_GetMessageWindowTextArea(SELFTEST_NO, &ax, &ay, &aw, &ah);
	CHECK(ax == 11 && ay == 12 && aw == 800 && ah == 200, "MsgWin.TextArea");
	PE_SetMessageWindowTextOriginPosMode(SELFTEST_NO, 1);
	CHECK(PE_GetMessageWindowTextOriginPosMode(SELFTEST_NO) == 1, "MsgWin.TextOriginPosMode");
	PE_SetMessageWindowTextSpeed(SELFTEST_NO, 30);
	CHECK(PE_GetMessageWindowTextSpeed(SELFTEST_NO) == 30, "MsgWin.TextSpeed");
	PE_SetMessageWindowTextSpace(SELFTEST_NO, 1, 2);
	int sp1 = 0, sp2 = 0;
	PE_GetMessageWindowTextSpace(SELFTEST_NO, &sp1, &sp2);
	CHECK(sp1 == 1 && sp2 == 2, "MsgWin.TextSpace");
	PE_SetMessageWindowInactiveMultipleColor(SELFTEST_NO, 91, 92, 93);
	CHECK(PE_GetMessageWindowInactiveMultipleColorR(SELFTEST_NO) == 91 &&
	      PE_GetMessageWindowInactiveMultipleColorG(SELFTEST_NO) == 92 &&
	      PE_GetMessageWindowInactiveMultipleColorB(SELFTEST_NO) == 93,
	      "MsgWin.InactiveMultipleColor");
	/* 文本 + 台词位置指纹（指纹无 getter，只能验证文本往返与不崩） */
	it = cstr_to_string("\x82\xb1\x82\xf1");
	PE_SetMessageWindowText(SELFTEST_NO, it, 1234, it, 7, 5);
	free_string(it);
	CHECK_STR(PE_GetMessageWindowText(SELFTEST_NO), "\x82\xb1\x82\xf1", "MsgWin.Text");

	/* --- MultiTextBox --- */
	it = cstr_to_string("multi"); PE_SetMultiTextBoxText(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetMultiTextBoxText(SELFTEST_NO), "multi", "MultiTextBox.Text");
	PE_SetMultiTextBoxMaxTextLength(SELFTEST_NO, 1000);
	PE_SetMultiTextBoxReadOnly(SELFTEST_NO, true);
	CHECK(PE_GetMultiTextBoxMaxTextLength(SELFTEST_NO) == 1000, "MultiTextBox.MaxTextLength");
	CHECK(PE_IsMultiTextBoxReadOnly(SELFTEST_NO), "MultiTextBox.ReadOnly");
	PE_SetMultiTextBoxVScrollShow(SELFTEST_NO, true);
	PE_SetMultiTextBoxHScrollShow(SELFTEST_NO, true);
	CHECK(PE_IsMultiTextBoxVScrollShow(SELFTEST_NO) &&
	      PE_IsMultiTextBoxHScrollShow(SELFTEST_NO), "MultiTextBox.ScrollShow");
	PE_SetMultiTextBoxBGColor(SELFTEST_NO, 51, 52, 53);
	CHECK(PE_GetMultiTextBoxBGR(SELFTEST_NO) == 51 &&
	      PE_GetMultiTextBoxBGG(SELFTEST_NO) == 52 &&
	      PE_GetMultiTextBoxBGB(SELFTEST_NO) == 53, "MultiTextBox.BGColor");
	PE_SetMultiTextBoxReadOnlyBGColor(SELFTEST_NO, 54, 55, 56);
	CHECK(PE_GetMultiTextBoxReadOnlyBGR(SELFTEST_NO) == 54 &&
	      PE_GetMultiTextBoxReadOnlyBGG(SELFTEST_NO) == 55 &&
	      PE_GetMultiTextBoxReadOnlyBGB(SELFTEST_NO) == 56,
	      "MultiTextBox.ReadOnlyBGColor");
	PE_SetMultiTextBoxFrameColor(SELFTEST_NO, 57, 58, 59);
	CHECK(PE_GetMultiTextBoxFrameR(SELFTEST_NO) == 57 &&
	      PE_GetMultiTextBoxFrameG(SELFTEST_NO) == 58 &&
	      PE_GetMultiTextBoxFrameB(SELFTEST_NO) == 59, "MultiTextBox.FrameColor");
	PE_SetMultiTextBoxSelectColor(SELFTEST_NO, 60, 61, 62);
	CHECK(PE_GetMultiTextBoxSelectR(SELFTEST_NO) == 60 &&
	      PE_GetMultiTextBoxSelectG(SELFTEST_NO) == 61 &&
	      PE_GetMultiTextBoxSelectB(SELFTEST_NO) == 62, "MultiTextBox.SelectColor");
	PE_SetMultiTextBoxCharSpace(SELFTEST_NO, 6);
	PE_SetMultiTextBoxLineSpace(SELFTEST_NO, 7);
	CHECK(PE_GetMultiTextBoxCharSpace(SELFTEST_NO) == 6, "MultiTextBox.CharSpace");
	CHECK(PE_GetMultiTextBoxLineSpace(SELFTEST_NO) == 7, "MultiTextBox.LineSpace");
	PE_SetMultiTextBoxPadding(SELFTEST_NO, 1, 2, 3, 4);
	CHECK(PE_GetMultiTextBoxPaddingTop(SELFTEST_NO) == 1 &&
	      PE_GetMultiTextBoxPaddingBottom(SELFTEST_NO) == 2 &&
	      PE_GetMultiTextBoxPaddingLeft(SELFTEST_NO) == 3 &&
	      PE_GetMultiTextBoxPaddingRight(SELFTEST_NO) == 4, "MultiTextBox.Padding");

	/* --- VScrollbar / HScrollbar（对称，只验 V 全量 + H 抽查） --- */
	PE_SetVScrollbarUpHeight(SELFTEST_NO, 20);
	PE_SetVScrollbarDownHeight(SELFTEST_NO, 21);
	CHECK(PE_GetVScrollbarUpHeight(SELFTEST_NO) == 20 &&
	      PE_GetVScrollbarDownHeight(SELFTEST_NO) == 21, "VScroll.Up/DownHeight");
	PE_SetVScrollbarTotalSize(SELFTEST_NO, 1000);
	PE_SetVScrollbarViewSize(SELFTEST_NO, 200);
	PE_SetVScrollbarScrollPos(SELFTEST_NO, 300);
	PE_SetVScrollbarScrollRate(SELFTEST_NO, 0.25f);
	PE_SetVScrollbarMoveSizeByButton(SELFTEST_NO, 40);
	CHECK(PE_GetVScrollbarTotalSize(SELFTEST_NO) == 1000 &&
	      PE_GetVScrollbarViewSize(SELFTEST_NO) == 200 &&
	      PE_GetVScrollbarScrollPos(SELFTEST_NO) == 300 &&
	      PE_GetVScrollbarScrollRate(SELFTEST_NO) == 0.25f &&
	      PE_GetVScrollbarMoveSizeByButton(SELFTEST_NO) == 40, "VScroll.尺寸与位置");
	it = cstr_to_string("vs.cg"); PE_SetVScrollbarCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetVScrollbarCGName(SELFTEST_NO), "vs.cg", "VScroll.CGName");
	it = cstr_to_string("vs.fl"); PE_SetVScrollbarFlatName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetVScrollbarFlatName(SELFTEST_NO), "vs.fl", "VScroll.FlatName");
	it = cstr_to_string("vs.up"); PE_SetVScrollbarOverrideUpCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetVScrollbarOverrideUpCGName(SELFTEST_NO), "vs.up", "VScroll.OvUpCG");
	it = cstr_to_string("vs.dn"); PE_SetVScrollbarOverrideDownCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetVScrollbarOverrideDownCGName(SELFTEST_NO), "vs.dn", "VScroll.OvDownCG");
	it = cstr_to_string("vs.bar"); PE_SetVScrollbarOverrideBarCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetVScrollbarOverrideBarCGName(SELFTEST_NO), "vs.bar", "VScroll.OvBarCG");
	it = cstr_to_string("vs.bg"); PE_SetVScrollbarOverrideBGCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetVScrollbarOverrideBGCGName(SELFTEST_NO), "vs.bg", "VScroll.OvBGCG");
	it = cstr_to_string("vs.upf");
	PE_SetVScrollbarOverrideUpFlatName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetVScrollbarOverrideUpFlatName(SELFTEST_NO), "vs.upf", "VScroll.OvUpFlat");

	PE_SetHScrollbarLeftWidth(SELFTEST_NO, 11);
	PE_SetHScrollbarRightWidth(SELFTEST_NO, 12);
	CHECK(PE_GetHScrollbarLeftWidth(SELFTEST_NO) == 11 &&
	      PE_GetHScrollbarRightWidth(SELFTEST_NO) == 12, "HScroll.Left/RightWidth");
	it = cstr_to_string("hs.left");
	PE_SetHScrollbarOverrideLeftCGName(SELFTEST_NO, it); free_string(it);
	CHECK_STR(PE_GetHScrollbarOverrideLeftCGName(SELFTEST_NO), "hs.left",
		  "HScroll.OvLeftCG");

	parts_release(SELFTEST_NO);
	/* 释放后再查一次：部件没了，getter 必须退回默认值而不是崩。 */
	CHECK_STR(PE_GetTextBoxText(SELFTEST_NO), "", "TextBox.Text after release");

	NOTICE("[wtest] 控件状态自检：%d 项失败（10 类控件）", fail);
#undef CHECK
#undef CHECK_STR
	return fail;
}
