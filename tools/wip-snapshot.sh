#!/usr/bin/env bash
# WIP 快照分支 —— 《复杂底层项目的工程管理》§6 ⑩ / 缺口 9
#
# 每天收工把工作树存成一个 wip/ 分支，防止「功能未完成不提交」导致
# 十几轮实验在环境丢失后没有任何版本记录。正式提交仍保持整洁。
#
# 用法：
#   tools/wip-snapshot.sh                        # 本地 wip/YYYYMMDD-HHMM
#   tools/wip-snapshot.sh before-h022           # 自定义标签
#   tools/wip-snapshot.sh before-h022 --push    # 额外推到 origin（显式 refspec）
#
# 硬约束（对应 §3.11 提交与分支纪律）：
#   - 不 checkout、不改工作树、不改当前分支、不产生 stash 记录；
#   - 不混进 main、不当成交付物；推送只用显式 refspec，不用普通 git push；
#   - subprojects/libsys4 远端无写权限，gitlink 必须继续指向上游 commit；
#     子模块工作树改动靠 patches/*.patch 入库，绝不在快照里记本地提交。
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

label="${1:-$(date +%Y%m%d-%H%M)}"
branch="wip/${label}"
do_push=0
for a in "$@"; do [ "$a" = "--push" ] && do_push=1; done

# 分支已存在时不重建；只在显式 --push 时把它推上去。
if git show-ref --verify --quiet "refs/heads/${branch}"; then
    if [ "${do_push}" = 1 ]; then
        git push origin "refs/heads/${branch}:refs/heads/${branch}"
        echo "pushed existing ${branch} -> origin/${branch}"
        exit 0
    fi
    echo "ERROR: ${branch} 已存在。换标签，或先删除：git branch -D ${branch}" >&2
    exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# 独立索引做快照：read-tree HEAD -> add -A -> write-tree，全程不碰工作树。
# .gitignore 仍生效，build/ 产物与 savedir/ 不会进快照。
export GIT_INDEX_FILE="${tmp}/index"
git read-tree HEAD
git add -A -- .
tree="$(git write-tree)"
unset GIT_INDEX_FILE

head_short="$(git rev-parse --short HEAD)"
status="$(git status --porcelain || true)"
status="${status:0:4000}"
sub_status="$(git -C subprojects/libsys4 status --short 2>/dev/null || true)"
sub_status="${sub_status:0:1200}"

msg="${tmp}/msg"
{
    echo "WIP snapshot ${label} (from ${head_short})"
    echo
    echo "工作树状态（生成时，截断 4000 字符）："
    echo "${status:-(clean)}"
    echo
    echo "subprojects/libsys4 工作树状态（不进 gitlink，改动以 patches/ 为准）："
    echo "${sub_status:-(clean)}"
    echo
    echo "查看差异： git diff HEAD..${branch}"
    echo "取回单文件： git checkout ${branch} -- <path>"
    echo "删除快照： git branch -D ${branch}"
} > "${msg}"

commit="$(git commit-tree "${tree}" -p HEAD -F "${msg}")"
git update-ref "refs/heads/${branch}" "${commit}"

echo "created ${branch} -> ${commit:0:8} (tree ${tree:0:8})"
echo "  diff: $(git diff --shortstat HEAD "${branch}")"
echo "  HEAD/当前分支未改动：$(git symbolic-ref --short HEAD)"

if [ "${do_push}" = 1 ]; then
    # 显式 refspec：只推 WIP 分支，绝不触碰 main / rance10-native。
    git push origin "${commit}:refs/heads/${branch}"
    echo "pushed origin/${branch}（仅备份，不是交付物）"
else
    echo "本地快照完成。异地备份：tools/wip-snapshot.sh ${label} --push"
fi
