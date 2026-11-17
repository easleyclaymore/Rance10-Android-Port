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
| `I011` | D216 EX 比率坐标（`activity.c:parse_part_fields/build_part`） | **全局**：所有标了「座標を率として認識する=1」的 EX 部件；Rance10 里已知 `MessageWindow01/02/01_talk` 的 `SYS_消息窗口`，坐标由 `(0,1)` 变 `(0,1080)`（配合 `原点座標模式=7` 贴屏幕底边） | 默认零开关基线（`d216`: `rc=0 / activities=36 / errors=0 / core_dump=0 / quest_map=1 / double_free=0 / save_files=8`）+ ADV 截图首现正文 | `XSYS4_EX_RATIO_POS=0` 即时回退；`git revert` 本轮提交 | 通过（与 D214 基线逐项一致） |
| `I012` | D217 EX 消息窗口排版参数 + 独立底板 CG 部件（`activity.c`、`render.c`） | 仅 `ACT_COMP_MESSAGE_WINDOW` 类型部件；每次装载多分配一个 CG 部件号（后续动态号整体 +1），字体由默认 16px 变 39px | 默认零开关基线（`d217`: `rc=0 / activities=36 / errors=0 / core_dump=0 / quest_map=1 / double_free=0 / save_files=8`）+ **完整第一章真实流程**（`d217full`: `Game/Battle/*` 31 个活动、`errors=0 / core_dump=0 / 运行期未实现 HLL=0`、背景 CG + 消息框 + 正文出图） | `XSYS4_MSGWIN_FULL=0` 即时回退；`git revert` 本轮提交 | 通过 |
| `I013` | D219 `PE_ReleaseParts` 递归释放子树（`parts.c`）+ `run-headless.sh` display 原子锁 | AIN 每次 `ReleaseParts` 会连带释放整棵部件子树，旧层残留部件减少；并行无头跑测不再抢同一个 Xvfb display | 默认零开关基线（`d219b`: `rc=0 / activities=36 / errors=0 / core_dump=0 / double_free=0 / quest_map=1 / save_files=8 / log_lines=720`，与 D217 逐项一致）+ 干净真实流程（`h025fix2`: `Game/Battle/*` 32、活动 108、`errors=0 / core_dump=0 / out_of_bounds=0`）+ `bash -n` 与锁回收 | `XSYS4_RELEASE_RECURSIVE=0` 即时回退；`git revert` 本轮提交 | 通过（H025 部分改善） |
| `I014` | D220 `cover_layer` 淡出层 A/B（`XSYS4_COVER_IGNORE_SHOW`） | 仅 `parts_update_cover_layer()` 的 opaque 统计；不改部件、不改存档 | 默认零开关基线 `d220`（四数与 D219 逐项一致、截图非黑 71.16%）+ 干净真实流程 `h033fix`（`Game/Battle/*` 32、零错误、零崩溃） | `XSYS4_COVER_IGNORE_SHOW=0`；代码已整段回退，工作树与 `18d4052` 一致 | **已回退（证伪：全帧 0 像素差异）** |
| `I015` | D222 取证链加固：`run-live.sh:shot_valid()/shot()` 空图校验与重试、`screenshot.c:XSYS4_SHOT_DIR` 引擎自截图、`check_core_deps.py:check_libsys4_patches()` 子模块补丁校验 | 仅跑测脚本 / Alt+S 截图路径 / 门禁；引擎渲染与存档字节不变，两个新开关默认关闭，不设 `XSYS4_SHOT_DIR` 时 `screenshot_save()` 仍走原 zenity 路径 | 默认零开关基线 `d222` 与 D221 逐项一致（`rc=0 / activities=36 / errors=0 / core_dump=0 / double_free=0 / quest_map=1 / save_files=8 / log_lines=720`）+ 门禁负向自检（忘 apply → 4 problem；已 apply → 4 段 verified / 0 problem）+ `bash -n` | 三处互不依赖，可分别 `git revert` | 通过 |
| `I016` | D223 `vm_frame_pump()` 强制每帧提交（`XSYS4_FRAME_PUMP_DIRTY`） | 全局帧心跳；每 4096 条指令多一次 `scene_render()+gfx_swap()` | 基线 `d223` 四数偏离历史（`activities=13 / scene_quest_map=0 / save_files=7 / log_lines=685`）、真实流程卡在 ADV 且自截图 0 张 → vsync 阻塞拖慢 VM，固定时序点击全部失效 | 代码已整段回退、开关登记已删；`d223b` 基线复核 | **已回退（证伪）** |
| `I017` | D223 `screenshot.c:XSYS4_SHOT_PARTS_DUMP` 同帧部件快照 | 仅当 `XSYS4_SHOT_DIR` 与本开关同时开启、且按 Alt+S 时触发**一次** `PE_DebugDumpParts("shot-engine")`；默认全关，不改帧率 | `h036probe` 取到黑屏帧 628 / 639 个部件快照（含 `layer_show` / `cover` / `vis`），且 `battle=32 / acts=108 / replay=1` 与对照组一致，证明不拖慢剧情 | 删开关即回退；`git revert` 本轮提交 | 通过 |
| `I018` | D224 `PE_SetAlpha()` 收窄 D188 提升：加 `common.w>=8 && h>=8` 才把 CP 部件的 `alpha<=0` 抬回 255（`parts.c`）；`render.c` / `screenshot.c` 新增同帧 CG 绘制日志（默认全关） | 仅影响 `PARTS_CONSTRUCTION_PROCESS` 且纹理 <8x8 的单色遮罩；卡片号段 `900000700-799` 的 alpha 分布前后完全一致（均为 0、120 个），无误伤 | 默认零开关基线 `d224` 与 D223 逐项一致（`rc=0 / activities=36 / errors=0 / core_dump=0 / double_free=0 / quest_map=1 / save_files=8 / log_lines=720`）+ 真实流程 `h037fix`（`battle=32 / acts=108 / replay=1 / errors=0 / core=0 / oob=0 / df=0`，`shot_002` **10579B 全黑 → 1207952B 有画面**，VS 对阵 + 战斗 UI 出图） | `XSYS4_NO_CP_ALPHA_FIX=1` 整体回退；`git revert` 本轮提交 | 通过 |
| `I019` | D226 `parts_flat_max_paint_alpha()`（`flat.c`）+ 遮挡推断按它收紧（`parts.c`）；`PE_DebugDumpParts` 增 flat 加载事实行、地板日志增 `flatmax=` | 所有整屏 flat 部件的地板判定；只影响 `PARTS_FLAT`，`XSYS4_NO_LAYER_COVER=1` 仍整体旁路 | 默认零开关基线 `d227` 与 `d224` 逐项一致；战斗两帧地板 `flatmax=255` 与旧口径同结论（该例非幽灵地板）；H039 被证伪 | 删判定即回退（回到只看名义 alpha）；`git revert` | 通过（契约修正，未改变本卡点） |
| `I020` | D227 `vm_call_hll_closure()` 对**借来的按值实参**补 `heap_ref`（`vm.c`） | 所有 HLL→AIN 回调：`Array.Find/Where/Any/All/First/BinarySearch` 的谓词、点击回调、`AUTO_*` 后门；引用计数平衡从"少一次"变"正好" | 默认零开关基线 `d227` 四数同 `d224`；同配方（`h40b` → `h41a`）不再出现 `Array_Find_ → heap_unref` core；`ERROR=0` | `git revert` 本轮提交（会重新暴露悬空字符串崩溃） | 通过 |
| `I021` | D229 `parts_update_mouse()` 的 `click_eligible` 加「必须真的可见（alpha>0）」 | 所有部件命中：不可点击且透明的部件不再抢点击，理论上让点击穿透到下层 | **证伪回退**：开场回想对话框的「以后再看」按钮是 `ck=0/alpha≈0` 的用户组件，改完点不动；同配方活动数 104→51、卡在 `ReplayMemoDialog`（h44a/h44b），回退后 h44c 恢复 104 | 已整段回退（代码只剩一行原判断） | **已回退（证伪）** |
| `I022` | D232 `flat_layer_paint_alpha()`：子时间线容器关键帧不再贡献 alpha，只递归子层（`flat.c`） | 所有铺满屏幕的演出 flat 的 cover 判定；`PARTS_FLAT` 专用，`XSYS4_NO_LAYER_COVER=1` 仍整体旁路 | 默认基线 `d233` 与 `d224` 逐项一致（rc=0 / activities=36 / errors=0 / core_dump=0 / double_free=0 / quest_map=1 / save_files=8 / log_lines=720）；同配方四张自截图 10579B → 2157158B | 删判定即回退（回到看名义 alpha，黑屏立刻复现） | 通过（真修） |
| `I023` | D233 `input.c` 的「先被看见再抬起」：合成按键的 press 保持到被 `key_is_down` 读到，release 挂起直到读过再抬（兜底 300ms）；`sact_Key_IsDown` 另有一次脉冲兜底 | 所有轮询 `Key_IsDown` 的 AIN 代码（`CASClick` 的右键/空格等待、`DALKDemo`、`InputDevice`）；左键的 `cur_clicking` 也因此能稳定跨一帧 | 默认基线 `d233` 与 `d224` 逐项一致；`XSYS4_KEY_DBG` 下 `IsDown(2)=1` / `IsDown(32)=1` 由 0 次变为可观测；Ctrl 长按快进不受影响（真实按住时 `seen_down` 立即置位，释放即刻生效） | `git revert`（回到同帧 press+release 被吞） | 通过 |
| `I024` | D236 帧泵触发从「每 4096 条指令」改为按墙钟（每 64 条检查，闸门仍是 16ms）| 所有纯 AIN 等待循环（`WaitForClick` / `WaitFrame`）里的动画时钟、motion 时钟、输入泵| 默认基线 `d237` 与 `d224` 逐项一致；同配方 900001059 播完时间 40s+ → 1–2s| `XSYS4_PUMP_MASK=0xFFF` 可退回旧行为 | 通过 |
| `I025` | D237 `PE_IsPartsFlatEnd()` 在 flat 未装载时返回 false（原为 true）| 所有用「flat 播完了吗」做等待条件的调用方（`SceneFlat@OnUpdate`、`Parts@IsMotionFlat`）| 默认基线 `d237` 与 `d224` 逐项一致；契约修正本身**未**消除 1200 次 Exit（flat 确实播完了），但去掉了「一创建就退出」这一类错误 | `git revert`（回到未装载=已结束） | 通过（契约修正） |
| `I026` | D238 三处纯诊断修正：① `parts/input.c` `[inp]` 计数器改每帧自增（限流探针不再永久静音）；② `activity.c` 未识别部件类型时打出类型串 + 新增 `XSYS4_PART_TYPE_DBG` 打 `种类別情报` 子键名；③ `XSYS4_INPUT_DBG` 心跳条件不变 | 只影响日志，**不改变任何渲染/输入判定**。②是"数字/计量部件"这条债的入口：靠它才读出 `数字パーツ` / `横向计量パーツ` 两个真实类型名 | 默认零开关基线 `d238` 与 `d224` 逐项一致（`rc=0 / activities=36 / errors=0 / core_dump=0 / double_free=0 / quest_map=1 / save_files=8 / log_lines=720`）；`XSYS4_INPUT_DBG` 下从"前 40 帧后永久沉默"变成每 60 帧一条心跳 | `git revert`（只是日志） | 通过 |
| `I027` | D238 两处语义修复（`6c568b7` + `3ac9738`）：① `component_type_from_name()` 精确匹配失败后剥 `パーツ` 后缀重查；② `activity.c` 解析数字/计量负载（`幅リスト/数值/字间距/桁数`、`分子/分母`）并调 `SetNumeralLinkedCG…` / `SetHGaugeCG+Rate` | ① **所有** EX 里以 `パーツ` 结尾的类型名（此前一律回退成 CG——包括地图 HP 视图 `QuestMapHpView`）；② 仅 `ACT_COMP_NUMERAL/HGAUGE/VGAUGE` 三类部件 | 默认零开关基线 `d238` 四数不变；`s13` 引擎自截图 `Round 1 / 10` + 红蓝 HP 条；`check_core_deps.py` PASS（hyp=44 roots=62 debts=25） | `git revert 3ac9738`（渲染）/ `git revert 6c568b7`（类型映射），两者独立 | 通过（`s13`） |

| `I028` | D239 `flat.c` `PE_GoFramePartsFlat()` 向前定位分支补 `root_state->stopped = false` | **所有**用 GoFrame 向前跳帧的 flat（战斗开始演出、任何停过 `is_stop` 的动画）；向后定位原本就清，语义不变 | 默认基线 `d238` 四数；`s20` 战斗开始演出播完、画面进入战斗 UI | `git revert`（回到「delta 被空转、帧不动」） | 未验（`s20/s22` 被崩溃阻断） |
| `I029` | D241 `heap.c` `heap_unref()` 加上界检查（`slot >= heap_size` 时忽略 + 限流告警） | **所有** unref 路径（引用计数族全量）——热路径加一次整数比较；非法槽从「越界 SIGSEGV」变成「忽略」 | 默认基线 `d238` 四数；`s23` 同 s22 配方不再崩溃 | `git revert`（回到越界崩） | 通过（`s26`：越界被忽略 `heap_size=110592`，同配方不再段错误）；根因另见 `[BAD-SLOT-REF]` |

| `I031` | D286 新增 `XSYS4_HLL_HOT` 全库计数（`ffi.c`）+ `tools/dev/hll_hotness.py` + `tools/run/matrix.sh` 场景矩阵 | 只读统计与跑测装置，**不改任何运行时语义**；`hll_call()` 入口多一次 `hll_hot_note()`（未开开关时是一次 `static` 判断就返回） | 默认零开关基线四数不变（`d304`: `acts=36 / err=0 / core=0 / quest_map=1 / rc=0`）；`xsys4 check` + `gen-doc-numbers.py --check` PASS | 纯加性：删开关/删两个脚本即回退；`git revert` 本轮提交 | 通过 |
| `I030` | D245 `tools/run/run-live.sh` `click()` 的 `mousedown→mouseup` 间隔 `0.12s → 0.2s` | **所有**跑测点击；只影响跑测装置，不影响引擎行为 | `s37`（0.12s）40 次点击 9 次 `EndInput`；`s38`（0.2s）40 次点击 15 次——都零 assert/core | 改回 `0.12` |

> 判据：每个「必重跑闭环」都要有当次实测数字或日志 tag；`待验` 条目收工前必须结掉。
> 全部数字以 `docs/STATS.md`（`tools/gen-doc-numbers.py` 生成）为准，禁止手写。
