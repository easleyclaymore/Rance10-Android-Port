#!/usr/bin/env python3
"""生成 docs/STATS.md，并把稳定数字注入 README.md 的标记块。

对应《复杂底层项目的工程管理》§6 ⑪ / 缺口 6：
「所有『共 N 个』的数字由脚本生成或直接写『以生成结果为准』，禁止手写」。
历史代价：同一份开关数量在三个文档里分别是 96 / 116 / 136；
PROJECT-MANAGEMENT.md §1 与 §9 的 HLL 覆盖率也自相矛盾。

用法：
    tools/gen-doc-numbers.py            # 生成 STATS.md + 注入 README 标记块
    tools/gen-doc-numbers.py --check    # 只校验；手写数字腐化 / 生成物过期即返回非 0

为避免自指，参与 --check 的指标**不含**：git 提交数、生成时间、
本文件（STATS.md）自身行数、含注入块的 README 行数。
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))   # .../Rance10_Port/xsystem4/tools
REPO = os.path.dirname(HERE)                         # .../Rance10_Port/xsystem4
PORT = os.path.dirname(REPO)                          # .../Rance10_Port
WS = os.path.dirname(PORT)                            # .../兰斯10

DOCS = os.path.join(REPO, "docs")
STATS = os.path.join(DOCS, "STATS.md")
README = os.path.join(WS, "README.md")
TECH_DEBT = os.path.join(WS, "TECH_DEBT.md")
HYP = os.path.join(DOCS, "HYPOTHESES.md")
IMPACT = os.path.join(DOCS, "IMPACT.md")
SWITCHES = os.path.join(DOCS, "SWITCHES.md")
PM = os.path.join(DOCS, "PROJECT-MANAGEMENT.md")

BEGIN = "<!-- STATS:BEGIN 由 tools/gen-doc-numbers.py 生成，不要手改 -->"
END = "<!-- STATS:END -->"

# 首次注入的锚点：README §4 里那句手写的开关数量。
ANCHOR = "`grep -rho 'XSYS4_[A-Z0-9_]*' Rance10_Port/xsystem4/src | sort -u` 可列全（96 个）。"


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def nlines(path):
    return len(read(path).splitlines())


def collect():
    m = {}

    # 1. 诊断开关：与 gen-switches.py 完全相同的 getenv("XSYS4_*") 口径。
    getenv = re.compile(r'getenv\(\s*"(XSYS4_[A-Z0-9_]+)"')
    names = set()
    for dirpath, _, fns in os.walk(os.path.join(REPO, "src")):
        for fn in fns:
            if fn.endswith((".c", ".h")):
                names.update(getenv.findall(read(os.path.join(dirpath, fn))))
    m["switches"] = len(names)
    sm = re.search(r"共 \*\*(\d+)\*\* 个开关，其中 \*\*(\d+)\*\* 个没有就近注释",
                   read(SWITCHES))
    m["switches_doc"] = int(sm.group(1)) if sm else -1
    m["switches_undoc"] = int(sm.group(2)) if sm else -1

    # 2. HLL：源码宏口径（行首导出宏计数），与 AIN 声明口径不同，见 STATS.md。
    impl = todo = 0
    for p in glob.glob(os.path.join(REPO, "src", "hll", "*.c")):
        for line in read(p).splitlines():
            s = line.lstrip()
            if s.startswith("HLL_TODO_EXPORT("):
                todo += 1
            elif s.startswith(("HLL_EXPORT(", "HLL_EXPORT_N(")):
                impl += 1
    m["hll_impl"], m["hll_todo"] = impl, todo
    m["hll_total"] = impl + todo
    m["hll_pct"] = "%.1f" % (100.0 * impl / (impl + todo)) if impl + todo else "0.0"

    # 3. 假设台账：按 section 标题归类，只取表格行首，避免「改判链」重复计数。
    st = {"open": 0, "ok": 0, "no": 0, "other": 0}
    seen = set()
    for sec in re.split(r"^## ", read(HYP), flags=re.M)[1:]:
        lines = sec.splitlines()
        title = lines[0] if lines else ""
        for hid in re.findall(r"^\|\s*`(H\d{3})`", sec, flags=re.M):
            if hid in seen:
                continue
            seen.add(hid)
            if "待验" in title:
                st["open"] += 1
            elif "成立" in title:
                st["ok"] += 1
            elif "证伪" in title or "取代" in title:
                st["no"] += 1
            else:
                st["other"] += 1
    m["hyp_total"] = len(seen)
    m.update(hyp_open=st["open"], hyp_ok=st["ok"], hyp_no=st["no"], hyp_other=st["other"])

    # 4. 根因编号 / 未结债 / 影响面条目 / 子模块补丁。
    corpus = read(README) + read(TECH_DEBT) + read(HYP)
    m["roots"] = len(set(re.findall(r"\bD\d{2,3}\b", corpus)))
    m["debts"] = len(set(re.findall(r"^\|\s*`\[([A-Z0-9-]+)\]`", read(TECH_DEBT), flags=re.M)))
    m["impact"] = len(re.findall(r"^\|\s*`I\d{3}`", read(IMPACT), flags=re.M))
    m["patches"] = len(glob.glob(os.path.join(REPO, "patches", "*.patch")))

    # 5. 最新一次默认零开关基线。
    bl = sorted(glob.glob(os.path.join(PORT, "logs", "baseline_*.txt")),
                key=os.path.getmtime, reverse=True)
    kv = {}
    if bl:
        for line in read(bl[0]).splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                kv[k.strip()] = v.strip()
    for key, out in (("tag", "bl_tag"), ("activities", "bl_acts"), ("errors", "bl_err"),
                     ("core_dump", "bl_core"), ("scene_quest_map", "bl_map"), ("rc", "bl_rc")):
        m[out] = kv.get(key, "-")

    # 6. 文档体积（排除 README 与 STATS.md：前者含注入块、后者是自身，都会自指）。
    m["doc_lines"] = [
        ("TECH_DEBT.md", nlines(TECH_DEBT)),
        ("docs/HYPOTHESES.md", nlines(HYP)),
        ("docs/IMPACT.md", nlines(IMPACT)),
        ("docs/SWITCHES.md", nlines(SWITCHES)),
        ("docs/PROJECT-MANAGEMENT.md", nlines(PM)),
    ]
    return m


def stats_text(m):
    o = []
    o.append("# 项目数字（自动生成）")
    o.append("")
    o.append("> 由 `tools/gen-doc-numbers.py` 生成，**不要手改**。改代码 / 文档后重跑一次；")
    o.append("> 校验用 `tools/gen-doc-numbers.py --check`（腐化即返回非 0）。")
    o.append("> 对应《复杂底层项目的工程管理》§6 ⑪ / 缺口 6：手写数字会腐化。")
    o.append("")
    o.append("## 1. 唯一权威数字")
    o.append("")
    o.append("| 指标 | 值 | 口径 | 来源 |")
    o.append("|---|---|---|---|")
    rows = [
        ("诊断开关", m["switches"], '`getenv(\"XSYS4_*\")` 去重，与 `gen-switches.py` 同口径', "`src/**/*.c,*.h`"),
        ("未标注开关", m["switches_undoc"], "无人工说明且无就近注释", "`docs/SWITCHES.md`"),
        ("HLL 已实现", m["hll_impl"], "`HLL_EXPORT(` + `HLL_EXPORT_N(` 行首计数", "`src/hll/*.c`"),
        ("HLL 缺口", m["hll_todo"], "`HLL_TODO_EXPORT(` 行首计数", "`src/hll/*.c`"),
        ("HLL 源码覆盖率", f"{m['hll_pct']}% ({m['hll_total']} 个导出)", "已实现 /（已实现 + 缺口）", "计算"),
        ("假设台账", f"{m['hyp_total']}（待验 {m['hyp_open']} / 成立 {m['hyp_ok']} / 证伪 {m['hyp_no']}）", "表格行首 `H<编号>` 去重，按 section 归类", "`docs/HYPOTHESES.md`"),
        ("根因编号", m["roots"], "`D<2-3 位>` 去重", "README + TECH_DEBT + HYPOTHESES"),
        ("未结技术债", m["debts"], "表格行首 `` `[标签]` `` 去重", "`TECH_DEBT.md`"),
        ("影响面清单", m["impact"], "表格行首 `I<编号>`", "`docs/IMPACT.md`"),
        ("子模块补丁", m["patches"], "`patches/*.patch`", "`patches/`"),
        ("默认基线", f"`{m['bl_tag']}`: acts={m['bl_acts']} / err={m['bl_err']} / core={m['bl_core']} / quest_map={m['bl_map']} / rc={m['bl_rc']}", "最新 `baseline_*.txt`", "`logs/`"),
    ]
    for k, v, how, src in rows:
        o.append(f"| {k} | {v} | {how} | {src} |")
    o.append("")
    o.append("> **HLL 口径警告**：本表是**源码宏口径**；`PROJECT-MANAGEMENT.md` 里的")
    o.append("> 「2,151 / 3,351（64%）」是 **AIN 声明口径**（另一天的快照），两者不可混用。")
    o.append("> 对外汇报只引用本表，且注明生成日期口径。")
    o.append("")
    o.append("## 2. 文档体积（不含 README 与 STATS.md，避免自指）")
    o.append("")
    o.append("| 文档 | 行数 |")
    o.append("|---|---|")
    for name, n in m["doc_lines"]:
        o.append(f"| `{name}` | {n} |")
    o.append("")
    o.append("## 3. 已知腐化数字（历史，勿再引用）")
    o.append("")
    o.append("| 出处 | 曾手写 | 性质 |")
    o.append("|---|---|---|")
    o.append("| `README.md` §4 | 96 个开关 | 已由本脚本注入块取代 |")
    o.append("| `SWITCHES.md` 历史快照 | 116 / 136 个开关 | 生成前快照；`--check` 会抓过期 |")
    o.append("| `PROJECT-MANAGEMENT.md` §1 | 2,151 / 3,351（64%） | AIN 声明口径，且与 §9 不一致 |")
    o.append("| `PROJECT-MANAGEMENT.md` §9 | 2,123 / 3,323 | 同书另一处，自相矛盾 |")
    o.append("")
    o.append("> 复盘文档保留历史数字是合理的（它记录当时状态），但**状态文档与汇报禁止再引用**。")
    return "\n".join(o) + "\n"


def inject_text(m):
    return "\n".join([
        BEGIN,
        "**自动生成数字**（禁止手写；完整口径见 `docs/STATS.md`）："
        f"诊断开关 **{m['switches']}**（未标注 {m['switches_undoc']}）｜ "
        f"HLL 源码口径 **{m['hll_impl']} / {m['hll_total']}（{m['hll_pct']}%）** ｜ "
        f"假设台账 / 根因编号 / 未结债 **{m['hyp_total']} / {m['roots']} / {m['debts']}** ｜ "
        f"影响面 / 子模块补丁 **{m['impact']} / {m['patches']}** ｜ "
        f"默认基线 `{m['bl_tag']}`: `acts={m['bl_acts']} / err={m['bl_err']} / "
        f"core={m['bl_core']} / quest_map={m['bl_map']}`。",
        END,
    ])


def main():
    # 单独克隆 xsystem4 时没有工作区级状态文档：跳过而不让门禁崩溃。
    if not os.path.exists(README) or not os.path.exists(TECH_DEBT):
        print("doc numbers: SKIP (工作区状态文档不可用，单独克隆 xsystem4 时正常)")
        return 0
    m = collect()
    st, inj = stats_text(m), inject_text(m)
    check = "--check" in sys.argv

    if check:
        ok = True
        if not os.path.exists(STATS):
            print("STALE  docs/STATS.md 不存在 —— 跑 tools/gen-doc-numbers.py")
            ok = False
        elif read(STATS) != st:
            print("STALE  docs/STATS.md 与当前源码 / 台账不符 —— 跑 tools/gen-doc-numbers.py")
            ok = False
        r = read(README)
        if BEGIN not in r or END not in r:
            print("STALE  README.md 缺少 STATS 注入块 —— 跑 tools/gen-doc-numbers.py")
            ok = False
        elif r[r.index(BEGIN):r.index(END) + len(END)] != inj:
            print("STALE  README.md 注入块数字腐化 —— 跑 tools/gen-doc-numbers.py")
            ok = False
        if m["switches"] != m["switches_doc"]:
            print(f"STALE  docs/SWITCHES.md 声明 {m['switches_doc']} 个开关，"
                  f"源码实为 {m['switches']} 个 —— 跑 tools/gen-switches.py")
            ok = False
        print(f"doc numbers: {'PASS' if ok else 'FAIL'} "
              f"(switches={m['switches']} hll={m['hll_impl']}/{m['hll_total']} "
              f"hyp={m['hyp_total']} roots={m['roots']} debts={m['debts']})")
        return 0 if ok else 1

    with open(STATS, "w", encoding="utf-8") as f:
        f.write(st)

    r = read(README)
    if BEGIN in r and END in r:
        r = r[:r.index(BEGIN)] + inj + r[r.index(END) + len(END):]
    elif ANCHOR in r:
        r = r.replace(ANCHOR, inj + "\n")
    else:
        print("ERROR: 找不到注入锚点，且 README 里没有 STATS 块", file=sys.stderr)
        return 1
    with open(README, "w", encoding="utf-8") as f:
        f.write(r)

    print(f"wrote {STATS}")
    print(f"injected README STATS block (switches={m['switches']}, "
          f"hll={m['hll_impl']}/{m['hll_total']}, hyp={m['hyp_total']}, roots={m['roots']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
