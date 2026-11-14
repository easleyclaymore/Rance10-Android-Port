#!/usr/bin/env python3
"""生成 docs/SWITCHES.md：所有 XSYS4_* 环境变量的清单。

为什么要有这个：引擎里有一百多个诊断开关，分散在 src/ 各处，
没有任何一处能回答"XSYS4_DG_LAZY 是干什么的"。这个脚本把它们扫出来，
按类别分组，并把"附近有没有注释"如实标出来 —— 没注释的就是技术债，
数量会随时间下降。

用法：
    tools/gen-switches.py            # 写到 docs/SWITCHES.md
    tools/gen-switches.py --check    # 只检查，有未标注开关就返回非 0

判据：`XSYS4_*` 开关名必须能在一行内说明用途；做不到的说明还没想清楚。
"""
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
OUT = os.path.join(ROOT, "docs", "SWITCHES.md")

GETENV = re.compile(r'getenv\(\s*"(XSYS4_[A-Z0-9_]+)"')
VARNAME = re.compile(r'"(XSYS4_[A-Z0-9_]+)"')

# 按用途分组：前缀 -> (分组名, 说明)
GROUPS = [
    ("XSYS4_DUMP_AT", "断点与快照", "在指定指令地址停下来，打印 this / 局部 / 栈"),
    ("XSYS4_CALL_TRACE", "断点与快照", "追踪指定 AIN 函数的调用点与返回地址"),
    ("XSYS4_HLL_TRACE", "断点与快照", "追踪 HLL 调用（可按库/函数收窄）"),
    ("XSYS4_FN_DUMP", "断点与快照", "函数统计与慢函数排行"),
    ("XSYS4_CENSUS", "断点与快照", "函数调用普查"),
    ("XSYS4_ARR", "容器与内存", "Array 相关诊断"),
    ("XSYS4_ARRAY", "容器与内存", "Array 相关诊断"),
    ("XSYS4_DG", "容器与内存", "委托（Delegate）相关诊断"),
    ("XSYS4_DF", "容器与内存", "委托页释放相关诊断"),
    ("XSYS4_MEM", "容器与内存", "堆内存与 RSS 监控"),
    ("XSYS4_ALLOC0", "容器与内存", "0 号槽分配诊断"),
    ("XSYS4_CTOR", "容器与内存", "构造函数调用诊断"),
    ("XSYS4_BADSTRUCT", "容器与内存", "非法结构体访问诊断"),
    ("XSYS4_BADTHIS", "容器与内存", "非法 this 诊断"),
    ("XSYS4_PARTS", "呈现与输入", "部件快照与渲染诊断"),
    ("XSYS4_CTRL", "呈现与输入", "controller 栈与视觉地板诊断"),
    ("XSYS4_ACT", "呈现与输入", "活动（Activity）装载与点击诊断"),
    ("XSYS4_INPUT", "呈现与输入", "输入命中诊断"),
    ("XSYS4_CLICK", "呈现与输入", "点击重定向诊断"),
    ("XSYS4_UPD", "呈现与输入", "部件更新诊断"),
    ("XSYS4_AUTO_", "自动化后门", "无头跑测用的自动推进后门"),
    ("XSYS4_NO_LAYER_COVER", "自动化后门", "关闭「视觉地板」优化"),
    ("XSYS4_DATA_DIR", "运行环境", "数据目录"),
    ("XSYS4_SOFT_STUB", "运行环境", "未实现导出用零桩顶替（诊断用）"),
    ("XSYS4_DBG_", "一次性探针", "临时调试探针（稳定后应删除）"),
]


def group_of(name):
    for prefix, group, _ in GROUPS:
        if name.startswith(prefix):
            return group
    return "其它"


def load_manual():
    """人工维护的「开关 -> 一句话用途」。

    生成器只能发现开关在哪，说不出它是干什么的 —— 就近注释抓到的往往是
    背景说明的残句（实测把 "ever run?" 当成 CALL_TRACE 的用途）。所以
    语义由这张表负责，机器只负责找漏。格式：`名字<TAB>说明`，`#` 开头是注释。
    """
    table = {}
    path = os.path.join(ROOT, "tools", "switch-desc.txt")
    if not os.path.exists(path):
        return table
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t", 1)
            if len(parts) == 2:
                table[parts[0].strip()] = parts[1].strip()
    return table


def nearby_comment(lines, idx):
    """只看紧邻的上一两行。宁可标 `—` 也不给误导性说明。"""
    for k in (idx - 1, idx - 2):
        if k < 0:
            break
        raw = lines[k].strip()
        m = re.match(r"^(?://+|\*|/\*+)\s*(.*?)\s*(?:\*/)?$", raw)
        if not m:
            continue
        text = m.group(1).strip("* \t")
        if not text or text.startswith("["):
            continue   # "[D164] ..." 是根因编号，不是用途说明
        if len(text) > 70:
            text = text[:67] + "…"
        return text
    return ""


def main():
    check_only = "--check" in sys.argv
    found = {}          # name -> list of (relpath, lineno)
    for dirpath, _dirnames, filenames in os.walk(SRC):
        for fn in filenames:
            if not fn.endswith((".c", ".h")):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, ROOT)
            try:
                with open(path, encoding="utf-8", errors="replace") as f:
                    lines = f.readlines()
            except OSError:
                continue
            for i, line in enumerate(lines):
                for m in GETENV.finditer(line):
                    found.setdefault(m.group(1), []).append((rel, i + 1, lines))

    manual = load_manual()
    rows = []
    undocumented = 0
    for name in sorted(found):
        where = found[name][0]
        comment = manual.get(name) or nearby_comment(where[2], where[1] - 1)
        if not comment:
            undocumented += 1
        rows.append((name, group_of(name), "%s:%d" % (where[0], where[1]), comment))

    if check_only:
        print("XSYS4 switches: %d total, %d undocumented" % (len(rows), undocumented))
        return 0 if undocumented == 0 else 1

    by_group = defaultdict(list)
    for r in rows:
        by_group[r[1]].append(r)

    order = []
    for _, group, _ in GROUPS:
        if group not in order:
            order.append(group)
    for group in by_group:
        if group not in order:
            order.append(group)

    out = []
    out.append("# 诊断开关总表\n")
    out.append("> 由 `tools/gen-switches.py` 生成，**不要手改**。改代码后重跑一次。\n")
    out.append("所有开关默认都是**关闭**的；只有显式设置才生效。")
    out.append("新增开关必须带一行用途说明，否则本表会显示 `—`，")
    out.append("它同时是一个可量化的债：`tools/gen-switches.py --check` 的返回值就是未标注数量。\n")
    out.append("共 **%d** 个开关，其中 **%d** 个没有就近注释。\n" % (len(rows), undocumented))
    out.append("| 分组 | 开关 | 位置 | 用途 |")
    out.append("|---|---|---|---|")
    for group in order:
        for name, _g, loc, comment in sorted(by_group.get(group, [])):
            out.append("| %s | `%s` | `%s` | %s |" % (group, name, loc, comment or "—"))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d switches, %d undocumented)" % (OUT, len(rows), undocumented))
    return 0


if __name__ == "__main__":
    sys.exit(main())
