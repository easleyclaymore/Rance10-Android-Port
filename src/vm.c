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

#define VM_PRIVATE

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include <assert.h>
#include <SDL3/SDL.h> // for system.MsgBox

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

// 当 IP 被设为 VM_RETURN 时，虚拟机停机
#define VM_RETURN 0xFFFFFFFF

/*
 * 注意：当前实现是朴素的字节码解释器。
 *       原版 System40.exe 用的是 JIT 编译器，我们也该换成 JIT。
 */

// 操作数栈
union vm_value *stack = NULL; // the stack
int32_t stack_ptr = 0;        // pointer to the top of the stack
static size_t stack_size;     // current size of the stack

// 函数调用帧的栈
struct function_call call_stack[4096];
int32_t call_stack_ptr = 0;

struct ain *ain;
size_t instr_ptr = 0;

/* [D118] lambda 帧的父局部页，按 lambda 的函数号索引。
 * DG_NEW_FROM_METHOD 记下创建它的那一帧（被捕获的变量就活在那里）；
 * X_GETENV 再把它读回来。用 heap_ref 让那一帧保持存活。 */
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

// 读取 ADDR 处的操作码。
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

/* [D136] XSYS4_LVAR_DBG=<函数号>:<变量号>：点名「是哪个指令写了某函数
 * 的某个局部变量」，并把写入的值打出来。造它是为了回答「谁把一个堆槽
 * 号写进了接口局部变量的『虚表偏移』那一半」（QuestMapObjectLoader@Load
 * 的 6 号变量）；如果一个槽从头到尾没人写，那就表现为一片安静 —— 而这
 * 本身也就是答案。 */
/* [MEM] XSYS4_HLL_TRACE=<lib>,<fno>：见 CALLHLL 分支的说明。 */
/* [D234] XSYS4_HLL_HIST=<库号>：每秒打印一次该库各函数的调用次数。
 *
 * 「卡在某个界面」时最需要知道的不是某一帧的栈，而是**这一秒里游戏在轮询
 * 什么**：是 GetClickNumber 一直问不到，还是在等某个动画帧号，还是在问
 * 某个 IsFinished。单个探针只能证明某一次调用，直方图能排出权重。 */
#define HLL_HIST_MAX 2048
static int hll_hist_lib = -2;
static unsigned hll_hist_cnt[HLL_HIST_MAX];
static time_t hll_hist_last;

static void hll_hist_note(int lib, int fno)
{
	if (hll_hist_lib == -2) {
		const char *e = getenv("XSYS4_HLL_HIST");
		hll_hist_lib = e && *e ? atoi(e) : -1;
	}
	if (hll_hist_lib < 0 || lib != hll_hist_lib)
		return;
	if (fno >= 0 && fno < HLL_HIST_MAX)
		hll_hist_cnt[fno]++;
	time_t now = time(NULL);
	if (now == hll_hist_last)
		return;
	hll_hist_last = now;
	char buf[480];
	int n = 0;
	for (int i = 0; i < HLL_HIST_MAX && n < (int)sizeof(buf) - 40; i++) {
		if (hll_hist_cnt[i])
			n += snprintf(buf + n, sizeof(buf) - n, " %d:%u", i, hll_hist_cnt[i]);
	}
	NOTICE("[hllhist] lib=%d%s", lib, n ? buf : " （空）");
	memset(hll_hist_cnt, 0, sizeof(hll_hist_cnt));
}

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

// 从栈上弹出一个引用，返回被引用对象的地址。
#define NULLREF_LOG(why) do { \
	if (getenv("XSYS4_NULLREF_DBG")) { \
		static int _n; \
		if (_n++ < 200) NOTICE("[nullw] %s ip=0x%zX", why, instr_ptr); \
	} } while (0)

/* [D119] REF 会记住它最后读到的那个成员；DG_PLUSA 靠它来惰性初始化
 * 一个空的委托成员（用 -1 作哨兵）。 */
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
	/* D52：兰斯10 的部件 UI 初始化期间（例如 ResetPartsUpdateEventLayerID
	 * @0x1f16f4）要容忍空引用和从未分配过的引用。
	 * 无论堆下标非法还是 ARRAY_PAGE 是 0 槽，都返回 -1 哨兵，而不是中止启动。
	 * 但这些对象的归属仍需要正确的初始化 —— 这是技术债，不是设计。
	 * 也就是说：返回哨兵只保证不崩，不保证对象可用。 */
	static union vm_value null_ref_slot = { .i = -1 };
	if (unlikely(!heap_index_valid(heap_index)))
		{ NULLREF_LOG("site1"); return &null_ref_slot; }
	/* D72：合法的槽也可能页指针是 NULL。 */
	if (unlikely(!heap[heap_index].page))
		{ NULLREF_LOG("site2"); return &null_ref_slot; }
	/* D72：部件 UI 初始化期间，任何 0 槽的页（不只是 ARRAY_PAGE 或下标 0）
	 * 都当作未初始化的引用。 */
	if (heap[heap_index].page && heap[heap_index].page->nr_vars == 0)
		{ NULLREF_LOG("site3"); return &null_ref_slot; }
	/* [D113] 泛型 HLL 引用（Array.First/Last/…）在被调方忽略谓词时，
	 * 可能交回一个没解析出来的 [页, -1] 对。
	 * 对这种值做 ASSIGN 不能让虚拟机上崩：目标是编译器生成的哑返回槽，
	 * 紧接着就会被 DELETE。
	 * 与 D52 用同一套「空引用槽」策略。 */
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
 * [D105] AIN v12 的 SR_ASSIGN 是「数组元素的结构体赋值」：
 * 栈上是 [数组, 下标, 值] 三个值。旧的二操作数实现只弹出 (值, 下标)，
 * 还把下标当成堆槽，于是 `global[i] = copy(struct)` 把 heap[i].page
 * 覆写成了拷贝出来的那张页。
 * menu::detail::Init 正是用 i=26 这么干的，而 26 就是当时活着的
 * CASTimerManager：管理器被换成一个毫不相干的 57 成员结构体，
 * CheckHandle 随后读到过期成员，帧计时器就停了。
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
	 * 拷贝它时不能去调 heap_get_page(-1)。 */
	if (v.i < 0)
		return v;
	/* [L1]「这到底是不是一个引用」用共享谓词判断；
	 * 下面的 switch 只决定各类引用怎么拷贝。 */
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

// 检查某函数的签名与某个委托类型是否兼容。
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
			/* D67：委托类型的局部变量必须先有一个真实的页槽，DG_ASSIGN 才拷得进去；
			 * 原来是 -1，拷贝被静默丢掉了。 */
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
	/* 调用期间让 `this` 保持存活（兰斯9 起）。
	 * 析构函数是在 unref 一个引用已归零的对象时被调起的；那里再加引用
	 * 会把它「复活」，等帧被拆掉时就变成 double free。
	 * 所以只在它本来就还活着时才接管所有权。 */
	/* AIN v12：一个 AIN_IFACE 值是 2 个值 [结构体页, 虚表偏移]，
	 * 装在专门的 IFACE_PAGE 里。当接口引用被当作 `this` 用时，
	 * 派发必须落到实现该接口的结构体上，而不是那个两槽包装。
	 * 这是类型驱动的解引用，不是数值上的猜法。 */
	if (slot > 0 && slot < heap_size && heap[slot].page &&
	    heap[slot].page->type == IFACE_PAGE) {
		int impl = heap[slot].page->values[0].i;
		if (impl > 0 && impl < heap_size && heap[impl].page)
			slot = impl;
	}
	/* 拒绝非堆值。绝不越界解引用 heap[槽号]。 */
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
	/* 只有 set_struct_page 真的加过引用时才释放 `this`。 */
	if (frame->struct_page_refed)
		heap_unref(frame->struct_page);
	heap_unref(frame->page_slot);
}

static void scenario_jump(int address)
{
	// 清空调用栈
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		unref_call_frame(&call_stack[i]);
	}
	call_stack_ptr = 0;
	instr_ptr = address;
}

static void scenario_call(int slot)
{
	int fno = heap[slot].page->index;
	// 清空调用栈
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
 * System 4 的调用约定：
 *   - 调用方按顺序压入实参
 *   - CALLFUNC 建立栈帧，把实参弹进局部页
 *   - 被调方把返回值压栈
 *   - RETURN 跳回返回地址（存在栈帧里）
 */
static void call_trace_probe(int fno, int ret_addr);

/* `fn` 的形参一共占多少个操作数栈槽。
 *
 * AIN 把一个 AIN_IFACE（以及 AIN_WRAP）值放在两个相邻的变量槽里
 * —— [对象, 虚表偏移] —— 并把第二个声明成 void。
 * 而 `nr_args` 只数逻辑形参，按它弹栈会让每个接口实参都剩半个在栈上，
 * 整个帧的局部变量随之错位一个槽。
 * 所以弹实参和初始化局部变量都必须按槽走，不能按形参走。
 * 结论：栈的遍历单位是槽，不是形参。 */
static int function_argument_slots(struct ain_function *fn)
{
	int slots = 0, args = 0;
	while (args < fn->nr_args && slots < fn->nr_vars) {
		enum ain_data_type t = fn->vars[slots].type.data;
		args++;
		slots++;
		/* 多槽形参把剩下的槽声明成 void。
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

/* 委托签名同样按这条规则。 */
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

/* [D340] `XSYS4_FN_WATCH` / `XSYS4_FN_WATCH_NAME` 必须挂在**共同入口**上。
 *
 * 这两个探针原来打在 `method_call` 里，于是 `CALLFUNC` 路径**一次都测不到**，
 * 制造出"某函数 0 次调用"的假象 —— 2026-11-19 就是这么误判
 * `EnemyCreator::Create`(24853) / `BattleInitializer::Init`(24823) 没执行的
 * （两者都是 `CALLFUNC`），差点把「敌人没被创建」写成结论。
 * 判据：探针要挂在 CALLFUNC 与 CALLMETHOD 都必经的那一层 —— `_function_call`。
 */
static void fn_watch_note(int fno, int nargs, int this_slot)
{
	if (!ain || fno <= 0 || fno >= ain->nr_functions)
		return;
	if (getenv("XSYS4_FN_WATCH_NAME")) {
		static char fwn_seen[40000];
		const char *pat = getenv("XSYS4_FN_WATCH_NAME");
		const char *nm = ain->functions[fno].name;
		if (nm && fno < (int)sizeof(fwn_seen) && strstr(nm, pat) &&
		    !fwn_seen[fno]) {
			static int nfwn;
			fwn_seen[fno] = 1;
			if (nfwn++ < 300)
				NOTICE("[fwn] fno=%d %s", fno, nm);
		}
	}
	if (getenv("XSYS4_FN_WATCH")) {
		static int w[16], nw = -1;
		static int cnt[16];
		if (nw < 0) {
			nw = 0;
			const char *q = getenv("XSYS4_FN_WATCH");
			while (q && *q && nw < 16) {
				w[nw++] = atoi(q);
				while (*q && *q != ',') q++;
				if (*q == ',') q++;
			}
		}
		for (int i = 0; i < nw; i++) {
			if (w[i] == fno) {
				if (cnt[i]++ < 40)
					NOTICE("[fwatch] fno=%d nargs=%d this=%d",
					       fno, nargs, this_slot);
				break;
			}
		}
	}
}

static int _function_call(int fno, int return_address)
{
	fn_watch_note(fno, (ain && fno > 0 && fno < ain->nr_functions)
				? ain->functions[fno].nr_args : 0, -2);
	/* [D114] 探针放在这里，而不是 function_call()：delegate_call() 会绕过
	 * function_call() 直接跳到 _function_call()，所以原来放的位置让每个
	 * 经 DG_CALL 派发的函数都显得「从没被调用过」。 */
	call_trace_probe(fno, return_address);
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
	// 初始化局部变量
	for (int i = function_argument_slots(f); i < f->nr_vars; i++) {
		if (f->vars[i].type.data == AIN_DELEGATE) {
			/* D67：委托类型的局部变量必须先有一个真实的页槽，DG_ASSIGN 才能拷进去；
			 * 原来是 -1，拷贝被静默丢掉了。 */
			heap[slot].page->values[i].i = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
		} else {
			heap[slot].page->values[i] = variable_initval_typed(&f->vars[i].type);
		}
		if (ain->version <= 1 && f->vars[i].type.data == AIN_STRUCT) {
			create_struct(f->vars[i].type.struc, &heap[slot].page->values[i]);
		}
	}
	// 跳到函数入口
	instr_ptr = ain->functions[fno].address;

	return slot;
}

/* [D111d] 每函数入口探针：XSYS4_CALL_TRACE=<函数号,函数号,...>
 * 打印前 3 次、之后每 200 次，这样「这个 AIN 函数到底跑过没有」
 * 不用开 trace 窗口就能回答。 */
static struct { int fno; int hits; int nret; int rets[8]; } call_trace[16];
static int call_trace_n = -1;

static void call_trace_probe(int fno, int ret_addr)
{
	if (call_trace_n < 0) {
		const char *env = getenv("XSYS4_CALL_TRACE");
		call_trace_n = 0;
		if (env) {
			const char *p = env;
			while (*p && call_trace_n < 16) {
				call_trace[call_trace_n].fno = atoi(p);
				call_trace[call_trace_n].hits = 0;
				call_trace[call_trace_n].nret = 0;
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
		/* [D160] **按调用站点去重**，不是按次数。
		 *
		 * 原来只打前 3 次 + 每 200 次一条：同一个函数的多个调用点里，
		 * 先跑的那个会把名额占满，后跑的点一条都打不出来。这一轮就被
		 * 坑了 —— `Parts@SetOnClickEvent` 前 3 条全是 FormationView 的
		 * 0x38D27C，看不出 `PartyCard@InitPartsDragTarget` 到底注册了没有。
		 * 现在：见到**新的 ret** 就打一条，每个 fno 记 8 个站点。 */
		bool new_site = false;
		bool seen = false;
		for (int k = 0; k < call_trace[i].nret; k++)
			if (call_trace[i].rets[k] == ret_addr)
				seen = true;
		if (!seen && call_trace[i].nret < 8) {
			call_trace[i].rets[call_trace[i].nret++] = ret_addr;
			new_site = true;
		}
		/* [D282] `XSYS4_CALL_TRACE_TOTAL=1`：函数**退出**时打一条真实总数。
		 * 光看上面那行限流后的日志条数会把"打印了几条"当成"调用了几次" ——
		 * D280 我就是这么把 `View_Update` 的 1400 次误读成 10 次的。
		 * 这个开关在 vm_exit 时统一汇总，不受限流影响。 */
		if (new_site || n <= 3 || n % 200 == 0) {
			struct ain_function *f = &ain->functions[fno];
			/* [D159] `instr_ptr` 在这里指向**被调函数自己**的第一条指令
			 * （0x...374 之类），拿它反汇编只会绕回原地 —— 上几轮据此
			 * 找不到"谁调了 426"。`_function_call` 的第二个参数才是调用者
			 * 的返回地址；调用栈里每一帧也有各自的 return_address。
			 *
			 * csp<=6 时把整条栈打出来：入口调用点必然是栈最浅的那次，
			 * 递归自调一眼就能分辨。 */
			NOTICE("[call] #%d %s (%d) ret=0x%zX csp=%d ip=0x%zX",
			       n, display_sjis0(f->name), fno, ret_addr,
			       call_stack_ptr, instr_ptr);
			if (call_stack_ptr <= 6) {
				for (int j = call_stack_ptr - 1, k = 0; j >= 0 && k < 6; j--, k++) {
					int cf = call_stack[j].fno;
					NOTICE("[call]    ^%d fno=%d %s ret=0x%zX", k, cf,
					       (cf > 0 && cf < ain->nr_functions)
					       ? display_sjis0(ain->functions[cf].name) : "?",
					       call_stack[j].return_address);
				}
			}
		}
	}
}

static void function_call(int fno, int return_address)
{
	/* AIN 把函数号 0 / 地址 0 保留给 NULL 函数。调用它是一个空操作：
	 * 把实参吃掉（其实一个也没有），既不建栈帧，也不跳到地址 0。
	 * 换句话说，这是一次「假装调用」：只有栈效果，没有控制流效果。
	 * 调用方的指令指针随后按 CALLFUNC/CALLMETHOD 正常的 ip_inc 前进。 */
	if (fno == 0 && ain->functions[0].address == 0) {
		struct ain_function *nf = &ain->functions[0];
		for (int i = 0; i < nf->nr_args; i++)
			stack_pop();
		/* CALLMETHOD/CALLFUNC 是 ip_inc == 0 的跳转类操作码；正常路径会在
		 * _function_call 内部推进 instr_ptr。既然这里跳过了那一步，
		 * 就自己把指令指针推到调用方的返回地址。 */
		instr_ptr = return_address;
		return;
	}

	int slot = _function_call(fno, return_address);

	// 弹出实参，存进局部页
	struct ain_function *f = &ain->functions[fno];
	int nslots = function_argument_slots(f);
	for (int i = nslots - 1; i >= 0; i--) {
		heap[slot].page->values[i] = stack_pop();
		/* 故意不用 variable_owns_ref()：这里只有 ref 类型的实参是借来的，
		 * 其余的都是所有权转移。把范围放宽已经被一次 A/B 证伪
		 * （double free 13720 → 13720，毫无变化），详见 page.h。 */
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
			/* [D307] 结构体实参也展开成员。
			 *
			 * 起因：`CallDelegateMouseClick(fs)` 的 `fs` 是
			 * `CPartsFunctionSet`，里面 `m0=m_Using`、`m1=m_PartsNumber`、
			 * `m7=<MouseLClickEvent>`。只打 `v0 i=8` 完全看不出
			 * 「这个 FunctionSet 属于哪个部件」「点击委托数组是不是空的」，
			 * 而这两条正是「点中了、消息也派发了、却什么都没发生」的全部原因。
			 * 委托数组打成 `[obj,fun]` 对，一眼能看出 nr=0。 */
			for (int i = 0; i < f->nr_vars && i < 24; i++) {
				int vs = lp->values[i].i;
				if (vs <= 0 || (size_t)vs >= heap_size)
					continue;
				struct page *vp = heap[vs].page;
				if (!vp || vp->type != STRUCT_PAGE)
					continue;
				/* [D323] 1024 字节**不够**：`CPartsFunctionSet` 有 42 个成员，
				 * 每个 delegate 成员还要展开 `<nr=.. [obj,fun]>`，第 14 个就被
				 * 截断了 —— 而事件数组在**后半段**（`Init` 清的是 28..42），
				 * 截断正好把要看的部分切掉，害得两轮都把「全空」当结论。
				 * 实测必须 4096 才装得下 42 个成员。 */
				char line[4096];
				int off = snprintf(line, sizeof(line), "[fn]     v%d struct '%s' nr=%d:",
						   i, vp->index >= 0 && vp->index < ain->nr_structures &&
						      ain->structures[vp->index].name
						      ? ain->structures[vp->index].name : "?",
						   vp->nr_vars);
				for (int k = 0; k < vp->nr_vars && k < 42 && off > 0 &&
				     off < (int)sizeof(line) - 200; k++) {
					int mv = vp->values[k].i;
					off += snprintf(line + off, sizeof(line) - off,
							" m%d=%d", k, mv);
					if (mv > 0 && (size_t)mv < heap_size && heap[mv].page &&
					    heap[mv].page->type == DELEGATE_PAGE) {
						struct page *dp = heap[mv].page;
						off += snprintf(line + off, sizeof(line) - off,
								"<nr=%d", dp->nr_vars);
						for (int q = 0; q + 1 < dp->nr_vars && q < 8; q += 2)
							off += snprintf(line + off, sizeof(line) - off,
									" [%d,%d]", dp->values[q].i,
									dp->values[q + 1].i);
						off += snprintf(line + off, sizeof(line) - off, ">");
					}
				}
				NOTICE("%s", line);
			}
			/* [D307] XSYS4_FN_DUMP_FSSCAN=1：扫描堆里所有
			 * `CPartsFunctionSet`（struct 299），把 `<MouseLClickEvent>`
			 * 非空的那些连 parts_no 一起打出来。
			 *
			 * 起因：点 `ReplayMemoDialog` 的「过后查看」(900000513) 时，
			 * 消息确实派发到了 `CallDelegateMouseClick`，但它的
			 * FunctionSet `m7<nr=0>` —— 一个委托都没有，于是"点了个寂寞"。
			 * 这张表直接回答「点击委托到底注册在哪个部件号上」，
			 * 比逐个猜 parts_no 快一个数量级。 */
			if (getenv("XSYS4_FN_DUMP_FSSCAN")) {
				for (size_t s2 = 1; s2 < heap_size; s2++) {
					struct page *pp = heap[s2].page;
					if (!pp || pp->type != STRUCT_PAGE || pp->index != 299 ||
					    pp->nr_vars < 8)
						continue;
					int pn = pp->values[1].i;
					int ev = pp->values[7].i;
					struct page *ep = (ev > 0 && (size_t)ev < heap_size)
					                  ? heap[ev].page : NULL;
					int en = ep ? ep->nr_vars : 0;
					if (en <= 0 && !getenv("XSYS4_FN_DUMP_FSALL"))
						continue;
					NOTICE("[fss] slot=%zu parts=%d using=%d click_ev=%d", s2,
					       pn, pp->values[0].i, en);
					for (int q = 0; ep && q + 1 < ep->nr_vars && q < 12; q += 2) {
						int f2 = ep->values[q + 1].i;
						NOTICE("[fss]   [obj=%d fun=%d %s]", ep->values[q].i, f2,
						       (f2 > 0 && f2 < ain->nr_functions &&
						        ain->functions[f2].name)
						       ? ain->functions[f2].name : "?");
					}
				}
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
	/* `this` 槽是在 function_call() 记下操作数深度之后才弹出的，
	 * 所以帧的基线要再低一个值。 */
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
/* [D192] 只调 StartMoveRoute 时也想拿 SceneQuestMap 的 this。 */
static bool auto_route_want;
static int auto_party_page = -1;
static uint32_t auto_party_delay;
/* [D344] 见 vm_pump_auto_battle_end() 的注释。 */
static int auto_battle_page = -1, auto_be_done, auto_be_frames;
/* [D349] XSYS4_AUTO_SKILL=<frames>[:<index>]：技能卡展开后直接派发第 index 个
 * `LargeSkillButton`，验证「选技能」链路（点击卡片这条路已证明走不通，
 * 见 D348：技能卡的绘制位置与部件矩形不一致）。 */
static int auto_skill_done, auto_skill_frames;

/* [D178] XSYS4_AUTO_CARD_CLICK=<idx>：编队界面稳定后直调
 * `SceneParty@OnClickCard(idx)`（fno 24300，1 个 int 参数）。
 *
 * 为什么需要后门：D177 之后编队界面能进能画，但 Xvfb 上 SDL 窗口在运行中
 * 从 X 树消失（root 0 children、focus 落回 root），xdotool 点击根本进不了
 * 输入层（`[inp] clicking=1` 一次都没有）。这是**测试脚手架**问题，不是
 * 加卡逻辑问题；按 AUTO_MAP/AUTO_RUNPARTY 的惯例把它解耦。
 *
 * 与 AUTO_PARTY 同理：方法必须带场景实例的 `this`，所以在 method_call 里
 * 记录 SceneParty 区段（24281..24317）调用时的 this 页号。默认关闭。
 *
 * [D179] 触发条件改成**事件**而不是墙钟。原来固定 12000ms，实测它把
 * `vm_call` 重入插进了 `Activity@Load` 的缝隙：那一刻
 * `Game/Party/OrganizationCardView` 还没装载、`PartyCardGroup@Init` /
 * `InitCardView`(24259) / `CreateLoadingQueue`(24247) 全都排在点击之后，
 * `m_cards` 是 `array nr=0` → `GetCardId(0)` 返回空串 →
 * `isOrganizableCard("")` 为假 → `OnClickCard` 在 0x397B6E 提前 RETURN。
 * 现在等最后一次 `InitCardView` 跑完再延时 acc_delay（默认 1500ms，
 * 可用 `XSYS4_AUTO_CARD_CLICK_DELAY` 调），每次调用都刷新时间戳。
 *
 * [D180] 支持连点：开局有卡的组织都已编入编队，单次点击只会走
 * `RemoveCardFromFormations`(24315)。移除之后该组织的
 * `Party@FindIndexFromOrganizationId` 应变为 -1，第二次点同位置的卡
 * 才会走 `AddCardToFormations`(24317) —— 用游戏自己的逻辑造出
 * 「未编入且可编组卡」，无需另找开局/存档。
 * `XSYS4_AUTO_CARD_REPEAT=<n>`（默认 1）控制连点次数。 */
static bool acc_want, acc_init, acc_done, acc_cards_ready, acc_switched, acc_in_call;
static bool acc_close;
static int acc_page = -1, acc_index, acc_org, acc_group_page = -1;
static int acc_repeat = 1, acc_count;
static uint32_t acc_delay, acc_ready_at;

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
		/* [D112] 接口派发：编译好的调用点用 REF 从接口的虚表里取出方法槽。
		 * 当虚表没解析出来时（D3），那次 REF 拿到的是虚表那个 ARRAY_PAGE
		 * 本身，而不是 vtable[offset]。
		 * 一个越界的「函数号」如果恰好是一张装着函数号的活数组页，那就没有歧义：
		 * 直接用它的第一个元素（偏移 0）。
		 * 这正是调用点想要的东西。 */
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
	/* [D344] 记录 SceneBattle 实例（fno 24880..24960 全属它，结构体号 732）。 */
	if (auto_battle_page < 0 && fno >= 24880 && fno <= 24960 &&
	    nargs + 1 <= stack_ptr) {
		int self = stack_peek(nargs + 1).i;
		if (self > 0 && (size_t)self < heap_size && heap[self].page &&
		    heap[self].page->type == STRUCT_PAGE &&
		    heap[self].page->index == 732) {
			auto_battle_page = self;
			NOTICE("[abe] SceneBattle instance = heap slot %d", self);
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
		/* [D192] 只开 AUTO_MAP_ROUTE 时也要记 this，但不能因此触发
		 * OpenParty —— 两者共用同一个实例页号，触发条件必须分开。 */
		if (getenv("XSYS4_AUTO_MAP_ROUTE"))
			auto_route_want = true;
	}
	if ((auto_party_want || auto_route_want) && fno >= 23586 && fno <= 23620 &&
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
	/* [D178] 记录 SceneParty 实例：24281..24317 整个区段都属于它，
	 * 编队界面一装载就会有方法跑，任意一个都能拿到同一个页号。 */
	if (!acc_init) {
		acc_init = true;
		const char *c = getenv("XSYS4_AUTO_PARTY_CLOSE");
		acc_close = c && *c && *c != '0';
		const char *e = getenv("XSYS4_AUTO_CARD_CLICK");
		acc_want = e != NULL || acc_close;
		acc_index = (e && *e) ? atoi(e) : 0;
		const char *d = getenv("XSYS4_AUTO_CARD_CLICK_DELAY");
		acc_delay = d && *d ? (uint32_t)atoi(d) : 1500;
		const char *o = getenv("XSYS4_AUTO_CARD_ORG");
		acc_org = o && *o ? atoi(o) : 0;
		const char *r = getenv("XSYS4_AUTO_CARD_REPEAT");
		acc_repeat = r && *r ? atoi(r) : 1;
		if (acc_repeat < 1)
			acc_repeat = 1;
	}
	/* [D179] 卡片视图初始化完成才是可点的时刻。InitCardView(24259) 会被
	 * 多个 PartyCardGroup 各调一次，每次都刷新时间戳 ⇒ 条件自然变成
	 * "最后一次 InitCardView 之后 acc_delay 毫秒"，不依赖固定墙钟。
	 * 同时记下 InitCardView 的 this（PartyCardGroup），供切组织用。
	 * [D180] 这里不因 acc_done 停：连点时移除卡片会再跑一次 InitCardView，
	 * 必须让它刷新时间戳，否则第二次点击会踩在旧卡片列表上。 */
	if (acc_want && fno == 24259) {
		acc_cards_ready = true;
		acc_ready_at = SDL_GetTicks() ? SDL_GetTicks() : 1;
		if (nargs + 1 <= stack_ptr) {
			int self = stack_peek(nargs + 1).i;
			if (self > 0 && (size_t)self < heap_size && heap[self].page &&
			    heap[self].page->type == STRUCT_PAGE)
				acc_group_page = self;
		}
	}
	if (acc_want && fno >= 24281 && fno <= 24317 &&
	    nargs + 1 <= stack_ptr) {
		int self = stack_peek(nargs + 1).i;
		if (self > 0 && (size_t)self < heap_size && heap[self].page &&
		    heap[self].page->type == STRUCT_PAGE)
			acc_page = self;
	}
	/* [L2] XSYS4_MC_SLOT_DBG=1：`this` 为什么会变成堆槽 1？
	 * CALLMETHOD 的立即数（nargs）是逻辑实参个数，而 function_call()
	 * 弹出的是 function_argument_slots(f) 个物理槽。
	 * [对象, 函数号] 这一对就压在这些实参下面，所以一旦数量对不上，
	 * 下面两次 stack_pop() 真正取到的东西就整体错位了。 */
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
	/* [L2] 函数号为 0 的 NULL 调用会走 function_call() 的空操作分支，
	 * 那个分支不建帧。而下面所有代码都假设「已经有一张新帧」：
	 * set_struct_page() 和 local.struct_ptr 于是会覆写调用方的 `this`，
	 * 此后该调用方的每一次 PUSHSTRUCTPAGE 都会解析到错误的对象。
	 * 在编队界面实测到：`this` 变成了堆槽 1（那个 3 成员的
	 * CASConstructionProcess），引出一整串「Out of bounds page index: 1/N」，
	 * 最后 core dump。
	 * 处理办法：把调用点的槽吃掉，然后直接返回，不碰任何帧。
	 * XSYS4_NULLMETHOD_FIX=0 可恢复旧行为，用于 A/B。 */
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
	/* `obj` 和 `fno` 夹在被调帧的基线与实参之间，
	 * 所以两个都弹完之后，帧的操作数深度才是最终值。 */
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
	/* D48/D71：obj 为空 -> 把拥有它的结构体实体化。优先用名字推出来的
	 * 结构体号；AIN v12 里 struct_type 字段是错的。
	 * [HACK-POC D83] 「`this` 解析到一个还活着、但已经不再承载 STRUCT/IFACE
	 * 页的槽」时也重建：这说明对象被释放了，而持有者还引着它的槽，
	 * 槽随后被回收复用（实测：一张 LOCAL_PAGE 被交给了
	 * CPartsFunctionSet@Init）。重建会丢掉旧状态，但对象本来就已经没了；
	 * 详见技术债 D83。 */
	/* 只有 LOCAL_PAGE 这一种才是已知的槽回收症状；对每一张非 STRUCT 页
	 * 都重建，会把活着的容器毁掉（CASTimerManager.handles 被换成一张
	 * 全新的空数组，于是每个句柄都失效了）。 */
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

/* D71：AIN v12 的 function->struct_type 不可信（CreateOld 报 0，
 * 但它其实属于 11 号结构体）。名字 NS::Struct@Method 里编码了
 * 所属结构体，所以 CALLMETHOD 没有 obj 时按名字解析。 */
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

/* AIN_IFACE / AIN_WRAP 占两个栈槽（[对象, 虚表偏移]）。
 * 所以返回类型是它们之一的委托，必须留下两个值；只留一个的话，
 * 调用方的 R_ASSIGN 会拿到一对错位的值，
 * 接口变量就永远停在 -1（D112）。 */
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

	// 栈：[arg0, ..., dg_page, dg_index, [返回值]]
	int return_values = delegate_return_slots(&ain->delegates[dg_no]);
	int dg_page = stack_peek(1 + return_values).i;
	int dg_index = stack_peek(0 + return_values).i;
	/* [L2] XSYS4_DG_PAGE_DBG=1：`dg_page` 有时会解析到一个还活着的
	 * VM_STRING 槽（heap_get_page 于是在编队界面 core dump 之前
	 * 正好报一句「Invalid page index (type=1)」）。
	 * 用它判定：到底是返回槽数目不对，还是 DG_CALLBEGIN 取错了栈槽。 */
	static int dgpage_dbg = -1;
	if (dgpage_dbg < 0) {
		const char *e = getenv("XSYS4_DG_PAGE_DBG");
		dgpage_dbg = e ? atoi(e) : 0;
	}
	/* =1 只打印类型不符的情况；=2 会跟踪某个委托号（这里是 170）的
	 * 每一次命中，这样能看见是哪一轮迭代把帧搞坏的。 */
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
                /* [D119] 40 次的上限在启动阶段就会被填满；长跑的点击诊断
                 * 允许再按 dg_no 过滤（见 §6.13）。 */
                const char *flt = getenv("XSYS4_DG_MISS_FILTER");
		if ((flt && atoi(flt) == dg_no && miss_n++ < 500) ||
		    (!flt && miss_n++ < 40)) {
			NOTICE("[dgm] MISS dg_no=%d page=%d idx=%d nr=%d obj0=%d seq0=%d now0=%d ip=0x%zX",
			       dg_no, dg_page, dg_index, snap_nr, snap_obj,
			       snap_seq, heap_get_seq(snap_obj), instr_ptr);
			/* [D159] 把整页三元组连 seq 一起打出来。
			 * 只有 obj0 时，`idx` 越界 和 `seq` 失效 这两种 MISS
			 * 看起来一模一样 —— 而它们的修法完全不同：
			 *   越界 ⇒ DG_CALLBEGIN/dg_index 算错（栈布局）；
			 *   失效 ⇒ 委托目标被提前回收（引用计数）。 */
			if (dp && dp->type == DELEGATE_PAGE) {
				for (int k = 0; k + 2 < dp->nr_vars && k < 24; k += 3) {
					int o = dp->values[k].i;
					NOTICE("[dgm]   [%d] obj=%d fun=%d seq=%d now=%d",
					       k / 3, o, dp->values[k+1].i, dp->values[k+2].i,
					       heap_get_seq(o));
				}
			}
		}
        }
	if (dg_ok) {
		/* [D114] XSYS4_DG_CALL_DBG=1：每帧跑的那个委托到底派发到了
		 * 哪个 AIN 函数？当初就是用它在 parts::detail::CallPartsUpdateEvent ->
		 * CASTask@UpdateEvent 这条断链上定位的。 */
		static int dgc_dbg_n;
		if (getenv("XSYS4_DG_CALL_DBG") && dgc_dbg_n++ < 20000) {
			const char *nm = "?";
			if (fun > 0 && fun < ain->nr_functions)
				nm = ain->functions[fun].name;
			NOTICE("[dgc] dg_no=%d obj=%d fun=%d %s page=%d idx=%d",
			       dg_no, obj, fun, nm, dg_page, dg_index);
		}
		// 弹出上一个返回值
		for (int rv = 0; rv < return_values; rv++)
			stack_pop();
		// dg_index 自增
		stack[stack_ptr - 1].i++;

		int slot = _function_call(fun, instr_ptr + instruction_width(DG_CALL));
		if (dg_drift_on && dg_drift_depth > 0)
			dg_drift[dg_drift_depth - 1].last_fun = fun;

		// 把实参拷进局部页
		struct ain_function_type *dg = &ain->delegates[dg_no];
		int dg_slots = delegate_argument_slots(dg);
		for (int i = dg_slots - 1; i >= 0; i--) {
			/* 栈：[实参槽 0 ... 实参槽 n-1, dg_page, dg_index] */
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
		// 调用结束：清理栈并跳回返回地址
		/* 一个 AIN_IFACE / AIN_WRAP 返回值占两个槽；只弹一个的话，
		 * 另一半会留在栈上，从此之后每个操作数都错位
		 * （Scenes::RunIScene 于是把一个 SceneStack 存进了 `s` 变量，
		 * 而不是工厂返回的那个场景）。 */
		union vm_value r[2];
		for (int rv = 0; rv < return_values; rv++)
			r[rv] = stack_pop();
		stack_pop(); // dg_index
		stack_pop(); // dg_page
		/* [L2] 离线核对（tools/dgcheck）证明：全部 1276 个委托都满足
		 * nr_variables == delegate_argument_slots()，所以这里的清栈与
		 * DG_CALLBEGIN 本来就是对合的。怀疑「弹多了」的假设已被证伪，
		 * 不要改它。 */
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

/* [D337] 把「引擎捕获到的一次点击」派发给 AIN 侧的按钮对象。
 *
 * 这一拍在原版里由引擎完成，移植时整条漏掉了 —— 它就是 `[TEXTBUTTON-CLICK]`
 * （回想对话「過后查看」点得中、点完没反应）的根因。
 *
 * 反汇编证据（全部来自 game/Rance10.ain）：
 * ① `Parts@SetOnClickEvent`(0x47a33e) 只做两件事：`this.SetShow(1)` 和
 *    `this.m7.DG_ASSIGN(dg)` —— 点击委托存在 **AIN `Parts` 对象自己的字段 7**
 *    里，既不进 `CPartsFunctionSet`，也与 `PartsEngine.GetDelegateIndex` 无关；
 * ② 派发它的是 `Parts@OnClick`(29541)，签名 `OnClick(partsNumber, x, y)`，
 *    函数体就是 `this.m7.DG_CALL(...)`。`ain_xref call 29541` 得 **0 个 AIN
 *    调用点**，运行期也从未执行 ⇒ 它本来就该由引擎回调；
 * ③ `Button@Init`(0x34a7b8) 正是走 ① 注册的
 *    （`this.m3.SetOnClickEvent(DG_NEW(this.OnClick))`）；
 * ④ `TextButton` 不走 ①：`TextButton@SetOnClickEvent`(0x34fa94) 把委托存进
 *    自身 m5，再由 `TextButton@Init` 生成的 lambda 36571 负责
 *    「if (this.IsClickable()) this.m5.DG_CALL()」；
 * ⑤ `AFL_Parts_AddMouseLClickEvent`(6892) 那条路（写 `CPartsFunctionSet.m7`）
 *    在本作运行期 **0 次调用**（只有编辑器 / backlog / config / CSystemButton
 *    那几处），所以 ADV 按钮的 FS `m7` 本来就该是空的 —— 前几轮一直盯着
 *    FS 表找缺口，方向是错的。
 *
 * 实测对账（`ap1`）：1228 个 AIN `Parts` 实例里只有 1 个 m7 非空；被点的
 * `900000574` 对应 `slot=91402`，m7 为空。与 ③④ 完全一致：**部件号 → 按钮
 * 对象**的映射只有 AIN 自己知道，引擎必须自己扫。
 *
 * 两条派发互不重叠，各自是 AIN 自己的写法：
 *  A. `Parts` 实例：`values[0] == 部件号` 且 m7 非空 → 调 `Parts@OnClick`；
 *  B. `TextButton` 实例：`values[3]` 是 `Parts` 对象页，取它的 `values[0]`
 *     与部件号比对，命中则跑 lambda 36571（它自带 `IsClickable` 判定）。
 *
 * 只在「本帧刚捕获到点击」时登记一次，由帧泵（`vm_frame_pump`）在安全的
 * 位置消费 —— 与 `AUTO_*` 后门同一套路，避免在 `parts_update_mouse` 里
 * 直接嵌套执行 AIN（那时 `struct parts *` 随时可能被委托释放）。
 * `XSYS4_PARTS_EVENT=0` 关闭。
 */
static bool pe_click_want_pending;
static bool pe_click_on = true, pe_click_init;
static bool pe_click_reent;
static int pe_click_parts, pe_click_x, pe_click_y;

void vm_request_parts_click(int parts_no, int x, int y)
{
	if (!pe_click_init) {
		pe_click_init = true;
		const char *e = getenv("XSYS4_PARTS_EVENT");
		pe_click_on = !(e && !strcmp(e, "0"));
	}
	if (!pe_click_on || !ain || parts_no <= 0)
		return;
	pe_click_parts = parts_no;
	pe_click_x = x;
	pe_click_y = y;
	pe_click_want_pending = true;
}

static int vm_fno_named(const char *name)
{
	for (int i = 0; i < ain->nr_functions; i++) {
		const char *nm = ain->functions[i].name;
		if (nm && !strcmp(nm, name))
			return i;
	}
	return -1;
}

static int vm_struct_named(const char *name)
{
	for (int i = 0; i < ain->nr_structures; i++) {
		const char *nm = ain->structures[i].name;
		if (nm && !strcmp(nm, name))
			return i;
	}
	return -1;
}

/* 委托页里是否有条目（三元组 obj/fun/seq）。 */
static int vm_delegate_count(int slot)
{
	if (slot <= 0 || (size_t)slot >= heap_size)
		return 0;
	struct page *p = heap[slot].page;
	if (!p || p->type != DELEGATE_PAGE)
		return 0;
	return p->nr_vars / 3;
}

/* `Parts` 对象页 → 它的部件号（字段 0，`Parts@GetPartsNumber` 就是直接读它）。 */
static int vm_parts_obj_number(int slot)
{
	if (slot <= 0 || (size_t)slot >= heap_size)
		return -1;
	struct page *p = heap[slot].page;
	if (!p || p->type != STRUCT_PAGE || p->nr_vars < 1)
		return -1;
	return p->values[0].i;
}

/* [D341] 诊断：打印所有 `LargeSkillButton` 实例的字段，并标出哪一槽指向 `Parts` 对象。
 *
 * 起因：技能卡是 `LargeSkillButton` 活动，它的 `SetOnClickEvent`(25350) 把委托
 * 存在 **`this.m3`**（不是 `Parts.m7`、也不是 `TextButton.m5`），
 * 所以 D337 的两类派发都覆盖不到它。要补第三类，先得知道「按钮对象 → 部件号」
 * 走的是哪个字段 —— 用这个把每个实例的 m0..m9 连同「是不是 Parts 页」一次打全。 */
static void vm_dbg_lsb(void)
{
	static int lsb_st = -2, parts_st = -2;
	if (lsb_st == -2) {
		lsb_st = vm_struct_named("LargeSkillButton");
		parts_st = vm_struct_named("Parts");
	}
	if (lsb_st < 0) {
		NOTICE("[pev]   LargeSkillButton 结构未找到");
		return;
	}
	int n = 0;
	for (size_t i = 1; i < heap_size && n < 6; i++) {
		struct page *p = heap[i].page;
		if (!p || p->type != STRUCT_PAGE || p->index != lsb_st)
			continue;
		n++;
		NOTICE("[pev]   LSB slot=%d nr_vars=%d", (int)i, p->nr_vars);
		for (int k = 0; k < p->nr_vars && k < 10; k++) {
			int v = p->values[k].i;
			const char *tag = "";
			if (v > 0 && (size_t)v < heap_size && heap[v].page &&
			    heap[v].page->type == STRUCT_PAGE &&
			    heap[v].page->index == parts_st)
				tag = " <-- Parts";
			NOTICE("[pev]     m%d=%d%s", k, v, tag);
		}
		if (p->nr_vars > 8) {
			int a = vm_parts_obj_number(p->values[7].i);
			int b = vm_parts_obj_number(p->values[8].i);
			NOTICE("[pev]     卡片部件号 m7->%d m8->%d", a, b);
			if (a > 0) PE_DebugPartsInfo(a);
			if (b > 0) PE_DebugPartsInfo(b);
		}
	}
}

/* [D344] `XSYS4_AUTO_BATTLE_END=<frames>`：到点后**直接调**
 * `SceneBattle@ProcessPostPlayerTurn`(24886)，绕开"角色卡点不中"这个 UI 问题，
 * 单独验证「回合收尾逻辑本身能不能跑」。
 *
 * 为什么需要：`isActionAvailable`(24884) 是 `!CheckActionCountLimit &&
 * SkillUsingHelper::IsActionAvailable && 全局 UI 状态`，只要还有角色能行动就
 * 返回 true —— 四人各行动一次才会走到 `ProcessPostPlayerTurn`。
 * 但角色卡是 `0x0` 布局盒 + 子树几何，靠 xdotool 点命中率很低
 * （`rc3` 成功、`rc5` 同样坐标就成了别的 TextButton）。
 * 这与 `XSYS4_AUTO_PARTY` / `XSYS4_AUTO_MAP` 同性质：只为把跑测从
 * "点得中"变成"确定性"，不改变任何游戏语义，默认关闭。
 *
 * `SceneBattle` 实例页号在任一 `SceneBattle@*` 方法被调用时记下
 * （结构体号 732，见 `[dump] this=... struct=732`）。 */
void vm_pump_auto_battle_end(void)
{
	const char *e = getenv("XSYS4_AUTO_BATTLE_END");
	if (!e || auto_be_done || auto_battle_page <= 0)
		return;
	static int want_frames = -1;
	if (want_frames < 0)
		want_frames = *e ? atoi(e) : 300;
	if (++auto_be_frames < want_frames)
		return;
	auto_be_done = 1;
	NOTICE("[abe] 强制 ProcessPostPlayerTurn(page=%d) after %d frames",
	       auto_battle_page, auto_be_frames);
	vm_call(24886, auto_battle_page);
}

/* [D349] 见上方 auto_skill_* 的注释。判据：`LargeSkillButton` 实例的
 * `m3` 是非空委托、`m0` 是卡片位置（0/1）。默认关。 */
void vm_pump_auto_skill(void)
{
	const char *e = getenv("XSYS4_AUTO_SKILL");
	if (!e || auto_skill_done)
		return;
	/* 语法：`<frames>[:<x>,<y>]` 或 `<frames>[:#<index>]`
	 *   x,y   —— 选**子树覆盖该点**的技能卡（与真实点击同一判据，D341 用它成功过）
	 *   #n    —— 选堆里第 n 个（诊断用，容易选到别的卡上）
	 * 不给就取第一个。 */
	static int want = -1, want_idx = -1, want_x = -1, want_y = -1;
	static int lsb_st = -2, lsb_lambda = -2;
	if (want < 0) {
		want = atoi(e);
		if (want <= 0)
			want = 240;
		const char *c = strchr(e, ':');
		if (c) {
			if (c[1] == '#')
				want_idx = atoi(c + 2);
			else if (strchr(c, ',')) {
				want_x = atoi(c + 1);
				want_y = atoi(strchr(c, ',') + 1);
			} else
				want_idx = atoi(c + 1);
		}
		lsb_st = vm_struct_named("LargeSkillButton");
		lsb_lambda = vm_fno_named(
			"LargeSkillButton@<lambda : LargeSkillButton@InitParts()(125, 51)>");
		if (lsb_lambda < 0 && ain->nr_functions > 37029)
			lsb_lambda = 37029;
		NOTICE("[ask] AUTO_SKILL frames=%d idx=%d pos=(%d,%d) LSB=%d lambda=%d",
		       want, want_idx, want_x, want_y, lsb_st, lsb_lambda);
	}
	if (lsb_st < 0 || lsb_lambda < 0)
		return;
	if (++auto_skill_frames < want)
		return;

	const bool dbg = getenv("XSYS4_PARTS_EVENT_DBG") != NULL;
	int n = 0, pick = -1;
	for (size_t i = 1; i < heap_size; i++) {
		struct page *p = heap[i].page;
		if (!p || p->type != STRUCT_PAGE || p->index != lsb_st)
			continue;
		if (p->nr_vars < 9)
			continue;
		/* 就绪条件：`m3` 上已有委托（`LargeSkillButton@SetOnClickEvent`
		 * 由 `BattleCardButton@InitSkillButtonCollection` 在初始化后期设）。
		 * 没有它 `DG_CALL` 空转 —— `b220c30` 的失败就是这条。 */
		if (vm_delegate_count(p->values[3].i) <= 0)
			continue;
		/* 卡片区域是 m7/m8 两个 0×0 布局盒，几何在子树里。 */
		int ba = vm_parts_obj_number(p->values[7].i);
		int bb = vm_parts_obj_number(p->values[8].i);
		bool hit = false;
		if (want_x >= 0)
			hit = PE_PartsCovers(ba, want_x, want_y) ||
			      PE_PartsCovers(bb, want_x, want_y) ||
			      PE_SubtreeCovers(ba, want_x, want_y) ||
			      PE_SubtreeCovers(bb, want_x, want_y);
		if (dbg) {
			NOTICE("[ask]   LSB slot=%d m0=%d m7=%d m8=%d hit=%d",
			       (int)i, p->values[0].i, ba, bb, hit);
			for (int k = 0; k < p->nr_vars && k < 12; k++) {
				int v = p->values[k].i;
				const char *ty = "?";
				const char *txt = "";
				static char buf[128];
				buf[0] = 0;
				if (v > 0 && (size_t)v < heap_size) {
					switch (heap[v].type) {
					case VM_STRING:
						ty = "str";
						if (heap[v].s && heap[v].s->text) {
							snprintf(buf, sizeof(buf), " \"%s\"",
								 display_sjis0(heap[v].s->text));
							txt = buf;
						}
						break;
					case VM_PAGE:
						ty = heap[v].page &&
						     heap[v].page->type == STRUCT_PAGE
						     ? "struct" :
						     (heap[v].page &&
						      heap[v].page->type == DELEGATE_PAGE
						      ? "dg" : "page");
						break;
					default: ty = "val"; break;
					}
				}
				NOTICE("[ask]     m%d=%d %s%s", k, v, ty, txt);
			}
			if (ba > 0)
				PE_DebugPartsInfo(ba);
			if (bb > 0)
				PE_DebugPartsInfo(bb);
		}
		if (want_x >= 0 && !hit)
			continue;
		if (want_idx >= 0 ? n == want_idx : (pick < 0))
			pick = (int)i;
		n++;
		if (pick > 0 && (want_x >= 0 || want_idx >= 0))
			break;
	}
	if (pick <= 0) {
		/* 技能卡是异步展开的（`ShowCardButton` 之后还要 FadeIn），
		 * 固定延时容易太早 ⇒ 等到出现为止，上限 3000 帧。 */
		if (auto_skill_frames > 20000) {
			auto_skill_done = 1;
			NOTICE("[ask] 20000 帧内没有命中的 LargeSkillButton（扫描到 %d 个）", n);
		}
		return;
	}
	auto_skill_done = 1;
	NOTICE("[ask] 派发 LargeSkillButton slot=%d（候选 %d 个）", pick, n);

	/* [D368] 直接调 `SceneBattle@OnClickSkill(cardId, skillIndex)`。
	 *
	 * AIN 原本的链路是：
	 *   卡片部件点击 → `ActivityButton` 的 lambda 37568 → `m6` 委托表
	 *   → LSB 的 lambda 37029（`this.m3.DG_CALL(this.m0)`）
	 *   → collection 的 37032 → `BattleCardButton` 的 37011
	 *   → `this.m9.DG_CALL(this.m1.CardId, skillId)`   ← m9 是
	 *     `SetOnClickSkillEvent`(25256) 写进去的 (string,int) 委托
	 *   → `SceneBattle@OnClickSkill`(24897)。
	 *
	 * 实测（`d356`/`d364`）整条链每一跳都执行，但 24897 始终 0 次 ——
	 * 说明最后那一跳的 `m9` 委托在运行期是空的（`SetOnClickSkillEvent`
	 * 只在 `BattleCardButtonCollection@Init` 里被调过一次，之后被清理）。
	 *
	 * 与其在这里补一个"引擎自己造委托"，不如按 AIN 自己的语义
	 * 直接调用同一个方法：`cardId` 取 `LeaderCard.CardId`（成员 0，
	 * `LeaderCard@CardId::get` 就是 `return this.m0`），`skillIndex`
	 * 取 LSB 自己的 `m0`（`d367` 实测它是 0/1，正好是卡内技能序号）。
	 * `XSYS4_AUTO_SKILL_MODE=lambda` 可切回原来的 lambda 派发做 A/B。 */
	static int use_lambda = -1;
	if (use_lambda < 0) {
		const char *m = getenv("XSYS4_AUTO_SKILL_MODE");
		use_lambda = m && !strcmp(m, "lambda");
	}
	/* 找 `LeaderCard`：`LargeSkillButton` 的成员里有它，但槽位在不同
	 * 版本/构造路径下不固定（`d367` 里 m1 是 struct，但没有 cardId），
	 * 所以按**结构体号**扫一遍成员，而不是写死下标。 */
	static int lc_st = -2;
	if (lc_st == -2)
		lc_st = vm_struct_named("LeaderCard");
	int cardid = -1;
	if (lc_st >= 0) {
		for (int k = 0; k < heap[pick].page->nr_vars && k < 12; k++) {
			int lc = heap[pick].page->values[k].i;
			if (lc <= 0 || (size_t)lc >= heap_size || !heap[lc].page ||
			    heap[lc].page->type != STRUCT_PAGE ||
			    heap[lc].page->index != lc_st ||
			    heap[lc].page->nr_vars < 1)
				continue;
			int v = heap[lc].page->values[0].i;
			if (v > 0 && (size_t)v < heap_size && heap[v].type == VM_STRING) {
				cardid = v;
				break;
			}
		}
	}
	/* 退一步：任何成员里"成员 0 是字符串"的结构体都当 LeaderCard。 */
	if (cardid < 0) {
		for (int k = 0; k < heap[pick].page->nr_vars && k < 12 && cardid < 0; k++) {
			int lc = heap[pick].page->values[k].i;
			if (lc <= 0 || (size_t)lc >= heap_size || !heap[lc].page ||
			    heap[lc].page->type != STRUCT_PAGE ||
			    heap[lc].page->nr_vars < 1)
				continue;
			int v = heap[lc].page->values[0].i;
			if (v > 0 && (size_t)v < heap_size && heap[v].type == VM_STRING &&
			    heap[v].s && heap[v].s->text &&
			    isprint(display_sjis0(heap[v].s->text)[0]))
				cardid = v;
		}
	}
	if (!use_lambda && cardid > 0 && auto_battle_page > 0) {
		NOTICE("[ask] OnClickSkill(cardId=\"%s\", skillIndex=%d)",
		       heap[cardid].s && heap[cardid].s->text
			       ? display_sjis0(heap[cardid].s->text) : "?",
		       heap[pick].page->values[0].i);
		stack_push(cardid);
		stack_push(heap[pick].page->values[0].i);
		vm_call(24897, auto_battle_page);
		return;
	}
	NOTICE("[ask] 回退到 lambda 派发（cardId 未取到）");
	vm_call(lsb_lambda, pick);
}

void vm_pump_parts_click(void)
{
	static int parts_st = -2, onclick_fno = -2;
	static int tbtn_st = -2, tbtn_lambda = -2;
	static int lsb_st = -2, lsb_lambda = -2;
	static bool logged;
	static int n_dispatch;

	if (!pe_click_want_pending || pe_click_reent || !ain)
		return;
	pe_click_want_pending = false;
	int no = pe_click_parts, x = pe_click_x, y = pe_click_y;

	if (parts_st == -2) {
		parts_st = vm_struct_named("Parts");
		onclick_fno = vm_fno_named("Parts@OnClick");
		tbtn_st = vm_struct_named("TextButton");
		tbtn_lambda = vm_fno_named("TextButton@<lambda : TextButton@Init(string)(51, 52)>");
		/* [D341] 技能卡是 `LargeSkillButton`：`SetOnClickEvent`(25350) 把委托
		 * 存在 **`this.m3`**，onclick 由 `InitParts` 生成的 lambda 37029
		 * (`PUSHSTRUCTPAGE 3 REF / PUSHSTRUCTPAGE 0 REF / DG_CALL 1023`) 派发；
		 * 卡片区域是 m7/m8 两个 `Parts`。 */
		lsb_st = vm_struct_named("LargeSkillButton");
		lsb_lambda = vm_fno_named(
			"LargeSkillButton@<lambda : LargeSkillButton@InitParts()(125, 51)>");
		if (lsb_lambda < 0 && ain->nr_functions > 37029) {
			const char *nm = ain->functions[37029].name;
			if (nm && strstr(nm, "LargeSkillButton") && strstr(nm, "InitParts"))
				lsb_lambda = 37029;
		}
		const bool dbg = getenv("XSYS4_PARTS_EVENT_DBG") != NULL;
		if (dbg)
			NOTICE("[pev] Parts=%d OnClick=%d TextButton=%d lambda=%d "
			       "LargeSkillButton=%d lambda=%d",
			       parts_st, onclick_fno, tbtn_st, tbtn_lambda,
			       lsb_st, lsb_lambda);
	}

	/* 先把候选槽号抄下来：vm_call 会分配新页，heap 可能重分配。 */
	int cand[24];
	int nc = 0, nparts = 0, ntbtn = 0, nlsb = 0;

	/* [D338] 命中号找不到按钮对象时，按**点击坐标**兜底再找一次：
	 * 战斗里点 (740,886) 命中记录的是角色卡内部的透明装饰层 `900000818`
	 * （`ck=0`），而角色卡本体 `900000813` 是 `ck=1` 的矩形检测件、与它重叠。
	 * 命中判据是全局的（一改波及标题/ADV），所以只在派发端补这一跳。
	 * `XSYS4_PARTS_EVENT_GEOM=0` 关闭。 */
	/* **默认生效，`=0` 关闭**（2026-11-19 转正）。
	 *
	 * `bt25` 曾显示它"让选项点击被改派、进不了战斗"，但那次同批改动里有个
	 * 变量遮蔽 bug（循环内重声明 `nc/nparts/ntbtn`），派发本来就全程失效 ——
	 * 修复后 `bt29`/`bt30` 重新 A/B：选项正常、战斗正常，点角色卡时
	 * 由它把 `900001014`（全屏件）改派到 `900000749`（角色卡，`ck=1`），
	 * AIN 的 `Parts@OnClick` 于是真的跑起来（技能列表弹出）。 */
	static int pe_geom = -1;
	if (pe_geom < 0) {
		const char *e = getenv("XSYS4_PARTS_EVENT_GEOM");
		pe_geom = !(e && !strcmp(e, "0"));
	}
	for (int pass = 0; pass < 2 && nc == 0; pass++) {
		if (pass == 1) {
			if (!pe_geom)
				break;
			int alt = PE_FindClickableAt(x, y);
			if (alt <= 0 || alt == no)
				break;
			if (getenv("XSYS4_PARTS_EVENT_DBG"))
				NOTICE("[pev] click=%d 无按钮对象，按坐标改派给 %d", no, alt);
			no = alt;
		}
		/* 注意：这里**不能**重新声明 nc/nparts/ntbtn —— 会遮蔽外层的，
		 * 扫描结果写进影子变量，外层 nc 恒为 0，派发直接 return
		 * （2026-11-19 就是这么把回想对话点不动的，bt27 复现）。 */
		if (tbtn_st >= 0 && tbtn_lambda >= 0 &&
		    ain->functions[tbtn_lambda].nr_args == 0) {
			/* 先精确匹配（按钮部件自己就是被点部件）。 */
			for (size_t i = 1; i < heap_size && nc < 24; i++) {
				struct page *p = heap[i].page;
				if (!p || p->type != STRUCT_PAGE || p->index != tbtn_st)
					continue;
				if (p->nr_vars < 6)
					continue;
				if (vm_parts_obj_number(p->values[3].i) != no)
					continue;
				if (vm_delegate_count(p->values[5].i) <= 0)
					continue;
				cand[nc++] = (int)i;
				ntbtn++;
			}
			/* [D337] 精确匹配不到时按**容器**算：Rance10 的按钮部件
			 * `光标选中透過` 常为 1，点击会穿过按钮落到父容器上，于是
			 * `clicked_parts` 是容器号（回想对话实测点了 574（容器），
			 * 而 TextButton 的部件是 575/579…）。此时「点在容器里」等价于
			 * 点中了容器里的按钮。 */
			if (!ntbtn) {
				for (size_t i = 1; i < heap_size && nc < 24; i++) {
					struct page *p = heap[i].page;
					if (!p || p->type != STRUCT_PAGE || p->index != tbtn_st)
						continue;
					if (p->nr_vars < 6)
						continue;
					int pn = vm_parts_obj_number(p->values[3].i);
					if (pn <= 0)
						continue;
					/* [D345] **不要用 `PE_SubtreeCovers` 做 TextButton 的
					 * 回退**：它太宽，会抢走角色卡。`rc5` 实测点角色卡
					 * (716,890) 时，B 组某 TextButton 的子树覆盖了该点，
					 * 于是 `nc>0`、坐标兜底根本不跑，角色卡永远点不到。
					 * 容器回退（`PE_PartsIsUnder`）保留 —— 那是 D337 为
					 * 回想对话按钮加的正确判据；防御按钮那种 0×0 布局盒
					 * 另想办法（放到 A 组之后按坐标兜底）。 */
					if (pn != no && !PE_PartsIsUnder(pn, no) &&
					    !PE_PartsCovers(pn, x, y))
						continue;
					if (vm_delegate_count(p->values[5].i) <= 0)
						continue;
					cand[nc++] = (int)i;
					ntbtn++;
				}
				if (ntbtn && getenv("XSYS4_PARTS_EVENT_DBG"))
					NOTICE("[pev] click=%d 落在容器上 -> 容器内 %d 个 TextButton",
					       no, ntbtn);
			}
		}
		/* A 组：`Button` 类在 `Parts.m7` 上注册的点击委托。 */
		if (parts_st >= 0 && onclick_fno >= 0 &&
		    ain->functions[onclick_fno].nr_args == 3) {
			for (size_t i = 1; i < heap_size && nc < 24; i++) {
				struct page *p = heap[i].page;
				if (!p || p->type != STRUCT_PAGE || p->index != parts_st)
					continue;
				if (p->nr_vars < 8 || p->values[0].i != no)
					continue;
				if (vm_delegate_count(p->values[7].i) <= 0)
					continue;
				cand[nc++] = (int)i;
				nparts++;
			}
			/* [D341] 精确号失败时按坐标找带委托的 Parts。 */
			if (!nparts) {
				for (size_t i = 1; i < heap_size && nc < 24; i++) {
					struct page *p = heap[i].page;
					if (!p || p->type != STRUCT_PAGE || p->index != parts_st)
						continue;
					if (p->nr_vars < 8 || p->values[0].i <= 0)
						continue;
					if (vm_delegate_count(p->values[7].i) <= 0)
						continue;
					if (!PE_PartsCovers(p->values[0].i, x, y) &&
					    !PE_SubtreeCovers(p->values[0].i, x, y))
						continue;
					if (getenv("XSYS4_PARTS_EVENT_DBG")) {
						NOTICE("[pev]   A组坐标候选 slot=%d parts=%d",
						       (int)i, p->values[0].i);
						int ds = p->values[7].i;
						struct page *dg = (ds > 0 && (size_t)ds < heap_size)
								  ? heap[ds].page : NULL;
						if (dg && dg->type == DELEGATE_PAGE) {
							for (int k = 0; k < dg->nr_vars / 3 && k < 4; k++)
								NOTICE("[pev]     dg[%d] obj=%d fun=%d",
								       k, dg->values[k * 3].i,
								       dg->values[k * 3 + 1].i);
						} else {
							NOTICE("[pev]     m7=%d 不是委托页(type=%d)",
							       ds, dg ? dg->type : -1);
						}
					}
					cand[nc++] = (int)i;
					nparts++;
					break;
				}
			}
		}
		/* C 组：`LargeSkillButton`（技能卡）。它不参与引擎命中
		 * （`bt10` 快照里卡片区 `ck=0 pc=1`），所以**只能按坐标**判断
		 * 「点在哪个卡里」：取 m7/m8 两个 `Parts` 的命中盒比对。
		 * 委托在 m3，派发走 lambda 37029。 */
		if (lsb_st >= 0 && lsb_lambda >= 0) {
			for (size_t i = 1; i < heap_size && nc < 24; i++) {
				struct page *p = heap[i].page;
				if (!p || p->type != STRUCT_PAGE || p->index != lsb_st)
					continue;
				if (p->nr_vars < 9)
					continue;
				if (vm_delegate_count(p->values[3].i) <= 0)
					continue;
				int ba = vm_parts_obj_number(p->values[7].i);
				int bb = vm_parts_obj_number(p->values[8].i);
				bool hit = (ba == no || bb == no ||
					    PE_PartsIsUnder(ba, no) ||
					    PE_PartsIsUnder(bb, no) ||
					    PE_PartsCovers(ba, x, y) ||
					    PE_PartsCovers(bb, x, y) ||
					    PE_SubtreeCovers(ba, x, y) ||
					    PE_SubtreeCovers(bb, x, y));
				if (!hit)
					continue;
				cand[nc++] = (int)i;
				nlsb++;
				/* 只派发一个：两张卡的子树会互相重叠（`sk13` 里同一次
				 * 点击命中 2 个实例，AIN 于是按了两次技能），
				 * 取最先命中的那个即可。 */
				break;
			}
			if (nlsb && getenv("XSYS4_PARTS_EVENT_DBG"))
				NOTICE("[pev] click=%d -> %d 个 LargeSkillButton", no, nlsb);
		}

	}
	if (!nc && getenv("XSYS4_PARTS_EVENT_DBG")) {
		NOTICE("[pev] click=%d 没有匹配的按钮对象（Parts=%d TextButton=%d LSB=%d）",
		       no, nparts, ntbtn, nlsb);
		PE_DebugCovers(x, y);
		vm_dbg_lsb();
		PE_DebugPartsInfo(no);
		{
			int alt = PE_FindClickableAt(x, y);
			NOTICE("[pev]   坐标兜底 FindClickableAt(%d,%d) = %d", x, y, alt);
			if (alt > 0)
				PE_DebugPartsInfo(alt);
		}
		/* 现场勘查：TextButton 实例的 m3 到底指向什么。 */
		static int ndump;
		if (tbtn_st >= 0 && ndump < 3) {
			ndump++;
			for (size_t i = 1; i < heap_size; i++) {
				struct page *p = heap[i].page;
				if (!p || p->type != STRUCT_PAGE || p->index != tbtn_st)
					continue;
				int m3 = p->nr_vars > 3 ? p->values[3].i : -1;
				struct page *q = (m3 > 0 && (size_t)m3 < heap_size)
						 ? heap[m3].page : NULL;
				NOTICE("[pev]   tbtn slot=%zu nr=%d v1=%d m3=%d -> %s idx=%d v0=%d nr=%d m5=%d",
				       i, p->nr_vars,
				       p->nr_vars > 1 ? p->values[1].i : -9, m3,
				       q ? (q->type == STRUCT_PAGE ? "struct" :
					    q->type == DELEGATE_PAGE ? "dgate" : "other")
					 : "null",
				       q ? q->index : -1,
				       q && q->nr_vars > 0 ? q->values[0].i : -999,
				       q ? q->nr_vars : -1,
				       p->nr_vars > 5 ? vm_delegate_count(p->values[5].i) : -9);
			}
		}
	}
	if (!nc)
		return;

	if (!logged && getenv("XSYS4_PARTS_EVENT_DBG")) {
		logged = true;
		NOTICE("[pev] click=%d -> %d 个候选对象（TextButton %d）",
		       no, nc, ntbtn);
	}

	pe_click_reent = true;
	for (int k = 0; k < nc; k++) {
		int slot = cand[k];
		if (slot <= 0 || (size_t)slot >= heap_size || !heap[slot].page)
			continue;
		const bool is_tbtn = heap[slot].page->index == tbtn_st;
		const bool is_lsb = heap[slot].page->index == lsb_st;
		if (n_dispatch < 40 && getenv("XSYS4_PARTS_EVENT_DBG"))
			NOTICE("[pev] #%d slot=%d %s", n_dispatch, slot,
			       is_lsb ? "LargeSkillButton" :
			       is_tbtn ? "TextButton" : "Parts");
		n_dispatch++;
		if (is_lsb) {
			vm_call(lsb_lambda, slot);
		} else if (is_tbtn) {
			vm_call(tbtn_lambda, slot);
		} else {
			/* [D346] 角色卡（`BattleCardButton`）的点击链在 AIN 侧断了一环：
			 * `InitClickTarget` 把 lambda **37013** 注册到卡片 Parts 的 m7，
			 * 而 37013 的函数体是 `this.m15.DG_CALL(this.m8, partsNumber)`；
			 * `m8` 只能由 `BattleCardButton@SetOnClickCardEvent`(25255) 赋值，
			 * 但 **25255 静态零调用点、运行期也 0 次** ⇒ m8 恒空，
			 * DG_CALL 是空转，所以点角色卡什么都不会发生。
			 * （`OnClickParty`(24900) 就是被 `InitViewObjects` 注册到
			 *  `BattleSkillSelector.m17` 的那个回调，只是中间这一跳没人接。）
			 * 判据：m7 委托的 fun == 37013 即角色卡 → 直接补调
			 * `SceneBattle@OnClickParty`（0 参，实例页号已记录）。 */
			int fun0 = 0;
			{
				int ds = heap[slot].page->values[7].i;
				struct page *dg = (ds > 0 && (size_t)ds < heap_size)
						  ? heap[ds].page : NULL;
				if (dg && dg->type == DELEGATE_PAGE && dg->nr_vars >= 3)
					fun0 = dg->values[1].i;
			}
			if (fun0 == 37013 && auto_battle_page > 0) {
				/* [D347] `OnClickParty`(24900) 打开的是**部队编成**（换人），
				 * 不是技能选择（`dc776cb` 的失误）。真正的技能列表由
				 * `BattleSkillSelector@ShowCardButton(val, index)`(25180)
				 * 驱动，它的调用者是 `Focus`(25177) / `FadeIn` / `SetParent`。
				 * `BattleSkillSelector` 就是 `SceneBattle.m17`（`InitViewObjects`
				 * 里 `m17.OnClickPartyEvent::add(...)` 的位置）。
				 * 先固定 index=0（第一张卡）验证链路；索引映射待补。 */
				int bs = 0;
				if (auto_battle_page > 0 &&
				    heap[auto_battle_page].page->nr_vars > 17)
					bs = heap[auto_battle_page].page->values[17].i;
				if (n_dispatch < 40 && getenv("XSYS4_PARTS_EVENT_DBG"))
					NOTICE("[pev] 角色卡 -> ShowCardButton(bs=%d, page=%d)",
					       bs, auto_battle_page);
				if (bs > 0 && (size_t)bs < heap_size && heap[bs].page) {
					stack_push(1);	/* val = true */
					stack_push(0);	/* index */
					vm_call(25180, bs);
				} else {
					vm_call(24900, auto_battle_page);
				}
			} else {
				stack_push(no);
				stack_push(x);
				stack_push(y);
				vm_call(onclick_fno, slot);
			}
		}
	}
	pe_click_reent = false;
}

void vm_call(int fno, int struct_page)
{
	size_t saved_ip = instr_ptr;
	if (struct_page < 0) {
		function_call(fno, VM_RETURN);
	} else {
		/* AIN v12：构造函数的实参已经在栈上了。
		 * method_call_fno 期望 [this, 实参...]（this 在实参下面），
		 * 所以要把 this 插到 nr_args 个实参之下，而不是压到栈顶。 */
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

/* 一个 AIN_IFACE（或被 AIN_WRAP 包过的接口）值占两个栈槽：
 * [对象堆槽, 虚表偏移]。对象的第一个结构体成员就是它的虚表，
 * 即一张装着 AIN 函数号的数组页；方法调用会把接口的虚表偏移
 * 加到方法槽号上，再到那里查函数号。
 *
 * `ref_slot` 是实现该接口的对象的引用。它的结构体号决定在
 * ain_interface 里查哪一行（例如 CSpriteParts(329) 声明了
 * IParts(358)->0 和 ISpriteParts(363)->309）。
 * 对象没有实现 `iface_no` 时返回 false；
 * AIN 的调用点把这种情况表示成空引用（-1），
 * 调用方按「调用不成立」处理。 */
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
		/* 转成具体类只有在「对象恰好就是该类」或「该类在它的接口
		 * 声明里」时才成立（后者上面已处理）。
		 * 早先那条「任何活对象都算合法向下转型」的兜底，
		 * 让一个 Stack 冒充成了 SceneQuestMap，
		 * 调用方随后去读一个 8 成员结构体的第 22 个成员。
		 * AIN 没有类继承表，所以无法校验的转型必须失败，
		 * 并把空引用留给每个调用点 —— 它们本来就会检查这个空引用。 */
		return false;
	}
	/* 已经是填好的两槽接口包装。 */
	if (p->type == IFACE_PAGE && p->nr_vars >= 2 && p->values[0].i > 0) {
		*obj = ref_slot;
		*offset = p->values[1].i;
		return true;
	}
	return false;
}

/* [D227] 按值传参且**拥有堆引用**的类型。`AIN_REF_TYPE` 是宏（多 case），
 * 不能放进表达式；它那类参数由 function_call() 自己 heap_ref，见调用点。
 *
 * [D240] 上面那句对 REF 类**不成立**：`vm_call_hll_closure()` 末尾是直接
 * `vm_call(fno, -1)`，根本没经过 `function_call()`。被调帧销毁时照样会
 * unref 局部页里的引用，所以 REF 类也必须在这里补一份 —— 否则
 * `Array.Find(ref array<T>, 谓词)` 的元素在谓词返回后变成悬空槽
 * （core：`heap_unref ← execute_instruction ← … ← array_predicate_matches`，
 * 崩溃前日志有 `Invalid string index: 821768443 (type=-1)`）。
 * 只列「拥有堆对象」的 REF：AIN_REF_INT/FLOAT/BOOL/LONG_INT/ENUM 是值，
 * `args[i].i` 是字面值，补 ref 会误伤。 */
static bool hll_arg_owns_ref(enum ain_data_type type)
{
	switch (type) {
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
		return true;
	case AIN_REF_STRING:
	case AIN_REF_STRUCT:
	case AIN_REF_DELEGATE:
	case AIN_REF_ARRAY:
	case AIN_REF_ARRAY_TYPE:
		return true;
	default:
		return false;
	}
}

/* 调用 AIN_HLL_FUNC 实参所指的那个 AIN 函数。
 *
 * AIN v12 把函数值压成 [this, 函数号] 一对；ffi.c 只保留函数号，
 * 所以 HLL 函数体看到的是一个裸函数号。兰斯10 用它来实现
 * Array 和 Container 库的谓词/比较器重载：
 * `args` 是逻辑实参（谓词一个元素，比较器两个）。
 *
 * 这个函数是闭包：它通过 X_GETENV 访问自己捕获的变量，
 * 而 X_GETENV 解析的是创建它的那一帧。对 HLL 回调来说，
 * 那一帧就是 HLL 函数的调用方（委托对象那条路在
 * DG_NEW_FROM_METHOD 处记录的是同一层关联），所以在这里把它绑好。
 *
 * 实参无法转换成声明的形参类型时返回 false；调用方于是把这次
 * 回调当作「没匹配上」，而不是拿一个坏实参去调函数。
 * 这一点对 Array.Find/Where 之类的谓词尤其重要：
 * 静默失败比带着坏实参进帧要安全得多。 */
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
				/* 把先前那些实参已经压进去的槽撤回来。 */
				while (pushed-- > 0)
					stack_pop();
				return false;
			}
			stack_push((union vm_value){ .i = obj });
			stack_push((union vm_value){ .i = offset });
			pushed += 2;
		} else {
			/* [D227] 借来的实参必须在这里补一份引用。
			 *
			 * `function_call()` 对**按值**的参数按"所有权转移"处理：
			 * 值进被调帧的局部页，帧销毁时 unref。而 HLL 侧（Array 的
			 * 谓词族：Find/Where/Any/All/First/BinarySearch）传进来的
			 * 是数组元素的**副本**，原元素仍归数组所有 —— 不补引用就
			 * 等于把数组那一份引用送给了被调帧，被调帧用完 unref 一次，
			 * 字符串/页提前释放，数组里留下悬空槽。
			 *
			 * 判据：战斗回合切换时 `Array.Find(ref array<string>, 谓词)`
			 * 的谓词做 `S_EQUALE`，对已释放槽 heap_unref，slot=918493
			 * 越界 → SIGSEGV（core 里 Array_Find_ → heap_unref）。
			 * `AIN_REF_*` 参数不补：`function_call()` 自己会 heap_ref，
			 * 由被调帧借用。 */
			if (hll_arg_owns_ref(param->data) &&
			    heap_index_valid(args[i].i))
				heap_ref(args[i].i);
			stack_push(args[i]);
			pushed++;
		}
	}
	/* AIN 把多槽形参列表的尾巴声明成 void（例如 AIN_IFACE 形参的
	 * 第二半）。而 function_call() 是无条件按 nr_args 弹槽的，
	 * 所以这里要把这些槽补齐。 */
	while (pushed < fn->nr_args) {
		stack_push((union vm_value){ .i = 0 });
		pushed++;
	}

	vm_call(fno, -1);
	/* 返回 AIN_IFACE / AIN_WRAP 的回调会在栈上留两个槽（见
	 * delegate_return_slots）。只弹一个的话，
	 * 操作数栈就会永久深出一个值。 */
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

/* 单实参形式：Array 的元素谓词。 */
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
	/* 经 _function_call() 进来的帧（委托调用、vm_call）从没记录过
	 * 自己的操作数栈，所以 entry_sp == -1。 */
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
		// XXX：调用方会 S_POP
		break;
	}
	case SYS_MSGBOX: {
		struct string *str = stack_peek_string(0);
		char *utf = sjis2utf(str->text, str->size);
		SDL_ShowSimpleMessageBox(0, "xsystem4", utf, NULL);
		free(utf);
		// XXX：调用方会 S_POP
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
		// 目录返回真；但如果路径以目录分隔符结尾，则返回假。
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
			// 停止执行
			vm_exit(1);
		}
		// XXX：调用方会 S_POP
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
		// FIXME：如果分配过引用数组，这里要释放
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
		// xsystem4 专有的系统调用号（供 hacks 使用）
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
	/* [D209 诊断后门] XSYS4_NODE_DIGIT=<0-9>：把 QuestMapParamAccessor 的
	 * KeyBase 末尾全角编号末位改掉（００１ → ００２）。只回答一个窄问题：
	 * 「KeyBase 是不是战斗唯一卡点」。必须放在主循环里：方法调用的 this 是在
	 * function_call() 返回之后才写进 local page，入口钩子里读不到。
	 * 全角数字低位字节连续，按原编码推导，不猜 GBK/SJIS；默认关。 */
	{
		static int nd_init, nd_digit, nd_logged;
		if (!nd_init) {
			const char *e = getenv("XSYS4_NODE_DIGIT");
			nd_digit = e ? atoi(e) : -1;
			nd_init = 1;
		}
		/* [D286] 上限 9 → 99：地图节点不止个位数，而 >9 以前是**静默忽略**的
		 * —— 我拿 10/15/20/25 跑了四轮「大范围扫描」，日志里一条 `[nodekey]`
		 * 都没有，那四组数据全是无效的。这种「没生效但看起来跑过了」比报错
		 * 更危险，所以：① 放宽到 99；② 十位/个位分别改写；③ 改不动时**明确
		 * 告警**而不是安静跳过。 */
		if (nd_digit >= 0 && nd_digit <= 99 && call_stack_ptr > 0) {
			struct function_call *cf = &call_stack[call_stack_ptr - 1];
			/* 只在 RunAdvAndBattleEvent(23486) 这条链上改：地图装载 /
			 * PreBattleAutoSave 也调 28578，早改会让 ００２ 节点缺字段，
			 * 实测 core 在 savedata.c:213 add_value_to_gsave 断言。 */
			int on_event_chain = 0;
			for (int k = call_stack_ptr - 2; k >= 0 && k >= call_stack_ptr - 10; k--)
				if (call_stack[k].fno == 23486) { on_event_chain = 1; break; }
			if (on_event_chain && cf->fno == 28578 && cf->struct_page > 0 &&
			    (size_t)cf->struct_page < heap_size &&
			    heap[cf->struct_page].page) {
				struct page *tp = heap[cf->struct_page].page;
				if (tp->nr_vars >= 1) {
					int ks = tp->values[0].i;
					struct string *s = (ks > 0 && (size_t)ks < heap_size &&
						heap[ks].type == VM_STRING) ? heap[ks].s : NULL;
					if (s && s->text) {
						size_t n = strlen(s->text);
						unsigned char *t = (unsigned char *)s->text;
						/* 末尾须是 3 个同高字节的全角数字，否则不动，避免改坏别的 key。
						 * 全角数字的末字节连续（０=0x50 … ９=0x59），所以
						 * `base + d` 就是数字 d 的末字节 —— 不猜 GBK/SJIS。
						 * 两位以上按位改写：个位放 nd%10，十位放 nd/10。 */
						if (n >= 6 && t[n-6] == t[n-2] && t[n-4] == t[n-2]) {
							unsigned char lastbase = t[n-5];
							t[n-1] = (unsigned char)(lastbase + nd_digit % 10);
							int tens = nd_digit / 10;
							if (tens > 0 && t[n-6] == t[n-2]) {
								/* 十位：把倒数第二个全角数字改成 tens */
								t[n-3] = (unsigned char)(t[n-7] + tens);
							}
							if (!nd_logged) {
								nd_logged = 1;
								NOTICE("[nodekey] KeyBase -> %d (tens=%d ones=%d)",
								       nd_digit, tens, nd_digit % 10);
							}
						} else if (!nd_logged) {
							/* 改不动必须说出来，不能静默跳过。 */
							nd_logged = 1;
							NOTICE("[nodekey] 无法改写 KeyBase（末 3 字节不是全角数字）"
							       "，节点 %d 未生效", nd_digit);
						}
					}
				}
			}
		}
	}
	/* XSYS4_TRACE=起始:结束（十六进制）：运行时指令跟踪，带栈顶值 */
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
		/* [D235] XSYS4_DUMP_TOPS=1：连栈顶 4 个值一起打。
		 * 断在分支指令上时，栈顶就是那条分支的判据 —— 光有 sp 看不出
		 * 「条件是真是假」，而这类「条件永远为假」正是最难查的一类。 */
		if (getenv("XSYS4_DUMP_TOPS")) {
			char tops[160];
			int n = 0;
			for (int i = 0; i < 4 && i < (int)stack_ptr; i++)
				n += snprintf(tops + n, sizeof(tops) - n, " %d",
					      stack_peek(i).i);
			NOTICE("[dump] tops=%s", tops);
		}
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
				/* [D162] 原来只打前 12 个成员。`PartyCard` 有 27 个，
				 * 而 `m_cursor`(m13) 恰好在截断之后 —— 查"点击绑到哪个
				 * 部件"时看不到关键成员。DUMP_AT 本身就是默认关的诊断
				 * 开关，放大到 32 不增加默认路径开销。 */
				for (int i = 0; i < tp->nr_vars && i < 32; i++) {
					int mv = tp->values[i].i;
					NOTICE("[dump]   this.m%d = %d", i, mv);
					/* [D166] 字符串成员连文本一起打。`LeaderCard.IsExist`
					 * 就是 `m_cardId != ""`，"空槽的 m_cardId 是空串还是
					 * null" 是那边唯一要回答的问题，而只看槽号看不出来。
					 * 必须在 page 分支之前判：字符串槽的 union 里 page 指针
					 * 是脏的（同 D152 对局部变量的处理）。 */
					if (mv > 0 && (size_t)mv < heap_size && heap[mv].type == VM_STRING) {
						const char *txt = heap[mv].s && heap[mv].s->text
						                  ? heap[mv].s->text : "";
						NOTICE("[dump]     -> str \"%.60s\"", display_sjis0(txt));
					} else
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
					} else if (mv > 0 && (size_t)mv < heap_size && heap[mv].page &&
					           heap[mv].page->type == STRUCT_PAGE) {
						/* [D162] 引用型成员（如 `PartyCard.m_cursor : Parts`）
						 * 顺着打一层 m0 —— 对 Parts 来说 m0 就是 partsNumber，
						 * 正是"这个对象挂在哪个部件上"的答案。 */
						struct page *sp = heap[mv].page;
						NOTICE("[dump]     -> struct no=%d nr=%d m0=%d", sp->index,
						       sp->nr_vars, sp->nr_vars > 0 ? sp->values[0].i : -9999);
					}
				}
			} else {
				NOTICE("[dump] this=slot %d (no page)", cf->struct_page);
			}
			for (int i = 0; lp && i < lp->nr_vars && i < fn->nr_vars; i++) {
				struct ain_type *vt = &fn->vars[i].type;
				char *ts = ain_strtype_d(ain, vt);
				int32_t v = lp->values[i].i;
				float v_as_f = lp->values[i].f;
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
				/* [D277] 声明类型是 float 的局部变量必须按 `%f` 打。
				 * 用 `%d` 读 union 的 `.i` 会把浮点位模式当整数 ——
				 * `PartsMotionTask@OnUpdate` 的 `r = Time/Span` 明明
				 * 算对了（实测 0.016/0.088/0.123…），dump 却一直显示 0，
				 * 害我一路怀疑 `ITOF`/`F_DIV` 的实现。 */
				if (fn->vars[i].type.data == AIN_FLOAT) {
					NOTICE("[dump]   l%d %-16s %-20s = %f%s", i,
					       fn->vars[i].name ? display_sjis0(fn->vars[i].name) : "?",
					       ts ? ts : "?", (double)v_as_f, extra);
				} else {
					NOTICE("[dump]   l%d %-16s %-20s = %d%s", i,
					       fn->vars[i].name ? display_sjis0(fn->vars[i].name) : "?",
					       ts ? ts : "?", v, extra);
				}
				/* [D279] 结构体 / 数组型的引用局部，把它的成员也打出来。
				 * 只打槽号时，「这个 motion key 的 begin/end alpha 到底是多少」
				 * 这种问题在 dump 里完全看不见 —— 而这正是当前卡点。
				 * 只展开一层、最多 16 个成员，避免又把日志冲掉。 */
				/* ARRAY_PAGE 且元素是结构体时（`a_type == AIN_ARRAY_STRUCT`），
				 * 同样把元素展开 —— `PartsMotionKey` 的 begin/end alpha 就藏在这里。
				 * 元素宽度要按 `array_real_element_size()` 走（接口数组是 2 槽/元素）。 */
				if (v > 0 && (size_t)v < heap_size && heap_index_valid(v) &&
				    heap[v].page && heap[v].page->type == ARRAY_PAGE &&
				    heap[v].page->a_type == AIN_ARRAY_STRUCT) {
					struct page *ap = heap[v].page;
					/* 接口型元素占 2 槽/元素（契约 27），其余 1 槽。
					 * 判据同 `is_interface`：看结构体声明。 */
					int esz = 1;
					if (ap->index >= 0 && ap->index < ain->nr_structures &&
					    ain->structures[ap->index].is_interface)
						esz = 2;
					int cnt = ap->nr_vars / esz;
					cnt = cnt < 3 ? cnt : 3;
					for (int e = 0; e < cnt; e++) {
						union vm_value ev = ap->values[e * esz];
						NOTICE("[dump]       l%d[%d] = %d (elem_size=%d)", i, e, ev.i, esz);
						if (ev.i > 0 && (size_t)ev.i < heap_size &&
						    heap_index_valid(ev.i) && heap[ev.i].page &&
						    heap[ev.i].page->type == STRUCT_PAGE) {
							struct page *sp = heap[ev.i].page;
							int lim = sp->nr_vars < 16 ? sp->nr_vars : 16;
							for (int k = 0; k < lim; k++) {
								const char *sn = NULL;
								if (sp->index >= 0 && sp->index < ain->nr_structures &&
								    k < ain->structures[sp->index].nr_members)
									sn = ain->structures[sp->index].members[k].name;
								NOTICE("[dump]         .m%d %-14s = %d",
								       k, sn ? display_sjis0(sn) : "?", sp->values[k].i);
							}
						}
					}
				}
				if (v > 0 && (size_t)v < heap_size && heap_index_valid(v) &&
				    heap[v].page && heap[v].page->type == STRUCT_PAGE) {
					struct page *sp = heap[v].page;
					int lim = sp->nr_vars < 16 ? sp->nr_vars : 16;
					for (int k = 0; k < lim; k++) {
						union vm_value mv = sp->values[k];
						const char *sn = NULL;
						if (sp->index >= 0 && sp->index < ain->nr_structures &&
						    k < ain->structures[sp->index].nr_members)
							sn = ain->structures[sp->index].members[k].name;
						NOTICE("[dump]       l%d.m%d %-14s = %d",
						       i, k, sn ? display_sjis0(sn) : "?", mv.i);
					}
				}
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
	// --- 栈管理 ---
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
		// 解引用：把「引用」变成「值」。
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
		// 解引用：引用套引用（REFREF）。
		/* [D136] XSYS4_REFREF_DBG=<十六进制 ip>：把某个 REFREF 正在读的页
		 * 整个转储出来 —— 类型、已定型的元素种类、结构体类型、大小，
		 * 以及最前面几个槽及其堆类型。造它是为了认出
		 * QuestMapObjectLoader@Load 的 2 槽元素（0x37cb90）背后到底是哪个
		 * 容器，它的第二半是一个堆槽，而不是虚表偏移。地址从环境变量给，
		 * 所以代码里不写死任何地址。 */
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
		// AB -> ABAB：把栈顶两项再复制一遍
		int a = stack_peek(1).i;
		int b = stack_peek(0).i;
		stack_push(a);
		stack_push(b);
		break;
	}
	case DUP_X2: {
		// ABC -> CABC：把栈底一项搬到栈顶
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
		// ABC -> BCABC：把栈底两项搬到栈顶
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
		// AB -> ABA：把栈顶项再压一次
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
	// --- 变量 ---
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
			/* [D177] 槽 0 = 全局页（heap[0].page），原来 `ws > 0` 把它排除了，
			 * 于是没法盯 `global[14]`（CASTimerManager 的全局单例）被谁改写。
			 * 现在允许 `XSYS4_OBJ_WATCH=0:14` 这种写法。 */
			if (ws >= 0 && _dh == ws && _dp == wm)
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
		/* [D177] XSYS4_G14_WATCH=1：只盯 global[14]（CASTimerManager 全局单例）
		 * 被改写的那一刻。与 OBJ_WATCH 的区别：
		 *   - 只在**值真的变化**时打印，不会被同一下标的普通计数淹没；
		 *   - 带调用栈，能直接定位是哪条 AIN 路径写的。
		 * 起因：CheckHandle 有时跑在空 manager 上（m1 nr=0），报「句柄错误」
		 * 后场景退出；而 global[14] 实测在 95（真 manager，struct=5）与
		 * 78（已被 double free 的 VM_PAGE 槽）之间跳。 */
		if (getenv("XSYS4_G14_WATCH")) {
			int _gh = stack_peek(2).i, _gp = stack_peek(1).i;
			if (_gh == 0 && _gp == 14) {
				int _nv = stack_peek(0).i;
				int _ov = heap[0].page ? heap[0].page->values[14].i : -1;
				static int _gn;
				if (_nv != _ov && _gn++ < 40) {
					NOTICE("[g14] ip=0x%zX old=%d new=%d local=%d st[3..0]=%d,%d,%d,%d",
					       instr_ptr, _ov, _nv, local_page_slot(),
					       stack_peek(3).i, stack_peek(2).i,
					       stack_peek(1).i, stack_peek(0).i);
					vm_stack_trace();
				}
			}
		}
		union vm_value val = stack_pop();
		/* [D136] ASSIGN 直接写槽，local_set() 根本看不到它，
		 * 所以 XSYS4_LVAR_DBG 对「局部变量最常见的赋值方式」是瞎的。
		 * 严格 opt-in：开关关掉时这就是原来那一行，
		 * 默认路径保持逐字节一致。 */
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
	/* AIN v12 的 0x10x 扩展：接口/向下转型断言。
	 *
	 * 栈约定由 tools/ain_icast.c 在全部 607 个调用点上统计得出
	 * （379 处 POP 丢弃、223 处与 -1 比较、5 处在 RETURN 之前），
	 * 并用 CTextParts@Core::get @0x2637f0 做了受控跟踪：
	 *   入：[..., 接口引用]
	 *   出：[..., 接口引用, 虚表偏移, 实现对象或 -1]   （净 +2）
	 * 最后那个值是转型结果（与 -1 比较或被 POP 掉）；
	 * 留下的两个值就是调用方要返回、或用 R_ASSIGN 存起来的 AIN_IFACE 值。
	 * 只返回一个值会让 DUP_U2 读到栈外，
	 * 产生那个众所周知的 0x8011 垃圾值（D32）。
	 *
	 * vtable_offset 不是常量：AIN 里声明了 278 条 ain_interface 记录，
	 * 光 IParts 一个接口，偏移就有 0/12/38/41/47/50/54/67/70/72/
	 * 82/103，取决于具体实现（logs/ain_icast.txt）。
	 * 栈上那个接口引用就是实现对象：它的
	 * STRUCT_PAGE.index 是 AIN 结构体号，对应的 interfaces[] 行
	 * 给出了向目标类型转换的 vtable_offset。
	 * 实参是具体结构体时就是向下转型；AIN 里没有继承表，
	 * 所以沿用引用里存着的偏移（D28）。 */
	/* 第一阶段（D44）：X_SET（0x104）在 libsys4 里尚未实现。
	 * 在 MapPair@... @0xd918f2 观察到的形式是：[member_ref, copy] ->
	 * 留一个值给紧随其后的 DELETE。全部 300 个调用点的完整语义
	 * 还需要做一次静态普查。 */
/* [D118] X_GETENV（0x103）：入 [局部页] -> 出 [父局部页]。
         * 父帧是创建这个 lambda 的那一帧（在 DG_NEW_FROM_METHOD 记录），
         * 而不是当前的调用方：FadeIn 的 onFinish lambda 读的是
         * FadeIn 的 var[2] 里捕获的 DG_Event。 */
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
		/* 经引用写入：[目标, 值] -> [值]。
		 *
		 * 兰斯10 最主要的用法是数组赋值（`member = <array>`，例如
		 * SceneQuestSelect@LoadQuestList 填 m_questIds）。AIN 的数组赋值
		 * 是把负载拷进目标对象，所以目标槽保持自己的身份，
		 * 元素被逐个拷过去。原来的实现把两个值都弹掉、什么都不存，
		 * 于是每个这样的成员都是空的。
		 *
		 *
		 * 其他形态暂时不动：
		 * 300 个调用点的普查还没做完（D44）。 */
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
		/* [D158] XSYS4_ICAST_DBG=1：只打**失败**的转型，不设总量配额。
		 *
		 * `X_ICAST` 失败时 AIN 侧拿到 -1，而 AIN 的错误分支往往只做
		 * "跳过"，不会报错 —— 图鉴列表为空就是这么来的。判据要能看到
		 * 目标类型号 + 源对象的真实结构体号，否则无法区分
		 * "对象类型确实不对" 和 "我们的转型判定太严"。 */
		if (!ok && getenv("XSYS4_ICAST_DBG")) {
			const char *what = "?";
			int sidx = -1;
			if (ref_slot > 0 && (size_t)ref_slot < heap_size &&
			    heap[ref_slot].page) {
				struct page *pp = heap[ref_slot].page;
				sidx = pp->type == STRUCT_PAGE ? pp->index : -pp->type;
				switch (pp->type) {
				case STRUCT_PAGE: what = "struct"; break;
				case IFACE_PAGE:  what = "iface";  break;
				case ARRAY_PAGE:  what = "array";  break;
				case DELEGATE_PAGE: what = "delegate"; break;
				default: what = "other"; break;
				}
			}
			NOTICE("[icast] fail to=%d src=%d kind=%s idx=%d ip=0x%zX fno=%d",
			       iface_no, ref_slot, what, sidx, instr_ptr,
			       call_stack_ptr > 0 ? call_stack[call_stack_ptr-1].fno : -1);
		}
		/* [对象, 虚表偏移] —— 对象在低槽。字节码按位置消费这一对：
		 * `X_ICAST` 后面紧跟的就是 R_ASSIGN(dst_page, dst_var, src_page, src_var)，
		 * 以及方法调用里的 REFREF，两处都要求对象在前。
		 * 先压偏移会让 R_ASSIGN 从一张莫名其妙的页里拷贝，
		 * 并让 GetResult 拿到的对象，
		 * 不是 LoadQuestList 填进去的那一个。 */
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
		/* [N100-x86_64] 对死槽/非法槽，heap_get_page() 返回 NULL
		 * （见 heap.c 的 D96：「调用方都已处理 NULL」）。R_ASSIGN 是唯一
		 * 没照做的那一个：x86_64 上一个非法下标（例如 314950，
		 * 其实是个 AIN 代码地址而不是堆槽）会走到这里，
		 * page_set_var() 直接解引用 NULL -> SIGSEGV。
		 * 处理办法：把引用对留在栈上（保持栈平衡），跳过这次写入，
		 * 与其他宽容的调用方完全一致。 */
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
		 * 构造函数的实参就在栈上、位于其下。 */
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
	// --- 控制流 ---
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
		/* libsys4 的 initialize_instructions()：version >= 11 时把操作数
		 * 标成 T_INT（nargs）；更老的 AIN 用的是函数号。 */
		int arg = get_argument(0);
		if (ain->version >= 11)
			method_call(arg, instr_ptr + instruction_width(CALLMETHOD));
		else
			method_call_fno(arg, instr_ptr + instruction_width(CALLMETHOD));
		break;
	}
	case CALLHLL: {
		/* AIN v12（initialize_instructions）：CALLHLL 有 3 个立即数。
		 * 第三个是泛型结果的具体 ain_data_type
		 * （无类型时是 -1）。把它交给 FFI 层，
		 * 这样 AIN_REF_HLL_PARAM 之类的结果才能按类型实例化。 */
		int tag = ain->version >= 11 ? get_argument(2) : -1;
		hll_hist_note(get_argument(0), get_argument(1));
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
			// XXX：我猜虚拟机是在这里预分配场景函数的局部页的。
			//      它确实往栈上压了一个看起来像页号的东西，
			//      但我没法证明这就是预分配。
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
		int v = stack_pop().i;
		uint32_t dst = get_switch_address(get_argument(0), v);
		/* [D285] `XSYS4_SWITCH_DBG=<no>[,<no>…]`：打印 SWITCH 的实参与落点。
		 *
		 * 为什么需要它：`SWITCH 496` 在 `PartsMotionTask@UpdateByKey` 里按
		 * `m_key` 派发到 `Parts@SetX/SetY/SetAlpha/…`。要知道「某个部件的
		 * alpha 为什么没被设」，光反汇编 case 表不够 —— 还得知道**实际**
		 * 弹出的值是几、落到哪一支。以前只能靠猜或临时加 printf。
		 *
		 * 默认关闭；按开关号过滤，避免每帧所有 SWITCH 都刷屏。 */
		static char sw_dbg[64];
		static int sw_dbg_init;
		if (!sw_dbg_init) {
			sw_dbg_init = 1;
			const char *e = getenv("XSYS4_SWITCH_DBG");
			if (e)
				snprintf(sw_dbg, sizeof(sw_dbg), ",%s,", e);
		}
		if (sw_dbg[0]) {
			char pat[24];
			snprintf(pat, sizeof(pat), ",%d,", get_argument(0));
			if (strstr(sw_dbg, pat))
				NOTICE("[switch] no=%d val=%d -> 0x%X", get_argument(0), v, dst);
		}
		instr_ptr = dst;
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
	// --- 算术 ---
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
	// 数值比较
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
	// +=、-= 等复合赋值
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
	// --- 64 位整数 ---
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
	// --- 浮点运算 ---
	//
	case FTOI: {
		/* [MEM] XSYS4_LVAR_DBG=<fno>:<var> 打开时，在被观察函数里把每次
		 * 浮点→整数转换的原始值打出来。`QuestMapObjectLoader@Load` 的循环
		 * 上界 rIndex 正是这样来的，垃圾浮点在这一步就能看见。 */
		if (call_stack_ptr > 0 && lvar_watch_matches(call_stack[call_stack_ptr-1].fno)) {
			/* [D284] 限流上限从 40 提到 400：40 条在多部件场景下会被
			 * 前面几个部件的淡入吃光，真正关心的那个部件一条都打不出来
			 * —— 这正是 D283/D284 卡住的地方（439 的 0.75/1.0 转换始终看不见）。
			 * 用常量而不是新开关：本探针已由 `XSYS4_LVAR_DBG` 整层门控，
			 * 再加一个环境变量只是把债务从代码搬到开关表。 */
			static int nf;
			if (nf++ < 400)
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
	// 浮点比较
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
	// --- 字符串 ---
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
		// 解引用：把引用变成字符串指针
		int str = stack_pop_var()->i;
		stack_push_string(string_ref(heap_get_string(str)));
		break;
	}
	//case S_REFREF: // ？？：它和普通的 REFREF 有什么区别、为什么需要？
	case S_ASSIGN: { // A = B
		int rval = stack_peek(0).i;
		int lval = stack_peek(1).i;
		heap_string_assign(lval, heap_get_string(rval));
		// 把 A 从栈上摘掉，但留下 B
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
		// TODO：这里能不能用 string_append？
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
	//case S_PUSHBACK: // ？？
	case S_PUSHBACK2: {
		int c = stack_pop().i;
		int str = stack_pop().i;
		string_push_back(&heap[str].s, c);
		break;
	}
	//case S_POPBACK: // ？？
	case S_POPBACK2: {
		int str = stack_pop().i;
		string_pop_back(&heap[str].s);
		break;
	}
	//case S_ERASE: // ？？
	case S_ERASE2: {
		stack_pop(); // ???
		int i = stack_pop().i; // index
		int str = stack_pop().i;
		string_erase(&heap[str].s, i);
		break;
	}
	case S_MOD: {
		/* libsys4::initialize_instructions 从 AIN v11 起给 S_MOD 一个
		 * T_INT 立即数；格式类型不再放在栈上。
		 * 这里再弹它会让每次调用多消耗一个值，
		 * 把 CardConstructProcessCacheFront 的 stack_ptr 推成负数（D42）。 */
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
	// --- 字符 ---
	case C_REF: {
		int i = stack_pop().i;
		int str = stack_pop().i;
		int32_t ch = string_get_char(heap_get_string(str), i);
		// 英文版用的 'é'
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
	// --- 结构体/类 ---
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
		/* AIN v12 不再为 SR_ASSIGN 单独压一个结构体类型操作数
		 * （与 CALLMETHOD/NEW 是同一处简化）。再弹它会让栈下溢，
		 * 产生像 0x8011 这样的垃圾页下标。
		 * 更老的 AIN 版本仍然带着它。 */
		if (ain->version > 1 && ain->version < 12)
			stack_pop(); // struct type
		/* v12 的形态：[数组, 下标, 值]。从操作数本身判断：
		 * 二操作数形态的三元组底部是结构体目标，
		 * 绝不会是一张活着的 ARRAY_PAGE。 */
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
	// -- 数组 --
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
	/* libsys4 把 A_REF 声明成 (T_PAGE)->()。T_PAGE 表示任何由堆承载的
	 * 对象，而 vm_pointer 的联合体里同时存 page* / string*。
	 * 上游代码一直直接调 copy_page()，于是字符串实参被当成页头来读：
	 * copy_page() 随后按一个垃圾 nr_vars 循环，直接段错误
	 * （D34，AFL_Parts_CreateText @0x1d106e）。
	 * 现在按槽类型分派；vm_copy_page / vm_string_ref 是一对对称的辅助函数，
	 * 各自分配一个新槽，并为被调方加一次引用。 */
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
		// FIXME：数组为空时字符串键没有释放
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
	// -- 简写指令（Alice 2010 加入）---
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
		// 注意：第 1 个实参（结构体类型）没有被使用
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
		/* [D114] XSYS4_DG_ASSIGN_DBG=1：在源委托被拷进事件注册之前，
		 * 确认它里面真的装着条目。 */
		static int dga_dbg_n;
		if (getenv("XSYS4_DG_ASSIGN_DBG") && dga_dbg_n++ < 60)
			NOTICE("[dga] set_i=%d dst_i=%d set=%p type=%d nr=%d new=%p nr=%d",
			       set_i, dst_i, (void*)set, set ? set->type : -1,
			       set ? set->nr_vars : -1, (void*)new_dg,
			       new_dg ? new_dg->nr_vars : -1);
		/* dst_i == -1 表示空的委托目标；delete_page(-1) 会拿一个非法槽
		 * 去调 heap_get_page（D57）。
		 * 而 heap_set_page() 本来就会忽略负槽。 */
		/* [D324] `XSYS4_DGA_DROP=1`：只在「目标为空槽（dst_i<=0）而源委托
		 * 里**确实装着条目**」时打一行 —— 这正是「委托被静默丢掉」的形状。
		 *
		 * 起因：`TextButton@SetOnClickEvent` 的写法是
		 * `this.m5; REF; <源 dg>; A_REF; DG_ASSIGN`。成员本来是空的，
		 * `REF` 给出 -1，下面只 `if (dst_i > 0)` 才写回 → 赋值**整个丢掉**，
		 * 于是 TextButton 的点击委托永远为空，`Motion` 即使跑了也没东西可派。
		 * 这个探针把「丢了什么」点名。 */
		if (getenv("XSYS4_DGA_DROP") && dst_i <= 0 && set &&
		    set->type == DELEGATE_PAGE && set->nr_vars > 0) {
			static int n;
			if (n++ < 300)
				NOTICE("[dgadrop] dst_i=%d set_nr=%d first=[%d,%d] ip=0x%zX",
				       dst_i, set->nr_vars, set->values[0].i,
				       set->nr_vars > 1 ? set->values[1].i : -1, instr_ptr);
		}
		/* [D324] `XSYS4_DG_ASSIGN_NULLALLOC=1`：目标成员是**空槽**时，
		 * 给它分配一张新委托页并写回成员，而不是把这次赋值丢掉。
		 *
		 * 为什么这么改：`TextButton@SetOnClickEvent` 的字节码是
		 * `PUSHSTRUCTPAGE; PUSH 5; REF; <源 dg>; A_REF; DG_ASSIGN`。
		 * 成员 `m5` 本来是空的，`REF` 只能给出 `-1`，而下面只
		 * `if (dst_i > 0)` 才写回 —— 于是**这次赋值无声无息地消失**。
		 * 结果就是：点击委托生成了、也注册了（`[dgadd]` 827 次），
		 * 却永远进不了 TextButton 的 `m5`，`Motion` 即使跑了也没东西可派。
		 *
		 * 写回位置用 `REF` 记录的 `last_ref_heap/index`（REF 每次都会更新），
		 * 并额外要求那一格**现在仍是空槽**，避免写到别处。
		 *
		 * **实测已证伪**：`XSYS4_DGA_DROP=1` 跑到回想对话框，`[dgadrop]` **0 条**
		 * —— 没有任何委托因为「目标是空槽」被丢掉。开关保留只为 A/B，
		 * 默认关；不要再据此改 DG_ASSIGN 的默认行为。 */
		if (dst_i <= 0 && set && set->nr_vars > 0 &&
		    getenv("XSYS4_DG_ASSIGN_NULLALLOC")) {
			if (last_ref_heap > 0 && (size_t)last_ref_heap < heap_size &&
			    heap[last_ref_heap].page &&
			    last_ref_index >= 0 &&
			    last_ref_index < heap[last_ref_heap].page->nr_vars &&
			    heap[last_ref_heap].page->values[last_ref_index].i <= 0) {
				int slot = heap_alloc_page(new_dg);
				heap[last_ref_heap].page->values[last_ref_index].i = slot;
				NOTICE("[dganull] 空成员承接委托 slot=%d nr=%d at=%d[%d]",
				       slot, new_dg ? new_dg->nr_vars : -1,
				       last_ref_heap, last_ref_index);
				stack_push(set_i);
				break;
			}
		}
		if (dst_i > 0)
			delete_page(dst_i);
		/* D75：槽 0 是保留的全局页。dst_i<=0 的 DG_ASSIGN
		 * 会把它覆写成一张委托页，此后每个
		 * PUSHGLOBALPAGE; PUSH n; REF 都读越界。 */
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
		/* [D119 探针] 看看到底是哪张委托页收到了 OK/Cancel 两段 lambda */
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
		/* [D119] -1/0 是「空委托」哨兵。AIN 把
		 * `button.OnClick += lambda` 编译成 REF(member) + DG_PLUSA；
		 * 如果这个成员从来没初始化过，注册就被静默丢掉
		 * （NetworkConfirmation 的 OK/Cancel lambda 37115/37116）。
		 * 所以这里惰性建一张委托页，并把它的槽号写回那个成员。 */
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
		/* D76：dst_i==0 是保留的全局页，不是委托。 */
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
		/* D76：DG_MINUSA 用同一条守卫。 */
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
		/* [D325] `XSYS4_DGNEW_DBG=1`：打印每次「由方法建委托」的 (obj,fun)。
		 * 起因：`CADVEngine@ClickUpdateTextButtonEvent`(2109) 从不执行，而
		 * `0x8a352/0x8a36c` 两处 `DG_NEW_FROM_METHOD(20907)` 是通往它的唯一
		 * 入口 —— 先确认这两次「建委托」到底发生了没有。 */
		if (getenv("XSYS4_DGNEW_DBG")) {
			static int n;
			if (n++ < 400)
				NOTICE("[dgnew] obj=%d fun=%d ip=0x%zX", obj, fun, instr_ptr);
		}
                /* [D118] 记住创建者那一帧的局部页，供 X_GETENV 使用 */
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

		// 调用前：[dg_page, arg0, ...]
		// 调用后：[arg0, ..., dg_page, 0(dg_index)]
		int dg_slots = delegate_argument_slots(dg);
		int dg_page = stack_peek(dg_slots).i;
		/* [L2] XSYS4_DG_PAGE_DBG=1：DG_CALL 之后会把某个 VM_STRING 槽
		 * 当成 dg_page。检查是不是 CALLBEGIN 在这里就取错了栈槽
		 * （也就是 delegate_argument_slots() 数少了）。 */
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

		// XXX：如果委托有返回值，这里先压若干哑值，
		//      好让 DG_CALL 把它们替换掉。AIN_IFACE 占两个槽。
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
		/* libsys4 的 initialize_instructions(v>=11) 给 DG_STR_TO_METHOD
		 * 一个 T_DLG 立即数，栈上只有一个实参（T_STRING）。老形态弹的是
		 * [str, dg_no]；在 v12 上这就多消耗一个值，使 str = -1，
		 * 于是 game_main 从来没注册上（D66）。 */
		if (ain->version >= 11) {
			dg_no = get_argument(0);
			str = stack_pop().i;
		} else {
			dg_no = stack_pop().i;
			str = stack_pop().i;
		}
		/* 空字符串槽表示「没有具名方法」：解析成函数号 0，
		 * 而不是去解引用 heap[-1]（D58）。 */
		if (str < 0) {
			stack_push((union vm_value){ .i = 0 });
			break;
		}
		int fno = get_function_by_name(heap_get_string(str)->text);
		stack_push(function_matches_delegate(dg_no, fno) ? fno : 0);
		heap_unref(str);
		break;
	}
	// -- 空操作 ---
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

/* --- 虚拟机帧泵（见 XSYS4_PUMP） --------------------------------- */
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

/* [D236] 帧泵的指令间隔掩码：默认每 64 条指令检查一次时钟。 */
static unsigned long long vm_pump_mask = 0;

static unsigned long long pump_mask(void)
{
	if (!vm_pump_mask) {
		const char *e = getenv("XSYS4_PUMP_MASK");
		unsigned long long m = e && *e ? strtoull(e, NULL, 0) : 0x3F;
		vm_pump_mask = m ? m : 0x3F;
	}
	return vm_pump_mask;
}

void vm_frame_pump(void)
{
	/* 动画时钟也必须在这里驱动。当 AIN 在
	 * parts::detail::WaitForClick（或其他纯 AIN 等待）里自旋时，
	 * 它永远走不到 PartsEngine.Update，于是每个 flat 部件都停在帧 0：
	 * 标题 logo（75 帧，首帧是空白）和对话框淡入淡出都不再前进，
	 * 画面看起来像冻住了 —— 尽管精灵每一帧都渲染了。
	 * 这里只推进时钟，不调完整的 PE_Update()，
	 * 后者会让已经准备好的精灵失效（D113）。 */
	static uint32_t last_tick;
	static bool auto_map_done;
	/* [D307] XSYS4_FN_LIST=<子串>：一次性把名字里含该子串的 AIN 函数
	 * （函数号 / 代码地址 / 名字）打到日志里。
	 *
	 * 起因：`CPartsMessageManager@CallDelegate` 的**函数号**只存在于
	 * `ain->functions[i].name` 这张内存表里，而 `--disasm` 全程反汇编一份
	 * 要两分钟、`--stat-method` 只印 CALLMETHOD 直方图。排查「点击消息被
	 * PopMessage 掉、委托从不执行」必须先知道 fno 才能上 `XSYS4_DUMP_AT`
	 * / `XSYS4_CALL_TRACE`，否则只能盲猜。 */
	{
		static bool fn_list_done;
		if (!fn_list_done) {
			fn_list_done = true;
			const char *pat = getenv("XSYS4_FN_LIST");
			/* `=*` 表示「全部导出」：一次性把 fno / 地址 / 名字落成表，
			 * 供 `tools/offline/callsite.py` 把调用点翻译成 AIN 函数名。
			 * 全量 8000+ 行，只在离线取证时开。 */
			if (pat && *pat && ain) {
				int n = 0;
				bool all = (pat[0] == '*' && pat[1] == 0);
				/* [D309] `#<号>[,<号>...]`：按函数号直接查名字。
				 * 反汇编里 `CALLMETHOD` 的立即数是 nargs、被调函数号在栈上，
				 * 静态反汇编拿不到；只能先在反汇编里认出「PUSH <号>」，
				 * 再用这个入口把号翻成名字。 */
				if (*pat == '#') {
					const char *q = pat + 1;
					while (*q) {
						int want = atoi(q);
						if (want > 0 && want < ain->nr_functions) {
							NOTICE("[fnlist] %d 0x%08x %s", want,
							       (unsigned)ain->functions[want].address,
							       ain->functions[want].name
							       ? ain->functions[want].name : "?");
							n++;
						}
						while (*q && *q != ',') q++;
						if (*q == ',') q++;
					}
					NOTICE("[fnlist] '#%s' 命中 %d 个", pat + 1, n);
					goto fn_list_done;
				}
				for (int i = 0; i < ain->nr_functions; i++) {
					const char *nm = ain->functions[i].name;
					if (nm && (all || strstr(nm, pat))) {
						NOTICE("[fnlist] %d 0x%08x %s", i,
						       (unsigned)ain->functions[i].address, nm);
						n++;
					}
				}
				NOTICE("[fnlist] '%s' 命中 %d 个", pat, n);
			}
		fn_list_done:;
			/* [D307] XSYS4_STRUCT_LIST=<子串>：把匹配的结构体连同成员名一起打出来。
			 * 查「m1 是委托表还是哈希表」这类问题时，成员名（`<AddMouseLClickEvent>`
			 * 之类）比任何猜测都直接。 */
			const char *sp = getenv("XSYS4_STRUCT_LIST");
			if (sp && *sp && ain) {
				for (int i = 0; i < ain->nr_structures; i++) {
					struct ain_struct *st = &ain->structures[i];
					if (!st->name || !strstr(st->name, sp))
						continue;
					NOTICE("[stlist] struct %d '%s' nr_members=%d",
					       i, st->name, st->nr_members);
					for (int k = 0; k < st->nr_members; k++)
						NOTICE("[stlist]   m%-2d %s", k,
						       st->members[k].name ? st->members[k].name : "?");
				}
			}
		}
	}
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
	/* [D337] 引擎捕获到的点击在这里派发给 AIN 侧的按钮对象
	 * （`parts_update_mouse` 只登记，见 vm_request_parts_click）。 */
	vm_pump_parts_click();
	vm_pump_auto_battle_end();
	vm_pump_auto_skill();
	/* [MEM] XSYS4_MEM_STAT=1：每约 2 秒打印一次虚拟机堆的构成。
	 * 只看 RSS 分不清「槽泄漏（堆数组增长）」「页泄漏」「字符串泄漏」，
	 * 这里遍历堆，把三类分别数出来。开销有上限：
	 * 每 2 秒扫一遍槽数组。 */
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
			/* RSS 与 glibc 的账对不上时：如果 rss 在涨而 malloc_inuse
			 * 不变，那增长就在我们的分配器之外
			 * （GL 驱动 / llvmpipe 缓冲区、纹理）。 */
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

	/* [D192] XSYS4_AUTO_MAP_ROUTE=<ms>：地图稳定后直调
	 * `SceneQuestMap@StartMoveRoute`(23606)。目的是回答一个二选一的问题：
	 * 「回合推进逻辑本身通不通」。
	 * 任务地图上点对象会走 `RunCurrentObjectEvent`(23609) →
	 * `InnerRunEvent`(23610) → `CreateSelection`(23603)，选择界面也建起来了
	 * （`this.m3` 是 `array nr=1`），但 `QuestMapSelectionButton` 的 FLAT
	 * 部件是空 flat（`hdr=0x0 / cg=0/0`），既画不出也点不到。先用后门把
	 * 流程往前推，才能分清「AIN 逻辑」和「flat 渲染」两头是谁坏了。 */
	{
		static bool route_done;
		static uint32_t route_seen_at, route_delay = UINT32_MAX;
		if (route_delay == UINT32_MAX) {
			const char *e = getenv("XSYS4_AUTO_MAP_ROUTE");
			route_delay = (e && *e) ? (uint32_t)atoi(e) : 0;
		}
		if (route_delay && !route_done && auto_party_page > 0) {
			if (!route_seen_at)
				route_seen_at = now ? now : 1;
			if (now - route_seen_at >= route_delay) {
				route_done = true;
				NOTICE("[auto] calling SceneQuestMap@StartMoveRoute (fno 23606) "
				       "on heap slot %d after %ums",
				       auto_party_page, now - route_seen_at);
				vm_call(23606, auto_party_page);
				NOTICE("[auto] returned from StartMoveRoute");
			}
		}
	}

	/* [AUTO] XSYS4_AUTO_RUNPARTY=<ms>：直接调 `Scenes::RunParty(1)`（fno 26394）。
	 *
	 * 与 XSYS4_AUTO_PARTY 的分工：那个走地图的 `SceneQuestMap@OpenParty`，
	 * 依赖地图 UI 的状态（要 `PUSHSTRUCTPAGE` 拿地图实例、要 IsShow 那一套），
	 * 一旦地图自己推到回合结算就调不动了（实测返回地址正常但一次都没进
	 * `CALLFUNC 26394`）。这个后门绕过地图，只回答一个问题：
	 * **`SceneParty` 这个场景本身能不能跑起来**。
	 *
	 * 默认关闭；值即延迟毫秒（`XSYS4_AUTO_RUNPARTY=8000`）。 */
	{
		static bool rp_done, rp_init;
		static uint32_t rp_delay, rp_at;
		if (!rp_init) {
			rp_init = true;
			const char *e = getenv("XSYS4_AUTO_RUNPARTY");
			rp_delay = e ? (uint32_t)(*e ? atoi(e) : 8000) : 0;
		}
		if (!rp_done && rp_delay && auto_map_done) {
			if (!rp_at)
				rp_at = now ? now : 1;
			if (now - rp_at >= rp_delay) {
				rp_done = true;
				union vm_value arg = { .i = 1 }, ret = { .i = 0 };
				NOTICE("[auto] calling Scenes::RunParty(1) (fno 26394) after %ums",
				       now - rp_at);
				bool ok = vm_call_hll_closure(26394, &arg, 1, &ret);
				NOTICE("[auto] RunParty returned ok=%d ret=%d", ok, ret.i);
			}
		}
	}

	/* [D178] XSYS4_AUTO_CARD_CLICK=<idx>：编队界面稳定后直调
	 * `SceneParty@OnClickCard(idx)`。与 RunParty 后门配套：那个负责进界面，
	 * 这个负责验证「点一张卡 → AddCardToFormations」的逻辑链路。
	 * 只回答一个窄问题：**加卡逻辑本身通不通**，不管 xdotool 点不点得中。 */
	/* [D180] acc_in_call 是防重入闸门：vm_call(24300) 内部会再进帧泵
	 * （WaitForClick 一族 HLL 会 pump），如果在 vm_call 期间没有屏障，
	 * 内层帧泵看到条件仍满足就再点一次，349 层嵌套直接栈溢出 core。
	 * 单次点击时原来的 `acc_done = true` 在 vm_call 之前，天然挡住了它；
	 * 改成连点后必须显式加这道闸。 */
	if (acc_want && !acc_done && !acc_in_call && acc_page > 0 &&
	    acc_cards_ready && acc_ready_at &&
	    now - acc_ready_at >= acc_delay) {
		/* [D190] `XSYS4_AUTO_PARTY_CLOSE=1`：直接调 `SceneParty@Close`
		 * (24301)。主线要验证的是**编队界面的出口**——Close 是「決定」
		 * 最终走的方法（内部先 `ShowLeaderIsEmptyDialog`(24302)，为真
		 * 则不关）。判据：Close 返回后 `SceneParty` 卸载、任务地图活动
		 * 恢复装载。 */
		if (acc_close) {
			acc_done = true;
			acc_in_call = true;
			NOTICE("[auto] calling SceneParty@Close (fno 24301) on heap slot %d, "
			       "%ums after last InitCardView", acc_page, now - acc_ready_at);
			vm_call(24301, acc_page);
			NOTICE("[auto] returned from Close");
			acc_in_call = false;
		} else
		/* [D179] 可选第一步：切到未编入编队的组织。OnClickCard 的
		 * fIndex = Party@FindIndexFromOrganizationId；当前组织已在编队
		 * 第 0 格（fIndex=0）→ 走 Remove/Swap 分支，只有 fIndex == -1
		 * 才会走 FindEmptyIndex + AddCardToFormations(24317)。
		 * ChangeOrganizationId(24245) 会重建 m_cards 并再跑一次
		 * InitCardView，后者刷新 acc_ready_at ⇒ 下一轮帧泵自然等到
		 * 新卡片列表稳定后再点。默认 acc_org=0，行为与原来一致。 */
		if (acc_org > 0 && !acc_switched && acc_group_page > 0) {
			acc_switched = true;
			NOTICE("[auto] switching PartyCardGroup to organization %d "
			       "(fno 24245, heap slot %d)", acc_org, acc_group_page);
			stack_push((union vm_value){ .i = acc_org });
			vm_call(24245, acc_group_page);
			acc_ready_at = SDL_GetTicks() ? SDL_GetTicks() : 1;
		} else {
			bool last = (acc_count + 1 >= acc_repeat);
			acc_in_call = true;
			acc_count++;
			/* 最后一击在 vm_call 之前封门；中间击靠 acc_in_call 挡。
			 * 两道都要有：只靠 acc_done 时，非最后一击仍是 false，
			 * vm_call 内的帧泵会无限递归。 */
			if (last)
				acc_done = true;
			NOTICE("[auto] click %d/%d: calling SceneParty@OnClickCard(%d) "
			       "(fno 24300) on heap slot %d, %ums after last InitCardView",
			       acc_count, acc_repeat, acc_index, acc_page,
			       now - acc_ready_at);
			stack_push((union vm_value){ .i = acc_index });
			vm_call(24300, acc_page);
			NOTICE("[auto] click %d/%d returned from OnClickCard",
			       acc_count, acc_repeat);
			acc_in_call = false;
			/* 还有下一击：重置计时。vm_call 期间若又跑了 InitCardView，
			 * 它会把 acc_ready_at 刷成更晚的值，这里只在没刷新时补一次，
			 * 保证两击之间至少隔 acc_delay。 */
			if (!last) {
				uint32_t t = SDL_GetTicks() ? SDL_GetTicks() : 1;
				if (t > acc_ready_at)
					acc_ready_at = t;
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

/* XSYS4_SP_WATCH=<函数号>：把某个 AIN 函数的每条指令的
 * ip/操作码/栈深都打出来。用来定位某个函数的操作数栈效果
 * 从哪条指令开始偏离「弹 nr_args、压返回值」。 */
static int sp_watch_fno = -2;

static void vm_execute(void)
{
	/* [D111e] 栈采样器：XSYS4_STACK_SAMPLE=1 每秒打印一次
	 * AIN 调用栈，这样不开 trace 窗口也能定位忙循环。 */
	static unsigned long long _insn;
	static time_t _last;
	if (sp_watch_fno == -2) {
		const char *w = getenv("XSYS4_SP_WATCH");
		sp_watch_fno = w && *w ? atoi(w) : -1;
	}
	for (;;) {
		/* [D325] `XSYS4_IP_WATCH="0x8a2aa-0x8a388"`：监视一段指令地址是否被执行。
		 *
		 * 起因：`20907 → 2108 → 2109`（TextButton 的点击更新链）所在的那段
		 * `0x8a2aa..0x8a388` 在反汇编里**没有 FUNC 标记**、不在函数表里，
		 * `XSYS4_FN_WATCH` 按 fno 打点根本覆盖不到。要判「这段匿名 lambda
		 * 到底跑没跑」，只能按**指令地址**看。 */
		{
			static long ip_lo = -2, ip_hi = -1;
			static int ip_n;
			if (ip_lo == -2) {
				ip_lo = -1;
				const char *e = getenv("XSYS4_IP_WATCH");
				if (e && *e) {
					char *end;
					ip_lo = strtol(e, &end, 16);
					if (*end == '-')
						ip_hi = strtol(end + 1, NULL, 16);
					else
						ip_hi = ip_lo;
				}
			}
			if (ip_lo >= 0 && (long)instr_ptr >= ip_lo &&
			    (long)instr_ptr <= ip_hi && ip_n++ < 40)
				NOTICE("[ipwatch] ip=0x%zX (第 %d 次进入该段)", instr_ptr, ip_n);
		}
		uint16_t opcode;
		++_insn;
		/* 帧心跳：在 AIN 指令流里定期提交一帧（XSYS4_NO_PUMP=1 关闭）。
		 *
		 * 有些界面（parts::detail::WaitForClick）在 AIN 层自旋等输入，永远
		 * 回不到 sact_Update()；那条路上既没有 handle_events() 也没有
		 * scene_render()，于是画面停在旧帧、点击也读不到。这里定期泵一次
		 * 事件并提交一帧，让纯 AIN 的等待循环也能被驱动。
		 *
		 * [D236] 触发间隔必须按**墙钟**而不是指令数来定。原先写死「每 4096
		 * 条指令」，可是 `WaitForClick` 这种循环每轮只有几百条指令、还要做
		 * 一次完整的 view 更新与合成，实测 4 轮/秒 —— 4 秒才凑够一次帧泵。
		 * 于是 `parts_flat_update()` 收到的 passed_time 是 AIN 自己算的 50ms，
		 * 一秒只推进 4×50ms：一张 76 帧、60fps 的回合演出要跑 40 秒以上，
		 * 场景等 `Parts.IsMotionFlat` 变假等到天荒地老（H044）。
		 * 现在每 64 条指令检查一次时钟，真正的闸门是 vm_frame_pump() 里的
		 * 16ms；指令密集的路径行为不变（16ms 闸门照旧），指令稀疏的等待
		 * 循环则能拿到应有的帧率。XSYS4_PUMP_MASK 可覆盖间隔做 A/B。 */
		if (unlikely((_insn & pump_mask()) == 0) && vm_frame_pump_enabled())
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
	// 调用全局变量的析构函数，但不 unref 它们 ——
	// 因为析构函数可能还会引用别的全局变量。
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
		// 《Dungeons & Dolls》在全局变量的析构函数里存盘
		call_global_destructors();
	}

	// 调用各库的退出例程
	exit_libraries();
	// 清空调用栈
	for (int i = call_stack_ptr - 1; i >= 0; i--) {
		if (call_stack[i].struct_page >= 0 && AIN_VERSION_GTE(ain, 6, 1))
			exit_unref(call_stack[i].struct_page);
		exit_unref(call_stack[i].page_slot);
	}
	// 释放全局变量
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

	// 初始化虚拟机状态
	if (!stack) {
		stack_size = INITIAL_STACK_SIZE;
		stack = xmalloc(INITIAL_STACK_SIZE * sizeof(union vm_value));
	}
	stack_ptr = 0;
	call_stack_ptr = 0;

	heap_init();
	init_libraries();

	// 初始化全局变量
	heap[0].ref = 1;
	heap[0].seq = heap_next_seq++;
	heap_set_page(0, alloc_page(GLOBAL_PAGE, 0, ain->nr_globals));
	for (int i = 0; i < ain->nr_globals; i++) {
		if (ain->globals[i].type.data == AIN_STRUCT) {
			// XXX：必须在调用之前给全局结构体分配好存储
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

	// 引用初始化器里装的是全局变量下标，不是堆槽。
	// 要在应用完普通初始化器之后再解析它们。
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

	// XXX：全局构造函数必须在非结构体变量初始化之后再调用
	//      否则构造函数里设好的全局值会被自身的 initval 覆写
	for (int i = 0; i < ain->nr_globals; i++) {
		struct ain_type *gt = &ain->globals[i].type;
		if (gt->data != AIN_STRUCT && gt->data != AIN_REF_STRUCT)
			continue;
		/* 结构体类全局变量的 variable_initval 是 -1。每个声明了结构体的
		 * 全局变量，都必须在成员方法把它当 `this` 用之前拥有一个对象
		 * （D45：CASConstructionProcess，D47：CASBGM@InitChannel）。 */
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
	/* [D282] 退出时汇总被跟踪函数的**真实调用次数**（不受打印限流影响）。 */
	if (getenv("XSYS4_CALL_TRACE_TOTAL"))
		for (int i = 0; i < call_trace_n; i++)
			if (call_trace[i].hits > 0)
				NOTICE("[calltotal] fno=%d hits=%d sites=%d",
				       call_trace[i].fno, call_trace[i].hits,
				       call_trace[i].nret);
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
