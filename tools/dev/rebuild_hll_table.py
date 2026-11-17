#!/usr/bin/env python3
"""Report / rebuild a position-indexed HLL_LIBRARY table from AIN fno order.

DRY RUN BY DEFAULT. Pass --apply to actually rewrite the source file.

Background: HLL_LIBRARY tables are indexed by fno, not by name. The
PartsEngine table in this fork has 515 entries while Rance10.ain declares
804 functions, and the order is completely different. Blind name-based
reuse is NOT sufficient: C signatures and module-init entry names differ
between AIN versions (a 2026-11-07 attempt made PE_Init unreachable and
crashed with ht_get_int(NULL)). Verify every entry before applying.

Usage:
  python3 tools/dev/rebuild_hll_table.py <libname> <src-file> <ain-file> [--apply]

hllsig 必须先构建（tools/build.sh 默认就会编它），产物在 tools/bin/。
以前这里写死 /tmp/hllsig，换台机器或清了 /tmp 就只能报“文件不存在”。
"""
import re, subprocess, sys, os

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HLLSIG = os.environ.get('OFFLINE_BIN', os.path.join(REPO, 'tools', 'bin'))
HLLSIG = os.path.join(HLLSIG, 'hllsig')


def main():
    if len(sys.argv) < 4:
        print(__doc__); return 1
    lib, src, ain = sys.argv[1], sys.argv[2], sys.argv[3]
    apply = '--apply' in sys.argv
    if not os.path.exists(HLLSIG):
        raise SystemExit('缺少 %s，先跑 tools/build.sh（或 xsys4 build tools）' % HLLSIG)
    sig = subprocess.check_output([HLLSIG, ain, lib], text=True)
    entries = [(int(m.group(1)), m.group(2)) for m in
               (re.match(r'f\[(\d+)\]\s+([A-Za-z0-9_]+)\(', l) for l in sig.splitlines()) if m]
    s = open(src, encoding='utf-8').read()
    start = s.index('HLL_LIBRARY(%s,' % lib)
    # 表的结束不能靠 `^\s*\);\s*$` 找：最后一个条目可以把 `));` 缩在同一行
    # （Math 表就是这样，D284 之后最后一行是 `HLL_EXPORT_NRT(Clamp, ..., AIN_FLOAT));`），
    # 那样正规匹配会直接报 `table end not found`。改用括号深度归零定位。
    depth = 0
    end = -1
    for i in range(start, len(s)):
        if s[i] == '(':
            depth += 1
        elif s[i] == ')':
            depth -= 1
            if depth == 0:
                end = i + 1
                while end < len(s) and s[end] in ' \t':
                    end += 1
                if end < len(s) and s[end] == ';':
                    end += 1
                break
    if end < 0:
        raise SystemExit('table end not found')
    old = s[start:end]
    # 导出宏共 4 个（D284 新增 RT/NRT 后）。只认前两个会把带返回类型标注的
    # int/float 重载当成未实现，重写表时给它们塞 HLL_TODO_EXPORT。
    # 按宏名存下**整段参数**，重写时原样喷出——否则 Math 的 6 组
    # (arity, ret_type) 标注会在一次 --apply 后静默丢掉，又变成 D284 那个 bug。
    impl = {}
    for macro, name, rest in re.findall(
            r'(HLL_EXPORT|HLL_EXPORT_N|HLL_EXPORT_RT|HLL_EXPORT_NRT|HLL_TODO_EXPORT)'
            r'\(\s*([A-Za-z0-9_]+)\s*,\s*([^)]*)', old):
        if macro != 'HLL_TODO_EXPORT':
            impl.setdefault(name, []).append((macro, rest.rstrip().rstrip(',')))
    lines = ['HLL_LIBRARY(%s,' % lib]
    reused = 0
    used = {}   # name -> 已用掉几个重载
    for fno, name in entries:
        # 表是**按 fno 位置**索引的：`Math.Min` 在 AIN 里有 6 个 fno，
        # 就要在 6 个位置各占一行。同名重载按源码顺序依次分配，
        # 用完后重复最后一个（避免最后一个重载变成空槽）。
        variants = impl.get(name)
        if variants:
            i = min(used.get(name, 0), len(variants) - 1)
            used[name] = used.get(name, 0) + 1
            macro, rest = variants[i]
            lines.append('\t    %s(%s, %s),' % (macro, name, rest)); reused += 1
        else:
            lines.append('\t    HLL_TODO_EXPORT(%s, NULL),' % name)
    lines[-1] = lines[-1].rstrip(',')
    lines.append(');')
    print('AIN functions      :', len(entries))
    print('existing impls     :', len(impl))
    print('reused by name     :', reused)
    print('TODO placeholders  :', len(entries) - reused)
    print('first 8 fno        :', [n for _, n in entries[:8]])
    if not apply:
        print('\nDRY RUN - nothing written. Review every reused C signature')
        print('and the module-init entry points before using --apply.')
        return 0
    open(src, 'w', encoding='utf-8').write(s[:start] + '\n'.join(lines) + s[end:])
    print('\nAPPLIED. Rebuild and run the game immediately; revert if it regresses.')
    return 0

if __name__ == '__main__':
    sys.exit(main())
