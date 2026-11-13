#!/bin/bash
# safe_run_click.sh <tag> [clicks] [seconds] [shot_path]
# 交互跑测扩展：与 safe_run.sh 同样的安全约束（独立 Xvfb、timeout -k 直管引擎、
# head -c 日志硬上限、跑完回收 Xvfb），额外在独立 display 上模拟若干次鼠标点击并抓屏。
# 用法示例:
#   XSYS4_CALL_TRACE=29656,29648,26422 tools/safe_run_click.sh d114 8 25 /tmp/d114.png
set -u
TAG="${1:-click}"
CLICKS="${2:-6}"
SECS="${3:-25}"
SHOT="${4:-/tmp/${TAG}.png}"
MAX_BYTES="${MAX_LOG_BYTES:-20000000}"
ROOT="${RANCE10_ROOT:-$(cd "$(dirname "$(realpath "$0")")/../.." && pwd)}"
export XSYS4_DATA_DIR="${XSYS4_DATA_DIR:-$ROOT/xsystem4}"
# [N100] see safe_run.sh: force x11 EGL/SDL so an X window exists and clicks land.
# [L3] see safe_run.sh: platform from RANCE10_PLATFORM / TERMUX_VERSION,
# never a hardcoded /data/data path.
PLAT="${RANCE10_PLATFORM:-}"
if [ -z "$PLAT" ] && [ -n "${TERMUX_VERSION:-}" ]; then PLAT=termux; fi
if [ -z "${RANCE10_DISPLAY_MODE:-}" ]; then
    if [ "$PLAT" = termux ]; then RANCE10_DISPLAY_MODE=termux
    else RANCE10_DISPLAY_MODE=x11; fi
fi
if [ "$RANCE10_DISPLAY_MODE" = x11 ]; then
    export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
    export EGL_PLATFORM="${EGL_PLATFORM:-x11}"
    export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
    export GALLIUM_DRIVER="${GALLIUM_DRIVER:-llvmpipe}"
fi
LOG_DIR="$ROOT/logs"
LOG="$LOG_DIR/safe_${TAG}.log"
BIN="$ROOT/xsystem4/build/src/xsystem4"
mkdir -p "$LOG_DIR"

DISP=":$((90 + RANDOM % 8))"
SCREEN_RES="${RANCE10_SCREEN:-1280x720x24}"
if [ "$RANCE10_DISPLAY_MODE" = x11 ]; then
    SCREEN_RES="${RANCE10_SCREEN:-1920x1080x24}"
fi
Xvfb "$DISP" -screen 0 "$SCREEN_RES" -nolisten tcp >/dev/null 2>&1 &
XPID=$!
sleep 1

cd "$ROOT/game" || { kill "$XPID" 2>/dev/null; exit 1; }
DISPLAY="$DISP" timeout -k 2 "$SECS" stdbuf -oL -eL "$BIN" --nodebug --encoding gbk . 2>&1 \
    | head -c "$MAX_BYTES" > "$LOG" &
RUNPID=$!

# Xvfb 没有窗口管理器，XTEST 的 ButtonPress 会落到 root 上被丢掉；必须先聚焦引擎窗口。
# [D115] 等游戏真正进入 start screen（日志出现 [act] loaded）再点，单纯等窗口
# 不可靠：AIN 初始化 10~20s，窗口 map 之后还要一段时间才响应输入。
# RANCE10_WAIT_LOADED=2 waits for the second activity (NetworkConfirmation)
# before clicking its buttons.
NEED_LOADED="${RANCE10_WAIT_LOADED:-1}"
for _w in $(seq 1 140); do
    n=$(grep -ac '\[act\] loaded' "$LOG" 2>/dev/null)
    [ "${n:-0}" -ge "$NEED_LOADED" ] 2>/dev/null && break
    sleep 0.5
done
sleep 1
# [N100] xdotool --name . can miss the SDL window (unnamed/UTF-8 title);
# xwininfo's child list gives the exact xsystem4 window id.
WID=$(DISPLAY="$DISP" xwininfo -root -children 2>/dev/null | awk '/"xsystem4"/{print $1; exit}')
if [ -z "${WID:-}" ]; then
    WID=$(DISPLAY="$DISP" xdotool search --onlyvisible --name . 2>/dev/null | tail -1)
fi
if [ -z "${WID:-}" ]; then
    WID=$(DISPLAY="$DISP" xwininfo -root -children 2>/dev/null | awk '/^[[:space:]]+0x[0-9a-f]+/{print $1; exit}')
fi
if [ -n "${WID:-}" ]; then
    DISPLAY="$DISP" xdotool windowfocus --sync "$WID" >/dev/null 2>&1 || true
    DISPLAY="$DISP" xdotool windowraise "$WID" >/dev/null 2>&1 || true
    eval "$(DISPLAY="$DISP" xdotool getwindowgeometry --shell "$WID" 2>/dev/null)"
    CX=$(( ${WIDTH:-1280} / 2 )); CY=$(( ${HEIGHT:-720} / 2 ))
else
    CX=400; CY=300
fi
echo "CLICK_WIN=${WID:-none} CENTER=${CX},${CY} COORDS=${RANCE10_CLICK_COORDS:-default}"

# RANCE10_CLICK_COORDS="825,793;825,887" rotates through explicit window-space
# coordinates (NetworkConfirmation OK/Cancel buttons at 1920x1080).
# 第三段可选，是鼠标键号："963,817,1" = 左键（默认），"960,540,3" = 右键。
# 有些界面（parts::detail::WaitForClick）只等右键或空格。
COORDS=()
if [ -n "${RANCE10_CLICK_COORDS:-}" ]; then
    IFS=';' read -r -a COORDS <<< "$RANCE10_CLICK_COORDS"
fi

# 每 1.5s 一次左键点击；按下保持 120ms，避免 down/up 同帧抵消（D113 的坑）
for _i in $(seq 1 "$CLICKS"); do
    sleep 1.5
    PX="$CX"; PY="$CY"; BTN="${RANCE10_CLICK_BUTTON:-1}"
    if [ ${#COORDS[@]} -gt 0 ]; then
        PT="${COORDS[$(( (_i - 1) % ${#COORDS[@]} ))]}"
        PX="${PT%%,*}"
        REST="${PT#*,}"
        PY="${REST%%,*}"
        if [ "$REST" != "$PY" ]; then
            BTN="${REST##*,}"
        fi
    fi
    if [ -n "${WID:-}" ]; then
        DISPLAY="$DISP" xdotool mousemove --window "$WID" "$PX" "$PY" >/dev/null 2>&1 || true
    else
        DISPLAY="$DISP" xdotool mousemove "$PX" "$PY" >/dev/null 2>&1 || true
    fi
    DISPLAY="$DISP" xdotool mousedown "$BTN" >/dev/null 2>&1 || true
    sleep 0.12
    DISPLAY="$DISP" xdotool mouseup "$BTN" >/dev/null 2>&1 || true
done

# 抓屏（Xvfb 上 root 窗口即可拿到 GL 内容，:2 的合成器不行）
DISPLAY="$DISP" import -window root "$SHOT" >/dev/null 2>&1 || true
wait "$RUNPID"; RC=$?
kill "$XPID" 2>/dev/null || true
wait "$XPID" 2>/dev/null || true

echo "EXIT=$RC (pipeline tail; engine killed by timeout is expected)"
echo "LOG=$LOG LOG_BYTES=$(stat -c %s "$LOG" 2>/dev/null || echo 0)"
echo "SHOT=$SHOT SHOT_BYTES=$(stat -c %s "$SHOT" 2>/dev/null || echo 0)"
grep -E '\*ERROR\*|Unimplemented HLL|Segmentation|SIGSEGV' "$LOG" 2>/dev/null | tail -6
find "$LOG_DIR" -maxdepth 1 -type f -name '*.log' -size +50M -delete 2>/dev/null || true
