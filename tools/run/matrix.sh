#!/bin/bash
# matrix.sh [基线标签] [节点列表]
#
# 场景矩阵：同一组判据扫多个地图节点，输出一份可直接 diff 的表。
#
# 为什么需要它
# ============
# 现在所有验收都靠**单条**跑测序列，最多到「战斗第一回合」。而 Rance10 是
# 37698 个 AIN 函数 + 52 个数据块（ＭＡＰ数据 2MB / 障碍物数据 3MB /
# 任务数据 1.1MB）的体量，我们实际跑过的活动只有 7 个目录。
#
# 后果：基线「0 ERROR / 0 core」只覆盖了游戏的一小部分。每开新章节大概率
# 撞新函数，而每次都要重新做跑测装置、重新人肉判断 —— 成本无法累积。
#
# 这个脚本把「基线」从一条序列升级成**矩阵**：每个节点跑同一组判据，
# 「今天修好了什么」不再靠人记，而是矩阵里哪一格变了。
#
# 判据（每个节点一行，全部可机器比较）
# ------------------------------------
#   rc        进程退出码（非 0 = 崩了或被杀）
#   err       `*ERROR*` 条数
#   core      `核心转储|Segmentation|core dump` 条数
#   df        `double free of slot` 条数
#   oob       `Out of bounds` 条数
#   acts      `[act] loaded` 条数
#   unimpl    运行期命中的未实现 HLL 数（见下方「热缺口」）
#   scenes    装载过的场景名（去重、排序后取前 6 个，用来看"走到哪")
#   black     末帧非黑像素比例（用引擎自截图，判断"画面有没有内容"）
#
# 关于 unimpl：启动期会为每个未实现导出打一行，那是**静态**缺口（544 条，
# 绝大部分永不执行）。真正要盯的是运行期命中 —— 用 `XSYS4_HLL_HOT=1`
# 的 `[hllhot]` 表交叉比对，见 `tools/dev/hll_hotness.py`。
#
# 用法
# ----
#   tools/run/matrix.sh m1              # 默认扫 1..4 节点
#   tools/run/matrix.sh m1 1,2,3,5,7    # 指定节点
#   MATRIX_SECS=100 tools/run/matrix.sh m1 1,2
#
# 输出：logs/matrix_<tag>.txt（表格）+ 每节点 logs/live_<tag>_n<N>.log
# 与上一份矩阵对比：`diff logs/matrix_a.txt logs/matrix_b.txt`
set -u

TAG="${1:-matrix}"
NODES="${2:-1,2,3,4}"
SECS="${MATRIX_SECS:-110}"

# [D170] 路径锚点
_d="$(dirname "$(realpath "$0")")"; while [ ! -f "$_d/lib/common.sh" ] && [ "$_d" != "/" ]; do _d="$(dirname "$_d")"; done; . "$_d/lib/common.sh"
HERE="$TOOLS_DIR"
ROOT="$PORT_DIR"
OUT="$LOGS_DIR/matrix_${TAG}.txt"

# 清掉所有 XSYS4_* 诊断变量：矩阵必须是「全默认配置」下的读数，
# 否则不同节点之间、不同日期之间的格子不可比。
for v in $(env | sed -n 's/^\(XSYS4_[A-Za-z0-9_]*\)=.*/\1/p' | sort -u); do
    unset "$v"
done

# 每个节点一份独立存档目录：共用会让前一个节点写的事件标记污染后一个。
node_run() {
    local n="$1"
    local tag="${TAG}_n${n}"
    local log="$LOGS_DIR/live_${tag}.log"
    local shot="/tmp/${tag}.png"

    # 节点推进：XSYS4_AUTO_MAP 进第一章地图，XSYS4_NODE_DIGIT 改 KeyBase 末位。
    # 序列只做「进地图 → 点两下推进 → 等」，不依赖任何具体按钮坐标，
    # 这样节点之间唯一变量就是那个末位数字。
    XSYS4_AUTO_MAP=1 \
    XSYS4_NODE_DIGIT="$n" \
    RANCE10_SAVE_DIR="$ROOT/logs/savedir/$tag" \
    RANCE10_LIVE_SEQ="wait:30000;key:Shift_L;wait:800;click:960,540;wait:5000;key:Shift_L;wait:800;click:960,300;wait:5000" \
    "$HERE/run/run-headless.sh" "$tag" "$SECS" "$shot" > "/tmp/${tag}.stdout" 2>&1
    echo $?
}

count() { local n; n=$(grep -ac "$2" "$1" 2>/dev/null); printf '%s' "${n:-0}"; }

# 末帧非黑像素比例。用引擎自截图（GL 直绘下 X11 抓屏会拿到空 framebuffer）。
paint_ratio() {
    local png="$1"
    [ -f "$png" ] || { printf 'n/a'; return; }
    python3 - "$png" <<'PY' 2>/dev/null || printf 'n/a'
import sys
try:
    from PIL import Image
except ImportError:
    print("nopil"); raise SystemExit
im = Image.open(sys.argv[1]).convert("RGB")
w, h = im.size
small = im.resize((160, 90))
px = list(small.getdata())
nb = sum(1 for p in px if p[0] + p[1] + p[2] > 30)
print("%.1f" % (100.0 * nb / len(px)))
PY
}

printf '# 场景矩阵 %s（全默认配置；SECS=%s）\n' "$TAG" "$SECS" > "$OUT"
printf '# 生成于 %s\n' "$(date '+%F %T')" >> "$OUT"
printf '%-4s %-4s %-5s %-5s %-4s %-5s %-5s %-7s %-6s %s\n' \
    node rc err core df oob acts unimpl black scenes >> "$OUT"

for n in ${NODES//,/ }; do
    rc=$(node_run "$n")
    log="$LOGS_DIR/live_${TAG}_n${n}.log"
    shot="/tmp/${TAG}_n${n}.png"
    err=$(count "$log" '\*ERROR\*')
    core=$(count "$log" '核心转储\|Segmentation\|core dump')
    df=$(count "$log" 'double free of slot')
    oob=$(count "$log" 'Out of bounds')
    acts=$(count "$log" '\[act\] loaded')
    # 运行期未实现：只数 `[stub]` 软桩命中（真被调用到的）
    unimpl=$(count "$log" '\[stub\]')
    black=$(paint_ratio "$shot")
    # 场景名：只取**行首**就是 `Game/` 的（UI 活动名是 Shift-JIS 字节，
    # 在 UTF-8 终端里是乱码，会把表格撞乱）。再剔掉动态后缀 `:0` / `:1`。
    scenes=$(grep -ao "^\[act\] loaded 'Game/[^']*'" "$log" 2>/dev/null \
             | sed "s/^\[act\] loaded '//; s/'$//" \
             | sed 's/:[0-9]*$//' | sort -u | head -6 | paste -sd, -)
    printf '%-4s %-4s %-5s %-5s %-4s %-5s %-5s %-7s %-6s %s\n' \
        "$n" "$rc" "$err" "$core" "$df" "$oob" "$acts" "$unimpl" "$black" "$scenes" >> "$OUT"
    printf '  node %s: rc=%s err=%s core=%s acts=%s unimpl=%s black=%s\n' \
        "$n" "$rc" "$err" "$core" "$acts" "$unimpl" "$black" >&2
done

echo
cat "$OUT"
echo
echo "MATRIX_FILE=$OUT"
