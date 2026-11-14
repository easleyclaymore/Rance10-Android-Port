#!/bin/bash
# alice.sh —— 包装外部 alice-tools（nunuhara/alice-tools），固定中文版必需的编码参数。
#
# 为什么需要这层包装：Rance10 是**简体中文版**，.afa 里的条目名和 .ain/.pactex 里的
# 字符串都是 GBK，而 alice-tools 默认按 Shift-JIS 处理。不加
# `--input-encoding gbk --output-encoding utf-8` 时：
#   - `alice ar list` 中途 `ERROR: iconv: Invalid or incomplete multibyte ...` 并截断；
#   - `alice ar extract -n "<名字>"` 永远匹配不到（名字是 GBK 字节）；
#   - 抽出来的文件可能只有几十字节的残缺内容。
# 这两个参数是**每条子命令都要带**的全局选项，且必须跟在子命令后面，
# 所以这层包装负责把它插到 "<组> <动作>" 之后。
#
# 安装（不在本仓库内，只在开发机上）：
#   git clone --recurse-submodules https://github.com/nunuhara/alice-tools
#   env -u PYTHONPATH -u PYTHONHOME meson setup build && ninja -C build
# 依赖：bison flex meson libpng libjpeg-turbo libwebp zlib（Arch 上全部现成）。
#
# 常用（都是只读操作，不改游戏数据）：
#   tools/alice.sh ar list      game/Rance10Pact.afa            # 列归档
#   tools/alice.sh ar extract -i 155 -o /tmp/x.pactex game/Rance10Pact.afa
#   tools/alice.sh ex dump      -o /tmp/x.x /tmp/x.pactex       # EX 全字段（部件表/类型名）
#   tools/alice.sh ain dump --function "SceneQuestMap@CheckNoLeader" -o /tmp/f.x game/Rance10.ain
#   tools/alice.sh asd dump     -o /tmp/s.x logs/savedir/xxx/GlobalSave.asd
#
# 与自家 tools/bin/* 的分工见同目录 README.md。
set -u

# [D171] 原来写死 /home/easley/... —— 换台机器就找不到。用 $HOME，并允许 ALICE_BIN 覆盖。
ALICE="${ALICE_BIN:-$HOME/tools/alice-tools/build/src/alice}"
if [ ! -x "$ALICE" ]; then
    echo "找不到 alice：$ALICE" >&2
    echo "用 ALICE_BIN=<path> 指定，或按本脚本头部注释构建 alice-tools" >&2
    exit 2
fi

if [ $# -lt 2 ]; then
    exec "$ALICE" "$@"
fi

# alice <组> <动作> [选项...] —— 全局选项必须跟在子命令之后
exec "$ALICE" "$1" "$2" \
     --input-encoding gbk --output-encoding utf-8 \
     "${@:3}"
