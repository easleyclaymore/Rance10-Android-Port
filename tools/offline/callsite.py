#!/usr/bin/env python3
"""把 HLL 函数的调用点连同**上下文**一起打出来 —— B1..B8 每批的第一步。

## 为什么需要它

L2 判据的核心是「这个返回值在调用点被**怎么用**」：
当 `IFZ` 条件是布尔、进 `PUSH`+算术是数值、传给下一个 `CALL*` 是句柄／下标。
只看实现永远推不出来，必须读调用点。

逐个函数反汇编要几十秒，而**全量反汇编只要 0.8 秒**：

    cd game && ../xsystem4/build/src/xsystem4 --nodebug --encoding gbk \\
        --disasm 0:0x700000 . > /tmp/ain_full.txt

（语法是 `起始:长度`，不是 `起始,结束` —— 写错会静默退化成 0x400 字节。）

调用点属于哪个 AIN 函数，靠 `XSYS4_FN_LIST=*` 导出的 fno↔地址↔名字表：

    XSYS4_FN_LIST='*' RANCE10_LIVE_SEQ="wait:16000" tools/run/run-headless.sh fn 20
    grep -a '\\[fnlist\\]' logs/live_fn.log | sed -E 's/^.*\\[fnlist\\] //' > /tmp/fnmap.txt

## 用法

    python3 tools/offline/callsite.py PartsEngine.SetActiveController
    python3 tools/offline/callsite.py PartsEngine.GetActiveController -A 12
    python3 tools/offline/callsite.py --list PartsEngine   # 只列函数名与调用点数
"""

import argparse
import collections
import os
import re
import sys

CALL_RE = re.compile(r"^(0x[0-9a-f]+): CALLHLL\s+arg=\d+\s+\(0x[0-9a-f]+\)(\S+?)\s*$")
FUNC_RE = re.compile(r"^(0x[0-9a-f]+): FUNC\s+arg=(\d+)")
ADDR_RE = re.compile(r"^(0x[0-9a-f]+):")


def load_fnmap(path):
    """fno -> (addr, name)"""
    m = {}
    if not os.path.exists(path):
        return m
    for ln in open(path, encoding="utf-8", errors="replace"):
        parts = ln.split(None, 2)
        if len(parts) == 3 and parts[0].isdigit():
            m[int(parts[0])] = parts[2].strip()
    return m


def scan(disasm, want=None, before=0):
    """返回 [(addr, callee, caller_fno, body_lines)]，按出现顺序。

    `body_lines` 是调用点之后、下一个 `FUNC` 之前的指令 —— 返回值的用法就在这几十行里。
    """
    out = []
    cur_fno = -1
    active = None
    prev = collections.deque(maxlen=max(before, 0) + 1)
    with open(disasm, encoding="utf-8", errors="replace") as f:
        for ln in f:
            m = FUNC_RE.match(ln)
            if m:
                cur_fno = int(m.group(2))
                active = None          # 新函数开始，上一处上下文到此为止
                prev.clear()
                continue
            m = CALL_RE.match(ln)
            if m:
                if want is None or m.group(2) == want:
                    active = [m.group(1), m.group(2), cur_fno,
                              list(prev)[-before:] if before else [], []]
                    out.append(active)
                else:
                    active = None
                continue
            if ADDR_RE.match(ln):
                prev.append(ln.rstrip("\n"))
                if active is not None:
                    active[4].append(ln.rstrip("\n"))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("func", nargs="?", help="Lib.Func，例如 PartsEngine.SetActiveController")
    ap.add_argument("--disasm", default="/tmp/ain_full.txt")
    ap.add_argument("--fnmap", default="/tmp/fnmap.txt")
    ap.add_argument("-A", "--after", type=int, default=8, help="调用点之后打印几行")
    ap.add_argument("-B", "--before", type=int, default=0, help="调用点之前打印几行（看实参怎么来的）")
    ap.add_argument("--list", metavar="LIB", help="只列该库所有被调用的 HLL 函数及调用点数")
    args = ap.parse_args()

    if not os.path.exists(args.disasm):
        print(f"缺反汇编缓存：{args.disasm}\n见本文件头部的生成命令", file=sys.stderr)
        return 2

    if args.list:
        import collections
        c = collections.Counter()
        for _, callee, _, _ in scan(args.disasm):
            if callee.startswith(args.list + "."):
                c[callee] += 1
        for k, v in sorted(c.items(), key=lambda kv: -kv[1]):
            print(f"{v:5d}  {k}")
        return 0

    if not args.func:
        print(__doc__)
        return 2

    fnmap = load_fnmap(args.fnmap)
    hits = scan(args.disasm, args.func, args.before)
    if not hits:
        print(f"{args.func} 没有 CALLHLL 调用点（可能不在本章路径上）", file=sys.stderr)
        return 1

    print(f"{args.func}: {len(hits)} 个调用点\n")
    for addr, _, fno, pre, post in hits:
        caller = fnmap.get(fno, f"<fno {fno}>")
        print(f"--- @{addr}  在 {caller} (fno {fno})")
        for ln in pre:
            print("  - ", ln[:120])
        print(f"  > CALLHLL {args.func}   @{addr}")
        for ln in post[: args.after]:
            print("   ", ln[:120])
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
