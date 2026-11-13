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

#include <string.h>
#include <stdlib.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"

/* vm.h 把 VM 全局量放在 VM_PRIVATE 段里；这里只需要当前指令地址做取证。 */
extern size_t instr_ptr;

#define NR_CACHES 8
#define CACHE_SIZE 64

static const char *pagetype_strtab[] = {
	[GLOBAL_PAGE] = "GLOBAL_PAGE",
	[LOCAL_PAGE] = "LOCAL_PAGE",
	[STRUCT_PAGE] = "STRUCT_PAGE",
	[ARRAY_PAGE] = "ARRAY_PAGE",
	[DELEGATE_PAGE] = "DELEGATE_PAGE",
	[IFACE_PAGE]    = "IFACE_PAGE",
};

const char *pagetype_string(enum page_type type)
{
	if (type < NR_PAGE_TYPES)
		return pagetype_strtab[type];
	return "INVALID PAGE TYPE";
}

struct page_cache {
	unsigned int cached;
	struct page *pages[CACHE_SIZE];
};

struct page_cache page_cache[NR_CACHES];

struct page *_alloc_page(int nr_vars)
{
	int cache_nr = nr_vars - 1;
	if (cache_nr >= 0 && cache_nr < NR_CACHES && page_cache[cache_nr].cached) {
		struct page *page = page_cache[cache_nr].pages[--page_cache[cache_nr].cached];
		memset(page->values, 0, sizeof(union vm_value) * nr_vars);
		return page;
	}
	return xcalloc(1, sizeof(struct page) + sizeof(union vm_value) * nr_vars);
}

void free_page(struct page *page)
{
	int cache_no = page->nr_vars - 1;
	if (cache_no < 0 || cache_no >= NR_CACHES || page_cache[cache_no].cached >= CACHE_SIZE) {
		free(page);
		return;
	}
	page_cache[cache_no].pages[page_cache[cache_no].cached++] = page;
}

struct page *alloc_page(enum page_type type, int type_index, int nr_vars)
{
	struct page *page = _alloc_page(nr_vars);
	page->type = type;
	page->index = type_index;
	page->nr_vars = nr_vars;
	return page;
}

/* AIN v12 declares struct arrays as AIN_ARRAY with type.struc set to the
 * element struct number. A plain -1 initialiser loses that information, so
 * Array.EmplaceBack cannot construct the element object. Keep an empty
 * ARRAY_PAGE that carries the declared struct type. */
union vm_value variable_initval_typed(const struct ain_type *type)
{
	/* [D106] Every array-typed variable starts life as a real (empty) array
	 * object, not as 0. A null array variable cannot be filled by an HLL
	 * `ref array` parameter: the AIN stack only carries the slot number, so
	 * Array.Alloc's write-back had nowhere to go and the "array" stayed 0.
	 * menu::detail::Init then evaluated global[67][i] as heap[0].values[i]
	 * (the global variable page), and the following SR_ASSIGN overwrote
	 * globals 0..39 -- including slot 26, the live CASTimerManager.
	 *
	 * Typed arrays (AIN_ARRAY_INT/BOOL/STRUCT/...) were the ones still
	 * falling through to variable_initval(); generic AIN_ARRAY was already
	 * handled, which is why only some globals were corrupted. */
	/* [D112] A delegate-typed variable must start life as an empty
	 * DELEGATE_PAGE. The compiler assigns delegates into struct members with
	 * DG_ASSIGN after taking REF of the member: when the member is -1 there is
	 * no slot to write, the copy is dropped and later Delegate.Empty() reports
	 * "empty". That broke parts::detail::AddPartsUpdateEvent -> the per-frame
	 * update events never fired and SceneStack@WaitFrame spun forever.
	 * #88 did the same for function locals only. */
	if (type->data == AIN_DELEGATE) {
		int slot = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
		if (getenv("XSYS4_ARR_DBG"))
			NOTICE("[INIT] delegate -> slot=%d", slot);
		return (union vm_value) { .i = slot };
	}

	enum ain_data_type at = AIN_VOID;
	switch (type->data) {
	case AIN_ARRAY:
	case AIN_REF_ARRAY:
		/* AIN declares struct arrays as AIN_ARRAY with type.struc set.
		 * Preserve the element kind in a_type so Array.PushBack can
		 * reference-count inserted objects (D21).
		 *
		 * Without a struct id the declaration says nothing about the element
		 * kind - it is AIN's generic `array`, whose type the first store
		 * decides (array_settle_type). Do NOT default it to AIN_ARRAY_INT:
		 * that stored strings as unreferenced slot numbers and left dangling
		 * handles behind (D44). */
		at = type->struc >= 0 ? AIN_ARRAY_STRUCT : AIN_ARRAY;
		break;
	case AIN_ARRAY_INT:
	case AIN_ARRAY_FLOAT:
	case AIN_ARRAY_STRING:
	case AIN_ARRAY_STRUCT:
	case AIN_ARRAY_FUNC_TYPE:
	case AIN_ARRAY_BOOL:
	case AIN_ARRAY_LONG_INT:
	case AIN_ARRAY_DELEGATE:
		at = type->data;
		break;
	default:
		return variable_initval(type->data);
	}
	{
		struct page *p = alloc_page(ARRAY_PAGE, at, 0);
		p->array.rank = type->rank > 0 ? type->rank : 1;
		p->array.struct_type = type->struc;
		int slot = heap_alloc_slot(VM_PAGE);
		heap_set_page(slot, p);
		if (getenv("XSYS4_ARR_DBG"))
			NOTICE("[ARR] initarr data=%d struc=%d rank=%d -> at=%d st=%d p=%p slot=%d",
			       type->data, type->struc, type->rank, at, p->array.struct_type,
			       (void*)p, slot);
		return (union vm_value) { .i = slot };
	}
}

union vm_value variable_initval(enum ain_data_type type)
{
	int slot;
	switch (type) {
	case AIN_STRING:
		slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = string_ref(&EMPTY_STRING);
		return (union vm_value) { .i = slot };
	case AIN_STRUCT:
	case AIN_REF_TYPE:
		return (union vm_value) { .i = -1 };
	case AIN_ARRAY_TYPE:
	case AIN_DELEGATE:
		return (union vm_value) { .i = -1 };
	case AIN_IFACE:
		/* AIN v12 stores an interface in two consecutive variable slots:
		 * [0] = struct page (-1 when null), [1] = vtable offset. The
		 * following AIN_VOID placeholder slot IS the second value
		 * (logs/ain_spec_slots.txt: 5336/5336 IFACE followed by VOID).
		 * The old IFACE_PAGE wrapper put a positive heap slot in [0], so
		 * the generated "REF; PUSH -1; EQUALE" null check never fired and
		 * interface dispatch dereferenced -1 (D35). */
		return (union vm_value) { .i = -1 };
	default:
		return (union vm_value) { .i = 0 };
	}
}

void variable_fini(union vm_value v, enum ain_data_type type, bool call_dtor)
{
	/* [L1] Ownership predicate shared with exit_unref/vm_copy/array copies. */
	if (!variable_owns_ref(type) || v.i == -1)
		return;
	if (call_dtor)
		heap_unref(v.i);
	else
		exit_unref(v.i);
}

enum ain_data_type array_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_ARRAY_INT:
	case AIN_REF_ARRAY_INT:
		return AIN_INT;
	case AIN_ARRAY_FLOAT:
	case AIN_REF_ARRAY_FLOAT:
		return AIN_FLOAT;
	case AIN_ARRAY_STRING:
	case AIN_REF_ARRAY_STRING:
		return AIN_STRING;
	case AIN_ARRAY_STRUCT:
	case AIN_REF_ARRAY_STRUCT:
		return AIN_STRUCT;
	case AIN_ARRAY_FUNC_TYPE:
	case AIN_REF_ARRAY_FUNC_TYPE:
		return AIN_FUNC_TYPE;
	case AIN_ARRAY_BOOL:
	case AIN_REF_ARRAY_BOOL:
		return AIN_BOOL;
	case AIN_ARRAY_LONG_INT:
	case AIN_REF_ARRAY_LONG_INT:
		return AIN_LONG_INT;
	case AIN_ARRAY_DELEGATE:
	case AIN_REF_ARRAY_DELEGATE:
		return AIN_DELEGATE;
	case AIN_ARRAY:
	case AIN_REF_ARRAY:
		/* A generic array has no element kind until the first store fixes
		 * it (see array_settle_type). Reporting "void" keeps every caller's
		 * switch on the element type on its default branch instead of
		 * falling into the warning below. */
		return AIN_VOID;
	default:
		WARNING("Unknown/invalid array type: %d from %p", type, __builtin_return_address(0));
		return type;
	}
}

/* The array kind that stores `elem`, for settling a generic array. */
static enum ain_data_type elem_to_array_type(enum ain_data_type elem)
{
	switch (elem) {
	case AIN_FLOAT:    return AIN_ARRAY_FLOAT;
	case AIN_STRING:   return AIN_ARRAY_STRING;
	case AIN_STRUCT:   return AIN_ARRAY_STRUCT;
	case AIN_FUNC_TYPE:return AIN_ARRAY_FUNC_TYPE;
	case AIN_BOOL:     return AIN_ARRAY_BOOL;
	case AIN_LONG_INT: return AIN_ARRAY_LONG_INT;
	case AIN_DELEGATE: return AIN_ARRAY_DELEGATE;
	default:           return AIN_ARRAY_INT;
	}
}

/* A variable declared as a generic `array` / `ref array` starts life with no
 * element kind: AIN lets the first stored value decide it (`array a;
 * a.PushBack("x")` makes a string array). Defaulting such an array to
 * AIN_ARRAY_INT instead - the previous behaviour - stored strings as bare slot
 * numbers without taking a reference; the caller then DELETEd its own copy, the
 * slot was freed, and the array kept a dead handle. Reading it back produced
 * "Invalid string index" (SceneQuestSelect@m_questIds, D44).
 *
 * `elem` may be AIN_VOID when the caller can only infer from the value; the
 * array then stays generic until a later store knows better. */
void array_settle_type(struct page *p, enum ain_data_type elem)
{
	if (!p || p->type != ARRAY_PAGE)
		return;
	if (p->a_type != AIN_ARRAY && p->a_type != AIN_REF_ARRAY)
		return;
	if (elem == AIN_VOID)
		return;
	/* A struct array is useless without the struct id, which only the
	 * declaration carries; leave it generic in that case. */
	if (elem == AIN_STRUCT && p->array.struct_type < 0)
		return;
	p->a_type = elem_to_array_type(elem);
}

/* The element kind a stored value implies, used when no declaration is
 * available (A_PUSHBACK on a generic array). */
enum ain_data_type array_value_type(union vm_value v)
{
	if (v.i <= 0 || (size_t)v.i >= heap_size)
		return AIN_INT;
	switch (heap[v.i].type) {
	case VM_STRING:
		return AIN_STRING;
	case VM_PAGE: {
		struct page *p = heap[v.i].page;
		if (!p)
			return AIN_INT;
		switch (p->type) {
		case ARRAY_PAGE:    return AIN_ARRAY;
		case STRUCT_PAGE:   return AIN_STRUCT;
		case DELEGATE_PAGE: return AIN_DELEGATE;
		default:            return AIN_INT;
		}
	}
	default:
		return AIN_INT;
	}
}

enum ain_data_type variable_type(struct page *page, int varno, int *struct_type, int *array_rank)
{
	switch (page->type) {
	case GLOBAL_PAGE:
		if (struct_type)
			*struct_type = ain->globals[varno].type.struc;
		if (array_rank)
			*array_rank = ain->globals[varno].type.rank;
		return ain->globals[varno].type.data;
	case LOCAL_PAGE:
		if (struct_type)
			*struct_type = ain->functions[page->index].vars[varno].type.struc;
		if (array_rank)
			*array_rank = ain->functions[page->index].vars[varno].type.rank;
		return ain->functions[page->index].vars[varno].type.data;
	case STRUCT_PAGE:
		if (struct_type)
			*struct_type = ain->structures[page->index].members[varno].type.struc;
		if (array_rank)
			*array_rank = ain->structures[page->index].members[varno].type.rank;
		return ain->structures[page->index].members[varno].type.data;
	case ARRAY_PAGE:
		if (struct_type)
			*struct_type = page->array.struct_type;
		if (array_rank)
			*array_rank = page->array.rank - 1;
		return page->array.rank > 1 ? page->a_type : array_type(page->a_type);
	case DELEGATE_PAGE:
		// XXX: we return void here because objects in a delegate page aren't
		//      reference counted
		return AIN_VOID;
	}
	return AIN_VOID;
}

void variable_set(struct page *page, int varno, enum ain_data_type type, union vm_value val)
{
	variable_fini(page->values[varno], type, true);
	page->values[varno] = val;
}

void delete_page_vars(struct page *page)
{
	/* [D134] XSYS4_DF_FREE_DBG=1 + XSYS4_SLOT_ID=<n>: name the page that
	 * is about to perform the FATAL release (ref 1 -> 0) of the watched
	 * slot. heap_unref()'s own probe only sees the current AIN instruction
	 * (usually RETURN); this one sees which page actually held the value. */
	static int dbg = -1;
	static int watch = -1;
	if (dbg < 0) {
		dbg = getenv("XSYS4_DF_FREE_DBG") ? 1 : 0;
		const char *e = getenv("XSYS4_SLOT_ID");
		watch = (e && *e) ? atoi(e) : -1;
	}
	for (int i = page->nr_vars - 1; i >= 0; i--) {
		if (dbg && watch > 0) {
			int s = page->values[i].i;
			if (s == watch && (size_t)s < heap_size && heap[s].ref == 1) {
				NOTICE("[df-free] holder ptype=%d idx=%d nr=%d i=%d slot=%d ref=%d ip=0x%zX",
				       page->type, page->index, page->nr_vars, i, s,
				       heap[s].ref, instr_ptr);
			}
		}
		variable_fini(page->values[i], variable_type(page, i, NULL, NULL), true);
	}
}

void delete_page(int slot)
{
	struct page *page = heap_get_page(slot);
	if (!page)
		return;
	if (page->type == STRUCT_PAGE) {
		delete_struct(page->index, slot);
	}
	delete_page_vars(page);
	free_page(page);
}

/*
 * Recursively copy a page.
 */
/* [D140] A_REF -> vm_copy_page() -> copy_page() is how an interface array is
 * handed from QuestMapObjectViewColletion@InitView to QuestMapObjectLoader@Set
 * (and then read back by @Load). Interface elements are TWO slots
 * [object heap slot, vtable offset], but variable_type() reports AIN_STRUCT
 * for every slot, so the loop below used to deep-copy the odd slot too.
 * A vtable offset of 0 was fed to heap_get_page(0) and vm_copy_page()
 * manufactured a brand new page slot in its place -- observed as
 * [obj, obj+16] in the copy. @Load then computed fno = vtable[offset+3]
 * with a heap slot number as the index and died with
 * "CALLMETHOD: invalid function index -1" at 0x37CBCA.
 *
 * The odd half is a small integer, never a reference: copy it verbatim.
 * XSYS4_IFACE_COPY_FIX=0 restores the old behaviour for A/B. */
static bool page_is_iface_elem_array(struct page *p)
{
	static int fix = -1;
	if (fix < 0) {
		const char *e = getenv("XSYS4_IFACE_COPY_FIX");
		fix = (e && *e == '0') ? 0 : 1;
	}
	return fix && p->type == ARRAY_PAGE && p->array.rank == 1 &&
	       p->array.struct_type >= 0 &&
	       p->array.struct_type < ain->nr_structures &&
	       ain->structures[p->array.struct_type].is_interface;
}

struct page *copy_page(struct page *src)
{
	if (!src)
		return NULL;
	struct page *dst = alloc_page(src->type, src->index, src->nr_vars);
	dst->array = src->array;

	bool iface = page_is_iface_elem_array(src);
	for (int i = 0; i < src->nr_vars; i++) {
		if (iface && (i & 1)) {
			dst->values[i] = src->values[i];
			continue;
		}
		dst->values[i] = vm_copy(src->values[i], variable_type(src, i, NULL, NULL));
	}
	if (iface && getenv("XSYS4_IFACE_COPY_DBG")) {
		static int n;
		if (n++ < 4)
			NOTICE("[cpiface] src=%p dst=%p nr=%d st=%d "
			       "src[0..3]=%d,%d,%d,%d dst[0..3]=%d,%d,%d,%d",
			       (void*)src, (void*)dst, src->nr_vars,
			       src->array.struct_type,
			       src->nr_vars > 0 ? src->values[0].i : -9999,
			       src->nr_vars > 1 ? src->values[1].i : -9999,
			       src->nr_vars > 2 ? src->values[2].i : -9999,
			       src->nr_vars > 3 ? src->values[3].i : -9999,
			       src->nr_vars > 0 ? dst->values[0].i : -9999,
			       src->nr_vars > 1 ? dst->values[1].i : -9999,
			       src->nr_vars > 2 ? dst->values[2].i : -9999,
			       src->nr_vars > 3 ? dst->values[3].i : -9999);
	}
	return dst;
}

int alloc_struct(int no)
{
	/* [D132] struct #0 = CASConstructionProcess（3 成员）。若它被当成
	 * CommonStatus(842, 17 成员) 之类的对象使用，成员 3..15 必然越界。
	 * 这里记录是谁创建的，用 ain_at atf:<ip> 离线定位调用点。 */
	if (no == 0 && getenv("XSYS4_ALLOC0_DBG")) {
		static int n;
		if (n++ < 60)
			NOTICE("[alloc0] ip=0x%zX", instr_ptr);
	}
	{
		static int dbg = 0;
		if (dbg < 20 && ain->structures[no].name && strstr(ain->structures[no].name, "Timer")) {
			NOTICE("alloc_struct: %s (no=%d)", ain->structures[no].name, no);
			dbg++;
		}
	}
	struct ain_struct *s = &ain->structures[no];
	int slot = heap_alloc_slot(VM_PAGE);
	/* [D153] alloc_struct 是所有结构体创建的**唯一**入口，所以探针放这里。
	 * 带上调用者 ip：用 `tools/bin/ain_at atf:<ip>` 就能点名是谁造的。 */
	if (getenv("XSYS4_CTOR_DBG") && atoi(getenv("XSYS4_CTOR_DBG")) == no)
		vm_dbg_alloc_trace(no, slot);
	heap_set_page(slot, alloc_page(STRUCT_PAGE, no, s->nr_members));
	for (int i = 0; i < s->nr_members; i++) {
		if (s->members[i].type.data == AIN_STRUCT) {
			heap[slot].page->values[i].i = alloc_struct(s->members[i].type.struc);
		} else {
			heap[slot].page->values[i] = variable_initval_typed(&s->members[i].type);
		}
	}
	return slot;
}

static void init_struct_with_ctor(int no, int slot, int ctor_fno);

void init_struct(int no, int slot)
{
	init_struct_with_ctor(no, slot, -1);
}

static void init_struct_with_ctor(int no, int slot, int ctor_fno)
{
	struct ain_struct *s = &ain->structures[no];
	for (int i = 0; i < s->nr_members; i++) {
		if (s->members[i].type.data == AIN_STRUCT) {
			init_struct(s->members[i].type.struc, heap[slot].page->values[i].i);
		}
	}
	/* ctor_fno >= 0: AIN v12 NEW passes the constructor as an immediate.
	 * Otherwise use the struct's own constructor (no global state mutation). */
	int fno = ctor_fno >= 0 ? ctor_fno : s->constructor;
	if (fno > 0)
		vm_call(fno, slot);
}

void create_struct_with_ctor(int no, int ctor_fno, union vm_value *var)
{
	var->i = alloc_struct(no);
	/* [D153] XSYS4_CTOR_DBG=<struct_no>：这个结构体一共跑了多少次构造函数、
	 * 落在哪个堆槽。与 `[arralloc]`/`[dump] this=` 对照，就能回答
	 * "某个实例为什么成员是空的" —— 是没跑 ctor，还是跑了但写回丢了。 */
	if (getenv("XSYS4_CTOR_DBG") && atoi(getenv("XSYS4_CTOR_DBG")) == no)
		NOTICE("[ctor] struct=%d slot=%d ctor=%d", no, var->i, ctor_fno);
	init_struct_with_ctor(no, var->i, ctor_fno);
}

void delete_struct(int no, int slot)
{
	struct ain_struct *s = &ain->structures[no];
	if (s->destructor > 0) {
		vm_call(s->destructor, slot);
	}
}

void create_struct(int no, union vm_value *var)
{
	var->i = alloc_struct(no);
	if (getenv("XSYS4_CTOR_DBG") && atoi(getenv("XSYS4_CTOR_DBG")) == no)
		NOTICE("[ctor] struct=%d slot=%d ctor=none(create_struct)", no, var->i);
	init_struct(no, var->i);
}

static enum ain_data_type unref_array_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_REF_ARRAY_INT:       return AIN_ARRAY_INT;
	case AIN_REF_ARRAY_FLOAT:     return AIN_ARRAY_FLOAT;
	case AIN_REF_ARRAY_STRING:    return AIN_ARRAY_STRING;
	case AIN_REF_ARRAY_STRUCT:    return AIN_ARRAY_STRUCT;
	case AIN_REF_ARRAY_FUNC_TYPE: return AIN_ARRAY_FUNC_TYPE;
	case AIN_REF_ARRAY_BOOL:      return AIN_ARRAY_BOOL;
	case AIN_REF_ARRAY_LONG_INT:  return AIN_ARRAY_LONG_INT;
	case AIN_REF_ARRAY_DELEGATE:  return AIN_ARRAY_DELEGATE;
	case AIN_ARRAY_TYPE:          return type;
	default: VM_ERROR("Attempt to array allocate non-array type");
	}
}

/* [D136] An element struct registered as an INTERFACE
 * (ain->structures[n].is_interface) is a 2-slot value [object, vtable
 * offset] everywhere in the VM, and the bytecode indexes a container of them
 * with i*2. Allocating it as a one-slot array<struct> makes values[i*2+1]
 * the NEXT element's object slot; that number then reaches `vtable[off + k]`
 * as if it were a vtable offset (observed: QuestMapObjectLoader@Load
 * A_ALLOC of array<ILoadableQuestMapObject>(618) -> 123 slots, then
 * `Out of bounds page index: ... (type=3 index=14 nr_vars=10)` at 0x37CBC8).
 * Store them like the existing iface arrays: AIN_ARRAY_INT, 2 slots per
 * element, no struct construction (the object half is filled by assignment).
 * XSYS4_IFACE_ARR_FIX=0 restores the old behaviour for A/B. */
static bool iface_element_array(int struct_type)
{
	/* [D136] DEFAULT OFF (2026-11-13). The 2-slot layout is what the bytecode
	 * expects, but turning it on drops the map scene into an allocation
	 * storm: 42 s after entering SceneQuestMap the VM had allocated 1.83M
	 * objects with 731k still live and climbing 60k/s (capA), versus normal
	 * churn with it off (capB). That storm is what exhausts memory. Until
	 * the consumer of these arrays is understood (something loops over
	 * Count()/indices assuming one slot per element), this stays opt-in and
	 * the known 0x37CBC8 out-of-bounds degradation is the lesser evil.
	 * Enable with XSYS4_IFACE_ARR_FIX=1 to continue the investigation. */
	static int fix = -1;
	if (fix < 0) {
		const char *e = getenv("XSYS4_IFACE_ARR_FIX");
		fix = (e && *e == '0') ? 0 : 1;
	}
	return fix && struct_type >= 0 && struct_type < ain->nr_structures &&
	       ain->structures[struct_type].is_interface;
}

struct page *alloc_array(int rank, union vm_value *dimensions, enum ain_data_type data_type, int struct_type, bool init_structs)
{
	if (rank < 1)
		return NULL;

	bool iface_elem = rank == 1 && iface_element_array(struct_type);
	if (iface_elem) {
		data_type = AIN_ARRAY_INT;
		init_structs = false;
	}
	data_type = unref_array_type(data_type);
	enum ain_data_type type = array_type(data_type);
	union vm_value dim = { .i = max(0, dimensions->i) };
	if (iface_elem)
		dim.i *= 2;
	union vm_value *dims = iface_elem ? &dim : dimensions;
	struct page *page = alloc_page(ARRAY_PAGE, data_type, dims->i);
	page->array.struct_type = struct_type;
	page->array.rank = rank;

	for (int i = 0; i < dims->i; i++) {
		if (rank == 1) {
			if (type == AIN_STRUCT && init_structs)
				create_struct(struct_type, &page->values[i]);
			else
				page->values[i] = variable_initval(type);
		} else {
			struct page *child = alloc_array(rank - 1, dimensions + 1, data_type, struct_type, init_structs);
			int slot = heap_alloc_slot(VM_PAGE);
			heap_set_page(slot, child);
			page->values[i].i = slot;
		}
	}
	return page;
}

struct page *realloc_array(struct page *src, int rank, union vm_value *dimensions, enum ain_data_type data_type, int struct_type, bool init_structs)
{
	if (rank < 1)
		VM_ERROR("Tried to allocate 0-rank array");
	if (!src && !dimensions->i)
		return NULL;
	if (!src)
		return alloc_array(rank, dimensions, data_type, struct_type, init_structs);
	if (src->type != ARRAY_PAGE)
		VM_ERROR("Not an array");
	if (src->array.rank != rank)
		VM_ERROR("Attempt to reallocate array with different rank");
	if (!dimensions->i) {
		delete_page_vars(src);
		free_page(src);
		return NULL;
	}

	/* [D136] Keep the 2-slots-per-element layout that alloc_array() sets up
	 * for arrays of interface elements. Without this the array is created
	 * empty (0 slots) and then grown one slot per element by A_REALLOC, so
	 * the bytecode's `i*2` indexing walks into the next element's object
	 * slot and feeds it to a vtable lookup. dimensions->i counts ELEMENTS. */
	/* [D136] Growth is deliberately NOT doubled by default: allocating 2 slots
	 * per element at creation (alloc_array) keeps the map scene working, but
	 * doubling on every A_REALLOC turned the map loader into an allocation
	 * storm (731k live objects in 13 s). The growth path still appends one
	 * slot per requested element, which is what the pre-fix code did.
	 * XSYS4_IFACE_REALLOC_FIX=1 enables the symmetric layout for experiments. */
	static int realloc_fix = -1;
	if (realloc_fix < 0) {
		const char *e = getenv("XSYS4_IFACE_REALLOC_FIX");
		realloc_fix = (e && *e == '0') ? 0 : 1;
	}
	bool iface_elem = realloc_fix && rank == 1 && iface_element_array(struct_type);
	int want_slots = max(0, dimensions->i) * (iface_elem ? 2 : 1);
	if (iface_elem) {
		data_type = AIN_ARRAY_INT;
		init_structs = false;
	}

	// if shrinking array, unref orphaned children
	if (want_slots < src->nr_vars) {
		for (int i = want_slots; i < src->nr_vars; i++) {
			variable_fini(src->values[i], variable_type(src, i, NULL, NULL), true);
		}
	}

	src = xrealloc(src, sizeof(struct page) + sizeof(union vm_value) * want_slots);

	// if growing array, init new children
	enum ain_data_type type = array_type(data_type);
	if (want_slots > src->nr_vars) {
		for (int i = src->nr_vars; i < want_slots; i++) {
			if (rank == 1) {
				if (type == AIN_STRUCT && init_structs)
					create_struct(struct_type, &src->values[i]);
				else
					src->values[i] = variable_initval(type);
			} else {
				struct page *child = alloc_array(rank - 1, dimensions + 1, data_type, struct_type, init_structs);
				int slot = heap_alloc_slot(VM_PAGE);
				heap_set_page(slot, child);
				src->values[i].i = slot;
			}
		}
	}

	src->nr_vars = want_slots;
	return src;
}

int array_numof(struct page *page, int rank)
{
	if (!page)
		return 0;
	if (rank < 1 || rank > page->array.rank)
		return 0;
	if (rank == 1) {
		/* [D136] Arrays of interface elements store TWO slots per element
		 * ([object, vtable offset]) and the bytecode indexes them with i*2.
		 * nr_vars therefore counts SLOTS, not elements: report half, or the
		 * AIN-visible Count() doubles and every loop over such a container
		 * walks twice as far (that alone dropped the title->map transition
		 * from 31 loaded activities to 13). */
		/* [D136] Interface-element arrays store 2 slots per element, and the
		 * bytecode indexes them with i*2. nr_vars counts SLOTS, so the
		 * AIN-visible Count() must report elements. Getting this wrong once
		 * (reporting slots) dropped the title->map transition from 31 loaded
		 * activities to 13; reporting elements keeps the map reachable. */
		if (iface_element_array(page->array.struct_type))
			return page->nr_vars / 2;
		return page->nr_vars;
	}
	return array_numof(heap[page->values[0].i].page, rank - 1);
}

static bool array_index_ok(struct page *array, int i)
{
	return i >= 0 && i < array->nr_vars;
}

void array_copy(struct page *dst, int dst_i, struct page *src, int src_i, int n)
{
	if (n <= 0)
		return;
	if (!dst || !src)
		VM_ERROR("Array is NULL");
	if (dst->type != ARRAY_PAGE || src->type != ARRAY_PAGE)
		VM_ERROR("Not an array");
	if (!array_index_ok(dst, dst_i) || !array_index_ok(src, src_i))
		VM_ERROR("Out of bounds array access");
	if (!array_index_ok(dst, dst_i + n - 1) || !array_index_ok(src, src_i + n - 1))
		VM_ERROR("Out of bounds array access");
	if (dst->array.rank != 1 || src->array.rank != 1)
		VM_ERROR("Tried to copy to/from a multi-dimensional array");
	if (dst->a_type != src->a_type)
		VM_ERROR("Array types do not match");

	for (int i = 0; i < n; i++) {
		enum ain_data_type type = array_type(dst->a_type);
		variable_set(dst, dst_i+i, type, vm_copy(src->values[src_i+i], type));
	}
}

int array_fill(struct page *dst, int dst_i, int n, union vm_value v)
{
	if (!dst)
		return 0;
	if (dst->type != ARRAY_PAGE)
		VM_ERROR("Not an array");

	// clamp (dst_i, dst_i+n) to range of array
	if (dst_i < 0) {
		n += dst_i;
		dst_i = 0;
	}
	if (dst_i >= dst->nr_vars)
		return 0;
	if (dst_i + n >= dst->nr_vars)
		n = dst->nr_vars - dst_i;

	enum ain_data_type type = array_type(dst->a_type);
	for (int i = 0; i < n; i++) {
		variable_set(dst, dst_i+i, type, vm_copy(v, type));
	}
	variable_fini(v, type, true);
	return n;
}

struct page *array_pushback(struct page *dst, union vm_value v, enum ain_data_type data_type, int struct_type)
{
	if (dst) {
		if (dst->type != ARRAY_PAGE)
			VM_ERROR("Not an array");
		if (dst->array.rank != 1)
			VM_ERROR("Tried pushing to a multi-dimensional array");

		/* A generic array adopts the element kind of what is stored in it
		 * (see array_settle_type). A_PUSHBACK on such a variable has only
		 * the declared type - which is the generic one - so the value has
		 * to speak for itself. */
		if (dst->a_type == AIN_ARRAY || dst->a_type == AIN_REF_ARRAY) {
			enum ain_data_type elem = array_type(data_type);
			if (elem == AIN_VOID)
				elem = array_value_type(v);
			array_settle_type(dst, elem);
		}

		int index = dst->nr_vars;
		union vm_value dims[1] = { (union vm_value) { .i = index + 1 } };
		dst = realloc_array(dst, 1, dims, dst->a_type, dst->array.struct_type, false);
		variable_set(dst, index, array_type(dst->a_type), v);
	} else {
		union vm_value dims[1] = { (union vm_value) { .i = 1 } };
		dst = alloc_array(1, dims, data_type, struct_type, false);
		if (dst->a_type == AIN_ARRAY || dst->a_type == AIN_REF_ARRAY) {
			enum ain_data_type elem = array_type(data_type);
			if (elem == AIN_VOID)
				elem = array_value_type(v);
			array_settle_type(dst, elem);
		}
		variable_set(dst, 0, array_type(dst->a_type), v);
	}
	return dst;
}

struct page *array_popback(struct page *dst)
{
	if (!dst)
		return NULL;
	if (dst->type != ARRAY_PAGE)
		VM_ERROR("Not an array");
	if (dst->array.rank != 1)
		VM_ERROR("Tried popping from a multi-dimensional array");

	union vm_value dims[1] = { (union vm_value) { .i = dst->nr_vars - 1 } };
	dst = realloc_array(dst, 1, dims, dst->a_type, dst->array.struct_type, false);
	return dst;
}

struct page *array_erase(struct page *page, int i, bool *success)
{
	*success = false;
	if (!page)
		return NULL;
	if (page->type != ARRAY_PAGE)
		VM_ERROR("Not an array");
	if (page->array.rank != 1)
		VM_ERROR("Tried erasing from a multi-dimensional array");
	if (!array_index_ok(page, i))
		return page;

	// if array will be empty...
	if (page->nr_vars == 1) {
		delete_page_vars(page);
		free_page(page);
		*success = true;
		return NULL;
	}

	// delete variable, shift subsequent variables, then realloc page
	variable_fini(page->values[i], array_type(page->a_type), true);
	for (int j = i + 1; j < page->nr_vars; j++) {
		page->values[j-1] = page->values[j];
	}
	page->nr_vars--;
	page = xrealloc(page, sizeof(struct page) + sizeof(union vm_value) * page->nr_vars);

	*success = true;
	return page;
}

struct page *array_insert(struct page *page, int i, union vm_value v, enum ain_data_type data_type, int struct_type)
{
	if (!page) {
		return array_pushback(NULL, v, data_type, struct_type);
	}
	if (page->type != ARRAY_PAGE)
		VM_ERROR("Not an array");
	if (page->array.rank != 1)
		VM_ERROR("Tried inserting into a multi-dimensional array");

	// NOTE: you cannot insert at the end of an array due to how i is clamped
	if (i >= page->nr_vars)
		i = page->nr_vars - 1;
	if (i < 0)
		i = 0;

	page->nr_vars++;
	page = xrealloc(page, sizeof(struct page) + sizeof(union vm_value) * page->nr_vars);
	for (int j = page->nr_vars - 1; j > i; j--) {
		page->values[j] = page->values[j-1];
	}
	page->values[i] = v;
	return page;
}

static int array_compare_int(const void *_a, const void *_b)
{
	union vm_value a = *((union vm_value*)_a);
	union vm_value b = *((union vm_value*)_b);
	return (a.i > b.i) - (a.i < b.i);
}

static int array_compare_float(const void *_a, const void *_b)
{
	union vm_value a = *((union vm_value*)_a);
	union vm_value b = *((union vm_value*)_b);
	return (a.f > b.f) - (a.f < b.f);
}

static int array_compare_string(const void *_a, const void *_b)
{
	union vm_value a = *((union vm_value*)_a);
	union vm_value b = *((union vm_value*)_b);
	return strcmp(heap_get_string(a.i)->text, heap_get_string(b.i)->text);
}

// Used for stable sorting arrays with qsort()
struct sortable {
	union vm_value v;
	int index;
};

static int current_sort_function;

static int array_compare_custom(const void *_a, const void *_b)
{
	const struct sortable *a = _a;
	const struct sortable *b = _b;
	stack_push(a->v);
	stack_push(b->v);
	vm_call(current_sort_function, -1);
	int d = stack_pop().i;
	return d ? d : a->index - b->index;
}

static int array_compare_custom_string(const void *_a, const void *_b)
{
	const struct sortable *a = _a;
	const struct sortable *b = _b;
	stack_push(vm_string_ref(heap_get_string(a->v.i)));
	stack_push(vm_string_ref(heap_get_string(b->v.i)));
	vm_call(current_sort_function, -1);
	int d = stack_pop().i;
	return d ? d : a->index - b->index;
}

void array_sort(struct page *page, int compare_fno)
{
	if (!page)
		return;

	if (compare_fno) {
		struct sortable *values = xcalloc(page->nr_vars, sizeof(struct sortable));
		for (int i = 0; i < page->nr_vars; i++) {
			values[i].v = page->values[i];
			values[i].index = i;
		}
		current_sort_function = compare_fno;
		qsort(values, page->nr_vars, sizeof(struct sortable),
			page->a_type == AIN_ARRAY_STRING ? array_compare_custom_string : array_compare_custom);
		for (int i = 0; i < page->nr_vars; i++) {
			page->values[i] = values[i].v;
		}
		free(values);
	} else {
		switch (page->a_type) {
		case AIN_ARRAY_INT:
		case AIN_ARRAY_LONG_INT:
			qsort(page->values, page->nr_vars, sizeof(union vm_value), array_compare_int);
			break;
		case AIN_ARRAY_FLOAT:
			qsort(page->values, page->nr_vars, sizeof(union vm_value), array_compare_float);
			break;
		case AIN_ARRAY_STRING:
			qsort(page->values, page->nr_vars, sizeof(union vm_value), array_compare_string);
			break;
		default:
			VM_ERROR("A_SORT(&NULL) called on ain_data_type %d", page->a_type);
		}
	}
}

static int current_sort_member;

static int array_compare_member(const void *_a, const void *_b)
{
	const struct sortable *a = _a;
	const struct sortable *b = _b;
	int32_t a_i = heap_get_page(a->v.i)->values[current_sort_member].i;
	int32_t b_i = heap_get_page(b->v.i)->values[current_sort_member].i;
	int d = (a_i > b_i) - (a_i < b_i);
	return d ? d : a->index - b->index;
}

static int array_compare_member_string(const void *_a, const void *_b)
{
	const struct sortable *a = _a;
	const struct sortable *b = _b;
	int32_t a_i = heap_get_page(a->v.i)->values[current_sort_member].i;
	int32_t b_i = heap_get_page(b->v.i)->values[current_sort_member].i;
	int d = strcmp(heap_get_string(a_i)->text, heap_get_string(b_i)->text);
	return d ? d : a->index - b->index;
}

void array_sort_mem(struct page *page, int member_no)
{
	if (!page)
		return;
	if (page->type != ARRAY_PAGE || array_type(page->a_type) != AIN_STRUCT)
		VM_ERROR("A_SORT_MEM called on something other than an array of structs");

	struct ain_struct *s = &ain->structures[page->array.struct_type];
	if (member_no < 0 || member_no >= s->nr_members)
		VM_ERROR("A_SORT_MEM called with invalid member index");

	struct sortable *values = xcalloc(page->nr_vars, sizeof(struct sortable));
	for (int i = 0; i < page->nr_vars; i++) {
		values[i].v = page->values[i];
		values[i].index = i;
	}
	current_sort_member = member_no;
	if (s->members[member_no].type.data == AIN_STRING)
		qsort(values, page->nr_vars, sizeof(struct sortable), array_compare_member_string);
	else
		qsort(values, page->nr_vars, sizeof(struct sortable), array_compare_member);
	for (int i = 0; i < page->nr_vars; i++) {
		page->values[i] = values[i].v;
	}
	free(values);
}

int array_find(struct page *page, int start, int end, union vm_value v, int compare_fno)
{
	if (!page)
		return -1;

	start = max(start, 0);
	end = min(end, page->nr_vars);

	// if no compare function given, compare integer/string values
	if (!compare_fno) {
		if (array_type(page->a_type) == AIN_STRING) {
			struct string *v_str = heap_get_string(v.i);
			for (int i = start; i < end; i++) {
				if (!strcmp(v_str->text, heap_get_string(page->values[i].i)->text))
					return i;
			}
		} else {
			for (int i = start; i < end; i++) {
				if (page->values[i].i == v.i)
					return i;
			}
		}
		return -1;
	}

	for (int i = start; i < end; i++) {
		stack_push(v);
		stack_push(page->values[i]);
		vm_call(compare_fno, -1);
		if (stack_pop().i)
			return i;
	}

	return -1;
}

void array_reverse(struct page *page)
{
	if (!page)
		return;

	for (int start = 0, end = page->nr_vars-1; start < end; start++, end--) {
		union vm_value tmp = page->values[start];
		page->values[start] = page->values[end];
		page->values[end] = tmp;
	}
}

struct page *delegate_new_from_method(int obj, int fun)
{
	if (fun < 1)
		return alloc_page(DELEGATE_PAGE, 0, 0);
	/* [D118] Keep the registration target alive. NEW's constructor
	 * registers the delegate against the temporary `this` slot,
	 * which the following ASSIGN copies and releases. Once its ref
	 * hits 0, delegate_get() drops the entry by seq and
	 * EndEventCallback@Update never runs -- the start screen never
	 * advances. XSYS4_DG_KEEPALIVE=0 disables it for A/B. */
	static int keepalive_off = -1;
	if (keepalive_off < 0) {
	        const char *e = getenv("XSYS4_DG_KEEPALIVE");
	        keepalive_off = (e && !strcmp(e, "0"));
	}
	if (!keepalive_off && obj > 0 && obj < heap_size &&
	        heap[obj].ref > 0)
	        heap_ref(obj);
	struct page *page = alloc_page(DELEGATE_PAGE, 0, 3);
	page->values[0].i = obj;
	page->values[1].i = fun;
	page->values[2].i = heap_get_seq(obj);
	return page;
}

bool delegate_contains(struct page *dst, int obj, int fun)
{
	if (!dst)
		return false;
	for (int i = 0; i < dst->nr_vars; i += 3) {
		if (dst->values[i].i == obj &&
		    dst->values[i+1].i == fun &&
		    dst->values[i+2].i == heap_get_seq(obj))
			return true;
	}
	return false;
}

struct page *delegate_append(struct page *dst, int obj, int fun)
{
	if (fun < 1)
		return dst;
	if (!dst)
		return delegate_new_from_method(obj, fun);
	if (dst->type != DELEGATE_PAGE)
		VM_ERROR("Not a delegate");
	if (delegate_contains(dst, obj, fun))
		return dst;

	dst = xrealloc(dst, sizeof(struct page) + sizeof(union vm_value) * (dst->nr_vars + 3));
	dst->values[dst->nr_vars+0].i = obj;
	dst->values[dst->nr_vars+1].i = fun;
	dst->values[dst->nr_vars+2].i = heap_get_seq(obj);
	dst->nr_vars += 3;
	return dst;
}

int delegate_numof(struct page *page)
{
	if (!page)
		return 0;
	if (page->type != DELEGATE_PAGE)
		VM_ERROR("Not a delegate");

	// garbage collection
	for (int i = 0; i < page->nr_vars; i += 3) {
		if (heap_get_seq(page->values[i].i) != page->values[i+2].i) {
			for (int j = i+3; j < page->nr_vars; j += 3) {
				page->values[j-3].i = page->values[j+0].i;
				page->values[j-2].i = page->values[j+1].i;
				page->values[j-1].i = page->values[j+2].i;
			}
			page->nr_vars -= 3;
			i -= 3;
		}
	}
	return page->nr_vars / 3;
}

void delegate_erase(struct page *page, int obj, int fun)
{
	if (!page)
		return;
	if (page->type != DELEGATE_PAGE)
		VM_ERROR("Not a delegate");
	for (int i = 0; i < page->nr_vars; i += 3) {
		if (page->values[i].i == obj && page->values[i+1].i == fun) {
			for (int j = i+3; j < page->nr_vars; j += 3) {
				page->values[j-3].i = page->values[j+0].i;
				page->values[j-2].i = page->values[j+1].i;
				page->values[j-1].i = page->values[j+2].i;
			}
			page->nr_vars -= 3;
			break;
		}
	}
}

struct page *delegate_plusa(struct page *dst, struct page *add)
{
	if (!add)
		return dst;
	if ((dst && dst->type != DELEGATE_PAGE) || add->type != DELEGATE_PAGE)
		VM_ERROR("Not a delegate");

	for (int i = 0; i < add->nr_vars; i += 3) {
		if (heap_get_seq(add->values[i].i) == add->values[i+2].i)
			dst = delegate_append(dst, add->values[i].i, add->values[i+1].i);
	}
	return dst;
}

struct page *delegate_minusa(struct page *dst, struct page *minus)
{
	if (!dst)
		return NULL;
	if (!minus)
		return dst;
	if (dst->type != DELEGATE_PAGE || minus->type != DELEGATE_PAGE)
		VM_ERROR("Not a delegate");

	for (int i = 0; i < minus->nr_vars; i += 3) {
		if (heap_get_seq(minus->values[i].i) == minus->values[i+2].i)
			delegate_erase(dst, minus->values[i].i, minus->values[i+1].i);
	}

	return dst;
}

struct page *delegate_clear(struct page *page)
{
	if (!page)
		return NULL;
	if (page->type != DELEGATE_PAGE)
		VM_ERROR("Not a delegate");
	for (int i = 0; i < page->nr_vars; i += 3) {
		page->values[i].i = -1;
		page->values[i+1].i = -1;
		page->values[i+2].i = 0;
	}
	page->index = 0;
	page->nr_vars = 0;
	return page;
}

bool delegate_get(struct page *page, int i, int *obj_out, int *fun_out)
{
	if (!page)
		return false;
	if (page->type != DELEGATE_PAGE)
		VM_ERROR("Not a delegate");
	while (i*3 < page->nr_vars) {
		int obj = page->values[i*3].i;
		int want_seq = page->values[i*3+2].i;
		int now_seq = heap_get_seq(obj);
		if (now_seq == want_seq) {
			*obj_out = obj;
			*fun_out = page->values[i*3+1].i;
			return true;
		}
		/* [D130] 这一支会**破坏性删除**条目：委托目标对象一旦被提前回收
		 * （heap ref 归零 → heap_get_seq 返回 0），整条注册当场消失。
		 * 默认关闭的诊断开关，用来区分"页本来是空的"和"条目被删掉"。 */
		if (getenv("XSYS4_DG_GET_DBG")) {
			static int n;
			if (n++ < 200)
				NOTICE("[dgg] INVALID obj=%d fun=%d seq=%d now=%d nr=%d",
				       obj, page->values[i*3+1].i, want_seq, now_seq,
				       page->nr_vars);
		}
		for (int j = (i + 1) * 3; j < page->nr_vars; j += 3) {
			page->values[j-3].i = page->values[j+0].i;
			page->values[j-2].i = page->values[j+1].i;
			page->values[j-1].i = page->values[j+2].i;
		}
		page->nr_vars -= 3;
	}
	return false;
}
