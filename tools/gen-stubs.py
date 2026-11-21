#!/usr/bin/env python3
"""生成 docs/STUBS.md —— HLL 未实现桩的工单。

为什么要这个：`HLL_TODO_EXPORT` 有 1191 个、`HLL_WARN_UNIMPLEMENTED` 若干、
D91 手写的 `SS_Stub_*` 又一批。它们散在 20 多个文件里，"还剩多少、哪些被真的
调用过"没人答得上来 —— 于是每轮只能靠日志里那 500 多条刷屏估个数。

本脚本只做一件事：把三种桩统一成一张表，按文件分组，并标出**日志里真的被调用过**
的名字（有日志才有列）。不做判断、不猜语义。

用法：
    python3 tools/gen-stubs.py                     # 扫代码，输出 docs/STUBS.md
    python3 tools/gen-stubs.py --logs ../logs      # 额外标注「被调用过」
    python3 tools/gen-stubs.py --check             # 只校验（CI 用）：有桩没登记就退出 1
"""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HLL = ROOT / 'src' / 'hll'
OUT = ROOT / 'docs' / 'STUBS.md'

# 三种桩的形态
RE_TODO = re.compile(r'HLL_TODO_EXPORT\(\s*(\w+)\s*,')
RE_WARN = re.compile(r'HLL_WARN_UNIMPLEMENTED\([^,]+,\s*[^,]+,\s*(\w+)\s*,\s*(\w+)')
RE_STUBFN = re.compile(r'^static\s+\w[\w\s\*]*?\b(SS_Stub_\w+)\s*\(', re.M)


def scan():
    """返回 {文件: [(kind, lib, name)]}"""
    out = {}
    for f in sorted(HLL.glob('*.c')):
        items = []
        txt = f.read_text(encoding='utf-8', errors='replace')
        for m in RE_TODO.finditer(txt):
            items.append(('TODO', f.stem, m.group(1)))
        for m in RE_WARN.finditer(txt):
            items.append(('WARN', m.group(1), m.group(2)))
        for m in RE_STUBFN.finditer(txt):
            items.append(('STUB', f.stem, m.group(1)))
        if items:
            out[f.name] = items
    return out


def called_names(log_dir):
    """日志里出现过 `Unimplemented library function: <lib>.<name>` 的集合。"""
    names = set()
    if not log_dir or not log_dir.is_dir():
        return names
    for log in log_dir.glob('live_*.log'):
        try:
            data = log.read_bytes()
        except OSError:
            continue
        for line in data.split(b'\n'):
            i = line.find(b'Unimplemented library function: ')
            if i < 0:
                continue
            tail = line[i + len(b'Unimplemented library function: '):].strip()
            dot = tail.rfind(b'.')
            if dot > 0:
                names.add(tail[dot + 1:].decode('utf-8', 'replace'))
    return names


def render(table, called):
    total = sum(len(v) for v in table.values())
    rows = []
    rows.append('# HLL 未实现桩（工单，脚本生成）')
    rows.append('')
    rows.append('> `python3 tools/gen-stubs.py` 生成，**不要手改**。'
                '`--logs ../logs` 会额外标注「日志里真的被调用过」。')
    rows.append('')
    rows.append('三种桩的形态：')
    rows.append('')
    rows.append('| 形态 | 含义 | 危险度 |')
    rows.append('|---|---|---|')
    rows.append('| `HLL_TODO_EXPORT` | 导出表里 `fun = NULL`，**链接失败** ⇒ 每次调用报未实现并静默返回 0 | 中（会刷屏、返回值可能是垃圾） |')
    rows.append('| `HLL_WARN_UNIMPLEMENTED` | 有函数，但**返回写死的默认值** | **高**：`SetXxx` 返回 `false` 等于告诉 AIN「设置失败」，会把它带进降级分支 |')
    rows.append('| `SS_Stub_*` | 手写空桩（D91 时代） | 高（同上，且没人看得出是假的） |')
    rows.append('')
    rows.append(f'总计 **{total}** 个。按文件：')
    rows.append('')
    rows.append('| 文件 | 总数 | 被调用过 | 其中 TODO / WARN / STUB |')
    rows.append('|---|---|---|---|')
    for fname, items in sorted(table.items(), key=lambda kv: -len(kv[1])):
        kinds = {'TODO': 0, 'WARN': 0, 'STUB': 0}
        hit = 0
        for k, _lib, name in items:
            kinds[k] += 1
            if name in called:
                hit += 1
        rows.append(f'| `{fname}` | {len(items)} | {hit} | '
                    f"{kinds['TODO']} / {kinds['WARN']} / {kinds['STUB']} |")
    rows.append('')
    rows.append('## 优先填哪些')
    rows.append('')
    rows.append('排序只看两条，不看数量：')
    rows.append('')
    rows.append('1. **`WARN` / `STUB` 优先于 `TODO`** —— `TODO` 至少会报错，'
                '而假成功/假失败会让 AIN 的判定**看起来成立**，错得更安静；')
    rows.append('2. **同一组 set/get 里只要有一个是桩，整组都要补** —— '
                '`Set` 存不住时 `Get` 返回什么都是错的（D393 的变量系统就是这个形状）。')
    rows.append('')
    rows.append('填完一批的验收一律是 `tools/run/smoke.sh quick`：')
    rows.append('退出码 0，且日志里该函数不再出现在 `Unimplemented library function` 后面。')
    rows.append('')
    return '\n'.join(rows) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--logs', type=pathlib.Path, default=None)
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()

    table = scan()
    called = called_names(args.logs)
    text = render(table, called)

    if args.check:
        if not OUT.exists() or OUT.read_text(encoding='utf-8') != text:
            print('STUBS.md 过期：跑 python3 tools/gen-stubs.py --logs ../logs', file=sys.stderr)
            return 1
        print('stubs: OK')
        return 0

    OUT.write_text(text, encoding='utf-8')
    total = sum(len(v) for v in table.values())
    print(f'wrote {OUT} ({total} stubs, {len(called)} names seen in logs)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
