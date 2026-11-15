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

#ifndef SYSTEM4_HEAP_H
#define SYSTEM4_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct string;
struct page;

enum vm_pointer_type {
	VM_PAGE,
	VM_STRING
};
#define NR_VM_POINTER_TYPES (VM_STRING+1)

// 堆对象：带引用计数。
struct vm_pointer {
	int ref;
	uint32_t seq;
	enum vm_pointer_type type;
	union {
		struct string *s;
		struct page *page;
	};
#ifdef DEBUG_HEAP
	size_t alloc_addr;
	size_t ref_addr[16];
	size_t ref_nr;
	size_t deref_addr[16];
	size_t deref_nr;
	size_t free_addr;
#endif
};

extern struct vm_pointer *heap;
extern size_t heap_size;

void heap_init(void);
void heap_delete(void);
void heap_grow(size_t new_size);

int32_t heap_alloc_slot(enum vm_pointer_type type);
void heap_ref(int slot);
void heap_unref(int slot);
void exit_unref(int slot);

uint32_t heap_get_seq(int slot);

bool heap_index_valid(int index);
bool page_index_valid(int index);
bool string_index_valid(int index);

struct page *heap_get_page(int index);
struct page *heap_get_delegate_page(int index);
struct string *heap_get_string(int index);
void heap_set_page(int slot, struct page *page);
void heap_string_assign(int slot, struct string *string);
void heap_struct_assign(int lval, int rval);
int32_t heap_alloc_page(struct page *page);
int32_t heap_alloc_string(struct string *s);

void heap_describe_slot(int slot);

#ifdef VM_PRIVATE

extern uint32_t heap_next_seq;
extern int32_t *heap_free_stack;
extern size_t heap_free_ptr;

#endif /* VM_PRIVATE */
#endif /* SYSTEM4_HEAP_H */
