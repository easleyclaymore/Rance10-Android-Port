#!/usr/bin/env python3
# [D110] 给 ffi.c 打软桩补丁。幂等：已打过则直接退出。
import io, sys

PATH = "/home/alin/Rance10_Port/xsystem4/src/ffi.c"

src = io.open(PATH, encoding="utf-8").read()

if "D110" in src:
    print("ALREADY_PATCHED")
    sys.exit(0)


def sub_once(text, old, new, tag):
    n = text.count(old)
    if n != 1:
        print("FAIL[%s]: matched %d times" % (tag, n))
        sys.exit(2)
    return text.replace(old, new, 1)


# ---- A) struct hll_function 增加 is_stub ----
old_a = """struct hll_function {
\tvoid *fun;
\tffi_cif cif;
\tunsigned int nr_args;
\tffi_type **args;
\tffi_type *return_type;
};"""
new_a = """struct hll_function {
\tvoid *fun;
\tffi_cif cif;
\tunsigned int nr_args;
\tffi_type **args;
\tffi_type *return_type;
\tint is_stub;            /* [D110] HLL_TODO_EXPORT 且已被零值桩接管 */
};"""
src = sub_once(src, old_a, new_a, "A-struct")

# ---- B) 软桩基础设施 ----
anchor_b = "static struct hll_function **libraries = NULL;"
infra = anchor_b + r"""

/* ---------------- [D110] 未实现 HLL 的软桩与触达清单 ----------------
 * HLL_TODO_EXPORT 展开为 { .name=..., .fun=NULL }，hll_call() 一旦执行到
 * 就 VM_ERROR -> sys_exit(1)，进程当场死亡：只能看到"最深的那一次"，
 * 后面还有什么没实现永远不知道。
 *
 * XSYS4_SOFT_STUB=1 时把这些导出换成"按返回类型给零值"的桩：
 *   - 参数仍走原有 ffi 编组路径，栈平衡/引用计数逻辑一字未改；
 *   - 每个桩首次触达在清单里记一行，之后只累加计数；
 *   - XSYS4_STUB_BT=1 时给前 32 个桩打印 VM 调用栈；
 *   - 清单默认 /tmp/xsys4_stubs.txt，可用 XSYS4_STUB_LOG 指定。
 * 不设 XSYS4_SOFT_STUB 时行为与以前完全一致（仍然 VM_ERROR 退出）。
 */
static int soft_stub_enabled(void)
{
\tstatic int mode = -1;
\tif (mode < 0) {
\t\tconst char *e = getenv("XSYS4_SOFT_STUB");
\t\tmode = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
\t}
\treturn mode;
}

static void stub_ret_void(void) { }
static int32_t stub_ret_i32(void) { return 0; }
static int64_t stub_ret_i64(void) { return 0; }
static float stub_ret_f32(void) { return 0.0f; }
static double stub_ret_f64(void) { return 0.0; }
static void *stub_ret_ptr(void) { return NULL; }

static void *soft_stub_for(ffi_type *ret)
{
\tif (!ret || ret == &ffi_type_void)
\t\treturn (void *)stub_ret_void;
\tswitch (ret->type) {
\tcase FFI_TYPE_POINTER: return (void *)stub_ret_ptr;
\tcase FFI_TYPE_FLOAT:   return (void *)stub_ret_f32;
\tcase FFI_TYPE_DOUBLE:  return (void *)stub_ret_f64;
\tcase FFI_TYPE_SINT64:
\tcase FFI_TYPE_UINT64:  return (void *)stub_ret_i64;
\tdefault:               return (void *)stub_ret_i32;
\t}
}

#define STUB_MAX 512
struct stub_hit {
\tint libno;
\tint fno;
\tunsigned long count;
};
static struct stub_hit stub_hits[STUB_MAX];
static int stub_nr = 0;
static int stub_nr_bt = 0;

static void stub_summary(void);

static FILE *stub_log_file(void)
{
\tstatic FILE *fp = NULL;
\tstatic int inited = 0;
\tif (!inited) {
\t\tinited = 1;
\t\tconst char *p = getenv("XSYS4_STUB_LOG");
\t\tif (!p || !*p)
\t\t\tp = "/tmp/xsys4_stubs.txt";
\t\tfp = fopen(p, "w");
\t\tif (fp) {
\t\t\tfprintf(fp, "# xsystem4 soft-stub hit list (XSYS4_SOFT_STUB=1)\n# lib.func  args  ret\n");
\t\t\tfflush(fp);
\t\t\tatexit(stub_summary);
\t\t}
\t}
\treturn fp;
}

static void stub_summary(void)
{
\tFILE *fp = stub_log_file();
\tif (!fp)
\t\treturn;
\tfprintf(fp, "\n# ===== summary: %d distinct unimplemented HLL functions hit =====\n", stub_nr);
\tfor (int i = 0; i < stub_nr; i++) {
\t\tstruct ain_library *lib = &ain->libraries[stub_hits[i].libno];
\t\tstruct ain_hll_function *f = &lib->functions[stub_hits[i].fno];
\t\tfprintf(fp, "%8lu  %s.%s\n", stub_hits[i].count, lib->name, f->name);
\t}
\tfflush(fp);
}

static void stub_note(int libno, int fno)
{
\tFILE *fp = stub_log_file();
\tif (!fp)
\t\treturn;
\tfor (int i = 0; i < stub_nr; i++) {
\t\tif (stub_hits[i].libno == libno && stub_hits[i].fno == fno) {
\t\t\tstub_hits[i].count++;
\t\t\treturn;
\t\t}
\t}
\tif (stub_nr >= STUB_MAX)
\t\treturn;
\tstruct ain_library *lib = &ain->libraries[libno];
\tstruct ain_hll_function *f = &lib->functions[fno];
\tstub_hits[stub_nr].libno = libno;
\tstub_hits[stub_nr].fno = fno;
\tstub_hits[stub_nr].count = 1;
\tstub_nr++;
\tfprintf(fp, "[stub %03d] %s.%s  args=%d ret=%s\n", stub_nr, lib->name, f->name,
\t        (int)f->nr_arguments, ain_strtype(ain, f->return_type.data, -1));
\tfflush(fp);
\tstatic int bt = -1;
\tif (bt < 0) {
\t\tconst char *e = getenv("XSYS4_STUB_BT");
\t\tbt = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
\t}
\tif (bt && stub_nr_bt < 32) {
\t\tstub_nr_bt++;
\t\tNOTICE("[stub] backtrace for %s.%s:", lib->name, f->name);
\t\tvm_stack_trace();
\t}
}
"""
infra = infra.replace("\\t", "\t")  # raw string 里的缩进转义还原成真 tab
src = sub_once(src, anchor_b, infra, "B-infra")

# ---- C) link 完成后替换 ----
old_c = """\tif (ffi_prep_cif(&dst->cif, FFI_DEFAULT_ABI, dst->nr_args, dst->return_type, dst->args) != FFI_OK)
\t\tERROR("Failed to link HLL function");
}"""
new_c = """\tif (ffi_prep_cif(&dst->cif, FFI_DEFAULT_ABI, dst->nr_args, dst->return_type, dst->args) != FFI_OK)
\t\tERROR("Failed to link HLL function");

\t/* [D110] 名字注册了但没有实现（HLL_TODO_EXPORT）：软桩模式下换成零值桩，
\t * 让流程继续往下走并在清单里留下证据；默认仍然留着 NULL 走 VM_ERROR。 */
\tif (!dst->fun && soft_stub_enabled()) {
\t\tdst->fun = soft_stub_for(dst->return_type);
\t\tdst->is_stub = 1;
\t}
}"""
src = sub_once(src, old_c, new_c, "C-link")

# ---- D) hll_call 触达记录 ----
old_d = """\tstruct hll_function *fun = &libraries[libno][fno];
\tif (!fun->fun)
\t\tVM_ERROR("Unimplemented HLL function: %s.%s", ain->libraries[libno].name, f->name);"""
new_d = """\tstruct hll_function *fun = &libraries[libno][fno];
\tif (!fun->fun)
\t\tVM_ERROR("Unimplemented HLL function: %s.%s", ain->libraries[libno].name, f->name);
\tif (fun->is_stub)
\t\tstub_note(libno, fno);"""
src = sub_once(src, old_d, new_d, "D-call")

# ---- E) audit 视图不要把软桩当成已实现 ----
old_e = """bool library_function_exists(int libno, int fno)
{
\treturn libraries[libno] && libraries[libno][fno].fun;
}"""
new_e = """bool library_function_exists(int libno, int fno)
{
\t/* [D110] 软桩只是运行期兜底，审计时仍算"未实现"。 */
\treturn libraries[libno] && libraries[libno][fno].fun && !libraries[libno][fno].is_stub;
}"""
src = sub_once(src, old_e, new_e, "E-exists")

io.open(PATH, "w", encoding="utf-8").write(src)
print("PATCH_OK")
