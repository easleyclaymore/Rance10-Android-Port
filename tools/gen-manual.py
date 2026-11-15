#!/usr/bin/env python3
"""把官方手册 game/Manual/*.html 转成一份可检索的 Markdown。

权威源是原 HTML + img/（官方中文说明书，有版权、不入库）；本脚本只做机械转换，
**不做 OCR**。生成物 game/Manual/MANUAL.md 与原文同目录，也不入库，仅供本地检索。

用法：
    tools/gen-manual.py            # 生成 MANUAL.md
    tools/gen-manual.py --check    # 源变了就提示重跑，返回非 0

为什么不能把转出来的 md 当成「完整手册」：手册里相当一部分关键信息只在图片里
（r01 的整张按键表是 img/rm01.gif，各界面基准是 rm*.jpg / pm*.jpg）。本脚本只
保留图片相对链接，图内文字没有提取 —— 需要精确信息时回看原图，
清单见 docs/MANUAL-INDEX.md。
"""
import os
import re
import sys
from html.parser import HTMLParser

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
PORT = os.path.dirname(REPO)
MANUAL = os.path.join(PORT, "game", "Manual")
OUT = os.path.join(MANUAL, "MANUAL.md")

# 章节顺序取自 index.html 的导航，固定下来，避免目录随文件系统排序漂移。
PAGES = [
    ("index.html", "封面"),
    ("startup.html", "新手教程"),
    ("01.html", "指南 01 游戏简介"),
    ("02.html", "指南 02 ＳＡＶＥ与ＬＯＡＤ"),
    ("03.html", "指南 03 基本玩法"),
    ("04.html", "指南 04 游戏流程"),
    ("05.html", "指南 05 图鉴与帮助按钮"),
    ("06.html", "指南 06 角色与物品"),
    ("07.html", "指南 07 部队数据"),
    ("08.html", "指南 08 准备＆作战环节"),
    ("09.html", "指南 09 任务"),
    ("10.html", "指南 10 战斗"),
    ("11.html", "指南 11 挑战模式与联合部队"),
    ("12.html", "指南 12 探险小窍门"),
    ("r01.html", "参考 01 基本操作"),
    ("r02.html", "参考 02 启动界面"),
    ("r03.html", "参考 03 ＭＥＮＵ"),
    ("r04.html", "参考 04 收藏界面"),
    ("r05.html", "参考 05 ＬＯＡＤ界面"),
    ("r06.html", "参考 06 事件・剧情界面"),
    ("r07.html", "参考 07 回合开场界面"),
    ("r08.html", "参考 08 准备作战界面"),
    ("r09.html", "参考 09 任务界面"),
    ("r10.html", "参考 10 部队编制界面"),
    ("r11.html", "参考 11 战斗开始界面"),
    ("r12.html", "参考 12 战斗界面"),
    ("r13.html", "参考 13 餐券界面"),
    ("r14.html", "参考 14 设置"),
    ("staff.html", "开发人员"),
    ("pub.html", "版权"),
]

HEADER = """# 兰斯10 官方游戏指南（本地生成物）

> **由 `tools/gen-manual.py` 从 `game/Manual/*.html` 机械转换，不要手改。**
>
> - **权威源 = 原 HTML + `img/`**（官方中文说明书，有版权，**不入库**）。
>   本文件只是派生索引，不随 git 提交；原文有歧义时一律回看 HTML / 原图。
> - **图片内文字未 OCR**：完整按键表（`img/rm01.gif`）、各界面基准图
>   （`img/rm*.jpg`、`img/pm*.jpg`）只保留相对链接，必须回看原图。
>   清单见 `docs/MANUAL-INDEX.md` 的「图片陷阱」。
> - 重跑 `tools/gen-manual.py`；校验 `tools/gen-manual.py --check`。
> - 编码 UTF-8；导航菜单已剥离，章节 / 表格 / 图片链接保留。

## 目录

"""


class MarkdownConverter(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.out = []
        self.skip = 0
        self.in_table = False
        self.rows = []
        self.row = None
        self.cell = None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag in ("script", "style", "head"):
            self.skip += 1
            return
        if self.skip:
            return
        if tag == "h3":
            self.out.append("\n\n### ")
        elif tag in ("h1", "h2", "h4"):
            self.out.append("\n\n## ")
        elif tag == "p":
            self.out.append("\n\n")
        elif tag == "br":
            self.out.append("\n")
        elif tag == "table":
            self.in_table = True
            self.rows = []
        elif tag == "tr" and self.in_table:
            self.row = []
        elif tag in ("td", "th") and self.in_table and self.row is not None:
            self.cell = []
        elif tag == "img":
            src = a.get("src", "")
            alt = a.get("alt", "").replace("|", "\\|")
            self.out.append("\n\n![%s](%s)\n\n" % (alt, src))
        # a / div / span 等容器只保留内部文字

    def handle_endtag(self, tag):
        if tag in ("script", "style", "head"):
            self.skip = max(0, self.skip - 1)
            return
        if self.skip:
            return
        if tag in ("h1", "h2", "h3", "h4", "p"):
            self.out.append("\n\n")
        elif tag in ("td", "th") and self.cell is not None:
            txt = re.sub(r"\s+", " ", "".join(self.cell)).strip()
            (self.row if self.row is not None else []).append(txt.replace("|", "\\|"))
            self.cell = None
        elif tag == "tr" and self.row is not None:
            self.rows.append(self.row)
            self.row = None
        elif tag == "table" and self.in_table:
            self.out.append(self._table())
            self.in_table = False

    def handle_data(self, data):
        if self.skip:
            return
        if self.cell is not None:
            self.cell.append(data)
        elif not self.in_table:
            self.out.append(data)

    def _table(self):
        if not self.rows:
            return "\n\n"
        n = max(len(r) for r in self.rows)
        pad = lambda r: (r + [""] * n)[:n]
        lines = ["| " + " | ".join(pad(self.rows[0])) + " |", "|" + "---|" * n]
        for r in self.rows[1:]:
            lines.append("| " + " | ".join(pad(r)) + " |")
        return "\n\n" + "\n".join(lines) + "\n\n"

    def text(self):
        s = "".join(self.out)
        s = re.sub(r"[ \t]+\n", "\n", s)
        s = re.sub(r"\n{3,}", "\n\n", s)
        return s.strip() + "\n"


def body_of(src):
    """剥掉 head 与 div#menu 导航，只留 div.main 正文。"""
    s = re.sub(r"<head.*?</head>", "", src, flags=re.S | re.I)
    m = re.search(r'<div[^>]*class="main"[^>]*>', s, re.I)
    if m:
        return s[m.end():]
    m2 = re.search(r"<body[^>]*>(.*)", s, re.S | re.I)
    b = m2.group(1) if m2 else s
    return re.sub(r'<div id="menu">.*?</div>\s*</div>', "", b, flags=re.S | re.I)


def build():
    parts = [HEADER]
    for fn, title in PAGES:
        parts.append("- [%s](%s)\n" % (title, fn))
    for fn, title in PAGES:
        path = os.path.join(MANUAL, fn)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            src = f.read()
        conv = MarkdownConverter()
        conv.feed(body_of(src))
        parts.append("\n---\n\n## %s\n\n> 源文件：`%s`\n\n" % (title, fn))
        parts.append(conv.text())
    return "".join(parts)


def main():
    if not os.path.isdir(MANUAL):
        print("gen-manual: SKIP (官方手册目录不可用，单独 clone 本仓库时正常)")
        return 0
    text = build()
    if "--check" in sys.argv:
        if not os.path.exists(OUT):
            print("gen-manual: SKIP (生成物尚未生成；本地派生文件，不阻塞门禁)")
            return 0
        with open(OUT, encoding="utf-8") as f:
            cur = f.read()
        if cur != text:
            print("STALE  game/Manual/MANUAL.md 与源 HTML 不符 —— 跑 tools/gen-manual.py")
            return 1
        print("gen-manual: PASS (%d 页, %d 字符)" % (len(PAGES), len(text)))
        return 0
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print("wrote %s (%d 页, %d 字符)" % (OUT, len(PAGES), len(text)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
