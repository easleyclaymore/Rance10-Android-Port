#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""列出 AliceSoft .afa 归档内的文件条目。

用法:
    afa_ls.py <archive.afa> [过滤子串]

归档结构 (v2):
    0x00 "AFAH" | 0x04 0x1c | 0x08 "AlicArch" | 0x10 version
    0x18 data_start | 0x1c "INFO" | 0x20 table_csize+16
    0x24 table_usize | 0x28 nr_files
    0x2c..   zlib 压缩的文件表
    data_start: "DATA" + data_size
文件表条目: [pad4][len4][name(pad4)] [no4?] [u0 4][u1 4][off 4][size 4]
"""
import struct
import sys
import zlib


def read_table(path):
    d = open(path, 'rb').read()
    if d[:4] != b'AFAH':
        raise SystemExit('not an AFA archive: %s' % path)
    data_start = struct.unpack_from('<I', d, 24)[0]
    csz = struct.unpack_from('<I', d, 32)[0] - 16
    n = struct.unpack_from('<I', d, 40)[0]
    table = zlib.decompress(d[44:44 + csz])

    entries = []
    o = 0
    for _ in range(n):
        o += 4
        sl = struct.unpack_from('<i', table, o)[0]
        o += 4
        name = table[o:o + sl].rstrip(b'\x00')
        o += sl
        u0, u1, off, size = struct.unpack_from('<iiii', table, o)
        o += 16
        entries.append((name.decode('cp932', 'replace'), off, size))
    return d, data_start, entries


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    filt = sys.argv[2] if len(sys.argv) > 2 else None
    d, data_start, entries = read_table(path)
    print('%s: %d entries, data_start=%d, file_size=%d'
          % (path, len(entries), data_start, len(d)))
    for i, (name, off, size) in enumerate(entries):
        if filt and filt.lower() not in name.lower():
            continue
        print('%4d  off=%-8d size=%-6d %s' % (i, off, size, name))
    return 0


if __name__ == '__main__':
    sys.exit(main())
