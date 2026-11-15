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

// 堆空闲槽链表
// 记录 `heap` 数组里当前没被占用的下标。
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
		/* [FIX D100] xrealloc 不会把新增区域清零，heap[i].seq 是未初始化的垃圾。
		 * heap_alloc_slot() 里 D83 那条守卫会跳过 seq != 0 的槽，
		 * 于是每个刚扩出来的槽都可能被误读成「还活着」，
		 * 让 heap_free_ptr 冲过数组末尾、把堆写坏 ——
		 * 就是 GlobalGameTimer 构造时看到的那个 calloc() 段错误。 */
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

/* [MEM] 分配/释放的累计计数，由 [mem]（XSYS4_MEM_STAT）打印。
 * 两者之差就是 D95 的代价：每个被释放的槽都永久退役，
 * 所以槽数组只会一直变大。 */
size_t heap_alloc_count = 0;
size_t heap_free_count = 0;

/* [MEM-GUARD] 存活槽数的硬上限。正常游戏峰值约 4 万；
 * 地图装载风暴（[MAP-LOAD-STORM]）会在 13 秒内把它推过 80 万并继续涨，
 * 这才是把内存吃干、被系统 kill 掉的原因。打开槽回收之后，
 * heap_free_ptr 就精确等于存活槽数，所以这是一个精确度量。
 * 在一个小而可预期的规模上大声失败，可以把「吃光内存」
 * 变成一条可复现、且能指名肇事函数的 VM 错误。
 * 报错信息里会带肇事函数名。
 * XSYS4_HEAP_LIVE_LIMIT=0 关闭这条守卫（不推荐）。 */
#define HEAP_LIVE_LIMIT_DEFAULT 500000

static size_t heap_live_limit(void)
{
	static long limit = -1;
	if (limit < 0) {
		const char *e = getenv("XSYS4_HEAP_LIVE_LIMIT");
		limit = (e && *e) ? atol(e) : HEAP_LIVE_LIMIT_DEFAULT;
	}
	return limit > 0 ? (size_t)limit : 0;
}

int32_t heap_alloc_slot(enum vm_pointer_type type)
{
	heap_alloc_count++;
	{
		size_t lim = heap_live_limit();
		if (lim && heap_free_ptr >= lim)
			VM_ERROR("live object limit exceeded (%zu slots, limit %zu): "
				 "runaway allocation, fno=%d ip=0x%zX. See "
				 "[MAP-LOAD-STORM] in TECH_DEBT.md; "
				 "raise with XSYS4_HEAP_LIVE_LIMIT",
				 heap_free_ptr, lim, hll_current_fno(), instr_ptr);
	}
	if (heap_free_ptr >= heap_size) {
		heap_grow(heap_size+HEAP_ALLOC_STEP);
	}

	int32_t slot = heap_free_stack[heap_free_ptr++];
	if (slot > 0 && slot == heap_watch_slot())
		NOTICE("[SLOT] alloc slot=%d type=%d ref=%d ip=0x%zX fno=%d",
		       slot, type, heap[slot].ref, instr_ptr, hll_current_fno());
	if (getenv("XSYS4_ARR_DBG") && slot == 400)
		NOTICE("[ARR] slot400 alloc type=%d", type);
	/* 槽 0 是全局变量页（PUSHGLOBALPAGE 压的就是字面量 0）。
	 * 绝不能把它回收给普通分配，否则 heap[0].page 会被结构体/数组盖掉，
	 * 全局读全部错乱（D54）。
	 * 另外 D83 曾有一条「跳过 seq != 0 的槽」的绕法，现已删除：
	 * 它其实是在补偿 D104/D106 修掉的所有权不平衡
	 * （数组类型变量初值为 0，Array.Alloc 的结果丢失，
	 * 下一个下标表达式就别名到了全局页）。
	 * 删之前已验证：关掉该跳过逻辑跑 25 秒稳定。 */
	while (slot == 0) {
		if (heap_free_ptr >= heap_size)
			heap_grow(heap_size + HEAP_ALLOC_STEP);
		slot = heap_free_stack[heap_free_ptr++];
	}
	/* 回收来的槽不能留着上一任的悬空指针：
	 * 只分配槽、不调用 heap_set_page 的调用方，
	 * 否则会读到一张过期的页，把对象身份搞错。 */
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
	/* [HACK-POC D95] 故意**不**回收这个槽。
	 *
	 * 兰斯10 的 Parts/Asra 层在对象释放后仍握着旧槽号
	 * （槽随后被重新发出去，页类型在持有者眼皮底下就变了）。
	 * 那会引出一串症状：一张 LOCAL 页被交给 CPartsFunctionSet@Init(3937)、
	 * 一张 DELEGATE 页被当成 `this`（heap[1]，越界 1/8）、
	 * CASTimerManager::handles 塌成 nr_vars=0。
	 * 它们都是同一件事：「回收后的槽仍被引用」。
	 *
	 * 让槽泄漏，这一整类问题就都不存在了：分配只增不减。
	 * 代价是一局几 MB；真正的修法是找出那处不平衡的 unref，
	 * 修好之后再把回收打开。 */
	heap[slot].seq = 0;
	/* [L1] XSYS4_RECYCLE_SLOTS=1 可恢复槽回收，用于受控 A/B。
	 * 默认关：D134 消掉了大部分 double free，
	 * 但只要还有过期持有者，它就会读到一个刚被复用的槽
	 * （类型不同、对象不同），而不是一个死槽。
	 * 只有等 [ARRAY-COPY] 与编队界面 core dump 都清掉、
	 * 且全量基线保持一致之后，才考虑翻默认值。 */
	heap_free_count++;
	/* [MEM] 2026-11-13：回收默认**打开**。D95 说泄漏「一局几 MB」，实测不是：
	 * 47 秒游戏就把槽数从 4096 推到 348160（约 7k 次分配/秒，
	 * 槽数组 + 空闲栈约 390 MB/小时），长会话最终被推进 swap 然后 OOM。
	 * D134 已经消掉了这个 hack 当初防的 double free 根因，
	 * 且 A/B（recA/recFull）显示打开回收后各项指标完全一致。
	 * 如果真出现过期持有者，用 XSYS4_RECYCLE_SLOTS=0
	 * 退回旧的「永不回收」行为 —— 那是 D95 留下的最后一道保险。
	 * 默认不再走它。 */
	static int recycle = -1;
	if (recycle < 0) {
		const char *e = getenv("XSYS4_RECYCLE_SLOTS");
		recycle = (e && !strcmp(e, "0")) ? 0 : 1;
	}
	if (recycle && slot > 0 && heap_free_ptr > 1)
		heap_free_stack[--heap_free_ptr] = slot;
}

static void heap_double_free(int32_t slot)
{
	/* [D133] XSYS4_DF_DBG=1：打印**过度 unref 的调用方**。
	 * instr_ptr 是当前 AIN 指令，call_stack 栈顶给出所属函数号，
	 * 于是 ip 直方图能告诉我们哪条指令路径 unref 了不属于它的槽。
	 * 默认关且有上限：一次跑测能产生 13720 次 double free，
	 * 不限流会把日志其余部分全埋掉。 */
	static int df_on = -1;
	static int df_n = 0;
	if (df_on < 0)
		df_on = getenv("XSYS4_DF_DBG") ? 1 : 0;
	if (df_on) {
		int max = 400;
		const char *e = getenv("XSYS4_DF_MAX");
		if (e && *e)
			max = atoi(e);
		df_n++;
		if (df_n <= max) {
			struct page *pg = heap[slot].type == VM_PAGE ? heap[slot].page : NULL;
			NOTICE("[df] n=%d slot=%d ref=%d seq=%u type=%s ip=0x%zX fno=%d csp=%d page=%p nr=%d hll=%d",
			       df_n, slot, heap[slot].ref, heap[slot].seq,
			       vm_ptrtype_string(heap[slot].type), instr_ptr,
			       call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
			       call_stack_ptr, (void *)pg, pg ? pg->nr_vars : -1,
			       hll_current_fno());
		} else if (df_n % 2000 == 0) {
			NOTICE("[df] ... %d double frees so far (detail capped at %d)", df_n, max);
		}
	}
#ifdef DEBUG_HEAP
		WARNING("double free of slot %d (%s)\nOriginally allocated at %X\nOriginally freed at %X",
			 slot, vm_ptrtype_string(heap[slot].type),
			 heap[slot].alloc_addr, heap[slot].free_addr);
#else
		WARNING("double free of slot %d (%s)", slot, vm_ptrtype_string(heap[slot].type));
#endif
}

/* [DBG] XSYS4_SLOT_ID=<n> 端到端跟踪一个堆槽（alloc / set_page /
 * ref / unref），用来定位不平衡的 unref，且不会淹掉日志。 */
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
	/* D79：对已释放槽重复 unref（例如两个局部变量共享同一个 VM_STRING，
	 * 见 CollectionSaveData::Save）。exit_unref() 已经是幂等的，
	 * 让 heap_unref 与它保持一致。 */
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
	/* [D134] 真正致命的那次释放（ref 1 → 0）。
	 * 「是哪张页」和「是哪条指令」对上。 */
	static int free_dbg = -1;
	if (free_dbg < 0)
		free_dbg = getenv("XSYS4_DF_FREE_DBG") ? 1 : 0;
	if (free_dbg && slot == heap_watch_slot()) {
		struct page *pg = heap[slot].type == VM_PAGE ? heap[slot].page : NULL;
		NOTICE("[df-free] fatal slot=%d ref=%d ip=0x%zX fno=%d csp=%d pg=%p ptype=%d idx=%d nr=%d",
		       slot, heap[slot].ref, instr_ptr,
		       call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
		       call_stack_ptr, (void *)pg, pg ? pg->type : -1,
		       pg ? pg->index : -1, pg ? pg->nr_vars : -1);
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
		/* 没有负载的字符串槽是合法状态：ffi.c 里 HLL 字符串返回的守卫
		 * 在绑定交回的东西不是 `struct string *` 时就会存 NULL。
		 * 也就是说这里允许 s == NULL。 */
		if (heap[slot].s)
			free_string(heap[slot].s);
		break;
	}
	heap[slot].ref = 0;
	/* 上面已经释放了负载的所有权；让这个槽保持不含指针，
	 * 这样任何 use-after-free 都会当场炸掉，
	 * 而不是悄悄复用旧对象。 */
	heap[slot].page = NULL;
	heap[slot].s = NULL;
	heap_free_slot(slot);
}

// XXX：heap_unref 的特殊版本，它不调用析构函数
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
				/* [L1] 与 variable_fini() 用同一个所有权谓词。 */
				if (!variable_owns_ref(variable_type(page, i, NULL, NULL)))
					continue;
				if (page->values[i].i == -1)
					continue;
				exit_unref(page->values[i].i);
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
	/* [HACK-POC D96] 容忍死槽而不是中止：Asra 层在释放后仍引用委托页
	 * （parts::AddErasingLayerEvent / CModeMark@Init 里的 DG_PLUSA）。
	 * 调用方本来就会处理 NULL。
	 * 真正修法：让委托活到最后一个持有者放手。 */
	if (unlikely(!page_index_valid(index))) {
		/* [L1] D96 假设「调用方都会处理 NULL」，但编队界面的 core dump
		 * （logs/live_fullA.log）证明至少有一个不会。
		 * 把 AIN 指令指针和所属函数号带进已有的告警里，
		 * 这样 ain_at atf:<ip> 能直接点名那个调用方。 */
		WARNING("Invalid page index: %d (type=%d) ip=0x%zX fno=%d csp=%d",
			index, heap_index_valid(index) ? heap[index].type : -1,
			instr_ptr,
			call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1,
			call_stack_ptr);
		return NULL;
	}
	return heap[index].page;
}

struct string *heap_get_string(int index)
{
	/* [D123] index <= 0 是 AIN 侧的"空字符串 / 未初始化引用"哨兵。
	 * -1 常见于比较（LeaderCard@IsExist 的 S_NOTE）；0 是保留的
	 * GLOBAL_PAGE 槽，heap_string_assign() 早已按 slot<=0 直接跳过（D43），
	 * 但 S_ASSIGN 会先调本函数取右值，于是 `QuestMapMiniCardView@UpdateCard`
	 * 里 A = <未初始化> 直接 VM_ERROR。统一返回空串，让赋值/比较自然进行。
	 * 之前 stack_peek_string() 绕过本函数、把 -1 当数组下标用，行为也不一致。 */
	if (unlikely(index <= 0))
		return &EMPTY_STRING;
	/* [D124] 与 D96 对 dead page 的处理一致：越界/类型不符的槽降级成空串。
	 *
	 * 走到这里通常意味着某处持有已释放对象的索引——进入第一章任务地图后
	 * heap 里出现成片 double free，`Party@GetCardId` 从 LeaderCardCollection
	 * 取到的元素指向一个 type=-1 的槽（实测索引 1933133，越过 heap_size），
	 * 原来这里 VM_ERROR 直接杀进程。调用方（`QuestMapMiniCardView@UpdateCard`
	 * 的 S_ASSIGN）只想拿到"这张卡不存在"，空串正好表达这个语义。
	 * 告警限流，避免掩掉真正的问题；根因见 TECH_DEBT [INIT-DOUBLEFREE]。 */
	if (unlikely(!string_index_valid(index))) {
		static int warned;
		if (warned++ < 24)
			WARNING("Invalid string index: %d (type=%d, returning empty)",
			        index, heap_index_valid(index) ? heap[index].type : -1);
		return &EMPTY_STRING;
	}
	/* [D123] 槽被释放后 heap[slot].s 会被置 NULL，但 [HACK-POC D95] 故意
	 * 不回收槽号、type 也仍留 VM_STRING，于是 string_index_valid() 会通过，
	 * 调用方直接 strcmp(s->text) 就段错误。点击标题菜单进入新游戏初始化时，
	 * 一批 VM_PAGE 被重复 unref（日志里成片 double free），随后 `main`(18397)
	 * 的 S_NOTE 正好读到这样的死槽。
	 *
	 * 与 D96 对 dead page 的处理一致：返回空串让流程走下去，而不是崩。
	 * 真正的修复是找出那批重复 unref 的来源，见 TECH_DEBT [INIT-DOUBLEFREE]。 */
	if (unlikely(!heap[index].s)) {
		static int warned;
		if (warned++ < 12)
			WARNING("use of released string slot %d (returning empty)", index);
		return &EMPTY_STRING;
	}
	return heap[index].s;
}

struct page *heap_get_delegate_page(int index)
{
	/* AIN v12 用 -1 表示「空委托」。delegate_append() 本来就把 NULL 目标
	 * 当成「懒创建」，所以在这里把这个哨兵映射过去，
	 * 而不是去索引 heap[-1]。 */
	if (index < 0)
		return NULL;
	struct page *page = heap_get_page(index);
	/* 兰斯10 会把尚未实体化的委托槽（例如 7）传给 DG_* 指令。
	 * 与现有的 index<0 空哨兵同样处理，而不是中止（D49）。
	 * （原文如此：这里容忍的是一个「未落地的槽号」。） */
	if (unlikely(page && page->type != DELEGATE_PAGE))
		return NULL;
	return page;
}

void heap_set_page(int slot, struct page *page)
{
	if (slot < 0)
		return; /* empty-delegate sentinel: nothing to write back yet */
	/* D76：槽 0 放的是 GLOBAL_PAGE（PUSHGLOBALPAGE 压字面量 0）。
	 * 只有最初分配 GLOBAL_PAGE 时能写它；写进别的页类型会覆盖掉
	 * 所有全局变量的读取。 */
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
	/* 槽 0 是保留的非法槽（见 heap_unref）。兰斯10 v12 里，
	 * S_ASSIGN 可能带着未初始化的字符串左值走到这里；
	 * 释放 heap[0].s 会报 "Double free of string object"（D43）。 */
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
	/* 槽 0 是全局变量页。SR_ASSIGN 可能带着未初始化的结构体左值 0 进来，
	 * 通过它写入会把整个全局页替换成一个结构体
	 * （gdb: heap_struct_assign(0,1573)）。丢弃这次写；
	 * -1 才是真正的空指针错误（D54）。 */
	if (unlikely(lval <= 0))
		return;   /* 0 = global page slot, -1 = null struct lvalue (D54) */
	if (lval == rval)
		return;
	/* 兰斯10 会带着过期/非页的槽（例如存着字符串的 6）走到 SR_ASSIGN。
	 * 这条守卫在 release 构建里也要留 —— 否则下面 DEBUG_HEAP 的检查
	 * 会让 heap_get_page() 直接中止启动（D54）。 */
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
		// TODO：打印函数名
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
