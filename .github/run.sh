#!/bin/sh
# 包装脚本：把自带的 libSDL3 放到最前面再启动。
#
# 为什么要这一层：系统源里通常还没有 SDL3（Ubuntu 24.04 就没有），
# 而本仓库用的是 SDL3 后端。tar 包里的 lib/ 带了构建时用的那一份，
# 这样解压即可运行，不用先装 SDL3。
DIR=$(dirname "$(readlink -f "$0")")
LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH
exec "$DIR/xsystem4" "$@"
