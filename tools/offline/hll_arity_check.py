#!/usr/bin/env python3
"""HLL 参数个数交叉核对：C 侧原型 vs AIN 声明。

为什么需要：
`--dump-hll`（D443/D493）给出 AIN 侧声明的每个 HLL 函数的实参个数；
libffi 调 HLL 时按**声明**建 cif——如果 C 函数的形参个数对不上，
ffi 会读越界的 args 数组，参数全是指向垃圾的指针（D91 的
`SS_Stub_GetMemoryInfo(float, float)` 就是实形 (ref int, ref int)
被写成两参 float 的错形桩）。

口径：
- C 侧参数个数 = 函数定义形参列表的顶层逗号数 + 1；`(void)` 记 0。
- `ref X` 在 C 侧是一个指针形参，仍算 1 个 —— 与 nr_args 同口径。
- 只核对**已实现**的函数（HLL_EXPORT / HLL_QUIET/HLL_WARN
  生成的定义也算——它们有真函数体）；HLL_TODO_EXPORT 是"没实现"，
  不在本表（那批归 reach_hll 管）。
- 已知豁免：无。发现一个修一个，或者在本文件加注释说明为什么
  该函数 arity 允许不同（历史上没有先例）。

用法：
    python3 tools/offline/hll_arity_check.py ../game/Rance10.ain
依赖引擎二进制取声明表（也可以先 --dump-hll 落盘再喂 tsv）。
"""

import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PORT = os.path.join(ROOT, '..')
HLL_DIR = os.path.join(ROOT, 'src', 'hll')

# 每个 C 形参类型的正则（用于把形参列表切成单个参数）
# 不需要精确类型名——按顶层逗号切就够了。


def load_declarations(ain_path):
    """[(lib, name, nr_args)]，取 --dump-hll 输出。"""
    exe = os.path.join(ROOT, 'build-sdl3', 'src', 'xsystem4')
    out = subprocess.run([exe, '--dump-hll', ain_path], capture_output=True,
                         text=True, env={k: v for k, v in os.environ.items()
                                         if k not in ('DISPLAY', 'WAYLAND_DISPLAY')})
    decls = []
    for line in out.stdout.splitlines():
        q = line.split('\t')
        if len(q) >= 4:
            decls.append((q[0], q[3], int(q[2])))
    return decls


def scan_hll_sources():
    """返回 {lib: (path, section_text)} 与 {文件: 源码文本}。

    一个 .c 可以宿主多个库（String.c 同时有 Int/Float）——导出查找
    必须限定在该库的 HLL_LIBRARY 段内，否则会把别的库的同名导出
    （Int.String vs Float.String）当成同一条。段 = 本库
    HLL_LIBRARY( 起到下一个 HLL_LIBRARY( 或文件尾。"""
    lib2file = {}
    texts = {}
    for p in glob.glob(os.path.join(HLL_DIR, '*.c')):
        s = open(p, encoding='utf-8', errors='replace').read()
        texts[p] = s
        for m in re.finditer(r'HLL_LIBRARY\((\w+)', s):
            lib2file[m.group(1)] = p
    sections = {}
    for lib, f in lib2file.items():
        s = texts[f]
        starts = [m.start() for m in re.finditer(r'HLL_LIBRARY\(', s)]
        key = None
        for m in re.finditer(r'HLL_LIBRARY\((\w+)', s):
            if m.group(1) == lib:
                key = m.start()
                break
        if key is None:
            sections[lib] = s
            continue
        nxt = min([x for x in starts if x > key], default=len(s))
        sections[lib] = s[key:nxt]
    return lib2file, texts, sections


def split_params(text):
    """按顶层逗号切形参列表；`(void)` 返回 []。"""
    depth = 0
    parts, cur = [], ''
    for ch in text:
        if ch in '(<[':
            depth += 1
        elif ch in ')>]':
            depth -= 1
        if ch == ',' and depth == 0:
            parts.append(cur)
            cur = ''
        else:
            cur += ch
    parts.append(cur)
    params = [p.strip() for p in parts if p.strip()]
    if len(params) == 1 and params[0] == 'void':
        return []
    return params


def find_c_definition(text, c_name):
    """找 `static ... c_name(形参)` 定义，返回形参列表或 None。"""
    # 定义形如：static <ret> c_name(type a, type b)\n{   或单行
    for m in re.finditer(r'(?:static\s+)?[A-Za-z_][A-Za-z0-9_ \*]*?\b%s\s*\('
                         % re.escape(c_name), text):
        # 从 ( 开始配平括号
        start = text.index('(', m.end() - 1)
        depth, i = 0, start
        while i < len(text):
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        body_after = text[i:i + 40]
        if '{' not in body_after:
            continue   # 前向声明，不是定义
        return split_params(text[start + 1:i])
    return None


def main():
    ain = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        PORT, 'game', 'Rance10.ain')
    ain = os.path.normpath(ain)
    decls = load_declarations(ain)
    lib2file, texts, sections = scan_hll_sources()

    # 每个文件：导出名 -> C 函数名（HLL_EXPORT 族第三参）。
    # HLL_TODO_EXPORT 不收（未实现，归 reach_hll 管）。
    export_re = re.compile(
        r'HLL_EXPORT\w*\(\s*(\w+)\s*,\s*(\w+)\s*\)')
    checked, problems, unresolved = 0, [], 0
    # [D497] 豁免：Array 库的谓词族（C 比 AIN 多一个 predicate 形参）。
    # 多出的形参由 array_has_predicate() 在运行期按**当前声明**自省守卫
    # （hll_current_fno + 末参是否 AIN_HLL_FUNC），1 参重载时永不使用
    # —— 这是 D59/D68/D313 记录在案的双重槽约定，不是错形。
    exempt = set()      # 集合（库名或 lib.name）；说明文字跟在赋值后
    exempt.add('Array') # 谓词族双重槽约定（见上方 [D497] 说明）
    exempt.add('SystemService.IsResetOnce')  # 条件重绑：PreLink 按
    # nr_arguments 挑实现（0 参用本形状、1 参用 Drapeko 形状），各自一致
    exempted = 0
    for lib, name, nr_args in sorted(decls):
        if lib in exempt:
            exempted += 1
            continue
        if ('%s.%s' % (lib, name)) in exempt:
            exempted += 1
            continue
        f = lib2file.get(lib)
        if not f:
            continue
        section = sections.get(lib, '')
        c_fn = None
        for m in export_re.finditer(section):
            if m.group(1) == name:
                c_fn = m.group(2)
                break
        if not c_fn:
            unresolved += 1
            continue
        # [D497] PreLink 重绑（MainEXFile 的 v12 一族）：运行期真正绑定的
        # 是 static_library_replace(&lib_X, "name", X_v12) 里的替换函数，
        # 导出表里那条是上游编辑器形式的旧形状。
        rep = re.search(r'static_library_replace\(&lib_%s,\s*"%s",\s*(\w+)\)'
                        % (re.escape(lib), re.escape(name)), texts[f])
        if rep:
            c_fn = rep.group(1)
        else:
            # 宏形态：`#define V12(name) static_library_replace(&lib_X,
            # #name, 前缀_##name##_v12)` + 一串 `V12(名字)`。
            # token-pasting 没法用字面正则搜，展开宏模板。
            for dm in re.finditer(
                    r'#define\s+(\w+)\(name\)\s+.*?static_library_replace\('
                    r'&lib_%s,\s*#name,\s*([A-Za-z_0-9]*##name##[A-Za-z_0-9]*)\)'
                    % re.escape(lib), texts[f], re.S):
                prefix = dm.group(2).replace('##name##', '%s')
                inv = re.search(r'\b%s\(%s\)' % (dm.group(1), re.escape(name)),
                                texts[f])
                if inv:
                    c_fn = prefix % name
                    break
        params = find_c_definition(texts[f], c_fn) if c_fn else None
        if params is None:
            unresolved += 1
            continue
        checked += 1
        if len(params) != nr_args:
            problems.append('%s.%s: AIN nr_args=%d, C `%s(%s)` = %d 参数'
                            % (lib, name, nr_args, c_fn,
                               ', '.join(params) if params else 'void',
                               len(params)))

    print('核对 %d 条（豁免 Array 谓词族 %d 条；%d 条找不到 C 定义，'
          '多为未实现声明）' % (checked, exempted, unresolved))
    if problems:
        print('参数个数不符 %d 条：' % len(problems))
        for p in problems:
            print('  ' + p)
        sys.exit(1)
    print('arity 全部一致')


if __name__ == '__main__':
    main()
