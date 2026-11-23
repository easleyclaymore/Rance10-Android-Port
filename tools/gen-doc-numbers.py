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
# [D273] 状态层文档从工作区根迁进仓库。
# 起因：根目录不是 git 仓库，README/TECH_DEBT 是项目最重要的状态文档却
# 完全没有版本控制 —— 改了无法提交、也没法回滚。而 xsystem4/README.md
# 已被 upstream fork 指针页占用，所以状态文档叫 docs/STATUS.md（职责是
# 「现状 + 契约 + 卡点 + 纪律」，名字比 README 更准确）。
README = os.path.join(DOCS, "STATUS.md")
TECH_DEBT = os.path.join(DOCS, "TECH_DEBT.md")
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
    # 导出宏共 4 个，必须全数进来：只数不带类型标注的 HLL_EXPORT/HLL_EXPORT_N
    # 会把 D284 引入的 HLL_EXPORT_RT/HLL_EXPORT_NRT（Math.c 的 6 组 int/float
    # 重载）漏掉 16 条 —— 数字变小、百分比变低，而 --check 仍然自洽通过。
    # 加新导出宏时**必须**同步改这里和 docs/STATS.md 的口径说明。
    HLL_IMPL_MACROS = (
        "HLL_EXPORT(", "HLL_EXPORT_N(",
        "HLL_EXPORT_RT(", "HLL_EXPORT_NRT(",
    )
    impl = todo = 0
    for p in glob.glob(os.path.join(REPO, "src", "hll", "*.c")):
        for line in read(p).splitlines():
            s = line.lstrip()
            if s.startswith("HLL_TODO_EXPORT("):
                todo += 1
            elif s.startswith(HLL_IMPL_MACROS):
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
    # [D501] 仓库内 docs/baseline/ 优先（干净 clone / CI 没有 PORT/logs，
    # 旧逻辑在那里永远算成 `-`，与提交进仓库的 STATS 对不上 ⇒ 门禁假红）。
    # PORT/logs 的本地跑测基线仍是回退来源。
    bl = sorted(glob.glob(os.path.join(DOCS, "baseline", "baseline_*.txt")),
                key=os.path.getmtime, reverse=True)
    if not bl:
        bl = sorted(glob.glob(os.path.join(PORT, "logs", "baseline_*.txt")),
                    key=os.path.getmtime, reverse=True)
    kv = {}
    if bl:
        for line in read(bl[0]).splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                kv[k.strip()] = v.strip()
    for key, out in (("tag", "bl_tag"), ("mode", "bl_mode"), ("activities", "bl_acts"), ("errors", "bl_err"),
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
        ("HLL 已实现", m["hll_impl"], "4 个导出宏行首计数（含 RT/NRT 重载）", "`src/hll/*.c`"),
        ("HLL 缺口", m["hll_todo"], "`HLL_TODO_EXPORT(` 行首计数", "`src/hll/*.c`"),
        ("HLL 源码覆盖率", f"{m['hll_pct']}% ({m['hll_total']} 个导出)", "已实现 /（已实现 + 缺口）", "计算"),
        ("假设台账", f"{m['hyp_total']}（待验 {m['hyp_open']} / 成立 {m['hyp_ok']} / 证伪 {m['hyp_no']}）", "表格行首 `H<编号>` 去重，按 section 归类", "`docs/HYPOTHESES.md`"),
        ("根因编号", m["roots"], "`D<2-3 位>` 去重", "README + TECH_DEBT + HYPOTHESES"),
        ("未结技术债", m["debts"], "表格行首 `` `[标签]` `` 去重", "`TECH_DEBT.md`"),
        ("影响面清单", m["impact"], "表格行首 `I<编号>`", "`docs/IMPACT.md`"),
        ("子模块补丁", m["patches"], "`patches/*.patch`", "`patches/`"),
        ("默认基线", f"`{m['bl_tag']}`（{m['bl_mode']} 档）: acts={m['bl_acts']} / err={m['bl_err']} / core={m['bl_core']} / quest_map={m['bl_map']} / rc={m['bl_rc']}", "最新 `baseline_*.txt`（档位决定断言面：quick 不到任务地图）", "`docs/baseline/`（本地回退 logs/）"),
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


def check_stale_workspace_copies():
    """[D273] 工作区根不许再留状态文档的旧副本。

    迁移时最容易发生的事：有人（或某个旧脚本）又在根目录写了一份
    README/TECH_DEBT，然后两份分叉。判据不是"文件存不存在"，而是
    "它有没有变小/变回状态文档" —— 指针页是几百字节且必须含指向
    docs/ 的链接；超过这个体量说明旧的正文又回来了。
    """
    problems = []
    for name, real in (("README.md", "docs/STATUS.md"),
                       ("TECH_DEBT.md", "docs/TECH_DEBT.md")):
        p = os.path.join(WS, name)
        if not os.path.exists(p):
            continue
        txt = read(p)
        if real not in txt:
            problems.append(f"工作区根 {name} 没有指向 {real} 的链接"
                            "（状态文档应只在仓库内，根目录只放指针页）")
        elif txt.count("\n") > 40:
            problems.append(f"工作区根 {name} 有 {txt.count(chr(10))} 行，"
                            f"像是状态文档正文又回来了 —— 只应是指针页")
    return problems


def check_agent_context_file():
    """[D291] 工作区根必须有 AGENTS.md，且它必须指向状态文档。

    为什么需要这条：Pi（以及同类 agent harness）只把 `AGENTS.md`/`CLAUDE.md`
    注入**系统提示**；`docs/STATUS.md` 是普通文件，读过才进对话，一压缩
    上下文就被摘要掉。项目曾多次出现「压缩后忘了核心目标是移植兰斯10」。

    两条判据：①**存在**且指向 docs/STATUS.md（否则等于没接线）；
    ②**不许变成状态文档副本** —— 卡点的唯一定义在 STATUS §0，AGENTS.md
    里写一份拷贝必然过期，而照着过期卡点干活是 D264–D269 的原始病因。
    """
    # [D501] 优先查**仓库内**的 AGENTS.md：干净 clone（CI / 其他开发者）
    # 没有工作区根，旧判据在那里必然 FAIL —— 门禁依赖仓库外文件是设计错误。
    # 工作区根那份是 agent harness 注入用的部署副本，有就顺带检查一致性。
    p = os.path.join(REPO, "AGENTS.md")
    if not os.path.exists(p):
        p = os.path.join(WS, "AGENTS.md")
    if not os.path.exists(p):
        return ["缺少 AGENTS.md（仓库根与工作区根都没有）—— 压缩上下文后没有任何东西"
                "保证模型知道核心目标是移植兰斯10（见 docs/STATUS.md 纪律 7 同源问题）"]
    txt = read(p)
    problems = []
    if "docs/STATUS.md" not in txt:
        problems.append("工作区根 AGENTS.md 没有指向 docs/STATUS.md"
                        "（它只该是指针 + 纪律，状态不许复制）")
    if txt.count("\n") > 120:
        problems.append(f"工作区根 AGENTS.md 有 {txt.count(chr(10))} 行 —— "
                        "像状态文档正文又回来了，只应是指针页")
    return problems


def check_status_debt_tag():
    """[D274] STATUS §0「最大卡点」必须挂一个**真实存在**的未结债标签。

    为什么需要这条：卡点那一格是自由文本，最容易和台账脱钩。真实发生过 ——
    STATUS 声称「战斗下半屏底板不渲染」，而台账里同一条债早已被 D264–D269
    证伪换成别的因果，后续会话照着过期卡点白跑好几轮。数字检查（debts=29）
    完全看不出来，因为债务总数没变，只是「哪一条」变了。

    判据：§0 的「最大卡点」行里至少有一个 `[TAG]`，且该 TAG 逐字出现在
    TECH_DEBT 的未结区（A/B/C 组之间）。已清零区里的标签不算 —— 否则
    「卡点指向一条已清的债」这种最该抓的情况反而会被放过。
    """
    import re
    problems = []
    if not (os.path.exists(README) and os.path.exists(TECH_DEBT)):
        return problems

    st = read(README)
    m = re.search(r"^\| \*\*最大卡点\*\* \|(.+)$", st, re.M)
    if not m:
        problems.append("STATUS §0 找不到「最大卡点」行（格式应为 | **最大卡点** | ... |）")
        return problems
    cell = m.group(1)
    tags = re.findall(r"`(\[[^\]]+\])`", cell)
    if not tags:
        problems.append("STATUS §0「最大卡点」没有挂 `[TAG]` —— "
                        "必须指向 docs/TECH_DEBT.md 未结区里的一条债")
        return problems

    lines = read(TECH_DEBT).splitlines()
    try:
        a = next(i for i, l in enumerate(lines) if l.startswith("## A."))
        c = next(i for i, l in enumerate(lines) if l.startswith("## 已清零"))
    except StopIteration:
        return problems   # 台账结构异常由 check_doc_structure 负责报
    open_tags = set()
    for n in range(a, c):
        mm = re.match(r"\| `(\[[^\]]+\])`", lines[n])
        if mm:
            open_tags.add(mm.group(1))

    # 已清零区里的标签单独收集，用于给出更准确的报错
    cleared = set()
    for n in range(c, len(lines)):
        mm = re.match(r"\| `(\[[^\]]+\])`", lines[n])
        if mm:
            cleared.add(mm.group(1))

    hit = [t for t in tags if t in open_tags]
    if not hit:
        for t in tags:
            if t in cleared:
                problems.append(f"STATUS §0「最大卡点」挂在 {t}，"
                                "但它在 TECH_DEBT 的**已清零**区 —— 卡点已过期，"
                                "先改台账再改这一行")
            else:
                problems.append(f"STATUS §0「最大卡点」挂在 {t}，"
                                "但 TECH_DEBT 未结区没有这个标签（拼写不符或已删）")
    return problems


def check_doc_coverage():
    """[D274] 文档地图必须**显式声明**每一个 .md 的去向。

    为什么需要：`notes/` 一直躺在仓库里（1413 行的 parts-engine.md），
    文档地图却一个字没提。它其实该「不纳管」，但**「未声明」和「声明为不纳管」
    是两回事** —— 前者让人以为漏了，后者是明确决定。game_compatibility.md
    就做对了（明确标「上游自带、不纳管」）。

    判据：除了生成物目录（docs/MANUAL、subprojects、build）之外，
    每个 .md 的路径都必须能在 STATUS §6 文档地图里找到（整段或作为目录前缀）。
    新增文档时要么进地图、要么进「上游自带/不纳管」那一行。
    """
    import re
    problems = []
    if not os.path.exists(README):
        return problems
    st = read(README)
    sec = re.search(r"^## 6\. 文档地图.*?(?=^## |\Z)", st, re.S | re.M)
    if not sec:
        return problems   # 缺 §6 本身由 check_doc_structure 之外的渠道发现
    map_text = sec.group(0)

    SKIP_DIRS = ("subprojects/", "build/", "docs/MANUAL/", ".git/")
    found = []
    for root, dirs, files in os.walk(REPO):
        rel = os.path.relpath(root, REPO)
        rel = "" if rel == "." else rel.replace(os.sep, "/") + "/"
        if any(rel.startswith(d) for d in SKIP_DIRS):
            dirs[:] = []
            continue
        for f in files:
            if not f.endswith(".md"):
                continue
            path = rel + f
            found.append(path)

    for path in sorted(found):
        if path in map_text:
            continue
        # 「声明整个目录」不能只看前缀 —— `docs/` 是个前缀，于是
        # `docs/随便新建.md` 会被静默放过（自测时正是这么漏的）。
        # 只承认**地图里真的写了**以 `/` 结尾的具体目录，
        # 且必须是该文件路径的前缀（逐层，不含最泛的那一层）。
        parents = ["/".join(path.split("/")[:i]) + "/"
                   for i in range(1, len(path.split("/")))]
        # 去掉过于宽泛的顶层目录（docs/、tools/ 这类不该自动吸纳新文件）
        TOP = {"docs/", "tools/", "src/", "include/", "test/"}
        if any(d in map_text and d not in TOP for d in parents):
            continue
        problems.append(f"文档地图未声明 {path} —— "
                        "要么加进 STATUS §6，要么归入「上游自带、不纳管」那一行")
    return problems


def check_doc_structure():
    """[D272] 结构腐化检查：数字对了不等于文档没烂。

    这三类是实际发生过的，光靠 num/pct 比对查不出来：
      1. README §3 契约编号重复（14/15 各出现两次），下游
         TECH_DEBT 用「契约 N」交叉引用 → 一插新条目全错位。
      2. TECH_DEBT 出现**没有表头的孤儿表格行** —— markdown 渲染后
         整行不可见，等于一条债凭空蒸发。
      3. 已结项（已清零/已结案/已修）留在「未结」区 → 读的人以为还要做。
    返回问题列表，空表示通过。
    """
    import re
    problems = []
    if not (os.path.exists(README) and os.path.exists(TECH_DEBT)):
        return problems

    # --- 1. README §3 契约编号：连续、不重复
    r = read(README)
    m = re.search(r"^## 3\..*?$(.*?)^## 4\.", r, re.S | re.M)
    if m:
        nums = [int(x) for x in re.findall(r"^(\d+)\.\s", m.group(1), re.M)]
        dup = sorted({n for n in nums if nums.count(n) > 1})
        if dup:
            problems.append(f"README §3 契约编号重复: {dup}")
        if nums and nums != list(range(1, len(nums) + 1)):
            problems.append(f"README §3 契约编号不连续: {nums}")
    else:
        problems.append("README 找不到 §3 必读契约")

    # --- 2. TECH_DEBT 孤儿表格行
    lines = read(TECH_DEBT).splitlines()
    covered = set()
    i = 0
    while i < len(lines):
        if (lines[i].startswith("|") and i + 1 < len(lines)
                and re.match(r"^\|[\s:\-\|]+\|$", lines[i + 1].strip())):
            j = i + 2
            while j < len(lines) and lines[j].startswith("|"):
                j += 1
            covered.update(range(i + 1, j + 1))
            i = j
        else:
            i += 1
    orphans = [n for n in range(1, len(lines) + 1)
               if lines[n - 1].startswith("|") and n not in covered]
    if orphans:
        problems.append(f"TECH_DEBT 孤儿表格行（无表头，渲染后不可见）: L{orphans}")

    # --- 3. 已结项留在未结区
    try:
        a = next(i for i, l in enumerate(lines) if l.startswith("## A."))
        c = next(i for i, l in enumerate(lines) if l.startswith("## 已清零"))
    except StopIteration:
        c = None
    if c is not None:
        # 只看**状态列**（标签后第二格）。整行搜"已修/已证伪"会大量误报：
        # [PROBE-GATED] 是"D238 已修这一处"（还剩其余变体）、
        # [BATTLE-STALL] 是"初判…已证伪"（债本身仍开着）、
        # [BATTLE-UI-BLANK] 是"已证伪的三条旁路"（旁路死了，债没死）。
        # 判据必须落在状态格，不能落在现象描述里。
        done = re.compile(r"已清零|已结案|已清偿")
        stuck = []
        for n in range(a, c):
            ln = lines[n]
            if not ln.startswith("| `["):
                continue
            cells = [x.strip() for x in ln.strip().strip("|").split("|")]
            status = cells[1] if len(cells) > 1 else ""
            if done.search(status):
                mm = re.match(r"\| `(\[[^\]]+\])`", ln)
                stuck.append(mm.group(1) if mm else ln[:20])
        if stuck:
            problems.append(f"TECH_DEBT 未结区里有已结项: {stuck}")
    return problems


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
        for p in (check_stale_workspace_copies()
                  + check_agent_context_file()
                  + check_status_debt_tag()
                  + check_doc_coverage()
                  + check_doc_structure()):
            print("STALE  " + p)
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
