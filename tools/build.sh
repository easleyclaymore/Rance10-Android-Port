#!/bin/bash
# build.sh — 编译离线工具（源码在本目录）到 ../tools/bin/
#
# 这些工具只读 AIN / ASD 文件：不启动游戏、不需要 X、不需要网络。
# 源码在 xsystem4/tools/*.c（随仓库版本管理），产物落在这里的 bin/。
#
#   tools/build.sh            编译全部
#   tools/build.sh ain_at     只编译一个
#
# 依赖：引擎已构建过（需要 xsystem4/build/subprojects/libsys4/libsys4.a）。
set -u

HERE="$(cd "$(dirname "$(realpath "$0")")" && pwd)"
SYS4="${RANCE10_SYS4:-$(dirname "$HERE")}"
INC="$SYS4/subprojects/libsys4/include"
LIB="$SYS4/build/subprojects/libsys4/libsys4.a"
SRC="$HERE"
# 产物落到项目级 tools/bin（与源码分开：源码进 git，二进制不进）
OUT="${RANCE10_TOOLS_BIN:-$(dirname "$SYS4")/tools/bin}"

# 需要暴露的离线工具。加新工具就加到这里（并在 tools/README.md 记一行）。
TOOLS_DEFAULT="ain_at gsave_dump ain_xref ain_audit ain_locals ain_spec \
               ain_refparam ain_icast ain_hllarity aindump"
# 注意：ain_*.c 之间大量使用同一套 libsys4 头，AGENTS 里也别各编一份。

if [ ! -f "$LIB" ]; then
    echo "缺少 $LIB" >&2
    echo "先构建引擎： (cd $SYS4 && ninja -C build)" >&2
    exit 1
fi

mkdir -p "$OUT"
CC="${CC:-cc}"
CFLAGS="${CFLAGS:--O1}"
LDLIBS="-lz -lm -ldeflate"

build_one() {
    local name="$1"
    local src="$SRC/$name.c"
    if [ ! -f "$src" ]; then
        echo "跳过（无源码）：$name" >&2
        return 0
    fi
    echo "  CC $name"
    if ! $CC $CFLAGS -I "$INC" "$src" "$LIB" -o "$OUT/$name" $LDLIBS; then
        echo "FAILED: $src" >&2
        return 1
    fi
}

rc=0
if [ $# -gt 0 ]; then
    for t in "$@"; do build_one "$t" || rc=1; done
else
    for t in $TOOLS_DEFAULT; do build_one "$t" || rc=1; done
fi

if [ "$rc" -eq 0 ]; then
    echo "OK -> $OUT"
    ls -1 "$OUT"
fi
exit "$rc"
