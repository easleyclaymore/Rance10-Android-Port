#!/bin/bash
# tools/lib/common.sh — 路径锚点与共用函数。
#
# 为什么要有这个文件：
# 之前每个脚本都自己算 `dirname "$0"/../..`，**假设的是自己的深度**。
# 于是"把脚本挪进子目录"这件事就变成了逐个手改相对层数 —— 漏一个就
# 只在某条路径上炸，而且往往等到跑测时才发现。
#
# 现在所有脚本都从这一个文件取路径。只要 `tools/lib/` 的位置不变，
# 脚本本身放到 `run/` `build/` `env/` 任何子目录都不用改。
#
# 用法（放在脚本开头）：
#     . "$(dirname "$(realpath "$0")")/../lib/common.sh"
#     # 之后可直接用 XSYS4_DIR / PORT_DIR / PROJ_DIR / GAME_AIN / ENGINE_BIN
#
# 覆盖：设置 `RANCE10_ROOT=<Rance10_Port 的绝对路径>` 可整体搬家
# （这是原有能力，保留）。

# --- 锚点：本文件自己在 tools/lib/，深度固定 ---
COMMON_DIR="$(cd "$(dirname "$(realpath "${BASH_SOURCE[0]:-$0}")")" && pwd)"
TOOLS_DIR="$(dirname "$COMMON_DIR")"        # .../xsystem4/tools
XSYS4_DIR="$(dirname "$TOOLS_DIR")"         # .../xsystem4
PORT_DIR="${RANCE10_ROOT:-$(dirname "$XSYS4_DIR")}"
PROJ_DIR="$(dirname "$PORT_DIR")"

# --- 常用路径 ---
ENGINE_BIN="${ENGINE_BIN:-$XSYS4_DIR/build/src/xsystem4}"
GAME_AIN="${GAME_AIN:-$PORT_DIR/game/Rance10.ain}"
GAME_DIR="${GAME_DIR:-$PORT_DIR/game}"
LOGS_DIR="${LOGS_DIR:-$PORT_DIR/logs}"
OFFLINE_BIN="${OFFLINE_BIN:-$TOOLS_DIR/bin}"   # [D171] 产物并入 xsystem4/tools/bin（已 gitignore）
AIN_AT="${AIN_AT:-$OFFLINE_BIN/ain_at}"

# --- 共用函数 ---
die() { echo "$(basename "$0"): $*" >&2; exit 1; }

# 我们的 python 脚本只依赖标准库，所以清掉 PYTHONPATH/PYTHONHOME 再跑：
# 一来更可重现，二来有些环境会往里塞坏掉的 shim（实测裸 python3 直接 abort）。
PY() { env -u PYTHONPATH -u PYTHONHOME python3 "$@"; }

need_engine() {
    [ -x "$ENGINE_BIN" ] || die "引擎未构建：$ENGINE_BIN（跑 tools/xsys4 build）"
}

need_gamedata() {
    [ -f "$GAME_AIN" ] || die "找不到游戏数据：$GAME_AIN（设 GAME=... 或用 RANCE10_ROOT 指到 Rance10_Port）"
}

need_offline() {
    [ -x "$AIN_AT" ] || die "离线工具未构建：$AIN_AT（跑 tools/xsys4 build tools）"
}

# 找空闲的 X display（:90 起，避开真实会话）
find_free_display() {
    local n
    for n in $(seq "${1:-90}" 120); do
        [ -e "/tmp/.X${n}-lock" ] && continue
        [ -e "/tmp/.X11-unix/X${n}" ] && continue
        echo "$n"
        return 0
    done
    return 1
}

export RANCE10_TOOLS_LIB="$COMMON_DIR"
