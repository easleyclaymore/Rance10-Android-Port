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

# [SAFE] 内存上限必须在这一层就有：baseline.sh / storm.sh 走 run-headless.sh
# 时已有上限，但**直接**调本脚本（调试时最容易这么做）会绕过它 —— 实测一次
# 直接把游戏放到 9.5 GB RSS，把整台机器的 OOM killer 触发、连带桌面程序被杀。
# 默认 3G；RANCE10_MEM_CAP=0 关闭（不推荐）。
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

# [SAFE] 桌面保护：本脚本会 xdotool 移动鼠标/点击、并用 x11grab 截屏，
# 只能在**独立 Xvfb** 上跑。run-headless.sh 用的是 :90..:120，正常路径不会
# 到这里；直接调本脚本时 DISPLAY 很可能是用户正在用的 :0/:1 —— 那种情况下
# 拒绝启动，而不是在人家桌面上点鼠标。
if [ -z "${RANCE10_LIVE_ALLOW_DESKTOP:-}" ]; then
    case "${DISPLAY:-}" in
        :9[0-9]|:1[0-2][0-9]) : ;;
        *)
            echo "拒绝在真实桌面运行：DISPLAY='${DISPLAY:-}'" >&2
            echo "请用 tools/run-headless.sh（自带独立 Xvfb :90+）；" >&2
            echo "确实要在当前桌面跑请显式设 RANCE10_LIVE_ALLOW_DESKTOP=1" >&2
            exit 3
            ;;
    esac
fi

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

# 构建新鲜度闸门：ninja 的失败信息可能是中文，用 grep -E "error|Linking"
# 过滤会看不到，于是后面整轮都在跑旧二进制 —— 探针"没输出"、修复"没效果"
# 全是假结论。宁可拒绝启动，也不要拿旧二进制的日志做判断。
if [ -f "$BIN" ]; then
    STALE="$(find "$ROOT/xsystem4/src" "$ROOT/xsystem4/include" \
             -type f \( -name '*.c' -o -name '*.h' \) -newer "$BIN" \
             -print -quit 2>/dev/null)"
    if [ -n "$STALE" ]; then
        echo "STALE BINARY: $STALE is newer than $BIN" >&2
        echo "先跑 env -u PYTHONHOME -u PYTHONPATH ninja -C build 并确认退出码为 0" >&2
        echo "（设 RANCE10_LIVE_ALLOW_STALE=1 可强制跳过检查）" >&2
        [ "${RANCE10_LIVE_ALLOW_STALE:-0}" = "1" ] || exit 2
    fi
fi

if [ -z "${DISPLAY:-}" ]; then
    echo "no DISPLAY; 请在图形会话里运行（或显式 export DISPLAY=:1）" >&2
    exit 1
fi

cd "$ROOT/game" || exit 1

# [D127 后续] 存档接通后，AFConfig.asd 会记住"网络已确认"，下一次启动
# 就不再弹 NetworkConfirmation —— 而本脚本的固定时序（等第 2 个 act、
# 再点 OK）依赖它存在，于是点击落空、只跑到 12 个活动。
#
# [L0] 存档目录必须显式、可控、可复现。默认每个 tag 一个干净目录（在工作区
# 内），跑完保留供检查；RANCE10_SAVE_DIR=<dir> 指定目录（做「写 → 读」往返
# 验证时复用同一份）；RANCE10_LIVE_KEEP_SAVE=1 才回落到引擎默认目录。
#
# 不能再依赖 `rm -rf ~/.xsystem4/...`：本机的 rm 被沙箱包了一层，删除会静默
# 失败，于是每轮跑测都在读 11 月 12 日的旧档 —— 修好的和没修好的都看不出来，
# 告警也全是陈年数据。改走 --save-folder 后，存档状态变成显式输入。
DEFAULT_SAVE_DIR="${HOME}/.xsystem4/$(printf '\xe5\x85\xb0\xe6\x96\xaf\xef\xbc\x91\xef\xbc\x90')/SaveData"
if [ "${RANCE10_LIVE_KEEP_SAVE:-0}" = "1" ]; then
    SAVE_DIR="$DEFAULT_SAVE_DIR"
else
    SAVE_DIR="${RANCE10_SAVE_DIR:-$LOG_DIR/savedir/$TAG}"
fi
mkdir -p "$SAVE_DIR" 2>/dev/null
echo "saves: $SAVE_DIR"

echo "DISPLAY=$DISPLAY TAG=$TAG SECS=$SECS"
echo "LOG=$LOG"

# 直接重定向到文件：走 `| head -c` 时管道会在 140 KB 处断流（实测），
# 后面的 [frame]/[act] 全都写不进去，误诊成“日志停了 = 进程卡住”。
timeout -k 2 "$SECS" stdbuf -oL -eL "$BIN" --nodebug --encoding gbk \
    --save-folder "$SAVE_DIR" . \
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
    # --sync 在窗口不响应时会无限等，同样加超时
    timeout -k 1 5 xdotool windowfocus --sync "$WID" >/dev/null 2>&1 || true
    timeout -k 1 5 xdotool windowraise "$WID" >/dev/null 2>&1 || true
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
    # [D149] 每次点击**重新解析窗口几何**，不再假设窗口固定在 (0,0)/1920x1080。
    #
    # 症状：跑测跑到某个场景之后再点，`[inp] pos=` 里的坐标再也不变。
    # `mouse_get_pos()` 是直接查 `SDL_GetMouseState()`，不是缓存，所以这只可能
    # 意味着指针真没动。用 `xdotool getmouselocation` 一验就露馅：
    #
    #   CLICK view=600,700 ... ptr=x:600 y:700 window:1135      ← 指针到了
    #   （游戏窗口是 0x200034 = 2097204）                        ← 但没落在游戏上
    #
    # 也就是窗口在中途换了位置/尺寸（Xvfb 无 WM，引擎重设窗口时没人补焦点与几何），
    # 而脚本用的是启动时算好的 SCALE/OFF，之后每次点击都点到别处去了。
    local gx=0 gy=0 gw="$WIN_W" gh="$WIN_H"
    if [ -n "$WID" ]; then
        local geom
        geom=$(xdotool getwindowgeometry --shell "$WID" 2>/dev/null) || true
        if [ -n "$geom" ]; then
            gx=$(echo "$geom" | awk -F= '/^X=/{print $2}')
            gy=$(echo "$geom" | awk -F= '/^Y=/{print $2}')
            gw=$(echo "$geom" | awk -F= '/^WIDTH=/{print $2}')
            gh=$(echo "$geom" | awk -F= '/^HEIGHT=/{print $2}')
        fi
    fi
    read -r wx wy <<< "$(awk -v x="$1" -v y="$2" -v ww="$gw" -v wh="$gh" \
        -v vw="$VIEW_W" -v vh="$VIEW_H" -v gx="$gx" -v gy="$gy" 'BEGIN{
            s = ww/vw < wh/vh ? ww/vw : wh/vh;
            ox = (ww-vw*s)/2; oy = (wh-vh*s)/2;
            printf "%d %d", gx+ox+x*s, gy+oy+y*s
        }')"
    if [ -n "$WID" ]; then
        xdotool windowfocus --sync "$WID" >/dev/null 2>&1 || true
    fi
    xdotool mousemove "$wx" "$wy" >/dev/null 2>&1 || true
    if [ -n "${XSYS4_CLICK_DBG:-}" ]; then
        echo "CLICK view=$1,$2 win=$wx,$wy btn=$3 geom=${gx},${gy},${gw}x${gh} wid=$WID ptr=$(xdotool getmouselocation 2>/dev/null | tr '\n' ' ')" >&2
        # 落点窗口 != 游戏窗口时，把整棵窗口树打出来：Xvfb 没有 WM，
        # 多出来的那层是谁只能靠树看。
        echo "--- xwininfo tree ---" >&2
        xwininfo -root -children 2>/dev/null | sed -n '5,14p' >&2
    fi
    xdotool mousedown "$3" >/dev/null 2>&1 || true
    sleep 0.12
    xdotool mouseup "$3" >/dev/null 2>&1 || true
}

shot() { # shot path
    # 窗口截图（import -window）在 GL 直绘时可能只拿到过期的 backing pixmap，
    # 所以在 X11 上优先用 ffmpeg 抓整个屏幕，拿到的才是合成后的真实画面。
    # [L0] ffmpeg x11grab 在 GL 画面繁忙时会挂住不返回（实测把整轮跑测
    # 拖到外层 90 秒硬超时），必须自己套一层超时；失败就退回 import。
    if command -v ffmpeg >/dev/null 2>&1; then
        timeout -k 1 6 ffmpeg -loglevel quiet -f x11grab \
               -video_size "${RANCE10_GRAB_SIZE:-1920x1080}" \
               -i "${DISPLAY}.0" -frames:v 1 -y "png:$1" >/dev/null 2>&1 && return 0
    fi
    [ -n "$WID" ] || return 0
    timeout -k 1 6 import -window "$WID" "png:$1" >/dev/null 2>&1 || true
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
            # clickunless:<x,y>:<log 里的固定字符串>
            # 只在日志里还没出现该字符串时才点。用来做"点一次没进就再点一次"
            # 的重试链：标题菜单的出画时刻受贴图加载影响，固定 sleep 后单次
            # 点击的命中率实测只有 ~30%，而一次跑测要 3 分钟。
            clickunless)
                coords="${arg%%:*}"
                pat="${arg#*:}"
                if grep -aqF "$pat" "$LOG"; then
                    echo "skip click (already at '$pat')"
                else
                    IFS=',' read -r cx cy <<< "$coords"
                    click "$cx" "$cy" 1
                    sleep 1.2
                fi
                ;;
            click|rclick)
                IFS=',' read -r cx cy <<< "$arg"
                if [ "$act" = "click" ]; then btn=1; else btn=3; fi
                click "$cx" "$cy" "$btn"
                sleep 1.2
                ;;
            # key:<X11 keysym>，例如 key:Return / key:Escape
            key)
                [ -n "$WID" ] && xdotool windowfocus --sync "$WID" >/dev/null 2>&1 || true
                xdotool key --clearmodifiers "$arg" >/dev/null 2>&1 || true
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
