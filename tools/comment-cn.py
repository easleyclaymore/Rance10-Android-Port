#!/usr/bin/env python3
"""注释中文化工作流（仅供本仓库使用的小工具）。

用法：
    python3 tools/comment-cn.py list  <file> [--skip-head 0]
        列出该文件里"还是英文"的注释行（行号 + 原文），方便逐行翻译。

    python3 tools/comment-cn.py apply <file> <tsv>
        按 TSV（第一列行号、第二列整行新内容，TAB 分隔）替换。
        只允许替换"看起来是注释"的行：替换前后的行首标记（// 或 *）必须一致，
        避免把代码或字符串误当成注释改掉。

    python3 tools/comment-cn.py stats
        全仓统计还有多少英文注释行（排除第三方库）。

术语表（本仓库统一口径，2026-11-15 用户要求"术语也换成中文"）：
     page→页   heap→堆   slot→槽   delegate→委托   interface/iface→接口
     vtable→虚表   parts→部件   sprite→精灵   texture→纹理   scene→场景
     struct→结构体   frame→帧   layer→图层   opcode→操作码   stack→栈
     保留原文的：AIN / VM / HLL / EX / FLAT（文件格式名）/ 函数名 / 类型名 /
     日志 tag / D 编号。代码形式的引用（`struct parts`、heap[0].s）不改。

为什么用"行号 + 整行替换"而不是正则：注释里全是标点和括号，正则很容易
改到代码或字符串字面量。行号替换只动指定行，且 apply 时会校验行首标记。
"""
import os
import re
import sys

# 第三方代码：不翻译（许可证原文 + 上游库，翻了既没学习价值又会和上游分叉）
SKIP_FILES = {
    'include/pl_mpeg.h',      # MPEG1 解码器（第三方）
    'include/queue.h',        # BSD sys/queue.h
    'include/cJSON.h',        # cJSON
    'src/cJSON.c',            # cJSON
    'include/sts_mixer.h',    # 上游混音器接口
    'src/dungeon/generator.c',
    'include/dungeon/dgn.h',
    'include/dungeon/dungeon.h',
    'src/3d/s3de.c',
}
SKIP_DIRS = ('subprojects/', 'build/', '.git/')

CJK = re.compile(r'[\u3000-\u303f\u4e00-\u9fff\uff00-\uffef]')
LATIN = re.compile(r'[A-Za-z]{3,}')


BLOCK_STAR = re.compile(r'^[\t ]*(?:\*(?:[\t ]|/|$)|/)')

def is_comment(line):
    st = line.strip()
    if st.startswith('//') or st.startswith('/*'):
        return True
    # 行首的 `*` 只有在 `* ` / `*/` / 单独一个 `*` 时才是块注释续行；
    # 否则 `*obj = -1;` 这种解引用代码会被误当成注释（曾经误计 1800 行）。
    return bool(BLOCK_STAR.match(line))


def marker(line):
    st = line.lstrip()
    for m in ('//', '/*', '*'):
        if st.startswith(m):
            return m
    return ''


# 「只由标识符/文件名构成」的注释行不算英文散文：`// TapirEngine`、
# `// construction.c`、`// save.c` 这类是给读者当前的文件/类型名，
# 翻成中文反而找不到对应物。
IDENT_ONLY = re.compile(r'^[\s/*!]*(?:[A-Za-z_][\w./-]*)(?:\([^)]*\))?[\s*/]*$')

def needs_translation(line):
    if not is_comment(line):
        return False
    if CJK.search(line) or not LATIN.search(line):
        return False
    body = line.lstrip().lstrip('/*!').strip()
    if IDENT_ONLY.match(body):          # 纯标识符 / 文件名
        return False
    if body.endswith(';'):              # 被注释掉的代码
        return False
    return True


def english_comment_lines(path, skip_head=0):
    out = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for i, line in enumerate(f, 1):
            if i <= skip_head:
                continue
            if needs_translation(line):
                out.append((i, line.rstrip('\n')))
    return out


def cmd_list(path, skip_head=0):
    rows = english_comment_lines(path, skip_head)
    for i, line in rows:
        print(f'{i}\t{line}')
    print(f'-- {len(rows)} 行待翻译：{path}', file=sys.stderr)


def cmd_apply(path, tsv):
    lines = open(path, encoding='utf-8').read().split('\n')
    changed = 0
    for raw in open(tsv, encoding='utf-8'):
        raw = raw.rstrip('\n')
        if not raw or raw.startswith('#'):
            continue
        no_s, new = raw.split('\t', 1)
        no = int(no_s)
        old = lines[no - 1]
        if marker(old) != marker(new):
            sys.exit(f'第 {no} 行标记不一致，拒绝修改：\n  旧: {old}\n  新: {new}')
        if not is_comment(old):
            sys.exit(f'第 {no} 行不是注释，拒绝修改：{old}')
        lines[no - 1] = new
        changed += 1
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))
    print(f'已替换 {changed} 行：{path}')


def iter_sources():
    for root in ('src', 'include', 'tools'):
        for dp, dn, fn in os.walk(root):
            rel_dir = dp.replace(os.sep, '/') + '/'
            if any(s in rel_dir for s in SKIP_DIRS):
                continue
            for f in sorted(fn):
                if os.path.splitext(f)[1] not in {'.c', '.h', '.py', '.sh'}:
                    continue
                p = os.path.join(dp, f)
                if p.replace(os.sep, '/') in SKIP_FILES:
                    continue
                yield p


def cmd_stats():
    total = 0
    rows = []
    for p in iter_sources():
        n = len(english_comment_lines(p))
        if n:
            rows.append((n, p))
            total += n
    rows.sort(reverse=True)
    for n, p in rows:
        print(f'{n:5d}  {p}')
    print(f'-- 合计 {total} 行英文注释，{len(rows)} 个文件')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == 'list':
        skip = 0
        args = [a for a in sys.argv[2:] if not a.startswith('--')]
        if '--skip-head' in sys.argv:
            skip = int(sys.argv[sys.argv.index('--skip-head') + 1])
        cmd_list(args[0], skip)
    elif cmd == 'apply':
        cmd_apply(sys.argv[2], sys.argv[3])
    elif cmd == 'stats':
        cmd_stats()
    else:
        sys.exit(__doc__)
