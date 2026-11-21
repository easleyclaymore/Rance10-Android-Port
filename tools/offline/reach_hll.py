#!/usr/bin/env python3
"""从给定入口做静态可达性分析，算出「会撞上哪些未实现的 HLL」。

为什么需要：`--dump-hll` 只能告我「哪些没实现」（339 条），
`XSYS4_SOFT_STUB` 只能告我「这轮跑到了哪些」（0 条）。两者之间有个空档：
**没实现、而且从主流程可达，只是这轮还没走到** —— 那批就是接下来一定会
撞上的。这个脚本补的就是这个空档。

做法：把反汇编里所有 `CALLMETHOD n -> [fno]` / `CALLFUNC fno` 读成有向边，
从入口集合 BFS；沿途遇到的 `CALLHLL <名字>` 若是未实现集合里的，就记下
（带最短深度，深度小 = 近）。

用法：
    python3 tools/offline/reach_hll.py /tmp/ain_full4.txt \
        --roots 22579,24882,23586,26388 \
        --hll /tmp/ainhll.tsv --src src/hll

局限：静态分析不区分运行时分支，所以结果是**上界**（可能到不了），
但不会是漏报 —— 「这里没列出来」比「这里列出来了」更强。
"""

import argparse
import collections
import glob
import os
import re
import sys

CALL_RE = re.compile(r'CALLMETHOD\s+arg=\d+\s+\(0x[0-9a-f]+\)\s+nargs=\d+\s+->\s+\[(\d+)\]')
FUNC_CALL_RE = re.compile(r'CALLFUNC\s+arg=(\d+)')
HLL_RE = re.compile(r'CALLHLL\s+arg=\d+\s+\(0x[0-9a-f]+\)\s*([A-Za-z0-9_.]+)')
PUSH_RE = re.compile(r'^0x[0-9a-f]+: PUSH\s+arg=(\d+)')
FUNC_RE = re.compile(r'^0x[0-9a-f]+: FUNC\s+arg=(\d+)')


def load_unimplemented(hll_tsv, src_dir):
    """未实现 = AIN 有声明，但对应库里没有同名的真实导出。"""
    decl = []
    with open(hll_tsv, encoding='utf-8', errors='replace') as f:
        for line in f:
            q = line.rstrip('\n').split('\t')
            if len(q) >= 5:
                decl.append((q[0], q[3]))
    lib2files = collections.defaultdict(set)
    real = collections.defaultdict(set)
    for p in glob.glob(os.path.join(src_dir, '*.c')):
        s = open(p, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'HLL_LIBRARY\((\w+)', s):
            lib2files[m.group(1)].add(p)
        for m in re.finditer(r'HLL_EXPORT\w*\(\s*(\w+)\s*,', s):
            real[p].add(m.group(1))
    missing = set()
    for lib, name in decl:
        files = lib2files.get(lib)
        if not files:
            continue
        if not any(name in real[f] for f in files):
            missing.add('%s.%s' % (lib, name))   # 必须带库名，否则会跟别的库同名函数混起来
    return missing


def build_graph(dump):
    """返回 (call_edges, hll_edges)：fno -> set(fno) / fno -> set(hll 名)。"""
    edges = collections.defaultdict(set)
    hlls = collections.defaultdict(set)
    cur = None
    fnos = set()                 # 全部合法函数号：DG_NEW 边的合法性过滤用
    push_window = []             # 最近若干条 PUSH 的立即数（DG_NEW 的 fun 在其中）
    with open(dump, 'rb') as f:
        for raw in f:
            line = raw.decode('utf-8', 'replace')
            m = FUNC_RE.match(line)
            if m:
                cur = int(m.group(1))
                fnos.add(cur)
                push_window.clear()   # 窗口不跨函数
                continue
            if cur is None:
                continue
            m = CALL_RE.search(line)
            if m:
                edges[cur].add(int(m.group(1)))
                continue
            m = FUNC_CALL_RE.search(line)
            if m:
                edges[cur].add(int(m.group(1)))
                continue
            # [D497] 委托边：DG_NEW 从栈上弹 [obj, fun] 对，fun 是先前
            # PUSH 的立即数。只跟 CALLMETHOD/CALLFUNC 会漏掉"仅经委托
            # 调用"的函数（GUIDE 3.50 的 lambda 36970 就是这条形态）。
            # 口径：DG_NEW 往前看 12 条指令内的 PUSH 立即数，凡合法
            # 函数号都记一条边 —— 上界口径，宁可多连不可漏报
            # （PUSH 一个恰好等于函数号的普通整数会产生假边，但那只
            # 影响上界大小，不影响"列出来的就是可达"这个保证）。
            m = re.search(r'\bDG_NEW\b', line)
            if m:
                for v in push_window[-12:]:
                    if v in fnos:
                        edges[cur].add(v)
                push_window.clear()
                continue
            m = PUSH_RE.search(line)
            if m:
                push_window.append(int(m.group(1)))
                if len(push_window) > 40:
                    push_window.pop(0)
                continue
            m = HLL_RE.search(line)
            if m:
                hlls[cur].add(m.group(1))   # 保留 lib.name：同名函数在多个库里是完全不同的实现
    return edges, hlls


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump')
    ap.add_argument('--roots', required=True, help='逗号分隔的 fno')
    ap.add_argument('--hll', default='/tmp/ainhll.tsv')
    ap.add_argument('--src', default='src/hll')
    ap.add_argument('--sig', default='/tmp/ainsig.tsv')
    ap.add_argument('--max-depth', type=int, default=12)
    a = ap.parse_args()

    roots = [int(x) for x in a.roots.split(',') if x.strip().isdigit()]
    missing = load_unimplemented(a.hll, a.src)
    edges, hlls = build_graph(a.dump)

    name = {}
    if os.path.exists(a.sig):
        for line in open(a.sig, encoding='utf-8', errors='replace'):
            q = line.rstrip('\n').split('\t')
            if len(q) >= 4 and q[0].isdigit():
                name[int(q[0])] = q[3]

    # BFS
    depth = {r: 0 for r in roots}
    q = collections.deque(roots)
    hits = {}   # hll 名 -> (深度, 首次出现于哪个 fno)
    while q:
        f = q.popleft()
        d = depth[f]
        for h in hlls.get(f, ()):
            if h in missing and h not in hits:
                hits[h] = (d, f)
        if d >= a.max_depth:
            continue
        for g in edges.get(f, ()):
            if g not in depth:
                depth[g] = d + 1
                q.append(g)

    print('入口 %d 个，可达函数 %d 个（深度上限 %d）' % (len(roots), len(depth), a.max_depth))
    print('可达且未实现的 HLL：%d 条\n' % len(hits))
    for h, (d, via) in sorted(hits.items(), key=lambda kv: (kv[1][0], kv[0])):
        print('  d=%-2d %-44s 经 %s(%d)' % (d, h, name.get(via, '?')[:40], via))
    return 0


if __name__ == '__main__':
    sys.exit(main())
