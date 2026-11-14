#!/bin/bash
# Rance10 启动器（chroot 内，必须 uid=1000 运行）
# 用法: run-rance10.sh [DISPLAY]
#
# 设计要点：
#  - 不用 `exec 引擎 | head`：管道会让 kill 只打到外层 shell，引擎变孤儿。
#  - **不要**用 ulimit -f 限日志：mesa 会用 memfd 分配缓冲，文件大小上限会
#    直接把引擎打死（实测 20MB 上限 → SIGXFSZ、日志 0 字节）。
#  - 改为 supervisor 每 5 秒查一次日志体积，超 50MB 就终止；
#    同时负责在桌面/终端退出时把引擎一起杀掉。
set -u

# [D170] 路径锚点：向上找到 tools/lib/common.sh —— 这样一来脚本放到
# 任何子目录（run/ build/ env/）都不用改路径计算，只改这一行不用动。
_d="$(dirname "$(realpath "$0")")"; while [ ! -f "$_d/lib/common.sh" ] && [ "$_d" != "/" ]; do _d="$(dirname "$_d")"; done; . "$_d/lib/common.sh"
PORT="$PORT_DIR"
export XSYS4_DATA_DIR="${XSYS4_DATA_DIR:-$PORT/xsystem4}"
# [N100] desktop Linux: force the x11 EGL platform so SDL really creates a window.
# Termux keeps its own Termux:X11 + zink/Turnip configuration untouched.
# [L3] see safe_run.sh: RANCE10_PLATFORM / TERMUX_VERSION, no absolute path.
PLAT="${RANCE10_PLATFORM:-}"
if [ -z "$PLAT" ] && [ -n "${TERMUX_VERSION:-}" ]; then PLAT=termux; fi
if [ "$PLAT" != termux ]; then
    export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
    export EGL_PLATFORM="${EGL_PLATFORM:-x11}"
fi
GAME="$PORT/game"
BIN="$ENGINE_BIN"
LOG="${RANCE10_LOG:-${TMPDIR:-.}/rance10_run.log}"
MAX=$((50 * 1024 * 1024))

if [ $# -ge 1 ]; then export DISPLAY="$1"; fi
if [ -z "${DISPLAY:-}" ]; then export DISPLAY=:0; fi

[ -x "$BIN" ] || { echo "找不到引擎: $BIN" >&2; exit 1; }
[ -d "$GAME" ] || { echo "找不到游戏目录: $GAME" >&2; exit 1; }

if ! xdpyinfo -display "$DISPLAY" >/dev/null 2>&1; then
    echo "连不上 X server: $DISPLAY（先把 Termux:X11 桌面开起来）" >&2
    exit 2
fi

cd "$GAME" || exit 1

"$BIN" --nodebug --encoding gbk . >"$LOG" 2>&1 &
CHILD=$!
trap 'kill -9 "$CHILD" 2>/dev/null; exit 0' INT TERM

while kill -0 "$CHILD" 2>/dev/null; do
    # 用 wait 等 sleep，否则 bash 要等 sleep 结束才执行 trap，杀进程要滞后 5 秒
    sleep 5 &
    wait $!
    S=$(stat -c %s "$LOG" 2>/dev/null || echo 0)
    if [ "$S" -gt "$MAX" ]; then
        echo "日志超过 50MB（$S 字节），终止游戏" >&2
        kill -9 "$CHILD" 2>/dev/null
        break
    fi
done

wait "$CHILD" 2>/dev/null
