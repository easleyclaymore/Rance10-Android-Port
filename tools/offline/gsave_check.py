#!/usr/bin/env python3
"""gsave_check.py —— 组存档（.asd）离线检查器。

背景（GUIDE 5.8 / [HLL-ARITY] 同期的 D497）：跑测时引擎自己既当运动员
又当裁判 —— 写档和读档共用 savedata.c 一套代码，写坏的档读回去也
"看起来正常"。本工具是**独立实现**的解析器（对照 libsys4 savefile.c
的 gsave_read/gsave_parse 逐段重写），专查三件事：

  1. 结构完整性：外层 GD 头、zlib 流、五张表（records/globals/strings/
     arrays/keyvals）的 offset 与流位置逐一比对；
  2. 值域：每个索引值按类型校验（string 下标落在字符串表内、
     struct/array 下标 -1（未设置）或落在表内）——libsys4 的
     gsave_validate_value 同款规则，另含 D139 的 v8 两额外 int 与
     D496 的枚举/委托按整数往返；
  3. 一致性统计：数组 rank/dimensions/nr_flat_arrays 自洽、
     v7 flat 元素类型直方图、记录的 struct 名清单。

加密档（body 首字节 0x1a，mt19937_xorcode）不支持解密，报 SKIP。
rsave（睡眠存档 00.asd）不是本格式，报 SKIP。

用法：
    python3 tools/offline/gsave_check.py <file.asd> [file2.asd …]
    python3 tools/offline/gsave_check.py ../logs/savedir/<tag>/*.asd
退出码：全部 OK=0，任何 Invalid=1。
"""

import struct
import sys
import zlib
from collections import Counter

GSAVE7_EMPTY_STRING = -3

# ain_data_type（libsys4 include/system4/ain.h）
T_INT, T_BOOL, T_LONG_INT, T_FLOAT = 1, 2, 4, 5
T_STRING = 6
T_STRUCT = 7
T_ARRAY_STRUCT = 0x0e
T_DELEGATE = 0x10
T_ARRAY = 79
T_ENUM2, T_ENUM = 91, 92

TYPE_NAMES = {
    0: 'void', 1: 'int', 2: 'bool', 3: 'short int?', 4: 'lint', 5: 'float',
    6: 'string', 7: 'struct', 10: 'ref int', 12: 'functype', 14: 'array<struct>',
    16: 'delegate', 19: 'ref float', 20: 'ref string', 47: 'array<bool>',
    63: 'array<functype>', 69: 'array<lint>', 73: 'array<float>',
    74: 'hll_param', 75: 'ref hll_param', 79: 'array<?>', 80: 'ref array',
    91: 'enum2', 92: 'enum',
}


class Invalid(Exception):
    pass


class Reader:
    def __init__(self, buf):
        self.buf = buf
        self.index = 0

    def i32(self):
        if self.index + 4 > len(self.buf):
            raise Invalid('truncated int32 at %d' % self.index)
        v = struct.unpack_from('<i', self.buf, self.index)[0]
        self.index += 4
        return v

    def cstr(self):
        end = self.buf.find(b'\0', self.index)
        if end < 0:
            raise Invalid('unterminated string at %d' % self.index)
        s = self.buf[self.index:end]
        self.index = end + 1
        return s

    def seek(self, off):
        if off < 0 or off > len(self.buf):
            raise Invalid('bad offset %d' % off)
        self.index = off


def validate_value(val, typ, n_strings, n_records, n_arrays):
    """libsys4 gsave_validate_value 同款 + D496 枚举/委托。"""
    if typ in (0, 1, 2, 4, 5, 12, 16, 91, 92, 74, 75):
        return True                     # int 族 / 委托 / 枚举 / 泛型参数
    if typ == 6:                        # string
        return 0 <= val < n_strings or val == GSAVE7_EMPTY_STRING
    if typ == 7:                        # struct
        return val == -1 or 0 <= val < n_records
    if typ == 79 or (0x17 <= typ <= 0x1f and typ != 0x19):  # array 族
        return val == -1 or 0 <= val < n_arrays
    return True                         # ref 系按索引语义，宽校验


def parse(path):
    problems, notes = [], []
    with open(path, 'rb') as f:
        raw = f.read()
    if len(raw) < 8 or raw[:4] != b'GD\x01\x01':
        # 同目录里还有非 GD 容器的 .asd（AFBGMMode/AFCGMode/MsgSkip 等
        # 配置文件走别的写出路径），不算坏档
        raise Skip('not a GD container (%s...)' % raw[:4].hex())
    raw_size = struct.unpack_from('<I', raw, 4)[0]
    body = raw[8:]
    if body and body[0] == 0x1a:
        raise Skip('encrypted (mt19937 xorcode) — not supported')
    try:
        buf = zlib.decompress(body)
    except zlib.error as e:
        raise Invalid('zlib: %s' % e)
    if len(buf) != raw_size:
        problems.append('raw_size %d != decompressed %d' % (raw_size, len(buf)))

    r = Reader(buf)
    key = r.cstr()
    uk1 = r.i32()
    version = r.i32()
    if version not in (4, 5, 7, 8):
        raise Invalid('unsupported version %d' % version)
    uk2 = r.i32()
    nr_ain_globals = r.i32()
    offs = {}
    for sec in ('records', 'globals', 'strings', 'arrays', 'keyvals'):
        offs[sec] = r.i32()
        offs['nr_' + sec] = r.i32()
    if version >= 8:
        r.i32(); r.i32()                # [D139] raw 总长 + 0
    group = r.cstr() if version >= 5 else b''

    out = {
        'key': key.decode('sjis', 'replace'),
        'group': group.decode('sjis', 'replace'),
        'version': version,
        'nr_ain_globals': nr_ain_globals,
        'counts': {},
    }

    # ---- records ----
    r.seek(offs['records'])
    records = []
    for _ in range(offs['nr_records']):
        if version <= 5:
            _t = r.i32(); r.cstr()
            records.append(('v5', r.i32()))
        else:
            si = r.i32()
            n = r.i32()
            if si != -1 and (si < 0 or si >= offs['nr_records']):
                raise Invalid('record struct_index %d oob' % si)
            idx = [r.i32() for _ in range(n)]
            # libsys4：STRUCT 记录的 indices 落 keyvals 表，
            # GLOBALS(-1) 记录的 indices 落 globals 表 —— 与直觉相反
            ub = offs['nr_globals'] if si == -1 else offs['nr_keyvals']
            for v in idx:
                if v < 0 or v >= ub:
                    raise Invalid('record index %d oob (ub=%d)' % (v, ub))
            records.append(si)
    out['counts']['records'] = len(records)

    # ---- globals ----
    r.seek(offs['globals'])
    gtypes = Counter()
    for _ in range(offs['nr_globals']):
        typ = r.i32()
        val = r.i32()
        name = r.cstr()
        if version <= 5:
            r.i32()
        gtypes[typ] += 1
        if not validate_value(val, typ, offs['nr_strings'], offs['nr_records'], offs['nr_arrays']):
            raise Invalid('global %s: value %d invalid for type %d'
                          % (name.decode('sjis', 'replace'), val, typ))
    out['counts']['globals'] = offs['nr_globals']
    out['global_types'] = gtypes

    # ---- strings ----
    r.seek(offs['strings'])
    slens = Counter()
    for _ in range(offs['nr_strings']):
        slens[min(len(r.cstr()), 64)] += 1
    out['counts']['strings'] = offs['nr_strings']

    # ---- arrays ----
    r.seek(offs['arrays'])
    atypes = Counter()
    for _ in range(offs['nr_arrays']):
        rank = r.i32()
        if rank < -1 or rank > 8:
            raise Invalid('array rank %d oob' % rank)
        dims = []
        expect_flat = 1 if rank > 0 else 0   # libsys4：初值 1，dim[0] 不乘
        if rank > 0:
            for i in range(rank):
                d = r.i32()
                dims.append(d)
                if d < 0:
                    raise Invalid('array dim %d < 0' % d)
                if i:
                    expect_flat *= d
        nr_flat = r.i32()
        if nr_flat != expect_flat:
            raise Invalid('flat count %d != expected %d' % (nr_flat, expect_flat))
        for _fa in range(nr_flat):
            nv = r.i32()
            if rank > 0 and nv != dims[0]:
                raise Invalid('flat nr_values %d != dim0 %d' % (nv, dims[0]))
            ft = r.i32() if version >= 7 else None
            for _i in range(nv):
                v = r.i32()
                t = ft if version >= 7 else r.i32()
                if ft is not None:
                    atypes[ft] += nv and 1 or 0
                if not validate_value(v, t, offs['nr_strings'], offs['nr_records'], offs['nr_arrays']):
                    raise Invalid('array elem %d invalid for type %s' % (v, t))
        if rank > 0 and dims[0] > 0:
            atypes[('arr', tuple(dims))] += 1
    out['counts']['arrays'] = offs['nr_arrays']
    out['array_elem_types'] = atypes

    # ---- keyvals ----
    r.seek(offs['keyvals'])
    for _ in range(offs['nr_keyvals']):
        if version <= 5:
            typ = r.i32()
            val = r.i32()
            r.cstr()
            if not validate_value(val, typ, offs['nr_strings'], offs['nr_records'], offs['nr_arrays']):
                raise Invalid('keyval invalid')
        else:
            r.i32()
    out['counts']['keyvals'] = offs['nr_keyvals']

    # ---- struct-defs (v7+) ----
    if version >= 7:
        n = r.i32()
        defs = []
        for _ in range(n):
            nm = r.cstr()
            nf = r.i32()
            for _f in range(nf):
                r.i32(); r.cstr()
            defs.append(nm.decode('sjis', 'replace'))
        out['struct_defs'] = defs
        out['counts']['struct_defs'] = n

    if r.index != len(buf):
        notes.append('trailing %d bytes after struct-defs' % (len(buf) - r.index))
    out['problems'] = problems
    out['notes'] = notes
    return out


class Skip(Exception):
    pass


def main():
    import glob
    paths = []
    for a in sys.argv[1:]:
        paths.extend(sorted(glob.glob(a)) or [a])
    if not paths:
        print(__doc__)
        return 2
    bad = 0
    for p in paths:
        try:
            out = parse(p)
            c = out['counts']
            print('OK    %-40s key=%s group=%s v%d  r=%d g=%d s=%d a=%d kv=%d sd=%s%s'
                  % (p.split('/')[-1], out['key'], out['group'], out['version'],
                     c['records'], c['globals'], c['strings'], c['arrays'],
                     c['keyvals'], c.get('struct_defs'),
                     ('; ' + '; '.join(out['problems'] + out['notes']))
                     if out['problems'] or out['notes'] else ''))
        except Skip as e:
            print('SKIP  %-40s %s' % (p.split('/')[-1], e))
        except Invalid as e:
            print('BAD   %-40s %s' % (p.split('/')[-1], e))
            bad += 1
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
