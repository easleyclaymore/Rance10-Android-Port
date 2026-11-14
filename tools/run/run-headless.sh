#!/bin/bash
# run-headless.sh <tag> [seconds] [shot_path]
#
# 在**独立 Xvfb** 里跑引擎，并把 DISPLAY 指过去，再复用 run-live.sh 的
# 完整点击序列（网络确认 OK → 右键进标题 → RANCE10_LIVE_SEQ）。
#
# 与 run-local.sh / 直接调 run-live.sh 的区别：那两个会连到调用者正在看的
# 真实桌面（本机显示器 / XWayland）。本脚本进来先 unset DISPLAY /
# WAYLAND_DISPLAY / XAUTHORITY，任何子命令都不可能碰到本机显示器。
#
# 用法（全部环境同 run-live.sh）：
#   tools/run-headless.sh hdA 150 /tmp/hdA.png
#   RANCE10_LIVE_SEQ="wait:3000;click:646,738;until:SceneQuestMap:40000;wait:3000" \
#       tools/run-headless.sh hdB 180 /tmp/hdB.png
set -u

# [MEM] 跑测必须限内存：这个引擎有两处历史泄漏（槽号不回收、地图/CG 纹理），
# 不设上限时长时间跑测会把整台机器推到 OOM —— 实测两次都是这么挂的。
# 默认 3G（正常峰值约 300-600 MB），命中上限只会杀死本次游戏进程，
# 不会波及桌面。RANCE10_MEM_CAP=0 才关闭（不推荐），其他值如 6G 可自行收紧。
MEM_CAP="${RANCE10_MEM_CAP:-3G}"
if [ -z "${RANCE10_MEM_CAP_ACTIVE:-}" ] && [ "$MEM_CAP" != "0" ]; then
    if command -v systemd-run >/dev/null 2>&1 &&
       systemd-run --user --scope -q -p MemoryMax="$MEM_CAP" -p MemorySwapMax=0 true 2>/dev/null; then
        export RANCE10_MEM_CAP_ACTIVE=1
        exec systemd-run --user --scope -q -p MemoryMax="$MEM_CAP" \
             -p MemorySwapMax=0 bash "$0" "$@"
    fi
    echo "WARNING: 无 systemd-run，本次跑测不受内存上限保护（RANCE10_MEM_CAP=$MEM_CAP）" >&2
fi

TAG="${1:-headless}"
SECS="${2:-180}"
SHOT="${3:-/tmp/headless_${TAG}.png}"
# [D170] 路径锚点：向上找到 tools/lib/common.sh —— 这样一来脚本放到
# 任何子目录（run/ build/ env/）都不用改路径计算，只改这一行不用动。
_d="$(dirname "$(realpath "$0")")"; while [ ! -f "$_d/lib/common.sh" ] && [ "$_d" != "/" ]; do _d="$(dirname "$_d")"; done; . "$_d/lib/common.sh"
HERE="$TOOLS_DIR"

# 硬约束：切断与本机图形会话的一切联系
unset DISPLAY WAYLAND_DISPLAY XAUTHORITY

# 从 :90 起找空闲 display，避开 :0/:1 等真实会话以及别人的 Xvfb
DNUM=""
for n in $(seq 90 120); do
    [ -e "/tmp/.X${n}-lock" ] && continue
    [ -e "/tmp/.X11-unix/X${n}" ] && continue
    DNUM="$n"
    break
done
if [ -z "$DNUM" ]; then
    echo "no free X display in :90..:120" >&2
    exit 1
fi

RES="${RANCE10_HEADLESS_RES:-1920x1080x24}"
Xvfb ":${DNUM}" -screen 0 "$RES" -nolisten tcp -ac -noreset \
    > "/tmp/xvfb_${DNUM}.log" 2>&1 &
XPID=$!

cleanup() {
    kill "$XPID" 2>/dev/null || true
    wait "$XPID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

SOCK="/tmp/.X11-unix/X${DNUM}"
for _ in $(seq 1 60); do
    [ -S "$SOCK" ] && break
    if ! kill -0 "$XPID" 2>/dev/null; then
        echo "Xvfb died on startup; see /tmp/xvfb_${DNUM}.log" >&2
        exit 1
    fi
    sleep 0.1
done
if [ ! -S "$SOCK" ]; then
    echo "Xvfb socket $SOCK never appeared" >&2
    exit 1
fi

export DISPLAY=":${DNUM}"
echo "HEADLESS DISPLAY=$DISPLAY Xvfb_pid=$XPID RES=$RES"
echo "本机显示器未参与；日志与截图仍写到 run-live.sh 的既有位置"

# 引擎 core dump 之后 run-live.sh 可能卡在 xdotool --sync / ffmpeg x11grab
# 的等待上（实测卡了 7 分钟不退）。外层再包一层硬超时，保证 Xvfb 一定
# 被 trap 回收，不把后续跑测堵死。
HARD=$((SECS + 90))
timeout -k 5 "$HARD" "$TOOLS_DIR/run/run-live.sh" "$TAG" "$SECS" "$SHOT"
RC=$?
if [ "$RC" -eq 124 ] || [ "$RC" -eq 137 ]; then
    echo "run-live.sh 未在 ${HARD}s 内结束（rc=$RC），已强制收尾；看 logs/live_${TAG}.log" >&2
fi
exit "$RC"
