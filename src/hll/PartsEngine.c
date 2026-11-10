/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/string.h"

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

// Oyako Rankan
static bool PartsEngine_AddDrawCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddDrawCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// Oyako Rankan
static bool PartsEngine_AddCopyCutCGToPartsConstructionProcess_old(int parts_no,
		struct string *cg_name, int dx, int dy, int sx, int sy, int w, int h,
		int state)
{
	return PE_AddCopyCutCGToPartsConstructionProcess(parts_no, cg_name, dx, dy, w, h,
			sx, sy, w, h, 0, state);
}

// Rance 9: argument types changed from int to float
static void PartsEngine_SetComponentPos(int parts_no, float x, float y)
{
	PE_SetPos(parts_no, x, y);
}

// Rance 9: return type changed from int to float
static float PartsEngine_Parts_GetPartsUpperLeftPosX(int parts_no, int state)
{
	return PE_GetPartsUpperLeftPosX(parts_no, state);
}

static float PartsEngine_Parts_GetPartsUpperLeftPosY(int parts_no, int state)
{
	return PE_GetPartsUpperLeftPosY(parts_no, state);
}

// Rance 9: return type changed from int to float
static float PartsEngine_Parts_GetComponentPosX(int parts_no)
{
	return PE_GetPartsX(parts_no);
}

static float PartsEngine_GetComponentPosY(int parts_no)
{
	return PE_GetPartsY(parts_no);
}

// Rance 9: X/Y coordinate types changed from int to float
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
		WARNING("AddConstructProcess: DrawLine unimplemented");
		break;
	default:
		WARNING("AddConstructProcess: unknown command %d", command);
		break;
	}
}

// Rance9 predates the layer functions at func_id 3-5.  Use the numbering from
// later PartsEngine versions as canonical and adjust Rance9's ids to match.
static bool parts_func_uses_rance9_ids;

// Generic dispatch function for PartsEngine operations.
// func_id selects the operation; arguments and return values are passed
// through three typed arrays (int/bool, float, string).
//
// Calling convention (from game script):
//   1. Push input values into the appropriate arrays.
//   2. Push a placeholder (0 or "") for each output slot.
//   3. Call PartsFunc; outputs are written back into the placeholder slots.
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
	// Layers are identified by their index in the controller stack, so the
	// layer id and index conversions are identity functions.
	case 3:  // int GetLayerIndex(int layer_id)
	case 4:  // int GetLayerID(int layer_index)
		REQUIRE_INTS(2);
		ints[1].i = ints[0].i;
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

/* Parts_SetComment(int, string) -> void: debug-only annotation attached to
 * a parts object. The PE backend exposes no comment slot and game logic
 * never reads it back, so this is a deliberate no-op. */
static void PartsEngine_SetComment(int parts_no, struct string *comment)
{
	(void)parts_no;
	(void)comment;
}

/* AIN f[70]: void SetWantSaveBackScene(int parts_no, bool want).
 * AIN_BOOL is marshalled as ffi_type_sint32, so the C side takes int. */
static void PE_SetWantSaveBackScene(int parts_no, int want)
{
	PE_parts_set_want_save_back_scene(parts_no, !!want);
}

/* AIN f[776]: void AddPartsConstructionProcess(int, ref array, ref array,
 * ref array, int). Rance10's debug FPS graph builds its bars with this.
 * The case-162 dispatcher takes flat union vm_value arrays instead, so the
 * array->op mapping still has to be ported (D37). */
static void PE_AddPartsConstructionProcess(int parts_no, struct page **ints,
        struct page **floats, struct page **strings, int state)
{
	(void)parts_no; (void)ints; (void)floats; (void)strings; (void)state;
}

/* AIN f[12]: void UpdateComponent(int, int, bool, float, float).
 * The old binding pointed at the 3-arg PartsEngine_Update (D62). */
static void PartsEngine_UpdateComponent(int parts_no, int component_no, int flag, float f1, float f2)
{
	(void)parts_no; (void)component_no; (void)flag; (void)f1; (void)f2;
}
/* AIN PartsEngine.Parts_GetParentPartsNumber: stage 1, no parent parts
 * tracking in the port, report "none" (-1) (D80). */
static int PartsEngine_Parts_GetParentPartsNumber(uintptr_t parts_no)
{
	(void)parts_no;
	/* [HACK-POC D81] probe only: report 'no parent' as 0, which is what
	 * AIN AFL_Parts_GetLayerIDByParts (fno 7260) terminates on. */
	return 0;
}

/* AIN PartsEngine.CreateUserPartsNumber: allocate a fresh user parts number.
 * Called from Parts@AllocatePartsNumber (fno 29534) and the result is stored
 * directly as the parts number, so it must be unique and free.
 * [HACK-POC D82] reuses the system free-number allocator (1000001000+); the
 * user-parts segment semantics are not verified yet. */
static int PartsEngine_CreateUserPartsNumber(void)
{
	return PE_GetFreeNumber();
}
/* AIN f[66]: void SetEnableInputProcess(int parts_no, bool enable).
 * [HACK-POC D85] no per-parts state is kept yet; the call must not abort. */
static void PartsEngine_SetEnableInputProcess(int parts_no, int enable)
{
	(void)parts_no;
	(void)enable;
}

/* AIN f: void Parts_SetPartsTextCountReturn. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_Parts_SetPartsTextCountReturn(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void Parts_SetPartsTextHighlight. [HACK-POC D85] setter stubbed; no state kept. */
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

/* AIN f: void SetButtonColor. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetButtonColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN f: void SetButtonEnable. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetButtonEnable(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetButtonSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetButtonSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetCheckBoxColor. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetCheckBoxColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN f: void SetCheckBoxSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetCheckBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetComboBoxSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetComboBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetComboBoxTextMargin. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetComboBoxTextMargin(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetComponentMipmap. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetComponentMipmap(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetComponentTextureFilterType. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetComponentTextureFilterType(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetFocus. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetFocus(int a0)
{
	(void)a0;
}

/* AIN f: void SetHScrollbarLeftWidth. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetHScrollbarLeftWidth(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetHScrollbarMoveSizeByButton. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetHScrollbarMoveSizeByButton(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetHScrollbarRightWidth. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetHScrollbarRightWidth(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetHScrollbarScrollPos. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetHScrollbarScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetHScrollbarSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetHScrollbarSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetHScrollbarTotalSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetHScrollbarTotalSize(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetHScrollbarViewSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetHScrollbarViewSize(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetListBoxLineHeight. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetListBoxLineHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetListBoxMargin. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetListBoxMargin(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetListBoxScrollPos. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetListBoxScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetListBoxSelectIndex. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetListBoxSelectIndex(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetListBoxSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetListBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetMultiTextBoxMaxTextLength. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetMultiTextBoxMaxTextLength(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetMultiTextBoxSelectColor. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetMultiTextBoxSelectColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN f: void SetMultiTextBoxSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetMultiTextBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetTextBoxMaxTextLength. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetTextBoxMaxTextLength(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetTextBoxSelectColor. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetTextBoxSelectColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN f: void SetTextBoxSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetTextBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetVScrollbarDownHeight. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetVScrollbarDownHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetVScrollbarMoveSizeByButton. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetVScrollbarMoveSizeByButton(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetVScrollbarScrollPos. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetVScrollbarScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetVScrollbarSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetVScrollbarSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetVScrollbarTotalSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetVScrollbarTotalSize(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetVScrollbarUpHeight. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetVScrollbarUpHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetVScrollbarViewSize. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetVScrollbarViewSize(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetButtonCGName. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetButtonCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetButtonText. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetButtonText(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetCheckBoxCGName. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetCheckBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetCheckBoxText. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetCheckBoxText(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetComboBoxCGName. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetComboBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetComboBoxText. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetComboBoxText(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetHScrollbarCGName. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetHScrollbarCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetListBoxCGName. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetListBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetListBoxItem. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetListBoxItem(int a0, int a1, struct string * a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN f: void SetMultiTextBoxCGName. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetMultiTextBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetMultiTextBoxText. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetMultiTextBoxText(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetTextBoxCGName. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetTextBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetTextBoxText. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetTextBoxText(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN f: void SetVScrollbarCGName. [HACK-POC D85] setter stubbed; no state kept. */
static void PE_Noop_SetVScrollbarCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN Parts_SetAnchor: void Parts_SetAnchor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetAnchor(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN Parts_SetFlatBeginSeekFrame: void Parts_SetFlatBeginSeekFrame. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetFlatBeginSeekFrame(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetFlatEnableLayer: void Parts_SetFlatEnableLayer. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetFlatEnableLayer(int a0, struct string * a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN Parts_SetHGaugeReverse: void Parts_SetHGaugeReverse. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetHGaugeReverse(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetLockUpdate: void Parts_SetLockUpdate. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetLockUpdate(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN Parts_SetPosForRate: void Parts_SetPosForRate. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetPosForRate(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN Parts_SetRubyCharSpace: void Parts_SetRubyCharSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetRubyCharSpace(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetSoundName: void Parts_SetSoundName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetSoundName(int a0, struct string * a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetSwipeType: void Parts_SetSwipeType. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetSwipeType(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN Parts_SetTextEnableTag: void Parts_SetTextEnableTag. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetTextEnableTag(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetTextPos: void Parts_SetTextPos. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetTextPos(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetTextShowTime: void Parts_SetTextShowTime. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetTextShowTime(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetUpdateLookAtPos: void Parts_SetUpdateLookAtPos. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetUpdateLookAtPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN Parts_SetVGaugeReverse: void Parts_SetVGaugeReverse. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetVGaugeReverse(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN Parts_SetWheelable: void Parts_SetWheelable. [HACK-POC D85] setter stubbed. */
static void PE_Noop_Parts_SetWheelable(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetActiveController: void SetActiveController. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetActiveController(int a0)
{
	(void)a0;
}

/* AIN SetActivityBG: void SetActivityBG. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetActivityBG(struct string * a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetActivityEXText: void SetActivityEXText. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetActivityEXText(struct string * a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetActivityEndKey: void SetActivityEndKey. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetActivityEndKey(struct string * a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetButtonCharSpace: void SetButtonCharSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetButtonCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetButtonFlatName: void SetButtonFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetButtonFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetButtonLineSpace: void SetButtonLineSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetButtonLineSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetButtonTextOriginPosMode: void SetButtonTextOriginPosMode. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetButtonTextOriginPosMode(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxButtonMode: void SetCheckBoxButtonMode. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetCheckBoxButtonMode(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxCharSpace: void SetCheckBoxCharSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetCheckBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxEnable: void SetCheckBoxEnable. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetCheckBoxEnable(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxFlatName: void SetCheckBoxFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetCheckBoxFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxLineSpace: void SetCheckBoxLineSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetCheckBoxLineSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxOverrideCGName: void SetCheckBoxOverrideCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetCheckBoxOverrideCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxOverrideCheckedCGName: void SetCheckBoxOverrideCheckedCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetCheckBoxOverrideCheckedCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxOverrideCheckedFlatName: void SetCheckBoxOverrideCheckedFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetCheckBoxOverrideCheckedFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxOverrideFlatName: void SetCheckBoxOverrideFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetCheckBoxOverrideFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetCheckBoxTextOriginPosMode: void SetCheckBoxTextOriginPosMode. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetCheckBoxTextOriginPosMode(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetClickMissSoundName: void SetClickMissSoundName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetClickMissSoundName(struct string * a0)
{
	(void)a0;
}

/* AIN SetComboBoxCharSpace: void SetComboBoxCharSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComboBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComboBoxListBoxCharSpace: void SetComboBoxListBoxCharSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComboBoxListBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComboBoxListBoxHeight: void SetComboBoxListBoxHeight. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComboBoxListBoxHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComboBoxListBoxItem: void SetComboBoxListBoxItem. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComboBoxListBoxItem(int a0, int a1, struct string * a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetComboBoxListBoxLineHeight: void SetComboBoxListBoxLineHeight. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComboBoxListBoxLineHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComboBoxListBoxMargin: void SetComboBoxListBoxMargin. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComboBoxListBoxMargin(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetComboBoxListBoxScrollPos: void SetComboBoxListBoxScrollPos. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComboBoxListBoxScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComboBoxListBoxSelectIndex: void SetComboBoxListBoxSelectIndex. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComboBoxListBoxSelectIndex(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentCheckBoxShowLinkNumber: void SetComponentCheckBoxShowLinkNumber. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentCheckBoxShowLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentClipArea: void SetComponentClipArea. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentClipArea(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetComponentEnableClipArea: void SetComponentEnableClipArea. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentEnableClipArea(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentMessageWindowEffectLink: void SetComponentMessageWindowEffectLink. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentMessageWindowEffectLink(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentReverseLR: void SetComponentReverseLR. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentReverseLR(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentReverseTB: void SetComponentReverseTB. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentReverseTB(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentScrollAlphaLinkNumber: void SetComponentScrollAlphaLinkNumber. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentScrollAlphaLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentScrollPosXLinkNumber: void SetComponentScrollPosXLinkNumber. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentScrollPosXLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentScrollPosYLinkNumber: void SetComponentScrollPosYLinkNumber. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentScrollPosYLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentShowEditor: void SetComponentShowEditor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentShowEditor(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetComponentTextureAddressType: void SetComponentTextureAddressType. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetComponentTextureAddressType(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetEnableSpeedUp: void SetEnableSpeedUp. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetEnableSpeedUp(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFontColorForBackScene: void SetFontColorForBackScene. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFontColorForBackScene(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetFormCaptionColor: void SetFormCaptionColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormCaptionColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetFormCaptionPlusHeight: void SetFormCaptionPlusHeight. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormCaptionPlusHeight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormClientColor: void SetFormClientColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormClientColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetFormEnableCloseButton: void SetFormEnableCloseButton. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormEnableCloseButton(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormFold: void SetFormFold. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormFold(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormGroupViewSpan: void SetFormGroupViewSpan. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormGroupViewSpan(int a0, int a1, int a2, int a3, int a4, int a5)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;
}

/* AIN SetFormHScrollPos: void SetFormHScrollPos. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormHScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormIcon: void SetFormIcon. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormIcon(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormMaxSize: void SetFormMaxSize. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormMaxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetFormMinSize: void SetFormMinSize. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormMinSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetFormResizable: void SetFormResizable. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormResizable(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormSize: void SetFormSize. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetFormTitle: void SetFormTitle. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormTitle(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetFormVScrollPos: void SetFormVScrollPos. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetFormVScrollPos(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollTotalSizeLinkNumber: void SetHScrollTotalSizeLinkNumber. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollTotalSizeLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollViewSizeLinkNumber: void SetHScrollViewSizeLinkNumber. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollViewSizeLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarFlatName: void SetHScrollbarFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollbarFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideBGCGName: void SetHScrollbarOverrideBGCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollbarOverrideBGCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideBGFlatName: void SetHScrollbarOverrideBGFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollbarOverrideBGFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideBarCGName: void SetHScrollbarOverrideBarCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollbarOverrideBarCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideBarFlatName: void SetHScrollbarOverrideBarFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollbarOverrideBarFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideLeftCGName: void SetHScrollbarOverrideLeftCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollbarOverrideLeftCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideLeftFlatName: void SetHScrollbarOverrideLeftFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollbarOverrideLeftFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideRightCGName: void SetHScrollbarOverrideRightCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollbarOverrideRightCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetHScrollbarOverrideRightFlatName: void SetHScrollbarOverrideRightFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetHScrollbarOverrideRightFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetKeyWaitCGName: void SetKeyWaitCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetKeyWaitCGName(int a0, struct string * a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetKeyWaitFlatName: void SetKeyWaitFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetKeyWaitFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetKeyWaitPos: void SetKeyWaitPos. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetKeyWaitPos(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetKeyWaitShow: void SetKeyWaitShow. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetKeyWaitShow(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetLayoutBoxPadding: void SetLayoutBoxPadding. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetLayoutBoxPadding(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetLayoutBoxReturnSizeForRate: void SetLayoutBoxReturnSizeForRate. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetLayoutBoxReturnSizeForRate(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetListBoxCharSpace: void SetListBoxCharSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetListBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetLockInputState: void SetLockInputState. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetLockInputState(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowActive: void SetMessageWindowActive. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMessageWindowActive(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowCGName: void SetMessageWindowCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMessageWindowCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowFlatName: void SetMessageWindowFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMessageWindowFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowFlatShowWaitFrameNumber: void SetMessageWindowFlatShowWaitFrameNumber. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMessageWindowFlatShowWaitFrameNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowInactiveMultipleColor: void SetMessageWindowInactiveMultipleColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMessageWindowInactiveMultipleColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetMessageWindowText: void SetMessageWindowText. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMessageWindowText(int a0, struct string * a1, int a2, struct string * a3, int a4, int a5)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;
}

/* AIN SetMessageWindowTextArea: void SetMessageWindowTextArea. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMessageWindowTextArea(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetMessageWindowTextOriginPosMode: void SetMessageWindowTextOriginPosMode. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMessageWindowTextOriginPosMode(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMessageWindowTextSpace: void SetMessageWindowTextSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMessageWindowTextSpace(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetMessageWindowTextSpeed: void SetMessageWindowTextSpeed. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMessageWindowTextSpeed(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMotionData: void SetMotionData. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMotionData(int a0, struct string * a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetMovieTime: void SetMovieTime. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMovieTime(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetMultiTextBoxBGColor: void SetMultiTextBoxBGColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMultiTextBoxBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetMultiTextBoxCharSpace: void SetMultiTextBoxCharSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMultiTextBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMultiTextBoxFrameColor: void SetMultiTextBoxFrameColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMultiTextBoxFrameColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetMultiTextBoxHScrollShow: void SetMultiTextBoxHScrollShow. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMultiTextBoxHScrollShow(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMultiTextBoxLineSpace: void SetMultiTextBoxLineSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMultiTextBoxLineSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMultiTextBoxPadding: void SetMultiTextBoxPadding. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMultiTextBoxPadding(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetMultiTextBoxReadOnly: void SetMultiTextBoxReadOnly. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMultiTextBoxReadOnly(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetMultiTextBoxReadOnlyBGColor: void SetMultiTextBoxReadOnlyBGColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMultiTextBoxReadOnlyBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetMultiTextBoxVScrollShow: void SetMultiTextBoxVScrollShow. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetMultiTextBoxVScrollShow(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetNumeralFullPitch: void SetNumeralFullPitch. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetNumeralFullPitch(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetNumeralShowPadding: void SetNumeralShowPadding. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetNumeralShowPadding(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetNumeralShowType: void SetNumeralShowType. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetNumeralShowType(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetOpenTextBoxIME: void SetOpenTextBoxIME. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetOpenTextBoxIME(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetPanelAlphaGradationBottom: void SetPanelAlphaGradationBottom. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetPanelAlphaGradationBottom(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetPanelAlphaGradationLeft: void SetPanelAlphaGradationLeft. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetPanelAlphaGradationLeft(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetPanelAlphaGradationRight: void SetPanelAlphaGradationRight. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetPanelAlphaGradationRight(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetPanelAlphaGradationTop: void SetPanelAlphaGradationTop. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetPanelAlphaGradationTop(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetPanelColor: void SetPanelColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetPanelColor(int a0, int a1, int a2, int a3, int a4)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
}

/* AIN SetPanelSize: void SetPanelSize. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetPanelSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetRectangleDetectionPoint: void SetRectangleDetectionPoint. [HACK-POC D85] setter stubbed. */
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

/* AIN SetShowForBackScene: void SetShowForBackScene. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetShowForBackScene(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetSpinBoxBGColor: void SetSpinBoxBGColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetSpinBoxBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetSpinBoxCGName: void SetSpinBoxCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetSpinBoxCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetSpinBoxCharSpace: void SetSpinBoxCharSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetSpinBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetSpinBoxDecimalPlace: void SetSpinBoxDecimalPlace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetSpinBoxDecimalPlace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetSpinBoxFrameColor: void SetSpinBoxFrameColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetSpinBoxFrameColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetSpinBoxReadOnly: void SetSpinBoxReadOnly. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetSpinBoxReadOnly(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetSpinBoxReadOnlyBGColor: void SetSpinBoxReadOnlyBGColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetSpinBoxReadOnlyBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetSpinBoxSelectColor: void SetSpinBoxSelectColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetSpinBoxSelectColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetSpinBoxSize: void SetSpinBoxSize. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetSpinBoxSize(int a0, int a1, int a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetTextBoxBGColor: void SetTextBoxBGColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetTextBoxBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetTextBoxCharSpace: void SetTextBoxCharSpace. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetTextBoxCharSpace(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetTextBoxFrameColor: void SetTextBoxFrameColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetTextBoxFrameColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetTextBoxReadOnly: void SetTextBoxReadOnly. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetTextBoxReadOnly(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetTextBoxReadOnlyBGColor: void SetTextBoxReadOnlyBGColor. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetTextBoxReadOnlyBGColor(int a0, int a1, int a2, int a3)
{
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
}

/* AIN SetUserComponentData: void SetUserComponentData. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetUserComponentData(int a0, struct string * a1, struct string * a2)
{
	(void)a0;
	(void)a1;
	(void)a2;
}

/* AIN SetUserComponentName: void SetUserComponentName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetUserComponentName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollTotalSizeLinkNumber: void SetVScrollTotalSizeLinkNumber. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollTotalSizeLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollViewSizeLinkNumber: void SetVScrollViewSizeLinkNumber. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollViewSizeLinkNumber(int a0, int a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarFlatName: void SetVScrollbarFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollbarFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideBGCGName: void SetVScrollbarOverrideBGCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollbarOverrideBGCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideBGFlatName: void SetVScrollbarOverrideBGFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollbarOverrideBGFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideBarCGName: void SetVScrollbarOverrideBarCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollbarOverrideBarCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideBarFlatName: void SetVScrollbarOverrideBarFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollbarOverrideBarFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideDownCGName: void SetVScrollbarOverrideDownCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollbarOverrideDownCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideDownFlatName: void SetVScrollbarOverrideDownFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollbarOverrideDownFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideUpCGName: void SetVScrollbarOverrideUpCGName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollbarOverrideUpCGName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN SetVScrollbarOverrideUpFlatName: void SetVScrollbarOverrideUpFlatName. [HACK-POC D85] setter stubbed. */
static void PE_Noop_SetVScrollbarOverrideUpFlatName(int a0, struct string * a1)
{
	(void)a0;
	(void)a1;
}

/* AIN PartsEngine f[22]: bool IsExistActivity(string name).
 * [HACK-POC D89] the whole Activity subsystem (f[22..52]) is unimplemented.
 * Reporting "absent" keeps RunStartScreen's overlay setup on the no-activity
 * path instead of aborting. Real fix: activity table + file loading. */
static int PartsEngine_IsExistActivity(struct string *name)
{
	PE_HACK_HIT("PartsEngine_IsExistActivity");
	if (getenv("XSYS4_PE_DBG"))
		NOTICE("[pe-hack]   name=%s", name ? name->text : "(null)");
	(void)name;
	return 0;
}

/* AIN f[23] CreateActivity. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_CreateActivity(struct string * a0)
{
	PE_HACK_HIT("PE_Act_CreateActivity");
	(void)a0;
	return 0;
}

/* AIN f[24] ReleaseActivity. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_ReleaseActivity(struct string * a0, struct page ** a1)
{
	PE_HACK_HIT("PE_Act_ReleaseActivity");
	(void)a0;
	(void)a1;
	return 0;
}

/* AIN f[26] LoadActivityEXText. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_LoadActivityEXText(struct string * a0, struct string * a1, int a2)
{
	PE_HACK_HIT("PE_Act_LoadActivityEXText");
	(void)a0;
	(void)a1;
	(void)a2;
	return 0;
}

/* AIN f[27] ReadActivityFile. [HACK-POC D89] Activity subsystem stub. */
/* [D110f] pactex == System4 EX 格式（HEAD/EXTF/DATA + 字节置换表 + zlib）。
 * libsys4 的 ex_read() 已经能完整解析，这里只把结构打出来，
 * 用来确定 Activity 需要哪些字段，再决定数据结构。
 */
static void pe_act_dump_value(struct ex_value *v, int d);

static void pe_act_pad(char *buf, int d)
{
	int n = d * 2;
	if (n > 60)
		n = 60;
	memset(buf, ' ', n);
	buf[n] = 0;
}

static void pe_act_dump_tree(struct ex_tree *t, int d)
{
	char pad[64];
	pe_act_pad(pad, d);
	NOTICE("%s- '%s' leaf=%d", pad, t->name ? t->name->text : "?", t->is_leaf);
	if (t->is_leaf) {
		pe_act_dump_value(&t->leaf.value, d + 1);
	} else if (d < 6) {
		for (uint32_t i = 0; i < t->nr_children && i < 24; i++)
			pe_act_dump_tree(&t->children[i], d + 1);
		if (t->nr_children > 24)
			NOTICE("%s  ... %u children total", pad, t->nr_children);
	} else {
		NOTICE("%s  ... %u children (depth limit)", pad, t->nr_children);
	}
}

static void pe_act_dump_value(struct ex_value *v, int d)
{
	char pad[64];
	pe_act_pad(pad, d);
	switch (v->type) {
	case EX_INT:
		NOTICE("%sint %d", pad, v->i);
		break;
	case EX_FLOAT:
		NOTICE("%sfloat %.4f", pad, v->f);
		break;
	case EX_STRING:
		NOTICE("%sstr '%s'", pad, v->s ? v->s->text : "");
		break;
	case EX_TABLE: {
		struct ex_table *t = v->t;
		NOTICE("%stable %ux%u", pad, t->nr_rows, t->nr_columns);
		for (uint32_t c = 0; c < t->nr_columns && c < 20; c++)
			NOTICE("%s  col[%u] '%s' %s", pad, c,
			       t->fields[c].name ? t->fields[c].name->text : "?",
			       ex_strtype(t->fields[c].type));
		if (d > 5)
			break;
		for (uint32_t r = 0; r < t->nr_rows && r < 6; r++) {
			NOTICE("%s  row[%u]", pad, r);
			for (uint32_t c = 0; c < t->nr_columns && c < 20; c++) {
				struct ex_value *cv = ex_table_get(t, r, c);
				if (cv)
					pe_act_dump_value(cv, d + 2);
			}
		}
		if (t->nr_rows > 6)
			NOTICE("%s  ... %u rows total", pad, t->nr_rows);
		break;
	}
	case EX_LIST: {
		struct ex_list *l = v->list;
		NOTICE("%slist %u", pad, l->nr_items);
		if (d > 5)
			break;
		for (uint32_t i = 0; i < l->nr_items && i < 6; i++) {
			struct ex_value *iv = ex_list_get(l, i);
			if (iv)
				pe_act_dump_value(iv, d + 1);
		}
		if (l->nr_items > 6)
			NOTICE("%s  ... %u items total", pad, l->nr_items);
		break;
	}
	case EX_TREE:
		pe_act_dump_tree(v->tree, d);
		break;
	default:
		NOTICE("%s? unknown type=%d", pad, v->type);
		break;
	}
}

static void pe_act_dump_ex(struct archive_data *ad)
{
	static int done = 0;
	if (done)
		return;
	done = 1;
	struct ex *ex = ex_read((const uint8_t *)ad->data, ad->size);
	if (!ex) {
		NOTICE("[pe-ex] ex_read failed");
		return;
	}
	NOTICE("[pe-ex] '%s' blocks=%u", ad->name ? ad->name : "?", ex->nr_blocks);
	for (uint32_t i = 0; i < ex->nr_blocks; i++) {
		NOTICE("[pe-ex] block[%u] '%s'", i,
		       ex->blocks[i].name ? ex->blocks[i].name->text : "?");
		pe_act_dump_value(&ex->blocks[i].val, 1);
	}
	ex_free(ex);
}

static int PE_Act_ReadActivityFile(struct string * a0, struct string * a1, int a2)
{
	PE_HACK_HIT("PE_Act_ReadActivityFile");
	/* [D110d] 一次性定位：活动文件叫什么、在哪个归档里。
	 * a0/a1 的真实语义由调用方决定，先把两个都打出来再判。 */
	if (getenv("XSYS4_PE_DBG")) {
		NOTICE("[pe-hack]   args a0=%s a1=%s a2=%d",
		       a0 ? a0->text : "(null)", a1 ? a1->text : "(null)", a2);
		if (a0) {
			for (int t = 0; t <= ASSET_FLASH; t++) {
				int id = -1;
				bool ex = asset_exists_by_name((enum asset_type)t, a0->text, &id);
				NOTICE("[pe-hack]     asset %-5s exists=%d id=%d",
				       asset_strtype((enum asset_type)t), ex, id);
				/* [D110e] PACT 命中即落盘，供离线分析活动文件格式 */
				if (ex && t == ASSET_PACT && getenv("XSYS4_PE_DUMP")) {
					struct archive_data *ad = asset_get((enum asset_type)t, id);
					if (ad && ad->data) {
						FILE *df = fopen("/tmp/act_dump.bin", "wb");
						if (df) {
							fwrite(ad->data, 1, ad->size, df);
							fclose(df);
						}
						NOTICE("[pe-hack]     dumped '%s' size=%d -> /tmp/act_dump.bin",
						       ad->name ? ad->name : "(?)", (int)ad->size);
						if (getenv("XSYS4_PE_DUMP_EX"))
							pe_act_dump_ex(ad);
					}
				}
			}
		}
	}
	(void)a0; (void)a1; (void)a2;
	return 0;
}

/* AIN f[28] WriteActivityFile. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_WriteActivityFile(struct string * a0, struct string * a1)
{
	PE_HACK_HIT("PE_Act_WriteActivityFile");
	(void)a0;
	(void)a1;
	return 0;
}

/* AIN f[29] IsExistActivityFile. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_IsExistActivityFile(struct string * a0)
{
	PE_HACK_HIT("PE_Act_IsExistActivityFile");
	(void)a0;
	return 0;
}

/* AIN f[30] AddActivityParts. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_AddActivityParts(struct string * a0, struct string * a1, int a2)
{
	PE_HACK_HIT("PE_Act_AddActivityParts");
	(void)a0;
	(void)a1;
	(void)a2;
	return 0;
}

/* AIN f[31] RemoveActivityParts. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_RemoveActivityParts(struct string * a0, struct string * a1)
{
	PE_HACK_HIT("PE_Act_RemoveActivityParts");
	(void)a0;
	(void)a1;
	return 0;
}

/* AIN f[32] RemoveAllActivityParts. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_RemoveAllActivityParts(struct string * a0)
{
	PE_HACK_HIT("PE_Act_RemoveAllActivityParts");
	(void)a0;
	return 0;
}

/* AIN f[33] NumofActivityParts. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_NumofActivityParts(struct string * a0)
{
	PE_HACK_HIT("PE_Act_NumofActivityParts");
	(void)a0;
	return -1;
}

/* AIN f[35] IsExistActivityPartsByName. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_IsExistActivityPartsByName(struct string * a0, struct string * a1)
{
	PE_HACK_HIT("PE_Act_IsExistActivityPartsByName");
	(void)a0;
	(void)a1;
	return 0;
}

/* AIN f[36] IsExistActivityPartsByNumber. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_IsExistActivityPartsByNumber(struct string * a0, int a1)
{
	PE_HACK_HIT("PE_Act_IsExistActivityPartsByNumber");
	(void)a0;
	(void)a1;
	return 0;
}

/* AIN f[37] GetActivityPartsNumber. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_GetActivityPartsNumber(struct string * a0, struct string * a1)
{
	PE_HACK_HIT("PE_Act_GetActivityPartsNumber");
	(void)a0;
	(void)a1;
	return -1;
}

/* AIN f[39] GetActivityEXID. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_GetActivityEXID(struct string * a0)
{
	PE_HACK_HIT("PE_Act_GetActivityEXID");
	(void)a0;
	return -1;
}

/* AIN f[44] AddActivityCloseParts. [HACK-POC D89] Activity subsystem stub. */
static void PE_Act_AddActivityCloseParts(struct string * a0, struct string * a1)
{
	PE_HACK_HIT("PE_Act_AddActivityCloseParts");
	(void)a0;
	(void)a1;
}

/* AIN f[45] IsExistActivityCloseParts. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_IsExistActivityCloseParts(struct string * a0, struct string * a1)
{
	PE_HACK_HIT("PE_Act_IsExistActivityCloseParts");
	(void)a0;
	(void)a1;
	return 0;
}

/* AIN f[46] AddActivityLockedParts. [HACK-POC D89] Activity subsystem stub. */
static void PE_Act_AddActivityLockedParts(struct string * a0, struct string * a1)
{
	PE_HACK_HIT("PE_Act_AddActivityLockedParts");
	(void)a0;
	(void)a1;
}

/* AIN f[47] IsExistActivityLockedParts. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_IsExistActivityLockedParts(struct string * a0, struct string * a1)
{
	PE_HACK_HIT("PE_Act_IsExistActivityLockedParts");
	(void)a0;
	(void)a1;
	return 0;
}

/* AIN f[49] EraseActivityEndKey. [HACK-POC D89] Activity subsystem stub. */
static void PE_Act_EraseActivityEndKey(struct string * a0, int a1)
{
	PE_HACK_HIT("PE_Act_EraseActivityEndKey");
	(void)a0;
	(void)a1;
}

/* AIN f[50] IsExistActivityEndKey. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_IsExistActivityEndKey(struct string * a0, int a1)
{
	PE_HACK_HIT("PE_Act_IsExistActivityEndKey");
	(void)a0;
	(void)a1;
	return 0;
}

/* AIN f[51] NumofActivityEndKey. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_NumofActivityEndKey(struct string * a0)
{
	PE_HACK_HIT("PE_Act_NumofActivityEndKey");
	(void)a0;
	return -1;
}

/* AIN f[52] GetActivityEndKey. [HACK-POC D89] Activity subsystem stub. */
static int PE_Act_GetActivityEndKey(struct string * a0, int a1)
{
	PE_HACK_HIT("PE_Act_GetActivityEndKey");
	(void)a0;
	(void)a1;
	return -1;
}

/* AIN v12 PartsEngine.GetControllerLength (lib 27 fno 10): the count that
 * parts::detail::NumofLayer iterates over. Layers are controller indices in
 * this port, so it is the controller count (see case 5 of PartsEngine_PartsFunc). */
static int PE_GetControllerLength(void)
{
	return PE_get_nr_controllers();
}

/* Controller ids are their stack index in this port (same identity mapping the
 * layer helpers use), so GetControllerID(i) is i. */
static int PE_GetControllerID(int index)
{
	return index;
}

HLL_LIBRARY(PartsEngine,
	    HLL_EXPORT(_PreLink, PartsEngine_PreLink),
	    // for versions without PartsEngine.Init
	    HLL_EXPORT(_ModuleInit, PartsEngine_ModuleInit),
	    HLL_EXPORT(_ModuleFini, PartsEngine_ModuleFini),
	    // Oyako Rankan
	    HLL_EXPORT(SetWantSaveBackScene, PE_SetWantSaveBackScene),
	    HLL_EXPORT(GetActiveController, PE_get_active_controller),
	    HLL_EXPORT(AddPartsConstructionProcess, PE_AddPartsConstructionProcess),
	    HLL_EXPORT(Init, PE_Init),
	    HLL_EXPORT(Update, PartsEngine_Update),
	    HLL_TODO_EXPORT(UpdateGUIController, PartsEngine_UpdateGUIController),
	    HLL_EXPORT(GetFreeSystemPartsNumber, PE_GetFreeNumber),
	    // FIXME: what is the difference?
	    HLL_EXPORT(GetFreeSystemPartsNumberNotSaved, PE_GetFreeNumber),
	    HLL_EXPORT(IsExistParts, PE_IsExist),
	    HLL_EXPORT(SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(GetPartsCGName, PE_GetPartsCGName),
	    HLL_EXPORT(SetPartsCGSurfaceArea, PE_SetPartsCGSurfaceArea),
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
	    HLL_EXPORT(SetNumeralCG, PE_SetNumeralCG),
	    HLL_EXPORT(SetNumeralLinkedCGNumberWidthWidthList, PE_SetNumeralLinkedCGNumberWidthWidthList),
	    HLL_TODO_EXPORT(SetNumeralFont, PartsEngine_SetNumeralFont),
	    HLL_EXPORT(SetNumeralNumber, PE_SetNumeralNumber),
	    HLL_EXPORT(SetNumeralShowComma, PE_SetNumeralShowComma),
	    HLL_EXPORT(SetNumeralSpace, PE_SetNumeralSpace),
	    HLL_EXPORT(SetNumeralLength, PE_SetNumeralLength),
	    HLL_EXPORT(SetNumeralSurfaceArea, PE_SetNumeralSurfaceArea),
	    HLL_EXPORT(SetPartsRectangleDetectionSize, PE_SetPartsRectangleDetectionSize),
	    HLL_TODO_EXPORT(SetPartsRectangleDetectionSurfaceArea, PartsEngine_SetPartsRectangleDetectionSurfaceArea),
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
	    HLL_EXPORT(Load, PE_Load),
	    // Rance 9
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
	    /* Rance10.ain f[177] = Parts_SetComment. This table is indexed by
	     * fno, not by name; see TECH_DEBT D26 (table rebuild pending). */
	    HLL_EXPORT(Parts_SetComment, PartsEngine_SetComment),
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
	    HLL_TODO_EXPORT(GetComponentMagX, PartsEngine_GetComponentMagX),
	    HLL_TODO_EXPORT(GetComponentMagY, PartsEngine_GetComponentMagY),
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
	    HLL_TODO_EXPORT(CheckBoxChecked, PartsEngine_CheckBoxChecked),
	    HLL_TODO_EXPORT(IsCheckBoxChecked, PartsEngine_IsCheckBoxChecked),
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
	    HLL_EXPORT(Parts_SetPartsCG, PE_SetPartsCG),
	    HLL_EXPORT(Parts_GetPartsCGName, PE_GetPartsCGName),
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
	    HLL_TODO_EXPORT(Parts_SetPartsRectangleDetectionSurfaceArea, PartsEngine_Parts_SetPartsRectangleDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsCGDetectionSize, PE_SetPartsCGDetectionSize),
	    HLL_TODO_EXPORT(Parts_SetPartsCGDetectionSurfaceArea, PartsEngine_Parts_SetPartsCGDetectionSurfaceArea),
	    HLL_EXPORT(Parts_SetPartsFlat, PE_SetPartsFlat),
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
	    HLL_TODO_EXPORT(Parts_ChangeFlatCG, PartsEngine_Parts_ChangeFlatCG),
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
	    HLL_TODO_EXPORT(Parts_GetParentPartsNumber, PartsEngine_Parts_GetParentPartsNumber),
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
	    HLL_EXPORT(SetActiveController, PE_Noop_SetActiveController),
	    HLL_EXPORT(SetActivityBG, PE_Noop_SetActivityBG),
	    HLL_EXPORT(SetActivityEXText, PE_Noop_SetActivityEXText),
	    HLL_EXPORT(SetActivityEndKey, PE_Noop_SetActivityEndKey),
	    HLL_EXPORT(SetButtonCharSpace, PE_Noop_SetButtonCharSpace),
	    HLL_EXPORT(SetButtonFlatName, PE_Noop_SetButtonFlatName),
	    HLL_EXPORT(SetButtonLineSpace, PE_Noop_SetButtonLineSpace),
	    HLL_EXPORT(SetButtonTextOriginPosMode, PE_Noop_SetButtonTextOriginPosMode),
	    HLL_EXPORT(SetCheckBoxButtonMode, PE_Noop_SetCheckBoxButtonMode),
	    HLL_EXPORT(SetCheckBoxCharSpace, PE_Noop_SetCheckBoxCharSpace),
	    HLL_EXPORT(SetCheckBoxEnable, PE_Noop_SetCheckBoxEnable),
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
	    HLL_EXPORT(SetLayoutBoxPadding, PE_Noop_SetLayoutBoxPadding),
	    HLL_EXPORT(SetLayoutBoxReturnSizeForRate, PE_Noop_SetLayoutBoxReturnSizeForRate),
	    HLL_EXPORT(SetListBoxCharSpace, PE_Noop_SetListBoxCharSpace),
	    HLL_EXPORT(SetLockInputState, PE_Noop_SetLockInputState),
	    HLL_EXPORT(SetMessageWindowActive, PE_Noop_SetMessageWindowActive),
	    HLL_EXPORT(SetMessageWindowCGName, PE_Noop_SetMessageWindowCGName),
	    HLL_EXPORT(SetMessageWindowFlatName, PE_Noop_SetMessageWindowFlatName),
	    HLL_EXPORT(SetMessageWindowFlatShowWaitFrameNumber, PE_Noop_SetMessageWindowFlatShowWaitFrameNumber),
	    HLL_EXPORT(SetMessageWindowInactiveMultipleColor, PE_Noop_SetMessageWindowInactiveMultipleColor),
	    HLL_EXPORT(SetMessageWindowText, PE_Noop_SetMessageWindowText),
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
	    HLL_EXPORT(SetShowForBackScene, PE_Noop_SetShowForBackScene),
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
	    HLL_EXPORT(SetUserComponentData, PE_Noop_SetUserComponentData),
	    HLL_EXPORT(SetUserComponentName, PE_Noop_SetUserComponentName),
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
	    HLL_EXPORT(GetActivityPartsNumber, PE_Act_GetActivityPartsNumber),
	    HLL_EXPORT(GetActivityEXID, PE_Act_GetActivityEXID),
	    HLL_EXPORT(AddActivityCloseParts, PE_Act_AddActivityCloseParts),
	    HLL_EXPORT(IsExistActivityCloseParts, PE_Act_IsExistActivityCloseParts),
	    HLL_EXPORT(AddActivityLockedParts, PE_Act_AddActivityLockedParts),
	    HLL_EXPORT(IsExistActivityLockedParts, PE_Act_IsExistActivityLockedParts),
	    HLL_EXPORT(EraseActivityEndKey, PE_Act_EraseActivityEndKey),
	    HLL_EXPORT(IsExistActivityEndKey, PE_Act_IsExistActivityEndKey),
	    HLL_EXPORT(NumofActivityEndKey, PE_Act_NumofActivityEndKey),
	    HLL_EXPORT(GetActivityEndKey, PE_Act_GetActivityEndKey)
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
