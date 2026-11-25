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
# 序列里 click/rclick 之后的停顿。默认 1.2s 不变；长剧情连点时用
# RANCE10_CLICK_SLEEP=0.5 之类收紧，否则 200+ 步要跑十几分钟。
CLICK_SLEEP="${RANCE10_CLICK_SLEEP:-1.2}"

# [D170] 路径锚点：向上找到 tools/lib/common.sh —— 这样一来脚本放到
# 任何子目录（run/ build/ env/）都不用改路径计算，只改这一行不用动。
_d="$(dirname "$(realpath "$0")")"; while [ ! -f "$_d/lib/common.sh" ] && [ "$_d" != "/" ]; do _d="$(dirname "$_d")"; done; . "$_d/lib/common.sh"
ROOT="$PORT_DIR"
export XSYS4_DATA_DIR="${XSYS4_DATA_DIR:-$XSYS4_DIR}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export EGL_PLATFORM="${EGL_PLATFORM:-x11}"
# [D249] 不再默认强制软件渲染。机器有 Intel UHD（/dev/dri/renderD128），
# `eglinfo` 实测能拿到 `Mesa Intel(R) Graphics (ADL-N)`；而这里原先写死
# `LIBGL_ALWAYS_SOFTWARE=1` + `GALLIUM_DRIVER=llvmpipe`，13.5fps 的瓶颈
# 有一半来自这行。注意 **Xvfb 没有 GLX**（glxinfo 空输出），X11 下 SDL
# 默认走 GLX，所以必须 `SDL_VIDEO_X11_FORCE_EGL=1` 才会走 EGL 硬件路径。
# 回退：XSYS4_FORCE_SOFTWARE_GL=1。
if [ "${XSYS4_FORCE_SOFTWARE_GL:-0}" = "1" ]; then
    export LIBGL_ALWAYS_SOFTWARE=1
    export GALLIUM_DRIVER="${GALLIUM_DRIVER:-llvmpipe}"
else
    export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-0}"
    export SDL_VIDEO_X11_FORCE_EGL="${SDL_VIDEO_X11_FORCE_EGL:-1}"
fi

LOG_DIR="$LOGS_DIR"
LOG="$LOG_DIR/live_${TAG}.log"
BIN="$ENGINE_BIN"
mkdir -p "$LOG_DIR"

# 构建新鲜度闸门：ninja 的失败信息可能是中文，用 grep -E "error|Linking"
# 过滤会看不到，于是后面整轮都在跑旧二进制 —— 探针"没输出"、修复"没效果"
# 全是假结论。宁可拒绝启动，也不要拿旧二进制的日志做判断。
if [ -f "$BIN" ]; then
    STALE="$(find "$XSYS4_DIR/src" "$XSYS4_DIR/include" \
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

cd "$GAME_DIR" || exit 1

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
# [D193] 引擎把缩略图写到 `$SAVE_DIR/SaveData/<name>.qnt`（见 PreBattleAutoSave），
# 只建 SAVE_DIR 时 `gfx_save_texture` 会报
# "Failed to open .../SaveData/Thumb9999.qnt: No such file or directory"。
mkdir -p "$SAVE_DIR/SaveData" 2>/dev/null
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
# [D498] 每次要与窗口交互前都**重新解析** WID（带重试）。
# 引擎在无 WM 的 Xvfb 里会间歇性地把窗口从 root 的子窗口表里挪走/重建
# （SDL 侧 wid 不变、无 DESTROYED 事件），启动时缓存的 `$WID` 很快就失效。
# `click()` 在 D149/D178 已经每次重解析，但 `engineshot`/`key` 分支一直
# 在用旧 id —— 这正是 D376 记录的「第一张 shot 之后不再有第二张」。
refresh_wid() {
    local fresh=""
    for _ in 1 2 3 4 5 6 7 8; do
        fresh=$(find_wid)
        [ -n "$fresh" ] && break
        sleep 0.06
    done
    [ -n "$fresh" ] && WID="$fresh"
    return 0
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
    # [D178] D149 只重算了**几何**，没重算 WID。实测引擎中途重设 SDL 窗口后
    # 旧 WID 失效，启动时缓存的 `0x200034` 再 windowfocus 已经没人接收，
    # 焦点落到 root(1135)，点击根本进不了输入层（`[inp] clicking=1` 一次都没有）。
    # 外部轮询同一 display 全程能看到 xsystem4 窗口 —— 所以窗口没消失，是 id 换了。
    # 现在每次点击重新解析当前窗口 id，并对它重新聚焦/抬升。
    refresh_wid
    local curwid="$WID" freshwid
    # [D498] 解析窗口 id 要**重试**。
    #
    # 实测（Xvfb `:90`，无窗口管理器）：设置菜单打开的那一瞬间，
    # `xwininfo -root -children` 会短暂返回 **0 children** —— 引擎的窗口
    # 在这一刻不在 root 的子窗口表里（SDL/X11 的内部重排，不是它被销毁：
    # SDL 侧全程 `wid=2`、无 DESTROYED/HIDDEN 事件）。原来的实现这时退回
    # 启动时缓存的 `$WID`，于是 `mousemove` 落到 root（`window:1135`），
    # 引擎收不到任何指针事件 —— 表现为「菜单里怎么点都没反应」。
    #
    # 重试 8 次 × 60ms ≈ 半秒；仍找不到才退回旧 WID。
    for _ in 1 2 3 4 5 6 7 8; do
        freshwid=$(find_wid)
        [ -n "$freshwid" ] && break
        sleep 0.06
    done
    if [ -n "$freshwid" ]; then
        curwid="$freshwid"
    fi
    local gx=0 gy=0 gw="$WIN_W" gh="$WIN_H"
    if [ -n "$curwid" ]; then
        local geom
        geom=$(xdotool getwindowgeometry --shell "$curwid" 2>/dev/null) || true
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
    if [ -n "$curwid" ]; then
        # [D246] 无 WM 时 `windowfocus --sync` 可能永久阻塞：s41/s42 的
        # `click:1845,818` 一次都没执行，日志里 `pos=(1845` 出现 0 次，
        # 而进程还活着、日志还在涨 —— 脚本卡在这一行直到跑测超时。
        # engineshot 分支早就加了 timeout，这里同样必须有。
        timeout -k 1 2 xdotool windowfocus --sync "$curwid" >/dev/null 2>&1 || true
        timeout -k 1 2 xdotool windowraise "$curwid" >/dev/null 2>&1 || true
    fi
    timeout -k 1 2 xdotool mousemove "$wx" "$wy" >/dev/null 2>&1 || true
    if [ -n "${XSYS4_CLICK_DBG:-}" ]; then
        echo "CLICK view=$1,$2 win=$wx,$wy btn=$3 geom=${gx},${gy},${gw}x${gh} wid=$curwid boot_wid=$WID ptr=$(xdotool getmouselocation 2>/dev/null | tr '\n' ' ')" >&2
        # [D178] 注意重定向顺序：`2>/dev/null >&2` 会让 stdout 也进 /dev/null
        # （fd2 已指向 /dev/null，随后 stdout 再指向它）—— 之前正是这样误判成
        # 「窗口树为空」。要先把 stdout 指到 stderr，再丢掉命令自己的 stderr。
        echo "--- xwininfo tree ---" >&2
        xwininfo -root -children >&2 2>/dev/null
        echo "--- focus=$(xdotool getwindowfocus 2>/dev/null) active=$(xdotool getactivewindow 2>/dev/null)" >&2
        echo "--- DISPLAY=${DISPLAY:-unset} Xvfb_pids=$(pgrep Xvfb 2>/dev/null | tr '\n' ' ') game_pid=${GAMEPID:-unknown}" >&2
        echo "--- live xsystem4 windows: $(xwininfo -root -children 2>/dev/null | awk '/"xsystem4"/{print $1}' | tr '\n' ' ')" >&2
    fi
    # [D498-A] 试着在 move 与 down 之间加 0.25s 的间隔（q1..q3），结果
    # **3/3 全失败**：这 0.25s 里引擎会先处理新位置的 MOUSE_ENTER/悬停态，
    # AIN 的输入窗口恰好在那时关闭，按下沿就整个丢了。回退成「move 后
    # 立刻按下」—— 按下沿与位置变化同帧到达，反而稳定（uc1 实证）。
    # 记录在这里，防止以后再踩。
    xdotool mousedown "$3" >/dev/null 2>&1 || true
    # [D245] 0.12s 在 llvmpipe 下只有约 1.5 帧（实测 13.5fps → 74ms/帧），
    # 按下态落在帧边界上时整帧采样不到，40 次点击只有 9 次产生 EndInput。
    # 拉到 0.2s（≈3 帧）保证至少两次 UpdateInput 能看到按下态。
    # [D528] 改为可调：检查点恢复后 AIN 在纯自旋等待里，输入阶段只由
    # 帧泵驱动（实测 ~1.7 次/秒），0.2s 的按住会被整段跳过——
    # clicking 恒 0、捕获条件（prev_clicking && !cur_clicking）永不成立。
    # RANCE10_CLICK_HOLD=<秒> 拉长按住，覆盖帧泵间隔。
    sleep "${RANCE10_CLICK_HOLD:-0.2}"
    xdotool mouseup "$3" >/dev/null 2>&1 || true
}

# [D508] drag <view_x1> <view_y1> <view_x2> <view_y2>：按住拖动（滑块专用）。
#
# click() 只有「瞬时按放」，验证不了 HSCROLLBAR 的拖动跟随——input.c 的
# D500 分支是按 `cur_clicking || prev_clicking` 每帧从指针位置重算
# scroll_rate 的，没有真实的按下-移动序列就永远是「点一下跳一下」。
# 这里按「move → down → 分步 move → up」发，步间隔 ≥2 帧（llvmpipe 74ms/帧
# → 0.15s），保证 AIN 每帧都能看到按下的指针在新位置。
# 坐标换算与 click() 同源（窗口几何每次重算，见 D149/D178）。
drag() {
    local x1="$1" y1="$2" x2="$3" y2="$4"
    refresh_wid
    local curwid="$WID" freshwid
    for _ in 1 2 3 4 5 6 7 8; do
        freshwid=$(find_wid)
        [ -n "$freshwid" ] && break
        sleep 0.06
    done
    [ -n "$freshwid" ] && curwid="$freshwid"
    local gx=0 gy=0 gw="$WIN_W" gh="$WIN_H"
    if [ -n "$curwid" ]; then
        local geom
        geom=$(xdotool getwindowgeometry --shell "$curwid" 2>/dev/null) || true
        if [ -n "$geom" ]; then
            gx=$(echo "$geom" | awk -F= '/^X=/{print $2}')
            gy=$(echo "$geom" | awk -F= '/^Y=/{print $2}')
            gw=$(echo "$geom" | awk -F= '/^WIDTH=/{print $2}')
            gh=$(echo "$geom" | awk -F= '/^HEIGHT=/{print $2}')
        fi
    fi
    if [ -n "$curwid" ]; then
        timeout -k 1 2 xdotool windowfocus --sync "$curwid" >/dev/null 2>&1 || true
        timeout -k 1 2 xdotool windowraise "$curwid" >/dev/null 2>&1 || true
    fi
    local wx wy
    local -a WX=() WY=()
    local step
    for step in 0 1 2 3 4 5 6 7; do
        local vx vy
        read -r vx vy <<< "$(awk -v a="$x1" -v b="$y1" -v c="$x2" -v d="$y2" \
            -v n="$step" 'BEGIN{ t=n/7.0; printf "%d %d", a+(c-a)*t, b+(d-b)*t }')"
        read -r wx wy <<< "$(awk -v x="$vx" -v y="$vy" -v ww="$gw" -v wh="$gh" \
            -v vw="$VIEW_W" -v vh="$VIEW_H" -v gx="$gx" -v gy="$gy" 'BEGIN{
                s = ww/vw < wh/vh ? ww/vw : wh/vh;
                ox = (ww-vw*s)/2; oy = (wh-vh*s)/2;
                printf "%d %d", gx+ox+x*s, gy+oy+y*s
            }')"
        WX+=("$wx"); WY+=("$wy")
    done
    timeout -k 1 2 xdotool mousemove "${WX[0]}" "${WY[0]}" >/dev/null 2>&1 || true
    timeout -k 1 2 xdotool mousedown 1 >/dev/null 2>&1 || true
    sleep 0.15
    for step in 1 2 3 4 5 6 7; do
        timeout -k 1 2 xdotool mousemove "${WX[$step]}" "${WY[$step]}" >/dev/null 2>&1 || true
        sleep 0.15
    done
    timeout -k 1 2 xdotool mouseup 1 >/dev/null 2>&1 || true
    echo "DRAG view=$x1,$y1->$x2,$y2 steps=8 wid=$curwid"
}


# [D222] import -window 在 GL 直绘下会拿到过期的空 backing pixmap，产出
# 390 字节的 1-bit 全黑图：它看起来像「画面全黑」，实际是截图失败。
# H035 就是被这张空图骗出来的假象（H031 之后第二次）。小于 2KB 一律判失败。
shot_valid() {
    [ -s "$1" ] || return 1
    [ "$(stat -c%s "$1" 2>/dev/null || echo 0)" -ge 2048 ] || return 1
    return 0
}

shot() { # shot path
    local out="$1"
    # 窗口截图（import -window）在 GL 直绘时可能只拿到过期的 backing pixmap，
    # 所以在 X11 上优先用 ffmpeg 抓整个屏幕，拿到的才是合成后的真实画面。
    # [L0] ffmpeg x11grab 在 GL 画面繁忙时会挂住不返回（实测把整轮跑测
    # 拖到外层 90 秒硬超时），必须自己套一层超时；失败就退回 import。
    if command -v ffmpeg >/dev/null 2>&1; then
        timeout -k 1 6 ffmpeg -loglevel quiet -f x11grab \
               -video_size "${RANCE10_GRAB_SIZE:-1920x1080}" \
               -i "${DISPLAY}.0" -frames:v 1 -y "png:$out" >/dev/null 2>&1 || true
        shot_valid "$out" && return 0
    fi
    [ -n "$WID" ] || return 0
    timeout -k 1 6 import -window "$WID" "png:$out" >/dev/null 2>&1 || true
    shot_valid "$out" && return 0
    # 重试 root window：它是合成后的整屏，通常比 GL 窗口的 backing pixmap 新
    timeout -k 1 6 import -window root "png:$out" >/dev/null 2>&1 || true
    shot_valid "$out" || echo "[shot-empty] $out: grab failed after retries" >&2
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
# [D182] XSYS4_AUTO_MAP 会自动调 Rance10@RunGameChapter1 直接进第一章地图，
# SceneTitle 永远不会出现在日志里；原循环会跑满 RC_LIMIT 次、白等约 14 秒，
# 把后面的动作序列推到 timeout 之后（[SEQ-CLICK-TIMEOUT] / D181）。
if [ -z "${XSYS4_AUTO_MAP:-}" ]; then
    for i in $(seq 1 "$RC_LIMIT"); do
        grep -aq "SceneTitle" "$LOG" && break
        click 960 540 3
        sleep 1.6
    done
fi

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
            # drag:<x1,y1,x2,y2>：按住从 (x1,y1) 拖到 (x2,y2)（滑块拖动验证用）
            drag)
                IFS=',' read -r dx1 dy1 dx2 dy2 <<< "$arg"
                drag "$dx1" "$dy1" "$dx2" "$dy2"
                sleep "$CLICK_SLEEP"
                ;;
            click|rclick)
                IFS=',' read -r cx cy <<< "$arg"
                if [ "$act" = "click" ]; then btn=1; else btn=3; fi
                click "$cx" "$cy" "$btn"
                sleep "$CLICK_SLEEP"
                ;;
            # key:<X11 keysym>，例如 key:Return / key:Escape
            key)
                # [D246] 同 click：无 WM 时 --sync 可能永久阻塞，必须带 timeout。
                [ -n "$WID" ] && timeout -k 1 2 xdotool windowfocus --sync "$WID" >/dev/null 2>&1 || true
                xdotool key --clearmodifiers "$arg" >/dev/null 2>&1 || true
                sleep 1.2
                ;;
            # engineshot: 让引擎自己 dump GL main surface（需 XSYS4_SHOT_DIR）。
            # 不能用 --clearmodifiers，否则组合键里的 Alt 会被清掉。
            # 这是绕开 X11 抓屏假象的权威画面证据，见 GUIDE 1.13。
            engineshot)
                # [D376] 两条 `--sync` 都要 timeout：无 WM 的 Xvfb 里
                # `windowfocus --sync` 会**永久阻塞**（和下面 click 分支
                # 同一个坑），一旦卡住整条动作序列就停在这里 —— `bcb2`/`bcb3`
                # 都是这样：第一张 shot 之后不再有任何 `[shot-engine]`，
                # 日志也不再增长，而进程还活着。当时只有 `windowactivate`
                # 带了 timeout，漏了紧跟着的 `windowfocus` 与 `key alt+s`。
                # [D498] 另外 `engineshot` 一直用启动时缓存的 `$WID`——引擎
                # 中途重建窗口后它就失效了，第二张 shot 永远拍不到。先重解析。
                refresh_wid
                [ -n "$WID" ] && timeout -k 1 3 xdotool windowactivate --sync "$WID" >/dev/null 2>&1 || true
                [ -n "$WID" ] && timeout -k 1 2 xdotool windowfocus --sync "$WID" >/dev/null 2>&1 || true
                # 点击后焦点可能已变，先走焦点路径再退回直接投递到窗口
                timeout -k 1 2 xdotool key alt+s >/dev/null 2>&1 || \
                    timeout -k 1 2 xdotool key --window "$WID" alt+s >/dev/null 2>&1 || true
                sleep 2.5
                ;;
            # keydown:/keyup: 保持按键状态。官方指南 r01：按住 Ctrl
            # 「不论是否已读，强制快进」，AIN 用 key_is_down(VK_CONTROL)
            # 检测，短促的 key: 无效，必须成对使用。
            keydown|keyup)
                refresh_wid
                [ -n "$WID" ] && timeout -k 1 2 xdotool windowfocus --sync "$WID" >/dev/null 2>&1 || true
                xdotool "$act" "$arg" >/dev/null 2>&1 || true
                sleep "$CLICK_SLEEP"
                ;;
            # skip:<ms>[:<x>,<y>[:<pat>]] —— 按住 Ctrl 快进 ms 毫秒，期间每 2 秒在
            # (x,y) 点一次。官方操作表：按住 Ctrl「不论是否已读，强制快进」。
            # 为什么需要它：`keydown:ctrl;wait:...;keyup:ctrl` 只能纯快进，
            # 而 ADV/战况播报途中会不停弹出 `ReplayMemoDialog`（图鉴已获得）把它
            # 挡住，快进就停在那里 —— 必须一边快进一边把弹窗点掉（D387）。
            # 第三个参数 `<pat>`：日志里一出现这个字符串就提前结束（用于
            # 「一直推进到进战斗为止」，不必猜要跑几分钟 —— D387 里猜 600 秒
            # 有时到、有时不到）。
            skip)
                ms="${arg%%:*}"
                rest="${arg#*:}"
                sxy=""
                pat=""
                if [ "$rest" != "$arg" ]; then
                    sxy="${rest%%:*}"
                    [ "$sxy" != "$rest" ] && pat="${rest#*:}"
                fi
                [ -n "$WID" ] && timeout -k 1 2 xdotool windowfocus --sync "$WID" >/dev/null 2>&1 || true
                timeout -k 1 2 xdotool keydown ctrl >/dev/null 2>&1 || true
                st=$SECONDS
                while [ $(( (SECONDS - st) * 1000 )) -lt "$ms" ]; do
                    if [ -n "$pat" ] && grep -aqF "$pat" "$LOG"; then
                        echo "skip: found '$pat', stop early at ${SECONDS}s (loaded=$(grep -ac 'act\] loaded' "$LOG"))"
                        break
                    fi
                    if [ -n "$sxy" ]; then
                        IFS=',' read -r cx cy <<< "$sxy"
                        timeout -k 1 2 xdotool mousemove "$cx" "$cy" click 1 >/dev/null 2>&1 || true
                    fi
                    sleep 2
                done
                timeout -k 1 2 xdotool keyup ctrl >/dev/null 2>&1 || true
                ;;
            *)
                echo "unknown step: $step" >&2
                ;;
        esac
        # [D182] 自动推进的回归跑测不需要逐步截图：ffmpeg x11grab 在 GL 繁忙时
        # 每次要等满 6 秒超时，5 步就是 30 秒。要逐帧对照时设 RANCE10_STEP_SHOT=1。
        # [D319] `RANCE10_NO_STEP_SHOT=1` 关掉逐步截图。
        # 起因：x11grab 在 GL 繁忙时单次要等满 6 秒超时，40 步的连点序列光截图
        # 就能吃掉 4 分钟，把「跑到战斗」这件事拖成十几分钟。上面 D182 已经为
        # AUTO_MAP 路径做过同样的取舍，这里补一个对普通路径的闸。
        if [ "${RANCE10_NO_STEP_SHOT:-0}" != "1" ] && \
           { [ "${RANCE10_STEP_SHOT:-0}" = "1" ] || [ -z "${XSYS4_AUTO_MAP:-}" ]; }; then
            shot "$(printf '%s_%02d_%s.png' "${SHOT%.png}" "$IDX" "$act")"
        fi
    done
    if [ "${RANCE10_STEP_SHOT:-0}" = "1" ] || [ -z "${XSYS4_AUTO_MAP:-}" ]; then
        echo "SEQ_LAST=$(printf '%s_%02d_%s.png' "${SHOT%.png}" "$IDX" "${act:-none}")"
    fi
fi
# [D182] AUTO_MAP 路径下前置流程变短，前面那张主截图会拍在场景切换的
# clear color 阶段（纯黑，实测 390 字节）。序列跑完后补拍一次，保证 SHOT
# 指向真实画面；默认路径（无 AUTO_MAP）逐字不变。
if [ -n "${XSYS4_AUTO_MAP:-}" ] && [ "${RANCE10_STEP_SHOT:-0}" != "1" ]; then
    shot "$SHOT"
fi
echo "SHOT=$SHOT"

wait "$RUNPID" 2>/dev/null
tail -3 "$LOG"
