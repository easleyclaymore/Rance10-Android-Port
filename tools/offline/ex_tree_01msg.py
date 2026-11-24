#!/usr/bin/env python3
# [D517] 3D 页 EX 树解析：读出 ３Ｄ页面 主容器及各容器的 配置/レイアウト 声明。
# 用法:
#   ex_tree_3d.py                     全树 dump（叶子值截断）
#   ex_tree_3d.py <部件名子串>         只 dump 该节点
#   ex_tree_3d.py --configs           列出所有含 配置 字段的节点
#
# 关键：pact 里的字符串是 GBK 重打包（汉化组），原始字节保留后用
# 多编码尝试解码，避免 cp932 一次解码把信息丢掉。
import struct, zlib, sys, importlib.util

spec = importlib.util.spec_from_file_location('pd', 'tools/offline/pactex_dump.py')
pd = importlib.util.module_from_spec(spec); spec.loader.exec_module(pd)


def gbk(raw):
    """原始字节 → 可读串。汉化组的字符串是 GBK；少数残留日文是 cp932。"""
    for enc in ('gbk', 'cp932', 'shift_jis'):
        try:
            return raw.decode(enc)
        except Exception:
            continue
    return raw.decode('gbk', errors='replace')


def load_tree():
    d = open('../game/Rance10Pact.afa', 'rb').read()
    data_start = struct.unpack_from('<I', d, 24)[0]
    csz = struct.unpack_from('<I', d, 32)[0] - 16
    n = struct.unpack_from('<I', d, 40)[0]
    t = zlib.decompress(d[44:44 + csz])
    o = 0; target = None
    for _ in range(n):
        o += 4
        sl = struct.unpack_from('<i', t, o)[0]; o += 4
        raw_name = t[o:o + sl].rstrip(b'\x00'); o += sl
        u0, u1, off, size = struct.unpack_from('<iiii', t, o); o += 16
        if "消息速度页面" in gbk(raw_name):
            target = (data_start + off, size)
    buf = d[target[0]:target[0] + target[1]]
    info = pd.decode_pactex(buf)
    return info['raw'], info['nr_blocks']


class R:
    def __init__(s, b):
        s.b = b; s.i = 0

    def i32(s):
        v = struct.unpack_from('<i', s.b, s.i)[0]; s.i += 4; return v

    def f32(s):
        v = struct.unpack_from('<f', s.b, s.i)[0]; s.i += 4; return v

    def pstr(s):
        ln = s.i32()
        if ln < 0:
            return b''
        b = s.b[s.i:s.i + ln]; s.i += ln
        return b.rstrip(b'\x00')


def read_value(r, t):
    if t == 1: return r.i32()
    if t == 2: return r.f32()
    if t == 3: return r.pstr()
    if t == 4: return ('TABLE', read_table(r))
    if t == 5: return ('LIST', read_list(r))
    if t == 6: return ('TREE', read_tree(r))
    raise Exception('bad value type %d @%d' % (t, r.i))


def read_list(r):
    n = r.i32(); out = []
    for _ in range(n):
        t = r.i32(); sz = r.i32(); start = r.i
        v = read_value(r, t); out.append((t, v)); r.i = start + sz
    return out


def read_field(r):
    ft = r.i32(); name = r.pstr(); has = r.i32(); idx = r.i32()
    v = read_value(r, ft) if has else None
    sub = [read_field(r) for _ in range(r.i32())] if ft == 4 else None
    return (name, ft, v, sub)


def read_table(r):
    nf = r.i32(); fields = [read_field(r) for _ in range(nf)]
    nc = r.i32(); nr = r.i32()
    rows = [[read_value(r, f[1]) for f in fields] for _ in range(nr)]
    return (fields, rows)


def read_tree(r):
    name = r.pstr(); is_leaf = r.i32()
    if is_leaf:
        vt = r.i32(); sz = r.i32(); start = r.i
        vname = r.pstr()
        v = read_value(r, vt); r.i = start + sz; r.i32()
        return ('leaf', name, vname, v)
    n = r.i32()
    return ('node', name, [read_tree(r) for _ in range(n)])


def read_block(r):
    t = r.i32(); sz = r.i32(); start = r.i
    name = r.pstr()
    v = read_value(r, t); r.i = start + sz
    return name, t, v


def fmt_val(v):
    if isinstance(v, tuple) and v and v[0] == 'LIST':
        items = v[1]
        if len(items) <= 6:
            parts = []
            for t, x in items:
                if isinstance(x, tuple):
                    parts.append(x[0] + '[..]')
                elif isinstance(x, bytes):
                    parts.append(repr(gbk(x)))
                else:
                    parts.append(repr(x))
            return '[' + ', '.join(parts) + ']'
        return 'LIST(%d)' % len(items)
    if isinstance(v, tuple) and v and v[0] == 'TABLE':
        return 'TABLE[..]'
    if isinstance(v, bytes):
        return repr(gbk(v))[:50]
    return repr(v)[:50]


def leaf_name(n):
    return gbk(n[1])


def field_of(node, key):
    """在节点的直接 leaf 子件里找字段名含 key 的值。"""
    for c in node[2]:
        if c[0] == 'leaf' and key in leaf_name(c):
            return leaf_name(c), c[3]
    return None, None


def walk(node, depth, maxd, out):
    if node[0] == 'leaf':
        out.append('  ' * depth + '%s = %s' % (leaf_name(node), fmt_val(node[3])))
        return
    extra = ''
    if depth > 0:
        # 部件节点：把类型/名字打在同一行，便于读
        tn, tv = field_of(node, 'パーツ类型')
        nm = gbk(node[1])
        if tn and isinstance(tv, bytes):
            extra = '  <%s> %s' % (gbk(tv), nm)
    out.append('  ' * depth + '[%s]%s' % (gbk(node[1]), extra))
    if depth < maxd:
        for c in node[2]:
            walk(c, depth + 1, maxd, out)


def find(node, want):
    if node[0] == 'node' and want in gbk(node[1]):
        return node
    if node[0] == 'node':
        for c in node[2]:
            f = find(c, want)
            if f:
                return f
    return None


def find_all(node, want, acc):
    if node[0] == 'node':
        if want in gbk(node[1]):
            acc.append(node)
        for c in node[2]:
            find_all(c, want, acc)


def main():
    raw, nr_blocks = load_tree()
    r = R(raw)
    blocks = [read_block(r) for _ in range(nr_blocks)]
    tree = blocks[0][2][1]

    args = sys.argv[1:]
    if args and args[0] == '--configs':
        acc = []
        find_all(tree, 'レイアウト', acc)
        print('レイアウト 容器数:', len(acc))
        for node in acc:
            tn, tv = field_of(node, 'パーツ类型')
            cn, cv = field_of(node, '配置')
            print('  %-28s 类型=%-22s 配置=%s' % (
                gbk(node[1])[:28],
                gbk(tv) if isinstance(tv, bytes) else tv,
                cv))
        # 所有带 配置 的节点：打印「部件名 / 种类別情报 / 类型 / 配置」
        print()
        print('== 全树 配置 声明 ==')

        def scan(n, owner):
            if n[0] != 'node':
                return
            me = gbk(n[1])
            # 部件节点：有 种类別情报 子件
            is_part = any(c[0] == 'node' and gbk(c[1]) == '种类別情报'
                          for c in n[2])
            if is_part:
                owner = me
                ti = None
                for c in n[2]:
                    if c[0] == 'node' and gbk(c[1]) == '种类別情报':
                        ti = c
                if ti is not None:
                    tn, tv = field_of(ti, 'パーツ类型')
                    cn, cv = field_of(ti, '配置')
                    if cn is not None:
                        print('  %-26s %-22s 配置=%s' % (
                            owner[:26],
                            gbk(tv) if isinstance(tv, bytes) else tv,
                            cv))
            for c in n[2]:
                scan(c, owner)
        scan(tree, '')
        return

    if args:
        node = find(tree, args[0])
        if not node:
            print('not found:', args[0]); return
        out = []
        walk(node, 0, 99, out)
        print('\n'.join(out))
        return

    out = []
    walk(tree, 0, 99, out)
    print('\n'.join(out))


main()
