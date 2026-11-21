#!/usr/bin/env python3
"""ain_show.py —— 按函数号反汇编一个 AIN 函数（读 `--disasm` 的全量输出）。

用法：
    ain_show.py <dump> <fno> [fno2 ...]        打完整函数体
    ain_show.py <dump> <fno> --calls           只列它调用的函数
    ain_show.py <dump> <fno> --who             只列**谁**调用它（全 dump 扫）
    ain_show.py <dump> <fno> --flat            不含内联 lambda 的主体

为什么单独一个工具：`--disasm` 输出是地址升序的平铺文本，函数边界只能靠
`FUNC/ENDFUNC` 配。每次手写 bisect 脚本既慢又容易把区间划错
（曾把第二章的 `RunShop` 算进第一章）。

**lambda 是内联的**：外层函数的 `FUNC` 之后、它自己的 `ENDFUNC` 之前，
还会出现别的完整 `FUNC…ENDFUNC`（编译器把闭包体排在调用点后面，用一条
`JUMP` 跳过）。所以配对必须用栈：`FUNC` 入栈、`ENDFUNC` 出栈，
出栈那一刻记下该函数的 (起始行, 结束行)。区间里天然包含内联 lambda。

只读，不依赖引擎。
"""
import re
import sys

FUNC_RE = re.compile(r'^0x[0-9a-f]+: FUNC\s+arg=(\d+)')
ENDFUNC_RE = re.compile(r'^0x[0-9a-f]+: ENDFUNC\s+arg=(\d+)')
ADDR_RE = re.compile(r'^0x([0-9a-f]+): FUNC')
CALL_RE = re.compile(r'\[(\d+)\] (.+?) @0x([0-9a-f]+)\s*$')


def build_index(path):
    """返回 {fno: (start_line, end_line, addr)}。

    函数边界的真相（全量 dump 实测）：
      - `FUNC` 35934 条，`ENDFUNC` 只有 15684 条 —— **`ENDFUNC` 只给内联 lambda 用**，
        普通方法没有结束标记，靠 `RETURN` 结束。
      - 内联 lambda 的 `FUNC` **紧跟在一条 `JUMP` 之后**（外层用 JUMP 跳过闭包体）；
        同级下一个方法的 `FUNC` 前面是 `RETURN` / `ENDFUNC`。
    所以用「前一条是不是 JUMP」区分「嵌套 lambda」和「下一个同级方法」。
    按栈序配对会在缺 `ENDFUNC` 处整体错位（实测残栈 20250 条），不能那样做。
    """
    index = {}
    addrs = {}
    stack = []
    prev_op = None
    n = 0
    with open(path, 'rb') as f:
        for raw in f:
            line = raw.decode('utf-8', 'replace')
            m = FUNC_RE.match(line)
            if m:
                if stack and prev_op != 'JUMP':
                    while stack:
                        fno, st = stack.pop()
                        index[fno] = (st, n - 1, addrs[fno])
                fno = int(m.group(1))
                am = ADDR_RE.match(line)
                addrs[fno] = int(am.group(1), 16)
                stack.append((fno, n))
                prev_op = 'FUNC'
                n += 1
                continue
            m = ENDFUNC_RE.match(line)
            if m:
                if stack:
                    fno, st = stack.pop()
                    index[fno] = (st, n, addrs[fno])
                prev_op = 'ENDFUNC'
                n += 1
                continue
            am = re.match(r'^0x[0-9a-f]+: ([A-Z_0-9]+)', line)
            if am:
                prev_op = am.group(1)
            n += 1
    while stack:
        fno, st = stack.pop()
        index[fno] = (st, n - 1, addrs[fno])
    return index


def read_lines(path, start, end):
    out = []
    with open(path, 'rb') as f:
        for i, raw in enumerate(f):
            if i < start:
                continue
            if i > end:
                break
            out.append(raw.decode('utf-8', 'replace').rstrip('\n'))
    return out


def callees(body):
    out = []
    for line in body:
        if 'CALLFUNC' not in line and 'CALLMETHOD' not in line:
            continue
        tail = line.split('->', 1)[1].strip() if '->' in line else line
        if tail == '?':
            out.append(('?', 'dynamic'))
            continue
        m = CALL_RE.search(line.strip())
        out.append((m.group(1), m.group(2).strip()) if m else ('?', 'dynamic'))
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    path = argv[1]
    args = argv[2:]
    mode = None
    if args and args[-1].startswith('--'):
        mode = args[-1]
        args = args[:-1]
    index = build_index(path)

    if mode == '--who':
        # 单遍流式扫：`read_lines` 每个函数都从文件头重读，是 O(n^2)，
        # 全量 dump 上要跑几分钟。这里只走一遍。
        target = int(args[0])
        want = '[%d]' % target
        n = 0
        cur = None
        prev_op = None
        stack = []
        with open(path, 'rb') as f:
            for raw in f:
                line = raw.decode('utf-8', 'replace')
                m = FUNC_RE.match(line)
                if m:
                    if stack and prev_op != 'JUMP':
                        stack = []
                    stack.append(int(m.group(1)))
                    cur = stack[-1]
                    prev_op = 'FUNC'
                    continue
                if ENDFUNC_RE.match(line):
                    if stack:
                        stack.pop()
                    cur = stack[-1] if stack else None
                    prev_op = 'ENDFUNC'
                    continue
                am = re.match(r'^0x[0-9a-f]+: ([A-Z_0-9]+)', line)
                if am:
                    prev_op = am.group(1)
                if cur is not None and cur != target and want in line and \
                        ('CALLFUNC' in line or 'CALLMETHOD' in line):
                    print('fno=%-6d %s' % (cur, line.strip()))
                    n += 1
        print('== %d 个调用者' % n)
        return 0

    for a in args:
        fno = int(a)
        if fno not in index:
            print('no such function: %d' % fno)
            continue
        start, end, addr = index[fno]
        body = read_lines(path, start, end)
        if mode == '--flat':
            body = [l for l in body if not FUNC_RE.match(l)]
        if mode == '--calls':
            print('===== fno %d @0x%06x =====' % (fno, addr))
            for t, nm in callees(body):
                print('  %-6s %s' % (t, nm))
        else:
            print('===== fno %d @0x%06x  (%d 行) =====' % (fno, addr, len(body)))
            print('\n'.join(body))
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
