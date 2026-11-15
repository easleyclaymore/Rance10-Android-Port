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

/* AIN v12 把结构体数组声明成 AIN_ARRAY，type.struc 里放元素的结构体号。
 * 只用 -1 初始化会丢掉这条信息，Array.EmplaceBack 就构造不出元素对象。
 * 这里保留一张空的 ARRAY_PAGE，把声明的结构体类型带在身上。
 * （页是空的，但 a_type / struct_type 已经是对的。） */
union vm_value variable_initval_typed(const struct ain_type *type)
{
	/* [D106] 每个数组类型的变量一开始就是一张真实的（空）数组页，而不是 0。
	 * 值为 0 的数组变量没法被 HLL 的 `ref array` 形参填内容：
	 * AIN 栈上只传槽号，Array.Alloc 的回写没有地方落，
	 * 这个「数组」于是就还是 0。
	 * 随后 menu::detail::Init 把 global[67][i] 算成 heap[0].values[i]
	 * （也就是全局变量页），紧跟着的 SR_ASSIGN 就覆写了
	 * 全局槽 0..39 —— 其中槽 26 是当时活着的 CASTimerManager。
	 *
	 * 当时仍会掉进 variable_initval() 的是**带类型**的数组
	 * （AIN_ARRAY_INT/BOOL/STRUCT/…）；泛型 AIN_ARRAY 早已单独处理，
	 * 所以只有一部分全局变量被写坏。 */
	/* [D112] 委托类型的变量一开始必须是一张空的 DELEGATE_PAGE。
	 * 编译器往结构体成员里赋委托时走 DG_ASSIGN，赋值前先对成员取 REF：
	 * 成员是 -1 时没有槽可写，这次拷贝被丢掉，
	 * 之后的 Delegate.Empty() 就报「空」。
	 * 这直接弄坏了 parts::detail::AddPartsUpdateEvent —— 每帧的更新事件
	 * 一个都不触发，SceneStack@WaitFrame 于是永远自旋。
	 * #88 只修了函数局部变量这一半。 */
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
		/* AIN 把结构体数组声明为 AIN_ARRAY，并设置 type.struc。
		 * 把元素种类保留在 a_type 里，这样 Array.PushBack 才能对插入的对象
		 * 正确加引用计数（D21）。
		 *
		 * 没有结构体号时，声明本身说不清元素种类 —— 那就是 AIN 的泛型 `array`，
		 * 它的类型由第一次写入决定（见 array_settle_type）。
		 * 不要默认成 AIN_ARRAY_INT：那会把字符串当成未加引用的槽号存下去，
		 * 留下一堆悬空句柄。
		 * 这就是 D44。 */
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
		/* AIN v12 把一个接口值存进两个相邻的变量槽：
		 * [0] = 结构体页（空时为 -1），[1] = 虚表偏移。
		 * 紧随其后的 AIN_VOID 占位槽**就是**第二个值
		 * （logs/ain_spec_slots.txt：5336/5336 个 IFACE 后面都跟着 VOID）。
		 * 旧的 IFACE_PAGE 包装在 [0] 里放的是一个正的堆槽号，
		 * 于是编译器生成的「REF; PUSH -1; EQUALE」判空永远不成立，
		 * 接口派发就去解引用 -1（D35）。 */
		return (union vm_value) { .i = -1 };
	default:
		return (union vm_value) { .i = 0 };
	}
}

void variable_fini(union vm_value v, enum ain_data_type type, bool call_dtor)
{
	/* [L1] 与 exit_unref / vm_copy / 数组拷贝共用的「所有权」谓词。 */
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
		/* 泛型数组在第一次写入定型之前没有元素种类
		 * （见 array_settle_type）。这里报「void」，可以让每个调用方
		 * 对元素类型的 switch 落到 default 分支，
		 * 而不是掉进下面的告警里。 */
		return AIN_VOID;
	default:
		WARNING("Unknown/invalid array type: %d from %p", type, __builtin_return_address(0));
		return type;
	}
}

/* 能装下 `elem` 的数组种类；用来给泛型数组定型。 */
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

/* 声明成泛型 `array` / `ref array` 的变量，一开始没有元素种类：
 * AIN 让第一次写入的值来定（`array a; a.PushBack("x")` 就成了字符串数组）。
 * 以前的实现把它默认成 AIN_ARRAY_INT，于是字符串被当成裸槽号存进去、
 * 又没加引用；调用方随后 DELETE 自己那份拷贝，槽被释放，
 * 数组里却留着死句柄。读回来就报 "Invalid string index"
 * （SceneQuestSelect@m_questIds，D44）。
 *
 *
 * 调用方只能从值本身推断时，`elem` 可能是 AIN_VOID；
 * 那这个数组就继续当泛型，等后面某次写入再说。 */
void array_settle_type(struct page *p, enum ain_data_type elem)
{
	if (!p || p->type != ARRAY_PAGE)
		return;
	if (p->a_type != AIN_ARRAY && p->a_type != AIN_REF_ARRAY)
		return;
	if (elem == AIN_VOID)
		return;
	/* 没有结构体号的结构体数组没法用，而这个号只有声明里才有；
	 * 那就让它继续当泛型。 */
	if (elem == AIN_STRUCT && p->array.struct_type < 0)
		return;
	p->a_type = elem_to_array_type(elem);
}

/* 由存储值本身推出的元素种类；没有声明可查时用它
 * （例如泛型数组上的 A_PUSHBACK）。 */
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
		// XXX：这里返回 void，因为委托页里的对象
		//      不参与引用计数
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
	/* [D134] XSYS4_DF_FREE_DBG=1 + XSYS4_SLOT_ID=<n>：点名那张
	 * 即将对监视槽执行**致命释放**（ref 1 → 0）的页。
	 * heap_unref() 自己的探针只看得到当前 AIN 指令
	 * （通常是 RETURN）；这里的探针能看到究竟是哪张页持有这个值。 */
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
 * 递归复制一张页。
 */
/* [D140] 接口数组从 QuestMapObjectViewColletion@InitView 交给
 * QuestMapObjectLoader@Set（再由 @Load 读回）走的就是
 * A_REF → vm_copy_page() → copy_page()。接口元素是**两**个槽
 * [对象堆槽, 虚表偏移]，但 variable_type() 对每个槽都报 AIN_STRUCT，
 * 于是下面这个循环把奇槽也深拷贝了。
 * 虚表偏移 0 被喂给 heap_get_page(0)，vm_copy_page() 就地
 * 造出一个全新的页槽 —— 拷贝里观测到的是 [obj, obj+16]。
 * @Load 随后用堆槽号当索引去算 fno = vtable[offset+3]，
 * 在 0x37CBCA 报「CALLMETHOD: invalid function index -1」直接挂掉。
 *
 *
 * 奇槽里是小整数，永远不是引用：原样拷贝即可。
 * XSYS4_IFACE_COPY_FIX=0 可回退到旧行为，用于 A/B。 */
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
	/* ctor_fno >= 0：AIN v12 的 NEW 把构造函数当立即数传进来。
	 * 否则用结构体自带的构造函数（不改任何全局状态）。 */
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

/* [D136] 注册成**接口**的元素结构体（ain->structures[n].is_interface）
 * 在整个 VM 里都是 2 个槽的值 [对象, 虚表偏移]，
 * 字节码用 i*2 去索引这种容器。
 * 若按一槽一个元素去分配 array<struct>，values[i*2+1] 就落到
 * 下一个元素的对象槽上；那个数字又被当成虚表偏移送进
 * `vtable[off + k]`（实测：QuestMapObjectLoader@Load 里
 * A_ALLOC array<ILoadableQuestMapObject>(618) → 123 槽，
 * 随后在 0x37CBC8 报「Out of bounds page index: …」）。
 * 与既有的接口数组一样存：AIN_ARRAY_INT、每元素 2 槽、
 * 不构造结构体（对象那一半由赋值填）。
 * XSYS4_IFACE_ARR_FIX=0 可回退到旧行为，用于 A/B。 */
static bool iface_element_array(int struct_type)
{
	/* [D136] 默认**关**（2026-11-13）。2 槽布局才是字节码期望的，
	 * 但打开它会把地图场景拖进分配风暴：
	 * 进 SceneQuestMap 42 秒，VM 已分配 183 万个对象、
	 * 其中 73.1 万还活着并仍在以 6 万/秒上涨（capA），
	 * 关掉时则是正常抖动（capB）。这场风暴会把内存吃干。
	 * 在这些数组的消费方弄清楚之前（有东西按「一槽一元素」
	 * 循环 Count()/下标），它保持 opt-in，
	 * 已知的 0x37CBC8 越界降级反而是较小的恶。
	 * 想继续查就把 XSYS4_IFACE_ARR_FIX=1 打开。 */
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

	/* [D136] 保留 alloc_array() 给接口元素数组建好的「每元素 2 槽」布局。
	 * 不这么做的话，数组会以 0 槽创建、再由 A_REALLOC 每次只长 1 槽，
	 * 于是字节码的 `i*2` 索引会走进下一个元素的对象槽，
	 * 把那个数字喂给虚表查找。dimensions->i 数的是**元素**。 */
	/* [D136] 增长路径默认**不**乘 2：建数组时就按每元素 2 槽分配
	 * （alloc_array）能让地图场景正常工作，但每次 A_REALLOC 都翻倍
	 * 会把地图装载变成分配风暴（13 秒 73.1 万活对象）。
	 * 增长路径仍然按请求的元素数追加 1 槽，与修复前一致。
	 * XSYS4_IFACE_REALLOC_FIX=1 可打开对称布局做实验。 */
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

	// 数组缩小时：unref 被摘掉的子项
	if (want_slots < src->nr_vars) {
		for (int i = want_slots; i < src->nr_vars; i++) {
			variable_fini(src->values[i], variable_type(src, i, NULL, NULL), true);
		}
	}

	src = xrealloc(src, sizeof(struct page) + sizeof(union vm_value) * want_slots);

	// 数组增长时：初始化新增的子项
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
		/* [D136] 接口元素数组每元素 2 槽（[对象, 虚表偏移]），
		 * 字节码用 i*2 索引。所以 nr_vars 数的是**槽**不是元素：
		 * 这里必须报一半，否则 AIN 看到的 Count() 翻倍，
		 * 每个遍历这种容器的循环都会走两倍远
		 * （光这一条就把标题→地图的装载活动数从 31 掉到 13）。 */
		/* [D136] 接口元素数组每元素存 2 个槽，字节码用 i*2 索引。
		 * nr_vars 数的是槽，所以 AIN 看到的 Count() 必须报元素数。
		 * 这一点曾经搞错过一次（报了槽数），
		 * 标题→地图的装载活动数就从 31 掉到 13；报元素数才能进地图。 */
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

	// 把 (dst_i, dst_i+n) 夹到数组范围内
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

		/* 泛型数组会采用「存进去的值」的元素种类（见 array_settle_type）。
		 * 对这种变量做 A_PUSHBACK 时只有声明类型可用 —— 而它是泛型，
		 * 所以只能让值自己说话。 */
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

	// 若数组将被清空……
	if (page->nr_vars == 1) {
		delete_page_vars(page);
		free_page(page);
		*success = true;
		return NULL;
	}

	// 删掉该变量、把后面的变量前移，然后重分配这张页
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

	// 注意：由于 i 的夹取方式，不能在数组末尾插入
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

// 用 qsort() 做稳定排序时用
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

	// 没给比较函数时，按整数/字符串值比较
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
	/* [D118] 让注册目标活着。NEW 的构造函数把委托注册到
	 * 临时的 `this` 槽上，随后的 ASSIGN 会把它拷走并释放。
	 * 它的引用一旦归零，delegate_get() 就按 seq 丢掉这条记录，
	 * EndEventCallback@Update 再也不跑 —— 开始界面就永远不前进。
	 * XSYS4_DG_KEEPALIVE=0 可关掉它做 A/B。 */
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

	// 垃圾回收
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
