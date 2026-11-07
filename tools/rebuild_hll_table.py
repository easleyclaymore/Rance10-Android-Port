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
  python3 tools/rebuild_hll_table.py <libname> <src-file> <ain-file> [--apply]
"""
import re, subprocess, sys, os

def main():
    if len(sys.argv) < 4:
        print(__doc__); return 1
    lib, src, ain = sys.argv[1], sys.argv[2], sys.argv[3]
    apply = '--apply' in sys.argv
    sig = subprocess.check_output(['/tmp/hllsig', ain, lib], text=True)
    entries = [(int(m.group(1)), m.group(2)) for m in
               (re.match(r'f\[(\d+)\]\s+([A-Za-z0-9_]+)\(', l) for l in sig.splitlines()) if m]
    s = open(src, encoding='utf-8').read()
    start = s.index('HLL_LIBRARY(%s,' % lib)
    end_m = re.search(r'^\s*\);\s*$', s[start:], re.M)
    if not end_m: raise SystemExit('table end not found')
    end = start + end_m.end()
    old = s[start:end]
    impl = {name: cfun for macro, name, cfun in re.findall(
        r'(HLL_EXPORT|HLL_TODO_EXPORT)\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)', old)
        if macro == 'HLL_EXPORT'}
    lines = ['HLL_LIBRARY(%s,' % lib]
    reused = 0
    for fno, name in entries:
        if name in impl:
            lines.append('\t    HLL_EXPORT(%s, %s),' % (name, impl[name])); reused += 1
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
