#!/usr/bin/env python3
"""按字节码找「谁调用了某个函数」—— 修 `ain_xref` 的两处盲区。

## 为什么需要这个

`tools/bin/ain_xref <ain> call <fno>` 只认反汇编里**成功解析出目标名**的
`CALLMETHOD`。兰斯10 的 AIN 里大量 `CALLMETHOD` 解析结果是错的
（打印成 `message::detail::GetMessageSpeedRate` 之类），于是这些调用点
统计不到，输出 `0 sites`。

2026-11-19/20 因为这个盲区误判过至少三次：

- `AFL_Parts_AddMouseLClickEvent`(6892) → 判成「运行期 0 次调用」，
  其实是 `ActivityButton@0` 给**每个** ActivityButton 注册点击的必经之路；
- `LargeSkillButton@SetOnClickEvent`(25350) → 判成「零调用点 ⇒ m3 恒空」，
  实际由 `BattleCardButton@InitSkillButtonCollection` 调到；
- `BattleCardButton@SetOnClickCardEvent`(25255) → 同上。

## 判据（AIN 的调用约定）

    [this] [method] [arg0] [arg1] ... CALLMETHOD nargs

也就是说 `CALLMETHOD` 之前**最后压进去的方法号**就是被调函数。

## 第二个坑：嵌套 lambda

AIN 把 lambda 的函数体**就地**放在外层函数中间：

    FUNC <外层>
      ... 外层代码 ...
      JUMP <lambda 之后>
    FUNC <lambda>        <- 嵌套
      ...
    ENDFUNC <lambda>
      ... 外层代码继续 ...
    ENDFUNC <外层>

所以「最近的 FUNC 头」经常是 lambda，而不是真正的调用者。本脚本用栈还原
（`FUNC` 压栈、`ENDFUNC` 弹出），报告的 enclosing 就是真正的外层函数。

用法：
    python3 tools/offline/ain_callers.py /tmp/ain_full.txt 25350
    python3 tools/offline/ain_callers.py /tmp/ain_full.txt 25350 --context 6

`/tmp/ain_full.txt` 这样生成（**必须带 game 目录**）：

    env -u DISPLAY ./build/src/xsystem4 --disasm 0:0x700000 game > /tmp/ain_full.txt
"""
import re
import sys

FUNC_RE = re.compile(r'0x([0-9a-f]+): FUNC\s+arg=(\d+)')
END_RE = re.compile(r'0x([0-9a-f]+): ENDFUNC\s+arg=(\d+)')


def enclosing_map(lines):
    """地址 → 该地址真正所属的（最外层）函数号。"""
    out = {}
    stack = []
    for ln in lines:
        m = FUNC_RE.match(ln)
        if m:
            stack.append(int(m.group(2)))
            continue
        m = END_RE.match(ln)
        if m:
            if stack and stack[-1] == int(m.group(2)):
                stack.pop()
            continue
        m = re.match(r'0x([0-9a-f]+):', ln)
        if m and stack:
            out[int(m.group(1), 16)] = stack[-1]
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    path = sys.argv[1]
    target = int(sys.argv[2])
    ctx = 0
    if '--context' in sys.argv:
        ctx = int(sys.argv[sys.argv.index('--context') + 1])

    lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
    owner = enclosing_map(lines)
    pat = re.compile(r'PUSH\s+arg=%d\b' % target)
    # `CALLFUNC arg=N` 是直接调用（非方法），同样要算进来；
    # `AFL_*` 这类全局函数全走这条路。
    callfunc_pat = re.compile(r'CALLFUNC\s+arg=%d\b' % target)
    sites = []
    for i, ln in enumerate(lines):
        if callfunc_pat.search(ln):
            m = re.match(r'0x([0-9a-f]+):', ln)
            addr = int(m.group(1), 16) if m else 0
            sites.append((addr, owner.get(addr)))
            continue
        if not pat.search(ln):
            continue
        # 往后找 CALLMETHOD；允许中间夹其它压栈
        for k in range(i + 1, min(i + 10, len(lines))):
            if 'CALLMETHOD' in lines[k]:
                m = re.match(r'0x([0-9a-f]+):', ln)
                addr = int(m.group(1), 16) if m else 0
                sites.append((addr, owner.get(addr)))
                break
            if 'FUNC' in lines[k] or 'ENDFUNC' in lines[k]:
                break

    from collections import Counter
    cnt = Counter(f for _, f in sites)
    print("target %d: %d 个 CALLMETHOD 调用点，%d 个不同的调用函数"
          % (target, len(sites), len(cnt)))
    for f, c in cnt.most_common():
        print("  调用函数 fno=%s  ×%d" % (f, c))
    if ctx:
        for addr, f in sites[:8]:
            print("\n---- 0x%08x (调用函数 fno=%s)" % (addr, f))
            for i, ln in enumerate(lines):
                if ln.startswith('0x%08x:' % addr):
                    for j in range(max(0, i - ctx), min(len(lines), i + 3)):
                        print("   ", lines[j].rstrip())
                    break
    return 0


if __name__ == '__main__':
    sys.exit(main())
