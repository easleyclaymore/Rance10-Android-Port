# 假设台账

**用途**：黑盒字节码移植里，最贵的不是修 bug，而是反复验证同一个错假设
（D193「按钮没有几何」、D199「坐标出屏」、D206「選択肢点不中」都是这样）。
这份台账把「提出 → 判据 → 验证 → 结论」固化下来，**证伪的假设不删除**。

## 规则

1. 一条假设一个 ID（`H001`…），状态只有 `待验 / 成立 / 证伪 / 被取代`。
2. 提出时必须同时写**可证伪判据**（观察到什么就说明它错）和**验证方式**；
   没有判据的直觉不算假设，不登记。
3. 证伪后必须写清**证据位置**（日志 tag / 反汇编地址 / 可复现命令），
   这样下一轮能一眼看出「这条别再查了」。
4. 结论被推翻时，在「改判链」里把旧 ID 指向新 ID，不静默改口。
5. 只记与**当前或近期卡点**有关的假设；历史全文留 `archive/`（只读）。

## 当前待验（按优先级）

| ID | 假设 | 可证伪判据 | 验证方式 | 状态 |
|---|---|---|---|---|
| `H014` | 消息窗口正文不出是部件 `show/alpha` 或文本纹理路径（`[UC-TEXSHADER]`）问题，与消息链无关 | `SetMessageWindowText` 收到非空文本但 `900000347` 的 `show/alpha` 正常且字形纹理有像素 | `XSYS4_ACT_PART_DBG` + 渲染准入探针 + 截图 | 待验 |
| `H023` | 正常游戏路径靠「选中相邻地图节点 / 结束回合」让 AIN 自己把 `QuestMapParamAccessor.this.m0` 从 `００１` 写成 `００２` | 不带 `XSYS4_NODE_DIGIT` 也出现 `...００２.战斗` 查询并命中 `23489` | 反汇编 `SceneQuestMap` 的节点选择 / 移动回调，或用 `XSYS4_INPUT_ALL` 找相邻节点命中区后点它 | 待验 |
| `H020` | `GetQuestEventList` 的 `S_NOTE` 判空或 `Array.PushBack` 在引擎侧有偏差，导致明明查到了值也没进事件列表 | `EX_String` 对 `.战斗` 返回非空串，但 `28578` 的 `result` 仍 `nr=0` | 给 `EX_String`(5467) 加返回探针，或 `XSYS4_LVAR_DBG=28578:3` 看 `b` 的槽号是否 >0 | 待验 |

## 已证伪（保留，别回头查）

| ID | 假设 | 为什么错 | 证据位置 |
|---|---|---|---|
| `H001` | ADV 背景不出是 `cover_layer` / 背景快照 / `message_window_show` 遮挡 | 真正原因是 `PartsEngine.UpdateComponent`(f12) 空桩，帧心跳根本没跑；修后截图 10910B→680564B | D203 / `PartsEngine.c`；`archive/` 取证链 |
| `H002` | AIN 无 MSGF 段时把 `msgf` 设成 `-1` 就行 | `_MSG` 是 JMP 类（`ip_inc=0`），`if (msgf<0) break;` 会死循环；实测执行约 2693 万次 | `vm.c` `_MSG` case；`instructions.c` `_JMP(_MSG,…)` |
| `H003` | 剧情推进慢纯粹是逐字动画，按住 Ctrl 快进后就能一路到战斗 | Ctrl 快进确实把 14 分钟压到 60 秒，但 60 秒 / 4 分钟 / 补点击三次结果完全一致（333 次 Set），停点固定 | D205 / `logs/live_ctrlskip.log`、`live_ctrllong.log`、`live_ctrlbreak.log` |
| `H004` | 硬置 `global[28]`（`advengine::detail::IsSkip` 直接返回它）能跳过逐句绘制 | 后门确实触发，但剧情**根本没进** `０２／ＳＴＡＲＴ`（0 命中），卡在等待 | `logs/live_advskip.log`；PoC 已按纪律回退 |
| `H005` | 「選択肢按钮点不中」是坐标不对（动态号段从 `900000307` 漂到 `900000479-508`） | `QuestMapSelectionButton` 活动**0 次装载**，按钮根本没创建 | D206 / `logs/live_geodump.log`：`CreateSelection` 之后全是 `Game/Adv/NamePlate` |
| `H006` | 可见部件 `click=0` 是 EX「点击許可」键名/值不对 | 离线导出 `QuestMapSelectionButton.pactex`，该 GBK 键出现 8 次且**值 = 1**；AIN 全库只调 1 次 `Parts_SetClickable` | `tools/offline/pactex_dump.py game/Rance10Pact.afa QuestMapSelectionButton --raw` |
| `H007` | `XSYS4_ACT_CLICKABLE_ALL=1` 能修好選択肢点击 | 只让活动文件声明的静态部件（`900000030`）变 `click=1`，运行时动态件不受影响，`23559` 仍 0 次 | `activity.c:733`；`logs/live_clickable.log` |
| `H008` | `parts_is_visible()` 漏过滤 `show=0`，导致不可见全屏层吃掉点击 | D128 已修，`parts_internal.h:513` 第一句就 `if (!parts->global.show) return false`；`[inp]` 的 `show=0` 是另一帧快照 | `parts_internal.h:513`、`logs/live_selclick.log` |
| `H009` | `AnteaterADVLogList.AddText` 段错误是参数个数不匹配 | 真因是两个参数皆 `AIN_STRING` **按值**，库表却绑 `struct string**` 的 ref 实现；ffi 传 `struct string*`，解引用成 `0x100000800000008` | D204 / core `string_append` 栈；`AnteaterADVEngine.c:_PreLink` |
| `H011` | 事件列表为空是 `AUTO_MAP`/`AUTO_MAP_ROUTE` 后门跳过真实地图节点选择所致 | 真实流程（点 `100,60` 进地图，不用任何后门）同样 `SetMessageWindowText` 333 次、同样停在 `ReplayMemoDialog`、`23489` 仍 0 次 | D207 / `logs/live_realflow.log` |
| `H012` | 三个事件名 getter 全空是 EX KeyBase 拼错（D202 同类问题） | `XSYS4_FN_DUMP=5467` 实测 `EX_String` 收到的 key 完全正确：`任务数据.荷鲁斯的宇宙飞船.００１.故事` / `.战斗` / `.战斗２`，`００１` 编号已生效 | D207 / `logs/live_exkey.log`（1049 次调用） |
| `H018` | `parts_hittest` 用「局部 hitbox + 直接父 global.pos」、渲染用 `global.pos`，两套坐标错开几百像素 | `PARTS_DUMP` 的 `(548,718)` 只是**装载瞬间快照**；input 帧里 `global.pos` 本就是 `(0,40)`，两套一致。加 `XSYS4_HITBOX_GLOBAL=1` 改用 global 基准后点 `646,738` 仍不进地图（证伪后已回退） | D207 / `logs/live_both.log`、`live_hitfix2.log`；`input.c:parts_hitbox_at` |
| `H021` | 真实点中地图节点就能把 KeyBase 推进到 `００２` | 网格点击确实命中 `SceneQuestMap@RunCurrentObjectEvent`(23609)，但 `EX_String` 仍只查 `００１.战斗` / `００１.战斗２`；其它节点（００２/００３/４００…）只查 `.传送`（地图渲染用）。点击改不了 KeyBase | D207 / `logs/live_nodegrid.log`（7137 行，key 去重清单） |

## 已成立

| ID | 结论 | 关键判据 | 位置 |
|---|---|---|---|
| `H010` | AIN v12 无 MSGF 段时 `ain_open_conv` 的 `xcalloc` 把 `msgf` 留成 0（NULL function），`_MSG` 三参数被吞 | 按名解析到裸 `message`/3（Rance10 `fno 6100`）后 `AddMessageText`(6136) 首次命中、`SetMessageWindowText` 收到非空 GBK 文本 | D204 / `hacks.c:resolve_v12_msgf()` |
| `H015` | 官方「按住 Ctrl 不论是否已读强制快进」是长剧情唯一实用入口 | 逐点 320 击/14 分钟 vs 按住 Ctrl 60 秒，`SetMessageWindowText` 同为 333 次 | D205 / `run-live.sh` `keydown:/keyup:` |
| `H016` | `CreateSelection` 走的是 `DebugForceExit`(23627) 空事件分支 | 调用轨迹实测 `23627` 命中、`QuestMapSelectionButton` 0 次装载 | D206 / `logs/live_selbranch.log` |
| `H017` | 标题 `Chapter1` 按钮的**真实命中区在左上角**，`(646,740)` 是错的；点 `(100,60)` 能走真实流程进地图 | `SceneQuestMap` 从长期 0 变成 **1 次命中**，且 `23486 → 23487` 开场剧情照常触发 | D207 / `logs/live_topleft.log`、`live_realflow.log` |
| `H019` | `００１` 节点的 `.战斗` 返回空是**正常数据**，战斗事件挂在别的编号节点 | 离线 dump `game/Rance10EX.ex`：`任务数据 → 荷鲁斯的宇宙飞船 → ００１` 只有 `故事 = "０２／ＳＴＡＲＴ"`（**无 `战斗` 字段**）；`００２` 有 `战斗 = "m11杂鱼／１"`，`００３` = `m11杂鱼／２`，`０１２` = `m11杂鱼／３` | D207 / `exdump_tool game/Rance10EX.ex`；`/tmp/ex_tree.txt:96433` |

| `H022` | KeyBase 是 `QuestMapParamAccessor.this.m0` 的**字符串本身**（`任务数据.荷鲁斯的宇宙飞船.００１`），不是循环算出来的编号；`GetQuestEventList`(28578) 的 `index` 只是同节点内 `.战斗２/.故事２` 的序号后缀 | `XSYS4_DUMP_AT=45c8aa` 同帧读出 `this.m0` 字符串、`index=1` 且 `s/b/m` 全空（循环正确退出）；诊断后门把末位改成 `２` 后 EX 立刻查 `００２.战斗` 并命中 `23489` | D209 / `logs/live_keydump.log:730-760`、`live_node002d.log` |
| `H024` | 战斗链剩下的缺口是两组**跨库** HLL：`MainEXFile.Save/Load`（战斗前快照存档）与 `AFAFactory` 搜索族 6 个导出（战斗 UI 的 `Parts@ResolveFlatName`）；静态清单只扫 `Battle*` 所以漏判 | 补齐后 `23489 → 26388 → Game/Battle*` 32 个活动装载、VS 画面出图、零 `Unimplemented HLL` / 零崩溃 | D209 / `logs/live_battle3.log`；结论见根 `TECH_DEBT.md`，阶段性清单归档 工作区根 `archive/Rance10_HLL-GAP-全文-20261115.md` |

## 改判链

```
H001 证伪 ──> D203 UpdateComponent 空桩（背景出图）
H002 证伪 ──> H010 成立（msgf 解析）──> H009 证伪（AddText 按值签名）──> 消息链通
H003 证伪 ──> H015 成立（Ctrl 快进）──> H004 证伪 ──> 停点固定，不是时长问题
H005 证伪 ──> H006/H007/H008 证伪 ──> H016 成立（空事件分支）
                                        ├──> H011 证伪（真实流程同样空）
                                        ├──> H012 证伪（KeyBase 实测正确）
                                        ├──> H017 成立（真实流程可进地图）
                                        ├──> H018 证伪（坐标双基准是快照错觉）
                                        ├──> H019 成立（EX 铁证：战斗在 ００２ 节点）
                                        │      ├──> H021 证伪（点节点不改 KeyBase）
                                        │      ├──> H022 成立（this.m0 就是 KeyBase）
                                        │      │      └──> H024 成立（补齐跨库 HLL，战斗打通）
                                        │      └──> H023 待验（正常路径推进节点）
                                        └──> H020 / H014 待验
H001 证伪 ──> H017 成立 的另一条证据：标题按钮命中区在 (0,40)-(196,80)
```

## 维护约定

- 每轮开工先看「当前待验」，选定 1–2 条再动手，不要同时开四条。
- 验证完**当轮**改状态：成立就写进 `README.md` / `TECH_DEBT.md` 的结论，
  证伪就留在本表并补证据位置。
- 台账随仓库推送（`origin/rance10-native`），但**不随项目状态频繁重写**；
  方法论沉淀进 `docs/GUIDE/`，完整取证进工作区 `archive/`。
