#!/usr/bin/env python3
# [D110e] 把 PACT 里命中的活动文件 dump 到 /tmp 供格式分析
import io, sys

PATH = "/home/alin/Rance10_Port/xsystem4/src/hll/PartsEngine.c"
src = io.open(PATH, encoding="utf-8").read()

if "D110e" in src:
    print("ALREADY_PATCHED")
    sys.exit(0)


def sub_once(text, old, new, tag):
    n = text.count(old)
    if n != 1:
        print("FAIL[%s]: matched %d" % (tag, n))
        sys.exit(2)
    return text.replace(old, new, 1)


src = sub_once(src, '#include "asset_manager.h"',
                    '#include "asset_manager.h"\n#include "system4/archive.h"', "inc")

old = '''\t\t\t\tNOTICE("[pe-hack]     asset %-5s exists=%d id=%d",
\t\t\t\t       asset_strtype((enum asset_type)t), ex, id);'''
new = '''\t\t\t\tNOTICE("[pe-hack]     asset %-5s exists=%d id=%d",
\t\t\t\t       asset_strtype((enum asset_type)t), ex, id);
\t\t\t\t/* [D110e] PACT 命中即落盘，供离线分析活动文件格式 */
\t\t\t\tif (ex && t == ASSET_PACT && getenv("XSYS4_PE_DUMP")) {
\t\t\t\t\tstruct archive_data *ad = asset_get((enum asset_type)t, id);
\t\t\t\t\tif (ad && ad->data) {
\t\t\t\t\t\tFILE *df = fopen("/tmp/act_dump.bin", "wb");
\t\t\t\t\t\tif (df) {
\t\t\t\t\t\t\tfwrite(ad->data, 1, ad->size, df);
\t\t\t\t\t\t\tfclose(df);
\t\t\t\t\t\t}
\t\t\t\t\t\tNOTICE("[pe-hack]     dumped '%s' size=%d -> /tmp/act_dump.bin",
\t\t\t\t\t\t       ad->name ? ad->name : "(?)", (int)ad->size);
\t\t\t\t\t}
\t\t\t\t}'''
src = sub_once(src, old, new, "dump")

io.open(PATH, "w", encoding="utf-8").write(src)
print("PATCH_OK")
