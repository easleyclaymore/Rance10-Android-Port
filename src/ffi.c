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

#define VM_PRIVATE

#include <stdlib.h>
#include <string.h>
#include <ffi.h>
#include "system4/ain.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

#define HLL_MAX_ARGS 64

struct hll_function {
	void *fun;
	ffi_cif cif;
	unsigned int nr_args;
	ffi_type **args;
	ffi_type *return_type;
};

static struct hll_function **libraries = NULL;

bool library_exists(int libno)
{
	return libraries[libno];
}

bool library_function_exists(int libno, int fno)
{
	return libraries[libno] && libraries[libno][fno].fun;
}

/*
 * Primitive HLL function tracing facility.
 */
//#define TRACE_HLL
#ifdef TRACE_HLL
#include "system4/string.h"

struct traced_library {
	const char *name;
	bool (*should_trace)(struct ain_hll_function *f, union vm_value **args);
};

static bool chipmunk_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"SYSTEM_SetReadMessageSkipping",
		"Update",
		"KeepPreviousView",
		"Sleep",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	return true;
}

#include "parts.h"
#include "parts/parts_internal.h"
static bool gui_engine_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"Parts_UpdateMotionTime",
		"UpdateInputState",
		"UpdateComponent",
		"UpdateParts",
		"GetMessageType",
		"ReleaseMessage",
		"Parts_SetPartsPixelDecide",
		"Parts_GetPartsShow",
		"Parts_IsCursorIn",
		"Parts_GetPartsAlpha",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	if (!strcmp(f->name, "Parts_SetPartsCG")) {
		static char *u = NULL;
		if (!u)
			u = utf2sjis("システム／ボタン／メニュー／通常", 0);
		struct string ***strs = (void*)args;
		struct string *s = *strs[1];
		return strcmp(s->text, u);
	}
	if (!strcmp(f->name, "Parts_SetPos")) {
		if (args[1]->i < 0 || args[2]->i < 0)
			return false;
		return PE_GetPartsX(args[0]->i) != args[1]->i || PE_GetPartsY(args[0]->i) != args[2]->i;
	}
	if (!strcmp(f->name, "Parts_SetZ"))
		return PE_GetPartsZ(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetShow"))
		return PE_GetPartsShow(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetAlpha"))
		return PE_GetPartsAlpha(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetPartsCGSurfaceArea")) {
		return false;
		struct parts *p = parts_try_get(args[0]->i);
		if (!p)
			return true;
		Rectangle *r = &p->states[args[5]->i].common.surface_area;
		return args[1]->i != r->x || args[2]->i != r->y
			|| args[3]->i != r->w || args[4]->i != r->h;
	}
	if (!strcmp(f->name, "Parts_SetAddColor")) {
		struct parts *p = parts_try_get(args[0]->i);
		if (!p)
			return true;
		SDL_Color *c = &p->local.add_color;
		return args[1]->i != c->r || args[2]->i != c->g || args[3]->i != c->b;
	}
	if (!strcmp(f->name, "Parts_GetPartsX"))
		return false;
	return true;
}

static bool parts_engine_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	if (!strcmp(f->name, "Update"))
		return false;
	if (!strcmp(f->name, "SetAddColor"))
		return false;
	if (!strcmp(f->name, "SetAlpha"))
		return PE_GetPartsAlpha(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "SetPartsCGSurfaceArea"))
		return args[1]->i >= 0 && args[2]->i >= 0;
	if (!strcmp(f->name, "SetPartsConstructionSurfaceArea"))
		return args[1]->i >= 0 && args[2]->i >= 0;
	if (!strcmp(f->name, "SetPartsMagX"))
		return args[1]->f > 1.001 || args[1]->f < -1.001;
	if (!strcmp(f->name, "SetPartsMagY"))
		return args[1]->f > 1.001 || args[1]->f < -1.001;
	if (!strcmp(f->name, "SetPartsOriginPosMode"))
		return PE_GetPartsOriginPosMode(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "SetPartsRotateZ"))
		return args[1]->f > 0.001 || args[1]->f < -0.001;
	if (!strcmp(f->name, "SetPos"))
		return PE_GetPartsX(args[0]->i) != args[1]->i || PE_GetPartsX(args[0]->i) != args[2]->i;
	if (!strcmp(f->name, "SetShow")) {
		if (args[0]->i == 0)
			return false; // ???
		return PE_GetPartsShow(args[0]->i) != args[1]->i;
	}
	if (!strcmp(f->name, "SetZ"))
		return PE_GetPartsZ(args[0]->i) != args[1]->i;
	return true;
}

static bool sact_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"Key_IsDown",
		"Mouse_ClearWheel",
		"Mouse_GetPos",
		"Mouse_GetWheel",
		"SP_GetWidth",
		"SP_GetHeight",
		"SP_ExistAlpha",
		"Update",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	return true;
}

struct traced_library traced_libraries[] = {
	{ "ChipmunkSpriteEngine", chipmunk_should_trace },
	{ "GUIEngine", gui_engine_should_trace },
	{ "PartsEngine", parts_engine_should_trace },
	{ "SACTDX", sact_should_trace },
};

static void print_hll_trace(struct ain_library *lib, struct ain_hll_function *f,
		      struct hll_function *fun, union vm_value *r, void *_args)
{
	sys_message("(%s) ", display_sjis0(ain->functions[call_stack[call_stack_ptr-1].fno].name));
	sys_message("%s.%s(", lib->name, f->name);
	union vm_value **args = _args;
	for (int i = 0; i < f->nr_arguments; i++) {
		if (i > 0) {
			sys_message(", ");
		}
		sys_message("%s=", display_sjis0(f->arguments[i].name));
		switch (f->arguments[i].type.data) {
		case AIN_INT:
		case AIN_LONG_INT:
			sys_message("%d", args[i]->i);
			break;
		case AIN_FLOAT:
			sys_message("%f", args[i]->f);
			break;
		case AIN_STRING: {
			struct string ***strs = _args;
			struct string *s = *strs[i];
			sys_message("\"%s\"", display_sjis0(s->text));
			break;
		}
		case AIN_BOOL:
			sys_message("%s", args[i]->i ? "true" : "false");
			break;
		case AIN_STRUCT:
			sys_message("<struct>");
			break;
		case AIN_ARRAY_TYPE:
			sys_message("<array>");
			break;
		case AIN_REF_TYPE:
			sys_message("<ref>");
			break;
		default:
			sys_message("<%d>", f->arguments[i].type.data);
			break;
		}
	}
	sys_message(")");

	ffi_call(&fun->cif, (void*)fun->fun, r, _args);

	switch (f->return_type.data) {
	case AIN_VOID:
		break;
	case AIN_INT:
		sys_message(" -> %d", r->i);
		break;
	case AIN_FLOAT:
		sys_message(" -> %f", r->f);
		break;
	case AIN_STRING:
		sys_message(" -> \"%s\"", display_sjis0(((struct string*)r->ref)->text));
		break;
	case AIN_BOOL:
		sys_message(" -> %s", r->i ? "true" : "false");
		break;
	default:
		sys_message(" -> <%d>", f->return_type.data);
		break;
	}
	for (int i = 0; i < f->nr_arguments; i++) {
		union vm_value ***args = _args;
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
			sys_message(" (%s=%d)", display_sjis0(f->arguments[i].name), (*args[i])->i);
			break;
		case AIN_REF_FLOAT:
			sys_message(" (%s=%f)", display_sjis0(f->arguments[i].name), (*args[i])->f);
			break;
		default:
			break;
		}
	}
	sys_message("\n");
}

static void trace_hll_call(struct ain_library *lib, struct ain_hll_function *f,
		      struct hll_function *fun, union vm_value *r, void *_args)
{
	for (unsigned i = 0; i < sizeof(traced_libraries)/sizeof(*traced_libraries); i++) {
		struct traced_library *l = &traced_libraries[i];
		if (!strcmp(lib->name, l->name)) {
			if (!l->should_trace || l->should_trace(f, _args)) {
				print_hll_trace(lib, f, fun, r, _args);
				return;
			}
			break;
		}
	}

	ffi_call(&fun->cif, (void*)fun->fun, r, _args);
}
#endif /* TRACE_HLL */

/* Type tag of the CALLHLL whose HLL body is currently executing.
 * Exposed to HLL implementations through hll_generic_result_type() so that
 * generic Array/Container calls can materialise correctly typed elements. */
static int hll_result_tag = -1;

int hll_generic_result_type(void)
{
	return hll_result_tag;
}

/* A reference to a heap-owned object is just the object's handle (one stack
 * value); a reference to a value living inside a page is a two-value
 * [page, var] pair. This is not inferred from runtime behaviour: the AIN
 * variable-slot census (logs/ain_spec_slots.txt) shows AIN_REF_INT/FLOAT/
 * BOOL/ENUM, AIN_WRAP and AIN_IFACE are always followed by an AIN_VOID
 * placeholder slot, while AIN_REF_STRUCT/STRING/DELEGATE/ARRAY occupy one
 * slot; the ref-argument marshalling above has the same two-slot list.
 * Independent confirmation: tools/ain_refparam.c classifies all 452 typed
 * AIN_REF_HLL_PARAM call sites in Rance10.ain by how the following bytecode
 * consumes the result - tags 10/47/89/92 are 100% R_ASSIGN (two values),
 * tags 12/13/21 are 100% ASSIGN/CALLMETHOD (one value), no exceptions
 * (logs/ain_refparam.txt).
 * See Rance10_AINv12_字节码规范-20261107.md, "引用槽宽" . */
static int hll_ref_result_is_single_value(int tag)
{
	switch (tag) {
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_ARRAY:
	case AIN_DELEGATE:
	case AIN_FUNC_TYPE:
	case AIN_REF_STRING:
	case AIN_REF_STRUCT:
	case AIN_REF_ARRAY:
	case AIN_REF_DELEGATE:
	case AIN_REF_FUNC_TYPE:
	case AIN_ARRAY_INT:
	case AIN_ARRAY_FLOAT:
	case AIN_ARRAY_STRING:
	case AIN_ARRAY_STRUCT:
	case AIN_ARRAY_FUNC_TYPE:
	case AIN_ARRAY_BOOL:
	case AIN_ARRAY_LONG_INT:
	case AIN_ARRAY_DELEGATE:
	case AIN_REF_ARRAY_INT:
	case AIN_REF_ARRAY_FLOAT:
	case AIN_REF_ARRAY_STRING:
	case AIN_REF_ARRAY_STRUCT:
	case AIN_REF_ARRAY_FUNC_TYPE:
	case AIN_REF_ARRAY_BOOL:
	case AIN_REF_ARRAY_LONG_INT:
	case AIN_REF_ARRAY_DELEGATE:
		return 1;
	default: /* int/float/bool/long/enum/iface/wrap... -> [page, var] */
		return 0;
	}
}

void hll_call(int libno, int fno, int result_tag)
{
	struct ain_hll_function *f = &ain->libraries[libno].functions[fno];
	static int hll_trace = -1;
	if (hll_trace < 0) {
		const char *t = getenv("XSYS4_HLL_TRACE");
		hll_trace = t ? 1 : 0;
	}
	if (hll_trace)
		NOTICE("[hll] lib=%d fno=%d tag=%d nr_args=%d sp=%d ret=%d %s.%s",
		       libno, fno, result_tag, f->nr_arguments, stack_ptr,
		       f->return_type.data, ain->libraries[libno].name, f->name);

	if (!libraries[libno])
		VM_ERROR("Unimplemented HLL function: %s.%s", ain->libraries[libno].name, f->name);

	struct hll_function *fun = &libraries[libno][fno];
	if (!fun->fun)
		VM_ERROR("Unimplemented HLL function: %s.%s", ain->libraries[libno].name, f->name);

	void *args[HLL_MAX_ARGS];
	void *ptrs[HLL_MAX_ARGS];
	// Copy reference arguments to the stack to protect against heap
	// reallocation during HLL calls.
	void *heap_ptrs[HLL_MAX_ARGS];
	int heap_slots[HLL_MAX_ARGS];
	/* AIN_HLL_PARAM is an 8-byte value while VM stack slots are 4 bytes.
	 * Materialise full-width temporaries so libffi never reads into the
	 * adjacent stack slot (that produced values like 0x7F0000003E). */
	uint64_t hll_values[HLL_MAX_ARGS];
	memset(heap_slots, 0, sizeof(heap_slots));
	memset(hll_values, 0, sizeof(hll_values));

	for (int i = f->nr_arguments - 1; i >= 0; i--) {
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
		case AIN_REF_LONG_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT: {
			// need to create pointer for immediate ref types
			stack_ptr -= 2;
			int pageno = stack[stack_ptr].i;
			int varno  = stack[stack_ptr+1].i;
			ptrs[i] = &heap[pageno].page->values[varno];
			args[i] = &ptrs[i];
			break;
		}
		case AIN_STRING:
			stack_ptr--;
			args[i] = &heap[stack[stack_ptr].i].s;
			break;
		case AIN_REF_STRING:
			stack_ptr--;
			heap_slots[i] = stack[stack_ptr].i;
			heap_ptrs[i] = heap[stack[stack_ptr].i].s;
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		case AIN_STRUCT:
		case AIN_ARRAY_TYPE:
			stack_ptr--;
			args[i] = &heap[stack[stack_ptr].i].page;
			break;
		case AIN_REF_STRUCT:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY:   /* 80: null 时预分配一个空数组，否则沿用原 slot */
			stack_ptr--;
			heap_slots[i] = stack[stack_ptr].i;
			if (heap_slots[i] <= 0) {
				/* 调用者的数组变量是 null：先给它一个空数组对象，
				 * 否则 HLL 函数无法把新建的数组写回（ref 只给了值，没给变量位置） */
				struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0);
				p->array.rank = 1;
				p->array.struct_type = -1;
				heap_slots[i] = heap_alloc_page(p);
				stack[stack_ptr].i = heap_slots[i];
			}
			heap_ptrs[i] = heap[heap_slots[i]].page;
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		case AIN_HLL_PARAM:
			stack_ptr--;
			hll_values[i] = (uint32_t)stack[stack_ptr].i;
			args[i] = &hll_values[i];
			break;
		case AIN_HLL_FUNC:
			/* D59: most t95 args are one fno; only BinarySearch predicate is [page, fno]. */
			if (libno == 5) {
				stack_ptr -= 2;
				hll_values[i] = (uint32_t)stack[stack_ptr + 1].i;
			} else {
				stack_ptr--;
				hll_values[i] = (uint32_t)stack[stack_ptr].i;
			}
			args[i] = &hll_values[i];
			break;
		case AIN_REF_DELEGATE:
		case AIN_DELEGATE:
			/* Delegate.Set expects a delegate PAGE pointer (D59). */
			stack_ptr--;
			heap_slots[i] = stack[stack_ptr].i;
			heap_ptrs[i] = (heap_slots[i] > 0 && heap_slots[i] < heap_size) ? heap[heap_slots[i]].page : NULL;
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		default:
			stack_ptr--;
			args[i] = &stack[stack_ptr];
			break;
		}
	}

	union vm_value r;
	hll_result_tag = result_tag;
#ifdef TRACE_HLL
	trace_hll_call(&ain->libraries[libno], f, fun, &r, args);
#else
	ffi_call(&fun->cif, (void*)fun->fun, &r, args);
#endif
	hll_result_tag = -1;


	for (int i = 0, j = 0; i < f->nr_arguments; i++, j++) {
		// XXX: We don't increase the ref count when passing ref arguments to HLL
		//      functions, so we need to avoid decreasing it via variable_fini
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
		case AIN_REF_LONG_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT:
		case AIN_HLL_FUNC:
			if (libno == 5)
				j++;
			break;
		case AIN_REF_STRING:
			heap[heap_slots[i]].s = heap_ptrs[i];
			break;
		case AIN_REF_STRUCT:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY:   /* 80 */
			heap[heap_slots[i]].page = heap_ptrs[i];
			break;
		case AIN_REF_FUNC_TYPE:
		case AIN_REF_DELEGATE:
		case AIN_DELEGATE:
			break;
		case AIN_ARRAY_TYPE:
			// Sys41VM doesn't make a copy when passing an array by value.
			if (ain->version <= 1)
				break;
			// fallthrough
		default:
			variable_fini(stack[stack_ptr+j], f->arguments[i].type.data, false);
			break;
		}
	}

	int slot;
	switch (f->return_type.data) {
	case AIN_VOID:
		break;
	case AIN_STRING:
		slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = r.ref;
		stack_push(slot);
		break;
	case AIN_BOOL:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		stack_push(*(bool*)&r);
#pragma GCC diagnostic pop
		break;
    case AIN_REF_HLL_PARAM: {
        /* A HLL ref-param returns an element address. Resolve it only
         * against THIS call's argument slots: scanning the whole heap can
         * match a stale/aliased slot and create a dangling reference. */
        void *addr = r.ref;
        int32_t hi = -1, pi = -1;
        if (addr) {
            for (int i = 0; i < f->nr_arguments; i++) {
                int slot = heap_slots[i];
                if (slot <= 0 || slot >= heap_size)
                    continue;
                if (heap[slot].type != VM_PAGE)
                    continue;
                struct page *pg = heap[slot].page;
                if (!pg)
                    continue;
                union vm_value *base = pg->values;
                if ((union vm_value *)addr >= base &&
                    (union vm_value *)addr < base + pg->nr_vars) {
                    hi = slot;
                    pi = (int32_t)((union vm_value *)addr - base);
                    break;
                }
            }
        }
        /* AIN v12 CALLHLL's third immediate is the concrete element type of
         * this generic call. AIN_REF_HLL_PARAM returns a REFERENCE, whose
         * stack representation is decided by that type (see the helper's
         * comment), not by guessing a pair of (index, value).
         *
         * Disassembly of CASTimerManager@CreateHandle proves both forms are
         * consumed inside one function:
         *   0x56a0 CALLHLL Array.EmplaceBack tag=13(AIN_STRUCT) -> 1 value,
         *          assigned to local[1] (AIN_REF_STRUCT), then DELETEd;
         *   0x56f6 CALLHLL Array.EmplaceBack tag=47(AIN_BOOL)  -> 2 values,
         *          stored by R_ASSIGN into local[2..3] (AIN_REF_BOOL +
         *          AIN_VOID placeholder) and dereferenced by ASSIGN.
         * Returning the raw element index as the leading value made DELETE
         * heap_unref() an unrelated heap slot (D31). */
        if (result_tag >= 0 && addr && hi > 0) {
            union vm_value v = *(union vm_value *)addr;
            if (hll_ref_result_is_single_value(result_tag)) {
                /* The container keeps one reference, the caller DELETEs its
                 * own. Take an extra one so DELETE cannot drop the last ref
                 * and leave the container with a dangling slot (D21). */
                if (v.i > 0 && v.i < heap_size && heap[v.i].ref > 0)
                    heap_ref(v.i);
                stack_push(v);
            } else {
                stack_push((union vm_value){ .i = hi });
                stack_push((union vm_value){ .i = pi });
            }
        } else {
            stack_push((union vm_value){ .i = hi });
            stack_push((union vm_value){ .i = pi });
        }
        break;
    }
	default:
		stack_push(r);
		break;
	}
}

extern struct static_library lib_ACXLoader;
extern struct static_library lib_ACXLoaderP2;
extern struct static_library lib_ADVSYS;
extern struct static_library lib_AFAFactory;
extern struct static_library lib_AliceLogo;
extern struct static_library lib_AliceLogo2;
extern struct static_library lib_AliceLogo3;
extern struct static_library lib_AliceLogo4;
extern struct static_library lib_AliceLogo5;
extern struct static_library lib_AnteaterADVEngine;
extern struct static_library lib_Clipboard;
extern struct static_library lib_Delegate;
extern struct static_library lib_system;
extern struct static_library lib_EXWriter;
extern struct static_library lib_ADVEngine;
extern struct static_library lib_String;
extern struct static_library lib_InstallInfo;
extern struct static_library lib_Sys43VM;
extern struct static_library lib_TextFile;
extern struct static_library lib_TextSurfaceManager;
extern struct static_library lib_Int;
extern struct static_library lib_Float;
extern struct static_library lib_FileDialog;
extern struct static_library lib_PCInfo;
extern struct static_library lib_Array;
extern struct static_library lib_BanMisc;
extern struct static_library lib_Bitarray;
extern struct static_library lib_CalcTable;
extern struct static_library lib_CGManager;
extern struct static_library lib_ChipmunkSpriteEngine;
extern struct static_library lib_ChrLoader;
extern struct static_library lib_CommonSystemData;
extern struct static_library lib_Confirm;
extern struct static_library lib_Confirm2;
extern struct static_library lib_Confirm3;
extern struct static_library lib_CrayfishLogViewer;
extern struct static_library lib_Cursor;
extern struct static_library lib_DALKDemo;
extern struct static_library lib_DALKEDemo;
extern struct static_library lib_Data;
extern struct static_library lib_DataFile;
extern struct static_library lib_Discord;
extern struct static_library lib_DrawDungeon;
extern struct static_library lib_DrawDungeon2;
extern struct static_library lib_DrawDungeon14;
extern struct static_library lib_DrawEffect;
extern struct static_library lib_DrawField;
extern struct static_library lib_DrawGraph;
extern struct static_library lib_DrawMovie;
extern struct static_library lib_DrawMovie2;
extern struct static_library lib_DrawMovie3;
extern struct static_library lib_DrawNumeral;
extern struct static_library lib_DrawPluginManager;
extern struct static_library lib_DrawRain;
extern struct static_library lib_DrawRipple;
extern struct static_library lib_DrawSimpleText;
extern struct static_library lib_DrawSnow;
extern struct static_library lib_File;
extern struct static_library lib_File2;
extern struct static_library lib_FileOperation;
extern struct static_library lib_FillAngle;
extern struct static_library lib_GoatGUIEngine;
extern struct static_library lib_Gpx2Plus;
extern struct static_library lib_GUIEngine;
extern struct static_library lib_HTTPDownloader;
extern struct static_library lib_IbisInputEngine;
extern struct static_library lib_InputDevice;
extern struct static_library lib_InputString;
extern struct static_library lib_KiwiSoundEngine;
extern struct static_library lib_LoadCG;
extern struct static_library lib_MADLoader;
extern struct static_library lib_MainEXFile;
extern struct static_library lib_MainSurface;
extern struct static_library lib_MamanyoDemo;
extern struct static_library lib_MamanyoSDemo;
extern struct static_library lib_MarmotModelEngine;
extern struct static_library lib_Math;
extern struct static_library lib_MapLoader;
extern struct static_library lib_MenuMsg;
extern struct static_library lib_MonsterInfo;
extern struct static_library lib_MsgLogManager;
extern struct static_library lib_MsgLogViewer;
extern struct static_library lib_MsgSkip;
extern struct static_library lib_MusicSystem;
extern struct static_library lib_NewFont;
extern struct static_library lib_OutputLog;
extern struct static_library lib_P3MapSprite;
extern struct static_library lib_P3SquareSprite;
extern struct static_library lib_PassRegister;
extern struct static_library lib_PastelChime2;
extern struct static_library lib_PartsEngine;
extern struct static_library lib_PixelRestore;
extern struct static_library lib_PlayDemo;
extern struct static_library lib_PlayMovie;
extern struct static_library lib_ReignEngine;
extern struct static_library lib_SACT2;
extern struct static_library lib_SACTDX;
extern struct static_library lib_SealEngine;
extern struct static_library lib_SengokuRanceFont;
extern struct static_library lib_Sound2ex;
extern struct static_library lib_SoundFilePlayer;
extern struct static_library lib_StoatSpriteEngine;
extern struct static_library lib_StretchHelper;
extern struct static_library lib_SystemService;
extern struct static_library lib_SystemServiceEx;
extern struct static_library lib_TapirEngine;
extern struct static_library lib_Timer;
extern struct static_library lib_Toushin3Loader;
extern struct static_library lib_vmAnime;
extern struct static_library lib_vmArray;
extern struct static_library lib_vmCG;
extern struct static_library lib_vmChrLoader;
extern struct static_library lib_vmCursor;
extern struct static_library lib_vmDalkGaiden;
extern struct static_library lib_vmData;
extern struct static_library lib_vmDialog;
extern struct static_library lib_vmDrawGauge;
extern struct static_library lib_vmDrawMsg;
extern struct static_library lib_vmDrawNumber;
extern struct static_library lib_vmFile;
extern struct static_library lib_vmGraph;
extern struct static_library lib_vmGraphQuake;
extern struct static_library lib_vmKey;
extern struct static_library lib_vmMapLoader;
extern struct static_library lib_vmMsgLog;
extern struct static_library lib_vmMsgSkip;
extern struct static_library lib_vmMusic;
extern struct static_library lib_vmSound;
extern struct static_library lib_vmSprite;
extern struct static_library lib_vmString;
extern struct static_library lib_vmSurface;
extern struct static_library lib_vmSystem;
extern struct static_library lib_vmTimer;
extern struct static_library lib_ValueEncryption;
extern struct static_library lib_VSFile;

static bool library_alias(const char *ain_name, const char *lib_name)
{
	/* Rance X 及之后的游戏把 AnteaterADVEngine 改名为 AnteaterADVLogList */
	if (!strcmp(ain_name, "AnteaterADVLogList") && !strcmp(lib_name, "AnteaterADVEngine"))
		return true;
	return false;
}

static struct static_library *static_libraries[] = {
	&lib_ACXLoader,
	&lib_ACXLoaderP2,
	&lib_ADVSYS,
	&lib_AFAFactory,
	&lib_AliceLogo,
	&lib_AliceLogo2,
	&lib_AliceLogo3,
	&lib_AliceLogo4,
	&lib_AliceLogo5,
	&lib_AnteaterADVEngine,
	&lib_Clipboard,
	&lib_Delegate,
	&lib_system,
	&lib_EXWriter,
	&lib_ADVEngine,
	&lib_String,
	&lib_InstallInfo,
	&lib_Sys43VM,
	&lib_TextFile,
	&lib_TextSurfaceManager,
	&lib_Int,
	&lib_Float,
	&lib_FileDialog,
	&lib_PCInfo,
	&lib_Array,
	&lib_BanMisc,
	&lib_Bitarray,
	&lib_CalcTable,
	&lib_CGManager,
	&lib_ChipmunkSpriteEngine,
	&lib_ChrLoader,
	&lib_CommonSystemData,
	&lib_Confirm,
	&lib_Confirm2,
	&lib_Confirm3,
	&lib_CrayfishLogViewer,
	&lib_Cursor,
	&lib_DALKDemo,
	&lib_DALKEDemo,
	&lib_Data,
	&lib_DataFile,
	&lib_Discord,
	&lib_DrawDungeon,
	&lib_DrawDungeon2,
	&lib_DrawDungeon14,
	&lib_DrawEffect,
	&lib_DrawField,
	&lib_DrawGraph,
	&lib_DrawMovie,
	&lib_DrawMovie2,
	&lib_DrawMovie3,
	&lib_DrawNumeral,
	&lib_DrawPluginManager,
	&lib_DrawRain,
	&lib_DrawRipple,
	&lib_DrawSimpleText,
	&lib_DrawSnow,
	&lib_File,
	&lib_File2,
	&lib_FileOperation,
	&lib_FillAngle,
	&lib_GoatGUIEngine,
	&lib_Gpx2Plus,
	&lib_GUIEngine,
	&lib_HTTPDownloader,
	&lib_IbisInputEngine,
	&lib_InputDevice,
	&lib_InputString,
	&lib_KiwiSoundEngine,
	&lib_LoadCG,
	&lib_MADLoader,
	&lib_MainEXFile,
	&lib_MainSurface,
	&lib_MamanyoDemo,
	&lib_MamanyoSDemo,
	&lib_MarmotModelEngine,
	&lib_Math,
	&lib_MapLoader,
	&lib_MenuMsg,
	&lib_MonsterInfo,
	&lib_MsgLogManager,
	&lib_MsgLogViewer,
	&lib_MsgSkip,
	&lib_MusicSystem,
	&lib_NewFont,
	&lib_OutputLog,
	&lib_P3MapSprite,
	&lib_P3SquareSprite,
	&lib_PassRegister,
	&lib_PastelChime2,
	&lib_PartsEngine,
	&lib_PixelRestore,
	&lib_PlayDemo,
	&lib_PlayMovie,
	&lib_ReignEngine,
	&lib_SACT2,
	&lib_SACTDX,
	&lib_SealEngine,
	&lib_SengokuRanceFont,
	&lib_Sound2ex,
	&lib_SoundFilePlayer,
	&lib_StoatSpriteEngine,
	&lib_StretchHelper,
	&lib_SystemService,
	&lib_SystemServiceEx,
	&lib_TapirEngine,
	&lib_Timer,
	&lib_Toushin3Loader,
	&lib_vmAnime,
	&lib_vmArray,
	&lib_vmCG,
	&lib_vmChrLoader,
	&lib_vmCursor,
	&lib_vmDalkGaiden,
	&lib_vmData,
	&lib_vmDialog,
	&lib_vmDrawGauge,
	&lib_vmDrawMsg,
	&lib_vmDrawNumber,
	&lib_vmFile,
	&lib_vmGraph,
	&lib_vmGraphQuake,
	&lib_vmKey,
	&lib_vmMapLoader,
	&lib_vmMsgLog,
	&lib_vmMsgSkip,
	&lib_vmMusic,
	&lib_vmSound,
	&lib_vmSprite,
	&lib_vmString,
	&lib_vmSurface,
	&lib_vmSystem,
	&lib_vmTimer,
	&lib_ValueEncryption,
	&lib_VSFile,
	NULL
};

static ffi_type *ain_to_ffi_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_VOID:
		return &ffi_type_void;
	case AIN_INT:
	case AIN_BOOL:
		return &ffi_type_sint32;
	case AIN_LONG_INT:
		return &ffi_type_sint64;
	case AIN_FLOAT:
		return &ffi_type_float;
	case AIN_HLL_PARAM:     // 74: 泛型参数，按 8 字节值传递（不是指针！）
		return &ffi_type_uint64;
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_FUNC_TYPE:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
	case AIN_REF_TYPE:
	case AIN_REF_HLL_PARAM: // 75: ref generic param（返回引用，按指针）
	case AIN_HLL_FUNC:      // 95: predicate function type (needs pointer)
	case AIN_ARRAY:         // 79: generic array
	case AIN_IMAIN_SYSTEM: // ???
		return &ffi_type_pointer;
	default:
		ERROR("Unhandled type in HLL function: %s", ain_strtype(ain, type, -1));
	}
}

static void link_static_library_function(struct hll_function *dst, struct ain_hll_function *src, void *funcptr)
{
	dst->fun = funcptr;
	dst->nr_args = src->nr_arguments;
	dst->args = xcalloc(dst->nr_args, sizeof(ffi_type*));

	for (unsigned int i = 0; i < dst->nr_args; i++) {
		dst->args[i] = ain_to_ffi_type(src->arguments[i].type.data);
	}
	dst->return_type = ain_to_ffi_type(src->return_type.data);

	if (ffi_prep_cif(&dst->cif, FFI_DEFAULT_ABI, dst->nr_args, dst->return_type, dst->args) != FFI_OK)
		ERROR("Failed to link HLL function");
}

/*
 * "Link" a library that has been compiled into the xsystem4 executable.
 */
static struct hll_function *link_static_library(struct ain_library *ainlib, struct static_library *lib)
{
	struct hll_function *dst = xcalloc(ainlib->nr_functions, sizeof(struct hll_function));

	for (int i = 0; i < ainlib->nr_functions; i++) {
		for (int j = 0; lib->functions[j].name; j++) {
			if (!strcmp(ainlib->functions[i].name, lib->functions[j].name)) {
				link_static_library_function(&dst[i], &ainlib->functions[i], lib->functions[j].fun);
				break;
			}
		}
		if (!dst[i].fun)
			;//WARNING("Unimplemented library function: %s.%s", ainlib->name, ainlib->functions[i].name);
		else if (ainlib->functions[i].nr_arguments >= HLL_MAX_ARGS)
			ERROR("Too many arguments to library function: %s", ainlib->functions[i].name);
	}

	return dst;
}

static void library_run(struct static_library *lib, const char *name)
{
	for (int i = 0; lib->functions[i].name; i++) {
		if (!strcmp(lib->functions[i].name, name)) {
			((void(*)(void))lib->functions[i].fun)();
			break;
		}
	}
}

static void library_run_all(const char *name)
{
	for (int i = 0; i < ain->nr_libraries; i++) {
		for (int j = 0; static_libraries[j]; j++) {
			if (!strcmp(ain->libraries[i].name, static_libraries[j]->name) ||
			    library_alias(ain->libraries[i].name, static_libraries[j]->name)) {
				library_run(static_libraries[j], name);
				break;
			}
		}
	}
}

static void link_libraries(void)
{
	if (libraries)
		return;

	libraries = xcalloc(ain->nr_libraries, sizeof(struct hll_function*));

	for (int i = 0; i < ain->nr_libraries; i++) {
		for (int j = 0; static_libraries[j]; j++) {
			if (!strcmp(ain->libraries[i].name, static_libraries[j]->name) ||
			    library_alias(ain->libraries[i].name, static_libraries[j]->name)) {
				libraries[i] = link_static_library(&ain->libraries[i], static_libraries[j]);
				break;
			}
		}
		if (!libraries[i])
			WARNING("Unimplemented library: %s", ain->libraries[i].name);
	}
}

bool libraries_initialized = false;

void init_libraries(void)
{
	library_run_all("_PreLink");
	link_libraries();
	library_run_all("_ModuleInit");
	libraries_initialized = true;
}

void exit_libraries(void)
{
	if (libraries_initialized)
		library_run_all("_ModuleFini");
}

void static_library_replace(struct static_library *lib, const char *name, void *fun)
{
	for (int i = 0; lib->functions[i].name; i++) {
		if (!strcmp(lib->functions[i].name, name)) {
			lib->functions[i].fun = fun;
			return;
		}
	}
	ERROR("No library function '%s.%s'", lib->name, name);
}
