#!/usr/bin/env python3
# [D110d] 探针：ReadActivityFile 的参数 + 各归档类型里能否按名找到该活动
import io, sys

PATH = "/home/alin/Rance10_Port/xsystem4/src/hll/PartsEngine.c"
src = io.open(PATH, encoding="utf-8").read()

if "D110d" in src:
    print("ALREADY_PATCHED")
    sys.exit(0)


def sub_once(text, old, new, tag):
    n = text.count(old)
    if n != 1:
        print("FAIL[%s]: matched %d" % (tag, n))
        sys.exit(2)
    return text.replace(old, new, 1)


src = sub_once(src, '#include "parts.h"\n#include "hll.h"',
                    '#include "parts.h"\n#include "asset_manager.h"\n#include "hll.h"', "inc")

old = '''static int PE_Act_ReadActivityFile(struct string * a0, struct string * a1, int a2)
{
\tPE_HACK_HIT("PE_Act_ReadActivityFile");
\t(void)a0; (void)a1; (void)a2;
\treturn 0;
}'''
new = '''static int PE_Act_ReadActivityFile(struct string * a0, struct string * a1, int a2)
{
\tPE_HACK_HIT("PE_Act_ReadActivityFile");
\t/* [D110d] 一次性定位：活动文件叫什么、在哪个归档里。
\t * a0/a1 的真实语义由调用方决定，先把两个都打出来再判。 */
\tif (getenv("XSYS4_PE_DBG")) {
\t\tNOTICE("[pe-hack]   args a0=%s a1=%s a2=%d",
\t\t       a0 ? a0->text : "(null)", a1 ? a1->text : "(null)", a2);
\t\tif (a0) {
\t\t\tfor (int t = 0; t <= ASSET_FLASH; t++) {
\t\t\t\tint id = -1;
\t\t\t\tbool ex = asset_exists_by_name((enum asset_type)t, a0->text, &id);
\t\t\t\tNOTICE("[pe-hack]     asset %-5s exists=%d id=%d",
\t\t\t\t       asset_strtype((enum asset_type)t), ex, id);
\t\t\t}
\t\t}
\t}
\t(void)a0; (void)a1; (void)a2;
\treturn 0;
}'''
src = sub_once(src, old, new, "readfile")

io.open(PATH, "w", encoding="utf-8").write(src)
print("PATCH_OK")
