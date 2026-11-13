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
#include <stdint.h>
#include "vm.h"
#include "xsystem4.h"
#include "system4/ain.h"

/* Defined next to the Array predicate helpers below; Array_Last_ needs them
 * before that point in the file. */
static bool array_has_predicate(void);
static bool array_predicate_matches(uintptr_t predicate, union vm_value elem);

/* [DBG] XSYS4_ARR_DBG=1 gates a compact page-lifecycle trace for array
 * bugs (D102): every op prints the page pointer and element count so a
 * container that silently gets replaced can be followed across the run. */
static int arr_dbg_on(void)
{
	static int v = -1;
	if (v < 0) {
		const char *e = getenv("XSYS4_ARR_DBG");
		v = (e && *e && *e != '0');
	}
	return v;
}
#define ARRDBG(...) do { if (arr_dbg_on()) NOTICE(__VA_ARGS__); } while (0)

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
	ARRDBG("[ARR] alloc: size=%d type=%d a=%d b=%d tag=%d fno=%d (out=%p -> %p)",
		size, type, a, b, hll_generic_result_type(), hll_current_fno(),
		(void*)out, out ? (void*)*out : NULL);
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
	ARRDBG("[ARR] alloc old=%p nr=%d st=%d -> size=%d", (void*)*out, (*out)->nr_vars, struct_type, size);
	/* A container declared with a struct_type holds struct objects, even
	 * when the generic Alloc argument carries no element type (-1).
	 * Without this, struct arrays degenerate to AIN_ARRAY_INT and their
	 * elements never get reference-counted (D21). */
	/* [D136] An element struct that is registered as an INTERFACE
	 * (ain->structures[n].is_interface) is a 2-slot value
	 * [object, vtable offset] everywhere else in the VM, and the bytecode
	 * indexes such a container with i*2 (e.g. QuestMapObjectLoader@Load
	 * @0x37cb90 does REFREF on this.member3 + i*2). Building it as a
	 * one-slot array<struct> made values[i*2+1] the NEXT element's object
	 * slot, which then went into `vtable[offset + 3]` as if it were a
	 * vtable offset -> "Out of bounds page index ... (type=3 nr_vars=10)".
	 * XSYS4_IFACE_ARR_FIX=0 restores the old behaviour for A/B. */
	static int iface_arr_fix = -1;
	if (iface_arr_fix < 0) {
		const char *e = getenv("XSYS4_IFACE_ARR_FIX");
		iface_arr_fix = (e && *e == '0') ? 0 : 1;
	}
	bool iface_elem = iface_arr_fix && struct_type >= 0 &&
			  struct_type < ain->nr_structures &&
			  ain->structures[struct_type].is_interface;
	if (struct_type >= 0 && !iface_elem)
		type = AIN_STRUCT;
	/* ARRAY_PAGE 的 a_type 必须存【数组类型】（AIN_ARRAY_STRUCT 等），
	 * 不是元素类型；否则 variable_type()/variable_fini() 会拿错类型 */
	/* AIN v12: an AIN_IFACE element is two consecutive value slots
	 * ([struct page, vtable offset]) and bytecode indexes them with i*2
	 * (CDebugFPSGraph@Create @0xb238e). size counts ELEMENTS. Use plain
	 * AIN_ARRAY_INT storage so variable_fini() does not unref the second
	 * slot of each element as if it were an object handle. */
	int iface_array = (hll_generic_result_type() == AIN_IFACE) || iface_elem;
	if (iface_array)
		n *= 2;
	struct page *p = alloc_page(ARRAY_PAGE,
	                            iface_array ? AIN_ARRAY_INT : elem_to_array_type(type), n);
	p->array.rank = 1;
	p->array.struct_type = struct_type;
	/* [D121] 结构体数组的元素必须在这里就构造出来（并跑构造函数）。
	 * Rance10 的 SceneTitle.m_btnMenu 是 array<TextButton>：Alloc(8) 之后
	 * 每个元素都该是一个构造好的 TextButton，成员 0 是 <vtable>。
	 * 只把 8 个槽清成 0 的话，场景创建事件里 `m_btnMenu[menu]` 拿到 0，
	 * 接着读 vtable 就变成 "Out of bounds page index" 并中止进程。 */
	if (struct_type >= 0 && !iface_array) {
		for (int i = 0; i < n; i++) {
			create_struct(struct_type, &p->values[i]);
			ARRDBG("[ARR]   elem[%d] = %d", i, p->values[i].i);
		}
	} else {
		/* [L2] alloc_page() zeroes the value slots, but 0 is HEAP SLOT 0,
		 * not "unset": for struct/array/delegate elements the unset value
		 * is -1 (see variable_initval). Leaving zeros here made an
		 * unallocated struct element look like a live int, and the save
		 * writer then tried to serialise slot 0 as a STRUCT_PAGE
		 * (observed on ReplayCgInfoCollection.m_list, 567 elements).
		 * NOT flipped by default: with -1 elements the engine reaches a
		 * different pre-existing bug (QuestMapObjectLoader@Load calls a
		 * method on an element that was never assigned), so this is a
		 * separate task. XSYS4_ARR_UNSET_FIX=1 evaluates it. */
		static int unset_fix = -1;
		if (unset_fix < 0) {
			const char *e = getenv("XSYS4_ARR_UNSET_FIX");
			unset_fix = (e && *e == '1') ? 1 : 0;
		}
		if (unset_fix) {
			enum ain_data_type et = array_type(p->a_type);
			for (int i = 0; i < n; i++)
				p->values[i] = variable_initval(et);
		}
	}
	*out = p;
	ARRDBG("[ARR] alloc new=%p nr=%d st=%d at=%d", (void*)p, p->nr_vars, p->array.struct_type, p->a_type);
}

/* [D138] 单位换算点 —— 这一族 bug 的共同形状。
 *
 * realloc_array()/alloc_array() 的参数是**元素数**（和 AIN 的 A_REALLOC 一致，
 * 接口元素内部再乘 2 变成槽数）。但 Array.* 的每个 helper 手里只有 nr_vars，
 * 那是**槽数**。两边混用会怎样，实测：
 *
 *   QuestMapObjectViewColletion@InitView: list.PushBack(...)  // list 是
 *   array<ILoadableQuestMapObject>（接口元素 = 2 槽/元素）
 *   → 每轮 push 传 old_n+1（槽），realloc 再 ×2 ⇒ 0,2,6,14,30,… 翻倍增长，
 *     123 个元素的循环把数组撑到几十万槽，RSS 以 ~500 MB/s 涨到 9.5 GB，
 *     直接把整台机器推到 OOM。
 *
 * 所以：凡是要"长到这么多**槽**"的地方，先过这里换成元素数。
 */
static bool arr_is_iface_elems(struct page *p)
{
	return p->array.rank == 1 && p->array.struct_type >= 0 &&
	       p->array.struct_type < ain->nr_structures &&
	       ain->structures[p->array.struct_type].is_interface;
}

static int arr_want_elems(struct page *p, int want_slots)
{
	if (want_slots < 0)
		want_slots = 0;
	return arr_is_iface_elems(p) ? want_slots / 2 : want_slots;
}

/* [D138] Array.Free(array& a) 的语义是"清空"，不是"什么都不做"。
 *
 * 原来这里是空实现（注释写"让 GC 自己处理"），于是所有把 Free 当 Clear
 * 用的地方都变成了"只增不减"。最典型的一处：
 *
 *   QuestMap@CreateRoute():
 *     foreach (obj in MapObjects.Objects) {      // 123 个对象
 *         Array.Free(branchType);                // ← 期望清空
 *         branchType.PushBack(0); PushBack(1); PushBack(2);   // 每轮 3 个
 *         foreach (bt in branchType) { ... }     // 数量决定内层循环次数
 *     }
 *
 * 因为 Free 不起作用，branchType 每轮 +3（3,6,9,12,…），而内层 foreach
 * 的边界又是它自己的长度，于是内层循环永不结束 —— 实测 `Array.Numof`
 * 被调 35 万次、`live` 从 2 万涨到 50 万，就是 [MAP-LOAD-STORM]。
 *
 * 正确做法是"原地截断"：元素逐个 fini（该 unref 的 unref、该跑析构的跑），
 * 再把 nr_vars 归零，页本身保留（调用方随后还要 PushBack，置 NULL 会让
 * 后续 PushBack 拿到空页）。XSYS4_ARRAY_FREE_FIX=0 回到空实现做 A/B。 */
static void Array_Free_(struct page **a)
{
	static int fix = -1;
	if (fix < 0) {
		const char *e = getenv("XSYS4_ARRAY_FREE_FIX");
		fix = (e && *e == "0") ? 0 : 1;
	}
	if (!fix || !a || !*a)
		return;
	struct page *p = *a;
	if (p->type != ARRAY_PAGE)
		return;
	for (int i = p->nr_vars - 1; i >= 0; i--)
		variable_fini(p->values[i], variable_type(p, i, NULL, NULL), true);
	p->nr_vars = 0;
}

static int Array_Numof_(struct page **a, int rank)
{
	if (!a || !*a)
		return 0;
	int n = (*a)->nr_vars;
	/* Numof returns the ELEMENT count. #43 allocates 2 slots per
	 * AIN_IFACE element (bytecode indexes with i*2), so divide the slot
	 * count back for that tag (D46). */
	(void)rank;
	if (hll_generic_result_type() == AIN_IFACE)
		n /= 2;
	return n;
}

static bool Array_Empty_(struct page **a)
{
	return Array_Numof_(a, 0) == 0;
}


/* AIN Array f[62..65]: ref Last(ref array [, predicate]). */
static void *Array_Last_(struct page **a, uintptr_t predicate)
{
	struct page *p = a ? *a : NULL;
	if (!p || p->type != ARRAY_PAGE || p->nr_vars == 0)
		return NULL;
	if (!array_has_predicate())
		return &p->values[p->nr_vars - 1];
	for (int i = p->nr_vars - 1; i >= 0; i--)
		if (array_predicate_matches(predicate, p->values[i]))
			return &p->values[i];
	return NULL;
}

static void *Array_At_(struct page **a, int index)
{
	if (!a || !*a) {
		return NULL;
	}
	struct page *p = *a;
	if (index < 0 || index >= p->nr_vars) {
		ARRDBG("[ARR] at(%d) OOB p=%p nr=%d at=%d st=%d", index, (void*)p,
		       p->nr_vars, p->a_type, p->array.struct_type);
		return NULL;
	}
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
/* [D137] 谓词重载：f[48]/[44]/[46] 的第二个参数是 AIN 比较函数号（t95），
 * 不是值。以前这里直接 return -1/0（TODO D39 的桩），于是游戏里"有序数组
 * 插入"的循环永远收敛不了 —— 实测 `QuestMapRouteFinder@Find` 在 40 秒内
 * 调了 35 万次 Numof、2.7 万次 PushBack，把内存从 2 万推到 80 万对象
 * （[MAP-LOAD-STORM]）。
 *
 * 语义按 System4 的比较函数约定：`cmp(elem)` 返回 <0 / 0 / >0，
 * 把 elem 与闭包捕获的 key 做比较。返回 false 表示该元素无法比较，
 * 调用方按"未找到"处理，与其它桩的契约一致。 */
static bool pred_cmp_ok;

static int pred_cmp(struct page *p, int i, int func)
{
	union vm_value arg = p->values[i];
	union vm_value ret;
	pred_cmp_ok = false;
	bool ok = vm_call_hll_closure(func, &arg, 1, &ret);

	/* [D137] XSYS4_PRED_DBG=1：把"叫了谁、比哪个元素、返回什么"打出来。
	 * 判断依据只有两条：返回值是 <0/0/>0 的比较结果，还是 0/1 的布尔？
	 * 以及它是否随元素变化（恒定 = 约定不对，搜索永远走同一边）。 */
	static int dbg = -1, ndbg;
	if (dbg < 0) {
		const char *e = getenv("XSYS4_PRED_DBG");
		dbg = (e && *e && *e != '0') ? 1 : 0;
	}
	if (dbg && ndbg++ < 40) {
		const char *nm = (func > 0 && func < ain->nr_functions &&
				  ain->functions[func].name)
				 ? display_sjis0(ain->functions[func].name) : "?";
		/* 元素自身的 key 成员（struct 的第一个 int 成员）与被查 key 对照，
		 * 用来区分"元素没填"和"比较方向反了"。 */
		int ekey = -9999;
		struct page *ep = heap_index_valid(arg.i) ? heap[arg.i].page : NULL;
		if (ep && ep->type == STRUCT_PAGE && ep->nr_vars > 0)
			ekey = ep->values[0].i;
		NOTICE("[cmp] fno=%d %s elem[%d]=%d(struct nr=%d key=%d) -> ok=%d ret=%d",
		       func, nm, i, arg.i, ep ? ep->nr_vars : -1, ekey, (int)ok, ret.i);
	}

	if (!ok)
		return 0;
	pred_cmp_ok = true;
	if (ret.i == 0)
		return 0;
	return ret.i < 0 ? -1 : 1;
}

static bool pred_bound_enabled(void)
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("XSYS4_PRED_BOUND");
		on = (e && *e == '0') ? 0 : 1;
	}
	return on > 0;
}

/* 该调用是不是 t95（函数号）重载 */
static bool bound_is_predicate(int fno)
{
	return fno == 44 || fno == 46 || fno == 48;
}

static int Array_BinarySearch_(struct page **a, uint64_t key)
{
	if (!a || !*a)
		return -1;
	int fno = hll_current_fno();
	if (bound_is_predicate(fno) && pred_bound_enabled()) {
		/* 精确查找：cmp == 0 的下标 */
		struct page *p = *a;
		int lo = 0, hi = p->nr_vars - 1;
		while (lo <= hi) {
			int mid = lo + (hi - lo) / 2;
			int c = pred_cmp(p, mid, (int)key);
			if (!pred_cmp_ok)
				return -1;
			if (c == 0)
				return mid;
			if (c < 0)
				lo = mid + 1;
			else
				hi = mid - 1;
		}
		return -1;
	}
	(void)key;
	return -1;
}

/* AIN f[43..46] LowerBound/UpperBound(ref array, key|predicate) -> int.
 * Predicate overloads (t95) pass an AIN comparison-function number, which
 * is not a comparable value; full predicate dispatch is D39. Return a
 * valid insertion position (0) so the caller's Insert path stays in
 * bounds. The t74 overloads should do a real ordered lookup (D39). */
/* LowerBound：第一个 cmp >= 0 的位置；UpperBound：第一个 cmp > 0 的位置。
 * 两者都必须落在 [0, nr_vars] 内，调用方直接拿它当插入点。 */
static int pred_bound(struct page *p, int func, bool upper)
{
	int lo = 0, hi = p->nr_vars;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		int c = pred_cmp(p, mid, func);
		if (!pred_cmp_ok)
			return 0;
		if (c < 0 || (upper && c == 0))
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static int Array_LowerBound_(struct page **a, uint64_t key)
{
	int fno = hll_current_fno();
	if (a && *a && bound_is_predicate(fno) && pred_bound_enabled())
		return pred_bound(*a, (int)key, false);
	return 0;
}

static int Array_UpperBound_(struct page **a, uint64_t key)
{
	int fno = hll_current_fno();
	if (a && *a && bound_is_predicate(fno) && pred_bound_enabled())
		return pred_bound(*a, (int)key, true);
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
	union vm_value dim = { .i = arr_want_elems(p, n + 1) };
	struct page *np = realloc_array(p, 1, &dim, at, st, false);
	if (!np)
		return;
	for (int i = n; i > index; i--)
		np->values[i] = np->values[i - 1];
	np->values[index] = *(union vm_value *)&value;
	*a = np;
}

/* AIN v12 exposes several same-named overloads with different arities.
 * link_static_library binds them all to one C symbol, so dispatch on the
 * current fno (ffi.c) and read the arguments positionally. All scalars are
 * passed in 64-bit registers on AArch64, hence uintptr_t. (D62) */
static int Array_Copy_(uintptr_t p0, uintptr_t p1, uintptr_t p2, uintptr_t p3, uintptr_t p4)
{
	struct page **dst = NULL, **src = NULL;
	int dpos = 0, spos = 0, count = -1;
	switch (hll_current_fno()) {
	case 25: dst=(void*)p0; src=(void*)p1; break;
	case 26: dst=(void*)p0; dpos=(int)p1; src=(void*)p2; break;
	case 27: dst=(void*)p0; src=(void*)p1; spos=(int)p2; count=(int)p3; break;
	case 28: dst=(void*)p0; dpos=(int)p1; src=(void*)p2; spos=(int)p3; count=(int)p4; break;
	default: return 0;
	}
	if (!dst || !src || !*src || (*src)->type != ARRAY_PAGE) return 0;
	struct page *sp = *src;
	if (spos < 0) spos = 0;
	if (count < 0 || spos + count > sp->nr_vars) count = sp->nr_vars - spos;
	if (count <= 0) return 0;
	for (int i = 0; i < count; i++) {
		if (dpos + i >= (*dst)->nr_vars) break;
		(*dst)->values[dpos + i] = sp->values[spos + i];
	}
	return count;
}

static int Array_Fill_(uintptr_t p0, uintptr_t p1, uintptr_t p2, uintptr_t p3)
{
	struct page **a = (void*)p0;
	int from = 0, count = -1;
	uint64_t value;
	if (hll_current_fno() == 24) { from=(int)p1; count=(int)p2; value=(uint64_t)p3; }
	else { value = (uint64_t)p1; }
	if (!a || !*a || (*a)->type != ARRAY_PAGE) return 0;
	struct page *pg = *a;
	if (from < 0) from = 0;
	if (count < 0 || from + count > pg->nr_vars) count = pg->nr_vars - from;
	for (int i = 0; i < count; i++) pg->values[from + i] = *(union vm_value *)&value;
	return count < 0 ? 0 : count;
}
/* AIN f[54]/f[55]: void Unique(ref array[, predicate]) - dedupe in place.
 * Predicate overload falls back to raw-value comparison (D63). */
static void Array_Unique_(struct page **a, uintptr_t predicate)
{
	(void)predicate;
	if (!a || !*a || (*a)->type != ARRAY_PAGE) return;
	struct page *p = *a;
	int n = p->nr_vars, w = 0;
	for (int i = 0; i < n; i++) {
		bool dup = false;
		for (int j = 0; j < w; j++)
			if (p->values[j].i == p->values[i].i) { dup = true; break; }
		if (!dup) p->values[w++] = p->values[i];
	}
	if (w == n) return;
	union vm_value dim = { .i = arr_want_elems(p, w) };
	enum ain_data_type at = p->a_type;
	int st = p->array.struct_type;
	struct page *np = realloc_array(p, 1, &dim, at, st, false);
	if (np) *a = np;
}
/* AIN f[51]: array Where(ref array, predicate) -> array.
 * Full predicate dispatch needs vm_call (D63); stage 1 returns a shallow
 * copy of the whole array so the caller has a valid array to iterate. */
/* AIN f[29..34]: Sort/AscSort/DescSort/QuickSort(ref array[, cmp]) -> ref array.
 * Stage 1: sort a shallow copy by raw int values and return its slot.
 * Predicate overloads (t95) and descending order are not implemented yet;
 * a proper cmp dispatch needs vm_call (D78). */
/* [D134] Several stage-1 array HLL stubs return a NEW array page whose
 * values are copied verbatim from the source. That page is later torn down
 * by delete_page_vars(), which unrefs every reference-typed element. If the
 * copy did not take a reference of its own, the copy and the source both
 * release the same object: this is the 7-card cascade behind
 * [INIT-DOUBLEFREE] ([df-free] holder ptype=3 idx=17 nr=7, i=6, slot=41079).
 * Set XSYS4_ARRAY_REF_FIX=0 for an A/B comparison with the old behaviour. */
static int array_ref_fix(void)
{
	static int v = -1;
	if (v < 0) {
		const char *e = getenv("XSYS4_ARRAY_REF_FIX");
		v = (e && !strcmp(e, "0")) ? 0 : 1;
	}
	return v;
}

static bool array_elem_owns_ref(struct page *src, int i)
{
	/* [L1] Shared with variable_fini()/exit_unref() — one answer for
	 * "which element types need their own heap reference". */
	return variable_owns_ref(variable_type(src, i, NULL, NULL));
}

static void array_copy_values(struct page *dst, struct page *src)
{
	for (int i = 0; i < src->nr_vars; i++) {
		dst->values[i] = src->values[i];
		if (array_ref_fix() && dst->values[i].i > 0 &&
		    array_elem_owns_ref(src, i))
			heap_ref(dst->values[i].i);
	}
}

/* AIN f[4]/f[5]: ShallowCopy(ref array) -> array. Stage 1: copy the page
 * and return a fresh heap slot (D80). */
static int Array_ShallowCopy_(struct page **a)
{
	struct page *src = a ? *a : NULL;
	if (!src || src->type != ARRAY_PAGE)
		return -1;
	struct page *np = alloc_page(ARRAY_PAGE, src->a_type, src->nr_vars > 0 ? src->nr_vars : 1);
	np->array = src->array;
	np->nr_vars = src->nr_vars;
	array_copy_values(np, src);
	return heap_alloc_page(np);
}
static int Array_QuickSort_(struct page **a, uintptr_t predicate)
{
	(void)predicate;
	struct page *src = a ? *a : NULL;
	if (!src || src->type != ARRAY_PAGE)
		return -1;
	struct page *np = alloc_page(ARRAY_PAGE, src->a_type, src->nr_vars);
	np->array = src->array;
	array_copy_values(np, src);
	for (int i = 1; i < np->nr_vars; i++) {
		union vm_value key = np->values[i];
		int j = i - 1;
		while (j >= 0 && np->values[j].i > key.i) {
			np->values[j+1] = np->values[j];
			j--;
		}
		np->values[j+1] = key;
	}
	return heap_alloc_page(np);
}
static int Array_Where_(struct page **a, uintptr_t predicate)
{
	(void)predicate;
	if (!a || !*a || (*a)->type != ARRAY_PAGE) return -1;
	struct page *src = *a;
	struct page *np = alloc_page(ARRAY_PAGE, src->a_type, src->nr_vars);
	np->array = src->array;
	array_copy_values(np, src);
	return heap_alloc_page(np);
}
static int Array_Find_(uintptr_t p0, uintptr_t p1, uintptr_t p2, uintptr_t p3)
{
	int fno = hll_current_fno();
	bool ranged = (fno == 37 || fno == 41);
	struct page **a = (void*)p0;
	uint64_t value = ranged ? p3 : p1;
	int from = ranged ? (int)p1 : 0;
	int to = ranged ? (int)p2 : 0;
	if (!a || !*a)
		return -1;
	struct page *p = *a;
	if (ranged && (from < 0 || to > p->nr_vars || to < from)) return -1;
	int begin = ranged ? from : 0, end = ranged ? to : p->nr_vars;
	for (int i = begin; i < end; i++)
		if (elem_eq(p, i, &value)) {
			ARRDBG("[ARR] find p=%p nr=%d v=%llu -> %d", (void*)p, p->nr_vars,
			       (unsigned long long)value, i);
			return i;
		}
	ARRDBG("[ARR] find p=%p nr=%d v=%llu -> -1", (void*)p, p->nr_vars,
	       (unsigned long long)value);
	return -1;
}

static int Array_FindLast_(uintptr_t p0, uintptr_t p1, uintptr_t p2, uintptr_t p3)
{
	int fno = hll_current_fno();
	bool ranged = (fno == 38 || fno == 42);
	struct page **a = (void*)p0;
	uint64_t value = ranged ? p3 : p1;
	int from = ranged ? (int)p1 : 0;
	int to = ranged ? (int)p2 : 0;
	if (!a || !*a)
		return -1;
	struct page *p = *a;
	if (ranged && (from < 0 || to > p->nr_vars || to < from)) return -1;
	int begin = ranged ? from : 0, end = ranged ? to : p->nr_vars;
	for (int i = end - 1; i >= begin; i--)
		if (elem_eq(p, i, &value))
			return i;
	return -1;
}

static bool Array_IsExist_(struct page **a, uint64_t value)
{
	return Array_Find_((uintptr_t)a, value, 0, 0) >= 0;
}

static void Array_PushBack_(struct page **a, uint64_t value)
{
	/* A zero value is a legitimate element (0, false, the null string, an
	 * interface whose vtable offset is 0). The old `!value` guard silently
	 * dropped it, so `Array.PushBack(a, 0)` lost the element:
	 * CommonFrame@GetFramePointTypeList builds {0,1,2} and got {1,2}, which
	 * then indexed past the end of the array. */
	if (!a || !*a)
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
	/* XSYS4_ARR_TYPE_DBG=1: report elements whose heap type does not match the
	 * array's element type. A string array receiving a page slot (or vice
	 * versa) is how a "string" member ends up holding an object. */
	if (getenv("XSYS4_ARR_TYPE_DBG") && p->a_type == AIN_ARRAY_STRING &&
	    value > 0 && value < heap_size && heap[value].type != VM_STRING) {
		static int n;
		if (n++ < 20)
			NOTICE("[arrtype] push %d into string array nr=%d (slot type=%d) fno=%d",
			       (int)value, p->nr_vars, heap[value].type, hll_current_fno());
	}
	int old_n = p->nr_vars;
	/* p->a_type is already an ARRAY type (AIN_ARRAY_STRUCT, ...).
	 * realloc_array() expects that array type, not the element type:
	 * elem_to_array_type() on an array type falls through to AIN_ARRAY_INT
	 * and silently turns struct arrays into int arrays.
	 *
	 * A generic `array` still has no element kind; CALLHLL's third immediate
	 * carries the concrete type of this call, which is exactly what AIN uses
	 * to type such an array on its first store. */
	enum ain_data_type at = p->a_type;
	if (at == AIN_ARRAY || at == AIN_REF_ARRAY) {
		int tag = hll_generic_result_type();
		array_settle_type(p, tag >= 0 ? (enum ain_data_type)tag
					     : array_value_type(*(union vm_value *)&value));
		at = p->a_type;
	}
	int st = p->array.struct_type;
	bool iface = arr_is_iface_elems(p);
	union vm_value dim = { .i = arr_want_elems(p, old_n + (iface ? 2 : 1)) };
	struct page *np = realloc_array(p, 1, &dim, at, st, false);
	if (!np)
		return;
	if (iface) {
		/* 一个接口元素占两槽 [对象槽, vtable 偏移]。偏移不是调用方传进来的
		 * （HLL 只拿到对象槽），要按接口号从对象声明的接口表里反推 ——
		 * 与 Array.At 返回接口值时用的是同一个函数。 */
		int obj = -1, off = 0;
		if (!vm_iface_pair(st, (int)value, &obj, &off)) {
			obj = (int)value;
			off = 0;
		}
		np->values[old_n].i = obj;
		if (old_n + 1 < np->nr_vars)
			np->values[old_n + 1].i = off;
	} else {
		np->values[old_n] = *(union vm_value *)&value;
	}
	/* The container becomes an independent owner of a heap object.
	 * Take a reference for it: the caller still owns its own reference
	 * and will release it with DELETE. Without this, DELETE drops the
	 * last ref and the container is left holding a dead slot (D21). */
	enum ain_data_type elem = array_type(at);
	if ((elem == AIN_STRUCT || elem == AIN_STRING || elem == AIN_DELEGATE ||
	     elem == AIN_ARRAY) && value > 0 && value < heap_size &&
	    heap[value].ref > 0)
		heap_ref((int)value);
	*a = np;
}

static void Array_PopBack_(struct page **a)
{
	if (!a || !*a)
		return;
	struct page *p = *a;
	if (p->nr_vars > 0)
		p->nr_vars--;
}


static int Array_Numof_(struct page **a, int rank);

/* AIN declares EmplaceBack(ref array) with ONE argument. The old
 * two-argument C stub read an extra uninitialised register and wrote it
 * into the new element, producing a garbage "this" for CALLMETHOD.
 * The element is value-initialised according to the CALLHLL type tag. */
static void *Array_EmplaceBack_(struct page **a)
{
	if (!a || !*a)
		return NULL;
	struct page *p = *a;
	int old_n = p->nr_vars;
	int st = p->array.struct_type;
	int tag = hll_generic_result_type();
	/* A generic array takes the element kind of the value being added. */
	if (p->a_type == AIN_ARRAY || p->a_type == AIN_REF_ARRAY)
		array_settle_type(p, tag >= 0 ? (enum ain_data_type)tag : AIN_INT);
	ARRDBG("[ARR] emp p=%p nr=%d at=%d st=%d", (void*)p, old_n, p->a_type, st);
	bool iface = arr_is_iface_elems(p);
	union vm_value dim = { .i = arr_want_elems(p, old_n + (iface ? 2 : 1)) };
	struct page *np = realloc_array(p, 1, &dim, p->a_type, st, false);
	ARRDBG("[ARR] emp np=%p nr=%d", (void*)np, np ? np->nr_vars : -1);
	if (!np)
		return NULL;
	*a = np;
	if (tag == AIN_STRUCT && st >= 0)
		create_struct(st, &np->values[old_n]);
	else
		np->values[old_n] = variable_initval(tag >= 0 ? tag : AIN_INT);
	return &np->values[old_n];
}

static void Array_Realloc_(struct page **a, int size, uintptr_t extra)
{
	(void)extra;
	if (getenv("XSYS4_ARRAY_DBG"))
	if (!a)
		return;
	if (size < 0)
		size = 0;
	if (!*a) {
		if (size == 0)
			return; /* realloc_array() would free it immediately (D114) */
		struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, size);
		p->array.rank = 1;
		p->array.struct_type = -1;
		*a = p;
		return;
	}
	struct page *p = *a;
	if (p->type != ARRAY_PAGE)
		return;
	/* [D114] p->a_type is already an ARRAY type (AIN_ARRAY_STRUCT, ...);
	 * elem_to_array_type() on it falls through to AIN_ARRAY_INT and silently
	 * turns struct arrays into int arrays -- the exact bug already fixed in
	 * Array_PushBack_. realloc_array() wants the array type itself. */
	if (size == 0) {
		/* realloc_array(..., 0) calls delete_page_vars + free_page and
		 * returns NULL, leaving *a dangling. Keep a valid empty page:
		 * parts::detail::CallPartsUpdateEvent calls Realloc(0) every
		 * frame for the empty EndUpdateEvent list. */
		p->nr_vars = 0;
		return;
	}
	union vm_value dim = { .i = size };
	struct page *np = realloc_array(p, 1, &dim, p->a_type, p->array.struct_type, false);
	if (np)
		*a = np;
}

static void Array_ReallocInit_(struct page **a, int size, uint64_t value)
{
	Array_Realloc_(a, size, 0);
	if (!a || !*a)
		return;
	/* Fill with `value` even when it is zero: the freshly realloc'ed page is
	 * not guaranteed to be zeroed, and 0 is a legitimate fill. */
	struct page *p = *a;
	for (int i = 0; i < p->nr_vars; i++)
		p->values[i] = *(union vm_value *)&value;
}

static bool Array_Erase_(struct page **a, int index)
{
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

/* AIN Array f[15]: bool EraseAll(ref array, predicate).
 * Removes every element the predicate accepts; returns whether the array had
 * anything to examine (the caller distinguishes "emptied" from "was empty"). */
static int Array_EraseAll_(struct page **a, uintptr_t predicate)
{
	struct page *p = a ? *a : NULL;
	if (!p || p->type != ARRAY_PAGE)
		return 0;
	int before = p->nr_vars;
	int n = 0;
	for (int i = 0; i < before; i++) {
		if (!array_predicate_matches(predicate, p->values[i]))
			p->values[n++] = p->values[i];
	}
	p->nr_vars = n;
	return before > 0;
}

/* Array registers several same-named AIN functions (First / Last / Any / ...),
 * all bound to one C symbol. Whether the trailing predicate argument is
 * present, and therefore whether the extra uintptr_t parameter means anything,
 * is decided by the declaration of the function being executed. */
static bool array_has_predicate(void)
{
	int lib = ain_get_library(ain, "Array");
	int fno = hll_current_fno();
	if (lib < 0 || fno < 0 || fno >= ain->libraries[lib].nr_functions)
		return false;
	struct ain_hll_function *decl = &ain->libraries[lib].functions[fno];
	return decl->nr_arguments > 0 &&
		decl->arguments[decl->nr_arguments - 1].type.data == AIN_HLL_FUNC;
}

/* Evaluate the predicate on one element. `predicate` is the AIN function index
 * that ffi.c extracted from the [this, fno] pair the caller pushed; see
 * vm_call_hll_func() for how the closure environment is bound. */
static bool array_predicate_matches(uintptr_t predicate, union vm_value elem)
{
	union vm_value r;
	if (!vm_call_hll_func((int)predicate, elem, &r))
		return false;
	return r.i != 0;
}

/* AIN Array f[58..61]: ref First(ref array [, predicate]). */
static void *Array_First_(struct page **a, uintptr_t predicate)
{
	struct page *p = a ? *a : NULL;
	if (!p || p->type != ARRAY_PAGE || p->nr_vars == 0)
		return NULL;
	if (!array_has_predicate())
		return &p->values[0];
	for (int i = 0; i < p->nr_vars; i++)
		if (array_predicate_matches(predicate, p->values[i]))
			return &p->values[i];
	return NULL;
}

/* AIN Array f[20]: bool All(ref array, predicate). */
static int Array_All_(struct page **a, uintptr_t predicate)
{
	struct page *p = a ? *a : NULL;
	if (!p || p->nr_vars == 0)
		return 1;
	for (int i = 0; i < p->nr_vars; i++)
		if (!array_predicate_matches(predicate, p->values[i]))
			return 0;
	return 1;
}

/* AIN Array f[21..22]: bool Any(ref array [, predicate]). */
static int Array_Any_(struct page **a, uintptr_t predicate)
{
	struct page *p = a ? *a : NULL;
	if (!p || p->nr_vars == 0)
		return 0;
	if (!array_has_predicate())
		return 1;
	for (int i = 0; i < p->nr_vars; i++)
		if (array_predicate_matches(predicate, p->values[i]))
			return 1;
	return 0;
}

/* Append every element of `src` to `*dst`. The destination takes a reference
 * for each heap object it now owns; the source keeps its own (see
 * Array_PushBack_). */
static void array_append(struct page **dst, struct page *src)
{
	if (!dst || !*dst || !src || src->nr_vars == 0)
		return;
	struct page *p = *dst;
	if (p->type != ARRAY_PAGE || src->type != ARRAY_PAGE)
		return;
	int old_n = p->nr_vars;
	enum ain_data_type at = p->a_type;
	int st = p->array.struct_type;
	union vm_value dim = { .i = arr_want_elems(p, old_n + src->nr_vars) };
	struct page *np = realloc_array(p, 1, &dim, at, st, false);
	if (!np)
		return;
	memcpy(&np->values[old_n], src->values,
	       (size_t)src->nr_vars * sizeof(union vm_value));
	enum ain_data_type elem = array_type(at);
	if (elem == AIN_STRUCT || elem == AIN_STRING || elem == AIN_DELEGATE ||
	    elem == AIN_ARRAY) {
		for (int i = 0; i < src->nr_vars; i++) {
			int v = np->values[old_n + i].i;
			if (v > 0 && v < heap_size && heap[v].ref > 0)
				heap_ref(v);
		}
	}
	*dst = np;
}

/* AIN Array f[10]: void Concat(ref array, ref array) - append src to dst. */
static void Array_Concat_(struct page **dst, struct page **src)
{
	if (!src || !*src)
		return;
	array_append(dst, *src);
}

/* AIN Array f[3]: void Duplicate(ref array, ref array) - replace dst with a
 * copy of src. */
static void Array_Duplicate_(struct page **dst, struct page **src)
{
	if (!dst || !*dst || !src || !*src)
		return;
	struct page *d = *dst, *s = *src;
	if (d->type != ARRAY_PAGE || s->type != ARRAY_PAGE)
		return;
	union vm_value dim = { .i = arr_want_elems(d, s->nr_vars) };
	struct page *np = realloc_array(d, 1, &dim, d->a_type, d->array.struct_type,
					false);
	if (!np)
		return;
	memcpy(np->values, s->values, (size_t)s->nr_vars * sizeof(union vm_value));
	enum ain_data_type elem = array_type(d->a_type);
	if (elem == AIN_STRUCT || elem == AIN_STRING || elem == AIN_DELEGATE ||
	    elem == AIN_ARRAY) {
		for (int i = 0; i < s->nr_vars; i++) {
			int v = np->values[i].i;
			if (v > 0 && v < heap_size && heap[v].ref > 0)
				heap_ref(v);
		}
	}
	*dst = np;
}

/* AIN Array f[53]: void Shuffle(ref array, int seed).
 * Deterministic for a given seed, so a save/load pair replays identically. */
static void Array_Shuffle_(struct page **a, int seed)
{
	struct page *p = a ? *a : NULL;
	if (!p || p->type != ARRAY_PAGE || p->nr_vars < 2)
		return;
	uint32_t state = seed > 0 ? (uint32_t)seed : 1u;
	for (int i = p->nr_vars - 1; i > 0; i--) {
		state = state * 1103515245u + 12345u;
		int j = (int)((state >> 16) % (uint32_t)(i + 1));
		union vm_value tmp = p->values[i];
		p->values[i] = p->values[j];
		p->values[j] = tmp;
	}
}

/* AIN Array f[16]: ref array Remain(ref array, predicate).
 * Drops every element the predicate rejects, in place. Rance10 never reads the
 * result (no CALLHLL site for lib 5 func 16), so only the mutation is
 * implemented. */
static int Array_Remain_(struct page **a, uintptr_t predicate)
{
	struct page *p = a ? *a : NULL;
	if (!p || p->type != ARRAY_PAGE)
		return 0;
	int n = 0;
	for (int i = 0; i < p->nr_vars; i++) {
		if (array_predicate_matches(predicate, p->values[i]))
			p->values[n++] = p->values[i];
	}
	p->nr_vars = n;
	return 0;
}

/* AIN Array f[66..73]: ref Min/Max(ref array [, predicate]).
 * Without a predicate the elements are ordered by value; with one the
 * predicate acts as a "less than" comparator, as in Sort. */
static void *array_extreme_(struct page **a, uintptr_t predicate, bool want_max)
{
	struct page *p = a ? *a : NULL;
	if (!p || p->type != ARRAY_PAGE || p->nr_vars == 0)
		return NULL;
	int best = 0;
	if (!array_has_predicate()) {
		for (int i = 1; i < p->nr_vars; i++) {
			bool less = p->values[i].i < p->values[best].i;
			if (less == want_max)
				best = i;
		}
	} else {
		for (int i = 1; i < p->nr_vars; i++) {
			union vm_value args[2] = {
				want_max ? p->values[best] : p->values[i],
				want_max ? p->values[i] : p->values[best],
			};
			union vm_value r;
			if (vm_call_hll_closure((int)predicate, args, 2, &r) && r.i)
				best = i;
		}
	}
	return &p->values[best];
}

static void *Array_Min_(struct page **a, uintptr_t predicate)
{
	return array_extreme_(a, predicate, false);
}

static void *Array_Max_(struct page **a, uintptr_t predicate)
{
	return array_extreme_(a, predicate, true);
}

HLL_LIBRARY(Array,
	    HLL_EXPORT(EraseAll, Array_EraseAll_),
	    HLL_EXPORT(At, Array_At_),
    HLL_EXPORT(Last, Array_Last_),
	    HLL_EXPORT(Find, Array_Find_),
	    HLL_EXPORT(BinarySearch, Array_BinarySearch_),
	    HLL_EXPORT(Insert, Array_Insert_),
	    HLL_EXPORT(Copy, Array_Copy_),
	    HLL_EXPORT(ShallowCopy, Array_ShallowCopy_),
	    HLL_EXPORT(Sort, Array_QuickSort_),
	    HLL_EXPORT(AscSort, Array_QuickSort_),
	    HLL_EXPORT(DescSort, Array_QuickSort_),
	    HLL_EXPORT(QuickSort, Array_QuickSort_),
	    HLL_EXPORT(Where, Array_Where_),
	    HLL_EXPORT(Unique, Array_Unique_),
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
	    HLL_TODO_EXPORT(VS_or, Array_VS_or),
	    HLL_EXPORT(First, Array_First_),
	    HLL_EXPORT(Last, Array_Last_),
	    HLL_EXPORT(Any, Array_Any_),
	    HLL_EXPORT(All, Array_All_),
	    HLL_EXPORT(Min, Array_Min_),
	    HLL_EXPORT(Max, Array_Max_),
	    HLL_EXPORT(Concat, Array_Concat_),
	    HLL_EXPORT(Duplicate, Array_Duplicate_),
	    HLL_EXPORT(Shuffle, Array_Shuffle_),
	    HLL_EXPORT(Remain, Array_Remain_)
	    );
