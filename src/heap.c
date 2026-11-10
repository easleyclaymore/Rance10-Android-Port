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
#include <assert.h>
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

#define INITIAL_HEAP_SIZE  4096
#define HEAP_ALLOC_STEP    4096

struct vm_pointer *heap = NULL;
size_t heap_size = 0;
uint32_t heap_next_seq;

// Heap free list
// This is a list of unused indices into the 'heap' array.
int32_t *heap_free_stack = NULL;
size_t heap_free_ptr = 0;

static const char *vm_ptrtype_strtab[] = {
	[VM_PAGE] = "VM_PAGE",
	[VM_STRING] = "VM_STRING",
};

static const char *vm_ptrtype_string(enum vm_pointer_type type) {
	if (type < NR_VM_POINTER_TYPES)
		return vm_ptrtype_strtab[type];
	return "INVALID POINTER TYPE";
}

void heap_grow(size_t new_size)
{
	assert(new_size > heap_size);
	heap = xrealloc(heap, sizeof(struct vm_pointer) * new_size);
	heap_free_stack = xrealloc(heap_free_stack, sizeof(int32_t) * new_size);
	for (size_t i = heap_size; i < new_size; i++) {
		heap[i].ref = 0;
		/* [FIX D100] xrealloc does not zero the new region, so heap[i].seq was
		 * uninitialised garbage. heap_alloc_slot()'s D83 guard skips any slot
		 * whose seq != 0, so every freshly-grown slot could be misread as "still
		 * live", sending heap_free_ptr racing past the array and corrupting the
		 * heap -- the observed calloc() SEGV during GlobalGameTimer construction. */
		heap[i].seq = 0;
		heap[i].page = NULL;
		heap[i].s = NULL;
		heap[i].type = 0;
		heap_free_stack[i] = i;
	}
	heap_size = new_size;
}

void heap_init(void)
{
	if (!heap) {
		heap_size = INITIAL_HEAP_SIZE;
		heap = xcalloc(1, INITIAL_HEAP_SIZE * sizeof(struct vm_pointer));
		heap_free_stack = xmalloc(INITIAL_HEAP_SIZE * sizeof(int32_t));
	} else {
		memset(heap, 0, heap_size * sizeof(struct vm_pointer*));
	}

	for (size_t i = 0; i < heap_size; i++) {
		heap_free_stack[i] = i;
	}
	heap_free_ptr = 1; // global page at index 0
	heap_next_seq = 1;
}

int heap_watch_slot(void);

int32_t heap_alloc_slot(enum vm_pointer_type type)
{
	if (heap_free_ptr >= heap_size) {
		heap_grow(heap_size+HEAP_ALLOC_STEP);
	}

	int32_t slot = heap_free_stack[heap_free_ptr++];
	if (slot > 0 && slot == heap_watch_slot())
		NOTICE("[SLOT] alloc slot=%d type=%d ref=%d ip=0x%zX fno=%d",
		       slot, type, heap[slot].ref, instr_ptr, hll_current_fno());
	if (getenv("XSYS4_ARR_DBG") && slot == 400)
		NOTICE("[ARR] slot400 alloc type=%d", type);
	/* Slot 0 is the global variable page (PUSHGLOBALPAGE pushes literal 0).
	 * Never recycle it for ordinary allocations, otherwise heap[0].page is
	 * overwritten by a struct/array and global reads go wild (D54).
	 * The D83 workaround that also skipped slots with seq != 0 was removed:
	 * it was compensating for the unbalanced ownership that D104/D106 fixed
	 * (array-typed variables starting as 0, so Array.Alloc lost its result and
	 * the next index expression aliased the global page). Verified stable for
	 * 25s with the skip disabled before removing it. */
	while (slot == 0) {
		if (heap_free_ptr >= heap_size)
			heap_grow(heap_size + HEAP_ALLOC_STEP);
		slot = heap_free_stack[heap_free_ptr++];
	}
	/* A recycled slot must not retain a dangling pointer from its previous
	 * lifetime: callers that only allocate a slot (without heap_set_page)
	 * would otherwise observe a stale page and corrupt object identity. */
	heap[slot].page = NULL;
	heap[slot].s = NULL;
	heap[slot].ref = 1;
	heap[slot].seq = heap_next_seq++;
	heap[slot].type = type;
#ifdef DEBUG_HEAP
	heap[slot].alloc_addr = instr_ptr;
	memset(heap[slot].ref_addr, 0, sizeof(heap[slot].ref_addr));
	heap[slot].ref_nr = 0;
	memset(heap[slot].deref_addr, 0, sizeof(heap[slot].deref_addr));
	heap[slot].deref_nr = 0;
	heap[slot].free_addr = 0;
#endif
	return slot;
}

static void heap_free_slot(int32_t slot)
{
	/* [HACK-POC D95] Do NOT recycle the slot.
	 *
	 * Rance10's Parts/Asra layers keep holding stale slot numbers after an
	 * object is released (the slot is then handed out again and its page type
	 * changes under the holder). That produced a cascade of symptoms: a LOCAL
	 * page handed to CPartsFunctionSet@Init (3937), a DELEGATE page used as
	 * `this` (heap[1], 1/8 out of bounds), CASTimerManager::handles collapsing
	 * to nr_vars=0. All of them are "recycled slot still referenced".
	 *
	 * Leaking the slot removes the entire class: allocation only ever grows.
	 * Cost is a few MB over a session; the real fix is to find the unbalanced
	 * unref and restore recycling. */
	heap[slot].seq = 0;
	/* Keep the (now released) payload pointer so stale holders read a dead but
	 * mapped page instead of a NULL/aliased one. */
	(void)slot;
}

static void heap_double_free(int32_t slot)
{
#ifdef DEBUG_HEAP
		WARNING("double free of slot %d (%s)\nOriginally allocated at %X\nOriginally freed at %X",
			 slot, vm_ptrtype_string(heap[slot].type),
			 heap[slot].alloc_addr, heap[slot].free_addr);
#else
		WARNING("double free of slot %d (%s)", slot, vm_ptrtype_string(heap[slot].type));
#endif
}

/* [DBG] XSYS4_SLOT_ID=<n> traces one heap slot end to end (alloc / set_page /
 * ref / unref). Used to locate unbalanced unrefs without flooding the log. */
int heap_watch_slot(void)
{
	static int w = -2;
	if (w == -2) {
		const char *e = getenv("XSYS4_SLOT_ID");
		w = (e && *e) ? atoi(e) : -1;
	}
	return w;
}

void heap_ref(int32_t slot)
{
	if (slot > 0 && slot == heap_watch_slot())
		NOTICE("[SLOT] ref slot=%d type=%d ref %d->%d ip=0x%zX fno=%d page=%p nr=%d",
		       slot, heap[slot].type, heap[slot].ref, heap[slot].ref + 1,
		       instr_ptr, hll_current_fno(),
		       heap[slot].type == VM_PAGE ? (void*)heap[slot].page : NULL,
		       (heap[slot].type == VM_PAGE && heap[slot].page) ? heap[slot].page->nr_vars : -1);
	if (slot == -1)
		return;
	heap[slot].ref++;
#ifdef DEBUG_HEAP
	heap[slot].ref_addr[heap[slot].ref_nr++ % 16] = instr_ptr;
#endif
}

void heap_unref(int slot)
{
	if (slot > 0 && slot == heap_watch_slot())
		NOTICE("[SLOT] unref slot=%d type=%d ref=%d ip=0x%zX fno=%d page=%p nr=%d",
		       slot, heap[slot].type, heap[slot].ref, instr_ptr, hll_current_fno(),
		       heap[slot].type == VM_PAGE ? (void*)heap[slot].page : NULL,
		       (heap[slot].type == VM_PAGE && heap[slot].page) ? heap[slot].page->nr_vars : -1);
	/* slot 0 是保留的无效槽；若某个 page 成员的值是 0（未初始化的引用），
	 * 这里会 unref 到 slot 0 并触发 delete_page(0) -> 遍历成员 -> 又是 0，
	 * 形成无限递归（gdb backtrace 已确认）。必须挡掉。 */
	if (slot <= 0)
		return;
	if (getenv("XSYS4_ARR_DBG") && slot == 400)
		NOTICE("[ARR] slot400 unref type=%d ref=%d page=%p", heap[slot].type, heap[slot].ref, (void*)heap[slot].page);
	if (slot == 255) {
		static int n = 0;
		if (n < 8) {
			NOTICE("heap_unref(255): ref=%d -> %d", heap[slot].ref, heap[slot].ref - 1);
			n++;
		}
	}
	/* D79: repeated unref of an already-released slot (e.g. two local
	 * variables sharing one VM_STRING in CollectionSaveData::Save).
	 * exit_unref() is already idempotent; make heap_unref match. */
	if (unlikely(heap[slot].ref <= 0)) {
		heap_double_free(slot);
		return;
	}
	if (heap[slot].ref > 1) {
#ifdef DEBUG_HEAP
		heap[slot].deref_addr[heap[slot].deref_nr++ % 16] = instr_ptr;
#endif
		heap[slot].ref--;
		return;
	}
#ifdef DEBUG_HEAP
	heap[slot].free_addr = instr_ptr;
#endif
	switch (heap[slot].type) {
	case VM_PAGE:
		if (heap[slot].page) {
			if (getenv("XSYS4_ARR_DBG") && heap[slot].page->type == ARRAY_PAGE)
				NOTICE("[ARR] free slot=%d p=%p nr=%d at=%d st=%d",
				       slot, (void*)heap[slot].page, heap[slot].page->nr_vars,
				       heap[slot].page->a_type, heap[slot].page->array.struct_type);
			delete_page(slot);
		}
		break;
	case VM_STRING:
		free_string(heap[slot].s);
		break;
	}
	heap[slot].ref = 0;
	/* Ownership of the payload has been released above; keep the slot
	 * pointer-free so any use-after-free fails loudly instead of silently
	 * reusing the old object. */
	heap[slot].page = NULL;
	heap[slot].s = NULL;
	heap_free_slot(slot);
}

// XXX: special version of heap_unref which avoids calling destructors
void exit_unref(int slot)
{
	if (slot < 0 || (size_t)slot >= heap_size) {
		WARNING("out of bounds heap index: %d", slot);
		return;
	}
	if (getenv("XSYS4_ARR_DBG") && slot == 400)
		NOTICE("[ARR] slot400 exitunref type=%d ref=%d page=%p", heap[slot].type, heap[slot].ref, (void*)heap[slot].page);
	if (heap[slot].ref <= 0) {
		heap_double_free(slot);
		return;
	}
	if (heap[slot].ref > 1) {
#ifdef DEBUG_HEAP
		heap[slot].deref_addr[heap[slot].deref_nr++ % 16] = 0xDEADC0DE;
#endif
		heap[slot].ref--;
		return;
	}
	switch (heap[slot].type) {
	case VM_PAGE:
		if (heap[slot].page) {
			struct page *page = heap[slot].page;
			if (getenv("XSYS4_ARR_DBG") && page->type == ARRAY_PAGE)
				NOTICE("[ARR] exitfree slot=%d p=%p at=%d nr=%d st=%d",
				       slot, (void*)page, page->a_type, page->nr_vars,
				       page->array.struct_type);
			for (int i = 0; i < page->nr_vars; i++) {
				switch (variable_type(page, i, NULL, NULL)) {
				case AIN_STRING:
				case AIN_STRUCT:
				case AIN_DELEGATE:
				case AIN_ARRAY_TYPE:
				case AIN_REF_TYPE:
					if (page->values[i].i == -1)
						break;
					exit_unref(page->values[i].i);
					break;
				default:
					break;
				}
			}
			free_page(page);
		}
		break;
	case VM_STRING:
		free_string(heap[slot].s);
		break;
	}
	heap[slot].ref = 0;
	heap_free_slot(slot);
}

uint32_t heap_get_seq(int slot)
{
	return heap_index_valid(slot) ? heap[slot].seq : 0;
}

bool heap_index_valid(int index)
{
	return index >= 0 && (size_t)index < heap_size && heap[index].ref > 0;
}

bool page_index_valid(int index)
{
	return heap_index_valid(index) && heap[index].type == VM_PAGE;
}

bool string_index_valid(int index)
{
	return heap_index_valid(index) && heap[index].type == VM_STRING;
}

struct page *heap_get_page(int index)
{
	/* [HACK-POC D96] Tolerate a dead slot instead of aborting: the Asra layer
	 * still references delegate pages after release (DG_PLUSA from
	 * parts::AddErasingLayerEvent / CModeMark@Init). Callers already handle
	 * NULL. Real fix: keep the delegate alive until the last holder drops it. */
	if (unlikely(!page_index_valid(index))) {
		WARNING("Invalid page index: %d (type=%d)", index,
			heap_index_valid(index) ? heap[index].type : -1);
		return NULL;
	}
	return heap[index].page;
}

struct string *heap_get_string(int index)
{
	if (unlikely(!string_index_valid(index)))
		VM_ERROR("Invalid string index: %d", index);
	return heap[index].s;
}

struct page *heap_get_delegate_page(int index)
{
	/* AIN v12 uses -1 as the empty-delegate sentinel. delegate_append()
	 * already treats a NULL destination as "create lazily", so map the
	 * sentinel here instead of indexing heap[-1]. */
	if (index < 0)
		return NULL;
	struct page *page = heap_get_page(index);
	/* Rance10 passes not-yet-materialised delegate slots (e.g. 7) to DG_*
	 * instructions. Treat them exactly like the existing index<0 empty
	 * sentinel instead of aborting (D49). */
	if (unlikely(page && page->type != DELEGATE_PAGE))
		return NULL;
	return page;
}

void heap_set_page(int slot, struct page *page)
{
	if (slot < 0)
		return; /* empty-delegate sentinel: nothing to write back yet */
	/* D76: slot 0 holds the GLOBAL_PAGE (PUSHGLOBALPAGE pushes literal 0).
	 * Only the initial GLOBAL_PAGE allocation may write it; any other
	 * page type would clobber all global variable reads. */
	if (slot == 0 && page && page->type != GLOBAL_PAGE) {
		WARNING("heap_set_page(0): rejected non-global page type=%d", page->type);
		return;
	}
#ifdef DEBUG_HEAP
	if (unlikely(!page_index_valid(slot)))
		VM_ERROR("Invalid page index: %d", index);
#endif
	if (slot > 0 && slot == heap_watch_slot())
		NOTICE("[SLOT] set_page slot=%d old=%p new=%p type=%d ref=%d ip=0x%zX fno=%d",
		       slot, (void*)heap[slot].page, (void*)page,
		       page ? page->type : -1, heap[slot].ref, instr_ptr, hll_current_fno());
	heap[slot].page = page;
}

void heap_string_assign(int slot, struct string *string)
{
	/* Slot 0 is the reserved invalid slot (see heap_unref). S_ASSIGN can
	 * reach it with an uninitialised string lvalue in Rance10 v12; freeing
	 * heap[0].s produced "Double free of string object" (D43). */
	if (slot <= 0)
		return;
#ifdef DEBUG_HEAP
	if (unlikely(!string_index_valid(slot)))
		VM_ERROR("Tried to assign string to non-string slot");
#endif
	if (heap[slot].s) {
		free_string(heap[slot].s);
	}
	heap[slot].s = string_ref(string);
}

void heap_struct_assign(int lval, int rval)
{
	/* Slot 0 is the global variable page. SR_ASSIGN can arrive with an
	 * uninitialised struct lvalue of 0; writing through it replaced the
	 * whole global page with a struct (gdb: heap_struct_assign(0,1573)).
	 * Drop that write; -1 remains the real null-pointer error (D54). */
	if (unlikely(lval <= 0))
		return;   /* 0 = global page slot, -1 = null struct lvalue (D54) */
	if (lval == rval)
		return;
	/* Rance10 reaches SR_ASSIGN with stale/non-page slots (e.g. 6 holding
	 * a string). Guard in release builds too - the DEBUG_HEAP checks below
	 * otherwise let heap_get_page() abort startup (D54). */
	if (unlikely(!page_index_valid(lval) || !page_index_valid(rval)))
		return;
#ifdef DEBUG_HEAP
	if (unlikely(!page_index_valid(lval)))
		VM_ERROR("Invalid page index: %d", lval);
	if (unlikely(!page_index_valid(rval)))
		VM_ERROR("Invalid page index: %d", rval);
	if (unlikely(heap[lval].page && heap[lval].page->type != STRUCT_PAGE))
		VM_ERROR("SR_ASSIGN to non-struct page");
	if (unlikely(heap[rval].page && heap[rval].page->type != STRUCT_PAGE))
		VM_ERROR("SR_ASSIGN from non-struct page");
	if (unlikely(heap[lval].page && heap[rval].page && heap[lval].page->index != heap[rval].page->index))
		VM_ERROR("SR_ASSIGN with different struct types");
#endif
	if (heap[lval].page) {
		delete_page(lval);
	}
	heap_set_page(lval, copy_page(heap[rval].page));
}

int32_t heap_alloc_string(struct string *s)
{
	int slot = heap_alloc_slot(VM_STRING);
	heap[slot].s = s;
	return slot;
}

int32_t heap_alloc_page(struct page *page)
{
	int slot = heap_alloc_slot(VM_PAGE);
	heap[slot].page = page;
	if (getenv("XSYS4_ARR_DBG") && page && page->type == ARRAY_PAGE)
		NOTICE("[ARR] born slot=%d p=%p at=%d nr=%d st=%d",
		       slot, (void*)page, page->a_type, page->nr_vars, page->array.struct_type);
	return slot;
}

static void describe_page(struct page *page)
{
	if (!page) {
		sys_message("NULL_PAGE\n");
		return;
	}

	switch (page->type) {
	case GLOBAL_PAGE:
		sys_message("GLOBAL_PAGE\n");
		break;
	case LOCAL_PAGE:
		sys_message("LOCAL_PAGE: %s\n", display_sjis0(ain->functions[page->index].name));
		break;
	case STRUCT_PAGE:
		sys_message("STRUCT_PAGE: %s\n", display_sjis0(ain->structures[page->index].name));
		break;
	case ARRAY_PAGE:
		sys_message("ARRAY_PAGE: %s\n", display_sjis0(ain_strtype(ain, page->a_type, page->array.struct_type)));
		break;
	case DELEGATE_PAGE:
		// TODO: list function names
		sys_message("DELEGATE_PAGE\n");
		break;
	case IFACE_PAGE:
		sys_message("IFACE_PAGE: [struct=%d vtable=%d]\n",
			    page->values[0].i, page->values[1].i);
		break;
	}
}

void heap_describe_slot(int slot)
{
	if (heap[slot].type == VM_STRING && heap[slot].s == &EMPTY_STRING)
		return;
#ifdef DEBUG_HEAP
	sys_message("[%d](%d)(%08X)[", slot, heap[slot].ref, heap[slot].alloc_addr);
	for (int i = 0; i < heap[slot].ref_nr && i < 16; i++) {
		if (i > 0)
			sys_message(",");
		sys_message("%08X", heap[slot].ref_addr[i]);
	}
	sys_message("][");
	for (int i = 0; i < heap[slot].deref_nr && i < 16; i++) {
		if (i > 0)
			sys_message(",");
		sys_message("%08X", heap[slot].deref_addr[i]);
	}
	sys_message("] = ");
#else
	sys_message("[%d](%d) = ", slot, heap[slot].ref);
#endif
	switch (heap[slot].type) {
	case VM_PAGE:
		describe_page(heap[slot].page);
		break;
	case VM_STRING:
		if (heap[slot].s) {
			sys_message("STRING: %s\n", display_sjis0(heap[slot].s->text));
		} else {
			sys_message("STRING: NULL\n");
		}
		break;
	default:
		sys_message("???\n");
		break;
	}
}
