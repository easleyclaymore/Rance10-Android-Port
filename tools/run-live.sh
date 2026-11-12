#!/bin/bash
# run-live.sh <tag> [seconds] [shot_path]
#
# 在调用者的真实桌面（DISPLAY / XWayland）上跑引擎，并自动按 Rance10 的前进节奏点：
#   1. 等 NetworkConfirmation 装载完（日志里第 2 次 "[act] loaded"）→ 左键点 OK
#   2. 之后每 1.6 s 右键点画面中央，直到 SceneTitle 装载或点击次数用完
#   3. 中途与结束时各抓一张窗口截图（真实桌面下 root 窗口抓不到 GL 内容，必须用窗口 id）
#
# 与 safe_run_click.sh 的区别：那个自己起 Xvfb，这个用你正在看的桌面。
set -u
TAG="${1:-live}"
SECS="${2:-180}"
SHOT="${3:-/tmp/live_${TAG}.png}"
MAX_BYTES="${MAX_LOG_BYTES:-20000000}"
OK_X="${RANCE10_LIVE_OK_X:-963}"
OK_Y="${RANCE10_LIVE_OK_Y:-817}"
RC_LIMIT="${RANCE10_LIVE_RCLICKS:-8}"

ROOT="${RANCE10_ROOT:-$(cd "$(dirname "$(realpath "$0")")/../.." && pwd)}"
export XSYS4_DATA_DIR="${XSYS4_DATA_DIR:-$ROOT/xsystem4}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export EGL_PLATFORM="${EGL_PLATFORM:-x11}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"
export GALLIUM_DRIVER="${GALLIUM_DRIVER:-llvmpipe}"

LOG_DIR="$ROOT/logs"
LOG="$LOG_DIR/live_${TAG}.log"
BIN="$ROOT/xsystem4/build/src/xsystem4"
mkdir -p "$LOG_DIR"

if [ -z "${DISPLAY:-}" ]; then
    echo "no DISPLAY; 请在图形会话里运行（或显式 export DISPLAY=:1）" >&2
    exit 1
fi

cd "$ROOT/game" || exit 1

# [D127 后续] 存档接通后，AFConfig.asd 会记住"网络已确认"，下一次启动
# 就不再弹 NetworkConfirmation —— 而本脚本的固定时序（等第 2 个 act、
# 再点 OK）依赖它存在，于是点击落空、只跑到 12 个活动。
# 跑测默认清档以保证可复现；要保留存档请设 RANCE10_LIVE_KEEP_SAVE=1。
SAVE_DIR="${HOME}/.xsystem4/$(printf '\xe5\x85\xb0\xe6\x96\xaf\xef\xbc\x91\xef\xbc\x90')/SaveData"
if [ "${RANCE10_LIVE_KEEP_SAVE:-0}" != "1" ] && [ -d "$SAVE_DIR" ]; then
    rm -rf "$SAVE_DIR"
    echo "cleared saves: $SAVE_DIR"
fi

echo "DISPLAY=$DISPLAY TAG=$TAG SECS=$SECS"
echo "LOG=$LOG"

# 直接重定向到文件：走 `| head -c` 时管道会在 140 KB 处断流（实测），
# 后面的 [frame]/[act] 全都写不进去，误诊成“日志停了 = 进程卡住”。
timeout -k 2 "$SECS" stdbuf -oL -eL "$BIN" --nodebug --encoding gbk . \
    > "$LOG" 2>&1 &
RUNPID=$!

loaded_count() {
    local n
    n=$(grep -ac '\[act\] loaded' "$LOG" 2>/dev/null)
    echo "${n:-0}"
}

# 1) 等窗口
# [D128] 这里以前取"第一个"叫 xsystem4 的窗口。上一局残留的窗口（或另一个
# 仍活着的实例）会让指针被 warp 到一个不属于本进程的窗口上，于是
# SDL_GetMouseState 永远是 (0,0)，AIN 侧 GetClickNumber 恒为 0 ——
# 表现成"点了没反应"，实际是点击根本没进本进程的窗口。
# 现在按窗口 owner pid 匹配本进程真正的子进程；找不到才退回最后一个。
GAMEPID=""
for _ in $(seq 1 20); do
    GAMEPID=$(pgrep -P "$RUNPID" 2>/dev/null | head -1)
    [ -n "$GAMEPID" ] && break
    sleep 0.2
done
find_wid() {
    local w p
    for w in $(xwininfo -root -children 2>/dev/null | awk '/"xsystem4"/{print $1}'); do
        p=$(xdotool getwindowpid "$w" 2>/dev/null)
        if [ -n "$GAMEPID" ] && [ "$p" = "$GAMEPID" ]; then
            echo "$w"
            return
        fi
    done
    xwininfo -root -children 2>/dev/null | awk '/"xsystem4"/{w=$1} END{print w}'
}
WID=""
for _ in $(seq 1 60); do
    WID=$(find_wid)
    [ -n "$WID" ] && break
    sleep 0.5
done
echo "PID=$RUNPID GAMEPID=${GAMEPID:-unknown}"
if [ -n "$WID" ]; then
    xdotool windowfocus --sync "$WID" >/dev/null 2>&1 || true
    xdotool windowraise "$WID" >/dev/null 2>&1 || true
    eval "$(xdotool getwindowgeometry --shell "$WID" 2>/dev/null)"
fi
echo "WINDOW=${WID:-none}"

# 引擎按 ViewWidth/ViewHeight（默认 1920x1080）渲染，窗口可能被缩放到屏幕大小
# 并做信箱化，所以传入的坐标是“视图坐标”，这里换算成窗口坐标再点。
VIEW_W="${RANCE10_VIEW_W:-1920}"
VIEW_H="${RANCE10_VIEW_H:-1080}"
WIN_W="${WIDTH:-$VIEW_W}"
WIN_H="${HEIGHT:-$VIEW_H}"
read -r SCALE OFF_X OFF_Y <<< "$(awk -v ww="$WIN_W" -v wh="$WIN_H" \
    -v vw="$VIEW_W" -v vh="$VIEW_H" 'BEGIN{
        s = ww/vw < wh/vh ? ww/vw : wh/vh;
        printf "%.6f %.3f %.3f", s, (ww-vw*s)/2, (wh-vh*s)/2
    }')"
echo "SCALE=$SCALE OFF=${OFF_X},${OFF_Y} WIN=${WIN_W}x${WIN_H} VIEW=${VIEW_W}x${VIEW_H}"

click() { # click <view_x> <view_y> <button>
    local wx wy
    read -r wx wy <<< "$(awk -v x="$1" -v y="$2" -v s="$SCALE" \
        -v ox="$OFF_X" -v oy="$OFF_Y" 'BEGIN{ printf "%d %d", ox+x*s, oy+y*s }')"
    if [ -n "$WID" ]; then
        xdotool mousemove --window "$WID" "$wx" "$wy" >/dev/null 2>&1 || \
            xdotool mousemove "$wx" "$wy" >/dev/null 2>&1 || true
    else
        xdotool mousemove "$wx" "$wy" >/dev/null 2>&1 || true
    fi
    xdotool mousedown "$3" >/dev/null 2>&1 || true
    sleep 0.12
    xdotool mouseup "$3" >/dev/null 2>&1 || true
}

shot() { # shot path
    # 窗口截图（import -window）在 GL 直绘时可能只拿到过期的 backing pixmap，
    # 所以在 X11 上优先用 ffmpeg 抓整个屏幕，拿到的才是合成后的真实画面。
    if command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -loglevel quiet -f x11grab -video_size "${RANCE10_GRAB_SIZE:-1920x1080}" \
               -i "${DISPLAY}.0" -frames:v 1 -y "png:$1" >/dev/null 2>&1 && return 0
    fi
    [ -n "$WID" ] || return 0
    import -window "$WID" "png:$1" >/dev/null 2>&1 || true
}

# 2) 等网络确认框（第 2 次 loaded）
for _ in $(seq 1 120); do
    [ "$(loaded_count)" -ge 2 ] && break
    sleep 0.5
done
sleep 1
# 点 OK 并重试：AIN 侧的 BeginInput..EndInput 输入窗口只在它的消息循环里短暂
# 打开，单击有一次没落在窗口内就整局卡在确认框（实测约一半概率）。
# 这里多点几次，直到第 3 个 activity（标题）装载。
for _ in $(seq 1 6); do
    [ "$(loaded_count)" -ge 3 ] && break
    echo "OK click at ${OK_X},${OK_Y} (loaded=$(loaded_count))"
    click "$OK_X" "$OK_Y" 1
    sleep 1.2
done

sleep 2
shot "${SHOT%.png}_dialog.png"

# 3) 右键推进到标题
for i in $(seq 1 "$RC_LIMIT"); do
    grep -aq "SceneTitle" "$LOG" && break
    click 960 540 3
    sleep 1.6
done

sleep 2
shot "$SHOT"
echo "TITLE_LOADED=$(grep -ac 'SceneTitle' "$LOG") loaded=$(loaded_count)"
echo "MID=${SHOT%.png}_dialog.png"

# 4) 可选的动作序列：RANCE10_LIVE_SEQ="move:548,718;click:646,738;wait:3000"
#    每步之后都截一张图（<shot>_<序号>_<动作>.png），方便逐帧对照。
IDX=0
if [ -n "${RANCE10_LIVE_SEQ:-}" ]; then
    IFS=';' read -r -a STEPS <<< "$RANCE10_LIVE_SEQ"
    for step in "${STEPS[@]}"; do
        [ -z "$step" ] && continue
        IDX=$((IDX + 1))
        act="${step%%:*}"
        arg="${step#*:}"
        case "$act" in
            wait)
                sleep "$(awk -v ms="$arg" 'BEGIN{printf "%.2f", ms/1000}')"
                ;;
            # until:<log 里的固定字符串>[:超时毫秒]
            # 场景切换是异步的（贴图/存档会插在中间），固定 sleep 截到的往往是
            # 上一幕。等到日志里真的出现目标场景再继续/截图。
            until)
                pat="${arg%%:*}"
                tmo="${arg#*:}"
                [ "$tmo" = "$arg" ] && tmo=60000
                waited=0
                while [ "$waited" -lt "$tmo" ]; do
                    grep -aqF "$pat" "$LOG" && break
                    sleep 0.4
                    waited=$((waited + 400))
                done
                echo "until '$pat' waited=${waited}ms"
                sleep 1.5
                ;;
            move)
                IFS=',' read -r mx my <<< "$arg"
                read -r wx wy <<< "$(awk -v x="$mx" -v y="$my" -v s="$SCALE" \
                    -v ox="$OFF_X" -v oy="$OFF_Y" \
                    'BEGIN{ printf "%d %d", ox+x*s, oy+y*s }')"
                xdotool mousemove --window "$WID" "$wx" "$wy" >/dev/null 2>&1 || \
                    xdotool mousemove "$wx" "$wy" >/dev/null 2>&1 || true
                sleep 0.6
                ;;
            click|rclick)
                IFS=',' read -r cx cy <<< "$arg"
                if [ "$act" = "click" ]; then btn=1; else btn=3; fi
                click "$cx" "$cy" "$btn"
                sleep 1.2
                ;;
            *)
                echo "unknown step: $step" >&2
                ;;
        esac
        shot "$(printf '%s_%02d_%s.png' "${SHOT%.png}" "$IDX" "$act")"
    done
    echo "SEQ_LAST=$(printf '%s_%02d_%s.png' "${SHOT%.png}" "$IDX" "${act:-none}")"
fi
echo "SHOT=$SHOT"

wait "$RUNPID" 2>/dev/null
tail -3 "$LOG"
