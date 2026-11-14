#!/bin/bash
# Rance10 安全跑测包装 —— 2026-11-10 日志爆盘事故后的强制入口。
# 用法: tools/safe_run.sh <tag> [seconds]
# 保证: 日志 <= MAX_LOG_BYTES(默认20MB); timeout 直接管引擎二进制;
#       独立 Xvfb 回收; 跑完自动清掉任何 >50M 的历史日志。
set -u
TAG="${1:-run}"
SECS="${2:-20}"
MAX_BYTES="${MAX_LOG_BYTES:-20000000}"
# [D170] 路径锚点：向上找到 tools/lib/common.sh —— 这样一来脚本放到
# 任何子目录（run/ build/ env/）都不用改路径计算，只改这一行不用动。
_d="$(dirname "$(realpath "$0")")"; while [ ! -f "$_d/lib/common.sh" ] && [ "$_d" != "/" ]; do _d="$(dirname "$_d")"; done; . "$_d/lib/common.sh"
ROOT="$PORT_DIR"
export XSYS4_DATA_DIR="${XSYS4_DATA_DIR:-$ROOT/xsystem4}"
# [N100] Xvfb has no DRI3; mesa falls back to surfaceless EGL and SDL creates
# no X window at all (root 0 children, black screenshot, XTEST clicks lost).
# Force the x11 platform on desktop Linux. Termux keeps its own X11+zink setup.
# [L3] Platform is explicit (RANCE10_PLATFORM=termux|linux) or detected from
# Termux's own env var. Never key off a hardcoded Android absolute path.
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
LOG_DIR="$LOGS_DIR"
LOG="$LOG_DIR/safe_${TAG}.log"
BIN="$ENGINE_BIN"
mkdir -p "$LOG_DIR"

# 1) 独立 Xvfb, 绕开 xvfb-run 的 shell 包装(昨天的孤儿进程来源)
DISP=":$((90 + RANDOM % 8))"
# [N100] Rance10 EX parts use 1920x1080 coordinates; use a matching screen so
# the SDL window is fully on-screen and button hitboxes are reachable.
SCREEN_RES="${RANCE10_SCREEN:-1280x720x24}"
if [ "$RANCE10_DISPLAY_MODE" = x11 ]; then
    SCREEN_RES="${RANCE10_SCREEN:-1920x1080x24}"
fi
Xvfb "$DISP" -screen 0 "$SCREEN_RES" -nolisten tcp >/dev/null 2>&1 &
XPID=$!
sleep 1

cd "$GAME_DIR" || { kill "$XPID" 2>/dev/null; exit 1; }
# 2) 管道硬截断: 写满上限后 head 退出, 引擎失去 fd (SIGPIPE) 必然停止;
#    timeout -k 再兜一层 SIGKILL, 杜绝会话结束后继续写文件。
DISPLAY="$DISP" timeout -k 2 "$SECS" stdbuf -oL -eL "$BIN" --nodebug --encoding gbk . 2>&1 \
    | head -c "$MAX_BYTES" > "$LOG"
RC=${PIPESTATUS[0]}
kill "$XPID" 2>/dev/null || true
wait "$XPID" 2>/dev/null || true

echo "EXIT=$RC"
grep -E '\*ERROR\*|Unimplemented HLL|Segmentation|SIGSEGV' "$LOG" 2>/dev/null | tail -6
echo "LOG_BYTES=$(stat -c %s "$LOG" 2>/dev/null || echo 0)"

# 3) 兜底: 任何遗留巨型日志立即清掉
find "$LOG_DIR" -maxdepth 1 -type f -name '*.log' -size +50M -delete 2>/dev/null || true
exit "$RC"
