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

#ifndef SYSTEM4_PAGE_H
#define SYSTEM4_PAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "system4/ain.h"
#include "vm.h"

struct ain_variable;
enum ain_data_type;

enum page_type {
	GLOBAL_PAGE,
	LOCAL_PAGE,
	STRUCT_PAGE,
	ARRAY_PAGE,
	DELEGATE_PAGE,
	IFACE_PAGE,
};

#define NR_PAGE_TYPES (IFACE_PAGE+1)

/*
 * page（页）是一组**有序**的变量。全局/局部变量、结构体、数组、委托都靠它实现：
 *
 * 全局变量：所有全局变量放在同一张 global page 里。
 *
 * 局部变量：每次函数调用都有一张 page 承载它的局部变量。
 *
 * 结构体：每个结构体对象是一张 page，槽位就是成员。
 *
 * 数组：每个数组对象是一张 page，槽位就是元素。
 * 多维数组是「page 树」——也就是说整个数组在内存里**不是**连续的一段。
 *
 * 委托：每个委托对象是一张 page，每 3 个槽存一条 (object, function, seq)。
 *
 * 接口：一个 AIN_IFACE 值是 2 个值 [结构体页, vtable 偏移]，用专门的两槽
 * IFACE_PAGE 承载，这样 REF/REFREF 才能对它解引用。
 */
struct page {
	enum page_type type;
	// `index` 这个字段随页类型而变：
	// ----------------
	// GLOBAL_PAGE:   未使用
	// LOCAL_PAGE:    函数号
	// STRUCT_PAGE:   结构体号
	// ARRAY_PAGE:    元素数据类型（例如 AIN_ARRAY_STRING 的元素是 AIN_STRING）
	// DELEGATE_PAGE: 未使用
	union {
		int index;
		enum ain_data_type a_type;
	};
	union {
		// 局部页专有的元数据
		struct {
			int struct_ptr;
		} local;
		// 数组页专有的元数据
		struct {
			int struct_type;
			int rank;
		} array;
	};
	int nr_vars;
	union vm_value values[];
};

static inline union vm_value page_get_var(struct page *page, int i)
{
#ifdef DEBUG_HEAP
	if (i < 0 || i >= page->nr_vars)
		VM_ERROR("Invalid page index: %d", i);
#endif
	return page->values[i];
}

static inline void _page_set_var(struct page *page, int i, union vm_value v)
{
#ifdef DEBUG_HEAP
	if (i < 0 || i >= page->nr_vars)
		VM_ERROR("Invalid page index: %d", i);
#endif
	/* [DBG] XSYS4_WATCH_IFACE=1：盯接口元素数组（struct_type 618）的
	 * **奇数槽**写入 —— 那是 iface 的第二槽 [vtable 偏移]。
	 * 用来回答"写的时候明明是 0，为什么读出来是堆槽号"。 */
	{
		static int on = -1;
		if (on < 0)
			on = getenv("XSYS4_WATCH_IFACE") ? 1 : 0;
		if (on && page->type == ARRAY_PAGE &&
		    page->array.struct_type == 618 && (i & 1)) {
			extern size_t instr_ptr;
			static int n;
			if (n++ < 20)
				NOTICE("[wiface] ip=0x%zX page=%p i=%d old=%d new=%d nr=%d",
				       instr_ptr, (void *)page, i, page->values[i].i,
				       v.i, page->nr_vars);
		}
	}
	page->values[i] = v;
}

#define page_set_var(page, i, v) _page_set_var(page, i, vm_value_cast(v))

// 变量
union vm_value variable_initval(enum ain_data_type type);
/* 用完整的 AIN 类型初始化一个变量（数组元素的结构体类型也带在里面）。 */
union vm_value variable_initval_typed(const struct ain_type *type);
/* [L1] 「哪些类型的值**拥有**堆引用（拆页时 variable_fini / exit_unref 会释放它）」
 * 这个问题只有这一个答案。
 * 任何把这类值复制进页的写入端都必须与它配对处理
 * （见 hll/Array.c 的 array_copy_values()，D134）。
 *
 * 注意：function_call() **故意**不用这个谓词。AIN 的调用约定是
 * 非 ref 参数按「所有权转移」处理。曾试过把 ref 扩到所有拥有引用的类型，
 * A/B 实测**证伪**（double-free 次数 13720 → 13720，毫无变化）。
 * 没有新证据不要「顺手统一」这个不对称。 */
static inline bool variable_owns_ref(enum ain_data_type type)
{
	switch (type) {
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
	case AIN_REF_TYPE:
		return true;
	default:
		return false;
	}
}

void variable_fini(union vm_value v, enum ain_data_type type, bool call_dtor);
enum ain_data_type variable_type(struct page *page, int varno, int *struct_type, int *array_rank);
void variable_set(struct page *page, int varno, enum ain_data_type type, union vm_value val);

// 页
struct page *alloc_page(enum page_type type, int type_index, int nr_vars);
void free_page(struct page *page);
struct page *copy_page(struct page *page);
void delete_page_vars(struct page *page);
void delete_page(int slot);

// 结构体
int alloc_struct(int no);
void init_struct(int no, int slot);
void delete_struct(int no, int slot);
void create_struct(int no, union vm_value *var);
void create_struct_with_ctor(int no, int ctor_fno, union vm_value *var);

// 数组
enum ain_data_type array_type(enum ain_data_type type);
/* 给泛型数组页（`array` / `ref array`）定下元素种类。 */
void array_settle_type(struct page *p, enum ain_data_type elem);
/* 由存储值本身推出的元素种类，给不带类型的写入路径用。 */
enum ain_data_type array_value_type(union vm_value v);
struct page *alloc_array(int rank, union vm_value *dimensions, enum ain_data_type data_type, int struct_type, bool init_structs);
struct page *realloc_array(struct page *src, int rank, union vm_value *dimensions, enum ain_data_type data_type, int struct_type, bool init_structs);
int array_numof(struct page *page, int rank);
void array_copy(struct page *dst, int dst_i, struct page *src, int src_i, int n);
int array_fill(struct page *dst, int dst_i, int n, union vm_value v);
struct page *array_pushback(struct page *dst, union vm_value v, enum ain_data_type data_type, int struct_type);
struct page *array_popback(struct page *dst);
struct page *array_erase(struct page *page, int i, bool *success);
struct page *array_insert(struct page *page, int i, union vm_value v, enum ain_data_type data_type, int struct_type);
void array_sort(struct page *page, int compare_fno);
void array_sort_mem(struct page *page, int member_no);
int array_find(struct page *page, int start, int end, union vm_value v, int compare_fno);
void array_reverse(struct page *page);

// 委托
struct page *delegate_new_from_method(int obj, int fun);
int delegate_numof(struct page *page);
bool delegate_contains(struct page *dst, int obj, int fun);
void delegate_erase(struct page *page, int obj, int fun);
struct page *delegate_append(struct page *dst, int obj, int fun);
struct page *delegate_plusa(struct page *dst, struct page *add);
struct page *delegate_minusa(struct page *dst, struct page *minus);
struct page *delegate_clear(struct page *page);
bool delegate_get(struct page *page, int i, int *obj_out, int *fun_out);

#endif /* SYSTEM4_PAGE_H */
