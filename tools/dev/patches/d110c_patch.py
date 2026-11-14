#!/usr/bin/env python3
# [D110c] 给 D89 手写 Activity 桩加触达探针（fun 非 NULL，软桩清单看不到它们）
import io, re, sys

PATH = "/home/alin/Rance10_Port/xsystem4/src/hll/PartsEngine.c"
src = io.open(PATH, encoding="utf-8").read()

if "PE_HACK_HIT" in src:
    print("ALREADY_PATCHED")
    sys.exit(0)

old_inc = '#include <assert.h>\n\n#include "system4/ain.h"'
new_inc = '#include <assert.h>\n#include <stdlib.h>\n\n#include "system4/ain.h"'
assert src.count(old_inc) == 1, "inc"
src = src.replace(old_inc, new_inc, 1)

old_h = '#include "hll.h"\n'
new_h = '''#include "hll.h"

/* [D110c] D89 手写桩的触达探针。
 * 这类桩 .fun 非 NULL，走的是正常 HLL_EXPORT，所以既不会触发 VM_ERROR，
 * 也不会出现在 D110 的软桩清单里 —— 它们静默返回 0，最容易查漏。
 * XSYS4_PE_DBG=1 时每次触达打一行。 */
#define PE_HACK_HIT(fname) do { \\
	if (getenv("XSYS4_PE_DBG")) \\
		NOTICE("[pe-hack] %s", fname); \\
} while (0)
'''
assert src.count(old_h) == 1, "hll.h"
src = src.replace(old_h, new_h, 1)

pat = re.compile(r'(static (?:int|void) ((?:PE_Act_\w+)|PartsEngine_IsExistActivity)\([^)\n]*\)\n\{\n)')
src, n = pat.subn(lambda m: m.group(1) + '\tPE_HACK_HIT("' + m.group(2) + '");\n', src)
print("instrumented=%d" % n)
if n < 20:
    print("FAIL: too few functions instrumented")
    sys.exit(2)

# IsExistActivity 额外打印参数
old_i = '''\tPE_HACK_HIT("PartsEngine_IsExistActivity");
\t(void)name;'''
new_i = '''\tPE_HACK_HIT("PartsEngine_IsExistActivity");
\tif (getenv("XSYS4_PE_DBG"))
\t\tNOTICE("[pe-hack]   name=%s", name ? name->text : "(null)");
\t(void)name;'''
assert src.count(old_i) == 1, "isexist"
src = src.replace(old_i, new_i, 1)

io.open(PATH, "w", encoding="utf-8").write(src)
print("PATCH_OK")
