# 变更影响面清单

**用途**：《复杂底层项目的工程管理》§6 ⑥ / 缺口 10。全局语义改动**动手前**先写三行，
避免「改完才发现波及所有场景」——D199 曾计划改全局坐标语义，台账事后才补一句「会波及所有场景」。

## 规则

1. 一条改动一个 ID（`I001`…）。**只有全局语义 / 跨模块改动必填**；小修补不填，别加负担。
2. 三行内容：`会影响哪些场景` / `哪些已有闭环必须重跑` / `若失败怎么回退`。回退必须是可执行动作
   （开关 / 环境变量 / `git revert <commit>`），不写「重新检查」这种空话。
3. 证伪的实验也要留条目并写结论；A/B 开关必须**默认关**。
4. 本表只记影响面；根因与取证在 `TECH_DEBT.md` / `archive/`，假设在 `HYPOTHESES.md`。

## 模板

| ID | 改动（D 号 / 提交） | 影响场景 | 必重跑闭环 | 回退方式 | 结论 |
|---|---|---|---|---|---|
| `Ixxx` | | | | | 待验 / 通过 / 已回退 |

## 已登记

| ID | 改动 | 影响场景 | 必重跑闭环 | 回退方式 | 结论 |
|---|---|---|---|---|---|
| `I001` | D204 `hacks.c:resolve_v12_msgf`（`0c0fb38`） | 所有 AIN v12 且无 `MSGF` 段的游戏：`_MSG` 全 269677 条指令改走裸名 `message`/3 | 默认零开关基线 + 第一章 ADV 逐句推进 + 编队进出 + 回想对话框 | `XSYS4_MSGF_FIX=0`（内置 A/B，默认开） | 通过 |
| `I002` | D204 `AnteaterADVEngine.c:_PreLink` by-value 适配（`0c0fb38`） | 所有 `AnteaterADVLogList.AddText` 调用（ADV 日志 / 剧情回想） | 剧情走到 `AddText` 不 core + `ReplayMemoDialog` 装载 | `git revert 0c0fb38`，或按库签名条件跳过替换 | 通过 |
| `I003` | D205 `run-live.sh` 增加 `keydown:/keyup:`（`c9add54`） | 所有需要**按住**键的跑测序列；短促 `key:` 行为不变 | Ctrl 快进 60s 达 333 次 `SetMessageWindowText` | 加性变更：不使用新动作即等价旧行为 | 通过 |
| `I004` | D207 `input.c` 统一 `parts_hitbox_at()`（`736173b`） | **全场景所有部件**的点击命中与 `[inp]` 打印（本表最该填的一条） | 标题点击进地图、選択肢 / 编队按钮、对话框推进、默认基线四数 | `git revert 736173b`；`XSYS4_HITBOX_GLOBAL` 已证伪回退，默认行为不变 | 通过 |
| `I005` | D207 `baseline.sh` 标题坐标 `646,738 → 100,60`（`736173b`） | **所有**默认基线及其历史对比（旧值从未进过地图，两期读数不可直接比） | 默认零开关基线（`d207`: `acts=36 / quest_map=1 / err=0 / core=0`）+ 编队进出 | `RANCE10_BASE_SEQ=<旧序列>` 覆盖，或 `git revert 736173b` | 通过 |
| `I006` | D209 `XSYS4_NODE_DIGIT`：把 `QuestMapParamAccessor` KeyBase 从 `００１` 推到 `００２` | 任务地图**所有节点**的事件查询、存档 / 读档、回合推进；全局早改会在 `add_value_to_gsave` 断言崩溃，故限定 `23486` 链 | 默认基线 + `23489 → 26388 → Game/Battle*` + 零错误 | 开关默认关、**不改** `Rance10EX.ex`；`git revert` 本轮提交 | 通过（战斗链已验证） |
| `I007` | 三件基建：`IMPACT.md` / `STATS.md` / `gen-doc-numbers.py` / `wip-snapshot.sh` | 只影响文档与工具链，**不触碰运行时代码**；`wip/` 分支不进 `main`、不进交付历史 | `check_core_deps.py` PASS + `gen-doc-numbers.py --check` PASS + 默认基线四数不变 | 纯加性：`git revert` 对应提交；WIP 分支可直接删 | 通过 |
| `I008` | D209 补 `MainEXFile.Save/Load` + `AFAFactory` 搜索族 6 个导出 | 战斗前快照存档链（`gamesave::detail::存档実行` 5590/5591）与战斗 UI 的 `Parts@ResolveFlatName`；均为通用库，改动局部 | 进战斗零 `Unimplemented HLL` + VS 画面出图 + 默认基线无回归 | `git revert` 本轮提交；`MainEXFile` 空操作的前提是 EX 表只读（代码已注明） | 通过 |
| `I009` | D214 EX 布局盒初值改 `FREE` | **全局**：所有 EX 装载的布局盒/用户组件，标题菜单、回想对话框、選択肢、编队卡片列表的命中区与渲染坐标都会变；AIN 运行时显式设的布局类型不受影响 | 默认基线 + 标题/回想/選択肢画面 + `23489 → 26388 → Game/Battle*` + 零错误 | `XSYS4_EX_LAYOUT_FREE=0` 即时回退；`git revert` 代码提交；`baseline.sh` 标题坐标已同步为 `646,738` | 通过（`rc=0 / activities=36 / quest_map=1`） |
| `I010` | D215 gsave ARRAY 分支容错 | 全部存档/读档路径；只把「断言 abort」改成写 `unset` + 限流告警，不改有效数组的字节 | 战斗前自动存档不崩 + 默认基线 `double_free=0` + 存档文件可生成 | `git revert` 本轮提交；与 STRUCT 分支既有 L2 容错同口径 | 通过 |

> 判据：每个「必重跑闭环」都要有当次实测数字或日志 tag；`待验` 条目收工前必须结掉。
> 全部数字以 `docs/STATS.md`（`tools/gen-doc-numbers.py` 生成）为准，禁止手写。
