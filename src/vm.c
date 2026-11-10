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
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include <assert.h>
#include <SDL.h> // for system.MsgBox

#include "system4.h"
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "debugger.h"
#include "input.h"
#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

static inline int32_t lint_clamp(int64_t n)
{
	if (n < 0)
		return 0;
	if (n > INT32_MAX)
		return INT32_MAX;
	return (int32_t)n;
}

#define INITIAL_STACK_SIZE 4096

// When the IP is set to VM_RETURN, the VM halts
#define VM_RETURN 0xFFFFFFFF

/*
 * NOTE: The current implementation is a simple bytecode interpreter.
 *       System40.exe uses a JIT compiler, and we should too.
 */

// The stack
union vm_value *stack = NULL; // the stack
int32_t stack_ptr = 0;        // pointer to the top of the stack
static size_t stack_size;     // current size of the stack

// Stack of function call frames
struct function_call call_stack[4096];
int32_t call_stack_ptr = 0;

struct ain *ain;
size_t instr_ptr = 0;

bool vm_reset_once = false;

// Read the opcode at ADDR.
static int16_t get_opcode(size_t addr)
{
	return LittleEndian_getW(ain->code, addr);
}

static const char *current_instruction_name(void)
{
	int16_t opcode = get_opcode(instr_ptr);
	if (opcode >= 0 && opcode < NR_OPCODES)
		return instructions[opcode].name;
	return "UNKNOWN OPCODE";
}

static int local_page_slot(void)
{
	return call_stack[call_stack_ptr-1].page_slot;
}

struct page *local_page(void)
{
	return heap[local_page_slot()].page;
}

struct page *get_local_page(int frame_no)
{
	if (frame_no < 0 || frame_no >= call_stack_ptr)
		return NULL;
	int slot = call_stack[call_stack_ptr - (frame_no + 1)].page_slot;
	return slot < 1 ? NULL : heap[slot].page;
}

union vm_value local_get(int varno)
{
	return local_page()->values[varno];
}

static void local_set(int varno, int32_t value)
{
	local_page()->values[varno].i = value;
}

static union vm_value *local_ptr(int varno)
{
	return local_page()->values + varno;
}

struct page *global_page(void)
{
	return heap[0].page;
}

union vm_value global_get(int varno)
{
	return heap[0].page->values[varno];
}

void global_set(int varno, union vm_value val, bool call_dtors)
{
	switch (ain->globals[varno].type.data) {
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_ARRAY_TYPE:
		if (heap[0].page->values[varno].i > 0) {
			if (call_dtors)
				heap_unref(heap[0].page->values[varno].i);
			else
				exit_unref(heap[0].page->values[varno].i);
		}
	default:
		break;
	}
	heap[0].page->values[varno] = val;
}

static int32_t struct_page_slot(void)
{
	return call_stack[call_stack_ptr-1].struct_page;
}

static struct page *struct_page(void)
{
	return heap[struct_page_slot()].page;
}

struct page *get_struct_page(int frame_no)
{
	if (frame_no < 0 || frame_no >= call_stack_ptr)
		return NULL;
	int slot = call_stack[call_stack_ptr - (frame_no + 1)].struct_page;
	return slot < 1 ? NULL : heap[slot].page;
}

union vm_value member_get(int varno)
{
	return struct_page()->values[varno];
}

static void member_set(int varno, int32_t value)
{
	struct_page()->values[varno].i = value;
}

union vm_value stack_peek(int n)
{
	return stack[stack_ptr - (1 + n)];
}

union vm_value stack_pop(void)
{
	stack_ptr--;
	return stack[stack_ptr];
}

static union vm_value *stack_peek_ptr(int n)
{
	return &stack[stack_ptr - (1 + n)];
}

// Pop a reference off the stack, returning the address of the referenced object.
static union vm_value *stack_pop_var(void)
{
	int32_t page_index = stack_pop().i;
	int32_t heap_index = stack_pop().i;
	/* D52: tolerate null / never-allocated references during Rance10's
	 * Parts UI init (e.g. ResetPartsUpdateEventLayerID @0x1f16f4).
	 * Both a bad heap_index and a 0-slot ARRAY_PAGE yield a -1 sentinel
	 * instead of aborting startup; owning objects still need proper
	 * initialisation (TECH_DEBT). */
	static union vm_value null_ref_slot = { .i = -1 };
	if (unlikely(!heap_index_valid(heap_index)))
		return &null_ref_slot;
	/* D72: a valid slot may still have a NULL page pointer. */
	if (unlikely(!heap[heap_index].page))
		return &null_ref_slot;
	/* D72: any 0-slot page (not just ARRAY_PAGE/index 0) is an
	 * uninitialised reference during Parts UI setup. */
	if (heap[heap_index].page && heap[heap_index].page->nr_vars == 0)
		return &null_ref_slot;
	if (unlikely(!heap[heap_index].page || page_index >= heap[heap_index].page->nr_vars)) {
		struct page *pg = heap[heap_index].page;
		VM_ERROR("Out of bounds page index: %d/%d (type=%d nr_vars=%d ref=%d v0=%d v1=%d)",
			 heap_index, page_index,
			 pg ? pg->type : -1, pg ? pg->nr_vars : -1, heap[heap_index].ref,
			 pg && pg->nr_vars > 0 ? pg->values[0].i : -9999,
			 pg && pg->nr_vars > 1 ? pg->values[1].i : -9999);
	}
	return &heap[heap_index].page->values[page_index];
}

/*
 * [D105] AIN v12 SR_ASSIGN is an ARRAY-ELEMENT struct assignment:
 * the stack holds [array, index, value] (three values). The old 2-operand
 * implementation popped (value, index) and treated the index as a heap slot,
 * so `global[i] = copy(struct)` overwrote heap[i].page with the copied page.
 * menu::detail::Init does exactly that with i=26, which is the live
 * CASTimerManager slot: the manager was replaced by an unrelated 57-member
 * struct, CheckHandle then read a stale member and the frame timer stopped.
 */
static void sr_assign_element(int arr, int idx, int val)
{
	if (arr <= 0 || (size_t)arr >= heap_size)
		return;
	struct page *ap = heap[arr].page;
	if (!ap || ap->type != ARRAY_PAGE)
		return;
	if (idx < 0 || idx >= ap->nr_vars)
		return;
	int old = ap->values[idx].i;
	if (val > 0 && val != -1 && (size_t)val < heap_size && heap[val].ref > 0)
		heap_ref(val);
	ap->values[idx].i = val;
	if (old > 0 && old != val && (size_t)old < heap_size && heap[old].ref > 0)
		heap_unref(old);
}


union vm_value *stack_peek_var(void)
{
	int32_t page_index = stack_peek(0).i;
	int32_t heap_index = stack_peek(1).i;
	if (unlikely(!heap_index_valid(heap_index)))
		VM_ERROR("Out of bounds heap index: %d/%d", heap_index, page_index);
	if (unlikely(!heap[heap_index].page || page_index >= heap[heap_index].page->nr_vars))
		VM_ERROR("Out of bounds page index: %d/%d", heap_index, page_index);
	return &heap[heap_index].page->values[page_index];
}

static void stack_push_string(struct string *s)
{
	int32_t heap_slot = heap_alloc_slot(VM_STRING);
	heap[heap_slot].s = s;
	stack[stack_ptr++].i = heap_slot;
}

static struct string *stack_peek_string(int n)
{
	return heap[stack_peek(n).i].s;
}

int vm_string_ref(struct string *s)
{
	int slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = string_ref(s);
	return slot;
}

int vm_copy_page(struct page *page)
{
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, copy_page(page));
	return slot;
}

union vm_value vm_copy(union vm_value v, enum ain_data_type type)
{
	/* D73: a null reference (-1) is common in freshly built structs;
	 * copying it must not call heap_get_page(-1). */
	if (v.i < 0)
		return v;
	switch (type) {
	case AIN_STRING:
		return (union vm_value) { .i = vm_string_ref(heap_get_string(v.i)) };
	case AIN_STRUCT:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
		return (union vm_value) { .i = vm_copy_page(heap_get_page(v.i)) };
	case AIN_REF_TYPE:
		heap_ref(v.i);
		return v;
	default:
		return v;
	}
}

static int get_function_by_name(const char *name)
{
	for (int i = 0; i < ain->nr_functions; i++) {
		if (!strcmp(name, ain->functions[i].name))
			return i;
	}
	return -1;
}

// Check whether a function's signature is compatible with a delegate type.
static bool function_matches_delegate(int dg_no, int fno)
{
	if (dg_no < 0 || dg_no >= ain->nr_delegates)
		return false;
	if (fno < 0 || fno >= ain->nr_functions)
		return false;
	struct ain_function_type *dg = &ain->delegates[dg_no];
	struct ain_function *f = &ain->functions[fno];

	if (dg->return_type.data != f->return_type.data ||
	    dg->return_type.struc != f->return_type.struc)
		return false;
	if (dg->nr_arguments != f->nr_args)
		return false;
	for (int i = 0; i < dg->nr_arguments; i++) {
		struct ain_type *a = &dg->variables[i].type;
		struct ain_type *b = &f->vars[i].type;
		if (a->data != b->data || a->struc != b->struc || a->rank != b->rank)
			return false;
	}
	return true;
}

static int scenario_label_addr(const char *lname)
{
	for (int i = 0; i < ain->nr_scenario_labels; i++) {
		if (!strcmp(ain->scenario_labels[i].name, lname)) {
			return ain->scenario_labels[i].address;
		}
	}
	VM_ERROR("Invalid scenario label: %s", display_sjis0(lname));
}

static int alloc_scenario_page(const char *fname)
{
	int fno, slot;
	struct ain_function *f;

	if ((fno = get_function_by_name(fname)) < 0)
		VM_ERROR("Invalid scenario function: %s", display_sjis0(fname));
	f = &ain->functions[fno];

	slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, alloc_page(LOCAL_PAGE, fno, f->nr_vars));
	for (int i = 0; i < f->nr_vars; i++) {
		if (f->vars[i].type.data == AIN_DELEGATE) {
			/* D67: delegate locals need a real page slot before DG_ASSIGN
			 * can copy into them; -1 silently discarded the copy. */
			heap[slot].page->values[i].i = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
		} else {
			heap[slot].page->values[i] = variable_initval_typed(&f->vars[i].type);
		}
	}
	return slot;
}

static void set_struct_page(int slot)
{
	struct function_call *frame = &call_stack[call_stack_ptr-1];
	frame->struct_page = slot;
	frame->struct_page_refed = 0;
	/* Keep `this` alive during the call (from Rance9 onwards).
	 * A destructor is invoked while unref'ing an object whose ref already
	 * reached 0; taking a ref there would resurrect it and cause a double
	 * free when the frame is torn down. Only take ownership if still live. */
	/* AIN v12: an AIN_IFACE value is 2 values [struct page, vtable offset]
	 * held in an IFACE_PAGE. When an interface reference is used as `this`,
	 * dispatch must target the implementing struct, not the 2-slot wrapper.
	 * This is a type-driven dereference, not a numeric heuristic. */
	if (slot > 0 && slot < heap_size && heap[slot].page &&
	    heap[slot].page->type == IFACE_PAGE) {
		int impl = heap[slot].page->values[0].i;
		if (impl > 0 && impl < heap_size && heap[impl].page)
			slot = impl;
	}
	/* Reject non-heap values. Never dereference heap[slot] out of range. */
	if (slot < 0 || slot >= heap_size)
		slot = -1;
	frame->struct_page = slot;
	if (AIN_VERSION_GTE(ain, 6, 1) && slot > 0 && heap[slot].ref > 0) {
		heap_ref(slot);
		frame->struct_page_refed = 1;
	}
}

static void unref_call_frame(struct function_call *frame)
{
	/* Release `this` only if set_struct_page actually took a reference. */
	if (frame->struct_page_refed)
		heap_unref(frame->struct_page);
	heap_unref(frame->page_slot);
}

static void scenario_jump(int address)
{
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		unref_call_frame(&call_stack[i]);
	}
	call_stack_ptr = 0;
	instr_ptr = address;
}

static void scenario_call(int slot)
{
	int fno = heap[slot].page->index;
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		unref_call_frame(&call_stack[i]);
	}
	call_stack[0] = (struct function_call) {
		.fno = fno,
		.call_address = instr_ptr,
		.return_address = VM_RETURN,
		.page_slot = slot,
		.struct_page = -1,
		.struct_page_refed = 0,
	};
	call_stack_ptr = 1;
	instr_ptr = ain->functions[fno].address;
}

/*
 * System 4 calling convention:
 *   - caller pushes arguments, in order
 *   - CALLFUNC creates stack frame, pops arguments into local page
 *   - callee pushes return value on the stack
 *   - RETURN jumps to return address (saved in stack frame)
 */
static int _function_call(int fno, int return_address)
{
	struct ain_function *f = &ain->functions[fno];
	if (fno == 147) {
		NOTICE(">>> CASTimer@0 构造函数被调用 (ptr=%d)", call_stack_ptr);
	}
	if (f->address == 0) {
		NOTICE("_function_call: fno=%d (%s) address=0, ret=0x%x, ptr=%d",
		       fno, f->name ? f->name : "?", return_address, call_stack_ptr);
	}
	{
		static int dbg2 = 0;
		if (dbg2 < 30 && f->name && (strstr(f->name, "Create") || strstr(f->name, "Release"))) {
			NOTICE("_function_call: [ctor] fno=%d %s @0x%x", fno, f->name, f->address);
			dbg2++;
		}
	}
	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, alloc_page(LOCAL_PAGE, fno, f->nr_vars));
	heap[slot].page->local.struct_ptr = -1;

	call_stack[call_stack_ptr++] = (struct function_call) {
		.fno = fno,
		.call_address = instr_ptr,
		.return_address = return_address,
		.page_slot = slot,
		.struct_page = -1,
	};
	// initialize local variables
	for (int i = f->nr_args; i < f->nr_vars; i++) {
		if (f->vars[i].type.data == AIN_DELEGATE) {
			/* D67: delegate locals need a real page slot before DG_ASSIGN
			 * can copy into them; -1 silently discarded the copy. */
			heap[slot].page->values[i].i = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
		} else {
			heap[slot].page->values[i] = variable_initval_typed(&f->vars[i].type);
		}
		if (ain->version <= 1 && f->vars[i].type.data == AIN_STRUCT) {
			create_struct(f->vars[i].type.struc, &heap[slot].page->values[i]);
		}
	}
	// jump to function start
	instr_ptr = ain->functions[fno].address;

	return slot;
}

static void function_call(int fno, int return_address)
{
	/* AIN reserves fno 0 / address 0 for the NULL function. A call to it
	 * is a no-op: consume its arguments (there are none) without creating
	 * a frame or jumping to address 0. The caller's instruction pointer
	 * then advances by the normal ip_inc of CALLFUNC/CALLMETHOD. */
	if (fno == 0 && ain->functions[0].address == 0) {
		struct ain_function *nf = &ain->functions[0];
		for (int i = 0; i < nf->nr_args; i++)
			stack_pop();
		/* CALLMETHOD/CALLFUNC are JMP opcodes with ip_inc == 0; the normal
		 * path advances instr_ptr inside _function_call. Since we skip that,
		 * advance to the caller's return address ourselves. */
		instr_ptr = return_address;
		return;
	}

	int slot = _function_call(fno, return_address);

	// pop arguments, store in local page
	struct ain_function *f = &ain->functions[fno];
	for (int i = f->nr_args - 1; i >= 0; i--) {
		heap[slot].page->values[i] = stack_pop();
		switch (f->vars[i].type.data) {
		case AIN_REF_TYPE:
			heap_ref(heap[slot].page->values[i].i);
			break;
		default:
			break;
		}
	}
}

/* vm_call 用：fno 由参数给出，栈上只有 [struct_page] */
static void method_call_fno(int fno, int return_address)
{
	function_call(fno, return_address);
	int struct_page = stack_pop().i;
	set_struct_page(struct_page);
	heap[call_stack[call_stack_ptr-1].page_slot].page->local.struct_ptr = struct_page;
}

/* CALLMETHOD 用：栈布局 [obj, fno, args...]，fno 在 args 下面一层 */
static int struct_no_from_function(int fno);

static void method_call(int nargs, int return_address)
{
	int fno = stack_peek(nargs).i;
	if (fno < 0 || fno >= ain->nr_functions)
		VM_ERROR("CALLMETHOD: invalid function index %d", fno);
	function_call(fno, return_address);   /* 弹掉 nargs 个 args */
	stack_pop();                          /* 弹掉 fno */
	int obj = stack_pop().i;              /* 弹 obj */
	/* D48/D71: empty obj -> materialise the owning struct. Prefer the
	 * name-derived struct no; the AIN struct_type field is wrong on v12.
	 * [HACK-POC D83] Also rebuild when `this` resolves to a live slot that no
	 * longer holds a STRUCT/IFACE page: that means an object was released while
	 * a holder still referenced its slot, and the slot has since been recycled
	 * (observed: a LOCAL_PAGE handed to CPartsFunctionSet@Init). Rebuilding
	 * loses the old state, but the object is already gone; see TECH_DEBT D83. */
	/* Only the LOCAL_PAGE case is the known slot-recycling symptom; rebuilding on
	 * every non-STRUCT page destroyed live containers (CASTimerManager.handles
	 * got replaced by a fresh empty array, so every handle went stale). */
	bool _obj_bad = obj > 0 && (size_t)obj < heap_size && heap[obj].page &&
		heap[obj].page->type != STRUCT_PAGE && heap[obj].page->type != IFACE_PAGE;
	if (obj <= 0 || _obj_bad) {
		int sno = struct_no_from_function(fno);
		if (sno < 0)
			sno = ain->functions[fno].struct_type;
		if (sno >= 0 && sno < ain->nr_structures)
			obj = alloc_struct(sno);
	}
	set_struct_page(obj);
	heap[call_stack[call_stack_ptr-1].page_slot].page->local.struct_ptr = obj;
}

/* D71: AIN v12 function->struct_type is unreliable (CreateOld reports 0
 * but belongs to struct 11). The name "NS::Struct@Method" encodes the
 * owning struct, so resolve it by name when a CALLMETHOD has no obj. */
static int struct_no_from_function(int fno)
{
	if (fno < 0 || fno >= ain->nr_functions || !ain->functions[fno].name)
		return -1;
	const char *name = ain->functions[fno].name;
	const char *at = strrchr(name, '@');
	if (!at)
		return -1;
	size_t n = (size_t)(at - name);
	for (int i = 0; i < ain->nr_structures; i++) {
		const char *sn = ain->structures[i].name;
		if (sn && strlen(sn) == n && !strncmp(sn, name, n))
			return i;
	}
	return -1;
}
static void vm_execute(void);

static void delegate_call(int dg_no, int return_address)
{
	if (dg_no < 0 || dg_no >= ain->nr_delegates)
		VM_ERROR("Invalid delegate index");

	// stack: [arg0, ..., dg_page, dg_index, [return_value]]
	int return_values = (ain->delegates[dg_no].return_type.data != AIN_VOID) ? 1 : 0;
	int dg_page = stack_peek(1 + return_values).i;
	int dg_index = stack_peek(0 + return_values).i;
	int obj, fun;
	if (delegate_get(heap_get_delegate_page(dg_page), dg_index, &obj, &fun)) {
		// pop previous return value
		if (ain->delegates[dg_no].return_type.data != AIN_VOID) {
			stack_pop();
		}
		// increment dg_index
		stack[stack_ptr - 1].i++;

		int slot = _function_call(fun, instr_ptr + instruction_width(DG_CALL));

		// copy arguments into local page
		struct ain_function_type *dg = &ain->delegates[dg_no];
		for (int i = 0; i < dg->nr_arguments; i++) {
			union vm_value arg = stack_peek((dg->nr_arguments + 1) - i);
			heap[slot].page->values[i] = vm_copy(arg, dg->variables[i].type.data);
		}

		set_struct_page(obj);
	} else {
		// call finished: clean up stack and jump to return address
		union vm_value r;
		if (return_values) {
			r = stack_pop();
		}
		stack_pop(); // dg_index
		stack_pop(); // dg_page
		for (int i = ain->delegates[dg_no].nr_variables - 1; i >= 0; i--) {
			union vm_value v = stack_pop();
			enum ain_data_type type = ain->delegates[dg_no].variables[i].type.data;
			switch (type) {
			case AIN_REF_TYPE:
				break;
			default:
				variable_fini(v, type, true);
				break;
			}
		}
		if (return_values) {
			stack_push(r);
		}
		instr_ptr = get_argument(1);
	}
}

void vm_call(int fno, int struct_page)
{
	size_t saved_ip = instr_ptr;
	if (struct_page < 0) {
		function_call(fno, VM_RETURN);
	} else {
		/* AIN v12: constructor arguments are already on the stack.
		 * method_call_fno expects [this, args...] (this BELOW arguments),
		 * so insert this below nr_args arguments instead of pushing on top. */
		if (fno < 0 || fno >= ain->nr_functions)
			VM_ERROR("vm_call: invalid function index %d", fno);
		int nargs = ain->functions[fno].nr_args;
		if (nargs > 32)
			VM_ERROR("vm_call: too many arguments %d", nargs);
		union vm_value args[32];
		for (int i = nargs - 1; i >= 0; i--)
			args[i] = stack_pop();
		stack_push(struct_page);
		for (int i = 0; i < nargs; i++)
			stack_push(args[i]);
		method_call_fno(fno, VM_RETURN);
	}
	vm_execute();
	instr_ptr = saved_ip;
}

static void function_return(void)
{
	if (call_stack_ptr <= 0) {
		NOTICE("RET: 调用栈已空，VM 退出");
		vm_exit(0);
		return;
	}
	struct function_call *cf = &call_stack[call_stack_ptr - 1];
	if (cf->return_address == 0) {
		const char *nm = "?";
		if (cf->fno >= 0 && cf->fno < ain->nr_functions && ain->functions[cf->fno].name)
			nm = ain->functions[cf->fno].name;
		NOTICE("RET 异常: fno=%d (%s) return_address=0, ptr=%d", cf->fno, nm, call_stack_ptr);
	}
	unref_call_frame(cf);
	instr_ptr = cf->return_address;
	call_stack_ptr--;
}

static const SDL_MessageBoxButtonData ok_cancel_buttons[] = {
	{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK" },
	{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
};

static const SDL_MessageBoxButtonData stop_continue_buttons[] = {
	{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Stop" },
	{ 0, 0, "Continue" },
};

static struct string *get_func_stack_name(int index)
{
	int i = call_stack_ptr - (1 + index);
	if (i < 0 || i >= call_stack_ptr) {
		return string_ref(&EMPTY_STRING);
	}
	struct function_call *call = &call_stack[i];
	struct ain_function *fun = &ain->functions[call->fno];
	return cstr_to_string(fun->name);
}

static void system_call(enum syscall_code code)
{
	switch (code) {
	case SYS_EXIT: {// system.Exit(int nResult)
		vm_exit(stack_pop().i);
		break;
	}
	case SYS_GLOBAL_SAVE: { // system.GlobalSave(string szKeyName, string szFileName)
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(save_globals(heap_get_string(keyname)->text, heap_get_string(filename)->text, NULL, NULL));
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_GLOBAL_LOAD: { // system.GlobalLoad(string szKeyName, string szFileName)
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(load_globals(heap_get_string(keyname)->text, heap_get_string(filename)->text, NULL, NULL));
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_LOCK_PEEK: // system.LockPeek(void)
	case SYS_UNLOCK_PEEK: {// system.UnlockPeek(void)
		stack_push(1);
		break;
	}
	case SYS_RESET: {
		vm_reset();
		break;
	}
	case SYS_OUTPUT: {// system.Output(string szText)
		struct string *str = stack_peek_string(0);
		log_message("stdout", "%s", display_sjis0(str->text));
		// XXX: caller S_POPs
		break;
	}
	case SYS_MSGBOX: {
		struct string *str = stack_peek_string(0);
		char *utf = sjis2utf(str->text, str->size);
		SDL_ShowSimpleMessageBox(0, "xsystem4", utf, NULL);
		free(utf);
		// XXX: caller S_POPs
		break;
	}
	case SYS_MSGBOX_OK_CANCEL: {
		int result = 0;
		struct string *str = stack_peek_string(0);
		char *utf = sjis2utf(str->text, str->size);

		const SDL_MessageBoxData mbox = {
			SDL_MESSAGEBOX_INFORMATION,
			NULL,
			"xsystem4",
			utf,
			SDL_arraysize(ok_cancel_buttons),
			ok_cancel_buttons,
			NULL
		};
		if (SDL_ShowMessageBox(&mbox, &result)) {
			WARNING("Error displaying message box");
		}
		free(utf);
		heap_unref(stack_pop().i);
		stack_push(result);
		break;
	}
	case SYS_RESUME_SAVE: {
		union vm_value *success = stack_pop_var();
		struct string *filename = stack_peek_string(0);
		struct string *keyname = stack_peek_string(1);
		success->i = vm_save_image(keyname->text, filename->text);
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(1);
		break;
	}
	case SYS_RESUME_LOAD: {
		int filename_slot = stack_pop().i;
		int key_slot = stack_pop().i;
		vm_load_image(heap_get_string(key_slot)->text, heap_get_string(filename_slot)->text);
		stack_push(0);
		break;
	}
	case SYS_EXISTS_FILE: { // system.ExistsFile(string szFileName)
		int str = stack_pop().i;
		char *path = gamedir_path(heap_get_string(str)->text);
		int len = strlen(path);
		// Return true for directories, but false if the path ends with a directory separator.
		if (len > 0 && (path[len-1] == '/' || path[len-1] == '\\')) {
			stack_push(0);
		} else {
			stack_push(file_exists(path));
		}
		heap_unref(str);
		free(path);
		break;
	}
	case SYS_OPEN_WEB: {
#if !SDL_VERSION_ATLEAST(2, 0, 14)
		WARNING("SDL_OpenURL not available");
#else
		struct string *url = stack_peek_string(0);
		if (SDL_OpenURL(url->text) < 0) {
			WARNING("SDL_OpenURL failed: %s", SDL_GetError());
		}
#endif
		heap_unref(stack_pop().i);
		break;
	};
	case SYS_GET_SAVE_FOLDER_NAME: {// system.GetSaveFolderName(void)
		if (config.save_dir) {
			char *sjis = utf2sjis(config.save_dir, strlen(config.save_dir));
			stack_push_string(make_string(sjis, strlen(sjis)));
			free(sjis);
		} else {
			stack_push_string(string_ref(&EMPTY_STRING));
		}
		break;
	}
	case SYS_GET_TIME: {// system.GetTime(void)
		stack_push(vm_time());
		break;
	}
	case SYS_GET_GAME_NAME: {// system.GetGameName(void)
		stack_push_string(make_string(config.game_name, strlen(config.game_name)));
		break;
	}
	case SYS_ERROR: {// system.Error(string szText)
		int result = 0;
		struct string *str = stack_peek_string(0);
		char *utf = sjis2utf(str->text, str->size);
		sys_warning("*GAME ERROR*: %s\n", utf);
		const SDL_MessageBoxData mbox = {
			SDL_MESSAGEBOX_ERROR,
			NULL,
			"Game Error - xsystem4",
			utf,
			SDL_arraysize(stop_continue_buttons),
			stop_continue_buttons,
			NULL
		};
		if (SDL_ShowMessageBox(&mbox, &result)) {
			WARNING("Error displaying message box");
		}
		free(utf);
		if (result == 1) {
			// stop execution
			vm_exit(1);
		}
		// XXX: caller S_POPs
		break;
	}
	case SYS_EXISTS_SAVE_FILE: {
		int slot = stack_pop().i;
		char *path = savedir_path(heap_get_string(slot)->text);
		stack_push(file_exists(path));
		heap_unref(slot);
		free(path);
		break;
	}
	case SYS_IS_DEBUG_MODE: {// system.IsDebugMode(void)
		stack_push(0);
		break;
	}
	case SYS_GET_FUNC_STACK_NAME: { // system.GetFuncStackName(int nIndex)
		stack_push_string(get_func_stack_name(stack_pop().i));
		break;
	}
	case SYS_PEEK: {// system.Peek(void)
		handle_events();
		break;
	}
	case SYS_SLEEP: {// system.Sleep(int nSleep)
		int ms = stack_pop().i;
		vm_sleep(ms);
		break;
	}
	case SYS_RESUME_READ_COMMENT: {// system.ResumeReadComment(string szKeyName, string szFileName, ref array@string aszComment)
		int success;
		int comment = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		// FIXME: free ref'd array if allocated
		heap_set_page(comment, vm_load_image_comments(heap_get_string(keyname)->text,
							      heap_get_string(filename)->text,
							      &success));
		heap_unref(filename);
		heap_unref(keyname);
		stack_push(success);
		break;
	}
	case SYS_RESUME_WRITE_COMMENT: { // system.ResumeWriteComment(string szKeyName, string szFileName, ref array@string aszComment)
		int comment = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(vm_write_image_comments(heap_get_string(keyname)->text,
						   heap_get_string(filename)->text,
						   heap_get_page(comment)));
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_GROUP_SAVE: { // system.GroupSave(string szKeyName, string szFileName, string szGroupName, ref int nNumofLoad)
		union vm_value *n = stack_pop_var();
		int groupname = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(save_globals(heap_get_string(keyname)->text,
				      heap_get_string(filename)->text,
				      heap_get_string(groupname)->text,
				      &n->i));
		heap_unref(groupname);
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_GROUP_LOAD: { // system.GroupLoad(string szKeyName, string szFileName, string szGroupName, ref int nNumofLoad)
		union vm_value *n = stack_pop_var();
		int groupname = stack_pop().i;
		int filename = stack_pop().i;
		int keyname = stack_pop().i;
		stack_push(load_globals(heap_get_string(keyname)->text,
					heap_get_string(filename)->text,
					heap_get_string(groupname)->text,
					&n->i));
		heap_unref(groupname);
		heap_unref(filename);
		heap_unref(keyname);
		break;
	}
	case SYS_DELETE_SAVE_FILE: { // system.DeleteSaveFile(string szFileName)
		int filename = stack_pop().i;
		stack_push(delete_save_file(heap_get_string(filename)->text));
		heap_unref(filename);
		break;
	}
	case SYS_EXIST_FUNC: { // system.ExistFunc(string szFuncName)
		int funcname = stack_pop().i;
		stack_push(ain_get_function(ain, heap_get_string(funcname)->text) > 0);
		heap_unref(funcname);
		break;
	}
	case SYS_COPY_SAVE_FILE: { // system.CopySaveFile(string szDestFileName, string szSourceFileName)
		int src = stack_pop().i;
		int dst = stack_pop().i;
		char *u_src = savedir_path(heap_get_string(src)->text);
		char *u_dst = savedir_path(heap_get_string(dst)->text);
		stack_push(file_copy(u_src, u_dst));
		free(u_src);
		free(u_dst);
		heap_unref(src);
		heap_unref(dst);
		break;
	}
	default:
		// xsystem4-specific system calls (used for hacks)
		switch ((enum vm_extra_syscall)code) {
		case VM_XSYS_KEY_IS_DOWN:
			stack_push(key_is_down(stack_pop().i));
			break;
		default:
			VM_ERROR("Unimplemented syscall: 0x%X", code);
		}
	}
}

uint32_t get_switch_address(int no, int val)
{
	struct ain_switch *s = &ain->switches[no];
	for (int i = 0; i < s->nr_cases; i++) {
		if (s->cases[i].value == val) {
			return s->cases[i].address;
		}
	}
	if (s->default_address > 0)
		return s->default_address;
	else
		return instr_ptr + instruction_width(SWITCH);
}

uint32_t get_strswitch_address(int no, struct string *str)
{
	struct ain_switch *s = &ain->switches[no];
	for (int i = 0; i < s->nr_cases; i++) {
		if (!strcmp(str->text, ain->strings[s->cases[i].value]->text)) {
			return s->cases[i].address;
		}
	}
	if (s->default_address > 0)
		return s->default_address;
	else
		return instr_ptr + instruction_width(STRSWITCH);
}

static void echo_message(int i)
{
	NOTICE("MSG %d: %s", i, display_sjis0(ain->messages[i]->text));
}

static enum opcode execute_instruction(enum opcode opcode)
{
	/* XSYS4_TRACE=start:end (hex): runtime instruction trace with stack top */
	static uint32_t ts = 0, te = 0;
	static int trace_ready = 0;
	if (!trace_ready) {
		const char *t = getenv("XSYS4_TRACE");
		if (t) sscanf(t, "%x:%x", &ts, &te);
		trace_ready = 1;
	}
	if (ts && instr_ptr >= ts && instr_ptr < te) {
		int32_t a = stack_ptr > 0 ? stack[stack_ptr-1].i : 0;
		int32_t b = stack_ptr > 1 ? stack[stack_ptr-2].i : 0;
		int32_t c = stack_ptr > 2 ? stack[stack_ptr-3].i : 0;
		NOTICE("[trace] 0x%08zx %-20s sp=%d top=%d,%d,%d arg0=%d",
			instr_ptr, instructions[opcode].name ? instructions[opcode].name : "?",
			stack_ptr, a, b, c, get_argument(0));
	}
	/* XSYS4_DUMP_AT=hex: one-shot VM stack + heap slot dump at one ip.
	 * Diagnostic facility: env-gated, address-gated, no effect on execution. */
	static uint32_t dump_ip = 0;
	static int dump_ready = 0, dumped = 0;
	if (!dump_ready) {
		const char *d = getenv("XSYS4_DUMP_AT");
		if (d) sscanf(d, "%x", &dump_ip);
		dump_ready = 1;
	}
	if (dump_ip && !dumped && instr_ptr == dump_ip) {
		dumped = 1;
		NOTICE("[dump] ip=0x%08zx sp=%d heap_size=%d", instr_ptr, stack_ptr, heap_size);
		for (int i = stack_ptr - 1; i >= 0 && i >= stack_ptr - 10; i--) {
			int32_t v = stack[i].i;
			if (v > 0 && v < heap_size) {
				struct page *p = heap[v].page;
				NOTICE("[dump]  st[%d]=%d ref=%d nr_vars=%d type=%d v0=%d v1=%d", i, v,
					heap[v].ref, p ? p->nr_vars : -1, p ? p->type : -1,
					p && p->nr_vars > 0 ? p->values[0].i : -9999,
					p && p->nr_vars > 1 ? p->values[1].i : -9999);
			} else {
				NOTICE("[dump]  st[%d]=%d (nonheap)", i, v);
			}
		}
	}
	switch (opcode) {
	//
	// --- Stack Management ---
	//
	case PUSH: {
		stack_push(get_argument(0));
		break;
	}
	case POP: {
		stack_pop();
		break;
	}
	case F_PUSH: {
		stack_push(get_argument_float(0));
		break;
	}
	case REF: {
		// Dereference a reference to a value.
		int _hidx = stack_peek(1).i, _pidx = stack_peek(0).i;
		union vm_value *_v = stack_pop_var();
		if (getenv("XSYS4_MGRBT") && instr_ptr >= 0x53f0 && instr_ptr <= 0x5424) {
			struct page *_tp = (_hidx > 0 && (size_t)_hidx < heap_size) ? heap[_hidx].page : NULL;
			NOTICE("[THIS] checkhandle this_slot=%d type=%d index=%d nr=%d ref=%d memb1=%d ip=0x%zX",
			       _hidx, _tp ? _tp->type : -1, _tp ? _tp->index : -1,
			       _tp ? _tp->nr_vars : -1,
			       (_hidx > 0 && (size_t)_hidx < heap_size) ? heap[_hidx].ref : -1,
			       (_tp && _tp->nr_vars > 1) ? _tp->values[1].i : -999, instr_ptr);
			NOTICE("[THIS] global[14] declared data=%d struc=%d rank=%d value=%d",
			       ain->globals[14].type.data, ain->globals[14].type.struc,
			       ain->globals[14].type.rank,
			       heap[0].page ? heap[0].page->values[14].i : -999);
			NOTICE("[THIS] struct225: nr_members=%d nr_vmethods=%d name_ok=%d struct5_members=%d",
			       ain->structures[225].nr_members, ain->structures[225].nr_vmethods,
			       ain->structures[225].name != NULL, ain->structures[5].nr_members);
		}
		if (getenv("XSYS4_ARR_DBG")) {
			struct page *_mp = (_hidx > 0 && (size_t)_hidx < heap_size) ? heap[_hidx].page : NULL;
			if (_mp && _mp->type == STRUCT_PAGE && _mp->index == 5) {
				static int _mlim = 0;
				if (_mlim++ < 60) {
					int _v0 = _mp->nr_vars > 0 ? _mp->values[0].i : -1;
					int _v2 = _mp->nr_vars > 2 ? _mp->values[2].i : -1;
					int _v3 = _mp->nr_vars > 3 ? _mp->values[3].i : -1;
					NOTICE("[MGR] read memb=%d val=%d ref=%d slot=%d nr=%d v0=%d v2=%d v3=%d ip=0x%zX",
					       _pidx, _v->i,
					       (_v->i > 0 && (size_t)_v->i < heap_size) ? heap[_v->i].ref : -2,
					       _hidx, _mp->nr_vars, _v0, _v2, _v3, instr_ptr);
				}
			}
		}
		if (getenv("XSYS4_ARR_DBG") && _v->i > 0 && (size_t)_v->i < heap_size &&
		    heap[_v->i].ref <= 0) {
			struct page *_sp = (_hidx > 0 && (size_t)_hidx < heap_size) ? heap[_hidx].page : NULL;
			static int _lim = 0;
			if (_lim < 200 && _v->i != 255) {
				_lim++;
				char _buf[256]; int _o = 0;
				for (int _k = 0; _k < _sp->nr_vars && _k < 5; _k++) {
					int _m = _sp->values[_k].i;
					int _r = (_m > 0 && (size_t)_m < heap_size) ? heap[_m].ref : -1;
					_o += snprintf(_buf + _o, sizeof(_buf) - _o, "%s[%d]slot=%d(ref=%d)",
					               _k ? " " : "", _k, _m, _r);
				}
				NOTICE("[ARR] dead struct=%d type=%d no=%d nvars=%d member=%d val=%d page=%p %s",
				       _hidx, _sp ? _sp->type : -1, _sp ? _sp->index : -1,
				       _sp ? _sp->nr_vars : -1, _pidx, _v->i, (void*)_sp, _buf);
			}
		}
		stack_push(_v->i);
		break;
	}
	case REFREF: {
		// Dereference a reference to a reference.
		union vm_value *ref = stack_pop_var();
		stack_push(ref[0].i);
		stack_push(ref[1].i);
		break;
	}
	case DUP: {
		// A -> AA
		stack_push(stack_peek(0).i);
		break;
	}
	case DUP2: {
		// AB -> ABAB
		int a = stack_peek(1).i;
		int b = stack_peek(0).i;
		stack_push(a);
		stack_push(b);
		break;
	}
	case DUP_X2: {
		// ABC -> CABC
		int a = stack_peek(2).i;
		int b = stack_peek(1).i;
		int c = stack_peek(0).i;
		stack_set(2, c);
		stack_set(1, a);
		stack_set(0, b);
		stack_push(c);
		break;
	}
	case DUP2_X1: {
		// ABC -> BCABC
		int a = stack_peek(2).i;
		int b = stack_peek(1).i;
		int c = stack_peek(0).i;
		stack_set(2, b);
		stack_set(1, c);
		stack_set(0, a);
		stack_push(b);
		stack_push(c);
		break;
	}
	case DUP_U2: {
		// AB -> ABA
		stack_push(stack_peek(1).i);
		break;
	}
	case SWAP: {
		int a = stack_peek(1).i;
		stack_set(1, stack_peek(0));
		stack_set(0, a);
		break;
	}
	//
	// --- Variables ---
	//
	case PUSHGLOBALPAGE: {
		stack_push(0);
		break;
	}
	case PUSHLOCALPAGE: {
		stack_push(local_page_slot());
		break;
	}
	case PUSHSTRUCTPAGE: {
		stack_push(struct_page_slot());
		break;
	}
	case ASSIGN: {
		{
			int _dh = stack_peek(1).i, _dp = stack_peek(0).i;
			if (_dh > 0 && (size_t)_dh < heap_size && heap[_dh].page &&
			    heap[_dh].page->type == STRUCT_PAGE && heap[_dh].page->index == 5 &&
			    _dp == 1) {
				static int _wlim = 0;
				if (_wlim++ < 40) {
					int _old = heap[_dh].page->values[1].i;
					union vm_value _nv = stack_peek(2);
					NOTICE("[MGR] write old=%d(oldref=%d) new=%d(newref=%d) ip=0x%zX",
					       _old, (_old > 0 && (size_t)_old < heap_size) ? heap[_old].ref : -2,
					       _nv.i, (_nv.i > 0 && (size_t)_nv.i < heap_size) ? heap[_nv.i].ref : -2,
					       instr_ptr);
				}
			}
		}
		union vm_value val = stack_pop();
		stack_pop_var()[0] = val;
		stack_push(val);
		break;
	}
	case F_ASSIGN: {
		union vm_value val = stack_pop();
		stack_pop_var()[0] = val;
		stack_push(val);
		break;
	}

	case SH_GLOBALREF: { // VARNO
		stack_push(global_get(get_argument(0)).i);
		break;
	}
	case SH_LOCALREF: { // VARNO
		stack_push(local_get(get_argument(0)).i);
		break;
	}
	/* AIN v12 0x10x extension: interface / downcast assertion.
	 *
	 * Stack contract, established by tools/ain_icast.c over all 607 call
	 * sites (379 POP-discarded, 223 compared with -1, 5 before RETURN) and
	 * by controlled traces of CTextParts@Core::get @0x2637f0:
	 *   in:  [..., iface_ref]
	 *   out: [..., iface_ref, vtable_offset, impl_or_-1]     (net +2)
	 * The last value is the cast result (compared with -1 or POPped); the
	 * two values left behind are the AIN_IFACE value the caller returns or
	 * stores with R_ASSIGN. Returning a single value makes DUP_U2 read
	 * outside the stack and produces the well-known 0x8011 garbage (D32).
	 *
	 * vtable_offset is not a constant: the AIN declares 278 ain_interface
	 * entries, and IParts alone has offsets 0/12/38/41/47/50/54/67/70/72/
	 * 82/103 depending on the implementation (logs/ain_icast.txt). The
	 * interface reference on the stack IS the implementing object: its
	 * STRUCT_PAGE.index is the AIN struct number, whose interfaces[] row
	 * gives the vtable_offset for the requested target. A concrete-struct
	 * operand is a downcast; with no inheritance table in the AIN we keep
	 * the offset stored in the reference (D28). */
	/* Stage 1 (D44): X_SET (0x104) is unimplemented in libsys4.
	 * Observed form at MapPair@... @0xd918f2: [member_ref, copy] -> one
	 * value left for the following DELETE. Full semantics over all 300
	 * call sites still needs a static census. */
	case X_SET: {
		int v = stack_pop().i;
		stack_pop();
		stack_push((union vm_value){ .i = v });
		break;
	}
	case X_ICAST: {
		static int icast_trace = -1;
		static int icast_trace_count = 0;
		if (icast_trace < 0) {
			const char *t = getenv("XSYS4_ICAST_TRACE");
			icast_trace = t ? 1 : 0;
		}
		int iface_no = get_argument(0);
		int32_t ref_slot = stack_peek(0).i;
		int32_t impl = -1;
		int32_t vtable_offset = 0;
		int32_t sno = -1;
		int matched = 0;
		int target_is_iface = iface_no >= 0 && iface_no < ain->nr_structures
			&& ain->structures[iface_no].is_interface;

		if (ref_slot > 0 && ref_slot < heap_size && heap[ref_slot].page) {
			struct page *p = heap[ref_slot].page;
			if (p->type == STRUCT_PAGE && p->index >= 0
			    && p->index < ain->nr_structures) {
				/* The reference IS the implementing object. Its struct
				 * index selects the row in ain_interface; e.g.
				 * CSpriteParts(329) declares IParts(358)->0 and
				 * ISpriteParts(363)->309. */
				sno = p->index;
				struct ain_struct *st = &ain->structures[sno];
				for (int j = 0; j < st->nr_interfaces; j++) {
					if (st->interfaces[j].struct_type == iface_no) {
						vtable_offset = st->interfaces[j].vtable_offset;
						matched = 1;
						break;
					}
				}
				if (!matched && sno == iface_no) matched = 1;
				/* A concrete-class operand (is_interface=0) is a
				 * downcast: the AIN carries no inheritance table, so only
				 * object validity can be checked here. Keep the offset
				 * already stored in the reference (D28). */
				if (!matched && !target_is_iface && p->nr_vars >= 2) {
					vtable_offset = p->values[1].i;
					matched = 1;
				}
				if (matched) impl = ref_slot;
			} else if (p->type == IFACE_PAGE && p->nr_vars >= 2
			           && p->values[0].i > 0) {
				/* Populated 2-slot interface wrapper. */
				vtable_offset = p->values[1].i;
				impl = ref_slot;
				matched = 1;
			}
		}
		if (icast_trace && icast_trace_count++ < 200)
			NOTICE("[icast] target=%d ref=%d sno=%d matched=%d off=%d impl=%d",
			       iface_no, ref_slot, sno, matched, vtable_offset, impl);
		stack_push((union vm_value){ .i = vtable_offset });
		stack_push((union vm_value){ .i = impl });
		break;
	}
	case _EOF: // In Ain v0, opcode 0x62 is not EOF but SH_STRUCTREF
		if (ain->version != 0)
			VM_ERROR("Illegal opcode: 0x%04x", opcode);
		// fallthrough
	case SH_STRUCTREF: { // VARNO
		stack_push(member_get(get_argument(0)));
		break;
	}
	case SH_LOCALASSIGN: { // VARNO, VALUE
		local_set(get_argument(0), get_argument(1));
		break;
	}
	case SH_LOCALINC: { // VARNO
		int varno = get_argument(0);
		local_set(varno, local_get(varno).i+1);
		break;
	}
	case SH_LOCALDEC: { // VARNO
		int varno = get_argument(0);
		local_set(varno, local_get(varno).i-1);
		break;
	}
	case SH_LOCALDELETE: {
		int slot = local_get(get_argument(0)).i;
		if (slot != -1) {
			heap_unref(slot);
			local_set(get_argument(0), -1);
		}
		break;
	}
	case SH_LOCALCREATE: { // VARNO, STRUCTNO
		create_struct(get_argument(1), local_ptr(get_argument(0)));
		break;
	}
	case R_ASSIGN: {
		int src_var = stack_pop().i;
		int src_page = stack_pop().i;
		int dst_var = stack_pop().i;
		struct page *dst = heap_get_page(stack_pop().i);
		if (getenv("XSYS4_ARR_DBG") && dst && dst->type == STRUCT_PAGE &&
		    dst_var >= 0 && dst_var < dst->nr_vars) {
			static int _n = 0;
			if (_n++ < 60)
				NOTICE("[ARR] R_ASSIGN struct=%d no=%d dstvar=%d old=%d <- page=%d var=%d",
				       dst->index, dst->index, dst_var, dst->values[dst_var].i,
				       src_page, src_var);
		}
		page_set_var(dst, dst_var, src_page);
		page_set_var(dst, dst_var+1, src_var);
		stack_push(src_page);
		stack_push(src_var);
		break;
	}
	case R_EQUALE: {
		int rhs_var = stack_pop().i;
		int rhs_page = stack_pop().i;
		int lhs_var = stack_pop().i;
		int lhs_page = stack_pop().i;
		stack_push(lhs_page == rhs_page && lhs_var == rhs_var ? 1 : 0);
		break;
	}
	case R_NOTE: {
		int rhs_var = stack_pop().i;
		int rhs_page = stack_pop().i;
		int lhs_var = stack_pop().i;
		int lhs_page = stack_pop().i;
		stack_push(lhs_page == rhs_page && lhs_var == rhs_var ? 0 : 1);
		break;
	}
	case NEW: {
		/* AIN v12: struct no and constructor fno are immediate arguments;
		 * constructor arguments are on the stack below. */
		int struct_no = get_argument(0);
		int ctor_fno = get_argument(1);
		union vm_value v;
		if (struct_no < 0 || struct_no >= ain->nr_structures)
			VM_ERROR("NEW: invalid struct no %d", struct_no);
		create_struct_with_ctor(struct_no, ctor_fno, &v);
		stack_push(v);
		break;
	}
	case DELETE: {
		int slot = stack_pop().i;
		if (slot != -1)
			heap_unref(slot);
		break;
	}
	case SP_INC: {
		heap_ref(stack_pop().i);
		break;
	}
	case OBJSWAP: {
		stack_pop(); // type
		union vm_value *b = stack_pop_var();
		union vm_value *a = stack_pop_var();
		union vm_value tmp = *a;
		*a = *b;
		*b = tmp;
		break;
	}
	//
	// --- Control Flow ---
	//
	case CALLFUNC: {
		function_call(get_argument(0), instr_ptr + instruction_width(CALLFUNC));
		break;
	}
	case CALLFUNC2: {
		stack_pop(); // function-type index (only needed for compilation)
		function_call(stack_pop().i, instr_ptr + instruction_width(CALLFUNC2));
		break;
	}
	case CALLMETHOD: {
		/* libsys4 initialize_instructions(): version >= 11 marks the
		 * operand as T_INT (nargs); older AIN uses a function index. */
		int arg = get_argument(0);
		if (ain->version >= 11)
			method_call(arg, instr_ptr + instruction_width(CALLMETHOD));
		else
			method_call_fno(arg, instr_ptr + instruction_width(CALLMETHOD));
		break;
	}
	case CALLHLL: {
		/* AIN v12 (initialize_instructions): CALLHLL has 3 immediates.
		 * The third is the concrete ain_data_type of the generic result
		 * (or -1 when untyped). Pass it to the FFI layer so that
		 * AIN_REF_HLL_PARAM results can be materialised by type. */
		int tag = ain->version >= 11 ? get_argument(2) : -1;
		hll_call(get_argument(0), get_argument(1), tag);
		break;
	}
	case RETURN: {
		function_return();
		break;
	}
	case CALLSYS: {
		system_call(get_argument(0));
		break;
	}
	case CALLONJUMP: {
		int str = stack_pop().i;
		if (ain->scenario_labels) {
			stack_push(scenario_label_addr(heap_get_string(str)->text));
		} else {
			// XXX: I am GUESSING that the VM pre-allocates the scenario function's
			//      local page here. It certainly pushes what appears to be a page
			//      index to the stack.
			stack_push(alloc_scenario_page(heap_get_string(str)->text));
		}
		heap_unref(str);
		break;
	}
	case SJUMP: {
		if (ain->scenario_labels) {
			scenario_jump(stack_pop().i);
		} else {
			scenario_call(stack_pop().i);
		}
		break;
	}
	case _MSG: {
		if (config.echo)
			echo_message(get_argument(0));
		if (ain->msgf < 0)
			break;
		stack_push(get_argument(0));
		stack_push(ain->nr_messages);
		stack_push_string(string_ref(ain->messages[get_argument(0)]));
		function_call(ain->msgf, instr_ptr + instruction_width(_MSG));
		break;
	}
	case JUMP: { // ADDR
		instr_ptr = get_argument(0);
		break;
	}
	case IFZ: { // ADDR
		if (!stack_pop().i)
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFZ);
		break;
	}
	case IFNZ: { // ADDR
		if (stack_pop().i)
			instr_ptr = get_argument(0);
		else
			instr_ptr += instruction_width(IFNZ);
		break;
	}
	case SWITCH: {
		instr_ptr = get_switch_address(get_argument(0), stack_pop().i);
		break;
	}
	case STRSWITCH: {
		int str = stack_pop().i;
		instr_ptr = get_strswitch_address(get_argument(0), heap_get_string(str));
		heap_unref(str);
		break;
	}
	case ASSERT: {
		int line = stack_pop().i; // line number
		int file = stack_pop().i; // filename
		int expr = stack_pop().i; // expression
		if (!stack_pop().i) {
			sys_message("Assertion failed at %s:%d: %s\n",
					display_sjis0(heap_get_string(file)->text),
					line,
					display_sjis1(heap_get_string(expr)->text));
			vm_exit(1);
		}
		heap_unref(file);
		heap_unref(expr);
		break;
	}
	//
	// --- Arithmetic ---
	//
	case INV: {
		stack[stack_ptr-1].i = -stack[stack_ptr-1].i;
		break;
	}
	case NOT: {
		stack[stack_ptr-1].i = !stack[stack_ptr-1].i;
		break;
	}
	case COMPL: {
		stack[stack_ptr-1].i = ~stack[stack_ptr-1].i;
		break;
	}
	case ADD: {
		stack[stack_ptr-2].i += stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case SUB: {
		stack[stack_ptr-2].i -= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case MUL: {
		stack[stack_ptr-2].i *= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case DIV: {
		if (!stack[stack_ptr-1].i) {
			stack[stack_ptr-2].i = 0;
		} else {
			stack[stack_ptr-2].i /= stack[stack_ptr-1].i;
		}
		stack_ptr--;
		break;
	}
	case MOD: {
		if (!stack[stack_ptr-1].i) {
			stack[stack_ptr-2].i = 0;
		} else {
			stack[stack_ptr-2].i %= stack[stack_ptr-1].i;
		}
		stack_ptr--;
		break;
	}
	case AND: {
		stack[stack_ptr-2].i &= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case OR: {
		stack[stack_ptr-2].i |= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case XOR: {
		stack[stack_ptr-2].i ^= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case LSHIFT: {
		stack[stack_ptr-2].i <<= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	case RSHIFT: {
		stack[stack_ptr-2].i >>= stack[stack_ptr-1].i;
		stack_ptr--;
		break;
	}
	// Numeric Comparisons
	case LT: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a < b ? 1 : 0);
		break;
	}
	case GT: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a > b ? 1 : 0);
		break;
	}
	case LTE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a <= b ? 1 : 0);
		break;
	}
	case GTE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a >= b ? 1 : 0);
		break;
	}
	case NOTE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a != b ? 1 : 0);
		break;
	}
	case EQUALE: {
		int32_t b = stack_pop().i;
		int32_t a = stack_pop().i;
		stack_push(a == b ? 1 : 0);
		break;
	}
	// +=, -=, etc.
	case PLUSA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i += n);
		break;
	}
	case MINUSA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i -= n);
		break;
	}
	case MULA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i *= n);
		break;
	}
	case DIVA: {
		int32_t n = stack_pop().i;
		stack_push(n ? stack_pop_var()->i /= n : 0);
		break;
	}
	case MODA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i %= n);
		break;
	}
	case ANDA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i &= n);
		break;
	}
	case ORA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i |= n);
		break;
	}
	case XORA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i ^= n);
		break;
	}
	case LSHIFTA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i <<= n);
		break;
	}
	case RSHIFTA: {
		int32_t n = stack_pop().i;
		stack_push(stack_pop_var()->i >>= n);
		break;
	}
	case INC: {
		stack_pop_var()[0].i++;
		break;
	}
	case DEC: {
		stack_pop_var()[0].i--;
		break;
	}
	case ITOB: {
		stack_set(0, !!stack_peek(0).i);
		break;
	}
	//
	// --- 64-bit integers ---
	//
	case ITOLI: {
		stack_set(0, lint_clamp(stack_peek(0).i));
		break;
	}
	case LI_ADD: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a + b);
		stack_ptr--;
		break;
	}
	case LI_SUB: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a - b);
		stack_ptr--;
		break;
	}
	case LI_MUL: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a * b);
		stack_ptr--;
		break;
	}
	case LI_DIV: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = b ? lint_clamp(a / b) : 0;
		stack_ptr--;
		break;
	}
	case LI_MOD: {
		int64_t a = stack[stack_ptr-2].i;
		int64_t b = stack[stack_ptr-1].i;
		stack[stack_ptr-2].i = lint_clamp(a % b);
		stack_ptr--;
		break;
	}
	case LI_ASSIGN: {
		int64_t v = stack_pop().i;
		stack_push(stack_pop_var()->i = lint_clamp(v));
		break;
	}
	case LI_PLUSA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i + n));
		break;
	}
	case LI_MINUSA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i - n));
		break;
	}
	case LI_MULA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i * n));
		break;
	}
	case LI_DIVA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = (n ? lint_clamp((int64_t)v->i / n) : 0));
		break;
	}
	case LI_MODA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i % n));
		break;
	}
	case LI_ANDA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i & n));
		break;
	}
	case LI_ORA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i | n));
		break;
	}
	case LI_XORA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i ^ n));
		break;
	}
	case LI_LSHIFTA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i << n));
		break;
	}
	case LI_RSHIFTA: {
		int64_t n = stack_pop().i;
		union vm_value *v = stack_pop_var();
		stack_push(v->i = lint_clamp((int64_t)v->i >> n));
		break;
	}
	case LI_INC: {
		union vm_value *v = stack_pop_var();
		v->i = lint_clamp((int64_t)v->i + (int64_t)1);
		break;
	}
	case LI_DEC: {
		union vm_value *v = stack_pop_var();
		v->i = lint_clamp((int64_t)v->i - (int64_t)1);
		break;
	}
	//
	// --- Floating Point Arithmetic ---
	//
	case FTOI: {
		stack_set(0, (int32_t)stack_peek(0).f);
		break;
	}
	case ITOF: {
		stack_set(0, (float)stack_peek(0).i);
		break;
	}
	case F_INV: {
		stack_set(0, -stack_peek(0).f);
		break;
	}
	case F_ADD: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f + f);
		break;
	}
	case F_SUB: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f - f);
		break;
	}
	case F_MUL: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f * f);
		break;
	}
	case F_DIV: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f / f);
		break;
	}
	// floating point comparison
	case F_LT: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f < f ? 1 : 0);
		break;
	}
	case F_GT: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f > f ? 1 : 0);
		break;
	}
	case F_LTE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f <= f ? 1 : 0);
		break;
	}
	case F_GTE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f >= f ? 1 : 0);
		break;
	}
	case F_NOTE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f != f ? 1 : 0);
		break;
	}
	case F_EQUALE: {
		float f = stack_pop().f;
		stack_set(0, stack_peek(0).f == f ? 1 : 0);
		break;
	}
	case F_PLUSA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f += n);
		break;
	}
	case F_MINUSA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f -= n);
		break;
	}
	case F_MULA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f *= n);
		break;
	}
	case F_DIVA: {
		float n = stack_pop().f;
		stack_push(stack_pop_var()->f /= n);
		break;
	}
	//
	// --- Strings ---
	//
	case S_PUSH: {
		if (ain->version == 0)
			stack_push_string(string_ref(ain->messages[get_argument(0)]));
		else
			stack_push_string(string_ref(ain->strings[get_argument(0)]));
		break;
	}
	case S_POP: {
		heap_unref(stack_pop().i);
		break;
	}
	case S_REF: {
		// Dereference a reference to a string
		int str = stack_pop_var()->i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	//case S_REFREF: // ???: why/how is this different from regular REFREF?
	case S_ASSIGN: { // A = B
		int rval = stack_peek(0).i;
		int lval = stack_peek(1).i;
		heap_string_assign(lval, heap_get_string(rval));
		// remove A from the stack, but leave B
		stack_set(1, rval);
		stack_pop();
		break;
	}
	case S_PLUSA:
	case S_PLUSA2: {
		int a = stack_peek(1).i;
		int b = stack_peek(0).i;
		string_append(&heap[a].s, heap[b].s);
		heap_unref(b);
		stack_pop();
		stack_pop();
		stack_push_string(string_ref(heap[a].s));
		break;
	}
	case S_ADD: {
		int b = stack_pop().i;
		int a = stack_pop().i;
		// TODO: can use string_append here?
		stack_push_string(string_concatenate(heap_get_string(a), heap_get_string(b)));
		heap_unref(a);
		heap_unref(b);
		break;
	}
	case S_LT: {
		bool lt = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) < 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(lt);
		break;
	}
	case S_GT: {
		bool gt = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) > 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(gt);
		break;
	}
	case S_LTE: {
		bool lte = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) <= 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(lte);
		break;
	}
	case S_GTE: {
		bool gte = strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text) >= 0;
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(gte);
		break;
	}
	case S_NOTE: {
		bool noteq = !!strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text);
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(noteq);
		break;
	}
	case S_EQUALE: {
		bool eq = !strcmp(stack_peek_string(1)->text, stack_peek_string(0)->text);
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(eq);
		break;
	}
	case S_LENGTH: {
		int str = stack_pop_var()->i;
		stack_push(sjis_count_char(heap_get_string(str)->text));
		break;
	}
	case S_LENGTH2: {
		int str = stack_pop().i;
		stack_push(sjis_count_char(heap_get_string(str)->text));
		heap_unref(str);
		break;
	}
	case S_LENGTHBYTE: {
		int str = stack_pop_var()->i;
		stack_push(heap_get_string(str)->size);
		break;
	}
	case S_EMPTY: {
		bool empty = !stack_peek_string(0)->size;
		heap_unref(stack_pop().i);
		stack_push(empty);
		break;
	}
	case S_FIND: {
		int i = string_find(stack_peek_string(1), stack_peek_string(0));
		heap_unref(stack_pop().i);
		heap_unref(stack_pop().i);
		stack_push(i);
		break;
	}
	case S_GETPART: {
		int len = stack_pop().i; // length
		int i = stack_pop().i; // index
		struct string *s = string_copy(stack_peek_string(0), i, len);
		heap_unref(stack_pop().i);
		stack_push_string(s);
		break;
	}
	//case S_PUSHBACK: // ???
	case S_PUSHBACK2: {
		int c = stack_pop().i;
		int str = stack_pop().i;
		string_push_back(&heap[str].s, c);
		break;
	}
	//case S_POPBACK: // ???
	case S_POPBACK2: {
		int str = stack_pop().i;
		string_pop_back(&heap[str].s);
		break;
	}
	//case S_ERASE: // ???
	case S_ERASE2: {
		stack_pop(); // ???
		int i = stack_pop().i; // index
		int str = stack_pop().i;
		string_erase(&heap[str].s, i);
		break;
	}
	case S_MOD: {
		/* libsys4::initialize_instructions gives S_MOD one T_INT immediate
		 * from AIN v11 onwards; the format type no longer lives on the stack.
		 * Popping it here consumed one value too many per call and drove
		 * stack_ptr negative in CardConstructProcessCacheFront (D42). */
		int type = ain->version >= 11 ? get_argument(0) : stack_pop().i;
		union vm_value val = stack_pop();
		int fmt = stack_pop().i;
		int dst = heap_alloc_slot(VM_STRING);
		heap[dst].s = string_format(heap[fmt].s, val, type);
		heap_unref(fmt);
		stack_push(dst);
		break;
	}
	case I_STRING: {
		stack_push_string(integer_to_string(stack_pop().i));
		break;
	}
	case FTOS: {
		int precision = stack_pop().i;
		stack_push_string(float_to_string(stack_pop().f, precision));
		break;
	}
	case STOI: {
		int str = stack_pop().i;
		stack_push(string_to_integer(heap[str].s));
		heap_unref(str);
		break;
	}
	case FT_ASSIGNS: {
		//int functype = stack_pop().i;
		stack_pop();
		int str = stack_pop().i;
		int fno = get_function_by_name(heap_get_string(str)->text);
		stack_pop_var()->i = fno > 0 ? fno : 0;
		stack_push(str);
		break;
	}
	// --- Characters ---
	case C_REF: {
		int i = stack_pop().i;
		int str = stack_pop().i;
		int32_t ch = string_get_char(heap_get_string(str), i);
		// 'é' for EN Rance Quest
		if (game_rance8_mg && ch == -91)
			stack_push(165);
		else
			stack_push(ch);
		break;
	}
	case C_ASSIGN: {
		int c = stack_pop().i;
		int i = stack_pop().i;
		int str = stack_pop().i;
		string_set_char(&heap[str].s, i, c);
		stack_push(c);
		break;
	}
	//
	// --- Structs/Classes ---
	//
	case SR_REF: {
		stack_push(vm_copy_page(heap[stack_pop_var()->i].page));
		break;
	}
	case SR_REF2: {
		stack_push(vm_copy_page(heap[stack_pop().i].page));
		break;
	}
	case SR_POP: {
		heap_unref(stack_pop().i);
		break;
	}
case SR_ASSIGN: {
		/* AIN v12 no longer pushes a separate struct-type operand for
		 * SR_ASSIGN (same simplification as CALLMETHOD/NEW). Popping it
		 * underflowed the stack and produced garbage page indices like
		 * 0x8011. Older AIN versions still carry it. */
		if (ain->version > 1 && ain->version < 12)
			stack_pop(); // struct type
		/* v12 form: [array, index, value]. Decide from the operands
		 * themselves: the two-operand form has a struct destination on
		 * the bottom of the triple, never a live ARRAY_PAGE. */
		int _a0 = stack_peek(2).i, _a1 = stack_peek(1).i, _a2 = stack_peek(0).i;
		if (getenv("XSYS4_SRDBG")) {
			static int _n = 0;
			if (_n++ < 200) {
				struct page *_p0 = (_a0 > 0 && (size_t)_a0 < heap_size) ? heap[_a0].page : NULL;
				NOTICE("[SR] ip=0x%zX a0=%d(%s/%d) a1=%d a2=%d sp=%d",
				       instr_ptr, _a0, _p0 ? (_p0->type == ARRAY_PAGE ? "ARR" :
				       _p0->type == STRUCT_PAGE ? "STRUCT" :
				       _p0->type == LOCAL_PAGE ? "LOCAL" : "PG") : "nil",
				       _p0 ? _p0->nr_vars : -1, _a1, _a2, stack_ptr);
				if (instr_ptr == 0x187F82) {
					int g67 = heap[0].page ? heap[0].page->values[67].i : -1;
					struct page *ap = (g67 > 0 && (size_t)g67 < heap_size) ? heap[g67].page : NULL;
					NOTICE("[SR]   g67=%d pt=%d nr=%d v0..5=%d,%d,%d,%d,%d,%d a1page=%p",
					       g67, ap ? ap->type : -1, ap ? ap->nr_vars : -1,
					       ap && ap->nr_vars > 0 ? ap->values[0].i : -9,
					       ap && ap->nr_vars > 1 ? ap->values[1].i : -9,
					       ap && ap->nr_vars > 2 ? ap->values[2].i : -9,
					       ap && ap->nr_vars > 3 ? ap->values[3].i : -9,
					       ap && ap->nr_vars > 4 ? ap->values[4].i : -9,
					       ap && ap->nr_vars > 5 ? ap->values[5].i : -9,
					       (_a1 > 0 && (size_t)_a1 < heap_size) ? (void*)heap[_a1].page : NULL);
				}
			}
		}
		bool _elem = _a0 > 0 && (size_t)_a0 < heap_size && heap[_a0].page &&
			heap[_a0].page->type == ARRAY_PAGE && _a1 >= 0 &&
			_a1 < heap[_a0].page->nr_vars;
		if (_elem) {
			int rval = stack_pop().i;
			int idx = stack_pop().i;
			int arr = stack_pop().i;
			sr_assign_element(arr, idx, rval);
			stack_push(rval);
			break;
		}
		int rval = stack_pop().i;
		int lval = stack_pop().i;
		heap_struct_assign(lval, rval);
		stack_push(rval);
		break;
	}
	//
	// -- Arrays --
	//
	case A_ALLOC: {
		int struct_type;
		int rank = stack_pop().i;
		int varno = stack_peek(rank).i;
		int pageno = stack_peek(rank+1).i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		if (heap[array].page) {
			delete_page_vars(heap[array].page);
			free_page(heap[array].page);
		}
		heap_set_page(array, alloc_array(rank, stack_peek_ptr(rank-1), data_type, struct_type, true));
		stack_ptr -= rank + 2;
		break;
	}
	case A_REALLOC: {
		int struct_type;
		int rank = stack_pop().i; // rank
		int varno = stack_peek(rank).i;
		int pageno = stack_peek(rank+1).i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		heap_set_page(array, realloc_array(heap[array].page, rank, stack_peek_ptr(rank-1), data_type, struct_type, true));
		stack_ptr -= rank + 2;
		break;
	}
	case A_FREE: {
		int array = stack_pop_var()->i;
		if (heap[array].page) {
			delete_page_vars(heap[array].page);
			free_page(heap[array].page);
			heap_set_page(array, NULL);
		}
		break;
	}
	/* libsys4 declares A_REF as (T_PAGE)->(). T_PAGE means any heap-backed
	 * object, and vm_pointer stores page* / string* in the same union. The
	 * upstream code always called copy_page(), so a string argument was
	 * read as a page header: copy_page() then looped over a garbage
	 * nr_vars and segfaulted (D34, AFL_Parts_CreateText @0x1d106e).
	 * Dispatch on the slot type; vm_copy_page / vm_string_ref are symmetric
	 * helpers that allocate a fresh slot and take a reference for the callee. */
	case A_REF: {
		int array = stack_pop().i;
		int slot = -1;
		if (array > 0 && array < heap_size) {
			if (heap[array].type == VM_PAGE && heap[array].page)
				slot = vm_copy_page(heap[array].page);
			else if (heap[array].type == VM_STRING && heap[array].s)
				slot = vm_string_ref(heap[array].s);
		}
		stack_push((union vm_value){ .i = slot >= 0 ? slot : array });
		break;
	}
	case A_NUMOF: {
		int rank = stack_pop().i; // rank
		int array = stack_pop_var()->i;
		stack_push(array_numof(heap[array].page, rank));
		break;
	}
	case A_COPY: {
		int n = stack_pop().i;
		int src_i = stack_pop().i;
		int src = stack_pop().i;
		int dst_i = stack_pop().i;
		int dst = stack_pop_var()->i;
		array_copy(heap[dst].page, dst_i, heap[src].page, src_i, n);
		stack_push(n);
		break;
	}
	case A_FILL: {
		union vm_value val = stack_pop();
		int n = stack_pop().i;
		int i = stack_pop().i;
		int array = stack_pop_var()->i;
		stack_push(array_fill(heap[array].page, i, n, val));
		break;
	}
	case A_PUSHBACK: {
		int struct_type;
		union vm_value val = stack_pop();
		int varno = stack_pop().i;
		int pageno = stack_pop().i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		heap_set_page(array, array_pushback(heap[array].page, val, data_type, struct_type));
		break;
	}
	case A_POPBACK: {
		int array = stack_pop_var()->i;
		heap_set_page(array, array_popback(heap[array].page));
		break;
	}
	case A_EMPTY: {
		struct page *array = heap_get_page(stack_pop_var()->i);
		stack_push(!array || !array->nr_vars);
		break;
	}
	case A_ERASE: {
		int i = stack_pop().i;
		int array = stack_pop_var()->i;
		bool success = false;
		heap_set_page(array, array_erase(heap[array].page, i, &success));
		stack_push(success);
		break;
	}
	case A_INSERT: {
		int struct_type;
		union vm_value val = stack_pop();
		int i = stack_pop().i;
		int varno = stack_pop().i;
		int pageno = stack_pop().i;
		int array = heap[pageno].page->values[varno].i;
		enum ain_data_type data_type = variable_type(heap[pageno].page, varno, &struct_type, NULL);
		heap_set_page(array, array_insert(heap[array].page, i, val, data_type, struct_type));
		break;
	}
	case A_SORT: {
		int fno = stack_pop().i;
		int array = stack_pop_var()->i;
		array_sort(heap[array].page, fno);
		break;
	}
	case A_FIND: {
		int fno = stack_pop().i;
		union vm_value v = stack_pop();
		int end = stack_pop().i;
		int start = stack_pop().i;
		struct page *array = heap_get_page(stack_pop_var()->i);
		stack_push(array_find(array, start, end, v, fno));
		// FIXME: string key isn't freed if array is empty
		if (array && array_type(array->a_type) == AIN_STRING) {
			heap_unref(v.i);
		}
		break;
	}
	case A_REVERSE: {
		int array = stack_pop_var()->i;
		array_reverse(heap[array].page);
		break;
	}
	//
	// -- Shorthand Instructions (added in Alice 2010) ---
	//
	case SH_SR_ASSIGN: {
		int rval = stack_pop_var()->i;
		int lval = stack_pop().i;
		heap_struct_assign(lval, rval);
		break;
	}
	case SH_MEM_ASSIGN_LOCAL: {
		member_set(get_argument(0), local_get(get_argument(1)).i);
		break;
	}
	case A_NUMOF_GLOB_1: {
		int array = global_get(get_argument(0)).i;
		stack_push(array_numof(heap_get_page(array), 1));
		break;
	}
	case A_NUMOF_STRUCT_1: {
		int array = member_get(get_argument(0)).i;
		stack_push(array_numof(heap_get_page(array), 1));
		break;
	}
	case SH_MEM_ASSIGN_IMM: {
		member_set(get_argument(0), get_argument(1));
		break;
	}
	case SH_LOCALREFREF: {
		stack_push(local_get(get_argument(0)));
		stack_push(local_get(get_argument(0)+1));
		break;
	}
	case SH_LOCALASSIGN_SUB_IMM: {
		int n = get_argument(0);
		local_set(n, local_get(n).i - get_argument(1));
		break;
	}
	case SH_IF_LOC_LT_IMM: {
		if (local_get(get_argument(0)).i < get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_LT_IMM);
		break;
	}
	case SH_IF_LOC_GE_IMM: {
		if (local_get(get_argument(0)).i >= get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_GE_IMM);
		break;
	}
	case SH_LOCREF_ASSIGN_MEM: {
		struct page *page = heap_get_page(local_get(get_argument(0)).i);
		int index = local_get(get_argument(0)+1).i;
		page_set_var(page, index, member_get(get_argument(1)));
		break;
	}
	case PAGE_REF: {
		struct page *page = heap_get_page(stack_pop().i);
		stack_push(page_get_var(page, get_argument(0)));
		break;
	}
	case SH_GLOBAL_ASSIGN_LOCAL: {
		global_set(get_argument(0), local_get(get_argument(1)), true);
		break;
	}
	case SH_STRUCTREF_GT_IMM: {
		stack_push(member_get(get_argument(0)).i > get_argument(1) ? 1 : 0);
		break;
	}
	case SH_STRUCT_ASSIGN_LOCALREF_ITOB: {
		member_set(get_argument(0), !!local_get(get_argument(1)).i);
		break;
	}
	case SH_LOCAL_ASSIGN_STRUCTREF: {
		local_set(get_argument(0), member_get(get_argument(1)).i);
		break;
	}
	case SH_IF_STRUCTREF_NE_LOCALREF: {
		if (member_get(get_argument(0)).i != local_get(get_argument(1)).i)
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_NE_LOCALREF);
		break;
	}
	case SH_IF_STRUCTREF_GT_IMM: {
		if (member_get(get_argument(0)).i > get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_GT_IMM);
		break;
	}
	case SH_STRUCTREF_CALLMETHOD_NO_PARAM: {
		int memb_page = member_get(get_argument(0)).i;
		function_call(get_argument(1), instr_ptr + instruction_width(SH_STRUCTREF_CALLMETHOD_NO_PARAM));
		set_struct_page(memb_page);
		break;
	}
	case SH_STRUCTREF2: {
		int memb = member_get(get_argument(0)).i;
		stack_push(page_get_var(heap_get_page(memb), get_argument(1)));
		break;
	}
	case SH_REF_STRUCTREF2: {
		int page = stack_pop().i;
		int memb = page_get_var(heap_get_page(page), get_argument(0)).i;
		stack_push(page_get_var(heap_get_page(memb), get_argument(1)));
		break;
	}
	case SH_STRUCTREF3: {
		int memb0 = member_get(get_argument(0)).i;
		int memb1 = page_get_var(heap_get_page(memb0), get_argument(1)).i;
		stack_push(page_get_var(heap_get_page(memb1), get_argument(2)));
		break;
	}
	case SH_STRUCTREF2_CALLMETHOD_NO_PARAM: {
		int memb1 = member_get(get_argument(0)).i;
		int memb2 = page_get_var(heap_get_page(memb1), get_argument(1)).i;
		function_call(get_argument(2), instr_ptr + instruction_width(SH_STRUCTREF2_CALLMETHOD_NO_PARAM));
		set_struct_page(memb2);
		break;
	}
	case SH_IF_STRUCTREF_Z: {
		if (!member_get(get_argument(0)).i)
			instr_ptr = get_argument(1);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_Z);
		break;
	}
	case SH_IF_STRUCT_A_NOT_EMPTY: {
		struct page *array = heap_get_page(member_get(get_argument(0)).i);
		if (array && array->nr_vars)
			instr_ptr = get_argument(1);
		else
			instr_ptr += instruction_width(SH_IF_STRUCT_A_NOT_EMPTY);
		break;
	}
	case SH_IF_LOC_GT_IMM: {
		if (local_get(get_argument(0)).i > get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_GT_IMM);
		break;
	}
	case SH_IF_STRUCTREF_NE_IMM: {
		if (member_get(get_argument(0)).i != get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_NE_IMM);
		break;
	}
	case THISCALLMETHOD_NOPARAM: {
		int this_page = struct_page_slot();
		function_call(get_argument(0), instr_ptr + instruction_width(THISCALLMETHOD_NOPARAM));
		set_struct_page(this_page);
		break;
	}
	case SH_IF_LOC_NE_IMM: {
		if (local_get(get_argument(0)).i != get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_LOC_NE_IMM);
		break;
	}
	case SH_IF_STRUCTREF_EQ_IMM: {
		if (member_get(get_argument(0)).i == get_argument(1))
			instr_ptr = get_argument(2);
		else
			instr_ptr += instruction_width(SH_IF_STRUCTREF_EQ_IMM);
		break;
	}
	case SH_GLOBAL_ASSIGN_IMM: {
		global_set(get_argument(0), (union vm_value) { .i = get_argument(1) }, false);
		break;
	}
	case SH_LOCALSTRUCT_ASSIGN_IMM: {
		struct page *page = heap_get_page(local_get(get_argument(0)).i);
		page_set_var(page, get_argument(1), get_argument(2));
		break;
	}
	case SH_STRUCT_A_PUSHBACK_LOCAL_STRUCT: {
		int struct_type;
		int array = member_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRUCT);
		enum ain_data_type data_type = variable_type(struct_page(), get_argument(0), &struct_type, NULL);
		heap_set_page(array, array_pushback(heap_get_page(array), val, data_type, struct_type));
		break;
	}
	case SH_GLOBAL_A_PUSHBACK_LOCAL_STRUCT: {
		int struct_type;
		int array = global_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRUCT);
		enum ain_data_type data_type = variable_type(global_page(), get_argument(0), &struct_type, NULL);
		heap_set_page(array, array_pushback(heap_get_page(array), val, data_type, struct_type));
		break;
	}
	case SH_LOCAL_A_PUSHBACK_LOCAL_STRUCT: {
		int struct_type;
		int array = local_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRUCT);
		enum ain_data_type data_type = variable_type(local_page(), get_argument(0), &struct_type, NULL);
		heap_set_page(array, array_pushback(heap_get_page(array), val, data_type, struct_type));
		break;
	}
	case SH_IF_SREF_NE_STR0: {
		struct string *a = heap_get_string(stack_pop_var()->i);
		struct string *b = ain->strings[get_argument(0)];
		if (strcmp(a->text, b->text))
			instr_ptr = get_argument(1);
		else
			instr_ptr += instruction_width(SH_IF_SREF_NE_STR0);
		break;
	}
	case SH_S_ASSIGN_REF: {
		int rval = stack_pop_var()->i;
		int lval = stack_pop().i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_A_FIND_SREF: {
		union vm_value *v = stack_pop_var();
		int end = stack_pop().i;
		int start = stack_pop().i;
		int array = stack_pop_var()->i;
		stack_push(array_find(heap_get_page(array), start, end, *v, 0));
		break;
	}
	case SH_SREF_EMPTY: {
		stack_push(!heap_get_string(stack_pop_var()->i)->size);
		break;
	}
	case SH_STRUCTSREF_EQ_LOCALSREF: {
		struct string *a = heap_get_string(member_get(get_argument(0)).i);
		struct string *b = heap_get_string(local_get(get_argument(1)).i);
		stack_push(!strcmp(a->text, b->text));
		break;
	}
	case SH_LOCALSREF_EQ_STR0: {
		struct string *a = heap_get_string(local_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!strcmp(a->text, b->text));
		break;
	}
	case SH_STRUCTSREF_NE_LOCALSREF: {
		struct string *a = heap_get_string(member_get(get_argument(0)).i);
		struct string *b = heap_get_string(local_get(get_argument(1)).i);
		stack_push(!!strcmp(a->text, b->text));
		break;
	}
	case SH_LOCALSREF_NE_STR0: {
		struct string *a = heap_get_string(local_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!!strcmp(a->text, b->text));
		break;
	}
	case SH_STRUCT_SR_REF: {
		int sr = member_get(get_argument(0)).i;
		stack_push(vm_copy_page(heap_get_page(sr)));
		// NOTE: argument 1 (struct type) not used
		break;
	}
	case SH_STRUCT_S_REF: {
		int str = member_get(get_argument(0)).i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	case S_REF2: {
		struct page *page = heap_get_page(stack_pop().i);
		struct string *s = heap_get_string(page_get_var(page, get_argument(0)).i);
		stack_push_string(string_ref(s));
		break;
	}
	case SH_REF_LOCAL_ASSIGN_STRUCTREF2: {
		struct page *memb = heap_get_page(member_get(get_argument(0)).i);
		int page = local_get(get_argument(1)).i;
		int var = local_get(get_argument(1) + 1).i;
		page_set_var(heap_get_page(page), var, page_get_var(memb, get_argument(2)));
		break;
	}
	case SH_GLOBAL_S_REF: {
		int str = global_get(get_argument(0)).i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	case SH_LOCAL_S_REF: {
		int str = local_get(get_argument(0)).i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	case SH_LOCALREF_SASSIGN_LOCALSREF: {
		int lval = local_get(get_argument(0)).i;
		int rval = local_get(get_argument(1)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_LOCAL_APUSHBACK_LOCALSREF: {
		int array = local_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRING);
		heap_set_page(array, array_pushback(heap_get_page(array), val, AIN_ARRAY_STRING, -1));
		break;
	}
	case SH_S_ASSIGN_CALLSYS19: {
		struct string *name = get_func_stack_name(stack_pop().i);
		heap_string_assign(stack_pop().i, name);
		free_string(name);
		break;
	}
	case SH_S_ASSIGN_STR0: {
		int lval = stack_pop().i;
		heap_string_assign(lval, ain->strings[get_argument(0)]);
		break;
	}
	case SH_SASSIGN_LOCALSREF: {
		int lval = stack_pop().i;
		struct string *rval = heap_get_string(local_get(get_argument(0)).i);
		heap_string_assign(lval, rval);
		break;
	}
	case SH_STRUCTREF_SASSIGN_LOCALSREF: {
		int lval = member_get(get_argument(0)).i;
		int rval = local_get(get_argument(1)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_LOCALSREF_EMPTY: {
		stack_push(!heap_get_string(local_get(get_argument(0)).i)->size);
		break;
	}
	case SH_GLOBAL_APUSHBACK_LOCALSREF: {
		int array = global_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRING);
		heap_set_page(array, array_pushback(heap_get_page(array), val, AIN_ARRAY_STRING, -1));
		break;
	}
	case SH_STRUCT_APUSHBACK_LOCALSREF: {
		int array = member_get(get_argument(0)).i;
		union vm_value val = vm_copy(local_get(get_argument(1)), AIN_STRING);
		heap_set_page(array, array_pushback(heap_get_page(array), val, AIN_ARRAY_STRING, -1));
		break;
	}
	case SH_STRUCTSREF_EMPTY: {
		stack_push(!heap_get_string(member_get(get_argument(0)).i)->size);
		break;
	}
	case SH_GLOBALSREF_EMPTY: {
		stack_push(!heap_get_string(global_get(get_argument(0)).i)->size);
		break;
	}
	case SH_SASSIGN_STRUCTSREF: {
		int lval = stack_pop().i;
		int rval = member_get(get_argument(0)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_SASSIGN_GLOBALSREF: {
		int lval = stack_pop().i;
		int rval = global_get(get_argument(0)).i;
		heap_string_assign(lval, heap_get_string(rval));
		break;
	}
	case SH_STRUCTSREF_NE_STR0: {
		struct string *a = heap_get_string(member_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!!strcmp(a->text, b->text));
		break;
	}
	case SH_GLOBALSREF_NE_STR0: {
		struct string *a = heap_get_string(global_get(get_argument(0)).i);
		struct string *b = ain->strings[get_argument(1)];
		stack_push(!!strcmp(a->text, b->text));
		break;
	}
	case SH_LOC_LT_IMM_OR_LOC_GE_IMM: {
		int i = local_get(get_argument(0)).i;
		stack_push(i < get_argument(1) || i >= get_argument(2));
		break;
	}
	case A_SORT_MEM: {
		int mno = stack_pop().i;
		int array = stack_pop_var()->i;
		array_sort_mem(heap[array].page, mno);
		break;
	}
	case DG_SET: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		delete_page(dg_i);
		heap_set_page(dg_i, delegate_new_from_method(obj, fun));
		break;
	}
	case DG_ADD: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		struct page *dg = heap_get_delegate_page(dg_i);
		heap_set_page(dg_i, delegate_append(dg, obj, fun));
		break;
	}
	case DG_CALL: { // DG_TYPE, ADDR
		delegate_call(get_argument(0), get_argument(1));
		break;
	}
	case DG_NUMOF: {
		int dg = stack_pop().i;
		stack_push(delegate_numof(heap_get_delegate_page(dg)));
		break;
	}
	case DG_EXIST: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		stack_push(delegate_contains(heap_get_delegate_page(dg_i), obj, fun));
		break;
	}
	case DG_ERASE: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		int dg_i = stack_pop().i;
		delegate_erase(heap_get_delegate_page(dg_i), obj, fun);
		break;
	}
	case DG_CLEAR: {
		int slot = stack_pop().i;
		if (!slot)
			break;
		heap_set_page(slot, delegate_clear(heap_get_delegate_page(slot)));
		break;
	}
	case DG_COPY: {
		stack_push(vm_copy_page(heap_get_delegate_page(stack_pop().i)));
		break;
	}
	case DG_ASSIGN: {
		int set_i = stack_pop().i;
		int dst_i = stack_pop().i;
		struct page *set = heap_get_delegate_page(set_i);
		struct page *new_dg = copy_page(set);
		/* dst_i == -1 is an empty delegate target; delete_page(-1) would
		 * call heap_get_page on an invalid slot (D57). heap_set_page()
		 * already ignores negative slots. */
		if (dst_i > 0)
			delete_page(dst_i);
		/* D75: slot 0 is the reserved GLOBAL_PAGE. DG_ASSIGN with dst_i<=0
		 * used to overwrite it with a delegate page, so every later
		 * PUSHGLOBALPAGE; PUSH n; REF read out of bounds. */
		if (dst_i > 0)
			heap_set_page(dst_i, new_dg);
		stack_push(set_i);
		break;
	}
	case DG_PLUSA: {
		int add_i = stack_pop().i;
		int dst_i = stack_pop().i;
		struct page *add = heap_get_delegate_page(add_i);
		struct page *dst = heap_get_delegate_page(dst_i);
		/* D76: dst_i==0 is the reserved GLOBAL_PAGE, not a delegate. */
		if (dst_i > 0)
			heap_set_page(dst_i, delegate_plusa(dst, add));
		stack_push(add_i);
		break;
	}
	case DG_MINUSA: {
		int minus_i = stack_pop().i;
		int dst_i = stack_pop().i;
		struct page *minus = heap_get_delegate_page(minus_i);
		struct page *dst = heap_get_delegate_page(dst_i);
		/* D76: same guard for DG_MINUSA. */
		if (dst_i > 0)
			heap_set_page(dst_i, delegate_minusa(dst, minus));
		stack_push(minus_i);
		break;
	}
	case DG_POP: {
		heap_unref(stack_pop().i);
		break;
	}
	case DG_NEW_FROM_METHOD: {
		int fun = stack_pop().i;
		int obj = stack_pop().i;
		stack_push(heap_alloc_page(delegate_new_from_method(obj, fun)));
		break;
	}
	case DG_CALLBEGIN: { // DG_TYPE
		int dg_no = get_argument(0);
		if (dg_no < 0 || dg_no >= ain->nr_delegates)
			VM_ERROR("Invalid delegate index");
		struct ain_function_type *dg = &ain->delegates[dg_no];

		// Stack before: [dg_page, arg0, ...]
		// Stack after:  [arg0, ..., dg_page, 0(dg_index)]
		int dg_page = stack_peek(dg->nr_arguments).i;
		for (int i = 0; i < dg->nr_arguments; i++) {
			int pos = (stack_ptr - dg->nr_arguments) + i;
			stack[pos-1] = stack[pos];
		}
		stack[stack_ptr-1].i = dg_page;
		stack_push(0);

		// XXX: If the delegate has a return value, we push a dummy value
		//      so that DG_CALL can replace it
		if (dg->return_type.data != AIN_VOID) {
			stack_push(0);
		}
		break;
	}
	case DG_NEW: {
		stack_push(heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0)));
		break;
	}
	case DG_STR_TO_METHOD: {
		int dg_no, str;
		/* libsys4 initialize_instructions(v>=11) gives DG_STR_TO_METHOD one
		 * T_DLG immediate and only ONE stack argument (T_STRING). The legacy
		 * form popped [str, dg_no]; on v12 that consumed an extra value and made
		 * str = -1, so "game_main" was never registered (D66). */
		if (ain->version >= 11) {
			dg_no = get_argument(0);
			str = stack_pop().i;
		} else {
			dg_no = stack_pop().i;
			str = stack_pop().i;
		}
		/* A null string slot means "no named method": resolve to fno 0
		 * instead of dereferencing heap[-1] (D58). */
		if (str < 0) {
			stack_push((union vm_value){ .i = 0 });
			break;
		}
		int fno = get_function_by_name(heap_get_string(str)->text);
		stack_push(function_matches_delegate(dg_no, fno) ? fno : 0);
		heap_unref(str);
		break;
	}
	// -- NOOPs ---
	case FUNC:
		break;
	default:
#ifdef DEBUGGER_ENABLED
		if ((opcode & OPTYPE_MASK) == BREAKPOINT) {
			dbg_handle_breakpoint();
			return execute_instruction(opcode & ~OPTYPE_MASK);
		}
#endif
		VM_ERROR("Illegal opcode: 0x%04x", opcode);
	}
	return opcode;
}

/* 供 HLL（system.Error）打印调用栈用 */
void vm_dump_call_stack(int max)
{
	for (int i = 0; i < max; i++) {
		int idx = call_stack_ptr - (1 + i);
		if (idx < 0)
			break;
		int fno = call_stack[idx].fno;
		if (fno >= 0 && fno < ain->nr_functions && ain->functions[fno].name)
			fprintf(stderr, "  #%d %s\n", i, ain->functions[fno].name);
	}
}

static void vm_execute(void)
{
	for (;;) {
		uint16_t opcode;
		if (instr_ptr == VM_RETURN)
			return;
		if (unlikely(instr_ptr >= ain->code_size)) {
			VM_ERROR("Illegal instruction pointer: 0x%08lX", instr_ptr);
		}
		opcode = get_opcode(instr_ptr);
		opcode = execute_instruction(opcode);
		instr_ptr += instructions[opcode].ip_inc;
	}
}

static void call_global_destructors(void)
{
	if (heap_size <= 0 || heap[0].ref <= 0)
		return;
	struct page *global_page = heap_get_page(0);
	// Call global variable destructors, but do not unref them because the
	// destructors may reference other global variables.
	for (int i = global_page->nr_vars - 1; i >= 0; i--) {
		if (variable_type(global_page, i, NULL, NULL) != AIN_STRUCT)
			continue;
		int slot = global_page->values[i].i;
		delete_struct(heap_get_page(slot)->index, slot);
	}
}

static void vm_free(void)
{
	if (game_dungeons_and_dolls) {
		// Dungeons & Dolls saves the game state in destructors of global variables
		call_global_destructors();
	}

	// call library exit routines
	exit_libraries();
	// flush call stack
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		if (call_stack[i].struct_page >= 0 && AIN_VERSION_GTE(ain, 6, 1))
			exit_unref(call_stack[i].struct_page);
		exit_unref(call_stack[i].page_slot);
	}
	// free globals
	if (heap_size > 0 && heap[0].ref > 0)
		exit_unref(0);

	vm_reset_once = true;
}

static jmp_buf reset_buf;

_Noreturn void vm_reset(void)
{
	vm_free();
	longjmp(reset_buf, 1);
}

int vm_execute_ain(struct ain *program)
{
	ain = program;
	setjmp(reset_buf);

	// initialize VM state
	if (!stack) {
		stack_size = INITIAL_STACK_SIZE;
		stack = xmalloc(INITIAL_STACK_SIZE * sizeof(union vm_value));
	}
	stack_ptr = 0;
	call_stack_ptr = 0;

	heap_init();
	init_libraries();

	// Initialize globals
	heap[0].ref = 1;
	heap[0].seq = heap_next_seq++;
	heap_set_page(0, alloc_page(GLOBAL_PAGE, 0, ain->nr_globals));
	for (int i = 0; i < ain->nr_globals; i++) {
		if (ain->globals[i].type.data == AIN_STRUCT) {
			// XXX: need to allocate storage for global structs BEFORE calling
			//      constructors.
			/* 原来只 alloc_struct，不调构造函数 → 全局结构体的成员
			 * （如 g_ASTimerManager 的数组）永远不初始化 */
			create_struct(ain->globals[i].type.struc, &heap[0].page->values[i]);
		} else {
			heap[0].page->values[i] = variable_initval_typed(&ain->globals[i].type);
		}
	}
	for (int i = 0; i < ain->nr_initvals; i++) {
		int32_t index;
		struct ain_initval *v = &ain->global_initvals[i];
		switch (v->data_type) {
		case AIN_STRING:
			index = heap_alloc_slot(VM_STRING);
			heap[0].page->values[v->global_index].i = index;
			heap[index].s = make_string(v->string_value, strlen(v->string_value));
			break;
		default:
			heap[0].page->values[v->global_index].i = v->int_value;
			break;
		}
	}

	// Reference initializers contain global variable indices, not heap slots.
	// Resolve them after applying ordinary initializers.
	for (int i = 0; i < ain->nr_initvals; i++) {
		struct ain_initval *v = &ain->global_initvals[i];
		switch (ain->globals[v->global_index].type.data) {
		case AIN_REF_STRING:
		case AIN_REF_STRUCT:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY:
		case AIN_REF_DELEGATE:
			if (v->int_value != -1) {
				int slot = global_get(v->int_value).i;
				heap[0].page->values[v->global_index].i = slot;
				heap_ref(slot);
			}
			break;
		case AIN_REF_INT:
		case AIN_REF_FLOAT:
		case AIN_REF_BOOL:
		case AIN_REF_LONG_INT:
		case AIN_REF_FUNC_TYPE:
			if (v->int_value != -1) {
				heap[0].page->values[v->global_index].i = 0;
				heap[0].page->values[v->global_index + 1].i = v->int_value;
				heap_ref(0);
			}
			break;
		default:
			break;
		}
	}

	if (ain->alloc >= 0)
		vm_call(ain->alloc, -1); // function "0": allocate global arrays

	// XXX: global constructors must be called AFTER initializing non-struct variables
	//      otherwise a global set in a constructor will be clobbered by its initval
	for (int i = 0; i < ain->nr_globals; i++) {
		struct ain_type *gt = &ain->globals[i].type;
		if (gt->data != AIN_STRUCT && gt->data != AIN_REF_STRUCT)
			continue;
		/* variable_initval for struct-like globals is -1. Every global that
		 * names a struct must own an object before member methods use it as
		 * `this` (D45: CASConstructionProcess, D47: CASBGM@InitChannel). */
		int slot = heap[0].page->values[i].i;
		if (slot <= 0 && gt->struc >= 0 && gt->struc < ain->nr_structures) {
			slot = alloc_struct(gt->struc);
			heap[0].page->values[i].i = slot;
		}
		if (gt->data == AIN_STRUCT && slot > 0)
			init_struct(gt->struc, slot);
	}

	vm_call(ain->main, -1);
	return stack_pop().i;
}

void vm_stack_trace(void)
{
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		struct ain_function *f = &ain->functions[call_stack[i].fno];
		uint32_t addr = (i == call_stack_ptr - 1) ? instr_ptr : call_stack[i+1].call_address;
		sys_warning("\t0x%08x in %s\n", addr, display_sjis0(f->name));
	}
}

_Noreturn void _vm_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	sys_vwarning(fmt, ap);
	va_end(ap);
	sys_warning("at %s (0x%X) in:\n", current_instruction_name(), instr_ptr);
	vm_stack_trace();

	char msg[1024];
	va_start(ap, fmt);
	vsnprintf(msg, 1024, fmt, ap);
	va_end(ap);

	dbg_repl(DBG_STOP_ERROR, msg);
	sys_exit(1);
}

int vm_time(void)
{
	return SDL_GetTicks();
}

void vm_sleep(int ms)
{
	SDL_Delay(ms);
}

_Noreturn void vm_exit(int code)
{
	vm_free();
#ifdef DEBUG_HEAP
	for (size_t i = 0; i < heap_size; i++) {
		if (heap[i].ref > 0)
			heap_describe_slot(i);
	}
	sys_message("Number of leaked objects: %d\n", heap_free_ptr);
#endif
	sys_exit(code);
}
