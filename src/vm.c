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
#include "parts.h"
#include "scene.h"
#include "gfx/gfx.h"
#include "savedata.h"
#include "vm.h"
#include "pal.h"
#include <malloc.h>
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

/* [D118] Parent local page for lambda frames, indexed by lambda fno.
 * DG_NEW_FROM_METHOD records the creating frame (where captured variables
 * live); X_GETENV reads it back. The frame is kept alive with heap_ref. */
static int *lambda_parent_slot;
static int lambda_parent_n;
static void lambda_parent_init(void)
{
        if (!lambda_parent_slot && ain && ain->nr_functions > 0) {
                lambda_parent_slot = calloc(ain->nr_functions, sizeof(int));
                lambda_parent_n = ain->nr_functions;
        }
}

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

/* [D136] XSYS4_LVAR_DBG=<fno>:<var>: name the instruction that WRITES a
 * given local variable of a given function, with the value written. Built to
 * answer "who put a heap-slot number into the vtable-offset half of an
 * interface local" (QuestMapObjectLoader@Load, var 6); a slot that is never
 * written shows up as silence, which is itself the answer. */
/* [MEM] XSYS4_HLL_TRACE=<lib>,<fno>：见 CALLHLL 分支的说明。 */
static bool hll_trace_enabled(int lib, int fno)
{
	static int wlib = -2, wfno = -1, n;
	if (wlib == -2) {
		const char *e = getenv("XSYS4_HLL_TRACE");
		if (!e || sscanf(e, "%d,%d", &wlib, &wfno) != 2) {
			wlib = -1;
			wfno = -1;
		}
	}
	if (wlib < 0 || lib != wlib || fno != wfno)
		return false;
	return n++ < 60;
}

static int lvar_watch_fno = -2;
static int lvar_watch_var = -1;

static bool lvar_dbg_active(void)
{
	if (lvar_watch_fno == -2) {
		const char *e = getenv("XSYS4_LVAR_DBG");
		if (!e || sscanf(e, "%d:%d", &lvar_watch_fno, &lvar_watch_var) != 2) {
			lvar_watch_fno = -1;
			lvar_watch_var = -1;
		}
	}
	return lvar_watch_fno >= 0;
}

static bool lvar_watch_matches(int fno)
{
	return lvar_dbg_active() && fno == lvar_watch_fno;
}

static void lvar_dbg(int varno, int32_t value, int32_t old)
{
	if (!lvar_dbg_active() || varno != lvar_watch_var || call_stack_ptr <= 0)
		return;
	struct function_call *cf = &call_stack[call_stack_ptr - 1];
	if (cf->fno != lvar_watch_fno)
		return;
	static int n;
	if (n++ > 40)
		return;
	NOTICE("[lvar] ip=0x%zX fno=%d l%d: %d -> %d csp=%d", instr_ptr, cf->fno,
	       varno, old, value, call_stack_ptr);
	/* 谁调进来的：只看最近 6 帧，够回答"这条链从哪来" */
	for (int i = call_stack_ptr - 1, k = 0; i >= 0 && k < 6; i--, k++) {
		struct ain_function *f = &ain->functions[call_stack[i].fno];
		uint32_t addr = (i == call_stack_ptr - 1) ? instr_ptr
							  : call_stack[i + 1].call_address;
		NOTICE("[lvar]   #%d 0x%08X %s", k, addr,
		       f->name ? display_sjis0(f->name) : "?");
	}
}

static bool lvar_dbg_on(void)
{
	static int on = -1;
	if (on < 0)
		on = getenv("XSYS4_LVAR_DBG") ? 1 : 0;
	return on > 0;
}

static void local_set(int varno, int32_t value)
{
	if (lvar_dbg_on()) {
		int32_t old = local_page()->values[varno].i;
		local_page()->values[varno].i = value;
		lvar_dbg(varno, value, old);
		return;
	}
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
#define NULLREF_LOG(why) do { \
	if (getenv("XSYS4_NULLREF_DBG")) { \
		static int _n; \
		if (_n++ < 200) NOTICE("[nullw] %s ip=0x%zX", why, instr_ptr); \
	} } while (0)

/* [D119] REF remembers the last member it read; DG_PLUSA uses it to lazily
 * initialise an empty delegate member (the -1 sentinel). */
static int last_ref_heap = -1;
static int last_ref_index = -1;

static union vm_value *stack_pop_var(void)
{
	int32_t page_index = stack_pop().i;
	int32_t heap_index = stack_pop().i;
	/* [DBG] XSYS4_WATCH_IFACE=1：记录"即将通过指针读写 618 接口数组奇槽"
	 * 的指令。ASSIGN / S_ASSIGN 走 stack_pop_var()[0] = v 直写，不经过
	 * page_set_var，所以只盯 page_set_var 会漏掉真正的写入者。 */
	{
		static int on = -1;
		if (on < 0)
			on = getenv("XSYS4_WATCH_IFACE") ? 1 : 0;
		if (on && heap_index_valid(heap_index) && heap[heap_index].page) {
			struct page *wp = heap[heap_index].page;
			if (wp->type == ARRAY_PAGE && wp->array.struct_type == 618 &&
			    page_index >= 0 && page_index < wp->nr_vars && (page_index & 1)) {
				static int n;
				if (n++ < 30)
					NOTICE("[wiface2] ip=0x%zX heap=%d idx=%d old=%d nr=%d",
					       instr_ptr, heap_index, page_index,
					       wp->values[page_index].i, wp->nr_vars);
			}
		}
	}
	/* D52: tolerate null / never-allocated references during Rance10's
	 * Parts UI init (e.g. ResetPartsUpdateEventLayerID @0x1f16f4).
	 * Both a bad heap_index and a 0-slot ARRAY_PAGE yield a -1 sentinel
	 * instead of aborting startup; owning objects still need proper
	 * initialisation (TECH_DEBT). */
	static union vm_value null_ref_slot = { .i = -1 };
	if (unlikely(!heap_index_valid(heap_index)))
		{ NULLREF_LOG("site1"); return &null_ref_slot; }
	/* D72: a valid slot may still have a NULL page pointer. */
	if (unlikely(!heap[heap_index].page))
		{ NULLREF_LOG("site2"); return &null_ref_slot; }
	/* D72: any 0-slot page (not just ARRAY_PAGE/index 0) is an
	 * uninitialised reference during Parts UI setup. */
	if (heap[heap_index].page && heap[heap_index].page->nr_vars == 0)
		{ NULLREF_LOG("site3"); return &null_ref_slot; }
	/* [D113] Generic HLL refs (Array.First/Last/...) can hand back an
	 * unresolved [page, -1] pair when the callee ignored the predicate.
	 * ASSIGN on such a pair must not abort the VM; the destination is a
	 * compiler-generated dummy return slot that is DELETEd immediately.
	 * Same null_ref_slot strategy as D52. */
	if (unlikely(page_index < 0))
		{ NULLREF_LOG("site4"); return &null_ref_slot; }
	if (unlikely(!heap[heap_index].page || page_index >= heap[heap_index].page->nr_vars)) {
		struct page *pg = heap[heap_index].page;
		/* XSYS4_REFDBG=1：越界读一个 slot 时，把操作数栈顶和本帧的
		 * 局部变量一起打出来。只报"越界"没法判断是索引错还是 this 错。
		 * 注意必须放在 VM_ERROR 前面：后者是 _Noreturn，编译器会把
		 * 它之后的代码全部当作不可达删掉。 */
		if (getenv("XSYS4_REFDBG")) {
			NOTICE("[ref] ip=0x%zX page=%d idx=%d nstack=%d", instr_ptr,
			       heap_index, page_index, stack_ptr);
			if (pg && pg->type == STRUCT_PAGE && pg->index >= 0 &&
			    pg->index < ain->nr_structures)
				NOTICE("[ref]   struct='%s'",
				       display_sjis0(ain->structures[pg->index].name));
			for (int k = 0; k < 8 && k < stack_ptr; k++)
				NOTICE("[ref]   sp-%d = %d", k, stack[stack_ptr - 1 - k].i);
			if (call_stack_ptr > 0) {
				int lp_slot = call_stack[call_stack_ptr - 1].page_slot;
				struct page *lp = heap_get_page(lp_slot);
				if (lp) {
					NOTICE("[ref]   locals slot=%d nr=%d", lp_slot, lp->nr_vars);
					for (int k = 0; k < lp->nr_vars && k < 8; k++)
						NOTICE("[ref]     l%d = %d", k, lp->values[k].i);
				}
			}
		}
		/* [D120] 默认不再中止进程：这里读到的是"空对象的成员"，语义上
		 * 与 D52 的 null_ref_slot 同类（Rance10 的 TextButton 菜单数组在
		 * 场景 init 阶段还是空数组，创建事件却已经被回调）。中止进程会让
		 * 标题场景永远跑不起来；给一个 -1 哨兵槽，让调用方自己的空判断
		 * 生效。XSYS4_STRICT_REF=1 可以恢复成硬错误，供定位用。 */
		if (getenv("XSYS4_STRICT_REF"))
			VM_ERROR("Out of bounds page index: %d/%d (type=%d index=%d nr_vars=%d ref=%d v0=%d v1=%d)",
				 heap_index, page_index,
				 pg ? pg->type : -1, pg ? pg->index : -1, pg ? pg->nr_vars : -1,
				 heap[heap_index].ref,
				 pg && pg->nr_vars > 0 ? pg->values[0].i : -9999,
				 pg && pg->nr_vars > 1 ? pg->values[1].i : -9999);
		static int _oob_n;
		if (_oob_n++ < 20)
			WARNING("Out of bounds page index: %d/%d (type=%d index=%d nr_vars=%d) "
				"ip=0x%zX -> null slot (D120)",
				heap_index, page_index, pg ? pg->type : -1,
				pg ? pg->index : -1, pg ? pg->nr_vars : -1, instr_ptr);
		return &null_ref_slot;
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
	/* [D123] 走统一的槽校验/降级路径，不要直接读 heap[].s。
	 * 后者绕过了 heap_get_string() 对"已释放槽"的兜底：点击标题菜单进入
	 * 新游戏初始化时一批槽被重复 unref，S_NOTE 在这里拿到野指针，
	 * strcmp 直接段错误（core 栈：vm.c S_NOTE -> libc strcmp）。 */
	return heap_get_string(stack_peek(n).i);
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
	/* [L1] "Is this a reference at all?" uses the shared predicate; the
	 * switch below only decides HOW each kind is copied. */
	if (!variable_owns_ref(type))
		return v;
	switch (type) {
	case AIN_STRING:
		return (union vm_value) { .i = vm_string_ref(heap_get_string(v.i)) };
	case AIN_STRUCT:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE: {
		union vm_value r = { .i = vm_copy_page(heap_get_page(v.i)) };
		/* [D116] XSYS4_CP_DBG=1：结构体深拷贝时打源/副本的成员 0。
		 * Parts(1114) 的 m_partsNumber 若在拷贝时还是 0，副本就永远是 0。 */
		if (getenv("XSYS4_CP_DBG") && type == AIN_STRUCT) {
			struct page *sp = heap_get_page(v.i);
			struct page *dp = heap_get_page(r.i);
			if (sp && dp && sp->type == STRUCT_PAGE && sp->index == 1114) {
				static int n;
				if (n++ < 60)
					NOTICE("[cp] src=%d m0=%d -> dst=%d m0=%d ip=0x%zX",
					       v.i, sp->values[0].i, r.i, dp->values[0].i,
					       instr_ptr);
			}
		}
		return r;
	}
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
static void call_trace_probe(int fno);

/* Number of operand-stack slots the parameters of `fn` occupy.
 *
 * AIN stores an AIN_IFACE (and AIN_WRAP) value in TWO consecutive variable
 * slots - [object, vtable offset] - and declares the second one as void. The
 * `nr_args` counter only counts logical parameters, so popping that many
 * values leaves half of every interface argument on the stack and shifts the
 * frame's locals by one slot. Both the argument pop and the local-variable
 * initialisation have to walk slots, not parameters. */
static int function_argument_slots(struct ain_function *fn)
{
	int slots = 0, args = 0;
	while (args < fn->nr_args && slots < fn->nr_vars) {
		enum ain_data_type t = fn->vars[slots].type.data;
		args++;
		slots++;
		/* A multi-slot parameter declares its remaining slots as void.
		 *
		 * [D145] 被跳过的那个 void 槽**本身也算 nr_args 里的一个参数**。
		 * 反例（曾把编成界面整死）：`PartyModeButton@IsEnableDetail::postset`
		 * 声明成 vars=[WRAP oldValue, VOID, dummy, dummy]、nr_args=2 ——
		 * 编译器在调用点只压 2 槽（`[oldValue, value]`），而 nr_args 的 "2"
		 * 已经把 WRAP 的第二槽算进去了。旧版本只跳过 void 不递增 args，
		 * 于是把紧随其后的**局部临时变量**当成第 2 个参数，pop 3 槽，
		 * 把 [obj,fno] 一起弹掉：`this` 变成栈里剩下的 1（struct #0，
		 * 只有 3 个成员），紧接着 `this.m4` 越界 → 整个部队编成界面 core dump。
		 * 跳过 void 时同步 args++，两种声明形式（nr_args 数槽 或 数逻辑参数）
		 * 都能得到正确的槽数。 */
		if (t == AIN_IFACE || t == AIN_WRAP || t == AIN_OPTION)
			while (slots < fn->nr_vars &&
			       fn->vars[slots].type.data == AIN_VOID) {
				slots++;
				args++;
			}
	}
	return slots;
}

/* Same rule for delegate signatures. */
static int delegate_argument_slots(struct ain_function_type *dg)
{
	int slots = 0, args = 0;
	while (args < dg->nr_arguments && slots < dg->nr_variables) {
		enum ain_data_type t = dg->variables[slots].type.data;
		args++;
		slots++;
		if (t == AIN_IFACE || t == AIN_WRAP || t == AIN_OPTION)
			while (slots < dg->nr_variables &&
			       dg->variables[slots].type.data == AIN_VOID)
				slots++;
	}
	return slots;
}

static int _function_call(int fno, int return_address)
{
	/* [D114] Probe here, not in function_call(): delegate_call() bypasses
	 * function_call() and jumps straight to _function_call(), so the old
	 * placement made every DG_CALL-dispatched function look "never called". */
	call_trace_probe(fno);
	struct ain_function *f = &ain->functions[fno];
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
		.entry_sp = -1,
	};
	// initialize local variables
	for (int i = function_argument_slots(f); i < f->nr_vars; i++) {
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

/* [D111d] Per-function entry probe: XSYS4_CALL_TRACE=<fno,fno,...>
 * Prints the first 3 hits and then every 200th, so a "does this AIN function
 * ever run?" question can be answered without a trace window. */
static struct { int fno; int hits; } call_trace[16];
static int call_trace_n = -1;

static void call_trace_probe(int fno)
{
	if (call_trace_n < 0) {
		const char *env = getenv("XSYS4_CALL_TRACE");
		call_trace_n = 0;
		if (env) {
			const char *p = env;
			while (*p && call_trace_n < 16) {
				call_trace[call_trace_n].fno = atoi(p);
				call_trace[call_trace_n].hits = 0;
				call_trace_n++;
				while (*p && *p != ',') p++;
				if (*p == ',') p++;
			}
		}
	}
	for (int i = 0; i < call_trace_n; i++) {
		if (call_trace[i].fno != fno)
			continue;
		int n = ++call_trace[i].hits;
		if (n <= 3 || n % 200 == 0) {
			struct ain_function *f = &ain->functions[fno];
			NOTICE("[call] #%d %s (%d) from 0x%zX", n, display_sjis0(f->name), fno,
			       instr_ptr);
		}
	}
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
	int nslots = function_argument_slots(f);
	for (int i = nslots - 1; i >= 0; i--) {
		heap[slot].page->values[i] = stack_pop();
		/* Deliberately NOT variable_owns_ref(): only ref-typed arguments
		 * are borrowed here; the rest transfer ownership. Widening this
		 * was disproved by an A/B run (13720 -> 13720), see page.h. */
		switch (f->vars[i].type.data) {
		case AIN_REF_TYPE:
			heap_ref(heap[slot].page->values[i].i);
			break;
		default:
			break;
		}
	}
	call_stack[call_stack_ptr-1].entry_sp = stack_ptr;

	/* [D114-probe] XSYS4_FN_DUMP=<fno>: 打印该 AIN 函数前 3 次调用的实参/局部，
	 * 用来回答"参数到底是 0 还是 -1"这种只能靠观测的问题。默认关闭。 */
	if (getenv("XSYS4_FN_DUMP")) {
		static int fn_fno = -2, fn_n;
		if (fn_fno == -2)
			fn_fno = atoi(getenv("XSYS4_FN_DUMP"));
		/* XSYS4_FN_DUMP_SKIP=<v>：跳过首参等于 v 的调用（例如把每帧的悬停
		 * 消息 6 滤掉，只留点击/按键），否则转储全被噪音占满。 */
		static int fn_skip = -1;
		if (fn_skip == -1) {
			const char *sv = getenv("XSYS4_FN_DUMP_SKIP");
			fn_skip = sv ? atoi(sv) : -2;
		}
		/* [D122] XSYS4_FN_DUMP_ALL=1 关掉这个独立的 3 次闸门。
		 * 之前只改了 method_call 分支，普通函数仍有硬上限，导致
		 * "CreateComponent 只调 3 次"这类结论全是假的。 */
		if (fno == fn_fno && fn_skip != -2 && heap[slot].page->values[0].i == fn_skip)
			;
		else if (fno == fn_fno && (getenv("XSYS4_FN_DUMP_ALL") || fn_n++ < 3)) {
			struct page *lp = heap[slot].page;
			NOTICE("[fn] %s(%d) slot=%d nargs=%d nvars=%d",
			       display_sjis0(f->name), fno, slot, f->nr_args, f->nr_vars);
			for (int i = 0; i < f->nr_vars && i < 24; i++)
				NOTICE("[fn]   v%-2d type=%-3d i=%-8d f=%.3f", i,
				       f->vars[i].type.data, lp->values[i].i, lp->values[i].f);
			/* [D122] 字符串实参连文本一起打。之前只打 slot 号，
			 * GetActivityPartsNumber(activity, parts) 这类"按名字查"
			 * 的调用完全看不出传的是哪个名字。 */
			for (int i = 0; i < f->nr_vars && i < 24; i++) {
				if (f->vars[i].type.data != AIN_STRING)
					continue;
				int ss = lp->values[i].i;
				struct string *str =
					(ss > 0 && (size_t)ss < heap_size &&
					 heap[ss].type == VM_STRING) ? heap[ss].s : NULL;
				NOTICE("[fn]     v%d text='%s'", i,
				       str && str->text ? display_sjis0(str->text) : "(null)");
			}
			/* this 结构体成员也 dump 出来：SceneStack@IsActive 这类判据
			 * 全靠 this[6] 之类的字段，只打印实参看不到。 */
			int sp = lp->local.struct_ptr;
			if (sp > 0) {
				struct page *tp = heap_get_page(sp);
				if (tp && tp->type == STRUCT_PAGE) {
					NOTICE("[fn]   this slot=%d nr=%d struct='%s'", sp,
					       tp->nr_vars,
					       (tp->index >= 0 && tp->index < ain->nr_structures)
					       ? display_sjis0(ain->structures[tp->index].name) : "?");
					for (int i = 0; i < tp->nr_vars && i < 12; i++) {
						/* 成员也可能是一个 page 槽（闭包捕获的对象、
						 * 数组、结构体）。把它的 type/index/nr_vars
						 * 一起打出来，才能区分"空指针"与"类型不对"。 */
						int m = tp->values[i].i;
						struct page *mp = (m > 0 && (size_t)m < heap_size)
							? heap_get_page(m) : NULL;
						if (mp)
							NOTICE("[fn]     m%-2d i=%d -> page type=%d"
							       " index=%d nr=%d", i, m, mp->type,
							       mp->index, mp->nr_vars);
						else
							NOTICE("[fn]     m%-2d i=%d", i, m);
					}
				}
			}
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
	/* The `this` slot is popped after function_call() recorded the operand
	 * depth; the frame's baseline is therefore one value lower. */
	call_stack[call_stack_ptr-1].entry_sp = stack_ptr;
}

/* CALLMETHOD 用：栈布局 [obj, fno, args...]，fno 在 args 下面一层 */
static int struct_no_from_function(int fno);

/* [AUTO] XSYS4_AUTO_PARTY=1：把"部队编成界面"从点击里解耦出来。
 *
 * `SceneQuestMap@OpenParty`(fno 23595) 是地图左侧 [部队编制] 按钮的入口，
 * 但它是个**方法**，必须带着地图场景实例的 `this` 才能调。地图上那排按钮
 * 目前既没画出来也没命中盒，靠 xdotool 猜坐标不可行；所以在这里记录
 * SceneQuestMap 的方法被调用时的 `this`（地图一装载就会跑
 * `SceneQuestMap@InitComponent`），帧泵再拿这个页号回调 OpenParty。
 *
 * 这与 `XSYS4_AUTO_MAP` 同性质：只为把跑测从"点得中"变成"确定性"，
 * 不改变任何游戏语义，默认关闭。 */
static bool auto_party_want, auto_party_init;
static int auto_party_page = -1;
static uint32_t auto_party_delay;

static void method_call(int nargs, int return_address)
{
	int fno = stack_peek(nargs).i;
	/* XSYS4_MC_WATCH=<hex>：只在某一个 CALLMETHOD 站点打印现场。用来回答
	 * "这条 CALLMETHOD 到底派发到哪个函数、obj 是哪个堆槽"——栈宽度对不上
	 * 时（callee 声明 N 槽、call site 只压了 1 槽）只能靠猜。 */
	if (getenv("XSYS4_MC_WATCH") && instr_ptr == (size_t)strtoul(getenv("XSYS4_MC_WATCH"), NULL, 16)) {
		static int _mn;
		if (_mn++ < 20) {
			int _obj = stack_peek(nargs + 1).i;
			struct page *_op = (_obj > 0 && (size_t)_obj < heap_size)
				? heap[_obj].page : NULL;
			NOTICE("[mcw] ip=0x%zX nargs=%d fno=%d obj=%d(page type=%d idx=%d nr=%d) sp=%d",
			       instr_ptr, nargs, fno, _obj, _op ? _op->type : -1,
			       _op ? _op->index : -1, _op ? _op->nr_vars : -1, stack_ptr);
			for (int k = 0; k <= nargs + 2 && k <= 4; k++)
				NOTICE("[mcw]   t%d = %d", k, stack_peek(k).i);
			if (fno >= 0 && fno < ain->nr_functions)
				NOTICE("[mcw]   -> %s nr_args=%d nr_vars=%d",
				       display_sjis0(ain->functions[fno].name),
				       ain->functions[fno].nr_args,
				       ain->functions[fno].nr_vars);
			if (_op) {
				for (int k = 0; k < _op->nr_vars && k < 6; k++)
					NOTICE("[mcw]   obj.m%d = %d", k, _op->values[k].i);
			}
		}
	}
	if (fno < 0 || fno >= ain->nr_functions) {
		/* [D112] Interface dispatch: the compiled call site reads the method
		 * slot out of the interface's vtable with REF. When the vtable is not
		 * resolved (D3) that REF yields the vtable ARRAY_PAGE itself instead of
		 * vtable[offset]. An out-of-range "fno" that is a live array page of
		 * function numbers is unambiguous: use its first element (offset 0),
		 * which is what the call site asked for. */
		if (fno > 0 && (size_t)fno < heap_size && heap[fno].page &&
		    heap[fno].page->type == ARRAY_PAGE && heap[fno].page->nr_vars > 0) {
			int real = heap[fno].page->values[0].i;
			static int _n = 0;
			if (_n++ < 20)
				NOTICE("[iface] vtable resolve: %d -> %d (obj=%d nargs=%d)",
				       fno, real, stack_peek(nargs + 1).i, nargs);
			fno = real;
		}
		if (fno < 0 || fno >= ain->nr_functions) {
			/* [D148] 空接口引用的方法调用：降级成"跳过这一次调用"。
			 *
			 * fno==-1 表示调用点从**空对象**（-1，即 AIN 的 null 引用）
			 * 的 vtable 里取方法号，取不到。真实 System4 在这里会抛
			 * 运行时错误，游戏正常路径上不会出现；出现了就是移植侧的
			 * 某个引用没解析出来（见 [NULL-IFACE-CALL] 债务条目）。
			 *
			 * 以前这里是 VM_ERROR 直接杀进程，代价太大：部队编成界面
			 * 只在 10 个页签里的某一个上拿不到对象，整张界面就没了。
			 * 现在按"这次调用什么都没发生"处理 —— 消费调用点的 nargs
			 * 个参数、再消费 [obj, fno]，不压返回值（这类调用实测
			 * 都是 void 的 Init/设置类方法）。每次记一行，用
			 * XSYS4_NULL_METHOD=0 可恢复旧的直接中止行为，便于 A/B。 */
			static int null_method = -1;
			if (null_method < 0) {
				const char *e = getenv("XSYS4_NULL_METHOD");
				null_method = (e && *e == '0') ? 0 : 1;
			}
			if (null_method && fno == -1) {
				static int n;
				int slots = 0;
				int obj_slot = nargs + 1 <= stack_ptr ? stack_peek(nargs + 1).i : -2;
				if (n++ < 40)
					NOTICE("[nullcall] ip=0x%zX nargs=%d obj=%d sp=%d "
					       "-> 跳过（XSYS4_NULL_METHOD=0 可复现中止）",
					       instr_ptr, nargs, obj_slot, stack_ptr);
				/* 参数个数未知（fno 无效），只能按调用点的 nargs 消费：
				 * 反过来用 nargs 而不是 function_argument_slots，因为
				 * 后者需要函数声明。 */
				slots = nargs;
				for (int k = 0; k < slots && stack_ptr > 0; k++)
					stack_pop();
				if (stack_ptr >= 2) {
					stack_pop();   /* fno */
					stack_pop();   /* obj */
				}
				/* [D151] XSYS4_NULL_RET=1：补一个返回值，让这次调用的
				 * **净栈效果**和正常调用一致。
				 *
				 * 为什么：编译器在非 void 调用后面跟一条 POP 来丢弃
				 * 返回值。降级只弹不压，那条 POP 就会吃掉调用者的一个
				 * 槽 —— 判据是 `logs/live_dlg1.log:10487`：
				 *   [nullcall] ip=0x38FF22 -> [dgdrift] delta=-1 -> 崩
				 * 少的那一格正好是 dg_page 的位置，于是下一轮 DG_CALL
				 * 把字符串实参当成了委托页。
				 *
				 * 返回值宽度未知（fno 没解析出来），这里按最常见的
				 * "1 槽" 补；默认关，先 A/B 看能不能把崩溃变成"少初始
				 * 化一个按钮"。 */
				static int null_ret = -1;
				if (null_ret < 0) {
					/* [D154] 默认**开**。A/B 证据（同一序列）：
					 *   关：编队路径 err=1 + core dump
					 *   开：108 活动 / 0 ERROR / 无 core，基线不变
					 * XSYS4_NULL_RET=0 可回退，用于定位"到底跳过了哪些调用"。 */
					const char *e = getenv("XSYS4_NULL_RET");
					null_ret = (e && *e == '0') ? 0 : 1;
				}
				if (null_ret) {
					/* 必须是 -1（AIN 的空引用哨兵），不能用 0：
					 * 0 是保留的 GLOBAL_PAGE 槽，POP 时的
					 * variable_fini 会去 unref 它，实测触发
					 * 43497 次 VM_STRING double free。 */
					union vm_value nil = { .i = -1 };
					stack_push(nil);
				}
				call_stack[call_stack_ptr > 0 ? call_stack_ptr - 1 : 0].entry_sp =
					stack_ptr;
				return;
			}
			VM_ERROR("CALLMETHOD: invalid function index %d", fno);
		}
	}
	/* [AUTO] 记录 SceneQuestMap 场景实例的 `this`（见文件上方 auto_party_page
	 * 的注释）。fno 23586..23620 整个区段都属于 SceneQuestMap，随便哪个方法
	 * 先跑都能拿到同一个页号。 */
	if (!auto_party_init) {
		auto_party_init = true;
		auto_party_want = getenv("XSYS4_AUTO_PARTY") != NULL;
		const char *d = getenv("XSYS4_AUTO_PARTY_DELAY");
		auto_party_delay = d && *d ? (uint32_t)atoi(d) : 6000;
	}
	if (auto_party_want && fno >= 23586 && fno <= 23620 &&
	    nargs + 1 <= stack_ptr) {
		int self = stack_peek(nargs + 1).i;
		if (self > 0 && (size_t)self < heap_size && heap[self].page &&
		    heap[self].page->type == STRUCT_PAGE) {
			if (auto_party_page != self) {
				auto_party_page = self;
				NOTICE("[auto] SceneQuestMap instance = heap slot %d "
				       "(struct %d) via fno %d",
				       self, heap[self].page->index, fno);
			}
		}
	}
	/* [L2] XSYS4_MC_SLOT_DBG=1: why does `this` end up as heap slot 1?
	 * CALLMETHOD's immediate (nargs) is a LOGICAL argument count, while
	 * function_call() pops function_argument_slots(f) PHYSICAL slots.
	 * The [obj, fno] pair sits underneath those arguments, so a mismatch
	 * shifts what the two stack_pop() calls below actually retrieve. */
	static int mcslot = -1;
	if (mcslot < 0)
		mcslot = getenv("XSYS4_MC_SLOT_DBG") ? 1 : 0;
	int sp_before = stack_ptr;
	int csp_before = call_stack_ptr;
	int mc_slots = -1;
	int mc_peek[8] = { 0 };
	if (mcslot) {
		mc_slots = function_argument_slots(&ain->functions[fno]);
		for (int k = 0; k < 8 && k < stack_ptr; k++)
			mc_peek[k] = stack_peek(k).i;
	}
	function_call(fno, return_address);   /* 弹掉 nargs 个 args */
	/* [L2] A fno==0 NULL call takes function_call()'s no-op branch, which
	 * creates NO frame. Everything below assumes a fresh frame exists:
	 * set_struct_page() and local.struct_ptr would then overwrite the
	 * CALLER's `this`, so every later PUSHSTRUCTPAGE in that caller
	 * resolves to the wrong object. Observed in the party screen: `this`
	 * became heap slot 1 (the 3-member CASConstructionProcess), producing
	 * the whole `Out of bounds page index: 1/N` cascade and a core dump.
	 * Consume the call-site slots and return without touching any frame.
	 * XSYS4_NULLMETHOD_FIX=0 restores the old behaviour for A/B. */
	static int nullm_fix = -1;
	if (nullm_fix < 0) {
		const char *e = getenv("XSYS4_NULLMETHOD_FIX");
		nullm_fix = (e && !strcmp(e, "0")) ? 0 : 1;
	}
	if (nullm_fix && call_stack_ptr == csp_before) {
		stack_ptr = sp_before - (nargs + 2);  /* obj, fno, args */
		if (stack_ptr < 0)
			stack_ptr = 0;
		return;
	}
	stack_pop();                          /* 弹掉 fno */
	int obj = stack_pop().i;              /* 弹 obj */
	if (mcslot && (obj == 1 || mc_slots != nargs)) {
		static int mc_n;
		if (mc_n++ < 120)
			NOTICE("[mcslot] ip=0x%zX fno=%d %s nargs=%d slots=%d sp=%d->%d obj=%d top=[%d,%d,%d,%d,%d,%d]",
			       instr_ptr, fno, display_sjis0(ain->functions[fno].name),
			       nargs, mc_slots, sp_before, stack_ptr, obj,
			       mc_peek[0], mc_peek[1], mc_peek[2],
			       mc_peek[3], mc_peek[4], mc_peek[5]);
	}
	/* `obj` and `fno` sit between the callee's baseline and the arguments,
	 * so the frame's operand depth is only final after both are popped. */
	call_stack[call_stack_ptr-1].entry_sp = stack_ptr;
	/* [D116] XSYS4_BADTHIS=1：this 拿到了一个不是堆槽的值（例如 parts 编号），
	 * 后续对成员的写会落到错误地址且不报错。记下函数与调用 IP。 */
	if (getenv("XSYS4_BADTHIS")) {
		bool bad = obj <= 0 || (size_t)obj >= heap_size || !heap[obj].page;
		if (!bad) {
			enum page_type pt = heap[obj].page->type;
			bad = pt != STRUCT_PAGE && pt != IFACE_PAGE;
		}
		const char *nm = (fno > 0 && fno < ain->nr_functions) ?
			display_sjis0(ain->functions[fno].name) : "?";
		if (bad && obj > 0 && nm && strstr(nm, "Parts@")) {
			static int n;
			if (n++ < 30)
				NOTICE("[badthis] fno=%d %s obj=%d ip=0x%zX",
				       fno, nm, obj, instr_ptr);
		}
	}
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
		if (getenv("XSYS4_BADSTRUCT_DBG")) {
			static int n;
			if (n++ < 80)
				NOTICE("[rebuild] fno=%d %s obj=%d bad=%d sno=%d ip=0x%zX ret=0x%zX csp=%d",
				       fno, display_sjis0(ain->functions[fno].name),
				       obj, (int)_obj_bad, sno, instr_ptr,
				       call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].return_address : 0,
				       call_stack_ptr);
		}
		/* [D155] 补建对象按 struct 号**复用**，不要每次新建。
		 *
		 * 实测进编队界面 12 秒里 `[rebuild]` 打到打印上限 80 次 ——
		 * `Admiral@*` 36 次、`InstantTimer@*`、`TrophyCardSet@GetList` …
		 * 全是 `obj=-1`。这些调用本来就拿不到真对象，写进去也没人读，
		 * 但每次都 `alloc_struct` 一个（成员数组还要另分配），
		 * 在按帧调用的路径上就是稳定的泄漏。
		 *
		 * 复用后语义不变（都是"没有对象的临时容器"），分配次数降为
		 * 每个 struct 号一次。XSYS4_PHANTOM_REUSE=0 可退回旧行为。 */
		if (sno >= 0 && sno < ain->nr_structures) {
			static int phantom_on = -1;
			static int *phantom;
			if (phantom_on < 0) {
				const char *e = getenv("XSYS4_PHANTOM_REUSE");
				phantom_on = (e && *e == '0') ? 0 : 1;
				if (phantom_on)
					phantom = calloc(ain->nr_structures, sizeof(int));
			}
			if (phantom_on && phantom && phantom[sno] > 0 &&
			    (size_t)phantom[sno] < heap_size && heap[phantom[sno]].page &&
			    heap[phantom[sno]].page->type == STRUCT_PAGE) {
				obj = phantom[sno];
			} else {
				obj = alloc_struct(sno);
				if (phantom_on && phantom)
					phantom[sno] = obj;
			}
		}
	}
	/* [D132] `this` 指向了错误的结构体类型（例如本该是 CommonStatus(842)
	 * 却拿到 CASConstructionProcess(0, 3 成员)），成员访问必然越界。
	 * 默认关闭，用来定位是谁把对象建成了 struct #0。 */
	if (getenv("XSYS4_BADSTRUCT_DBG")) {
		int want = struct_no_from_function(fno);
		if (want > 0 && obj > 0 && (size_t)obj < heap_size && heap[obj].page &&
		    heap[obj].page->type == STRUCT_PAGE &&
		    heap[obj].page->index != want) {
			static int n;
			if (n++ < 80)
				NOTICE("[badstruct] fno=%d %s obj=%d got=%d want=%d ip=0x%zX "
				       "nargs=%d slots=%d sp=%d->%d csp=%d->%d",
				       fno, display_sjis0(ain->functions[fno].name), obj,
				       heap[obj].page->index, want, instr_ptr,
				       nargs, function_argument_slots(&ain->functions[fno]),
				       sp_before, stack_ptr, csp_before, call_stack_ptr);
		}
	}
	set_struct_page(obj);
	heap[call_stack[call_stack_ptr-1].page_slot].page->local.struct_ptr = obj;

	/* [D116] CALLMETHOD 的 `this` 要等 method_call 才知道（function_call 里
	 * struct_ptr 还是 -1），所以 this 成员转储放在这里。 */
	if (getenv("XSYS4_FN_DUMP") && atoi(getenv("XSYS4_FN_DUMP")) == fno) {
		static int mn;
		int _mn = mn++;
		/* [D122] XSYS4_FN_DUMP_ALL=1 关掉"前 3 次 + 每 300 次"的闸门。
		 * 低频函数（例如 AFL_Parts_AddMouseLClickEvent 全程只调 5 次）
		 * 用原闸门只能看到前 3 次，剩下的永远看不到。 */
		if (getenv("XSYS4_FN_DUMP_ALL") || _mn < 3 || _mn % 300 == 0) {
			if (getenv("XSYS4_CENSUS")) {
				int cnt = 0, zero = 0;
				for (int s2 = 1; s2 < heap_size && cnt < 40; s2++) {
					struct page *pp = heap[s2].page;
					if (!pp || pp->type != STRUCT_PAGE || pp->index != 1114)
						continue;
					cnt++;
					if (pp->values[0].i == 0) zero++;
					NOTICE("[census] slot=%d m0=%d nr=%d", s2, pp->values[0].i, pp->nr_vars);
				}
				NOTICE("[census] Parts 实例=%d 其中 m0==0 的=%d", cnt, zero);
			}
			struct page *tp = heap_get_page(obj);
			if (tp && tp->type == STRUCT_PAGE) {
				NOTICE("[fn] this(fno=%d) slot=%d nr=%d", fno, obj, tp->nr_vars);
				for (int i = 0; i < tp->nr_vars && i < 12; i++) {
					int v = tp->values[i].i;
					struct page *mp = v > 0 ? heap_get_page(v) : NULL;
					NOTICE("[fn]   m%-2d i=%-8d page=%d nr=%d", i, v,
					       mp ? mp->type : -1, mp ? mp->nr_vars : -1);
				}
			} else {
				NOTICE("[fn] this(fno=%d) slot=%d 不是 STRUCT（type=%d）",
				       fno, obj, tp ? tp->type : -1);
			}
		}
	}
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

/* [D150] 目标函数的 (ip, sp) 环形缓冲：栈不平才倒出来。
 * 只记最近 256 条，所以不会因为刷屏把出问题那一次挤出日志。 */
static struct sp_ring_ent { size_t ip; int sp; } sp_ring[256];
static int sp_ring_n;
static int sp_ring_fno = -1;

static void sp_ring_add(size_t ip, int sp)
{
	sp_ring[sp_ring_n & 255].ip = ip;
	sp_ring[sp_ring_n & 255].sp = sp;
	sp_ring_n++;
}

static void sp_ring_dump(int fno, int want, int got)
{
	int cnt = sp_ring_n < 256 ? sp_ring_n : 256;
	int start = sp_ring_n - cnt;
	NOTICE("[spr] fno=%d want=%d got=%d：倒出最近 %d 条", fno, want, got, cnt);
	for (int k = 0; k < cnt; k++) {
		int i = (start + k) & 255;
		NOTICE("[spr]   ip=0x%zX sp=%d", sp_ring[i].ip, sp_ring[i].sp);
	}
}

/* AIN_IFACE / AIN_WRAP occupy two stack slots ([object, vtable offset]).
 * A delegate whose return type is one of them must therefore leave two values
 * behind; a single value leaves the caller's R_ASSIGN with a misaligned pair
 * and the interface variable keeps -1 (D112). */
static int delegate_return_slots(const struct ain_function_type *dg)
{
	switch (dg->return_type.data) {
	case AIN_VOID:
		return 0;
	case AIN_IFACE:
	case AIN_WRAP:
		return 2;
	default:
		return 1;
	}
}

/* [D150] 委托循环的栈基线。
 *
 * `DG_CALLBEGIN` 把栈重排成 [args..., dg_page, dg_index]，之后 `DG_CALL`
 * 在循环里按固定偏移取 dg_page；只要**被调的回调体**多留或少弹一格，
 * 下一次迭代取到的 dg_page 就是别的什么东西（实测是一个 VM_STRING 槽，
 * 于是 heap_get_page 报 `Invalid page index: N (type=1)`）。
 *
 * 所以：CALLBEGIN 时记下 sp，每次 DG_CALL 与它比对。差一格就能直接点名
 * 是哪个 dg_no 的回调体没把栈摆平。默认关闭，XSYS4_DG_DRIFT=1 打开。 */
static struct { int sp; int dg_no; int last_fun; } dg_drift[16];
static int dg_drift_depth;
static int dg_drift_on = -1;

static void dg_drift_push(int dg_no)
{
	if (dg_drift_on < 0)
		dg_drift_on = getenv("XSYS4_DG_DRIFT") ? 1 : 0;
	if (!dg_drift_on || dg_drift_depth >= 16)
		return;
	dg_drift[dg_drift_depth].sp = stack_ptr;
	dg_drift[dg_drift_depth].dg_no = dg_no;
	dg_drift[dg_drift_depth].last_fun = -1;
	dg_drift_depth++;
}

static void dg_drift_check(int dg_no)
{
	if (!dg_drift_on || dg_drift_depth <= 0)
		return;
	int want = dg_drift[dg_drift_depth - 1].sp;
	if (stack_ptr == want)
		return;
	static int n;
	if (n++ < 40) {
		int lf = dg_drift[dg_drift_depth - 1].last_fun;
		NOTICE("[dgdrift] dg_no=%d sp=%d want=%d delta=%d ip=0x%zX fno=%d lastfun=%d %s",
		       dg_no, stack_ptr, want, stack_ptr - want, instr_ptr,
		       call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
		       lf, (lf > 0 && lf < ain->nr_functions)
		       ? display_sjis0(ain->functions[lf].name) : "-");
	}
}

static void delegate_call(int dg_no, int return_address)
{
	if (dg_no < 0 || dg_no >= ain->nr_delegates)
		VM_ERROR("Invalid delegate index");

	dg_drift_check(dg_no);

	// stack: [arg0, ..., dg_page, dg_index, [return_value]]
	int return_values = delegate_return_slots(&ain->delegates[dg_no]);
	int dg_page = stack_peek(1 + return_values).i;
	int dg_index = stack_peek(0 + return_values).i;
	/* [L2] XSYS4_DG_PAGE_DBG=1: `dg_page` sometimes resolves to a live
	 * VM_STRING slot (heap_get_page then warns "Invalid page index (type=1)"
	 * right before the party-screen core dump). Decide whether the return
	 * slot count is wrong or DG_CALLBEGIN picked the wrong stack slot. */
	static int dgpage_dbg = -1;
	if (dgpage_dbg < 0) {
		const char *e = getenv("XSYS4_DG_PAGE_DBG");
		dgpage_dbg = e ? atoi(e) : 0;
	}
	/* =1 prints only type mismatches; =2 traces every hit of one delegate
	 * number (170 here) so the iteration that corrupts the frame is visible. */
	bool dgpage_force = dgpage_dbg >= 2 && dg_no == 170;
	bool dgpage_bad = dg_page > 0 && (size_t)dg_page < heap_size &&
		heap_index_valid(dg_page) && heap[dg_page].type != VM_PAGE;
	if (dgpage_dbg && (dgpage_force || dgpage_bad)) {
		/* [D150] 异常命中**不受限流**。原来这里和正常轨迹共用一个
		 * `n++ < 60` 配额，party 界面里 60 条很快就用光，真正的坏帧
		 * 一条都没打出来——和 `[PROBE-GATED]` 同一个坑。 */
		static int n_normal, n_bad;
		int *cnt = dgpage_bad ? &n_bad : &n_normal;
		if (*cnt < (dgpage_bad ? 200 : 60)) {
			(*cnt)++;
			int t[6] = { 0 }, ty[6];
			for (int k = 0; k < 6 && k < stack_ptr; k++) {
				t[k] = stack_peek(k).i;
				ty[k] = (t[k] > 0 && (size_t)t[k] < heap_size &&
					 heap_index_valid(t[k])) ? heap[t[k]].type : -1;
			}
			NOTICE("[dgpage]%s ip=0x%zX fno=%d dg_no=%d rv=%d ret=%d nargs=%d dg_page=%d(type=%d ref=%d seq=%u) idx=%d sp=%d top=[%d,%d,%d,%d,%d,%d] ty=[%d,%d,%d,%d,%d,%d]",
			       dgpage_bad ? " BAD" : "",
			       instr_ptr,
			       call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
			       dg_no, return_values,
			       ain->delegates[dg_no].return_type.data,
			       ain->delegates[dg_no].nr_arguments,
			       dg_page, heap[dg_page].type, heap[dg_page].ref,
			       heap[dg_page].seq,
			       dg_index, stack_ptr,
			       t[0], t[1], t[2], t[3], t[4], t[5],
			       ty[0], ty[1], ty[2], ty[3], ty[4], ty[5]);
			/* 坏帧额外把栈上每一格的字符串内容打出来：`dg_page`
			 * 落到字符串槽时，那个字符串通常就是"被顶错位"的实参，
			 * 看到文本才能反推错了几格。 */
			if (dgpage_bad)
				for (int k = 0; k < 8 && k < stack_ptr; k++) {
					int v = t[k] < 0 ? 0 : t[k];
					(void)v;
					v = stack_peek(k).i;
					if (v > 0 && (size_t)v < heap_size &&
					    heap_index_valid(v) && heap[v].type == VM_STRING &&
					    heap[v].s && heap[v].s->text)
						NOTICE("[dgpage]   stk[%d] str=\"%.60s\"",
						       k, display_sjis0(heap[v].s->text));
				}
		}
	}
	int obj, fun;
	/* [D115] XSYS4_DG_MISS_DBG=1：DG_CALL 打空炮时也留痕。
	 * 场景的点击处理器若根本没注册进去，这里能看到 dg_no=37601 之后的
	 * 目标 delegate 是"空". */
	struct page *dp = heap_get_delegate_page(dg_page);
	int snap_nr = dp ? dp->nr_vars : -1;
	int snap_obj = (snap_nr >= 3) ? dp->values[0].i : -1;
	int snap_seq = (snap_nr >= 3) ? dp->values[2].i : -1;
	bool dg_ok = delegate_get(dp, dg_index, &obj, &fun);
        if (!dg_ok && getenv("XSYS4_DG_MISS_DBG")) {
                static int miss_n;
                /* [D119] a 40-hit cap fills up during startup; allow filtering
                 * by dg_no for long-running click diagnostics (see §6.13). */
                const char *flt = getenv("XSYS4_DG_MISS_FILTER");
		if ((flt && atoi(flt) == dg_no && miss_n++ < 500) ||
		    (!flt && miss_n++ < 40))
			NOTICE("[dgm] MISS dg_no=%d page=%d idx=%d nr=%d obj0=%d seq0=%d now0=%d ip=0x%zX",
			       dg_no, dg_page, dg_index, snap_nr, snap_obj,
			       snap_seq, heap_get_seq(snap_obj), instr_ptr);
        }
	if (dg_ok) {
		/* [D114] XSYS4_DG_CALL_DBG=1: which AIN function does a per-frame
		 * delegate actually dispatch to? Used to locate the broken
		 * parts::detail::CallPartsUpdateEvent -> CASTask@UpdateEvent chain. */
		static int dgc_dbg_n;
		if (getenv("XSYS4_DG_CALL_DBG") && dgc_dbg_n++ < 20000) {
			const char *nm = "?";
			if (fun > 0 && fun < ain->nr_functions)
				nm = ain->functions[fun].name;
			NOTICE("[dgc] dg_no=%d obj=%d fun=%d %s page=%d idx=%d",
			       dg_no, obj, fun, nm, dg_page, dg_index);
		}
		// pop previous return value
		for (int rv = 0; rv < return_values; rv++)
			stack_pop();
		// increment dg_index
		stack[stack_ptr - 1].i++;

		int slot = _function_call(fun, instr_ptr + instruction_width(DG_CALL));
		if (dg_drift_on && dg_drift_depth > 0)
			dg_drift[dg_drift_depth - 1].last_fun = fun;

		// copy arguments into local page
		struct ain_function_type *dg = &ain->delegates[dg_no];
		int dg_slots = delegate_argument_slots(dg);
		for (int i = dg_slots - 1; i >= 0; i--) {
			/* Stack: [arg slot 0 ... arg slot n-1, dg_page, dg_index] */
			union vm_value arg = stack_peek(2 + (dg_slots - 1 - i));
			heap[slot].page->values[i] = vm_copy(arg, dg->variables[i].type.data);
		}

		/* [D116] delegate 调用直接走 _function_call，绕开了 function_call 里的
		 * FN_DUMP 探针；这里补一份，否则"委托调用的函数"永远 dump 不到。 */
		if (getenv("XSYS4_FN_DUMP") && atoi(getenv("XSYS4_FN_DUMP")) == fun) {
			static int dn;
			int k = dn++;
			if ((k < 3 || k % 300 == 0) && fun > 0 && fun < ain->nr_functions) {
				NOTICE("[fn] %s(%d) [delegate obj=%d slot=%d]",
				       display_sjis0(ain->functions[fun].name), fun, obj,
				       slot, call_stack_ptr);
				for (int i = 0; i < dg->nr_arguments && i < 8; i++) {
					NOTICE("[fn]   a%d type=%d i=%d f=%.3f", i,
					       dg->variables[i].type.data,
					       heap[slot].page->values[i].i,
					       heap[slot].page->values[i].f);
					/* 字符串实参连内容一起打：委托的参数到底传了
					 * 哪个名字，只有看到文本才能和 AIN 里的字面量比对。 */
					if (dg->variables[i].type.data == AIN_STRING) {
						int ss = heap[slot].page->values[i].i;
						struct string *str =
							(ss > 0 && (size_t)ss < heap_size &&
							 heap[ss].type == VM_STRING) ? heap[ss].s : NULL;
						NOTICE("[fn]     text='%s'",
						       str && str->text ? display_sjis0(str->text) : "(null)");
					}
				}
				struct page *tp2 = obj > 0 ? heap_get_page(obj) : NULL;
				if (tp2 && tp2->type == STRUCT_PAGE) {
					NOTICE("[fn]   this slot=%d nr=%d struct='%s'", obj,
					       tp2->nr_vars,
					       (tp2->index >= 0 && tp2->index < ain->nr_structures)
					       ? display_sjis0(ain->structures[tp2->index].name) : "?");
					for (int i = 0; i < tp2->nr_vars && i < 10; i++) {
						int m = tp2->values[i].i;
						struct page *mp = (m > 0 && (size_t)m < heap_size)
							? heap_get_page(m) : NULL;
						if (mp)
							NOTICE("[fn]     m%-2d i=%d -> type=%d idx=%d nr=%d",
							       i, m, mp->type, mp->index, mp->nr_vars);
						else
							NOTICE("[fn]     m%-2d i=%d", i, m);
						/* 数组/结构成员的内容也打出来：闭包捕获的
						 * "菜单对象数组"是否被填过，只看类型看不出来。 */
						if (mp && i < 4)
							for (int j = 0; j < mp->nr_vars && j < 8; j++)
								NOTICE("[fn]       [%d] = %d", j, mp->values[j].i);
					}
				}
			}
		}

		set_struct_page(obj);
	} else {
		// call finished: clean up stack and jump to return address
		/* An AIN_IFACE / AIN_WRAP return value occupies two slots; popping
		 * only one left the second half on the stack, which shifted every
		 * later operand (Scenes::RunIScene then stored a SceneStack into the
		 * `s` variable instead of the scene the factory returned). */
		union vm_value r[2];
		for (int rv = 0; rv < return_values; rv++)
			r[rv] = stack_pop();
		stack_pop(); // dg_index
		stack_pop(); // dg_page
		/* [L2] Offline check (tools/dgcheck) proved that for all 1276
		 * delegates nr_variables == delegate_argument_slots(), so this
		 * teardown is already symmetric with DG_CALLBEGIN. The suspected
		 * "pops too many" hypothesis is disproved; do not change it. */
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
		for (int rv = return_values - 1; rv >= 0; rv--)
			stack_push(r[rv]);
		if (dg_drift_depth > 0)
			dg_drift_depth--;
		instr_ptr = get_argument(1);
	}
}

/* [D154] XSYS4_CTOR_DBG 的现场部分：`instr_ptr` 在"帧准备/委托派发"这类
 * 路径上会指向**被调函数的入口**（例如 0x464374 = GetList 自己的第一条
 * 指令），拿它去 `ain_at atf:` 只会绕回原地。返回地址才是真正的调用点。 */
void vm_dbg_alloc_trace(int struct_no, int slot)
{
	NOTICE("[alloc] struct=%d slot=%d ip=0x%zX csp=%d", struct_no, slot,
	       instr_ptr, call_stack_ptr);
	for (int i = call_stack_ptr - 1, k = 0; i >= 0 && k < 5; i--, k++) {
		int fno = call_stack[i].fno;
		NOTICE("[alloc]   #%d fno=%d %s ret=0x%zX", k, fno,
		       (fno > 0 && fno < ain->nr_functions)
		       ? display_sjis0(ain->functions[fno].name) : "?",
		       call_stack[i].return_address);
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

/* An AIN_IFACE (or AIN_WRAP-wrapped interface) value occupies two stack slots:
 * [object heap slot, vtable offset]. The object's leading struct member is its
 * vtable, i.e. an array page holding AIN function numbers; a method call adds
 * the interface's vtable offset to the method's slot index and looks the
 * function number up there.
 *
 * `ref_slot` is a reference to the implementing object. Its struct index
 * selects the row in ain_interface (e.g. CSpriteParts(329) declares
 * IParts(358)->0 and ISpriteParts(363)->309). Returns false when the object
 * does not implement `iface_no`, which the AIN call sites represent as a null
 * reference (-1). */
bool vm_iface_pair(int iface_no, int ref_slot, int *obj, int *offset)
{
	*obj = -1;
	*offset = 0;
	if (ref_slot <= 0 || (size_t)ref_slot >= heap_size || !heap[ref_slot].page)
		return false;

	struct page *p = heap[ref_slot].page;
	if (p->type == STRUCT_PAGE && p->index >= 0 && p->index < ain->nr_structures) {
		struct ain_struct *st = &ain->structures[p->index];
		for (int i = 0; i < st->nr_interfaces; i++) {
			if (st->interfaces[i].struct_type != iface_no)
				continue;
			*obj = ref_slot;
			*offset = st->interfaces[i].vtable_offset;
			return true;
		}
		if (p->index == iface_no) {
			*obj = ref_slot;
			return true;
		}
		/* A cast to a concrete class only succeeds for an object of exactly
		 * that class, or one that declares the class as an interface (handled
		 * above). The earlier blanket "any live object is a valid downcast"
		 * fallback let a Stack pass as a SceneQuestMap, and the caller then
		 * read member 22 of an 8-member struct. AIN carries no class
		 * inheritance table, so an unverifiable cast must fail and leave the
		 * null reference that every call site already checks for. */
		return false;
	}
	/* Already-populated two-slot interface wrapper. */
	if (p->type == IFACE_PAGE && p->nr_vars >= 2 && p->values[0].i > 0) {
		*obj = ref_slot;
		*offset = p->values[1].i;
		return true;
	}
	return false;
}

/* Invoke the AIN function an AIN_HLL_FUNC argument refers to.
 *
 * AIN v12 pushes a function value as a [this, fno] pair; ffi.c keeps only the
 * function index, so an HLL body sees a bare fno. Rance10 uses these for the
 * predicate / comparator overloads of the Array and Container libraries:
 * `args` are the logical arguments (one element for a predicate, two for a
 * comparator).
 *
 * The function is a closure: it reaches its captured variables through
 * X_GETENV, which resolves the frame that *created* it. For an HLL callback
 * that frame is the caller of the HLL function (the delegate-object path
 * records the same association at DG_NEW_FROM_METHOD), so bind it here.
 *
 * Returns false when an argument cannot be converted to the declared parameter
 * type; the caller then treats the callback as "no match" rather than calling
 * a function with a bogus argument. */
bool vm_call_hll_closure(int fno, const union vm_value *args, int nargs,
			 union vm_value *ret)
{
	if (fno <= 0 || fno >= ain->nr_functions)
		return false;
	struct ain_function *fn = &ain->functions[fno];
	if (fn->address == 0)
		return false;
	if (nargs > fn->nr_args)
		return false;

	lambda_parent_init();
	if (lambda_parent_slot) {
		int lp = local_page_slot();
		if (lp > 0 && (size_t)lp < heap_size && heap[lp].ref > 0 &&
		    lambda_parent_slot[fno] != lp) {
			/* [MEM] 只加不减就是泄漏：闭包每换一次父帧就给旧父帧留一份
			 * 永久的引用，那个局部页于是永远 free 不掉。地图装载时
			 * `QuestMapObjectCollection@Find` 每次谓词调用都会走到这里，
			 * 实测 24 秒内 live 从 2 万涨到 46 万、pages 涨到 25 万，
			 * 正是 [MAP-LOAD-STORM] 的直接来源。旧槽必须先 unref。 */
			int prev = lambda_parent_slot[fno];
			if (prev > 0 && prev != lp && (size_t)prev < heap_size)
				heap_unref(prev);
			lambda_parent_slot[fno] = lp;
			heap_ref(lp);
		}
	}

	int pushed = 0;
	for (int i = 0; i < nargs; i++) {
		struct ain_type *param = &fn->vars[i].type;
		if (param->data == AIN_IFACE || param->data == AIN_WRAP) {
			int obj, offset;
			if (!vm_iface_pair(param->struc, args[i].i, &obj, &offset)) {
				/* Undo the slots already pushed for earlier args. */
				while (pushed-- > 0)
					stack_pop();
				return false;
			}
			stack_push((union vm_value){ .i = obj });
			stack_push((union vm_value){ .i = offset });
			pushed += 2;
		} else {
			stack_push(args[i]);
			pushed++;
		}
	}
	/* AIN declares the tail of a multi-slot parameter list as void (the
	 * second half of an AIN_IFACE parameter, for example). function_call()
	 * pops nr_args slots unconditionally, so pad them here. */
	while (pushed < fn->nr_args) {
		stack_push((union vm_value){ .i = 0 });
		pushed++;
	}

	vm_call(fno, -1);
	/* A callback returning AIN_IFACE / AIN_WRAP leaves two slots behind (see
	 * delegate_return_slots); popping one would leave the operand stack
	 * permanently one value deep. */
	int ret_slots = fn->return_type.data == AIN_VOID ? 0
		: (fn->return_type.data == AIN_IFACE ||
		   fn->return_type.data == AIN_WRAP) ? 2 : 1;
	union vm_value result = { .i = 0 };
	if (ret_slots > 0)
		result = stack_pop();
	if (ret_slots > 1)
		stack_pop();
	if (ret)
		*ret = result;
	return true;
}

/* One-argument form: the Array element predicates. */
bool vm_call_hll_func(int fno, union vm_value elem, union vm_value *ret)
{
	return vm_call_hll_closure(fno, &elem, 1, ret);
}

static void function_return(void)
{
	if (call_stack_ptr <= 0) {
		NOTICE("RET: 调用栈已空，VM 退出");
		vm_exit(0);
		return;
	}
	struct function_call *cf = &call_stack[call_stack_ptr - 1];
	/* Frames entered through _function_call() (delegate calls, vm_call) never
	 * had their operand stack recorded, hence entry_sp == -1. */
	if (getenv("XSYS4_SP_BALANCE") && cf->entry_sp >= 0 &&
	    cf->fno > 0 && cf->fno < ain->nr_functions) {
		int rt = ain->functions[cf->fno].return_type.data;
		int want = cf->entry_sp + (rt == AIN_VOID ? 0 :
			(rt == AIN_IFACE || rt == AIN_WRAP) ? 2 : 1);
		if (stack_ptr != want) {
			static int n;
			static int cap = -1;
			if (cap < 0) {
				const char *c = getenv("XSYS4_SP_BALANCE");
				cap = c && *c && *c != '1' ? atoi(c) : 40;
			}
			if (n++ < cap)
				NOTICE("[sp] fno=%d %s want=%d got=%d (entry=%d ret_type=%d)",
				       cf->fno, display_sjis0(ain->functions[cf->fno].name),
				       want, stack_ptr, cf->entry_sp, rt);
		}
	}
	if (sp_ring_fno >= 0 && cf->fno == sp_ring_fno && cf->entry_sp >= 0) {
		int rt = ain->functions[cf->fno].return_type.data;
		int want = cf->entry_sp + (rt == AIN_VOID ? 0 :
			(rt == AIN_IFACE || rt == AIN_WRAP) ? 2 : 1);
		if (stack_ptr != want)
			sp_ring_dump(cf->fno, want, stack_ptr);
	}
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
	/* [MEM] XSYS4_RSS_LIMIT=<MB>：进程 RSS 超过阈值就报错中止，并在错误里
	 * 带上 fno/ip 与完整 AIN 调用栈。
	 * 为什么需要它：一旦代码停在一次很长的 vm_call 里（例如场景装载），
	 * 帧泵不再运行，`[mem]` 探针一行也不会输出 —— 这时只能靠在指令级检查，
	 * 否则只能看到"进程 RSS 涨到几个 G 然后被杀"，没有任何栈可查。
	 * 每 65536 条指令查一次，代价可忽略。默认关（0）。 */
	static long rss_limit = -2;
	static uint32_t rss_tick;
	if (rss_limit == -2) {
		const char *e = getenv("XSYS4_RSS_LIMIT");
		rss_limit = (e && *e) ? atol(e) : 0;
	}
	if (rss_limit > 0 && (++rss_tick & 0xFFFF) == 0) {
		uint64_t rss_b = 0, peak_b = 0;
		if (pal_get()->mem && pal_get()->mem->usage)
			pal_get()->mem->usage(&rss_b, &peak_b);
		if (rss_b > (uint64_t)rss_limit * 1024u * 1024u) {
			extern size_t gfx_texture_count, gfx_texture_pixels;
			VM_ERROR("RSS limit exceeded (%llu MB > %ld MB), live slots=%zu "
				 "textures=%zu tex_mb=%zu: runaway allocation outside the "
				 "VM heap. Raise with XSYS4_RSS_LIMIT",
				 (unsigned long long)(rss_b / (1024 * 1024)), rss_limit,
				 heap_free_ptr, gfx_texture_count,
				 gfx_texture_pixels * 4 / (1024 * 1024));
		}
	}
	/* [DBG] XSYS4_DUMP_AT=<hex>[,<hits>]：在指定 ip 停下并转储现场。
	 * 原来只打一次、只看栈；现在默认打 1 次、可用逗号调到多次，并把
	 * **当前帧的局部变量（带名字和声明类型）**与 8 帧 AIN 调用栈一并输出。
	 * 这是"原生断点"：不必起 gdb，也不必为每个问题临时加探针。 */
	static uint32_t dump_ip = 0;
	static int dump_ready = 0, dumped = 0, dump_max = 1;
	if (!dump_ready) {
		const char *d = getenv("XSYS4_DUMP_AT");
		if (d) {
			sscanf(d, "%x,%d", &dump_ip, &dump_max);
			if (dump_max <= 0)
				dump_max = 1;
		}
		dump_ready = 1;
	}
	if (dump_ip && dumped < dump_max && instr_ptr == dump_ip) {
		dumped++;
		NOTICE("[dump] ip=0x%08zx sp=%d heap_size=%zu hit=%d", instr_ptr, stack_ptr,
		       heap_size, dumped);
		if (call_stack_ptr > 0) {
			struct function_call *cf = &call_stack[call_stack_ptr - 1];
			struct ain_function *fn = &ain->functions[cf->fno];
			NOTICE("[dump] frame %s(%d) fno=%d", display_sjis0(fn->name), cf->fno,
			       cf->fno);
			struct page *lp = local_page();
			/* `this`：AIN 的 PUSHSTRUCTPAGE 取的是当前帧 local.struct_ptr。
			 * 定位"this 变成了别的对象"时，光看 locals 不够——必须把
			 * this 的页本身和它的前几个成员打出来。 */
			if (cf->struct_page > 0 && (size_t)cf->struct_page < heap_size &&
			    heap[cf->struct_page].page) {
				struct page *tp = heap[cf->struct_page].page;
				NOTICE("[dump] this=slot %d type=%d struct=%d nr=%d",
				       cf->struct_page, tp->type, tp->index, tp->nr_vars);
				for (int i = 0; i < tp->nr_vars && i < 12; i++) {
					int mv = tp->values[i].i;
					NOTICE("[dump]   this.m%d = %d", i, mv);
					/* 成员是数组时把前几个元素一起打出来：
					 * "数组元素是空引用"和"下标算错"是两种完全不同的病。 */
					if (mv > 0 && (size_t)mv < heap_size && heap[mv].page &&
					    heap[mv].page->type == ARRAY_PAGE) {
						struct page *ap = heap[mv].page;
						char buf[256] = "";
						size_t off = 0;
						for (int k = 0; k < ap->nr_vars && k < 12; k++)
							off += snprintf(buf + off, sizeof(buf) - off,
							                " %d", ap->values[k].i);
						NOTICE("[dump]     -> array nr=%d%s", ap->nr_vars, buf);
					} else if (mv > 0 && (size_t)mv < heap_size && heap[mv].page &&
						   heap[mv].page->type == DELEGATE_PAGE) {
						/* [D157] 委托成员也展开元素数。`m9`(`<OnClickEvent>`)
						 * 里有没有元素，直接决定"注册成功没有"——
						 * 这是"点击没反应"最需要回答的一个数。 */
						struct page *dp = heap[mv].page;
						char buf[256] = "";
						size_t off = 0;
						for (int k = 0; k + 1 < dp->nr_vars && k < 24; k += 2)
							off += snprintf(buf + off, sizeof(buf) - off,
							                " [obj=%d,fun=%d]",
							                dp->values[k].i, dp->values[k + 1].i);
						NOTICE("[dump]     -> delegate nr=%d%s", dp->nr_vars, buf);
					}
				}
			} else {
				NOTICE("[dump] this=slot %d (no page)", cf->struct_page);
			}
			for (int i = 0; lp && i < lp->nr_vars && i < fn->nr_vars; i++) {
				struct ain_type *vt = &fn->vars[i].type;
				char *ts = ain_strtype_d(ain, vt);
				int32_t v = lp->values[i].i;
				/* 引用型局部要连"它指向的页"一起打出来（长度/类型），
				 * 否则看不出这个容器是不是在长。 */
				char extra[160] = "";
				if (v > 0 && (size_t)v < heap_size &&
				    heap[v].type == VM_STRING) {
					/* 字符串局部：把文本本身打出来。只看堆槽号无法判断
					 * "GetPart 取到了第几个字符"这类差一错误。
					 * 必须在 page 分支之前判断：字符串槽的 union 里
					 * page 指针是脏的，会被误当成页。 */
					snprintf(extra, sizeof(extra), "  \"%.120s\"",
					         heap_get_string(v)->text);
				} else if (v > 0 && (size_t)v < heap_size && heap[v].page) {
					struct page *pp = heap[v].page;
					snprintf(extra, sizeof(extra),
						 "  [page type=%d a_type=%d nr=%d ref=%d]",
						 pp->type, pp->a_type, pp->nr_vars, heap[v].ref);
				}
				NOTICE("[dump]   l%d %-16s %-20s = %d%s", i,
				       fn->vars[i].name ? display_sjis0(fn->vars[i].name) : "?",
				       ts ? ts : "?", v, extra);
				free(ts);
			}
			for (int i = call_stack_ptr - 1, k = 0; i >= 0 && k < 8; i--, k++)
				NOTICE("[dump]   #%d %s", k,
				       display_sjis0(ain->functions[call_stack[i].fno].name));
		}
		for (int i = stack_ptr - 1; i >= 0 && i >= stack_ptr - 10; i--) {
			int32_t v = stack[i].i;
			if (v > 0 && (size_t)v < heap_size && heap_index_valid(v) &&
			    heap[v].type == VM_STRING) {
				/* [D152] 栈上的字符串槽连文本一起打。栈转储原来只认
				 * 页，字符串槽会掉进 "(nonheap)" 或被打成脏页——
				 * 而"这条指令要 Int() 的到底是哪个串"正是差一位类
				 * 问题的关键证据。 */
				NOTICE("[dump]  st[%d]=%d str=\"%s\"", i, v,
				       heap[v].s && heap[v].s->text
				       ? display_sjis0(heap[v].s->text) : "");
			} else if (v > 0 && (size_t)v < heap_size) {
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
	/* [D150] XSYS4_SP_TRACE=<fno>：把目标函数的 (ip, sp) 记进环形缓冲，
	 * 退出时若栈不平就整段倒出来。
	 *
	 * 为什么不是"边跑边打"：函数绝大多数调用是平的，刷屏只会把出问题那次
	 * 挤出日志（本轮已经被限流探针骗过两次）。环里只留最近 256 条，
	 * 不平才倒，倒出来的就是那一次。 */
	{
		static int tr_fno = -2;
		if (tr_fno == -2) {
			const char *e = getenv("XSYS4_SP_TRACE");
			tr_fno = (e && *e) ? atoi(e) : -1;
			sp_ring_fno = tr_fno;
		}
		if (tr_fno >= 0 && call_stack_ptr > 0 &&
		    call_stack[call_stack_ptr - 1].fno == tr_fno)
			sp_ring_add(instr_ptr, stack_ptr);
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
		/* [DBG] XSYS4_REF_AT=<hex ip>：在这一条 REF 上无条件打印基址/下标
		 * 以及基址页的形状。查"接口 vtable 取方法号"这类问题必须看这个：
		 * 越界探针只在出错时打印，而返回 -1 的情况（下标合法但槽是 -1）
		 * 恰恰是最常见的失败形态。 */
		{
			static long ref_at = -2;
			if (ref_at == -2) {
				const char *e = getenv("XSYS4_REF_AT");
				ref_at = (e && *e) ? strtol(e, NULL, 16) : -1;
			}
			if (ref_at >= 0 && (long)instr_ptr == ref_at) {
				struct page *bp = (_hidx > 0 && (size_t)_hidx < heap_size)
						  ? heap[_hidx].page : NULL;
				static int nref;
				if (nref++ < 12)
					NOTICE("[refat] ip=0x%zX base=%d(%s nr=%d idx=%d a_type=%d) "
					       "index=%d -> slot=%d",
					       instr_ptr, _hidx,
					       bp ? (bp->type == STRUCT_PAGE ? "struct"
						   : bp->type == ARRAY_PAGE ? "array" : "page")
						  : "none",
					       bp ? bp->nr_vars : -1, bp ? bp->index : -1,
					       bp ? bp->a_type : -1, _pidx,
					       (bp && _pidx >= 0 && _pidx < bp->nr_vars)
					       ? bp->values[_pidx].i : -9999);
			}
		}
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
		last_ref_heap = _hidx;
		last_ref_index = _pidx;
		stack_push(_v->i);
		break;
	}
	case REFREF: {
		// Dereference a reference to a reference.
		/* [D136] XSYS4_REFREF_DBG=<hex ip>: dump the page a specific REFREF
		 * is reading from -- type, settled element kind, struct type, size
		 * and the first slots with their heap kinds. Built to identify the
		 * container behind QuestMapObjectLoader@Load's 2-slot elements
		 * (0x37cb90), whose second half is a heap slot instead of a vtable
		 * offset. Address comes from the env so no address is baked in. */
		static long rrdbg = -2;
		if (rrdbg == -2) {
			const char *e = getenv("XSYS4_REFREF_DBG");
			rrdbg = (e && *e) ? strtol(e, NULL, 16) : -1;
		}
		if (rrdbg >= 0 && (long)instr_ptr == rrdbg) {
			int hs = stack_peek(1).i, pi = stack_peek(0).i;
			struct page *pg = heap_index_valid(hs) ? heap[hs].page : NULL;
			NOTICE("[refref] ip=0x%zX ptr=%p page=%d idx=%d type=%d a_type=%d struc=%d rank=%d nr=%d",
			       instr_ptr, (void*)pg, hs, pi, pg ? pg->type : -1,
			       pg ? pg->a_type : -1,
			       pg ? pg->array.struct_type : -1, pg ? pg->array.rank : -1,
			       pg ? pg->nr_vars : -1);
			for (int k = 0; pg && k < pg->nr_vars && k < 8; k++) {
				int v = pg->values[k].i;
				const char *ht = "int";
				if (heap_index_valid(v) && v > 0) {
					struct page *hp = heap[v].page;
					if (heap[v].type == VM_STRING) ht = "string";
					else ht = hp ? (hp->type == STRUCT_PAGE ? "struct"
						: hp->type == ARRAY_PAGE ? "array" : "page") : "nullpage";
				}
				NOTICE("[refref]   v[%d] = %d (%s)", k, v, ht);
			}
		}
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
		/* [D116] XSYS4_IP_WATCH=<hex_lo>:<hex_hi>：只看某个地址区间内的赋值，
		 * 直接看到"写到了哪个 slot、写了什么值"。 */
		if (getenv("XSYS4_IP_WATCH")) {
			static size_t lo = (size_t)-1, hi;
			if (lo == (size_t)-1) {
				const char *w = getenv("XSYS4_IP_WATCH");
				sscanf(w, "%zx:%zx", &lo, &hi);
			}
			if ((size_t)instr_ptr >= lo && (size_t)instr_ptr <= hi) {
				static int n;
				if (n++ < 40)
					NOTICE("[ipw] ip=0x%zX heap=%d page=%d val=%d",
					       instr_ptr, stack_peek(2).i, stack_peek(1).i,
					       stack_peek(0).i);
			}
		}
		/* [D126] XSYS4_OBJ_WATCH=<heap_slot>:<member>：盯着某个对象的某个
		 * 成员被谁写。用来抓"AIN 绕过属性 setter、直接改成员"这类写入
		 * （典型：GameContext.m_gamePhase 在进章节前就变成 1，
		 * 而 Phase::set 全程只被调用一次）。命中时打印 ip，再用
		 * `ain_at atf:0x...` 定位到函数。
		 * 例：XSYS4_OBJ_WATCH=2617:3 */
		if (getenv("XSYS4_OBJ_WATCH")) {
			static long ws = -1, wm = -1;
			static int wstruct = -1;
			if (ws == -1) {
				const char *w = getenv("XSYS4_OBJ_WATCH");
				if (w[0] == '#') {
					if (sscanf(w, "#%d:%ld", &wstruct, &wm) != 2)
						wstruct = -2;
				} else if (sscanf(w, "%ld:%ld", &ws, &wm) != 2) {
					ws = -2;
				}
			}
			/* 栈布局（顶→底）：value, member_index, heap_slot。
			 * stack_pop_var() 先弹 page_index 再弹 heap_index，
			 * 所以探测时 peek(0)=新值、peek(1)=成员号、peek(2)=槽号。 */
			int _dh = stack_peek(2).i, _dp = stack_peek(1).i;
			bool _hit = false;
			if (ws > 0 && _dh == ws && _dp == wm)
				_hit = true;
			else if (wstruct >= 0 && _dp == wm && _dh > 0 &&
				 (size_t)_dh < heap_size && heap[_dh].page &&
				 heap[_dh].page->type == STRUCT_PAGE &&
				 heap[_dh].page->index == wstruct)
				_hit = true;
			if (_hit) {
				static int _wn;
				if (_wn++ < 40)
					NOTICE("[objw] slot=%d m=%d val=%d ip=0x%zX fno=%d",
					       _dh, _dp, stack_peek(0).i, instr_ptr, hll_current_fno());
			}
		}
		union vm_value val = stack_pop();
		/* [D136] ASSIGN writes the slot directly, so local_set() never sees
		 * it and XSYS4_LVAR_DBG had a blind spot for the most common way a
		 * local is assigned. Strictly opt-in: with the switch off this is
		 * the original one-liner, so the default path stays byte-identical. */
		if (lvar_dbg_on()) {
			union vm_value *dstv = stack_pop_var();
			struct page *lp = local_page();
			if (lp && dstv >= lp->values && dstv < lp->values + lp->nr_vars)
				lvar_dbg((int)(dstv - lp->values), val.i, dstv->i);
			dstv[0] = val;
		} else {
			stack_pop_var()[0] = val;
		}
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
/* [D118] X_GETENV (0x103): in [local_page] -> out [parent_local_page].
         * The parent is the frame that CREATED this lambda (recorded at
         * DG_NEW_FROM_METHOD), not the current caller: FadeIn's onFinish
         * lambdas read the captured DG_Event from FadeIn's var[2]. */
        case X_GETENV: {
                stack_pop();
                int fno = call_stack[call_stack_ptr-1].fno;
                int parent = (fno > 0 && fno < lambda_parent_n) ? lambda_parent_slot[fno] : -1;
                if (parent > 0 && parent < heap_size && heap[parent].page) {
                        /* [D137] XSYS4_ENV_DBG=2：连"捕获成功"的情况也把父帧
                         * 内容打出来。比较 lambda 靠这里取 key；如果父帧里的
                         * 值不是预期的键，闭包捕获就是坏的，二分必然恒定走
                         * 同一边（实测 `KeyValueMap@Find` 的 lambda 恒返回 -1）。 */
                        static int envdbg = -1;
                        if (envdbg < 0) {
                                const char *e = getenv("XSYS4_ENV_DBG");
                                envdbg = e ? atoi(e) : 0;
                        }
                        if (envdbg >= 2) {
                                static int n;
                                if (n++ < 30) {
                                        struct page *pp = heap[parent].page;
                                        NOTICE("[env] fno=%d %s parent=%d nr=%d v0=%d v1=%d v2=%d v3=%d",
                                               fno, display_sjis0(ain->functions[fno].name),
                                               parent, pp->nr_vars,
                                               pp->nr_vars > 0 ? pp->values[0].i : -9999,
                                               pp->nr_vars > 1 ? pp->values[1].i : -9999,
                                               pp->nr_vars > 2 ? pp->values[2].i : -9999,
                                               pp->nr_vars > 3 ? pp->values[3].i : -9999);
                                }
                        }
                        stack_push(parent);
                } else {
                        static int n;
                        if (getenv("XSYS4_ENV_DBG") && n++ < 40)
                                NOTICE("[env] fno=%d %s has no parent frame (slot=%d)",
                                       fno, display_sjis0(ain->functions[fno].name), parent);
                        stack_push(-1);
                }
                break;
        }
	case X_SET: {
		/* Store through a reference: [destination, value] -> [value].
		 *
		 * Rance10's main use is array assignment (`member = <array>`, e.g.
		 * SceneQuestSelect@LoadQuestList filling m_questIds). AIN array
		 * assignment copies the payload into the destination object, so the
		 * destination slot keeps its identity and the elements are copied
		 * over. The previous implementation popped both values without
		 * storing anything, which left every such member empty.
		 *
		 * Other shapes are left untouched for now: a census of the 300 call
		 * sites is still outstanding (D44). */
		union vm_value value = stack_pop();
		int dst = stack_pop().i;
		bool copied = false;
		if (dst > 0 && (size_t)dst < heap_size && heap[dst].page &&
		    heap[dst].page->type == ARRAY_PAGE &&
		    value.i > 0 && (size_t)value.i < heap_size &&
		    heap[value.i].page && heap[value.i].page->type == ARRAY_PAGE) {
			struct page *d = heap[dst].page;
			struct page *src = heap[value.i].page;
			union vm_value dim = { .i = src->nr_vars };
			struct page *np = realloc_array(d, 1, &dim, src->a_type,
							src->array.struct_type, false);
			if (np) {
				np->nr_vars = src->nr_vars;
				enum ain_data_type elem = array_type(src->a_type);
				for (int i = 0; i < src->nr_vars; i++) {
					np->values[i] = src->values[i];
					if (elem != AIN_STRUCT && elem != AIN_STRING &&
					    elem != AIN_DELEGATE && elem != AIN_ARRAY)
						continue;
					int v = np->values[i].i;
					if (v > 0 && (size_t)v < heap_size && heap[v].ref > 0)
						heap_ref(v);
				}
				heap[dst].page = np;
				copied = true;
				if (getenv("XSYS4_XSET_DBG")) {
					static int n;
					if (n++ < 20)
						NOTICE("[xset] copy dst=%d nr=%d a_type=%d -> value=%d nr=%d",
						       dst, d->nr_vars, np->a_type,
						       value.i, np->nr_vars);
				}
			}
		}
		if (!copied && getenv("XSYS4_XSET_DBG")) {
			static int n;
			if (n++ < 20)
				NOTICE("[xset] unrelated shapes dst=%d value=%d ip=0x%zX",
				       dst, value.i, instr_ptr);
		}
		stack_push(value);
		break;
	}
	case X_ICAST: {
		int iface_no = get_argument(0);
		int ref_slot = stack_peek(0).i;
		int obj, offset;
		bool ok = vm_iface_pair(iface_no, ref_slot, &obj, &offset);
		/* [object, vtable offset] - object in the lower slot. Bytecode
		 * consumes the pair positionally: `X_ICAST` is immediately followed
		 * by R_ASSIGN (dst_page, dst_var, src_page, src_var) and by REFREF
		 * inside method calls, both of which need the object first. Pushing
		 * the offset first made R_ASSIGN copy from a bogus page and handed
		 * GetResult a different object than the one LoadQuestList filled. */
		stack_push((union vm_value){ .i = offset });
		stack_push((union vm_value){ .i = ok ? obj : -1 });
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
		if (getenv("XSYS4_RA_DBG") && instr_ptr >= 0x408f00 && instr_ptr <= 0x408f40) {
			NOTICE("[ra] ip=0x%zX dstPage=%p dstType=%d dstVar=%d src=(%d,%d)",
			       instr_ptr, (void*)dst, dst ? dst->type : -1, dst_var,
			       src_page, src_var);
		}
		/* [N100-x86_64] heap_get_page() returns NULL for a dead/invalid slot
		 * (see D96 in heap.c: "Callers already handle NULL"). R_ASSIGN was the
		 * one caller that did not: on x86_64 an invalid index (e.g. 314950,
		 * an AIN code address rather than a heap slot) reaches here and
		 * page_set_var() dereferences NULL -> SIGSEGV.
		 * Keep the reference pair on the stack (stack balance) and skip the
		 * store, exactly like the other tolerant callers. */
		if (unlikely(!dst)) {
			static int _ra_null_n = 0;
			if (_ra_null_n++ < 20)
				WARNING("[R_ASSIGN] null dst page ip=0x%zX src=(%d,%d) dstvar=%d - skipped",
				        instr_ptr, src_page, src_var, dst_var);
			stack_push(src_page);
			stack_push(src_var);
			break;
		}
		if (dst == local_page()) {
			lvar_dbg(dst_var, src_page, dst->values[dst_var].i);
			if (dst_var + 1 < dst->nr_vars)
				lvar_dbg(dst_var+1, src_var, dst->values[dst_var+1].i);
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
		/* [MEM] XSYS4_HLL_TRACE=<lib>,<fno>：打印某 HLL 调用的栈顶实参
		 * （整数原样，页引用给出它的 nr_vars/a_type）。用于回答"这个调用
		 * 每次拿到的容器是不是在变长"——Numof 的返回值一路涨，说明循环
		 * 里在追加，而不是循环边界算错。 */
		if (hll_trace_enabled(get_argument(0), get_argument(1))) {
			int a0 = stack_peek(0).i;
			struct page *p0 = heap_index_valid(a0) ? heap[a0].page : NULL;
			if (p0)
				NOTICE("[hll] lib=%d fno=%d arg0=slot %d -> page type=%d "
				       "a_type=%d nr_vars=%d struc=%d ip=0x%zX",
				       get_argument(0), get_argument(1), a0, p0->type,
				       p0->a_type, p0->nr_vars, p0->array.struct_type,
				       instr_ptr);
			else
				NOTICE("[hll] lib=%d fno=%d arg0=%d ip=0x%zX",
				       get_argument(0), get_argument(1), a0, instr_ptr);
			/* 第一次命中时把 AIN 调用栈打一遍：回答"谁在反复插入"。
			 * 只在第一次打印，避免风暴期间刷屏（实测 40 秒 2.7 万次）。 */
			static bool traced;
			if (!traced) {
				traced = true;
				for (int i = call_stack_ptr - 1, k = 0; i >= 0 && k < 10; i--, k++) {
					struct ain_function *f = &ain->functions[call_stack[i].fno];
					NOTICE("[hll]   #%d %s", k,
					       f->name ? display_sjis0(f->name) : "?");
				}
			}
		}
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
		/* [MEM] XSYS4_LVAR_DBG=<fno>:<var> 打开时，在被观察函数里把每次
		 * 浮点→整数转换的原始值打出来。`QuestMapObjectLoader@Load` 的循环
		 * 上界 rIndex 正是这样来的，垃圾浮点在这一步就能看见。 */
		if (call_stack_ptr > 0 && lvar_watch_matches(call_stack[call_stack_ptr-1].fno)) {
			static int nf;
			if (nf++ < 40)
				NOTICE("[ftoi] ip=0x%zX fno=%d float=%.9g -> %d", instr_ptr,
				       call_stack[call_stack_ptr-1].fno,
				       (double)stack_peek(0).f, (int32_t)stack_peek(0).f);
		}
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
		/* [D114] XSYS4_DG_ASSIGN_DBG=1: verify the source delegate really
		 * carries entries before it is copied into an event registration. */
		static int dga_dbg_n;
		if (getenv("XSYS4_DG_ASSIGN_DBG") && dga_dbg_n++ < 60)
			NOTICE("[dga] set_i=%d dst_i=%d set=%p type=%d nr=%d new=%p nr=%d",
			       set_i, dst_i, (void*)set, set ? set->type : -1,
			       set ? set->nr_vars : -1, (void*)new_dg,
			       new_dg ? new_dg->nr_vars : -1);
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
		/* [D119 probe] which delegate page receives the OK/Cancel lambdas */
		if (getenv("XSYS4_DGPLUSA_DBG")) {
			static int dgp_n;
			/* [D122] 上限可调：默认 400，排查"某个注册到底有没有发生"时
			 * 前 400 条会被 startup 的 CParts@Attach 占满。 */
			const char *mx = getenv("XSYS4_DGPLUSA_MAX");
			int limit = mx ? atoi(mx) : 400;
			if (dgp_n++ < limit) {
				/* 打印发起注册的 AIN 函数，才能把"谁注册的"和
				 * "谁没注册"对上。 */
				int cf = -1;
				for (int i = 0; i < ain->nr_functions; i++)
					if ((size_t)ain->functions[i].address <= instr_ptr &&
					    (cf < 0 || ain->functions[i].address > ain->functions[cf].address))
						cf = i;
				NOTICE("[dg+] add_i=%d fun=%d dst_i=%d dst_nr=%d refheap=%d refidx=%d from=%d %s",
				       add_i, add ? add->values[1].i : -1, dst_i,
				       dst ? dst->nr_vars : -1,
				       last_ref_heap, last_ref_index, cf,
				       cf >= 0 ? display_sjis0(ain->functions[cf].name) : "?");
				/* [D129] 注册为什么失败：delegate_plusa() 会逐个校验
				 * heap_get_seq(obj) == 创建时的 seq，对象一旦被提前回收
				 * 就静默丢弃整条注册。这里把校验现场打出来。 */
				if (add && add->type == DELEGATE_PAGE) {
					for (int k = 0; k + 2 < add->nr_vars; k += 3)
						NOTICE("[dg+seq] obj=%d fun=%d seq=%d now=%d dst_i=%d",
						       add->values[k].i, add->values[k+1].i,
						       add->values[k+2].i,
						       heap_get_seq(add->values[k].i), dst_i);
				} else {
					NOTICE("[dg+seq] add_i=%d not a delegate page dst_i=%d",
					       add_i, dst_i);
				}
			}
		}
		/* [D119] -1/0 is the "empty delegate" sentinel. AIN compiles
		 * `button.OnClick += lambda` as REF(member) + DG_PLUSA; if the member
		 * was never initialised the registration is silently dropped (the
		 * NetworkConfirmation OK/Cancel lambdas 37115/37116). Lazily create a
		 * delegate page and write its slot back into that member. */
		if (dst_i <= 0 && getenv("XSYS4_DG_LAZY") && add) {
			struct page *fresh = delegate_plusa(NULL, add);
			int new_i = heap_alloc_page(fresh);
			if (last_ref_heap > 0 && (size_t)last_ref_heap < heap_size &&
			    heap[last_ref_heap].page && last_ref_index >= 0 &&
			    last_ref_index < heap[last_ref_heap].page->nr_vars) {
				heap[last_ref_heap].page->values[last_ref_index].i = new_i;
				static int lazy_n;
				if (lazy_n++ < 20)
					NOTICE("[dgl] lazy delegate slot=%d heap=%d member=%d",
					       new_i, last_ref_heap, last_ref_index);
			}
			stack_push(add_i);
			break;
		}
		/* D76: dst_i==0 is the reserved GLOBAL_PAGE, not a delegate. */
		if (dst_i > 0) {
			struct page *merged = delegate_plusa(dst, add);
			heap_set_page(dst_i, merged);
			if (getenv("XSYS4_DGPLUSA_DBG"))
				NOTICE("[dg+after] dst_i=%d nr=%d (add_nr=%d)", dst_i,
				       merged ? merged->nr_vars : -1,
				       add ? add->nr_vars : -1);
		}
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
                /* [D118] remember the creating frame's local page for X_GETENV */
                if (!lambda_parent_slot)
                        lambda_parent_init();
                if (fun > 0 && fun < lambda_parent_n) {
                        int lp = local_page_slot();
                        if (lp > 0 && lp < heap_size && heap[lp].ref > 0 &&
                            lambda_parent_slot[fun] != lp) {
                                lambda_parent_slot[fun] = lp;
                                heap_ref(lp);
                        }
                }
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
		int dg_slots = delegate_argument_slots(dg);
		int dg_page = stack_peek(dg_slots).i;
		/* [L2] XSYS4_DG_PAGE_DBG=1: DG_CALL later sees a VM_STRING slot as
		 * dg_page. Check whether CALLBEGIN already picked the wrong stack
		 * slot here (i.e. delegate_argument_slots() undercounts). */
		static int dgbegin_dbg = -1;
		if (dgbegin_dbg < 0)
			dgbegin_dbg = getenv("XSYS4_DG_PAGE_DBG") ? 1 : 0;
		if (dgbegin_dbg && dg_page > 0 && (size_t)dg_page < heap_size &&
		    heap_index_valid(dg_page) && heap[dg_page].type != VM_PAGE) {
			static int n;
			if (n++ < 60) {
				int t[8] = { 0 }, ty[8];
				for (int k = 0; k < 8 && k < stack_ptr; k++) {
					t[k] = stack_peek(k).i;
					ty[k] = (t[k] > 0 && (size_t)t[k] < heap_size &&
						 heap_index_valid(t[k])) ? heap[t[k]].type : -1;
				}
				NOTICE("[dgbegin] ip=0x%zX fno=%d dg_no=%d slots=%d nargs=%d dg_page=%d(type=%d) sp=%d top=[%d,%d,%d,%d,%d,%d,%d,%d] ty=[%d,%d,%d,%d,%d,%d,%d,%d]",
				       instr_ptr,
				       call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
				       dg_no, dg_slots, dg->nr_arguments,
				       dg_page, heap[dg_page].type, stack_ptr,
				       t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7],
				       ty[0], ty[1], ty[2], ty[3], ty[4], ty[5], ty[6], ty[7]);
			}
		}
		for (int i = 0; i < dg_slots; i++) {
			int pos = (stack_ptr - dg_slots) + i;
			stack[pos-1] = stack[pos];
		}
		stack[stack_ptr-1].i = dg_page;
		stack_push(0);

		// XXX: If the delegate has a return value, we push dummy value(s)
		//      so that DG_CALL can replace them. AIN_IFACE takes two slots.
		for (int rv = delegate_return_slots(dg); rv > 0; rv--)
			stack_push(0);
		dg_drift_push(dg_no);
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

/* --- VM frame pump (see XSYS4_PUMP) -------------------------------------- */
static bool vm_frame_pump_on = false;
static bool vm_frame_pump_checked = false;

static bool vm_frame_pump_enabled(void)
{
	if (!vm_frame_pump_checked) {
		/* 默认开启。XSYS4_NO_PUMP=1 可关掉（对照实验用）。 */
		vm_frame_pump_on = getenv("XSYS4_NO_PUMP") == NULL;
		vm_frame_pump_checked = true;
	}
	return vm_frame_pump_on;
}

void vm_frame_pump(void)
{
	/* The animation clock has to be driven here as well. When AIN spins inside
	 * parts::detail::WaitForClick (or any other pure-AIN wait), it never
	 * reaches PartsEngine.Update, so every flat part stays on frame 0: the
	 * title logo (75 frames, first frame blank) and the dialog fades never
	 * advanced and the screen looked frozen even though the sprites were
	 * rendered every frame. Only the clock is advanced -- not the full
	 * PE_Update(), which would invalidate the prepared sprites (D113). */
	static uint32_t last_tick;
	static bool auto_map_done;
	uint32_t now = SDL_GetTicks();
	/* 这个函数每 4096 条指令被调用一次（一秒可达数千次）。没有时间闸门时，
	 * 输入阶段会给消息队列喂进成百上千条重复的 MOUSE_ON，AIN 侧每帧消费不
	 * 完，点击消息就被淹没。按 ~60 fps 限流。 */
	if (last_tick && now - last_tick < 16)
		return;
	int dt = last_tick ? (int)(now - last_tick) : 16;
	last_tick = now;
	if (dt > 100)
		dt = 100;
	/* [MEM] XSYS4_MEM_STAT=1: every ~2 s print what the VM heap is made of.
	 * RSS alone cannot tell a slot leak (heap array growth) from a page leak
	 * from a string leak; this walks the heap and counts each. Bounded cost:
	 * one pass over the slot array every 2 s. */
	if (getenv("XSYS4_MEM_STAT")) {
		static uint32_t last_mem;
		if (!last_mem || now - last_mem >= 2000) {
			last_mem = now ? now : 1;
			size_t live = 0, pages = 0, strs = 0, page_bytes = 0, str_bytes = 0;
			for (size_t i = 0; i < heap_size; i++) {
				if (heap[i].ref <= 0)
					continue;
				live++;
				if (heap[i].type == VM_PAGE && heap[i].page) {
					pages++;
					page_bytes += sizeof(struct page) +
						      sizeof(union vm_value) *
						      (size_t)heap[i].page->nr_vars;
				} else if (heap[i].type == VM_STRING && heap[i].s) {
					strs++;
					str_bytes += sizeof(struct string) + (size_t)heap[i].s->size + 1;
				}
			}
			/* RSS vs glibc breakdown: if rss grows while malloc_inuse
			 * stays flat, the growth is outside our allocator (GL driver
			 * / llvmpipe buffers, textures). */
			uint64_t rss_b = 0, peak_b = 0;
			if (pal_get()->mem && pal_get()->mem->usage)
				pal_get()->mem->usage(&rss_b, &peak_b);
			long rss_kb = (long)(rss_b / 1024), hwm_kb = (long)(peak_b / 1024);
			{
				extern size_t gfx_texture_count, gfx_texture_pixels;
				extern size_t heap_alloc_count, heap_free_count;
				NOTICE("[mem] textures=%zu tex_pixels_mb=%zu allocs=%zu "
				       "frees=%zu free_ptr=%zu",
				       gfx_texture_count,
				       gfx_texture_pixels * 4 / (1024 * 1024),
				       heap_alloc_count, heap_free_count, heap_free_ptr);
			}
			NOTICE("[mem] up=%us heap_slots=%zu live=%zu pages=%zu page_mb=%zu "
			       "strings=%zu str_mb=%zu rss_mb=%ld peak_mb=%ld "
			       "malloc_inuse_mb=%zu malloc_mmap_mb=%zu",
			       (unsigned)(now / 1000), heap_size, live, pages,
			       page_bytes / (1024 * 1024), strs,
			       str_bytes / (1024 * 1024), rss_kb / 1024, hwm_kb / 1024,
			       (size_t)(mallinfo2().uordblks / (1024 * 1024)),
			       (size_t)(mallinfo2().hblkhd / (1024 * 1024)));
		}
	}

	/* [AUTO] XSYS4_AUTO_MAP=1：启动若干毫秒后直接调用
	 * `Rance10@RunGameChapter1`(fno 22579)，跳过"点标题菜单"这一步。
	 * 理由：xdotool 单次点击命中率实测 ~30%，而一次跑测 3 分钟，
	 * 靠点击复现地图路径既慢又不稳定。延时可用 XSYS4_AUTO_MAP_DELAY 调。 */
	if (!auto_map_done) {
		static uint32_t auto_map_delay;
		static bool auto_map_on, auto_map_init;
		if (!auto_map_init) {
			auto_map_on = getenv("XSYS4_AUTO_MAP") != NULL;
			const char *d = getenv("XSYS4_AUTO_MAP_DELAY");
			auto_map_delay = d && *d ? (uint32_t)atoi(d) : 4000;
			auto_map_init = true;
		}
		if (auto_map_on && now >= auto_map_delay) {
			auto_map_done = true;
			NOTICE("[auto] calling Rance10@RunGameChapter1 (fno 22579) "
			       "after %ums", now);
			vm_call(22579, -1);
			NOTICE("[auto] returned from RunGameChapter1");
		}
	}

	/* [AUTO] XSYS4_AUTO_PARTY=1：地图装载后直接调 `SceneQuestMap@OpenParty`，
	 * 拿到"部队编成"界面。理由同 XSYS4_AUTO_MAP：地图上那排按钮没有命中盒，
	 * 点击不可复现，而编成界面是下一个必须打通的界面。 */
	{
		static bool party_done;
		static uint32_t party_seen_at;
		if (auto_party_want && !party_done && auto_party_page > 0) {
			if (!party_seen_at)
				party_seen_at = now ? now : 1;
			if (now - party_seen_at >= auto_party_delay) {
				party_done = true;
				NOTICE("[auto] calling SceneQuestMap@OpenParty (fno 23595) "
				       "on heap slot %d after %ums",
				       auto_party_page, now - party_seen_at);
				vm_call(23595, auto_party_page);
				NOTICE("[auto] returned from OpenParty");
			}
		}
	}

	parts_advance_animation(dt);

	PE_AdvanceMotionClock(dt);

	/* [D120] 输入阶段也属于帧心跳。只泵 SDL 事件不跑命中测试的话，
	 * AIN 在 WaitForClick 里自旋时 clicked_parts 永远是 0，点击随机丢失。 */
	PE_PumpInput(dt);
	if (scene_is_dirty) {
		scene_render();
		gfx_swap();
		scene_is_dirty = false;
	}
}

/* XSYS4_SP_WATCH=<fno>: log ip/opcode/stack depth for every instruction of one
 * AIN function. Used to locate where a function's operand-stack effect drifts
 * away from "pop nr_args, push return value(s)". */
static int sp_watch_fno = -2;

static void vm_execute(void)
{
	/* [D111e] Stack sampler: XSYS4_STACK_SAMPLE=1 prints the AIN call stack
	 * once per second, so a busy loop can be located without a trace window. */
	static unsigned long long _insn;
	static time_t _last;
	if (sp_watch_fno == -2) {
		const char *w = getenv("XSYS4_SP_WATCH");
		sp_watch_fno = w && *w ? atoi(w) : -1;
	}
	for (;;) {
		uint16_t opcode;
		++_insn;
		/* 帧心跳：在 AIN 指令流里定期提交一帧（XSYS4_NO_PUMP=1 关闭）。
		 *
		 * 有些界面（parts::detail::WaitForClick）在 AIN 层自旋等输入，永远
		 * 回不到 sact_Update()；那条路上既没有 handle_events() 也没有
		 * scene_render()，于是画面停在旧帧、点击也读不到。这里每 4096 条
		 * 指令泵一次事件并提交一帧，让纯 AIN 的等待循环也能被驱动。 */
		if (unlikely((_insn & 0xFFF) == 0) && vm_frame_pump_enabled())
			vm_frame_pump();
		if (((_insn & 0xFFFF) == 0) && getenv("XSYS4_STACK_SAMPLE")) {
			time_t now = time(NULL);
			if (now != _last) {
				_last = now;
				union vm_value g6 = global_get(6);
				fprintf(stderr, "[sample] ip=0x%zX g6=%d g4=%d\n", instr_ptr, g6.i,
					global_get(4).i);
				vm_dump_call_stack(10);
			}
		}
		if (instr_ptr == VM_RETURN)
			return;
		if (unlikely(instr_ptr >= ain->code_size)) {
			VM_ERROR("Illegal instruction pointer: 0x%08lX", instr_ptr);
		}
		opcode = get_opcode(instr_ptr);
		if (sp_watch_fno >= 0 && call_stack_ptr > 0 &&
		    call_stack[call_stack_ptr - 1].fno == sp_watch_fno) {
			static int n;
			if (n++ < 400)
				NOTICE("[spw] ip=0x%zX %-18s sp=%d", instr_ptr,
				       instructions[opcode & ~0xC000].name, stack_ptr);
		}
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
