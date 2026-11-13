#!/bin/bash
# storm.sh [tag] [seconds]
#
# 直接进入"地图装载风暴"现场，不依赖 xdotool 点击。
#
# 背景：点标题菜单进章节的单次命中率实测 ~30%，而一次跑测要 3 分钟，
# 靠点击复现 `QuestMapObjectLoader@Load` 的分配风暴既慢又不稳定。
# 引擎里有一个开关（XSYS4_AUTO_MAP）：启动若干毫秒后直接调用
# `Rance10@RunGameChapter1`(fno 22579)，跳过点击。
#
# 本脚本在此基础上把三个探针一次性打开：
#   XSYS4_MEM_STAT   每 2 s 一行堆构成（看 live/pages/rss 的斜率）
#   XSYS4_LVAR_DBG   盯 fno 23770 的局部变量；同时打开该函数内的 FTOI 探针
#                    （循环上界 rIndex 就是浮点转整数来的）和 6 帧调用栈
#   XSYS4_HEAP_LIVE_LIMIT  由 MEM-GUARD 使用，默认 50 万；这里显式写出来
#
# 用法：
#   tools/storm.sh stormA 60          # 默认盯 rIndex（l3）
#   XSYS4_LVAR_DBG=23770:2 tools/storm.sh stormB 60   # 改盯 lIndex（l2）
#
# 跑完看：
#   grep -a "\[auto\]\|\[ftoi\]\|\[lvar\]\|\[mem\]" logs/live_<tag>.log
set -u
TAG="${1:-storm}"
SECS="${2:-60}"
HERE="$(cd "$(dirname "$(realpath "$0")")" && pwd)"

export XSYS4_AUTO_MAP=1
export XSYS4_AUTO_MAP_DELAY="${XSYS4_AUTO_MAP_DELAY:-9000}"
export XSYS4_MEM_STAT="${XSYS4_MEM_STAT:-1}"
export XSYS4_LVAR_DBG="${XSYS4_LVAR_DBG:-23770:3}"
export XSYS4_HEAP_LIVE_LIMIT="${XSYS4_HEAP_LIVE_LIMIT:-500000}"
# 不点任何菜单：run-live 内置的"网络确认 OK"那一步仍然保留，
# 之后全是等待，进度完全由 XSYS4_AUTO_MAP 决定。
export RANCE10_LIVE_SEQ="${RANCE10_LIVE_SEQ:-wait:2000}"

exec "$HERE/run-headless.sh" "$TAG" "$SECS" "/tmp/${TAG}.png"
