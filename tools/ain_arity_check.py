"""Audit HLL argument arity: AIN declaration vs C implementation.

Usage: python3 tools/ain_arity_check.py <repo-root> <ain_hllarity.tsv>
A C prototype with MORE parameters than the AIN declaration is safe when the
function dispatches on hll_current_fno(); with FEWER parameters it is a real
arity mismatch and may shift every argument position.
"""
import re, glob, io, os, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else '.'
TSV  = sys.argv[2] if len(sys.argv) > 2 else '../logs/ain_hllarity.tsv'

ain = {}
for line in io.open(TSV, encoding='utf-8'):
    ln, lib, fno, fn, n = line.rstrip('\n').split('\t')
    ain.setdefault(lib, {}).setdefault(fn, []).append((int(fno), int(n)))

def count_params(sig):
    sig = sig.strip()
    if not sig or sig == 'void': return 0
    depth, n = 0, 1
    for ch in sig:
        if ch in '([{': depth += 1
        elif ch in ')]}': depth -= 1
        elif ch == ',' and depth == 0: n += 1
    return n

defs = {}
for path in glob.glob(ROOT + '/src/**/*.c', recursive=True):
    text = io.open(path, encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'(?:^|\n)\s*static\s+[A-Za-z_][^;{]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(([^()]*)\)\s*\{', text):
        defs.setdefault(m.group(1), (count_params(m.group(2)), os.path.basename(path)))

bindings = []
for path in glob.glob(ROOT + '/src/hll/*.c'):
    text = io.open(path, encoding='utf-8', errors='replace').read()
    for lm in re.finditer(r'HLL_LIBRARY\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,(.*?)\n\s*\);', text, re.S):
        lib, body = lm.group(1), lm.group(2)
        for em in re.finditer(r'HLL_(?:TODO_)?EXPORT\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)', body):
            bindings.append((lib, em.group(1), em.group(2)))

dangerous, superset, checked = [], [], 0
for lib, fn, cfn in bindings:
    if lib not in ain or fn not in ain[lib] or cfn not in defs: continue
    cn = defs[cfn][0]
    for fno, an in ain[lib][fn]:
        checked += 1
        if an > cn: dangerous.append((lib, fn, fno, an, cfn, cn))
        elif an < cn: superset.append((lib, fn, fno, an, cfn, cn))

print('bindings=%d arity_checked=%d dangerous(AIN>C)=%d superset(C>AIN)=%d' %
      (len(bindings), checked, len(dangerous), len(superset)))
for d in dangerous:
    print('  DANGEROUS %-20s %-26s f[%d] AIN=%d C=%d -> %s' % d[:6])
print('superset entries (safe, fno-dispatched):')
for d in superset[:20]:
    print('  ok        %-20s %-26s f[%d] AIN=%d C=%d -> %s' % (d[0], d[1], d[2], d[3], d[5], d[4]))
