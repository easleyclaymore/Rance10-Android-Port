#!/usr/bin/env python3
"""读 `XSYS4_FN_COVER` 的输出，报告「这一轮执行了游戏的多少函数」。

为什么需要：问「语义摸到多少」时，最没有信息量的回答是指着文档说"我写了
N 节"。可验证的口径只有一个 —— **游戏自己执行了多少函数，其中多少我们
已经写进过语义说明**。

用法：
    # 1) 跑测时打开覆盖探针
    XSYS4_FN_COVER=/tmp/cover.tsv ... tools/run/run-headless.sh tag 200
    # 2) 出报告
    python3 tools/offline/cover_report.py /tmp/cover.tsv

可选：
    --guide docs/GUIDE/03-semantics.md   统计"文档里出现过的 fno"覆盖率
    --prefix SceneBattle@                只看某个前缀的分组
"""

import argparse
import collections
import os
import re
import sys


def load_cover(path):
    """fno<TAB>nr_args<TAB>name，名字可能为空。"""
    rows = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            q = line.rstrip('\n').split('\t')
            if len(q) >= 3 and q[0].isdigit():
                rows.append((int(q[0]), int(q[1]) if q[1].lstrip('-').isdigit() else -1, q[2]))
    return rows


def load_total(path):
    """`--dump-signatures` 的输出，用来当分母。"""
    if not os.path.exists(path):
        return None
    n = 0
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            if line[:1].isdigit():
                n += 1
    return n


def guide_fnos(path):
    """文档里显式写出的 fno：括号里的 3~5 位数、以及 fno=NNN。"""
    if not path or not os.path.exists(path):
        return set()
    s = open(path, encoding='utf-8').read()
    out = set()
    for m in re.finditer(r'\((\d{3,5})(?:[,)])', s):
        out.add(int(m.group(1)))
    for m in re.finditer(r'fno\s*=?\s*(\d{3,5})', s):
        out.add(int(m.group(1)))
    return out


def group_of(name):
    """按「命名空间」切组，用来回答"哪一块碰得多、哪一块没碰"。"""
    for sep in ('@', '::'):
        if sep in name:
            return name.split(sep)[0] + sep
    return '(全局)'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cover')
    ap.add_argument('--sig', default='/tmp/ainsig.tsv')
    ap.add_argument('--guide', default='docs/GUIDE/03-semantics.md')
    ap.add_argument('--prefix', default=None)
    ap.add_argument('--top', type=int, default=15)
    a = ap.parse_args()

    rows = load_cover(a.cover)
    total = load_total(a.sig)
    fnos = {r[0] for r in rows}
    doc = guide_fnos(a.guide)

    print('本轮执行到的函数：%d' % len(rows))
    if total:
        print('AIN 函数总数    ：%d' % total)
        print('覆盖率          ：%.1f%%' % (100.0 * len(rows) / total))
    if doc:
        hit = len(fnos & doc)
        print('其中文档写过语义：%d  (%.1f%% of 已执行)' %
              (hit, 100.0 * hit / max(1, len(rows))))

    groups = collections.Counter(group_of(r[2]) for r in rows)
    print('\n按命名空间（前 %d）：' % a.top)
    for g, n in groups.most_common(a.top):
        print('  %-28s %d' % (g, n))

    if a.prefix:
        sel = [r for r in rows if r[2].startswith(a.prefix)]
        print('\n%s 前缀：执行到 %d 个' % (a.prefix, len(sel)))
        for fno, nargs, name in sorted(sel, key=lambda x: x[2])[:60]:
            mark = '*' if fno in doc else ' '
            print('  %s %-6d %s' % (mark, fno, name))
        print('  （* = 文档里写过）')
    return 0


if __name__ == '__main__':
    sys.exit(main())
