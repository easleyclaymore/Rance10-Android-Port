#!/bin/sh
# 日志/存档卫生：把跑测残留压到可控体积。
#
# 背景：每次 run-headless.sh 都会写一份 logs/live_<tag>.log 和一份
# logs/savedir/<tag>/。一天几十次跑测就是几十 MB 到上百 MB；不设上限的话
# 一个月能到几个 GB（D318 盘点：2180 个文件 / 222M，其中 savedir 91M）。
#
# 策略（都是"只碰明显过期的"）：
#   *.log   > 2 天  -> gzip -9（保留全文，仍然可 grep 前解压）
#   *.gz    > 30 天 -> 删除（那时通常是已结清的旧取证）
#   savedir/* > 7 天 -> 删除（跑测存档，不含游戏本体数据）
#
# 默认只打印报告，不删不改；确认后加 --apply。
#
#   sh tools/run/loghouse.sh            # 报告
#   sh tools/run/loghouse.sh --apply    # 执行

set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
LOGS="$ROOT/logs"
APPLY=0
[ "${1:-}" = "--apply" ] && APPLY=1

[ -d "$LOGS" ] || { echo "没有 logs 目录：$LOGS"; exit 0; }

cd "$LOGS"

printf '== 现状 ==\n'
printf '  总计        %s\n' "$(du -sh . | cut -f1)"
printf '  *.log       %s 个 %s\n' "$(ls *.log 2>/dev/null | wc -l | tr -d ' ')" "$(du -ch *.log 2>/dev/null | tail -1 | cut -f1)"
printf '  *.gz        %s 个 %s\n' "$(ls *.gz 2>/dev/null | wc -l | tr -d ' ')" "$(du -ch *.gz 2>/dev/null | tail -1 | cut -f1)"
printf '  savedir     %s 个 %s\n' "$(ls savedir 2>/dev/null | wc -l | tr -d ' ')" "$(du -sh savedir 2>/dev/null | cut -f1)"

OLD_LOG=$(find . -maxdepth 1 -name '*.log' -mtime +2 | wc -l | tr -d ' ')
OLD_GZ=$(find . -maxdepth 1 -name '*.gz' -mtime +30 | wc -l | tr -d ' ')
OLD_DIR=$(find savedir -mindepth 1 -maxdepth 1 -type d -mtime +7 2>/dev/null | wc -l | tr -d ' ')

printf '\n== 待处理 ==\n'
printf '  *.log  >2 天  -> gzip   %s 个\n' "$OLD_LOG"
printf '  *.gz   >30 天 -> 删除   %s 个\n' "$OLD_GZ"
printf '  savedir >7 天 -> 删除   %s 个\n' "$OLD_DIR"

if [ "$APPLY" -eq 0 ]; then
	printf '\n（未执行。确认后：sh tools/run/loghouse.sh --apply）\n'
	exit 0
fi

printf '\n== 执行 ==\n'
find . -maxdepth 1 -name '*.log' -mtime +2 -print0 | xargs -0 -r gzip -9
find . -maxdepth 1 -name '*.gz' -mtime +30 -print0 | xargs -0 -r rm -f
find savedir -mindepth 1 -maxdepth 1 -type d -mtime +7 -print0 2>/dev/null | xargs -0 -r rm -rf

printf '  处理后总计  %s\n' "$(du -sh . | cut -f1)"
printf '  处理后文件  %s 个\n' "$(find . -maxdepth 1 -type f | wc -l | tr -d ' ')"
