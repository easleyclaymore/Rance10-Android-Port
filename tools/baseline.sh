#!/bin/bash
# baseline.sh [tag] [seconds]
#
# 「全默认配置」基线：清掉所有 XSYS4_* 诊断变量，用固定序列跑一次无头
# 跑测，输出一组可直接 diff 的计数型指标。
#
# 为什么要单独一条命令：取证期间会随手设 XSYS4_DF_DBG / XSYS4_SLOT_ID /
# XSYS4_ARRAY_REF_FIX 等开关，A/B 对照若忘了清，两组跑的就不是同一个
# 配置。交付判据必须是"什么开关都不开"的那一组。
set -u
TAG="${1:-base}"
SECS="${2:-120}"
HERE="$(cd "$(dirname "$(realpath "$0")")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

# RANCE10_BASE_KEEP="XSYS4_FOO XSYS4_BAR" lets a controlled A/B switch
# through while still clearing every other diagnostic variable.
KEEP="${RANCE10_BASE_KEEP:-}"
for v in $(env | sed -n 's/^\(XSYS4_[A-Za-z0-9_]*\)=.*/\1/p' | sort -u); do
    case " $KEEP " in *" $v "*) continue ;; esac
    unset "$v"
done

# [L0] 进地图要重试：标题菜单出画时刻受贴图加载影响，单次点击命中率实测 ~30%，
# 一次跑测 3 分钟，赌不起。clickunless 只在"还没到目标场景"时点，
# 进了就不再点（避免多点一下打开别的面板）。
export RANCE10_LIVE_SEQ="${RANCE10_BASE_SEQ:-wait:2500;clickunless:646,738:SceneQuestMap;until:SceneQuestMap:12000;clickunless:646,738:SceneQuestMap;until:SceneQuestMap:12000;clickunless:646,738:SceneQuestMap;until:SceneQuestMap:12000;clickunless:646,738:SceneQuestMap;until:SceneQuestMap:15000;wait:15000}"
# [L0] The save directory is part of the fixture, not ambient state. Each tag
# gets its own clean dir under logs/savedir/; point RANCE10_BASE_SAVE_DIR at an
# existing one to verify a write -> read round trip across two runs.
SAVE_DIR="${RANCE10_BASE_SAVE_DIR:-$ROOT/logs/savedir/$TAG}"
export RANCE10_SAVE_DIR="$SAVE_DIR"
SHOT="${RANCE10_BASE_SHOT:-/tmp/baseline_${TAG}.png}"
LOG="$ROOT/logs/live_${TAG}.log"
OUT="$ROOT/logs/baseline_${TAG}.txt"

"$HERE/run-headless.sh" "$TAG" "$SECS" "$SHOT" > "/tmp/baseline_${TAG}.stdout" 2>&1
RC=$?

# grep -c prints "0" but exits 1 when there is no match; `|| echo 0` would
# therefore append a SECOND zero line and corrupt the metric. Capture first,
# then default once. (L0: the measurement itself must be trustworthy.)
count() { local n; n=$(grep -ac "$1" "$LOG" 2>/dev/null); printf '%s' "${n:-0}"; }
{
    printf 'tag=%s\n' "$TAG"
    printf 'rc=%s\n' "$RC"
    printf 'double_free=%s\n' "$(count 'double free of slot')"
    printf 'errors=%s\n' "$(count '\*ERROR\*')"
    printf 'activities=%s\n' "$(count '\[act\] loaded')"
    printf 'out_of_bounds=%s\n' "$(count 'Out of bounds')"
    printf 'core_dump=%s\n' "$(count '核心转储\|Segmentation\|core dump')"
    printf 'scene_quest_map=%s\n' "$(count 'SceneQuestMap')"
    # Fixture visibility: which save dir was used and how many save files the
    # run produced. A silently stale save dir poisons every other metric.
    printf 'saves=%s\n' "$SAVE_DIR"
    printf 'save_files=%s\n' "$(ls -1 "$SAVE_DIR" 2>/dev/null | wc -l)"
    # Informational only: total log lines track frame count, which varies
    # with CPU scheduling. Do NOT use it as a pass/fail metric.
    printf 'log_lines_info=%s\n' "$(wc -l < "$LOG" 2>/dev/null || echo 0)"
} | tee "$OUT"

echo "BASELINE_FILE=$OUT"
echo "LOG=$LOG"
exit "$RC"
