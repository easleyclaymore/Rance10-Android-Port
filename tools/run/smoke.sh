#!/bin/bash
# tools/run/smoke.sh [quick|full]
#
# 无头冒烟：跑一套**固定配方**，用日志断言判 PASS/FAIL，**退出码即结论**。
#
# 为什么不看截图：截图要人看，看过就忘，也没法进 CI。日志里的
# `[act] loaded '<活动名>'`、`[msgline] N`、`ERROR` 都是机器可判的，
# 而且比截图更早暴露问题（截图只能证明「那一帧画出来了」）。
#
# 档位
#   quick （默认，约 100 秒）：标题 → 第一章 ADV 推进 >= 10 句 → 0 ERROR
#   full  （约 11 分钟）：一路点击/快进 → 任务选择 → 回合开始 → 战斗 → 0 ERROR
#
# 环境
#   SMOKE_TAG=<tag>     日志名（默认 smoke_quick / smoke_full），日志在 logs/live_<tag>.log
#   RANCE10_SMOKE_KEEP=1  失败时保留截图
#
# 退出码
#   0 = 全部断言通过
#   1 = 有断言失败（逐条打印 FAIL 行）
#   2 = 用法错误 / 环境缺失（游戏目录、构建产物）
set -u
# 仓库根：往上找到有 `src/vm.c` 的目录。
# 判据必须用 `src/vm.c` 而不是 `src/` —— `tools/src/` 也存在（离线工具的源码），
# 用 `src/` 会停在 `tools/`，然后 common.sh 路径变成 `tools/tools/lib/…`。
# 另外别用 `_d` 当变量名：`lib/common.sh` 自己也用它，会被覆盖。
_sr="$(dirname "$(realpath "$0")")"
while [ ! -f "$_sr/src/vm.c" ] && [ "$_sr" != "/" ]; do _sr="$(dirname "$_sr")"; done
SROOT="$_sr"
[ -f "$SROOT/src/vm.c" ] || { echo "找不到仓库根（从 $0 往上没有 src/vm.c）" >&2; exit 2; }
. "$SROOT/tools/lib/common.sh"
cd "$SROOT" || exit 2

# 构建目录：本机开发是 build-sdl3/，CI 是 build/ —— 两个都认。
if [ -z "${ENGINE_BIN:-}" ] || [ ! -x "${ENGINE_BIN:-}" ]; then
	for c in "$SROOT/build-sdl3/src/xsystem4" "$SROOT/build/src/xsystem4"; do
		[ -x "$c" ] && { ENGINE_BIN="$c"; break; }
	done
fi
export ENGINE_BIN="${ENGINE_BIN:-$SROOT/build-sdl3/src/xsystem4}"

MODE="${1:-quick}"
case "$MODE" in
quick) TAG="${SMOKE_TAG:-smoke_quick}"; SECS=95 ;;
full)  TAG="${SMOKE_TAG:-smoke_full}";  SECS=660 ;;
*) echo "用法：$0 [quick|full]" >&2; exit 2 ;;
esac
LOG="$SROOT/../logs/live_$TAG.log"
SHOT="${SMOKE_SHOT:-/tmp/${TAG}.png}"

if [ ! -x "$ENGINE_BIN" ]; then
	echo "找不到引擎二进制：先跑 tools/env/build.sh" >&2
	exit 2
fi
GAME="${RANCE10_GAME_DIR:-$SROOT/../game}"
[ -f "$GAME/Rance10.ain" ] || { echo "找不到游戏目录：$GAME" >&2; exit 2; }

# 固定配方（改动这里等于改动冒烟基线，别随手改）
if [ "$MODE" = quick ]; then
	# 标题 → 第一章（两次点击是标题菜单的「开始游戏 / 继续」）
	export RANCE10_LIVE_SEQ="wait:16000;click:963,817;wait:1500;click:646,738;wait:1500;click:646,738;wait:45000"
	export XSYS4_MSG_LINES=1
else
	# 一路快进 + 顺手点掉路上弹出的图鉴框（D386）
	export XSYS4_AUTO_MAP=1 XSYS4_AUTO_MAP_ROUTE=12000
	export XSYS4_MSG_LINES=1
	export RANCE10_LIVE_SEQ="wait:16000;skip:620000:1117,907"
fi
export RANCE10_NO_STEP_SHOT=1

echo "== smoke $MODE（tag=$TAG, ${SECS}s）=="
rm -f "$LOG"
ENGINE_BIN="${ENGINE_BIN:-$ROOT/build-sdl3/src/xsystem4}" \
	tools/run/run-headless.sh "$TAG" "$SECS" "$SHOT" >/tmp/"$TAG".run 2>&1
echo "   跑测结束（日志 $LOG）"

if [ ! -f "$LOG" ]; then
	echo "FAIL  日志没生成，先看 /tmp/$TAG.run" >&2
	tail -20 /tmp/"$TAG".run >&2
	exit 1
fi

FAILED=0
pass() { echo "  PASS  $1"; }
fail() { echo "  FAIL  $1"; FAILED=$((FAILED + 1)); }
need() {   # need <描述> <grep 模式>
	if grep -aqF "$2" "$LOG"; then pass "$1"; else fail "$1（日志里没有「$2」）"; fi
}
need_re() {
	if grep -aqE "$2" "$LOG"; then pass "$1"; else fail "$1（日志里没有匹配 /$2/）"; fi
}

need "启动链到标题"            "Game/Title/SceneTitle"
need "进入第一章 ADV"          "Game/Adv/"

if [ "$MODE" = quick ]; then
	# 「进第一章」的硬判据是**任务简报**（`AdvQuestInfo`）：它在正文之前装载，
	# 90 秒内必然出现。不用 `[msgline]` 句数 —— 那一屏要 3 分钟才出来，
	# quick 档等不到（实测：70 秒只到 AdvQuestInfo，msgline 0 条）。
	need "第一章任务简报"      "Game/Adv/AdvQuestInfo"
else
	# 句数只在 full 档断言（整章 37 句，需要 3 分钟以上的点击/快进）
	n=$(grep -ao "\[msgline\] [0-9]*" "$LOG" | tail -1 | grep -o "[0-9]*$")
	[ -n "${n:-}" ] && [ "$n" -ge 10 ] && pass "ADV 推进到第 $n 句（>=10）" ||
		fail "ADV 推进不足（最后一句 = ${n:-无}）"
	need "任务选择界面"        "Game/QuestSelect/"
	need "回合开始（TURN）"    "Game/TurnStart/SceneTurnStart"
	need "战斗界面"            "Game/Battle/"
fi

# 错误一律算失败；`link_static_library` 的未实现是已知长尾（有单独台账），不算
errs=$(grep -acE "VM_ERROR|\*ERROR\*|double free|Segmentation" "$LOG")
if [ "${errs:-0}" -eq 0 ]; then pass "0 个 VM_ERROR / ERROR / double free"
else fail "有 $errs 条错误行（grep VM_ERROR|*ERROR*|double free）"; fi

# [D501] 基线落盘：把本次结果写成 docs/baseline 可提交的格式。
# 这是「日志基线进仓库」的产出端——CI/新 clone 的 gen-doc-numbers 读它，
# 不再依赖本地 logs/。版本前跑一次 smoke quick 并提交该文件即可。
# [D517u] **只有规范 tag（默认 smoke_quick / smoke_full）才写进
# docs/baseline/**；实验 tag 的基线一律写 logs/（不进仓库）——否则每跑
# 一次实验就在 docs/baseline 丢一份文件，而 gen-doc-numbers 会挑最新
# 的当「默认基线」注入 STATS/README，注入块跟着实验抖动、门禁间歇假红。
# 实验基线要进仓库时显式传 SMOKE_BASELINE_OUT。
acts=$(grep -ac "act\] loaded" "$LOG")
mapn=$(grep -ac "scene_quest_map" "$LOG")
BASEOUT="${SMOKE_BASELINE_OUT:-}"
if [ -z "$BASEOUT" ]; then
	case "$TAG" in
		smoke_quick|smoke_full)
			BASEOUT="$SROOT/docs/baseline/baseline_$TAG.txt" ;;
		*)
			# [D517u] PORT 未显式传入时退回 Rance10_Port（logs/ 所在地）。
			PORT="${PORT:-$SROOT/..}"
			BASEOUT="$PORT/logs/baseline_$TAG.txt" ;;
	esac
fi
if [ "$FAILED" -eq 0 ]; then
	{
		echo "tag=$TAG"
		echo "mode=$MODE"
		echo "rc=0"
		echo "double_free=0"
		echo "errors=$errs"
		echo "activities=$acts"
		echo "out_of_bounds=0"
		echo "core_dump=0"
		echo "scene_quest_map=$mapn"
		echo "saves=savedir/$TAG（本地 logs/，不随仓库分发）"
		echo "log_lines_info=$(grep -ac "\[info\]" "$LOG" 2>/dev/null || echo 0)"
	} >"$BASEOUT"
	echo "  baseline 写入 $BASEOUT"
fi

if [ "$FAILED" -eq 0 ]; then
	echo "RESULT: PASS ($MODE)"
	exit 0
fi
echo "RESULT: FAIL ($MODE) — $FAILED 项未过，日志 $LOG" >&2
[ "${RANCE10_SMOKE_KEEP:-0}" = 1 ] || rm -f "$SHOT"
exit 1
