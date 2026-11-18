#!/usr/bin/env python3
"""把 `XSYS4_HLL_HOT` 的热力表变成**可执行的工作队列**。

## 为什么需要它

`docs/STATS.md` 的「HLL 源码覆盖率 73.4%（3301/4495）」是**源码口径**，
分母包含 Rance10 根本不加载的库（`GUIEngine` / `ReignEngine` / `DrawDungeon` …，
合计 851 个缺口）。按字母顺序补实现，等于把时间花在永远不会被调用的函数上。

实测（`logs/live_h1.log`，标题→开场 ADV→30 次推进）：
整段第一章只调用了 **127** 个 HLL 函数。`XSYS4_HLL_HOT=1` 会把每个函数的
调用次数打到日志里，本脚本据此排出「真正值得实现」的顺序。

## 三类标记

- `TODO`  —— 没有 C 实现（启动时 `library_init` 打 `Unimplemented library function`）
- `STUB`  —— **有** C 实现，但函数体只有 `return 0/false/NULL;`，被算进「已实现」
- `OK`    —— 有真实实现

`STUB` 这一类是关键：`PartsEngine.GetActiveParts`（D309）和
`IsWantSaveBackScene`（D310）都属此类 —— 覆盖率数字里看不见它们，
但本周两个卡点恰好都出在这里。**热榜 × STUB 是最高优先级。**

## 用法

    python3 tools/offline/hll_hotness.py ../logs/live_h1.log [更多日志...]
    python3 tools/offline/hll_hotness.py --todo ../logs/live_h1.log   # 只看待办
    python3 tools/offline/hll_hotness.py --by-lib ../logs/live_h1.log # 按库分类
    python3 tools/offline/hll_hotness.py --emit docs/HLL-L3.md \
            --disasm /tmp/ain_full.txt ../logs/live_h1.log            # 把台账注入计划文档
"""

import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HLL_DIR = os.path.join(REPO, "src", "hll")

HOT_RE = re.compile(r"\[hllhot\]\s+(\S+)\s+(\S+)\s+(\d+)\s*$")
UNIMPL_RE = re.compile(r"Unimplemented library function:\s*([\w]+)\.(\S+)")
EXPORT_RE = re.compile(r"HLL_EXPORT(?:_N|_RT|_NRT)?\(\s*(\w+)\s*,\s*(\w+)\s*\)")
TODO_RE = re.compile(r"HLL_TODO_EXPORT\(\s*(\w+)")
# 只有这些语句的函数体 = 静默桩
TRIVIAL_STMT = re.compile(
    r"^\s*(?:\(\s*void\s*\)\s*[\w\[\]\.\->]+\s*;|return\s+(?:0|NULL|false|true|-1)\s*;)\s*$")


def scan_source():
    """返回 (todo:set, stub:set)，元素为 'Lib.Func'。"""
    todo, stub = set(), set()
    for path in glob.glob(os.path.join(HLL_DIR, "*.c")):
        lib = os.path.basename(path)[:-2]
        src = open(path, encoding="utf-8", errors="replace").read()
        # 去掉注释再扫：注释里提到宏名会让 TODO 统计虚高（踩过一次）。
        src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
        src = re.sub(r"//[^\n]*", "", src)
        lines = src.splitlines()

        for m in TODO_RE.finditer(src):
            todo.add(f"{lib}.{m.group(1)}")

        # C 函数名 -> 是否只有 return 0 类语句
        trivial = {}
        for i, ln in enumerate(lines):
            m = re.match(r"^(?:static\s+)?[A-Za-z_][\w \*]*?\b(\w+)\s*\([^;]*\)\s*$", ln)
            if not m or i + 1 >= len(lines) or lines[i + 1].strip() != "{":
                continue
            body, depth, j = [], 1, i + 2
            while j < len(lines) and depth:
                depth += lines[j].count("{") - lines[j].count("}")
                if depth:
                    body.append(lines[j])
                j += 1
            stmts = [b for b in body if b.strip()]
            if stmts and all(TRIVIAL_STMT.match(b) for b in stmts):
                trivial[m.group(1)] = True

        for m in EXPORT_RE.finditer(src):
            name, ptr = m.group(1), m.group(2)
            if trivial.get(ptr):
                stub.add(f"{lib}.{name}")
    return todo, stub


def parse_logs(paths):
    """返回 {func: calls}，以及日志里出现的未实现集合。"""
    calls, seen_unimpl = {}, set()
    for p in paths:
        for ln in open(p, encoding="utf-8", errors="replace"):
            m = HOT_RE.search(ln)
            if m:
                key = f"{m.group(1)}.{m.group(2)}"
                calls[key] = max(calls.get(key, 0), int(m.group(3)))
                continue
            m = UNIMPL_RE.search(ln)
            if m:
                seen_unimpl.add(f"{m.group(1)}.{m.group(2)}")
    return calls, seen_unimpl


def main(argv):
    todo_only = "--todo" in argv
    paths = [a for a in argv if not a.startswith("--")]
    if not paths:
        print(__doc__)
        return 2

    if "--emit" in argv:
        return emit_ledger(argv)

    todo, stub = scan_source()
    calls, _ = parse_logs(paths)
    if not calls:
        print("日志里没有 [hllhot] 行 —— 跑测时要带 XSYS4_HLL_HOT=1", file=sys.stderr)
        return 1

    def tag(fn):
        if fn in todo:
            return "TODO"
        if fn in stub:
            return "STUB"
        return "OK"

    rows = sorted(calls.items(), key=lambda kv: -kv[1])
    counts = {"TODO": 0, "STUB": 0, "OK": 0}
    for fn, _ in rows:
        counts[tag(fn)] += 1

    print(f"日志: {', '.join(os.path.basename(p) for p in paths)}")
    print(f"运行期被调用的 HLL 函数: {len(rows)} 个"
          f"（TODO {counts['TODO']} / STUB {counts['STUB']} / OK {counts['OK']}）")
    print(f"源码口径缺口合计: {len(todo)} 个"
          f"（其中运行期从未被调用: {len(todo) - counts['TODO']} 个）\n")

    if "--by-lib" in argv:
        per = {}
        for fn, n in rows:
            lib = fn.split(".")[0]
            e = per.setdefault(lib, {"n": 0, "calls": 0, "TODO": 0, "STUB": 0})
            e["n"] += 1
            e["calls"] += n
            e[tag(fn)] = e.get(tag(fn), 0) + 1
        print(f"{'库':22}{'函数数':>7}{'调用次数':>12}{'TODO':>6}{'STUB':>6}")
        print("-" * 56)
        for lib, e in sorted(per.items(), key=lambda kv: -kv[1]["calls"]):
            print(f"{lib:22}{e['n']:>7}{e['calls']:>12}{e['TODO']:>6}{e['STUB']:>6}")
        return 0

    if todo_only:
        rows = [r for r in rows if tag(r[0]) != "OK"]
        if not rows:
            print("本章路径上没有待办 —— 覆盖率数字是假的进展指标。")
            return 0

    print(f"{'次数':>10}  {'类':4}  函数")
    print("-" * 66)
    for fn, n in rows:
        t = tag(fn)
        print(f"{n:>10}  {t:4}  {fn}")

    handled = len(rows) - counts["TODO"] - counts["STUB"]
    print(f"\n真实覆盖率（本章路径）: {handled}/{len(rows)}"
          f" = {100.0 * handled / len(rows):.1f}%")
    return 0




# ---------------------------------------------------------------- 台账注入
LEDGER_TSV = os.path.join(REPO, "tools", "offline", "hll_ledger.tsv")
BEGIN = "<!-- BEGIN:LEDGER -->"
END = "<!-- END:LEDGER -->"
SITE_RE = re.compile(r"CALLHLL\s+arg=\d+\s+\(0x[0-9a-f]+\)(\S+?)\s*$")


def load_ledger():
    """读 func<TAB>级<TAB>契约/证据，缺省 L0。"""
    st = {}
    if not os.path.exists(LEDGER_TSV):
        return st
    for ln in open(LEDGER_TSV, encoding="utf-8"):
        if not ln.strip() or ln.startswith("#"):
            continue
        parts = ln.rstrip("\n").split("\t")
        if len(parts) >= 2:
            st[parts[0]] = (parts[1].strip() or "L0",
                            parts[2].strip() if len(parts) > 2 else "")
    return st


def count_sites(path):
    """每个函数的 AIN 调用点数量。"""
    import collections
    c = collections.Counter()
    for ln in open(path, encoding="utf-8", errors="replace"):
        m = SITE_RE.search(ln)
        if m:
            c[m.group(1)] += 1
    return c


def emit_ledger(argv):
    out_path = argv[argv.index("--emit") + 1]
    disasm = "/tmp/ain_full.txt"
    if "--disasm" in argv:
        disasm = argv[argv.index("--disasm") + 1]
    logs = [a for a in argv if not a.startswith("--") and a != out_path and a != disasm]
    if not logs:
        print("需要至少一个日志文件", file=sys.stderr)
        return 2

    calls, _ = parse_logs(logs)
    sites = count_sites(disasm) if os.path.exists(disasm) else {}
    st = load_ledger()

    rows = sorted(calls.items(), key=lambda kv: (kv[0].split(".")[0], -kv[1]))
    levels = {}
    for fn, _ in rows:
        lv = st.get(fn, ("L0", ""))[0]
        levels[lv] = levels.get(lv, 0) + 1

    buf = [BEGIN, ""]
    buf.append("由 `tools/offline/hll_hotness.py --emit` 生成，**勿手填**；"
               "级与证据在 `tools/offline/hll_ledger.tsv` 维护。")
    buf.append("")
    buf.append("| 级 | 函数数 | 含义 |")
    buf.append("|---|---:|---|")
    for lv in ("L0", "L1", "L2", "L3", "NA"):
        if levels.get(lv):
            label = {'L0':'注册','L1':'类型合法','L2':'单点语义',
                     'L3':'副作用联动','NA':'豁免'}[lv]
            if lv == "L2":
                label += "（getter 终态：无副作用可联动）"
            if lv == "L3":
                label += "（mutator：副作用链已核）"
            buf.append(f"| {lv} | {levels[lv]} | {label} |")
    buf.append(f"| **合计** | **{len(rows)}** | 本章路径调用的 HLL |")
    buf.append("")
    buf.append("| 库 | 函数 | 调用次数 | 调用点 | 级 | 契约 / 证据 |")
    buf.append("|---|---|---:|---:|:--:|---|")
    for fn, n in rows:
        lib, name = fn.split(".", 1)
        lv, note = st.get(fn, ("L0", ""))
        buf.append(f"| {lib} | `{name}` | {n} | {sites.get(fn, 0)} | {lv} | {note} |")
    buf.append("")
    buf.append(END)

    try:
        doc = open(out_path, encoding="utf-8").read()
    except FileNotFoundError:
        doc = ""
    block = "\n".join(buf)
    if BEGIN in doc and END in doc:
        doc = doc[:doc.index(BEGIN)] + block + doc[doc.index(END) + len(END):]
    else:
        doc = doc.rstrip("\n") + "\n\n" + block + "\n"
    open(out_path, "w", encoding="utf-8").write(doc)
    print(f"台账已写入 {out_path}：{len(rows)} 行，"
          + " / ".join(f"{k}={v}" for k, v in sorted(levels.items())))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
