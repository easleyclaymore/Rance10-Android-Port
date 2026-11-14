#!/bin/bash
# run-local.sh <tag> [seconds] [shot_path]
#
# 在当前桌面上直接运行引擎（不新建 Xvfb），用于肉眼观察与手动操作。
# 与 safe_run.sh / safe_run_click.sh 的区别：
#   - 复用调用者的 DISPLAY / WAYLAND_DISPLAY，窗口出现在真实桌面
#   - 不做点击模拟（想点时自己动鼠标），跑完只留日志和截图
#   - 不清理任何 X 服务，因为那不属于本脚本
#
# 日志固定写 logs/local_<tag>.log，截图默认 /tmp/local_<tag>.png。
set -u
TAG="${1:-local}"
SECS="${2:-90}"
SHOT="${3:-/tmp/local_${TAG}.png}"
MAX_BYTES="${MAX_LOG_BYTES:-20000000}"

# [D170] 路径锚点：向上找到 tools/lib/common.sh —— 这样一来脚本放到
# 任何子目录（run/ build/ env/）都不用改路径计算，只改这一行不用动。
_d="$(dirname "$(realpath "$0")")"; while [ ! -f "$_d/lib/common.sh" ] && [ "$_d" != "/" ]; do _d="$(dirname "$_d")"; done; . "$_d/lib/common.sh"
ROOT="$PORT_DIR"
export XSYS4_DATA_DIR="${XSYS4_DATA_DIR:-$ROOT/xsystem4}"

# 真实桌面下用 SDL 自己的后端；只有当 X11 可用时才强制 x11/EGL。
if [ -n "${DISPLAY:-}" ] && [ -z "${RANCE10_DISPLAY_MODE:-}" ]; then
    RANCE10_DISPLAY_MODE=x11
fi
if [ "${RANCE10_DISPLAY_MODE:-}" = x11 ]; then
    export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
    export EGL_PLATFORM="${EGL_PLATFORM:-x11}"
    export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
    export GALLIUM_DRIVER="${GALLIUM_DRIVER:-llvmpipe}"
fi

LOG_DIR="$LOGS_DIR"
LOG="$LOG_DIR/local_${TAG}.log"
BIN="$ENGINE_BIN"
mkdir -p "$LOG_DIR"

cd "$GAME_DIR" || exit 1
echo "DISPLAY=${DISPLAY:-none} TAG=$TAG SECS=$SECS"
echo "LOG=$LOG"

timeout -k 2 "$SECS" stdbuf -oL -eL "$BIN" --nodebug --encoding gbk . 2>&1 \
    | head -c "$MAX_BYTES" > "$LOG" &
RUNPID=$!

# 等窗口出现（最多 20 s），再留时间给初始化与开场
for _ in $(seq 1 40); do
    if command -v xdotool >/dev/null 2>&1 &&
       [ -n "${DISPLAY:-}" ] &&
       xdotool search --name "Rance" >/dev/null 2>&1; then
        break
    fi
    sleep 0.5
done

# 中段抓一张：过程与结果都留证，窗口关掉后就抓不到了
MID="${SHOT%.png}_mid.png"
sleep $(( SECS / 2 ))
[ -n "${DISPLAY:-}" ] && command -v magick >/dev/null 2>&1 &&
    { magick import -window root "$MID" 2>/dev/null || true; }

wait "$RUNPID" 2>/dev/null
echo "MID=$MID"
echo "SHOT=$SHOT"
tail -3 "$LOG"
