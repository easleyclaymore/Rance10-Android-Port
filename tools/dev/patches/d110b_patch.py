#!/usr/bin/env python3
# [D110b] 软桩自检：确认导出真的被替换（0 触达时也能区分"没踩到"与"机制没生效"）
import io, sys

PATH = "/home/alin/Rance10_Port/xsystem4/src/ffi.c"
src = io.open(PATH, encoding="utf-8").read()

if "D110b" in src:
    print("ALREADY_PATCHED")
    sys.exit(0)


def sub_once(text, old, new, tag):
    n = text.count(old)
    if n != 1:
        print("FAIL[%s]: matched %d times" % (tag, n))
        sys.exit(2)
    return text.replace(old, new, 1)


src = sub_once(src, """static struct stub_hit stub_hits[STUB_MAX];
static int stub_nr = 0;
static int stub_nr_bt = 0;""",
"""static struct stub_hit stub_hits[STUB_MAX];
static int stub_nr = 0;
static int stub_nr_bt = 0;
static int stub_replaced = 0;   /* [D110b] 链接期被零值桩接管的导出数 */""", "A-counter")

src = sub_once(src, """\tif (!dst->fun && soft_stub_enabled()) {
\t\tdst->fun = soft_stub_for(dst->return_type);
\t\tdst->is_stub = 1;
\t}""",
"""\tif (!dst->fun && soft_stub_enabled()) {
\t\tdst->fun = soft_stub_for(dst->return_type);
\t\tdst->is_stub = 1;
\t\tstub_replaced++;
\t}""", "B-count")

src = sub_once(src, """void hll_call(int libno, int fno, int result_tag)
{
\thll_call_fno = fno;""",
"""void hll_call(int libno, int fno, int result_tag)
{
\t/* [D110b] 首次 HLL 调用时落一次自检：清单为空时也能区分
\t * "一路没踩到空桩" 和 "软桩机制根本没生效"。 */
\tif (soft_stub_enabled()) {
\t\tstatic int self_checked = 0;
\t\tif (!self_checked) {
\t\t\tself_checked = 1;
\t\t\tFILE *sfp = stub_log_file();
\t\t\tif (sfp) {
\t\t\t\tfprintf(sfp, "# link-time: %d unimplemented exports replaced by zero-stubs\\n", stub_replaced);
\t\t\t\tfflush(sfp);
\t\t\t}
\t\t\tNOTICE("[stub] soft-stub active: %d unimplemented exports replaced", stub_replaced);
\t\t}
\t}
\thll_call_fno = fno;""", "C-selfcheck")

io.open(PATH, "w", encoding="utf-8").write(src)
print("PATCH_OK")
