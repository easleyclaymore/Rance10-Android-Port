#!/usr/bin/env python3
"""把 `XSYS4_HLL_HOT=1` 的热力表交叉比对「有没有 C 实现」，输出优先级清单。

为什么要这个工具
================
启动期 544 条 `Unimplemented library function`，看着像 544 个待办。实测恰恰
相反：**绝大多数从来没被调用过**。`GUIEngine` 有 336 个缺口，跑完第一章 +
战斗第一回合，运行期调用 **0 次** —— 那块是菜单/表单系统，我们根本没进去。

按缺口字母顺序补实现，会把时间花在永远不会执行的函数上。这个工具按
「跑过的场景里实际调了多少次」排序，并且只对**缺口**（热但未实现）报警。

三档分类
--------
  热   调用次数 > 0 且**没有** C 实现   → 立刻补，这些是真实的静默失败点
  温   有 C 实现但次数为 0              → 已走通的路径没用到，别急着优化
  冷   无实现且次数为 0                 → 冻结；等对应模块开工再说

用法
----
    # 1. 带开关跑一次（场景越全，数据越有用）
    XSYS4_HLL_HOT=1 XSYS4_AUTO_MAP=1 XSYS4_NODE_DIGIT=2 \
        tools/run/run-headless.sh hot1 60 /tmp/hot1.png

    # 2. 分析
    python3 tools/dev/hll_hotness.py ../logs/live_hot1.log

    # 只关心缺口
    python3 tools/dev/hll_hotness.py ../logs/live_hot1.log --gaps
    # 输出 JSON（给别的脚本吃）
    python3 tools/dev/hll_hotness.py ../logs/live_hot1.log --json

判断「有没有 C 实现」不用猜：跑一次引擎，启动期会为**每一个**未实现的导出
打一行 `Unimplemented library function: Lib.Func`。那个集合就是缺口集合，
和热力表取交集即可。
"""
import argparse
import collections
import json
import re
import sys

HOT_RE = re.compile(r"\[hllhot\]\s+(\S+)\s+(\S+)\s+(\d+)\s*$")
GAP_RE = re.compile(r"Unimplemented library function:\s*(\S+)\.(\S+)")


def parse(log_path):
    hot = collections.Counter()
    gaps = set()
    with open(log_path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = HOT_RE.search(line)
            if m:
                hot[(m.group(1), m.group(2))] += int(m.group(3))
                continue
            m = GAP_RE.search(line)
            if m:
                gaps.add((m.group(1), m.group(2)))
    return hot, gaps


def classify(hot, gaps):
    rows = []
    seen = set(hot) | gaps
    for key in seen:
        lib, fn = key
        calls = hot.get(key, 0)
        implemented = key not in gaps
        if calls > 0 and not implemented:
            tier = "hot"     # 真实静默失败点：被调用但没有实现
        elif calls > 0:
            tier = "live"    # 正常：实现了也在跑
        elif not implemented:
            tier = "cold"    # 冻结：没实现也没调用
        else:
            tier = "warm"    # 实现了但这次场景没走到
        rows.append({
            "lib": lib, "func": fn, "calls": calls,
            "implemented": implemented, "tier": tier,
        })
    rows.sort(key=lambda r: (-r["calls"], r["lib"], r["func"]))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", help="带 XSYS4_HLL_HOT=1 跑出来的日志")
    ap.add_argument("--gaps", action="store_true", help="只列缺口（hot 档）")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--top", type=int, default=0, help="只显示前 N 条")
    args = ap.parse_args()

    hot, gaps = parse(args.log)
    if not hot:
        sys.exit("日志里没有 [hllhot] 行 —— 这次跑测没开 XSYS4_HLL_HOT=1，"
                 "或者进程没走到 atexit（被 SIGKILL 而非正常退出）")

    rows = classify(hot, gaps)
    if args.gaps:
        rows = [r for r in rows if r["tier"] == "hot"]
    if args.top:
        rows = rows[:args.top]

    if args.json:
        json.dump(rows, sys.stdout, ensure_ascii=False, indent=1)
        print()
        return 0

    by_tier = collections.Counter(r["tier"] for r in classify(hot, gaps))
    print(f"日志        : {args.log}")
    print(f"有调用记录   : {len(hot)} 个 HLL 函数，共 {sum(hot.values())} 次")
    print(f"缺口（链接期）: {len(gaps)} 个")
    print()
    print("三档分布：")
    print(f"  hot   热（被调用但没实现） : {by_tier['hot']:4d}  ← 先补这些")
    print(f"  live  在跑（已实现）       : {by_tier['live']:4d}")
    print(f"  warm  已实现但本次没走到   : {by_tier['warm']:4d}")
    print(f"  cold  冷（没实现也没调用） : {by_tier['cold']:4d}  ← 冻结")
    print()

    hot_rows = [r for r in rows if r["tier"] == "hot"]
    if hot_rows:
        print("=" * 78)
        print("热缺口（被调用但无 C 实现）—— 这些是静默失败点，按次数排")
        print("=" * 78)
        print(f"{'次数':>9}  {'库':<20} {'函数':<40}")
        for r in hot_rows:
            print(f"{r['calls']:>9}  {r['lib']:<20} {r['func']:<40}")
    else:
        print("热缺口：无 —— 已走通的路径上没有「被调用但未实现」的 HLL。")

    if not args.gaps:
        print()
        print("=" * 78)
        print("按库汇总（热缺口数 / 总缺口 / 运行期调用次数）")
        print("=" * 78)
        agg = collections.defaultdict(lambda: {"gap_hot": 0, "gap_all": 0, "calls": 0})
        for r in classify(hot, gaps):
            a = agg[r["lib"]]
            a["calls"] += r["calls"]
            if not r["implemented"]:
                a["gap_all"] += 1
                if r["tier"] == "hot":
                    a["gap_hot"] += 1
        print(f"{'库':<22} {'热缺口':>7} {'总缺口':>7} {'调用次数':>10}")
        for lib, a in sorted(agg.items(), key=lambda kv: -kv[1]["gap_hot"] or -kv[1]["calls"]):
            if a["gap_all"] == 0 and a["calls"] == 0:
                continue
            flag = "  ← 有热缺口" if a["gap_hot"] else ""
            print(f"{lib:<22} {a['gap_hot']:>7} {a['gap_all']:>7} {a['calls']:>10}{flag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
