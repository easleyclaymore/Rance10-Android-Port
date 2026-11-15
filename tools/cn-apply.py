#!/usr/bin/env python3
"""按行号整行替换注释（注释中文化专用）。

用法：
    python3 tools/cn-apply.py <<'JSON'
    {"file": "src/vm.c",
     "lines": {"55": "// 当 IP 被设为 VM_RETURN 时，VM 停机",
               "59": " * 注意：当前实现是朴素的字节码解释器。"}}
    JSON

为什么按行号而不是正则：注释里全是括号、引号、反斜杠，正则很容易改到
字符串字面量或代码。这里只替换指定的那几行，并校验：
  1. 目标行原本确实是注释行（`//` / `/*` / `*`）；
  2. 替换后行首标记类型不变（`//` 还是 `//`，`*` 还是 `*`）；
  3. 若原行含 `*/`，替换文本也必须含 `*/`（否则块注释不闭合，
     编译直接炸）；原行不含 `*/` 时替换文本也不许带。

缩进由脚本保留（取原行的前导空白），JSON 里只写标记之后的内容但必须
自己带 `*` / `//` 标记。
"""
import json
import re
import sys


def marker(line):
    st = line.strip()
    if st.startswith('//'):
        return '//'
    if st.startswith('/*'):
        return '/*'
    if st.startswith('*/'):
        return '*/'
    if st.startswith('*'):
        return '*'
    return None


def main():
    data = json.load(sys.stdin)
    path = data['file']
    mapping = {int(k): v for k, v in data['lines'].items()}
    lines = open(path, encoding='utf-8').read().split('\n')
    for no, new in sorted(mapping.items()):
        if no < 1 or no > len(lines):
            sys.exit(f'{path}:{no} 超出范围（共 {len(lines)} 行）')
        old = lines[no - 1]
        m_old = marker(old)
        if m_old is None:
            sys.exit(f'{path}:{no} 不是注释行：{old!r}')
        m_new = marker(new)
        if m_new != m_old:
            sys.exit(f'{path}:{no} 行首标记变了（{m_old} -> {m_new}）：{new!r}')
        if ('*/' in old) != ('*/' in new):
            sys.exit(f'{path}:{no} 注释闭合符变了：{old!r} -> {new!r}')
        indent = re.match(r'[\t ]*', old).group(0)
        lines[no - 1] = indent + new.strip()
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))
    print(f'ok {path}: {len(mapping)} 行')


main()
