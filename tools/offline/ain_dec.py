#!/usr/bin/env python3
"""把 `--disasm` 的输出渲染成带控制流结构的伪代码。

为什么需要：`--disasm` 一行一条指令，一个方法动辄 300+ 行，其中
`PUSHLOCALPAGE / PUSH 2 / REF / DELETE / SWAP / ASSIGN` 这种「给 local2 赋值」
的固定花纹要占掉一半。人眼读这种流，读三条就忘了上面是什么，而这正是
「把语义一个个弄清」最贵的地方。

本工具不做真正的反编译（不做类型推断、不重建表达式树），只做四件确定的事：

  1. **访问器还原**：`PUSHLOCALPAGE; PUSH k; REF` → `local<k>`
                      `PUSHSTRUCTPAGE; PUSH k; REF` → `this.m<k>`
                      `PUSHGLOBALPAGE; PUSH k; REF`  → `global<k>`
  2. **调用还原**：`CALLMETHOD n` 的实参按 AIN 的压栈顺序还原成
                  `obj.Name(a1, a2, …)`（obj 在栈底，fno 在实参下面）。
  3. **控制流分块**：`IFZ/IFNZ + 跳转` 还原成 `if (…) { }`，`JUMP` 还原成
                  `goto LABEL`，前向跳转目标变成标签，并按块缩进。
  4. **赋值折叠**：`ASSIGN` 还原成 `target = value`；`DELETE` / `POP` /
                  `SP_INC` 这类栈整理指令直接吞掉。

不确定的地方**不猜**：栈空了就写 `?`，认不出的指令原样打印成 `# RAW <op>`。
宁可露出「这里我没懂」，也不要写一句看起来合理但是错的伪代码。

用法：
    python3 tools/offline/ain_dec.py <disasm.txt> <fno> [fno …]
    python3 tools/offline/ain_dec.py <disasm.txt> <fno> --raw   # 附原始指令
"""

import re
import sys

LINE_RE = re.compile(r'^0x([0-9a-f]+): ([A-Z_0-9]+)\s*(.*)$')
FUNC_RE = re.compile(r'^0x([0-9a-f]+): FUNC\s+arg=(\d+)')
ENDFUNC_RE = re.compile(r'^0x([0-9a-f]+): ENDFUNC\s+arg=(\d+)')
ARG_RE = re.compile(r'arg=(\d+)')
NAME_RE = re.compile(r'\[(\d+)\]\s+(.*?)\s+@0x')

PAGE_TOKENS = {'PUSHLOCALPAGE': 'L', 'PUSHGLOBALPAGE': 'G',
               'PUSHSTRUCTPAGE': 'S'}
PAGE_ACCESSOR = {'L': 'local', 'G': 'global', 'S': 'this.m', 'C': 'cap'}

# 只影响栈里"记账"、不产生行为的指令
NOISE = {'POP', 'SP_INC', 'DELETE', 'NOP'}

BINOPS = {'ADD': '+', 'SUB': '-', 'MUL': '*', 'DIV': '/'}


SIG = {}


def load_signatures(path):
    """`--dump-signatures` 的输出：fno TAB nr_args TAB nr_vars TAB name。"""
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            parts = line.rstrip('\n').split('\t')
            if len(parts) >= 2 and parts[0].isdigit():
                SIG[int(parts[0])] = int(parts[1])


def load_funcs(path):
    """返回 {fno: (start_line, end_line)}，用「FUNC 前一条是不是 JUMP」
    区分内联 lambda 与同级函数（ENDFUNC 只给 lambda 用）。"""
    index = {}
    stack = []
    prev_op = None
    n = 0
    with open(path, 'rb') as f:
        for raw in f:
            line = raw.decode('utf-8', 'replace').rstrip('\n')
            m = FUNC_RE.match(line)
            if m:
                if stack and prev_op != 'JUMP':
                    while stack:
                        fno, st = stack.pop()
                        index[fno] = (st, n - 1)
                stack.append((int(m.group(2)), n))
                prev_op = 'FUNC'
                n += 1
                continue
            if ENDFUNC_RE.match(line):
                if stack:
                    fno, st = stack.pop()
                    index[fno] = (st, n)
                prev_op = 'ENDFUNC'
                n += 1
                continue
            am = re.match(r'^0x[0-9a-f]+: ([A-Z_0-9]+)', line)
            if am:
                prev_op = am.group(1)
            n += 1
    while stack:
        fno, st = stack.pop()
        index[fno] = (st, n - 1)
    return index


def read_ranges(path, wanted):
    """一次遍历只取需要的行区间，避免每个函数都从文件头重读。"""
    out = {fno: [] for fno in wanted}
    n = 0
    with open(path, 'rb') as f:
        for raw in f:
            line = raw.decode('utf-8', 'replace').rstrip('\n')
            for fno, (a, b) in wanted.items():
                if a <= n <= b:
                    out[fno].append(line)
                    break
            n += 1
    return out


class Insn:
    __slots__ = ('addr', 'op', 'arg', 'raw')

    def __init__(self, addr, op, arg, raw):
        self.addr = addr
        self.op = op
        self.arg = arg
        self.raw = raw


def parse(lines):
    ins = []
    for l in lines:
        m = LINE_RE.match(l)
        if not m:
            continue
        addr = int(m.group(1), 16)
        op = m.group(2)
        rest = m.group(3)
        am = ARG_RE.search(rest)
        arg = int(am.group(1)) if am else None
        # 跳转目标在括号里：(0x1e0b7a)
        bm = re.search(r'\(0x([0-9a-f]+)\)', rest)
        tgt = int(bm.group(1), 16) if bm else None
        ins.append(Insn(addr, op, arg if tgt is None else tgt, l))
    return ins


def call_name(ins_rest):
    m = NAME_RE.search(ins_rest) or re.search(r'-> \[(\d+)\] (.*)', ins_rest)
    if m:
        return m.group(2).strip()
    m = re.search(r'-> (\S+)', ins_rest)
    return m.group(1) if m else '?'


def render(lines, header, show_raw=False):
    ins = parse(lines)
    if not ins:
        return ['%s: <空>' % header]

    # 跳转目标 → 标签
    targets = {}
    for i in ins:
        if i.op in ('IFZ', 'IFNZ', 'JUMP') and i.arg is not None:
            targets.setdefault(i.arg, 'L%x' % i.arg)

    out = ['%s' % header]
    stack = []
    indent = 0
    block_stack = []      # [(label, saved_indent)]

    def pop(n=1):
        got = []
        for _ in range(n):
            got.append(stack.pop() if stack else '?')
        got.reverse()
        return got

    def emit(s):
        out.append('    ' * indent + s)

    for idx, i in enumerate(ins):
        a = targets.get(i.addr)
        if a:
            # 到达某个标签：先把对应的块收掉
            while block_stack and block_stack[-1][0] == a:
                indent = block_stack.pop()[1]
                emit('}')
            emit('%s:' % a)

        op = i.op
        nxt = ins[idx + 1] if idx + 1 < len(ins) else None

        if op in PAGE_TOKENS:
            stack.append('@' + PAGE_TOKENS[op])
        elif op == 'PUSH':
            stack.append(str(i.arg))
        elif op == 'REF':
            val, page = pop(1)[0], (stack.pop() if stack else '?')
            if isinstance(page, str) and page.startswith('@'):
                stack.append('%s%s' % (PAGE_ACCESSOR[page[1:]], val))
            else:
                stack.append('%s[%s]' % (page, val))
        elif op == 'A_REF':
            v = pop(1)[0]
            stack.append('%s[]' % v)
        elif op == 'X_GETENV':
            # `PUSHLOCALPAGE; X_GETENV` 取的是**闭包捕获帧**的 local page，
            # 之后照常 `PUSH k; REF` 取第 k 个捕获变量。
            v = pop(1)[0]
            if isinstance(v, str) and v.startswith('@'):
                stack.append('@C')
            else:
                stack.append('capture(%s)' % v)
        elif op in BINOPS:
            b, c = pop(2)
            stack.append('(%s %s %s)' % (c, BINOPS[op], b))
        elif op == 'NOTE':          # != 
            b, c = pop(2)
            stack.append('(%s != %s)' % (c, b))
        elif op == 'EQUALE':
            b, c = pop(2)
            stack.append('(%s == %s)' % (c, b))
        elif op == 'NOT':
            v = pop(1)[0]
            stack.append('!%s' % v)
        elif op == 'INC':
            v = pop(1)[0]
            stack.append('%s + 1' % v)
        elif op == 'ASSIGN':
            val = pop(1)[0]
            tgt = pop(1)[0]
            emit('%s = %s' % (tgt, val))
        elif op in NOISE:
            pass
        elif op in ('IFZ', 'IFNZ'):
            cond = pop(1)[0]
            lbl = targets.get(i.arg, 'L%x' % (i.arg or 0))
            if op == 'IFNZ':
                emit('if (%s) {' % cond)
            else:
                emit('if (!(%s)) {' % cond)
            block_stack.append((lbl, indent))
            indent += 1
        elif op == 'JUMP':
            lbl = targets.get(i.arg, 'L%x' % (i.arg or 0))
            emit('goto %s' % lbl)
        elif op == 'RETURN':
            if stack:
                emit('return %s' % pop(1)[0])
            else:
                emit('return')
        elif op == 'CALLMETHOD':
            n = i.arg or 0
            args = pop(n)
            fno = pop(1)[0]
            obj = pop(1)[0]
            if obj == '@S':
                # `PUSHSTRUCTPAGE; PUSH <fno>; [args]; CALLMETHOD n` 是
                # 「对当前结构自身调方法」，接收者就是隐式 this。
                emit('this.%s(%s)' % (call_name(i.raw), ', '.join(args)))
            else:
                emit('%s -> %s(%s)' % (obj, call_name(i.raw), ', '.join(args)))
        elif op == 'CALLFUNC':
            n = SIG.get(i.arg)
            if n is None:
                emit('# CALLFUNC(arity 未知) %s' % call_name(i.raw))
                stack.clear()
            else:
                args = pop(n)
                emit('%s(%s)' % (call_name(i.raw), ', '.join(args)))
        elif op == 'CALLHLL':
            m = re.search(r'\)\s*([A-Za-z0-9_.]+)\s*$', i.raw)
            emit('hll %s' % (m.group(1) if m else '?'))
        elif op in ('FUNC', 'ENDFUNC'):
            pass
        elif op == 'DG_NEW_FROM_METHOD':
            # 生成一个「指向本结构某方法」的委托值，供 CreateEndTask 之类消费。
            stack.append('dg(method %s)' % (i.arg if i.arg is not None else '?'))
        elif op.startswith('DG_'):
            emit('%s %s' % (op, i.arg if i.arg is not None else ''))
            stack.clear()
        else:
            emit('# RAW %s' % i.raw.split(': ', 1)[-1])
            stack.clear()

        if show_raw:
            out[-1] = out[-1] + '   ;; ' + i.raw.split(': ', 1)[-1]

    while block_stack:
        indent = block_stack.pop()[1]
        emit('}')
    return out


def guess_arity(ins):
    """CALLFUNC 的实参个数反汇编里没写。用「上一条 CALLFUNC 之后压了几个值」
    猜不出来，所以这里返回 None，交给调用者按签名补。"""
    return None


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    path = argv[1]
    show_raw = '--raw' in argv
    # 签名表默认找 /tmp/ainsig.tsv；没有就退化成「CALLFUNC 只打名字」。
    for cand in ('/tmp/ainsig.tsv', '/tmp/fnsig.tsv'):
        try:
            load_signatures(cand)
            break
        except OSError:
            continue
    fnos = [int(a) for a in argv[2:] if a.isdigit()]
    index = load_funcs(path)
    wanted = {f: index[f] for f in fnos if f in index}
    missing = [f for f in fnos if f not in index]
    ranges = read_ranges(path, wanted)
    for f in fnos:
        if f not in index:
            print('fno %d: 未在 dump 里找到' % f)
            continue
        a, b = index[f]
        first = ranges[f][0] if ranges[f] else ''
        header = '===== fno %d  (%d 行) =====' % (f, b - a + 1)
        print('\n'.join(render(ranges[f], header, show_raw)))
        print()
    return 0 if not missing else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
