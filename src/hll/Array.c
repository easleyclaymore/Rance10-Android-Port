/* Copyright (C) 2025 kichikuou <KichikuouChrome@gmail.com>
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

#include <stdlib.h>
#include <string.h>

#include "hll.h"
#include "system4/string.h"
#include "vm/page.h"
#include "vm/heap.h"

static void check_array(struct page *array)
{
	if (array->type != ARRAY_PAGE || array->a_type != AIN_ARRAY_INT || array->array.rank != 1)
		VM_ERROR("Not a flat integer array");
}

//void Array_NV_copy(struct page **nArray, int nNum);
//void Array_NV_add(struct page **nArray, int nNum);
//void Array_NV_sub(struct page **nArray, int nNum);
//void Array_NV_mul(struct page **nArray, int nNum);
//void Array_NV_div(struct page **nArray, int nNum);
//void Array_NV_and(struct page **nArray, int nNum);
//void Array_NV_or(struct page **nArray, int nNum);
//void Array_NV_xor(struct page **nArray, int nNum);
//void Array_NV_min(struct page **nArray, int nNum);
//void Array_NV_max(struct page **nArray, int nNum);
//void Array_NN_copy(struct page **nArray, struct page *nArrayS);
//void Array_NN_add(struct page **nArray, struct page *nArrayS);
//void Array_NN_sub(struct page **nArray, struct page *nArrayS);
//void Array_NN_mul(struct page **nArray, struct page *nArrayS);
//void Array_NN_div(struct page **nArray, struct page *nArrayS);
//void Array_NN_and(struct page **nArray, struct page *nArrayS);
//void Array_NN_or(struct page **nArray, struct page *nArrayS);
//void Array_NN_xor(struct page **nArray, struct page *nArrayS);
//void Array_NN_min(struct page **nArray, struct page *nArrayS);
//void Array_NN_max(struct page **nArray, struct page *nArrayS);
//void Array_NS_copy(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_add(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_sub(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_mul(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_div(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_and(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_or(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_xor(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_min(struct page **nArray, struct page *sArray, int nMember);
//void Array_NS_max(struct page **nArray, struct page *sArray, int nMember);
//void Array_SV_copy(struct page **sArray, int nMember, int nNum);
//void Array_SV_add(struct page **sArray, int nMember, int nNum);
//void Array_SV_sub(struct page **sArray, int nMember, int nNum);
//void Array_SV_mul(struct page **sArray, int nMember, int nNum);
//void Array_SV_div(struct page **sArray, int nMember, int nNum);
//void Array_SV_and(struct page **sArray, int nMember, int nNum);
//void Array_SV_or(struct page **sArray, int nMember, int nNum);
//void Array_SV_xor(struct page **sArray, int nMember, int nNum);
//void Array_SV_min(struct page **sArray, int nMember, int nNum);
//void Array_SV_max(struct page **sArray, int nMember, int nNum);
//void Array_SN_copy(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_add(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_sub(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_mul(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_div(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_and(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_or(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_xor(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_min(struct page **sArray, int nMember, struct page *nArray);
//void Array_SN_max(struct page **sArray, int nMember, struct page *nArray);
//void Array_SS_copy(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_add(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_sub(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_mul(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_div(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_and(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_or(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_xor(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_min(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_SS_max(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS);

static int Array_NV_eneq(struct page *array, int num)
{
	if (!array)
		return 0;
	check_array(array);
	int count = 0;
	for (int i = 0; i < array->nr_vars; i++) {
		if (array->values[i].i == num)
			count++;
	}
	return count;
}

//int Array_NV_enne(struct page *nArray, int nNum);
//int Array_NV_enlo(struct page *nArray, int nNum);
//int Array_NV_enhi(struct page *nArray, int nNum);
//int Array_NV_enra(struct page *nArray, int nMin, int nMax);
//int Array_NN_eneq(struct page *nArray, struct page *nArrayS);
//int Array_NN_enne(struct page *nArray, struct page *nArrayS);
//int Array_NN_enlo(struct page *nArray, struct page *nArrayS);
//int Array_NN_enhi(struct page *nArray, struct page *nArrayS);
//int Array_NS_eneq(struct page *nArray, struct page *sArray, int nMember);
//int Array_NS_enne(struct page *nArray, struct page *sArray, int nMember);
//int Array_NS_enlo(struct page *nArray, struct page *sArray, int nMember);
//int Array_NS_enhi(struct page *nArray, struct page *sArray, int nMember);
//int Array_SV_eneq(struct page *sArray, int nMember, int nNum);
//int Array_SV_enne(struct page *sArray, int nMember, int nNum);
//int Array_SV_enlo(struct page *sArray, int nMember, int nNum);
//int Array_SV_enhi(struct page *sArray, int nMember, int nNum);
//int Array_SV_enra(struct page *sArray, int nMember, int nMin, int nMax);
//int Array_SN_eneq(struct page *sArray, int nMember, struct page *nArray);
//int Array_SN_enne(struct page *sArray, int nMember, struct page *nArray);
//int Array_SN_enlo(struct page *sArray, int nMember, struct page *nArray);
//int Array_SN_enhi(struct page *sArray, int nMember, struct page *nArray);
//int Array_SS_eneq(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS);
//int Array_SS_enne(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS);
//int Array_SS_enlo(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS);
//int Array_SS_enhi(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS);
//void Array_NV_cheq(struct page **nArray, int nNum, int nChg);
//void Array_NV_chne(struct page **nArray, int nNum, int nChg);
//void Array_NV_chlo(struct page **nArray, int nNum, int nChg);
//void Array_NV_chhi(struct page **nArray, int nNum, int nChg);
//void Array_NV_chra(struct page **nArray, int nMin, int nMax, int nChg);
//void Array_NN_cheq(struct page **nArray, struct page *nArrayS, int nChg);
//void Array_NN_chne(struct page **nArray, struct page *nArrayS, int nChg);
//void Array_NN_chlo(struct page **nArray, struct page *nArrayS, int nChg);
//void Array_NN_chhi(struct page **nArray, struct page *nArrayS, int nChg);
//void Array_NS_cheq(struct page **nArray, struct page *sArray, int nMember, int nChg);
//void Array_NS_chne(struct page **nArray, struct page *sArray, int nMember, int nChg);
//void Array_NS_chlo(struct page **nArray, struct page *sArray, int nMember, int nChg);
//void Array_NS_chhi(struct page **nArray, struct page *sArray, int nMember, int nChg);
//void Array_SV_cheq(struct page **sArray, int nMember, int nNum, int nChg);
//void Array_SV_chne(struct page **sArray, int nMember, int nNum, int nChg);
//void Array_SV_chlo(struct page **sArray, int nMember, int nNum, int nChg);
//void Array_SV_chhi(struct page **sArray, int nMember, int nNum, int nChg);
//void Array_SV_chra(struct page **sArray, int nMember, int nMin, int nMax, int nChg);
//void Array_SN_cheq(struct page **sArray, int nMember, struct page *nArray, int nChg);
//void Array_SN_chne(struct page **sArray, int nMember, struct page *nArray, int nChg);
//void Array_SN_chlo(struct page **sArray, int nMember, struct page *nArray, int nChg);
//void Array_SN_chhi(struct page **sArray, int nMember, struct page *nArray, int nChg);
//void Array_SS_cheq(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS, int nChg);
//void Array_SS_chne(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS, int nChg);
//void Array_SS_chlo(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS, int nChg);
//void Array_SS_chhi(struct page **sArray, int nMember, struct page *sArrayS, int nMemberS, int nChg);
//void Array_NV_fweq(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fwne(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fwlo(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fwhi(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fwra(struct page *nArray, int nMin, int nMax, struct page **nArrayD);
//void Array_NV_faeq(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fane(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_falo(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fahi(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fara(struct page *nArray, int nMin, int nMax, struct page **nArrayD);
//void Array_NV_foeq(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fone(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_folo(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fohi(struct page *nArray, int nNum, struct page **nArrayD);
//void Array_NV_fora(struct page *nArray, int nMin, int nMax, struct page **nArrayD);
//void Array_NN_fweq(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fwne(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fwlo(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fwhi(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_faeq(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fane(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_falo(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fahi(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_foeq(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fone(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_folo(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NN_fohi(struct page *nArray, struct page *nArrayS, struct page **nArrayD);
//void Array_NS_fweq(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fwne(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fwlo(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fwhi(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_faeq(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fane(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_falo(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fahi(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_foeq(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fone(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_folo(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_NS_fohi(struct page *nArray, struct page *sArray, int nMember, struct page **nArrayD);
//void Array_SV_fweq(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fwne(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fwlo(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fwhi(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fwra(struct page *sArray, int nMember, int nMin, int nMax, struct page **nArrayD);
//void Array_SV_faeq(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fane(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_falo(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fahi(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fara(struct page *sArray, int nMember, int nMin, int nMax, struct page **nArrayD);
//void Array_SV_foeq(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fone(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_folo(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fohi(struct page *sArray, int nMember, int nNum, struct page **nArrayD);
//void Array_SV_fora(struct page *sArray, int nMember, int nMin, int nMax, struct page **nArrayD);
//void Array_SN_fweq(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fwne(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fwlo(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fwhi(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_faeq(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fane(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_falo(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fahi(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_foeq(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fone(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_folo(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SN_fohi(struct page *sArray, int nMember, struct page *nArrayS, struct page **nArrayD);
//void Array_SS_fweq(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fwne(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fwlo(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fwhi(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_faeq(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fane(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_falo(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fahi(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_foeq(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fone(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_folo(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);
//void Array_SS_fohi(struct page *sArray, int nMember, struct page *sArrayS, int nMemberS, struct page **nArrayD);

static int Array_NV_sceq(struct page *array, int index, int num, int *out_index)
{
	if (!array)
		return 0;
	check_array(array);

	if (index >= 0) {
		for (int i = index; i < array->nr_vars; i++) {
			if (array->values[i].i == num) {
				*out_index = i;
				return 1;
			}
		}
	} else {
		// Negative index means backwards search
		index &= 0x7fffffff;
		if (index >= array->nr_vars)
			return 0;
		for (int i = index; i >= 0; --i) {
			if (array->values[i].i == num) {
				*out_index = i;
				return 1;
			}
		}
	}
	return 0;

}

//int Array_NV_scne(struct page *nArray, int nIndex, int nNum, int *pnIndex);
//int Array_NV_sclo(struct page *nArray, int nIndex, int nNum, int *pnIndex);
//int Array_NV_schi(struct page *nArray, int nIndex, int nNum, int *pnIndex);
//int Array_NV_scra(struct page *nArray, int nIndex, int nMin, int nMax, int *pnIndex);
//int Array_SV_sceq(struct page *sArray, int nMember, int nIndex, int nNum, int *pnIndex);
//int Array_SV_scne(struct page *sArray, int nMember, int nIndex, int nNum, int *pnIndex);
//int Array_SV_sclo(struct page *sArray, int nMember, int nIndex, int nNum, int *pnIndex);
//int Array_SV_schi(struct page *sArray, int nMember, int nIndex, int nNum, int *pnIndex);
//int Array_SV_scra(struct page *sArray, int nMember, int nIndex, int nMin, int nMax, int *pnIndex);
//int Array_NN_sclowest(struct page *nArray, struct page **nArrayD, int *pnIndex);
//int Array_NN_schighest(struct page *nArray, struct page **nArrayD, int *pnIndex);
//int Array_NS_sclowest(struct page *nArray, struct page **sArray, int nMember, int *pnIndex);
//int Array_NS_schighest(struct page *nArray, struct page **sArray, int nMember, int *pnIndex);
//int Array_SN_sclowest(struct page *sArray, int nMember, struct page **nArray, int *pnIndex);
//int Array_SN_schighest(struct page *sArray, int nMember, struct page **nArray, int *pnIndex);
//int Array_SS_sclowest(struct page *sArray, int nMember, struct page **sArrayD, int nMemberS, int *pnIndex);
//int Array_SS_schighest(struct page *sArray, int nMember, struct page **sArrayD, int nMemberS, int *pnIndex);
//int Array_VN_add(struct page *nArray);
//int Array_VN_and(struct page *nArray);
//int Array_VN_or(struct page *nArray);
//int Array_VS_add(struct page *sArray, int nMember);
//int Array_VS_and(struct page *sArray, int nMember);
//int Array_VS_or(struct page *sArray, int nMember);


/* ---- System 4.3 generic array API (Rance X) ----
 * Alloc(ref array, type, struct_type, rank, size): allocate the array object.
 * The exact meaning of the 4 ints is inferred; adjust after observing calls.
 */
static enum ain_data_type elem_to_array_type(enum ain_data_type t);

static void Array_Alloc_(struct page **out, int size, int type, int a, int b)
{
	WARNING("Array.Alloc: size=%d type=%d a=%d b=%d (out=%p -> %p)",
		size, type, a, b, (void*)out, out ? (void*)*out : NULL);
	if (!out || !*out)
		return;
	if (type < 0 || type > 100) {
		/* AIN v12: CALLHLL's third immediate carries the element type
		 * for generic Array calls; the HLL arguments themselves are -1. */
		int tag = hll_generic_result_type();
		type = tag >= 0 ? tag : AIN_INT;
	}
	int n = (size > 0 && size < (1 << 20)) ? size : 0;
	/* Keep the struct type declared by the AIN variable: struct arrays
	 * need it to construct elements in Array.EmplaceBack. */
	int struct_type = (*out)->array.struct_type;
	/* A container declared with a struct_type holds struct objects, even
	 * when the generic Alloc argument carries no element type (-1).
	 * Without this, struct arrays degenerate to AIN_ARRAY_INT and their
	 * elements never get reference-counted (D21). */
	if (struct_type >= 0)
		type = AIN_STRUCT;
	/* ARRAY_PAGE 的 a_type 必须存【数组类型】（AIN_ARRAY_STRUCT 等），
	 * 不是元素类型；否则 variable_type()/variable_fini() 会拿错类型 */
	/* AIN v12: an AIN_IFACE element is two consecutive value slots
	 * ([struct page, vtable offset]) and bytecode indexes them with i*2
	 * (CDebugFPSGraph@Create @0xb238e). size counts ELEMENTS. Use plain
	 * AIN_ARRAY_INT storage so variable_fini() does not unref the second
	 * slot of each element as if it were an object handle. */
	int iface_array = (hll_generic_result_type() == AIN_IFACE);
	if (iface_array)
		n *= 2;
	struct page *p = alloc_page(ARRAY_PAGE,
	                            iface_array ? AIN_ARRAY_INT : elem_to_array_type(type), n);
	p->array.rank = 1;
	p->array.struct_type = struct_type;
	*out = p;
	WARNING("Array.Alloc: 已填充 page=%p nr_vars=%d", (void*)p, p->nr_vars);
}

static void Array_Free_(struct page **a)
{
	/* 数组对象由 VM 的引用计数管理（variable_fini -> heap_unref）。
	 * 这里若把参数置 NULL 或直接释放，会和 VM 打架导致 double free。
	 * 保持空实现，让 GC 自己处理。 */
	(void)a;
}

static int Array_Numof_(struct page **a)
{
	if (!a || !*a)
		return 0;
	int n = (*a)->nr_vars;
	/* Numof returns the ELEMENT count. #43 allocates 2 slots per
	 * AIN_IFACE element (bytecode indexes with i*2), so divide the slot
	 * count back for that tag (D46). */
	if (hll_generic_result_type() == AIN_IFACE)
		n /= 2;
	return n;
}

static bool Array_Empty_(struct page **a)
{
	return Array_Numof_(a) == 0;
}


/* At(ref array, int index) -> ref <any>：返回元素引用（可读可写） */
static void *Array_Last_(struct page **a)
{
	NOTICE("[Array] Last");
	if (!a || !*a)
		return NULL;
	struct page *p = *a;
	if (p->nr_vars == 0)
		return NULL;
	return &p->values[p->nr_vars - 1];
}

static void *Array_At_(struct page **a, int index)
{
	NOTICE("[Array] At(%d)", index);
	if (!a || !*a)
		return NULL;
	struct page *p = *a;
	if (index < 0 || index >= p->nr_vars)
		return NULL;
	return &p->values[index];
}



/* a_type 存的是元素类型，而 realloc_array 要的是数组类型 */
static enum ain_data_type elem_to_array_type(enum ain_data_type t)
{
	switch (t) {
	case AIN_FLOAT:  return AIN_ARRAY_FLOAT;
	case AIN_STRING: return AIN_ARRAY_STRING;
	case AIN_STRUCT: return AIN_ARRAY_STRUCT;
	case AIN_BOOL:   return AIN_ARRAY_BOOL;
	default:         return AIN_ARRAY_INT;
	}
}

/* ---- 通用元素比较：t74 在 ffi 里是 void*（值的地址） ---- */
static bool elem_eq(struct page *p, int i, void *v)
{
	if (!v)
		return false;
	switch (p->a_type) {
	case AIN_FLOAT:
		return p->values[i].f == *(float *)v;
	case AIN_STRING: {
		struct string *a = heap_get_string(p->values[i].i);
		struct string *b = *(struct string **)v;
		if (!a || !b)
			return a == b;
		return !strcmp(a->text, b->text);
	}
	default:
		return p->values[i].i == *(int *)v;
	}
}

/* AIN f[47]/f[48] BinarySearch(ref array, key) -> int.
 * f[48] passes an AIN comparison-function number (t95) rather than a raw
 * key; a proper predicate dispatch needs vm_call (D39). Fall back to a
 * linear find with the same "not found = -1" contract as Array_Find_. */
static int Array_BinarySearch_(struct page **a, uint64_t key)
{
	if (!a || !*a)
		return -1;
	/* f[48] passes an AIN comparison-function number, not a value key;
	 * elem_eq() would dereference it as a heap object. Predicate dispatch
	 * is still TODO (D39), so report "not found" and let the caller use
	 * its FindInsertPos path. */
	(void)key;
	return -1;
}

/* AIN f[43..46] LowerBound/UpperBound(ref array, key|predicate) -> int.
 * Predicate overloads (t95) pass an AIN comparison-function number, which
 * is not a comparable value; full predicate dispatch is D39. Return a
 * valid insertion position (0) so the caller's Insert path stays in
 * bounds. The t74 overloads should do a real ordered lookup (D39). */
static int Array_LowerBound_(struct page **a, uint64_t key)
{
	(void)a; (void)key;
	return 0;
}

static int Array_UpperBound_(struct page **a, uint64_t key)
{
	(void)a; (void)key;
	return 0;
}

/* AIN f[11]: void Insert(ref array, int index, generic value). */
static void Array_Insert_(struct page **a, int index, uint64_t value)
{
	if (!a || !*a)
		return;
	struct page *p = *a;
	if (p->type != ARRAY_PAGE) {
		WARNING("[Array] Insert: invalid ref-array page type=%d, skipped", p->type);
		return;
	}
	int n = p->nr_vars;
	if (index < 0) index = 0;
	if (index > n) index = n;
	enum ain_data_type at = p->a_type;
	int st = p->array.struct_type;
	union vm_value dim = { .i = n + 1 };
	struct page *np = realloc_array(p, 1, &dim, at, st, false);
	if (!np)
		return;
	for (int i = n; i > index; i--)
		np->values[i] = np->values[i - 1];
	np->values[index] = *(union vm_value *)&value;
	*a = np;
}

/* AIN f[25..28] Array.Copy share one name but have 2/3/4/5 parameters
 * in different positions, so a single C prototype cannot serve all of
 * them. Stage 1 (D53): parameter-agnostic no-op keeps startup moving;
 * each overload needs its own binding by fno. */
static int Array_Copy_(void)
{
	return 0;
}

/* AIN Array.Fill overloads take (ref array, value[, from, count]) and
 * return void/int. Stage 1 (D55): parameter-agnostic no-op keeps the
 * input init chain moving; real element fill still to be implemented. */
static int Array_Fill_(void)
{
	return 0;
}

static int Array_Find_(struct page **a, uint64_t value)
{
	NOTICE("[Array] Find");
	if (!a || !*a)
		return -1;
	struct page *p = *a;
	for (int i = 0; i < p->nr_vars; i++)
		if (elem_eq(p, i, &value))
			return i;
	return -1;
}

static int Array_FindLast_(struct page **a, uint64_t value)
{
	NOTICE("[Array] FindLast");
	if (!a || !*a)
		return -1;
	struct page *p = *a;
	for (int i = p->nr_vars - 1; i >= 0; i--)
		if (elem_eq(p, i, &value))
			return i;
	return -1;
}

static bool Array_IsExist_(struct page **a, uint64_t value)
{
	NOTICE("[Array] IsExist");
	return Array_Find_(a, value) >= 0;
}

static void Array_PushBack_(struct page **a, uint64_t value)
{
	NOTICE("[Array] PushBack");
	if (!a || !*a || !value)
		return;
	struct page *p = *a;
	/* D41: Rance10's card-construct cache can pass a ref-array slot whose
	 * payload is not a page at all (observed: &ffi_type_void from libffi).
	 * Do not touch the slot - the HLL cleanup writes heap_ptrs back into it -
	 * just skip the append. */
	if (p->type != ARRAY_PAGE) {
		WARNING("[Array] PushBack: invalid ref-array page type=%d, skipped", p->type);
		return;
	}
	/* realloc_array 用 xrealloc 原地重分配，p 调用后立刻失效，
	 * 所以长度和类型必须先在参数求值阶段取出 */
	int old_n = p->nr_vars;
	/* p->a_type is already an ARRAY type (AIN_ARRAY_STRUCT, ...).
	 * realloc_array() expects that array type, not the element type:
	 * elem_to_array_type() on an array type falls through to AIN_ARRAY_INT
	 * and silently turns struct arrays into int arrays. */
	enum ain_data_type at = p->a_type;
	int st = p->array.struct_type;
	NOTICE("PushBack: p=%p n=%d at=%d st=%d", (void*)p, old_n, at, st);
	union vm_value dim = { .i = old_n + 1 };
	struct page *np = realloc_array(p, 1, &dim, at, st, false);
	NOTICE("PushBack: np=%p", (void*)np);
	if (!np)
		return;
	np->values[old_n] = *(union vm_value *)&value;
	/* The container becomes an independent owner of a heap object.
	 * Take a reference for it: the caller still owns its own reference
	 * and will release it with DELETE. Without this, DELETE drops the
	 * last ref and the container is left holding a dead slot (D21). */
	enum ain_data_type elem = array_type(at);
	if ((elem == AIN_STRUCT || elem == AIN_STRING || elem == AIN_DELEGATE ||
	     elem == AIN_ARRAY) && value > 0 && value < heap_size &&
	    heap[value].ref > 0)
		heap_ref((int)value);
	NOTICE("PushBack: 写入完成, *a 将更新");
	*a = np;
}

static void Array_PopBack_(struct page **a)
{
	NOTICE("[Array] PopBack");
	if (!a || !*a)
		return;
	struct page *p = *a;
	if (p->nr_vars > 0)
		p->nr_vars--;
}


static int Array_Numof_(struct page **a);

/* AIN declares EmplaceBack(ref array) with ONE argument. The old
 * two-argument C stub read an extra uninitialised register and wrote it
 * into the new element, producing a garbage "this" for CALLMETHOD.
 * The element is value-initialised according to the CALLHLL type tag. */
static void *Array_EmplaceBack_(struct page **a)
{
	NOTICE("[Array] EmplaceBack");
	if (!a || !*a)
		return NULL;
	struct page *p = *a;
	int old_n = p->nr_vars;
	int st = p->array.struct_type;
	union vm_value dim = { .i = old_n + 1 };
	struct page *np = realloc_array(p, 1, &dim, p->a_type, st, false);
	if (!np)
		return NULL;
	*a = np;
	int tag = hll_generic_result_type();
	if (tag == AIN_STRUCT && st >= 0)
		create_struct(st, &np->values[old_n]);
	else
		np->values[old_n] = variable_initval(tag >= 0 ? tag : AIN_INT);
	return &np->values[old_n];
}

static void Array_Realloc_(struct page **a, int size)
{
	NOTICE("[Array] Realloc(%d)", size);
	if (!a)
		return;
	if (size < 0)
		size = 0;
	if (!*a) {
		struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, size);
		p->array.rank = 1;
		p->array.struct_type = -1;
		*a = p;
		return;
	}
	struct page *p = *a;
	union vm_value dim = { .i = size };
	struct page *np = realloc_array(p, 1, &dim, elem_to_array_type(p->a_type), p->array.struct_type, false);
	if (np)
		*a = np;
}

static void Array_ReallocInit_(struct page **a, int size, uint64_t value)
{
	Array_Realloc_(a, size);
	if (!a || !*a || !value)
		return;
	struct page *p = *a;
	for (int i = 0; i < p->nr_vars; i++)
		p->values[i] = *(union vm_value *)&value;
}

static bool Array_Erase_(struct page **a, int index)
{
	NOTICE("[Array] Erase(%d)", index);
	if (!a || !*a)
		return false;
	struct page *p = *a;
	if (index < 0 || index >= p->nr_vars)
		return false;
	memmove(&p->values[index], &p->values[index + 1],
		(p->nr_vars - index - 1) * sizeof(union vm_value));
	p->nr_vars--;
	return true;
}

static void Array_Reverse_(struct page **a)
{
	if (!a || !*a)
		return;
	struct page *p = *a;
	for (int i = 0, j = p->nr_vars - 1; i < j; i++, j--) {
		union vm_value t = p->values[i];
		p->values[i] = p->values[j];
		p->values[j] = t;
	}
}

HLL_LIBRARY(Array,
	    HLL_EXPORT(At, Array_At_),
    HLL_EXPORT(Last, Array_Last_),
	    HLL_EXPORT(Find, Array_Find_),
	    HLL_EXPORT(BinarySearch, Array_BinarySearch_),
	    HLL_EXPORT(Insert, Array_Insert_),
	    HLL_EXPORT(Copy, Array_Copy_),
	    HLL_EXPORT(Fill, Array_Fill_),
	    HLL_EXPORT(LowerBound, Array_LowerBound_),
	    HLL_EXPORT(UpperBound, Array_UpperBound_),
	    HLL_EXPORT(FindLast, Array_FindLast_),
	    HLL_EXPORT(IsExist, Array_IsExist_),
	    HLL_EXPORT(PushBack, Array_PushBack_),
	    HLL_EXPORT(PopBack, Array_PopBack_),
	    HLL_EXPORT(EmplaceBack, Array_EmplaceBack_),
	    HLL_EXPORT(Realloc, Array_Realloc_),
	    HLL_EXPORT(Erase, Array_Erase_),
	    HLL_EXPORT(Reverse, Array_Reverse_),
	    HLL_EXPORT(Alloc, Array_Alloc_),
	    HLL_EXPORT(Free, Array_Free_),
	    HLL_EXPORT(Numof, Array_Numof_),
	    HLL_EXPORT(Empty, Array_Empty_),
	    HLL_TODO_EXPORT(NV_copy, Array_NV_copy),
	    HLL_TODO_EXPORT(NV_add, Array_NV_add),
	    HLL_TODO_EXPORT(NV_sub, Array_NV_sub),
	    HLL_TODO_EXPORT(NV_mul, Array_NV_mul),
	    HLL_TODO_EXPORT(NV_div, Array_NV_div),
	    HLL_TODO_EXPORT(NV_and, Array_NV_and),
	    HLL_TODO_EXPORT(NV_or, Array_NV_or),
	    HLL_TODO_EXPORT(NV_xor, Array_NV_xor),
	    HLL_TODO_EXPORT(NV_min, Array_NV_min),
	    HLL_TODO_EXPORT(NV_max, Array_NV_max),
	    HLL_TODO_EXPORT(NN_copy, Array_NN_copy),
	    HLL_TODO_EXPORT(NN_add, Array_NN_add),
	    HLL_TODO_EXPORT(NN_sub, Array_NN_sub),
	    HLL_TODO_EXPORT(NN_mul, Array_NN_mul),
	    HLL_TODO_EXPORT(NN_div, Array_NN_div),
	    HLL_TODO_EXPORT(NN_and, Array_NN_and),
	    HLL_TODO_EXPORT(NN_or, Array_NN_or),
	    HLL_TODO_EXPORT(NN_xor, Array_NN_xor),
	    HLL_TODO_EXPORT(NN_min, Array_NN_min),
	    HLL_TODO_EXPORT(NN_max, Array_NN_max),
	    HLL_TODO_EXPORT(NS_copy, Array_NS_copy),
	    HLL_TODO_EXPORT(NS_add, Array_NS_add),
	    HLL_TODO_EXPORT(NS_sub, Array_NS_sub),
	    HLL_TODO_EXPORT(NS_mul, Array_NS_mul),
	    HLL_TODO_EXPORT(NS_div, Array_NS_div),
	    HLL_TODO_EXPORT(NS_and, Array_NS_and),
	    HLL_TODO_EXPORT(NS_or, Array_NS_or),
	    HLL_TODO_EXPORT(NS_xor, Array_NS_xor),
	    HLL_TODO_EXPORT(NS_min, Array_NS_min),
	    HLL_TODO_EXPORT(NS_max, Array_NS_max),
	    HLL_TODO_EXPORT(SV_copy, Array_SV_copy),
	    HLL_TODO_EXPORT(SV_add, Array_SV_add),
	    HLL_TODO_EXPORT(SV_sub, Array_SV_sub),
	    HLL_TODO_EXPORT(SV_mul, Array_SV_mul),
	    HLL_TODO_EXPORT(SV_div, Array_SV_div),
	    HLL_TODO_EXPORT(SV_and, Array_SV_and),
	    HLL_TODO_EXPORT(SV_or, Array_SV_or),
	    HLL_TODO_EXPORT(SV_xor, Array_SV_xor),
	    HLL_TODO_EXPORT(SV_min, Array_SV_min),
	    HLL_TODO_EXPORT(SV_max, Array_SV_max),
	    HLL_TODO_EXPORT(SN_copy, Array_SN_copy),
	    HLL_TODO_EXPORT(SN_add, Array_SN_add),
	    HLL_TODO_EXPORT(SN_sub, Array_SN_sub),
	    HLL_TODO_EXPORT(SN_mul, Array_SN_mul),
	    HLL_TODO_EXPORT(SN_div, Array_SN_div),
	    HLL_TODO_EXPORT(SN_and, Array_SN_and),
	    HLL_TODO_EXPORT(SN_or, Array_SN_or),
	    HLL_TODO_EXPORT(SN_xor, Array_SN_xor),
	    HLL_TODO_EXPORT(SN_min, Array_SN_min),
	    HLL_TODO_EXPORT(SN_max, Array_SN_max),
	    HLL_TODO_EXPORT(SS_copy, Array_SS_copy),
	    HLL_TODO_EXPORT(SS_add, Array_SS_add),
	    HLL_TODO_EXPORT(SS_sub, Array_SS_sub),
	    HLL_TODO_EXPORT(SS_mul, Array_SS_mul),
	    HLL_TODO_EXPORT(SS_div, Array_SS_div),
	    HLL_TODO_EXPORT(SS_and, Array_SS_and),
	    HLL_TODO_EXPORT(SS_or, Array_SS_or),
	    HLL_TODO_EXPORT(SS_xor, Array_SS_xor),
	    HLL_TODO_EXPORT(SS_min, Array_SS_min),
	    HLL_TODO_EXPORT(SS_max, Array_SS_max),
	    HLL_EXPORT(NV_eneq, Array_NV_eneq),
	    HLL_TODO_EXPORT(NV_enne, Array_NV_enne),
	    HLL_TODO_EXPORT(NV_enlo, Array_NV_enlo),
	    HLL_TODO_EXPORT(NV_enhi, Array_NV_enhi),
	    HLL_TODO_EXPORT(NV_enra, Array_NV_enra),
	    HLL_TODO_EXPORT(NN_eneq, Array_NN_eneq),
	    HLL_TODO_EXPORT(NN_enne, Array_NN_enne),
	    HLL_TODO_EXPORT(NN_enlo, Array_NN_enlo),
	    HLL_TODO_EXPORT(NN_enhi, Array_NN_enhi),
	    HLL_TODO_EXPORT(NS_eneq, Array_NS_eneq),
	    HLL_TODO_EXPORT(NS_enne, Array_NS_enne),
	    HLL_TODO_EXPORT(NS_enlo, Array_NS_enlo),
	    HLL_TODO_EXPORT(NS_enhi, Array_NS_enhi),
	    HLL_TODO_EXPORT(SV_eneq, Array_SV_eneq),
	    HLL_TODO_EXPORT(SV_enne, Array_SV_enne),
	    HLL_TODO_EXPORT(SV_enlo, Array_SV_enlo),
	    HLL_TODO_EXPORT(SV_enhi, Array_SV_enhi),
	    HLL_TODO_EXPORT(SV_enra, Array_SV_enra),
	    HLL_TODO_EXPORT(SN_eneq, Array_SN_eneq),
	    HLL_TODO_EXPORT(SN_enne, Array_SN_enne),
	    HLL_TODO_EXPORT(SN_enlo, Array_SN_enlo),
	    HLL_TODO_EXPORT(SN_enhi, Array_SN_enhi),
	    HLL_TODO_EXPORT(SS_eneq, Array_SS_eneq),
	    HLL_TODO_EXPORT(SS_enne, Array_SS_enne),
	    HLL_TODO_EXPORT(SS_enlo, Array_SS_enlo),
	    HLL_TODO_EXPORT(SS_enhi, Array_SS_enhi),
	    HLL_TODO_EXPORT(NV_cheq, Array_NV_cheq),
	    HLL_TODO_EXPORT(NV_chne, Array_NV_chne),
	    HLL_TODO_EXPORT(NV_chlo, Array_NV_chlo),
	    HLL_TODO_EXPORT(NV_chhi, Array_NV_chhi),
	    HLL_TODO_EXPORT(NV_chra, Array_NV_chra),
	    HLL_TODO_EXPORT(NN_cheq, Array_NN_cheq),
	    HLL_TODO_EXPORT(NN_chne, Array_NN_chne),
	    HLL_TODO_EXPORT(NN_chlo, Array_NN_chlo),
	    HLL_TODO_EXPORT(NN_chhi, Array_NN_chhi),
	    HLL_TODO_EXPORT(NS_cheq, Array_NS_cheq),
	    HLL_TODO_EXPORT(NS_chne, Array_NS_chne),
	    HLL_TODO_EXPORT(NS_chlo, Array_NS_chlo),
	    HLL_TODO_EXPORT(NS_chhi, Array_NS_chhi),
	    HLL_TODO_EXPORT(SV_cheq, Array_SV_cheq),
	    HLL_TODO_EXPORT(SV_chne, Array_SV_chne),
	    HLL_TODO_EXPORT(SV_chlo, Array_SV_chlo),
	    HLL_TODO_EXPORT(SV_chhi, Array_SV_chhi),
	    HLL_TODO_EXPORT(SV_chra, Array_SV_chra),
	    HLL_TODO_EXPORT(SN_cheq, Array_SN_cheq),
	    HLL_TODO_EXPORT(SN_chne, Array_SN_chne),
	    HLL_TODO_EXPORT(SN_chlo, Array_SN_chlo),
	    HLL_TODO_EXPORT(SN_chhi, Array_SN_chhi),
	    HLL_TODO_EXPORT(SS_cheq, Array_SS_cheq),
	    HLL_TODO_EXPORT(SS_chne, Array_SS_chne),
	    HLL_TODO_EXPORT(SS_chlo, Array_SS_chlo),
	    HLL_TODO_EXPORT(SS_chhi, Array_SS_chhi),
	    HLL_TODO_EXPORT(NV_fweq, Array_NV_fweq),
	    HLL_TODO_EXPORT(NV_fwne, Array_NV_fwne),
	    HLL_TODO_EXPORT(NV_fwlo, Array_NV_fwlo),
	    HLL_TODO_EXPORT(NV_fwhi, Array_NV_fwhi),
	    HLL_TODO_EXPORT(NV_fwra, Array_NV_fwra),
	    HLL_TODO_EXPORT(NV_faeq, Array_NV_faeq),
	    HLL_TODO_EXPORT(NV_fane, Array_NV_fane),
	    HLL_TODO_EXPORT(NV_falo, Array_NV_falo),
	    HLL_TODO_EXPORT(NV_fahi, Array_NV_fahi),
	    HLL_TODO_EXPORT(NV_fara, Array_NV_fara),
	    HLL_TODO_EXPORT(NV_foeq, Array_NV_foeq),
	    HLL_TODO_EXPORT(NV_fone, Array_NV_fone),
	    HLL_TODO_EXPORT(NV_folo, Array_NV_folo),
	    HLL_TODO_EXPORT(NV_fohi, Array_NV_fohi),
	    HLL_TODO_EXPORT(NV_fora, Array_NV_fora),
	    HLL_TODO_EXPORT(NN_fweq, Array_NN_fweq),
	    HLL_TODO_EXPORT(NN_fwne, Array_NN_fwne),
	    HLL_TODO_EXPORT(NN_fwlo, Array_NN_fwlo),
	    HLL_TODO_EXPORT(NN_fwhi, Array_NN_fwhi),
	    HLL_TODO_EXPORT(NN_faeq, Array_NN_faeq),
	    HLL_TODO_EXPORT(NN_fane, Array_NN_fane),
	    HLL_TODO_EXPORT(NN_falo, Array_NN_falo),
	    HLL_TODO_EXPORT(NN_fahi, Array_NN_fahi),
	    HLL_TODO_EXPORT(NN_foeq, Array_NN_foeq),
	    HLL_TODO_EXPORT(NN_fone, Array_NN_fone),
	    HLL_TODO_EXPORT(NN_folo, Array_NN_folo),
	    HLL_TODO_EXPORT(NN_fohi, Array_NN_fohi),
	    HLL_TODO_EXPORT(NS_fweq, Array_NS_fweq),
	    HLL_TODO_EXPORT(NS_fwne, Array_NS_fwne),
	    HLL_TODO_EXPORT(NS_fwlo, Array_NS_fwlo),
	    HLL_TODO_EXPORT(NS_fwhi, Array_NS_fwhi),
	    HLL_TODO_EXPORT(NS_faeq, Array_NS_faeq),
	    HLL_TODO_EXPORT(NS_fane, Array_NS_fane),
	    HLL_TODO_EXPORT(NS_falo, Array_NS_falo),
	    HLL_TODO_EXPORT(NS_fahi, Array_NS_fahi),
	    HLL_TODO_EXPORT(NS_foeq, Array_NS_foeq),
	    HLL_TODO_EXPORT(NS_fone, Array_NS_fone),
	    HLL_TODO_EXPORT(NS_folo, Array_NS_folo),
	    HLL_TODO_EXPORT(NS_fohi, Array_NS_fohi),
	    HLL_TODO_EXPORT(SV_fweq, Array_SV_fweq),
	    HLL_TODO_EXPORT(SV_fwne, Array_SV_fwne),
	    HLL_TODO_EXPORT(SV_fwlo, Array_SV_fwlo),
	    HLL_TODO_EXPORT(SV_fwhi, Array_SV_fwhi),
	    HLL_TODO_EXPORT(SV_fwra, Array_SV_fwra),
	    HLL_TODO_EXPORT(SV_faeq, Array_SV_faeq),
	    HLL_TODO_EXPORT(SV_fane, Array_SV_fane),
	    HLL_TODO_EXPORT(SV_falo, Array_SV_falo),
	    HLL_TODO_EXPORT(SV_fahi, Array_SV_fahi),
	    HLL_TODO_EXPORT(SV_fara, Array_SV_fara),
	    HLL_TODO_EXPORT(SV_foeq, Array_SV_foeq),
	    HLL_TODO_EXPORT(SV_fone, Array_SV_fone),
	    HLL_TODO_EXPORT(SV_folo, Array_SV_folo),
	    HLL_TODO_EXPORT(SV_fohi, Array_SV_fohi),
	    HLL_TODO_EXPORT(SV_fora, Array_SV_fora),
	    HLL_TODO_EXPORT(SN_fweq, Array_SN_fweq),
	    HLL_TODO_EXPORT(SN_fwne, Array_SN_fwne),
	    HLL_TODO_EXPORT(SN_fwlo, Array_SN_fwlo),
	    HLL_TODO_EXPORT(SN_fwhi, Array_SN_fwhi),
	    HLL_TODO_EXPORT(SN_faeq, Array_SN_faeq),
	    HLL_TODO_EXPORT(SN_fane, Array_SN_fane),
	    HLL_TODO_EXPORT(SN_falo, Array_SN_falo),
	    HLL_TODO_EXPORT(SN_fahi, Array_SN_fahi),
	    HLL_TODO_EXPORT(SN_foeq, Array_SN_foeq),
	    HLL_TODO_EXPORT(SN_fone, Array_SN_fone),
	    HLL_TODO_EXPORT(SN_folo, Array_SN_folo),
	    HLL_TODO_EXPORT(SN_fohi, Array_SN_fohi),
	    HLL_TODO_EXPORT(SS_fweq, Array_SS_fweq),
	    HLL_TODO_EXPORT(SS_fwne, Array_SS_fwne),
	    HLL_TODO_EXPORT(SS_fwlo, Array_SS_fwlo),
	    HLL_TODO_EXPORT(SS_fwhi, Array_SS_fwhi),
	    HLL_TODO_EXPORT(SS_faeq, Array_SS_faeq),
	    HLL_TODO_EXPORT(SS_fane, Array_SS_fane),
	    HLL_TODO_EXPORT(SS_falo, Array_SS_falo),
	    HLL_TODO_EXPORT(SS_fahi, Array_SS_fahi),
	    HLL_TODO_EXPORT(SS_foeq, Array_SS_foeq),
	    HLL_TODO_EXPORT(SS_fone, Array_SS_fone),
	    HLL_TODO_EXPORT(SS_folo, Array_SS_folo),
	    HLL_TODO_EXPORT(SS_fohi, Array_SS_fohi),
	    HLL_EXPORT(NV_sceq, Array_NV_sceq),
	    HLL_TODO_EXPORT(NV_scne, Array_NV_scne),
	    HLL_TODO_EXPORT(NV_sclo, Array_NV_sclo),
	    HLL_TODO_EXPORT(NV_schi, Array_NV_schi),
	    HLL_TODO_EXPORT(NV_scra, Array_NV_scra),
	    HLL_TODO_EXPORT(SV_sceq, Array_SV_sceq),
	    HLL_TODO_EXPORT(SV_scne, Array_SV_scne),
	    HLL_TODO_EXPORT(SV_sclo, Array_SV_sclo),
	    HLL_TODO_EXPORT(SV_schi, Array_SV_schi),
	    HLL_TODO_EXPORT(SV_scra, Array_SV_scra),
	    HLL_TODO_EXPORT(NN_sclowest, Array_NN_sclowest),
	    HLL_TODO_EXPORT(NN_schighest, Array_NN_schighest),
	    HLL_TODO_EXPORT(NS_sclowest, Array_NS_sclowest),
	    HLL_TODO_EXPORT(NS_schighest, Array_NS_schighest),
	    HLL_TODO_EXPORT(SN_sclowest, Array_SN_sclowest),
	    HLL_TODO_EXPORT(SN_schighest, Array_SN_schighest),
	    HLL_TODO_EXPORT(SS_sclowest, Array_SS_sclowest),
	    HLL_TODO_EXPORT(SS_schighest, Array_SS_schighest),
	    HLL_TODO_EXPORT(VN_add, Array_VN_add),
	    HLL_TODO_EXPORT(VN_and, Array_VN_and),
	    HLL_TODO_EXPORT(VN_or, Array_VN_or),
	    HLL_TODO_EXPORT(VS_add, Array_VS_add),
	    HLL_TODO_EXPORT(VS_and, Array_VS_and),
	    HLL_TODO_EXPORT(VS_or, Array_VS_or)
	    );
