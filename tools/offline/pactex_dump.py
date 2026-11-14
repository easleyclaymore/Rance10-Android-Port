#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""解析 Rance10 的 .pactex（System4 ex 格式的 activity 变体）。

格式:
    0x00 "HEAD" | 0x04 len(=12)
    0x08 "EXTF" | 0x0c version | 0x10 nr_blocks
    0x14 "DATA" | 0x18 compressed_size | 0x1c uncompressed_size
    0x20 payload(compressed_size)
payload 先按 256 项置换表逐字节解码，再 zlib 解压，得到 EX 数据。

置换表（与 libsys4/src/ex.c: ex_init 一致）:
    n = popcount(i)
    n 偶 -> ROR(i, n)      n 奇 -> ROL(i, n)

用法:
    pactex_dump.py <archive.afa> <序号|名字子串> [--raw 输出文件] [--strings]

输出: 头部字段、解压长度校验、可打印字符串片段；--raw 可保存明文。
"""
import struct
import sys
import zlib


def ex_table():
    t = [0] * 256
    for i in range(256):
        n = bin(i).count('1')
        if (n & 1) == 0:
            t[i] = ((i << (8 - n)) | (i >> n)) & 0xFF
        else:
            t[i] = ((i >> (8 - n)) | (i << n)) & 0xFF
    return t


TABLE = ex_table()


def afa_entries(path):
    d = open(path, 'rb').read()
    if d[:4] != b'AFAH':
        raise SystemExit('not an AFA archive')
    data_start = struct.unpack_from('<I', d, 24)[0]
    csz = struct.unpack_from('<I', d, 32)[0] - 16
    n = struct.unpack_from('<I', d, 40)[0]
    t = zlib.decompress(d[44:44 + csz])
    out = []
    o = 0
    for _ in range(n):
        o += 4
        sl = struct.unpack_from('<i', t, o)[0]
        o += 4
        name = t[o:o + sl].rstrip(b'\x00').decode('cp932', 'replace')
        o += sl
        u0, u1, off, size = struct.unpack_from('<iiii', t, o)
        o += 16
        out.append((name, off, size))
    return d, data_start, out


def decode_pactex(buf):
    if buf[:4] != b'HEAD':
        raise SystemExit('no HEAD marker')
    nr_blocks = struct.unpack_from('<I', buf, 16)[0]
    if buf[20:24] != b'DATA':
        raise SystemExit('no DATA marker')
    csz, usz = struct.unpack_from('<II', buf, 24)
    payload = buf[32:32 + csz]
    if len(payload) != csz:
        raise SystemExit('truncated payload: %d != %d' % (len(payload), csz))
    decoded = bytes(TABLE[b] for b in payload)
    out = zlib.decompress(decoded)
    return dict(nr_blocks=nr_blocks, csz=csz, usz=usz, raw=out)


def printable(raw, minlen=4):
    runs = []
    cur = bytearray()
    for b in raw:
        if 0x20 <= b < 0x7f:
            cur.append(b)
            continue
        if len(cur) >= minlen:
            runs.append(cur.decode('ascii'))
        cur = bytearray()
    if len(cur) >= minlen:
        runs.append(cur.decode('ascii'))
    return runs


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    path, key = sys.argv[1], sys.argv[2]
    raw_out = None
    show_strings = False
    args = sys.argv[3:]
    i = 0
    while i < len(args):
        if args[i] == '--raw' and i + 1 < len(args):
            raw_out = args[i + 1]
            i += 2
        elif args[i] == '--strings':
            show_strings = True
            i += 1
        else:
            i += 1

    d, data_start, entries = afa_entries(path)
    hit = None
    if key.isdigit():
        hit = entries[int(key)]
    else:
        for e in entries:
            if key.lower() in e[0].lower():
                hit = e
                break
    if not hit:
        raise SystemExit('entry not found: %s' % key)

    name, off, size = hit
    buf = d[data_start + off:data_start + off + size]
    info = decode_pactex(buf)
    print('entry   : %s' % name)
    print('stored  : %d bytes  compressed=%d  blocks=%d'
          % (size, info['csz'], info['nr_blocks']))
    print('inflated: %d bytes (header says %d) %s'
          % (len(info['raw']), info['usz'],
             'OK' if len(info['raw']) == info['usz'] else 'MISMATCH'))
    if show_strings:
        for s in printable(info['raw']):
            print('  str: %s' % s)
    if raw_out:
        open(raw_out, 'wb').write(info['raw'])
        print('written : %s' % raw_out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
