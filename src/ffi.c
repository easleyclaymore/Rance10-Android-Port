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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ffi.h>
#include "system4/ain.h"
#include "system4/utfsjis.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

#define HLL_MAX_ARGS 64

struct hll_function {
	void *fun;
	ffi_cif cif;
	unsigned int nr_args;
	ffi_type **args;
	ffi_type *return_type;
	int is_stub;            /* [D110] HLL_TODO_EXPORT 且已被零值桩接管 */
};

static struct hll_function **libraries = NULL;

/* ---------------- [D110] 未实现 HLL 的软桩与触达清单 ----------------
 * HLL_TODO_EXPORT 展开为 { .name=..., .fun=NULL }，hll_call() 一旦执行到
 * 就 VM_ERROR -> sys_exit(1)，进程当场死亡：只能看到"最深的那一次"，
 * 后面还有什么没实现永远不知道。
 *
 * XSYS4_SOFT_STUB=1 时把这些导出换成"按返回类型给零值"的桩：
 *   - 参数仍走原有 ffi 编组路径，栈平衡/引用计数逻辑一字未改；
 *   - 每个桩首次触达在清单里记一行，之后只累加计数；
 *   - XSYS4_STUB_BT=1 时给前 32 个桩打印 VM 调用栈；
 *   - 清单默认 $TMPDIR/xsys4_stubs.txt（未设则当前目录），
 *     可用 XSYS4_STUB_LOG 指定；Core Layer 不写死绝对路径。
 * 不设 XSYS4_SOFT_STUB 时行为与以前完全一致（仍然 VM_ERROR 退出）。
 */
static int soft_stub_enabled(void)
{
	static int mode = -1;
	if (mode < 0) {
		const char *e = getenv("XSYS4_SOFT_STUB");
		mode = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
	}
	return mode;
}

static void stub_ret_void(void) { }
static int32_t stub_ret_i32(void) { return 0; }
static int64_t stub_ret_i64(void) { return 0; }
static float stub_ret_f32(void) { return 0.0f; }
static double stub_ret_f64(void) { return 0.0; }
static void *stub_ret_ptr(void) { return NULL; }

static void *soft_stub_for(ffi_type *ret)
{
	if (!ret || ret == &ffi_type_void)
		return (void *)stub_ret_void;
	switch (ret->type) {
	case FFI_TYPE_POINTER: return (void *)stub_ret_ptr;
	case FFI_TYPE_FLOAT:   return (void *)stub_ret_f32;
	case FFI_TYPE_DOUBLE:  return (void *)stub_ret_f64;
	case FFI_TYPE_SINT64:
	case FFI_TYPE_UINT64:  return (void *)stub_ret_i64;
	default:               return (void *)stub_ret_i32;
	}
}

#define STUB_MAX 512
struct stub_hit {
	int libno;
	int fno;
	unsigned long count;
};
static struct stub_hit stub_hits[STUB_MAX];
static int stub_nr = 0;
static int stub_nr_bt = 0;
static int stub_replaced = 0;   /* [D110b] 链接期被零值桩接管的导出数 */

static void stub_summary(void);
static void hll_hot_summary(void);

/* [D286] 全库 HLL 调用热力表（`XSYS4_HLL_HOT=1`）。
 *
 * 动机：启动期 544 条 `Unimplemented`，但绝大多数从未被调用过 ——
 * `GUIEngine` 336 个缺口、运行期零调用（菜单/表单系统没进去）。
 * 按缺口字母顺序补实现是纯浪费，必须按「跑过的场景里实际调了多少次」排。
 *
 * 全量数组而不是哈希：AIN 总共 36 个库、1586 个函数，单库最大 804，
 * 每格一个 `unsigned` 共 6KB。用固定上限查边界就能兼容任何 AIN 版本。 */
#define HLL_HOT_MAX_LIBS  64
#define HLL_HOT_MAX_FUNCS 1024
static unsigned hll_hot[HLL_HOT_MAX_LIBS][HLL_HOT_MAX_FUNCS];
static int hll_hot_init = -1;

static int hll_hot_enabled(void)
{
	if (hll_hot_init < 0) {
		const char *e = getenv("XSYS4_HLL_HOT");
		hll_hot_init = (e && *e && *e != '0') ? 1 : 0;
		if (hll_hot_init)
			atexit(hll_hot_summary);
	}
	return hll_hot_init;
}

static void hll_hot_note(int libno, int fno)
{
	if (!hll_hot_enabled())
		return;
	if (libno < 0 || libno >= HLL_HOT_MAX_LIBS ||
	    fno < 0 || fno >= HLL_HOT_MAX_FUNCS)
		return;
	hll_hot[libno][fno]++;
}

/* 退出时吐表。每行一个函数，带库名 / 函数名 / 调用次数 / 是否有 C 实现。
 * 按次数倒序，让 `tools/hll-hotness.py` 能直接当输入吃。 */
static void hll_hot_summary(void)
{
	if (!hll_hot_init || !ain)
		return;
	NOTICE("[hllhot] ===== 全库 HLL 调用热力表（次数倒序） =====");
	/* 不做全量排序（1586 项，简单选择排序够用且无需分配内存） */
	unsigned last = (unsigned)-1;
	for (int round = 0; round < 2000; round++) {
		int bl = -1, bf = -1;
		unsigned bv = 0;
		for (int l = 0; l < HLL_HOT_MAX_LIBS && l < ain->nr_libraries; l++) {
			int nf = ain->libraries[l].nr_functions;
			for (int f = 0; f < nf && f < HLL_HOT_MAX_FUNCS; f++) {
				unsigned v = hll_hot[l][f];
				if (v > bv && v < last) { bv = v; bl = l; bf = f; }
			}
		}
		if (bl < 0)
			break;
		last = bv;
		struct ain_library *lib = &ain->libraries[bl];
		struct ain_hll_function *fn = &lib->functions[bf];
		NOTICE("[hllhot] %-20s %-40s %8u", lib->name,
		       fn->name ? fn->name : "?", bv);
	}
}

static FILE *stub_log_file(void)
{
	static FILE *fp = NULL;
	static int inited = 0;
	if (!inited) {
		inited = 1;
		const char *p = getenv("XSYS4_STUB_LOG");
		char path[512];
		if (!p || !*p) {
			/* [L3] Core 层不允许写死 /tmp 路径：要用平台的
			 * 临时目录，取不到再退回当前工作目录。 */
			const char *tmp = getenv("TMPDIR");
			snprintf(path, sizeof(path), "%s/xsys4_stubs.txt",
				 (tmp && *tmp) ? tmp : ".");
			p = path;
		}
		fp = fopen(p, "w");
		if (fp) {
			fprintf(fp, "# xsystem4 soft-stub hit list (XSYS4_SOFT_STUB=1)\n# lib.func  args  ret\n");
			fflush(fp);
			atexit(stub_summary);
		}
	}
	return fp;
}

static void stub_summary(void)
{
	FILE *fp = stub_log_file();
	if (!fp)
		return;
	fprintf(fp, "\n# ===== summary: %d distinct unimplemented HLL functions hit =====\n", stub_nr);
	for (int i = 0; i < stub_nr; i++) {
		struct ain_library *lib = &ain->libraries[stub_hits[i].libno];
		struct ain_hll_function *f = &lib->functions[stub_hits[i].fno];
		fprintf(fp, "%8lu  %s.%s\n", stub_hits[i].count, lib->name, f->name);
	}
	fflush(fp);
}

static void stub_note(int libno, int fno)
{
	FILE *fp = stub_log_file();
	if (!fp)
		return;
	for (int i = 0; i < stub_nr; i++) {
		if (stub_hits[i].libno == libno && stub_hits[i].fno == fno) {
			stub_hits[i].count++;
			return;
		}
	}
	if (stub_nr >= STUB_MAX)
		return;
	struct ain_library *lib = &ain->libraries[libno];
	struct ain_hll_function *f = &lib->functions[fno];
	stub_hits[stub_nr].libno = libno;
	stub_hits[stub_nr].fno = fno;
	stub_hits[stub_nr].count = 1;
	stub_nr++;
	fprintf(fp, "[stub %03d] %s.%s  args=%d ret=%s\n", stub_nr, lib->name, f->name,
	        (int)f->nr_arguments, ain_strtype(ain, f->return_type.data, -1));
	fflush(fp);
	static int bt = -1;
	if (bt < 0) {
		const char *e = getenv("XSYS4_STUB_BT");
		bt = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
	}
	if (bt && stub_nr_bt < 32) {
		stub_nr_bt++;
		NOTICE("[stub] backtrace for %s.%s:", lib->name, f->name);
		vm_stack_trace();
	}
}


bool library_exists(int libno)
{
	return libraries[libno];
}

bool library_function_exists(int libno, int fno)
{
	/* [D110] 软桩只是运行期兜底，审计时仍算"未实现"。 */
	return libraries[libno] && libraries[libno][fno].fun && !libraries[libno][fno].is_stub;
}

/*
 * HLL 函数调用的简易跟踪设施（默认关，见下面的 TRACE_HLL）。
 */
//#define TRACE_HLL
#ifdef TRACE_HLL
#include "system4/string.h"

struct traced_library {
	const char *name;
	bool (*should_trace)(struct ain_hll_function *f, union vm_value **args);
};

static bool chipmunk_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"SYSTEM_SetReadMessageSkipping",
		"Update",
		"KeepPreviousView",
		"Sleep",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	return true;
}

#include "parts.h"
#include "parts/parts_internal.h"
static bool gui_engine_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"Parts_UpdateMotionTime",
		"UpdateInputState",
		"UpdateComponent",
		"UpdateParts",
		"GetMessageType",
		"ReleaseMessage",
		"Parts_SetPartsPixelDecide",
		"Parts_GetPartsShow",
		"Parts_IsCursorIn",
		"Parts_GetPartsAlpha",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	if (!strcmp(f->name, "Parts_SetPartsCG")) {
		static char *u = NULL;
		if (!u)
			u = utf2sjis("システム／ボタン／メニュー／通常", 0);
		struct string ***strs = (void*)args;
		struct string *s = *strs[1];
		return strcmp(s->text, u);
	}
	if (!strcmp(f->name, "Parts_SetPos")) {
		if (args[1]->i < 0 || args[2]->i < 0)
			return false;
		return PE_GetPartsX(args[0]->i) != args[1]->i || PE_GetPartsY(args[0]->i) != args[2]->i;
	}
	if (!strcmp(f->name, "Parts_SetZ"))
		return PE_GetPartsZ(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetShow"))
		return PE_GetPartsShow(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetAlpha"))
		return PE_GetPartsAlpha(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "Parts_SetPartsCGSurfaceArea")) {
		return false;
		struct parts *p = parts_try_get(args[0]->i);
		if (!p)
			return true;
		Rectangle *r = &p->states[args[5]->i].common.surface_area;
		return args[1]->i != r->x || args[2]->i != r->y
			|| args[3]->i != r->w || args[4]->i != r->h;
	}
	if (!strcmp(f->name, "Parts_SetAddColor")) {
		struct parts *p = parts_try_get(args[0]->i);
		if (!p)
			return true;
		SDL_Color *c = &p->local.add_color;
		return args[1]->i != c->r || args[2]->i != c->g || args[3]->i != c->b;
	}
	if (!strcmp(f->name, "Parts_GetPartsX"))
		return false;
	return true;
}

static bool parts_engine_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	if (!strcmp(f->name, "Update"))
		return false;
	if (!strcmp(f->name, "SetAddColor"))
		return false;
	if (!strcmp(f->name, "SetAlpha"))
		return PE_GetPartsAlpha(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "SetPartsCGSurfaceArea"))
		return args[1]->i >= 0 && args[2]->i >= 0;
	if (!strcmp(f->name, "SetPartsConstructionSurfaceArea"))
		return args[1]->i >= 0 && args[2]->i >= 0;
	if (!strcmp(f->name, "SetPartsMagX"))
		return args[1]->f > 1.001 || args[1]->f < -1.001;
	if (!strcmp(f->name, "SetPartsMagY"))
		return args[1]->f > 1.001 || args[1]->f < -1.001;
	if (!strcmp(f->name, "SetPartsOriginPosMode"))
		return PE_GetPartsOriginPosMode(args[0]->i) != args[1]->i;
	if (!strcmp(f->name, "SetPartsRotateZ"))
		return args[1]->f > 0.001 || args[1]->f < -0.001;
	if (!strcmp(f->name, "SetPos"))
		return PE_GetPartsX(args[0]->i) != args[1]->i || PE_GetPartsX(args[0]->i) != args[2]->i;
	if (!strcmp(f->name, "SetShow")) {
		if (args[0]->i == 0)
			return false; // ???
		return PE_GetPartsShow(args[0]->i) != args[1]->i;
	}
	if (!strcmp(f->name, "SetZ"))
		return PE_GetPartsZ(args[0]->i) != args[1]->i;
	return true;
}

static bool sact_should_trace(struct ain_hll_function *f, union vm_value **args)
{
	const char *no_trace[] = {
		"Key_IsDown",
		"Mouse_ClearWheel",
		"Mouse_GetPos",
		"Mouse_GetWheel",
		"SP_GetWidth",
		"SP_GetHeight",
		"SP_ExistAlpha",
		"Update",
	};
	for (unsigned i = 0; i < sizeof(no_trace) / sizeof(*no_trace); i++) {
		if (!strcmp(f->name, no_trace[i]))
			return false;
	}
	return true;
}

struct traced_library traced_libraries[] = {
	{ "ChipmunkSpriteEngine", chipmunk_should_trace },
	{ "GUIEngine", gui_engine_should_trace },
	{ "PartsEngine", parts_engine_should_trace },
	{ "SACTDX", sact_should_trace },
};

static void print_hll_trace(struct ain_library *lib, struct ain_hll_function *f,
		      struct hll_function *fun, union vm_value *r, void *_args)
{
	sys_message("(%s) ", display_sjis0(ain->functions[call_stack[call_stack_ptr-1].fno].name));
	sys_message("%s.%s(", lib->name, f->name);
	union vm_value **args = _args;
	for (int i = 0; i < f->nr_arguments; i++) {
		if (i > 0) {
			sys_message(", ");
		}
		sys_message("%s=", display_sjis0(f->arguments[i].name));
		switch (f->arguments[i].type.data) {
		case AIN_INT:
		case AIN_LONG_INT:
			sys_message("%d", args[i]->i);
			break;
		case AIN_FLOAT:
			sys_message("%f", args[i]->f);
			break;
		case AIN_STRING: {
			struct string ***strs = _args;
			struct string *s = *strs[i];
			sys_message("\"%s\"", display_sjis0(s->text));
			break;
		}
		case AIN_BOOL:
			sys_message("%s", args[i]->i ? "true" : "false");
			break;
		case AIN_STRUCT:
			sys_message("<struct>");
			break;
		case AIN_ARRAY_TYPE:
			sys_message("<array>");
			break;
		case AIN_REF_TYPE:
			sys_message("<ref>");
			break;
		default:
			sys_message("<%d>", f->arguments[i].type.data);
			break;
		}
	}
	sys_message(")");

	ffi_call(&fun->cif, (void*)fun->fun, r, _args);

	switch (f->return_type.data) {
	case AIN_VOID:
		break;
	case AIN_INT:
		sys_message(" -> %d", r->i);
		break;
	case AIN_FLOAT:
		sys_message(" -> %f", r->f);
		break;
	case AIN_STRING:
		sys_message(" -> \"%s\"", display_sjis0(((struct string*)r->ref)->text));
		break;
	case AIN_BOOL:
		sys_message(" -> %s", r->i ? "true" : "false");
		break;
	default:
		sys_message(" -> <%d>", f->return_type.data);
		break;
	}
	for (int i = 0; i < f->nr_arguments; i++) {
		union vm_value ***args = _args;
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
			sys_message(" (%s=%d)", display_sjis0(f->arguments[i].name), (*args[i])->i);
			break;
		case AIN_REF_FLOAT:
			sys_message(" (%s=%f)", display_sjis0(f->arguments[i].name), (*args[i])->f);
			break;
		default:
			break;
		}
	}
	sys_message("\n");
}

static void trace_hll_call(struct ain_library *lib, struct ain_hll_function *f,
		      struct hll_function *fun, union vm_value *r, void *_args)
{
	for (unsigned i = 0; i < sizeof(traced_libraries)/sizeof(*traced_libraries); i++) {
		struct traced_library *l = &traced_libraries[i];
		if (!strcmp(lib->name, l->name)) {
			if (!l->should_trace || l->should_trace(f, _args)) {
				print_hll_trace(lib, f, fun, r, _args);
				return;
			}
			break;
		}
	}

	ffi_call(&fun->cif, (void*)fun->fun, r, _args);
}
#endif /* TRACE_HLL */

/* 当前正在执行的这段 HLL 函数体所属 CALLHLL 的类型标签。
 * 通过 hll_generic_result_type() 暴露给 HLL 实现，
 * 这样泛型 Array/Container 调用才能实例化出类型正确的元素。 */
static int hll_result_tag = -1;
static int hll_call_fno = -1;

int hll_generic_result_type(void)
{
	return hll_result_tag;
}

int hll_current_fno(void)
{
	return hll_call_fno;
}

/* 一个 AIN_IFACE / AIN_WRAP 值占两个栈槽：
 * [对象堆槽, 虚表偏移]。兰斯10 通过泛型数组元素类型
 * （AIN_HLL_PARAM / AIN_REF_HLL_PARAM）传递和返回这类值，
 * 具体元素类型放在 CALLHLL 的第三个立即数里，
 * 所以编组层必须知道「泛型值什么时候是两槽宽」。 */
static bool hll_tag_is_two_slot(int tag)
{
	return tag == AIN_IFACE || tag == AIN_WRAP || tag == AIN_IFACE_WRAP;
}

/* 指向堆对象的引用，本身就是对象句柄（一个栈值）；
 * 指向「页里的值」的引用，则是两个值 [页, 变量号] 的对。
 * 这不是从运行期行为推出来的：AIN 变量槽普查
 * （logs/ain_spec_slots.txt）显示 AIN_REF_INT/FLOAT/
 * BOOL/ENUM、AIN_WRAP 和 AIN_IFACE 后面总跟着一个 AIN_VOID
 * 占位槽，而 AIN_REF_STRUCT/STRING/DELEGATE/ARRAY 只占一个槽；
 * 上面那套 ref 实参编组用的是同一张两槽清单。
 * 独立佐证：tools/ain_refparam.c 把 Rance10.ain 里全部 452 个
 * 带类型的 AIN_REF_HLL_PARAM 调用点，按后随字节码怎么消费结果
 * 分了类 —— 标签 10/47/89/92 是 100% 的 R_ASSIGN（两个值），
 * 标签 12/13/21 是 100% 的 ASSIGN/CALLMETHOD（一个值），无一例外
 * （logs/ain_refparam.txt）。
 * See Rance10_AINv12_字节码规范-20261107.md, "引用槽宽" . */
static int hll_ref_result_is_single_value(int tag)
{
	switch (tag) {
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_ARRAY:
	case AIN_DELEGATE:
	case AIN_FUNC_TYPE:
	case AIN_REF_STRING:
	case AIN_REF_STRUCT:
	case AIN_REF_ARRAY:
	case AIN_REF_DELEGATE:
	case AIN_REF_FUNC_TYPE:
	case AIN_ARRAY_INT:
	case AIN_ARRAY_FLOAT:
	case AIN_ARRAY_STRING:
	case AIN_ARRAY_STRUCT:
	case AIN_ARRAY_FUNC_TYPE:
	case AIN_ARRAY_BOOL:
	case AIN_ARRAY_LONG_INT:
	case AIN_ARRAY_DELEGATE:
	case AIN_REF_ARRAY_INT:
	case AIN_REF_ARRAY_FLOAT:
	case AIN_REF_ARRAY_STRING:
	case AIN_REF_ARRAY_STRUCT:
	case AIN_REF_ARRAY_FUNC_TYPE:
	case AIN_REF_ARRAY_BOOL:
	case AIN_REF_ARRAY_LONG_INT:
	case AIN_REF_ARRAY_DELEGATE:
		return 1;
	default: /* int/float/bool/long/enum/iface/wrap... -> [page, var] */
		return 0;
	}
}

void hll_call(int libno, int fno, int result_tag)
{
	/* [D286] `XSYS4_HLL_HOT=1`：全库 HLL 调用计数，退出时汇总。
	 *
	 * 为什么要它：启动期 544 条 `Unimplemented`，但**大部分从来没被调用过**
	 * —— `GUIEngine` 336 个缺口、运行期零调用（菜单/表单系统我们还没进去）。
	 * 按缺口字母顺序补实现是纯浪费；必须按「跑过的场景里实际被调了多少次」排。
	 *
	 * 为什么不用 `XSYS4_HLL_HIST`：那个一次只盯一个库、每秒打一次直方图，
	 * 要扫 36 个库就得跑 36 次（而且限流后的条数不能当次数，见 D280 的教训）。
	 * 这里一次跑完、进程退出时统一吐表。 */
	hll_hot_note(libno, fno);

	/* [D110b] 首次 HLL 调用时落一次自检：清单为空时也能区分
	 * "一路没踩到空桩" 和 "软桩机制根本没生效"。 */
	if (soft_stub_enabled()) {
		static int self_checked = 0;
		if (!self_checked) {
			self_checked = 1;
			FILE *sfp = stub_log_file();
			if (sfp) {
				fprintf(sfp, "# link-time: %d unimplemented exports replaced by zero-stubs\n", stub_replaced);
				fflush(sfp);
			}
			NOTICE("[stub] soft-stub active: %d unimplemented exports replaced", stub_replaced);
		}
	}
	hll_call_fno = fno;
	struct ain_hll_function *f = &ain->libraries[libno].functions[fno];
	static int hll_trace = -1;
	if (hll_trace < 0) {
		const char *t = getenv("XSYS4_HLL_TRACE");
		hll_trace = t ? 1 : 0;
	}
	if (hll_trace) {
		/* XSYS4_HLL_TRACE_FILTER 收窄轨迹：`27:73,27:170` 按库/序号，
		 * `PartsEngine` 这样带大写的词也匹配库名，其它写法按函数名子串。
		 * 不设则全打（很吵）。 */
		const char *flt = getenv("XSYS4_HLL_TRACE_FILTER");
		int show = flt == NULL;
		if (flt) {
			const char *p = flt;
			while (*p && !show) {
				const char *comma = strchr(p, ',');
				size_t len = comma ? (size_t)(comma - p) : strlen(p);
				if (len > 0) {
					const char *colon = memchr(p, ':', len);
					if (colon) {
						int l = atoi(p), f2 = atoi(colon + 1);
						if (l == libno && f2 == fno)
							show = 1;
					} else if (strlen(f->name) >= len &&
						   strncasecmp(f->name, p, len) == 0) {
						show = 1;
					} else if (strstr(ain->libraries[libno].name, p)) {
						/* 按库名（如 PartsEngine / SceneStack）收窄 */
						show = 1;
					}
				}
				if (!comma)
					break;
				p = comma + 1;
			}
		}
		if (show)
			NOTICE("[hll] ip=0x%zX lib=%d fno=%d tag=%d nr_args=%d sp=%d ret=%d %s.%s",
			       instr_ptr, libno, fno, result_tag, f->nr_arguments, stack_ptr,
			       f->return_type.data, ain->libraries[libno].name, f->name);
	}

	if (!libraries[libno])
		VM_ERROR("Unimplemented HLL function: %s.%s", ain->libraries[libno].name, f->name);

	struct hll_function *fun = &libraries[libno][fno];
	if (!fun->fun)
		VM_ERROR("Unimplemented HLL function: %s.%s", ain->libraries[libno].name, f->name);
	if (fun->is_stub)
		stub_note(libno, fno);

	void *args[HLL_MAX_ARGS];
	void *ptrs[HLL_MAX_ARGS];
	// 把 ref 实参再拷一份到栈上，防止 HLL 调用期间堆被重新分配。
	// （Heap realloc 会让传出去的指针失效。）
	void *heap_ptrs[HLL_MAX_ARGS];
	int heap_slots[HLL_MAX_ARGS];
	/* AIN_HLL_PARAM 是 8 字节的值，而 VM 的栈槽是 4 字节。
	 * 所以要造出满宽的临时变量，免得 libffi 读到相邻的栈槽
	 * （那会产生 0x7F0000003E 这样的值）。 */
	uint64_t hll_values[HLL_MAX_ARGS];
	memset(heap_slots, 0, sizeof(heap_slots));
	memset(hll_values, 0, sizeof(hll_values));

	for (int i = f->nr_arguments - 1; i >= 0; i--) {
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
		case AIN_REF_LONG_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT: {
			// 立即数式的 ref 类型需要先造出指针
			stack_ptr -= 2;
			int pageno = stack[stack_ptr].i;
			int varno  = stack[stack_ptr+1].i;
			ptrs[i] = &heap[pageno].page->values[varno];
			args[i] = &ptrs[i];
			break;
		}
		case AIN_STRING:
			stack_ptr--;
			args[i] = &heap[stack[stack_ptr].i].s;
			break;
		case AIN_REF_STRING:
			stack_ptr--;
			heap_slots[i] = stack[stack_ptr].i;
			heap_ptrs[i] = heap[stack[stack_ptr].i].s;
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		case AIN_STRUCT:
		case AIN_ARRAY_TYPE:
			stack_ptr--;
			args[i] = &heap[stack[stack_ptr].i].page;
			break;
		case AIN_REF_STRUCT:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY:   /* 80: null 时预分配一个空数组，否则沿用原 slot */
			stack_ptr--;
			heap_slots[i] = stack[stack_ptr].i;
			if (heap_slots[i] <= 0) {
				/* 调用者的数组变量是 null：先给它一个空数组对象，
				 * 否则 HLL 函数无法把新建的数组写回（ref 只给了值，没给变量位置） */
				struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0);
				p->array.rank = 1;
				p->array.struct_type = -1;
				heap_slots[i] = heap_alloc_page(p);
				stack[stack_ptr].i = heap_slots[i];
			}
			heap_ptrs[i] = heap[heap_slots[i]].page;
			/* D72：ref array 形参必须指向一张 ARRAY_PAGE。有些
			 * 部件 UI 的全局变量放的是 0 槽占位页；Array.PushBack
			 * 会跳过它们，数组于是永远长不大。这里统一成空数组。 */
			struct page *cur = (struct page *)heap_ptrs[i];
			/* [FIX D99] 槽活着但页是 NULL 的情况也要统一：
			 * 原来那句 `cur &&` 守卫跳过了这种情况，把 NULL 传给 HLL，
			 * 于是 Array.Numof() 返回 0、CreateHandle 产出 -1。 */
			if (!cur || cur->type != ARRAY_PAGE) {
				if (getenv("XSYS4_ARR_DBG"))
					NOTICE("[ARR] subst slot=%d ref=%d page=%p type=%d fno=%d",
					       heap_slots[i], heap_slots[i] > 0 ? heap[heap_slots[i]].ref : -1,
					       (void*)cur, cur ? cur->type : -1, hll_current_fno());
				if (getenv("XSYS4_SUBST_BT")) {
					NOTICE("[ARR] subst backtrace for slot=%d:", heap_slots[i]);
					vm_stack_trace();
				}
				struct page *np = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0);
				np->array.rank = 1;
				np->array.struct_type = -1;
				heap_ptrs[i] = np;
			}
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		case AIN_HLL_PARAM:
			if (hll_tag_is_two_slot(result_tag)) {
				/* [对象, 虚表偏移]：对象就是值。 */
				stack_ptr -= 2;
				hll_values[i] = (uint32_t)stack[stack_ptr].i;
			} else {
				stack_ptr--;
				hll_values[i] = (uint32_t)stack[stack_ptr].i;
			}
			args[i] = &hll_values[i];
			break;
		case AIN_HLL_FUNC:
			/* D59/D68：v12 的 T_FUNC 实参是 [对象, 函数号] 对；库 5 和库 11 都压两个值。 */
			if (libno == 5 || libno == 11) {
				stack_ptr -= 2;
				hll_values[i] = (uint32_t)stack[stack_ptr + 1].i;
			} else {
				stack_ptr--;
				hll_values[i] = (uint32_t)stack[stack_ptr].i;
			}
			args[i] = &hll_values[i];
			break;
		case AIN_REF_DELEGATE:
		case AIN_DELEGATE:
			/* Delegate.Set 期望的是委托页指针（D59）。 */
			stack_ptr--;
			heap_slots[i] = stack[stack_ptr].i;
			heap_ptrs[i] = (heap_slots[i] > 0 && heap_slots[i] < heap_size) ? heap[heap_slots[i]].page : NULL;
			ptrs[i] = &heap_ptrs[i];
			args[i] = &ptrs[i];
			break;
		default:
			stack_ptr--;
			args[i] = &stack[stack_ptr];
			break;
		}
	}

	union vm_value r;
	int sp_after_args = stack_ptr;
	hll_result_tag = result_tag;
#ifdef TRACE_HLL
	trace_hll_call(&ain->libraries[libno], f, fun, &r, args);
#else
	ffi_call(&fun->cif, (void*)fun->fun, &r, args);
#endif
	hll_result_tag = -1;


	for (int i = 0, j = 0; i < f->nr_arguments; i++, j++) {
		// XXX：把 ref 实参传给 HLL 时我们没有加引用计数，
		//      所以这里要避免再通过 variable_fini 减一次
		switch (f->arguments[i].type.data) {
		case AIN_REF_INT:
		case AIN_REF_LONG_INT:
		case AIN_REF_BOOL:
		case AIN_REF_FLOAT:
		case AIN_HLL_FUNC:
			if (libno == 5 || libno == 11)
				j++;
			break;
		case AIN_REF_STRING:
			/* D75：槽 0 是保留的全局页，绝不能被覆盖。 */
			if (heap_slots[i] > 0 && heap_slots[i] < heap_size)
				heap[heap_slots[i]].s = heap_ptrs[i];
			break;
		case AIN_REF_STRUCT:
		case AIN_REF_ARRAY_TYPE:
		case AIN_REF_ARRAY:   /* 80 */
			if (heap_slots[i] > 0 && heap_slots[i] < heap_size) {
				/* [D153] 与 Array.c 的 [arralloc] 配对：这行打不出来
				 * 就说明 HLL 改了容器却没写回成员。 */
				if (getenv("XSYS4_ARR_ALLOC_DBG") && libno == 5)
					NOTICE("[arrwb] slot=%d old=%p new=%p",
					       heap_slots[i], (void *)heap[heap_slots[i]].page,
					       (void *)heap_ptrs[i]);
				heap[heap_slots[i]].page = heap_ptrs[i];
			}
			break;
		case AIN_REF_FUNC_TYPE:
		case AIN_REF_DELEGATE:
		case AIN_DELEGATE:
			/* D69：HLL 可能重新分配过委托页，要写回去。 */
			if (heap_slots[i] > 0 && heap_slots[i] < heap_size)
				heap[heap_slots[i]].page = heap_ptrs[i];
			break;
		case AIN_ARRAY_TYPE:
			// Sys41VM 在按值传数组时不做拷贝。
			if (ain->version <= 1)
				break;
			// fallthrough
		default:
			variable_fini(stack[stack_ptr+j], f->arguments[i].type.data, false);
			break;
		}
	}

	int slot;
	switch (f->return_type.data) {
	case AIN_VOID:
		break;
	case AIN_STRING:
		/* 声明返回字符串的函数，必须交回一个 `struct string *`。
		 * 交回一个小整数说明 C 绑定和声明对不上（参数个数不对，或者是空桩），
		 * 存进槽里就得到一个「字符串」，它的负载会让第一个读它的 %s 崩掉。
		 * 所以让这个槽保持空，
		 * 并把肇事函数名报一次。 */
		if ((uintptr_t)r.ref < 0x1000) {
			static int n;
			if (n++ < 20)
				WARNING("%s.%s returned %p where a string was declared",
					ain->libraries[libno].name, f->name, r.ref);
			r.ref = NULL;
		}
		slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = r.ref;
		stack_push(slot);
		break;
	case AIN_BOOL:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
		stack_push(*(bool*)&r);
#pragma GCC diagnostic pop
		break;
    case AIN_REF_HLL_PARAM: {
        /* HLL 的 ref 形参返回的是一个元素地址。只能拿它和本次调用的
         * 实参槽比对：全堆扫描会匹配到一个过期/别名的槽，
         * 造出一个悬空引用。 */
        void *addr = r.ref;
        int32_t hi = -1, pi = -1;
        struct page *elem_page = NULL;
        if (addr) {
            for (int i = 0; i < f->nr_arguments; i++) {
                int slot = heap_slots[i];
                if (slot <= 0 || slot >= heap_size)
                    continue;
                if (heap[slot].type != VM_PAGE)
                    continue;
                /* [FIX D94] 拿 `addr` 去和 HLL 调用留在 heap_ptrs[i] 里的那张页比对，
                 * 而不是 heap[slot].page：被调方可能重新分配过数组
                 * （Array.EmplaceBack 就会做 `*a = np`），而那张新页要等
                 * 这个结果块之后才写回 heap[slot].page。
                 * 拿过期的页去比对，导致每个 tag=47 的 Array.EmplaceBack
                 * 都返回 (hi=-1,pi=-1)，于是 CASTimerManager@CreateHandle
                 * 产出非法句柄，CheckHandle 报「manager deleted」——
                 * 帧计时器停了，
                 * 场景再也走不出 WaitFrame。 */
                struct page *cand = NULL;
                switch (f->arguments[i].type.data) {
                case AIN_REF_ARRAY:
                case AIN_REF_ARRAY_TYPE:
                case AIN_REF_STRUCT:
                    cand = (struct page *)heap_ptrs[i];
                    break;
                default:
                    break;
                }
                struct page *pg = heap[slot].page;
                for (int c = 0; c < 2; c++) {
                    struct page *try_pg = c == 0 ? cand : pg;
                    if (!try_pg)
                        continue;
                    union vm_value *base = try_pg->values;
                    if ((union vm_value *)addr >= base &&
                        (union vm_value *)addr < base + try_pg->nr_vars) {
                        hi = slot;
                        pi = (int32_t)((union vm_value *)addr - base);
                        elem_page = try_pg;
                        break;
                    }
                }
                if (hi > 0)
                    break;
            }
        }
        /* AIN v12 CALLHLL 的第三个立即数是这次泛型调用的具体元素类型。
         * AIN_REF_HLL_PARAM 返回的是一个引用，它在栈上怎么表示
         * 由那个类型决定（见前面辅助函数的注释），
         * 而不是靠猜一对 (下标, 值)。
         *
         * CASTimerManager@CreateHandle 的反汇编证明两种形态
         * 都在同一个函数里被消费：
         *   0x56a0 CALLHLL Array.EmplaceBack tag=13(AIN_STRUCT) -> 1 value,
         *          assigned to local[1] (AIN_REF_STRUCT), then DELETEd;
         *   0x56f6 CALLHLL Array.EmplaceBack tag=47(AIN_BOOL)  -> 2 values,
         *          stored by R_ASSIGN into local[2..3] (AIN_REF_BOOL +
         *          AIN_VOID placeholder) and dereferenced by ASSIGN.
         * 把原始元素下标当作前导值返回，会让随后的 DELETE
         * 去 heap_unref() 一个毫不相干的堆槽（D31）。 */
        /* 这个结果占几个槽，由 CALLHLL 的具体元素类型决定；
         * 而且未解析的引用也必须留下正好那么多值：
         * 给一个单值标签压两个值的对，会让每次空的
         * `Array.Max`/`Array.Find` 都把操作数栈错开一个，
         * 从而把调用方的栈读进错误的槽
         * （`PlayerCardCollection@Add` 那次损坏）。 */
        bool is_iface = result_tag >= 0 && hll_tag_is_two_slot(result_tag);
        bool is_single = result_tag >= 0 && hll_ref_result_is_single_value(result_tag);
        bool resolved = addr && hi > 0;
        union vm_value elem = resolved ? *(union vm_value *)addr
                                       : (union vm_value){ .i = -1 };
        /* [D177] 引用型返回（`ref hll_param` / `ref array<?>` …）把 C 指针
         * 反解成 AIN 的 `(page_slot, index)` 二元组。反解失败的后果是**静默**
         * 的：结果退化成 (-1,-1)，上层把它当"空引用"，于是
         * `CASTimerManager@CheckHandle` 报「句柄错误」、场景直接退出。
         *
         * 默认关；打开后每一条引用返回都打一行，用来回答
         * "addr 落在谁的 values[] 里"。 */
        if (getenv("XSYS4_HLL_REFDBG")) {
            /* 只打"失败"和指定函数：全量打印会在 400 行处被自己的上限截断，
             * 恰好把要看的那一条切掉（第一次就踩了）。 */
            const char *want = getenv("XSYS4_HLL_REFDBG_FILTER");
            bool show = !resolved ||
                        (want && *want && f->name && strstr(f->name, want));
            static int _rn;
            if (show && _rn++ < 20000)
                NOTICE("[ref] %s.%s tag=%d addr=%p resolved=%d hi=%d pi=%d nargs=%d",
                       ain->libraries[libno].name, f->name, result_tag,
                       addr, resolved ? 1 : 0, hi, pi, f->nr_arguments);
        }

        if (is_iface) {
            /* [对象, 虚表偏移]；对象槽里是 -1 表示空接口。
             * 容器拥有它持有的那份引用，调用方会 DELETE 自己那份，
             * 所以这里给容器加一次引用。 */
            int iface = (resolved && elem_page && elem_page->type == ARRAY_PAGE)
                ? elem_page->array.struct_type : -1;
            int obj = -1, offset = 0;
            if (iface < 0 || !vm_iface_pair(iface, elem.i, &obj, &offset)) {
                obj = -1;
                offset = 0;
            } else if (obj > 0 && obj < heap_size && heap[obj].ref > 0) {
                heap_ref(obj);
            }
            stack_push((union vm_value){ .i = obj });
            stack_push((union vm_value){ .i = offset });
        } else if (is_single) {
            /* 一个堆句柄：容器保留自己那份引用，调用方
             * DELETE 自己那份（D21）。 */
            if (elem.i > 0 && elem.i < heap_size && heap[elem.i].ref > 0)
                heap_ref(elem.i);
            stack_push(elem);
        } else {
            /* [FIX D101] 引用形态返回 [hi, pi]，其中 hi 是页槽。
             * 随后 AIN 会对两者做 R_ASSIGN，并 DELETE local[2]（=hi），
             * 这就会 heap_unref() 那张页。
             * 容器本来就持有自己那份引用，
             * 所以不多加一次 ref 的话，这次 DELETE 会把它减到 0 并释放数组
             * （就是 CreateHandle 清理时观测到的 double free）。 */
            if (hi > 0 && hi < heap_size && heap[hi].ref > 0)
                heap_ref(hi);
            stack_push((union vm_value){ .i = hi });
            stack_push((union vm_value){ .i = pi });
        }
        break;
    }
	default:
		stack_push(r);
		break;
	}

	if (getenv("XSYS4_HLL_ARGDBG")) {
		/* 编组循环结束后，实参已经从栈上摘掉了，
		 * 所以唯一预期的变化就是声明的返回值。
		 * AIN_IFACE / AIN_WRAP / AIN_OPTION 返回会留下两个槽。 */
		int rt = f->return_type.data;
		int slots;
		if (rt == AIN_VOID)
			slots = 0;
		else if (rt == AIN_IFACE || rt == AIN_WRAP || rt == AIN_OPTION)
			slots = 2;
		else if (rt == AIN_REF_HLL_PARAM)
			/* 泛型返回值的宽度由 CALLHLL 的具体元素类型决定，
			 * 而不是由声明的类型决定（见
			 * hll_ref_result_is_single_value）。 */
			slots = (result_tag >= 0 &&
				 hll_ref_result_is_single_value(result_tag)) ? 1 : 2;
		else
			slots = 1;
		int expect = sp_after_args + slots;
		static int n;
		if (stack_ptr != expect && n++ < 60)
			NOTICE("[argdbg] %s.%s args=%d tag=%d sp_args=%d sp_end=%d expect=%d ret=%d",
			       ain->libraries[libno].name, f->name, f->nr_arguments,
			       result_tag, sp_after_args, stack_ptr, expect, rt);
	}
}

extern struct static_library lib_ACXLoader;
extern struct static_library lib_ACXLoaderP2;
extern struct static_library lib_ADVSYS;
extern struct static_library lib_AFAFactory;
extern struct static_library lib_AliceLogo;
extern struct static_library lib_AliceLogo2;
extern struct static_library lib_AliceLogo3;
extern struct static_library lib_AliceLogo4;
extern struct static_library lib_AliceLogo5;
extern struct static_library lib_AnteaterADVEngine;
extern struct static_library lib_Clipboard;
extern struct static_library lib_Delegate;
extern struct static_library lib_system;
extern struct static_library lib_EXWriter;
extern struct static_library lib_ADVEngine;
extern struct static_library lib_String;
extern struct static_library lib_InstallInfo;
extern struct static_library lib_Sys43VM;
extern struct static_library lib_TextFile;
extern struct static_library lib_TextSurfaceManager;
extern struct static_library lib_Int;
extern struct static_library lib_Float;
extern struct static_library lib_FileDialog;
extern struct static_library lib_PCInfo;
extern struct static_library lib_Array;
extern struct static_library lib_BanMisc;
extern struct static_library lib_Bitarray;
extern struct static_library lib_CalcTable;
extern struct static_library lib_CGManager;
extern struct static_library lib_ChipmunkSpriteEngine;
extern struct static_library lib_ChrLoader;
extern struct static_library lib_CommonSystemData;
extern struct static_library lib_Confirm;
extern struct static_library lib_Confirm2;
extern struct static_library lib_Confirm3;
extern struct static_library lib_CrayfishLogViewer;
extern struct static_library lib_Cursor;
extern struct static_library lib_DALKDemo;
extern struct static_library lib_DALKEDemo;
extern struct static_library lib_Data;
extern struct static_library lib_DataFile;
extern struct static_library lib_Discord;
extern struct static_library lib_DrawDungeon;
extern struct static_library lib_DrawDungeon2;
extern struct static_library lib_DrawDungeon14;
extern struct static_library lib_DrawEffect;
extern struct static_library lib_DrawField;
extern struct static_library lib_DrawGraph;
extern struct static_library lib_DrawMovie;
extern struct static_library lib_DrawMovie2;
extern struct static_library lib_DrawMovie3;
extern struct static_library lib_DrawNumeral;
extern struct static_library lib_DrawPluginManager;
extern struct static_library lib_DrawRain;
extern struct static_library lib_DrawRipple;
extern struct static_library lib_DrawSimpleText;
extern struct static_library lib_DrawSnow;
extern struct static_library lib_File;
extern struct static_library lib_File2;
extern struct static_library lib_FileOperation;
extern struct static_library lib_FillAngle;
extern struct static_library lib_GoatGUIEngine;
extern struct static_library lib_Gpx2Plus;
extern struct static_library lib_GUIEngine;
extern struct static_library lib_HTTPDownloader;
extern struct static_library lib_IbisInputEngine;
extern struct static_library lib_InputDevice;
extern struct static_library lib_InputString;
extern struct static_library lib_KiwiSoundEngine;
extern struct static_library lib_LoadCG;
extern struct static_library lib_MADLoader;
extern struct static_library lib_MainEXFile;
extern struct static_library lib_MainSurface;
extern struct static_library lib_MamanyoDemo;
extern struct static_library lib_MamanyoSDemo;
extern struct static_library lib_MarmotModelEngine;
extern struct static_library lib_Math;
extern struct static_library lib_MapLoader;
extern struct static_library lib_MenuMsg;
extern struct static_library lib_MonsterInfo;
extern struct static_library lib_MsgLogManager;
extern struct static_library lib_MsgLogViewer;
extern struct static_library lib_MsgSkip;
extern struct static_library lib_MusicSystem;
extern struct static_library lib_NewFont;
extern struct static_library lib_OutputLog;
extern struct static_library lib_P3MapSprite;
extern struct static_library lib_P3SquareSprite;
extern struct static_library lib_PassRegister;
extern struct static_library lib_PastelChime2;
extern struct static_library lib_PartsEngine;
extern struct static_library lib_PixelRestore;
extern struct static_library lib_PlayDemo;
extern struct static_library lib_PlayMovie;
extern struct static_library lib_ReignEngine;
extern struct static_library lib_SACT2;
extern struct static_library lib_SACTDX;
extern struct static_library lib_SealEngine;
extern struct static_library lib_SengokuRanceFont;
extern struct static_library lib_Sound2ex;
extern struct static_library lib_SoundFilePlayer;
extern struct static_library lib_StoatSpriteEngine;
extern struct static_library lib_StretchHelper;
extern struct static_library lib_SystemService;
extern struct static_library lib_SystemServiceEx;
extern struct static_library lib_TapirEngine;
extern struct static_library lib_Timer;
extern struct static_library lib_Toushin3Loader;
extern struct static_library lib_vmAnime;
extern struct static_library lib_vmArray;
extern struct static_library lib_vmCG;
extern struct static_library lib_vmChrLoader;
extern struct static_library lib_vmCursor;
extern struct static_library lib_vmDalkGaiden;
extern struct static_library lib_vmData;
extern struct static_library lib_vmDialog;
extern struct static_library lib_vmDrawGauge;
extern struct static_library lib_vmDrawMsg;
extern struct static_library lib_vmDrawNumber;
extern struct static_library lib_vmFile;
extern struct static_library lib_vmGraph;
extern struct static_library lib_vmGraphQuake;
extern struct static_library lib_vmKey;
extern struct static_library lib_vmMapLoader;
extern struct static_library lib_vmMsgLog;
extern struct static_library lib_vmMsgSkip;
extern struct static_library lib_vmMusic;
extern struct static_library lib_vmSound;
extern struct static_library lib_vmSprite;
extern struct static_library lib_vmString;
extern struct static_library lib_vmSurface;
extern struct static_library lib_vmSystem;
extern struct static_library lib_vmTimer;
extern struct static_library lib_ValueEncryption;
extern struct static_library lib_VSFile;

static bool library_alias(const char *ain_name, const char *lib_name)
{
	/* Rance X 及之后的游戏把 AnteaterADVEngine 改名为 AnteaterADVLogList */
	if (!strcmp(ain_name, "AnteaterADVLogList") && !strcmp(lib_name, "AnteaterADVEngine"))
		return true;
	return false;
}

static struct static_library *static_libraries[] = {
	&lib_ACXLoader,
	&lib_ACXLoaderP2,
	&lib_ADVSYS,
	&lib_AFAFactory,
	&lib_AliceLogo,
	&lib_AliceLogo2,
	&lib_AliceLogo3,
	&lib_AliceLogo4,
	&lib_AliceLogo5,
	&lib_AnteaterADVEngine,
	&lib_Clipboard,
	&lib_Delegate,
	&lib_system,
	&lib_EXWriter,
	&lib_ADVEngine,
	&lib_String,
	&lib_InstallInfo,
	&lib_Sys43VM,
	&lib_TextFile,
	&lib_TextSurfaceManager,
	&lib_Int,
	&lib_Float,
	&lib_FileDialog,
	&lib_PCInfo,
	&lib_Array,
	&lib_BanMisc,
	&lib_Bitarray,
	&lib_CalcTable,
	&lib_CGManager,
	&lib_ChipmunkSpriteEngine,
	&lib_ChrLoader,
	&lib_CommonSystemData,
	&lib_Confirm,
	&lib_Confirm2,
	&lib_Confirm3,
	&lib_CrayfishLogViewer,
	&lib_Cursor,
	&lib_DALKDemo,
	&lib_DALKEDemo,
	&lib_Data,
	&lib_DataFile,
	&lib_Discord,
	&lib_DrawDungeon,
	&lib_DrawDungeon2,
	&lib_DrawDungeon14,
	&lib_DrawEffect,
	&lib_DrawField,
	&lib_DrawGraph,
	&lib_DrawMovie,
	&lib_DrawMovie2,
	&lib_DrawMovie3,
	&lib_DrawNumeral,
	&lib_DrawPluginManager,
	&lib_DrawRain,
	&lib_DrawRipple,
	&lib_DrawSimpleText,
	&lib_DrawSnow,
	&lib_File,
	&lib_File2,
	&lib_FileOperation,
	&lib_FillAngle,
	&lib_GoatGUIEngine,
	&lib_Gpx2Plus,
	&lib_GUIEngine,
	&lib_HTTPDownloader,
	&lib_IbisInputEngine,
	&lib_InputDevice,
	&lib_InputString,
	&lib_KiwiSoundEngine,
	&lib_LoadCG,
	&lib_MADLoader,
	&lib_MainEXFile,
	&lib_MainSurface,
	&lib_MamanyoDemo,
	&lib_MamanyoSDemo,
	&lib_MarmotModelEngine,
	&lib_Math,
	&lib_MapLoader,
	&lib_MenuMsg,
	&lib_MonsterInfo,
	&lib_MsgLogManager,
	&lib_MsgLogViewer,
	&lib_MsgSkip,
	&lib_MusicSystem,
	&lib_NewFont,
	&lib_OutputLog,
	&lib_P3MapSprite,
	&lib_P3SquareSprite,
	&lib_PassRegister,
	&lib_PastelChime2,
	&lib_PartsEngine,
	&lib_PixelRestore,
	&lib_PlayDemo,
	&lib_PlayMovie,
	&lib_ReignEngine,
	&lib_SACT2,
	&lib_SACTDX,
	&lib_SealEngine,
	&lib_SengokuRanceFont,
	&lib_Sound2ex,
	&lib_SoundFilePlayer,
	&lib_StoatSpriteEngine,
	&lib_StretchHelper,
	&lib_SystemService,
	&lib_SystemServiceEx,
	&lib_TapirEngine,
	&lib_Timer,
	&lib_Toushin3Loader,
	&lib_vmAnime,
	&lib_vmArray,
	&lib_vmCG,
	&lib_vmChrLoader,
	&lib_vmCursor,
	&lib_vmDalkGaiden,
	&lib_vmData,
	&lib_vmDialog,
	&lib_vmDrawGauge,
	&lib_vmDrawMsg,
	&lib_vmDrawNumber,
	&lib_vmFile,
	&lib_vmGraph,
	&lib_vmGraphQuake,
	&lib_vmKey,
	&lib_vmMapLoader,
	&lib_vmMsgLog,
	&lib_vmMsgSkip,
	&lib_vmMusic,
	&lib_vmSound,
	&lib_vmSprite,
	&lib_vmString,
	&lib_vmSurface,
	&lib_vmSystem,
	&lib_vmTimer,
	&lib_ValueEncryption,
	&lib_VSFile,
	NULL
};

static ffi_type *ain_to_ffi_type(enum ain_data_type type)
{
	switch (type) {
	case AIN_VOID:
		return &ffi_type_void;
	case AIN_INT:
	case AIN_BOOL:
		return &ffi_type_sint32;
	case AIN_LONG_INT:
		return &ffi_type_sint64;
	case AIN_FLOAT:
		return &ffi_type_float;
	case AIN_HLL_PARAM:     // 74: 泛型参数，按 8 字节值传递（不是指针！）
		return &ffi_type_uint64;
	case AIN_STRING:
	case AIN_STRUCT:
	case AIN_FUNC_TYPE:
	case AIN_DELEGATE:
	case AIN_ARRAY_TYPE:
	case AIN_REF_TYPE:
	case AIN_REF_HLL_PARAM: // 75: ref generic param（返回引用，按指针）
	case AIN_HLL_FUNC:      // 95: predicate function type (needs pointer)
	case AIN_ARRAY:         // 79: generic array
	case AIN_IMAIN_SYSTEM: // ???
		return &ffi_type_pointer;
	default:
		ERROR("Unhandled type in HLL function: %s", ain_strtype(ain, type, -1));
	}
}

static void link_static_library_function(struct hll_function *dst, struct ain_hll_function *src, void *funcptr)
{
	dst->fun = funcptr;
	dst->nr_args = src->nr_arguments;
	dst->args = xcalloc(dst->nr_args, sizeof(ffi_type*));

	for (unsigned int i = 0; i < dst->nr_args; i++) {
		dst->args[i] = ain_to_ffi_type(src->arguments[i].type.data);
	}
	dst->return_type = ain_to_ffi_type(src->return_type.data);

	if (ffi_prep_cif(&dst->cif, FFI_DEFAULT_ABI, dst->nr_args, dst->return_type, dst->args) != FFI_OK)
		ERROR("Failed to link HLL function");

	/* [D110] 名字注册了但没有实现（HLL_TODO_EXPORT）：软桩模式下换成零值桩，
	 * 让流程继续往下走并在清单里留下证据；默认仍然留着 NULL 走 VM_ERROR。 */
	if (!dst->fun && soft_stub_enabled()) {
		dst->fun = soft_stub_for(dst->return_type);
		dst->is_stub = 1;
		stub_replaced++;
	}
}

/*
 * 把已经编译进 xsystem4 可执行文件的库「链接」起来。
 */
static struct hll_function *link_static_library(struct ain_library *ainlib, struct static_library *lib)
{
	struct hll_function *dst = xcalloc(ainlib->nr_functions, sizeof(struct hll_function));

	for (int i = 0; i < ainlib->nr_functions; i++) {
		bool matched = false;
		/* [D152] 同名重载按参数个数挑，不再"第一个同名就上"。
		 *
		 * 实锤：String.GetPart 在 AIN 里有 2 参（f21）和 3 参（f22）两个
		 * 重载，导出表只有一条 `GetPart -> String_GetPartLen`（3 参）。
		 * 于是 2 参调用把第三个参数读成寄存器垃圾，`GetPart("Tab1",3)`
		 * 时好时坏地返回 "1" 或 ""，调用方 `Int(...)-1` 算出 0 或 -1，
		 * `m_btn[-1]` 空引用 —— 这就是编成界面崩溃的间歇性来源。
		 *
		 * 评分：arity 相等 = 2，未标注(-1) = 1，明确不等 = 0；
		 * 取最高分，同分保留先出现的（旧行为不变）。 */
		struct static_hll_function *best = NULL;
		int best_score = -1;
		for (int j = 0; lib->functions[j].name; j++) {
			if (strcmp(ainlib->functions[i].name, lib->functions[j].name))
				continue;
			/* [D284] 评分维度加"返回类型"。
			 *
			 * `Math.Min`/`Max` 在 AIN 里各有 6 个重载：2/3/4 参 × int/float，
			 * 而 arity 相同 —— 只按 arity 打分时，float 版会被绑到
			 * `Math_Min(int,int)`，`rangeF(0,1,val)` 于是把 val 截断成 0
			 * （`Parts@SetAlpha` 收到 0.75 却算出 alpha=0）。
			 *
			 * 现在：arity 权重 4，返回类型权重 2（两者都命中 = 6）。
			 * 未标注的项拿基础分 1，保持"旧行为不变"的兜底。 */
			int score = 0;
			if (lib->functions[j].nr_args < 0)
				score += 1;
			else if (lib->functions[j].nr_args == ainlib->functions[i].nr_arguments)
				score += 4;
			if (lib->functions[j].ret_type >= 0) {
				int ain_ret = ainlib->functions[i].return_type.data;
				if (lib->functions[j].ret_type == ain_ret)
					score += 2;
				else
					score = 0;   /* 返回类型明确不符：直接排除 */
			}
			if (score > best_score) {
				best_score = score;
				best = &lib->functions[j];
			}
			if (score >= 6)
				break;
		}
		if (best) {
			link_static_library_function(&dst[i], &ainlib->functions[i], best->fun);
			matched = true;
		}
		/* [HACK][ZERO-STUB] 完全没有 C 导出的 AIN 函数，也仍然需要一个
		 * 准备好的 ffi cif，好让软桩路径能提供一个类型正确的零桩。
		 * 不设 XSYS4_SOFT_STUB 时 dst[i].fun 保持 NULL，
		 * 原来的 VM_ERROR 行为不变。已登记进技术债。 */
		if (!matched)
			link_static_library_function(&dst[i], &ainlib->functions[i], NULL);
		if (!dst[i].fun) {
			/* [D125] 这行告警以前被注释掉了，于是"某个库函数根本没实现"
			 * 在启动时完全无声——只有等它真被调用才炸（SOFT_STUB 打开时
			 * 甚至不炸，只返回零值）。ADV/剧情这类模块一旦命中零桩，
			 * 表现就是"剧情被跳过、状态机乱走"而日志里一片干净。
			 * 每个未实现导出只打一行，量很小，必须可见。 */
			WARNING("Unimplemented library function: %s.%s",
				ainlib->name, ainlib->functions[i].name);
		}
		else if (ainlib->functions[i].nr_arguments >= HLL_MAX_ARGS)
			ERROR("Too many arguments to library function: %s", ainlib->functions[i].name);
	}

	return dst;
}

static void library_run(struct static_library *lib, const char *name)
{
	for (int i = 0; lib->functions[i].name; i++) {
		if (!strcmp(lib->functions[i].name, name)) {
			((void(*)(void))lib->functions[i].fun)();
			break;
		}
	}
}

static void library_run_all(const char *name)
{
	for (int i = 0; i < ain->nr_libraries; i++) {
		for (int j = 0; static_libraries[j]; j++) {
			if (!strcmp(ain->libraries[i].name, static_libraries[j]->name) ||
			    library_alias(ain->libraries[i].name, static_libraries[j]->name)) {
				library_run(static_libraries[j], name);
				break;
			}
		}
	}
}

static void link_libraries(void)
{
	if (libraries)
		return;

	libraries = xcalloc(ain->nr_libraries, sizeof(struct hll_function*));

	for (int i = 0; i < ain->nr_libraries; i++) {
		for (int j = 0; static_libraries[j]; j++) {
			if (!strcmp(ain->libraries[i].name, static_libraries[j]->name) ||
			    library_alias(ain->libraries[i].name, static_libraries[j]->name)) {
				libraries[i] = link_static_library(&ain->libraries[i], static_libraries[j]);
				break;
			}
		}
		if (!libraries[i])
			WARNING("Unimplemented library: %s", ain->libraries[i].name);
	}
}

bool libraries_initialized = false;

void init_libraries(void)
{
	library_run_all("_PreLink");
	link_libraries();
	library_run_all("_ModuleInit");
	libraries_initialized = true;
}

void exit_libraries(void)
{
	if (libraries_initialized)
		library_run_all("_ModuleFini");
}

void static_library_replace(struct static_library *lib, const char *name, void *fun)
{
	for (int i = 0; lib->functions[i].name; i++) {
		if (!strcmp(lib->functions[i].name, name)) {
			lib->functions[i].fun = fun;
			return;
		}
	}
	ERROR("No library function '%s.%s'", lib->name, name);
}
