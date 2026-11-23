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

#ifndef SYSTEM4_VM_H
#define SYSTEM4_VM_H

#include <stdbool.h>
#include <stdint.h>
#include "system4.h"

struct ain;
enum ain_data_type;
struct page;
struct string;

// xsystem4 专有的系统调用号（供 hacks 使用）
enum vm_extra_syscall {
	VM_XSYS_KEY_IS_DOWN = 0x80000000,
};

// 非堆值：存在页里，也存在操作数栈上。
union vm_value {
	int32_t i;
	float f;
	void *ref; // 用于把 HLL 返回值强转成指针
};

struct static_hll_function {
	char *name;
	void *fun;
	/* [D152] 声明的参数个数；-1 = 未标注（沿用"按名字取第一个"的旧行为）。
	 * 同名重载（String.GetPart 2/3 参）必须靠它区分：以前只比名字，
	 * 2 参调用被绑到 3 参实现上，第三个参数直接读寄存器垃圾。 */
	int nr_args;
	/* [D284] 声明的返回类型（`AIN_INT` / `AIN_FLOAT` / …，-1 = 未标注）。
	 *
	 * 为什么必须有它：`Math.Min` 在 AIN 库表里有**同名同参数个数**的
	 * 整数版和浮点版（各 2/3/4 参），只看名字+arity 根本分不开 ——
	 * 实测浮点版被绑到整数实现 `Math_Min(int,int)` 上，
	 * `Parts@SetAlpha` 的 `rangeF(0,1,val)` 于是把 val **截断成 0**，
	 * 战斗卡按钮的淡入永远停在 alpha=0（`[BATTLE-FLAT-CLOCK]`）。
	 * 导出表里 `MinF` 这个名字在 AIN 中不存在，是死条目。 */
	int ret_type;
};

struct static_library {
	char *name;
	struct static_hll_function functions[];
};

extern struct ain *ain;

extern union vm_value *stack;
extern int32_t stack_ptr;

static inline union vm_value _vm_id(union vm_value v)
{
	return v;
}

static inline union vm_value vm_int(int32_t v)
{
	return (union vm_value) { .i = v };
}

static inline union vm_value vm_bool(bool b)
{
	return (union vm_value) { .i = !!b };
}

static inline union vm_value vm_float(float v)
{
	return (union vm_value) { .f = v };
}

#define vm_value_cast(v) _Generic((v),				\
				  union vm_value: _vm_id,	\
				  int32_t: vm_int,		\
				  bool: vm_bool,		\
				  float: vm_float)(v)

static inline void stack_set_value(int n, union vm_value v)
{
	stack[stack_ptr - (1 + n)] = v;
}

static inline void stack_push_value(union vm_value v)
{
	stack[stack_ptr++] = v;
}

// 把「栈顶往下第 N 个」槽设为 V。
#define stack_set(n, v) (stack_set_value((n), vm_value_cast(v)))
#define stack_push(v) (stack_push_value(vm_value_cast(v)))
union vm_value stack_pop(void);
union vm_value stack_peek(int n);
union vm_value *stack_peek_var(void);

union vm_value local_get(int varno);
union vm_value global_get(int varno);
union vm_value member_get(int varno);
void global_set(int varno, union vm_value val, bool call_dtors);
struct page *local_page(void);
struct page *get_local_page(int frame_no);
struct page *get_struct_page(int frame_no);

int vm_string_ref(struct string *s);
int vm_copy_page(struct page *page);
void vm_debug_fslist(void);
union vm_value vm_copy(union vm_value v, enum ain_data_type type);

int vm_execute_ain(struct ain *program);
void vm_call(int fno, int struct_page);
/* [D337] 引擎捕获到点击后，把「部件号」派发给 AIN 侧的按钮对象。
 * 先 `vm_request_parts_click()` 登记，再由帧泵 `vm_pump_parts_click()` 消费。 */
void vm_request_parts_click(int parts_no, int x, int y);
void vm_pump_parts_click(void);
/* [D500-g] 活动每帧步进：每帧调 AIN 的 UpdateActivity(254)
 * （CreateActivity 打开的活动存在时；XSYS4_ACT_STEP=0 可关）。 */
void vm_pump_activity_step(uint32_t now);
/* [D344] 诊断后门：到点后直接调 SceneBattle@ProcessPostPlayerTurn。 */
void vm_pump_auto_battle_end(void);
/* [D349] 诊断后门：技能卡展开后直接派发第 N 个 LargeSkillButton。 */
void vm_pump_auto_call(void);
void vm_pump_auto_call_tick(void);
void vm_pump_auto_skill(void);
bool vm_iface_pair(int iface_no, int ref_slot, int *obj, int *offset);
bool vm_call_hll_func(int fno, union vm_value elem, union vm_value *ret);
bool vm_call_hll_closure(int fno, const union vm_value *args, int nargs,
			 union vm_value *ret);
int vm_time(void);
void vm_sleep(int ms);

void hll_call(int libno, int fno, int result_tag);
/* 当前正在执行的 CALLHLL 的泛型结果类型（ain_data_type）；-1 表示未标注 */
int hll_generic_result_type(void);
int hll_current_fno(void);
bool library_exists(int libno);
bool library_function_exists(int libno, int fno);
void init_libraries(void);
void exit_libraries(void);

// 注意：这里的值与 ain_data_type 不重叠，理论上可以并进那个枚举。
enum string_format_type {
	STRFMT_INT = 2,
	STRFMT_FLOAT = 3,
	STRFMT_STRING = 4,
	STRFMT_BOOL = 48,
	STRFMT_LONG_INT = 56,
};
struct string *string_format(struct string *fmt, union vm_value arg, enum string_format_type type);

void vm_stack_trace(void);
_Noreturn void _vm_error(const char *fmt, ...);
_Noreturn void vm_exit(int code);
_Noreturn void vm_reset(void);

extern bool vm_reset_once;

#if defined(__ANDROID__) || defined(RELEASE)
// 用系统弹窗报告错误并退出（Android / RELEASE 构建）。
#define VM_ERROR ERROR
#else
#define VM_ERROR(fmt, ...) \
	_vm_error("*ERROR*(%s:%s:%d): " fmt "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)
#endif


/* [D154] 结构体创建现场（探针用）：打印调用栈的返回地址。 */
void vm_dbg_alloc_trace(int struct_no, int slot);

#ifdef VM_PRIVATE
#include "system4/little_endian.h"
#include "system4/ain.h"

struct function_call {
	int32_t fno;
	uint32_t call_address;
	uint32_t return_address;
	int32_t page_slot;
	int32_t struct_page;
	int32_t struct_page_refed;  /* 仅当 set_struct_page 真的加过引用时为真 */
	int32_t entry_sp;           /* 弹完实参之后的操作数栈深度 */
};

extern struct function_call call_stack[4096];
extern int32_t call_stack_ptr;

extern size_t instr_ptr;

// 读取当前指令的第 N 个立即数。
static inline int32_t get_argument(int n)
{
	return LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
}

// XXX：严格来说不可移植（依赖 float 的位模式）
static inline float get_argument_float(int n)
{
	union vm_value v;
	v.i = LittleEndian_getDW(ain->code, instr_ptr + 2 + n*4);
	return v.f;
}

uint32_t get_switch_address(int no, int val);
uint32_t get_strswitch_address(int no, struct string *str);

#endif /* VM_PRIVATE */

/* 睡眠存档（VM 全量快照）。不只是 vm.c 的 syscall 在用：HLL 侧
 * `system.ResumeSave/ResumeLoad`（src/hll/system.c，[D496]）也直接调，
 * 所以必须放在 VM_PRIVATE 守卫之外。 */
int vm_save_image(const char *key, const char *path);
void vm_load_image(const char *key, const char *path);
struct page *vm_load_image_comments(const char *key, const char *path, int *success);
int vm_write_image_comments(const char *key, const char *path, struct page *comments);

#endif /* SYSTEM4_VM_H */
