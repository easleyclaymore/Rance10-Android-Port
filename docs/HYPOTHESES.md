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
| `H014` | 消息窗口正文不出是 `show/alpha` 或文本纹理路径（`[UC-TEXSHADER]`）问题 | `SetMessageWindowText` 非空且 `show/alpha` 正常 → 假设错 | `XSYS4_TEXT_RENDER_DBG=900000300` 实测 `show=1 a=255 vis=1 mwshow=1 lines=1 wh=208x16`，准入全过 | 证伪 → 改判 H027 |
| `H027` | 消息窗口三项 EX 语义缺失：底板 `ＣＧ名` / `字体尺寸=39` / `文本エリア=(470,233,0,0)` | 补齐后 `字体尺寸` 实测生效（文字层 `185x39`/`481x39`）、底板 `set=1`；但截图仍无可见消息框 | D217 / `logs/live_msgwinB.log`、`live_msgwinD.log` | 成立（`文本エリア` 由 AIN 运行时接管，EX 装载期不必应用） |
| `H029` | 底板画不出来是**合成层**问题：半透明黑底板叠在同样全黑的 ADV 背景上，或 CG 纹理采样 / alpha 合成有缺陷 | 换到默认零开关真实流程后，底板若仍不可见则假设成立 | `[frame]` 探针 12 条 + 红色 add_color PoC 像素统计；真实流程 `d217full_16_wait.png` 底板清晰可见 | 证伪 → 改判 H031 |
| `H031` | `XSYS4_AUTO_MAP` 后门直调 `RunGameChapter1` 跳过背景层初始化，制造「ADV 全黑、底板一像素没画」的假象；**渲染验收必须用默认零开关真实流程** | 同一份代码在真实流程下画面仍全黑 → 假设错 | `logs/live_d217full.log` + `/tmp/d217full_16_wait.png`（背景 CG / 底板 / 名牌 / 正文全出图）vs `/tmp/msgwinC_05_wait.png`（全黑） | 成立 |
| `H025` | 战斗画面里残留的 ADV / 回想文字是 SceneStack 残留层未清理 | 关掉该层后文字消失且 `Game/Battle/*` 活动数不变 | **D225 改判**：`h037fix` 同帧快照 `vis=1 by ctrl {7: 262}` —— 只有当前战斗层可见，ADV 的 ctrl=3 全部 `vis=0`，不可能有旧层残留；装载入口本就有同名替换（D221），`[txtr]` 254 条又是全程采样。画面上那些文字是**当前层内 ADV 消息窗口未及时卸载**，属观感项 | 证伪（非层泄漏）；降级为观感长尾 |
| `H038` | 第二次点击推进到实际战斗回合时全屏变黑，是 `parts_cover_layer` 被抬到 10、仅剩一个 ctrl=10 全屏淡出层可见，且该层未移除 / 未淡入 | 让淡出层在切换完成后移除或降 alpha 后画面恢复 → 成立；若原版本来就该停在黑屏等输入，补一次更长等待 / 定向点击应出下一场景 | `e2e2` dump 2：`cover=10`、部件 `8:20 / 10:1`、**`vis=1 {10: 1}`**；对照组 `e2e` 的 `shot_004` 10579B 全黑、`battle=45 / acts=121 / errors=0 / core=0`；`XSYS4_NO_CP_ALPHA_FIX=1` 回退组更早黑（`shot_003`），排除 D188 同类遮罩误伤 | 待验（**下一阶段：实际战斗回合**，超出第一章判据） |
| `H044` | 回合切换/对话框场景「不结束」：`SceneStack@Join` 的 `while (!IsFinish()) Ｐ＿点击実行()` 出不来 | 若能让 `SceneStack@Exit` 真正结束那个 Join → 假设成立 | **结案：不是引擎 bug，是我的点击坐标高了 37px。** 三轮取证后逐条：① 点击链整条通（`EndInput clicked=…` → `CallFunctionMouseClick`(7942) → `SceneReplayMemoDialog@<lambda>`(36563) → `SceneStack@Exit`(29658)）；② `Exit` 与 `Join` 同一实例（h92 都是 77342）；③ `IsFinish` 实测 `0→1`（h93）；④ **h101 定位到真因**：点 `(1055,843)` 命中的是 `900000506`（TextButton 活动里 862x226 的面板 CG），`CallFunctionMouseClick` 照常被调 3 次，但 `TextButton@IsClickable`(22909)/36563/29658 一次都不来；`[inp]` 候选表里根本没有两个 UC 部件 —— 它们的命中盒是 `(984,880) 196x40`，我用截图目测的 843 差了 37px。改成 `(1055,900)` 后 h102 三次点击**全部**命中 `900000518` 并走到 36563 → 29658 | 已结案（装置问题，`click:1055,900`）。附带结论：`SceneReplayMemoDialog` 是**过场**，点掉后 `QuestMapSelectionButton` 装载；当时「回到地图 / `Game/Battle` 0 次」是 `s1` 点错選択肢坐标的假象 —— `s2` 改点 `1600,450` 后 `23489 → 26388 → Game/Battle/* 32` 全通，`[SELECTION-STALL]` 随之结清（见 `TECH_DEBT` 已清零） | 已结案 |
| `H045` | `[SELECTION-STALL]`：選択肢后不进战斗是「该节点本来无战斗」或 `CreateSelection`(23603) 的 `m3` 为空 | 点中選択肢后 `Game/Battle/*` 仍 0 则成立 | **证伪**：`s1` 命中 `900000518` 点掉回想对话框后，`1668,230/330` 落在 `900000505` 面板 CG、選択肢未被选中 → `Game/Battle` 0；`s2` 改点 `1600,450` 命中 `900000526 → 23486 #2/#3`，确认层 `900000505 → 23489 RunBattleEvent → 26388 RunBattleSceneSet → Game/Battle/* 32`（`logs/live_s2.log` 2031–2197，零 assert/core/double free） | 证伪 → 结清，与 H044 同属点击坐标装置问题 |
| `H046` | 战斗开始演出 flat 卡在 `frame=2/end=15` 是「`is_stop` 正常等待 AIN 指令」 | 若 AIN 调 `GoFramePartsFlat` 后帧仍不动 → 假设错（问题在引擎侧） | **成立（D239）**：`flat.c` 的 `PE_GoFramePartsFlat()` 向前定位分支只设 `pending_seek_delta`、**没清 `root_state->stopped`**，而 `flat_advance_layer()` 增量是 `stopped ? 0 : 1` → delta 被空转。`s13` dump 实测 `frame=2 end=15 adv=0 stop=0` 恒定；`s18` `SetPartsFlat ok … '战斗开始/…开始按钮／一般'` 证实存在 `开始按钮`（`pos=(960,540) 274x274`），但它被置顶的演出 flat（`gz=1<<30+1125`）压住 | 成立 → 修 `flat.c`，判据 `s20` |
| `H048` | 战斗中「连点 40 次仍停在 `Round 1/10`」是演出太长、需要更多点击 | 若 40 次点击后 `SceneStack@Exit`(29658) 仍为 0 → 假设错（是 Exit 从未被调，不是点得不够） | **部分证伪（`s44`）**：`XSYS4_CALL_TRACE=29658,29650,24882` 实测 `SceneStack@Exit` **5 次**、`IsFinish::set`(29650) **5 次**、`SceneBattle@Run`(24882) **仅 1 次** —— 退出链是通的，**不存在「每帧重建」**。反汇编给出的真实结构：`BattleScenes::RunBattle`(25945) = `Scenes::RunIScene(λ new SceneBattle)`；`SceneBattle@Run` 只做一次 UI 构建后调 `SceneStack@Join`(29656)，而 `Join` = `init; loop { if (IsFinish()) break; Ｐ＿点击実行(); } done`，每轮 `Ｐ＿点击実行` 等一次点击，5 次 `Exit` 对应 5 段子场景演完。**真正让观察失效的是跑测装置**：`click:1845,818` 在 `s41/s42/s44` 里 `pos=(1845` 出现 **0 次**，而进程存活、日志持续增长 —— `click()` 与 `key)` 的 `xdotool windowfocus --sync` 在无 WM 下永久阻塞（D246） | 修 D246（`click`/`key`/`keydown` 的 xdotool 全部带 `timeout -k 1 2`）后重验：判据是 `pos=(1845,818)` 出现且 `EndInput clicked=` 落在技能按钮上 |
| `H049` | `[BATTLE-UI-BLANK]` 的技能按钮底板不画，是 D214 把**真**布局盒（EX 声明 `レイアウトボックス`）也一律设成 FREE 造成的；按 `配置` 字段重排即可修好 | 按 `配置` 设 layout_type 后底板出现、且其余画面无回归 → 成立 | **证伪（`s55`）**：开 `XSYS4_EX_LAYOUT_BY_CONFIG=1` 后画面严重错乱——`Round`/`1`/`/`/`10` 被拆成竖排、技能名重叠成一坨、AP 球消失、`Game/Battle/*` 从 45 掉到 **32**、`EndInput` **0**。说明 `配置=1` 并不等于「容器内子部件纵向排列」，D214「EX 装载的静态容器不该走自动布局」的判断是对的。开关保留（默认关），代码路径不动 |
| `H047` | `Array.Find` 谓词崩溃是 D227 没覆盖 REF 类实参，把 REF 纳入 `hll_arg_owns_ref()` 即可 | 补 ref 后同配方不再崩 → 成立 | **证伪**：补上 `AIN_REF_STRING/STRUCT/DELEGATE/ARRAY` 后 `s22` 仍崩，**栈一字未变**——`heap_unref(slot=821768443)`；gdb 确认这个槽号与崩溃前日志 `Invalid string index: 821768443 (type=-1)` 同号，说明元素本身已是垃圾值，不是"引用差 1" | 证伪 → 改判 `[BAD-SLOT-REF]`；D241 只给 `heap_unref` 加上界防护 |
| `H039` | 黑屏的「全屏淡出层」其实是**不画像素的 phantom floor**：`parts_update_cover_layer()` 只按名义 alpha + 尺寸判地板，flat 内部 key 的 alpha/纹理可得性没算 | 若该 flat 的真实 key alpha < 250 或纹理缺失 → 它是幽灵地板，补上判定后画面恢复 | **D232 重新成立**：D226 当时用 `parts_flat_max_paint_alpha()` 判成 `flatmax=255`，但那 255 来自**子时间线容器关键帧**（`[0]图形 库=…(子时间线) key0 a=255`）——容器自己不画像素，只给子层当乘数。`XSYS4_FLAT_TREE=900001057` 实测：最后一帧所有子层要么 `local>=count` 要么 `a=0`，一个像素都不画。改成「容器不贡献 alpha、只递归子层」后，`cover` 从 8 掉到 ≤5、同配方四张引擎自截图**全非黑**（10579B → 2157158B） | 成立（D232） |
| `H040` | ~~黑屏真因是淡入淡出任务从不被推进~~ | 若 `PartsMotionTask@OnUpdate` / `UpdateEvent` 其实在持续跑 → 假设错 | **D230 证伪（探针误读）**：`XSYS4_CALL_TRACE` 的打印规则是「前 3 次 + 每个新调用站点 + 每 200 次」，日志里的 `#53` 是**第 53 次**命中而不是「总共 4 次」。实测 `UpdateEvent` ≥200 次、`OnUpdate` ≥53 次；`XSYS4_DUMP_AT=480d52`（`OnUpdate` 越过 `Parts.IsInitialized()` 之后的地址）命中 30+ 次 → **任务确实在跑**。这正是 GUIDE 1.13「限流探针的没有输出 ≠ 没有发生」的又一例 | 证伪；改判 H042 |
| `H041` | 回合切换时偶发 SIGSEGV，是 HLL→AIN 回调**借来的实参没补引用**：`Array.Find(ref array<string>, 谓词)` 把元素副本交给被调帧，被调帧 `S_EQUALE` 用完 unref，数组那份引用被吞 → 悬空字符串 | 修掉补引用后同配方不再崩、且 `core` 里 `Array_Find_ → heap_unref` 栈不再出现 | **D227 成立**：core（PID 88483）栈 `heap_unref(slot=918493) ← S_EQUALE ← vm_execute ← vm_call(37442) ← array_predicate_matches ← Array_Find_`，`918493` 越界；修后同配方不崩、基线 `d227` 四数与 `d224` 一致 | 成立（D227） |
| `H042` | ~~黑屏是演出 flat 播完没被移除~~ | 若能证明那张 flat 在最后一帧不画任何像素、黑屏是我们自己的 cover 过滤 → 假设错 | **D232 证伪**：`XSYS4_FLAT_TREE` 显示最后一帧所有子层超范围或 a=0；`XSYS4_NO_LAYER_COVER=1` 的 A/B 里同一时间点截图 2157158B（有画面）而默认是 10579B（全黑）——黑屏是 `cover=8` 把 ctrl<8 的 83 个 UI 部件全滤掉造成的，与「演出退场」无关 | 证伪；改判 H044 |
| `H032` | 残留根因是 `PE_RemoveController` **擦空层**：AIN 传的稳定 ID 指向刚 `AddLayer`、还没挂部件的空层，旧层部件从未被释放；叠加 `parts_release()` 只摘链不递归释放子部件 | 让 ID 落到实际挂部件的层（或递归释放子树）后残留文字仍在 → 假设错 | `logs/live_h025probe.log`：9 次 RemoveController 中 7 次 `released=0`、812 个部件全在 ctrl 0..7、回想对话框 20+ 部件 0 次 Release 且最后仍在表里 | 部分成立（D219 子树释放有效 → 见 H033） |
| `H033` | 剩下的「当前 ADV 场景文字 / 名牌」残留，根因是 `PE_RemoveController` 从未擦到实际挂部件的层（`released=0`），需要修正 `SceneStack@EraseLayer` 传入 ID 与目标层的对应 | 修正 ID→层映射后这些文字仍残留 → 假设错 | `h025fix2` 排除子树释放因素；`h033probe` 的 `[txtr]` 是**全程采样**（254 条、每号限 3 条），不能当最后一帧快照。装载入口 1136-1140 行本就有同名替换 → `activity_release()` 逐个 `PE_ReleaseParts`，故 22 次 `NamePlate` 装载不构成泄漏证据 | 证据不足，降级（见 H035） |
| `H035` | 战斗 VS 入场后长时间等待会整屏变黑，是等待玩家输入时的正常淡出，还是渲染层把战斗层隐藏了 | 长等待序列里补一次战斗点击后画面恢复 → 正常等待；仍黑 → 渲染 bug | **D222 定案**：`h025long` 的 390 字节「末帧全黑」是 `import -window` 抓到无内容 framebuffer 的空图（与纯黑 Xvfb 对照图 md5 `36eb5c51…` 完全一致），拿它跟正常帧做像素差无效，D221「文字消失 / 全帧差 969055」结论作废。改用引擎 `glReadPixels` 自截图（`XSYS4_SHOT_DIR` + Alt+S，绕开 X11 与 zenity）：`h035ctrl` 同一次跑测双时间点 25s=**953847B 有画面** / 50s=**10579B 全黑**，证实 GL 主 FBO 真的变黑。`h035engine` 点击后新增 13 个战斗 UI 活动（`BattleApView01` / `BattleApSphere:5..10` / `BattleRoundView(Big)` / `EnemyActionIconBase:0..3`，`battle` 32→45）但点击后 6s 自截图仍全黑；`h035final` 点击后 **20s** 仍全黑 → 排除等输入淡出与贴图加载延迟 | 成立（真渲染 bug：VS 演出结束后战斗 UI 未呈现，非正常淡出） |
| `H036` | 黑屏是 `scene_render()` 首行 `gfx_clear()` 在没有可见 sprite 时把主 FBO 清空，且此后不再被标脏重绘 | 在黑屏帧上用同帧部件快照 + 引擎自截图联合取证：战斗部件 `vis=1 / tex>0` 却仍全黑 → 命中；强制重绘后画面回来 → 成立 | **D223 部分证伪**：`h036probe`（`until:ReplayMemoDialog` 稳健配方，`battle=32 / acts=108 / replay=1`）同帧快照给出 `cover=7`、`layer_show` 仅 ctrl=3 为 0、`ctrl=7` **263 个 `vis=1`** 部件，其中 `900000614 1347x996` / `900000621 1920x693` / `900000628 1024x1024` 均 `tex=1 a=255` —— 准入与 sprite 注册（parts 层从不设 `sp.hidden`、`has_pixel` 恒 true）全部排除。`vm_frame_pump()` 强制每帧 render 的 A/B 反使基线 `activities 36→13 / scene_quest_map 1→0`、真实流程卡在 ADV，已整段回退 | 证伪（准入 / 注册 / 重绘三条均不成立），收敛到绘制执行层；见 H037 |
| `H037` | 黑屏是战斗部件 `global.alpha` 被设 0 后没有淡入恢复（D188 同类），或 `multiply_color` 被清零 | 在黑屏帧给 `parts_render_cg()` 加同帧诊断，看那些 `tex=1 a=255` 大部件有没有被调用、调用时 alpha/mul 实际值是多少；再单独 A/B alpha 容错 | **D224 改判（方向相反）**：同帧 CG 绘制日志显示黑屏期 `since_last=17313` 次绘制、`mul=(255,255,255)` / `scale=(1,1)` 全部正常，唯最后一次绘制是 `no=900000613`（`PARTS_CONSTRUCTION_PROCESS`、2x2 单色纹理 × `scale=(960,540)` 铺满全屏、`a=255`、`z=2147483520`）——全屏淡出遮罩。真因是 **D188 兜底修复**（`PE_SetAlpha()` 把 CP 部件的 `alpha<=0` 无条件抬回 255）误把 AIN 用来隐藏遮罩的 0 抬成不透明。`XSYS4_NO_CP_ALPHA_FIX=1` A/B：`shot_002` **10579B 全黑 → 1207952B 有画面** | 成立（真因 D188 误伤，不是「没淡入」）；D224 已修，H035 清零 |
| `H050` | `ReleaseActivity` 的 `EraseNumberList` 出参被丢弃，活动释放后其部件登记的委托仍留在 AIN 的消息分发表里；号复用时点击会命中**上一次活动的处理器** | 拿掉 `(void)` 后 `XSYS4_ACT_RELEASE_DGLIST_DBG=1` 必须打出非 0 的委托下标个数；若恒 0 说明 AIN 根本不用这个出参、假设错 | `Activity@Release`(29045) → `AFL_Activity_Release`(198) → `activity::Release`(224) → `activity::detail::Release`(391) → `CALLHLL 27 24`，形参名就是 `DelegateIndexList`；391 紧接着把它交给 `CPartsMessageManager@ReleaseFunctionSetList`(8007) → `ReleaseFunctionSetWithoutGC`(8005) 逐个 `Array.Erase`。`logs/live_d270c.log` 实测 `[actrel] 'Game/Battle/CardApView' parts=5 → delegate indices=5`（旧代码此处为 0） | 成立（D270）：`activity_release_ex()` 写回 `delegate_index`，与 `PE_RemoveController()` 同口径；默认生效，`XSYS4_ACT_RELEASE_DGLIST=0` 可回退 |
| `H051` | `矩形パーツ`（组件类型 23）的几何在**四角坐标**里，而 `activity.c` 一个字段都没解析 → 这类部件恒为 `0x0`，叠加 `render.c` 对 `PARTS_RECT_DETECTION` 直接 `break`，彻底不可见 | 若补上四角解析后 `XSYS4_PARTS_DUMP` 里 `st=11` 的 `0x0` 部件数归零 → 成立；若仍为 0 说明几何另有来源 | `XSYS4_PARTS_DUMP` 实测（d271probe）：`st=11` 去重 64 个部件里 **60 个 `0x0`**，只有 AIN 运行时显式设过的 4 个非 0；`ApBase`(900000449) `0x0`。`尺寸` 字段全 EX 统计只出现在 `按钮` 类型上（`低レベルパーツ` 40 个全无），所以 `part->w>0 && part->h>0` 分支对它永远进不去。修后 A/B：`XSYS4_EX_RECT_SIZE=0` → `0x0=60 / 非0=4`；默认 → `0x0=0 / 非0=64`，`ApBase` 拿到 `77x30`（与 EX 四角 77×30 一致） | 成立（D271）：`activity.c` 新增 `rect_corners_to_size()` + `XSYS4_EX_RECT_SIZE`；画面零像素差异、`EndInput` 落点不变（716/705）、基线 `d271final` 与 `d270final` 逐项一致 |
| `H052` | 技能按钮底板全黑的**剩余**原因是「矩形部件不绘制」，加个矩形填充就能出底板 | 若这些部件本就 `alpha=0` 或被隐藏（`表示=0`），加绘制也不会出像素 → 假设错 | **证伪**：`XSYS4_PARTS_DUMP`（d271on）逐活动 dump 里，`LargeSkillButton` 根(900000444) 与其子部件 在活动装载瞬间是 `a=255 / 表示=1`，但**同一部件号在 19 个不同 activity 的 dump 里都出现过**（号复用），拿任意一条「后来 a=0」当结论是误读（我第一次就掉进这个坑）。更关键的是 `Button`(900000445) 的 flat 实测 `loaded=1 hdr=228x162 tl=3` 但 **`libs=0 cg=0`** —— 一个纹理库都没有，而正常出图的战斗 flat 是 `libs=2~4`。也就是说底板的 flat 本身没有可画的图集，加矩形填充只会得到一块矩形，不是美术底板 | 证伪（D272 结论）：**不要**先加矩形绘制。下一步应查 `libs=0` 的成因（TALT/QNT 图集或 LIBL 段解析），判据是技能按钮 flat 的 `libs` 变为非 0 且 `cg`>0 |
| `H053` | 技能按钮底板不可见的下一层根因是 flat **零纹理库**（`libs=0`），而 D200 的 TALT 兜底只认 `QNT` 魔数，`战斗画面/シス／战斗／技能按钮／一般` 的 TALT 是 **AJP** | 若把判定放宽到「引擎认得的图片格式」，该 flat 的 `libs` 应变成 ≥1，且画面出现底板像素 → 成立 | `XSYS4_FLAT_LIBS_DBG` 实测：`技能按钮／一般` → `nr_libraries=0 nr_talt=1 talt_extra=0 talt[0] magic=AJP`；同族 `技能按钮小上／一般` → `nr_libraries=1`（有 LIBL 库，正常出图）—— 两者差异就在 TALT 格式。`cg_check_format()` 本来就认得 AJP（`ajp_extract` 一直能用），所以是 D200 的**判定过窄**而不是缺格式支持。修后 A/B：`XSYS4_TALT_EXTRA_FORMATS=qnt` → `libs=0`；默认 → `libs=1` | 成立（D275）：`flat.c` 改用 `cg_check_format()`；`技能按钮` 系列四个部件 `libs` 全部 0→1，同批 `开始按钮/防御按钮/AP 图标` 也一并恢复；画面非黑像素 +15617（+2.8%），diff 全落在 `(0,974)-(1910,1080)` 即按钮区；基线 `d275final` 与 `d274` 逐项一致 |
| `H054` | 战斗「点不动」是引擎丢点击或 z 序错 | 若 AIN 每次点击都收到回调，则点击链路通，问题在 AIN 侧的回合状态机 | **证伪（D276）**：`XSYS4_CALL_TRACE=7942` 实测每次点击都触发 `CallFunctionMouseClick`(7942)，且 `23489 → 26388 → 24882` 全命中 —— 点击链路完全正常。真正的阻塞是：(a) 点 `(960,540)` 命中的是「开始按钮」`900000716`（`战斗开始／开始按钮`，274x274，`pc=0` 会吃点击），这是 AIN 的正常等待，不是 bug；(b) `LargeSkillButton` 活动根 `900000439` 的 **`la=0`**（local alpha）→ 整棵子树 `vis=0`，所以技能卡不显示也不可点 | 已证伪旧判断（D276）。**下一手**：查 `900000439` 的 `la` 由哪次 `PE_SetAlpha` 写成 0 —— 判据是找到对应 AIN 调用点，并区分「AIN 有意隐藏（战斗未进入玩家回合）」与「我们读错 alpha 语义」 |
| `H055` | `LargeSkillButton` 活动根 `900000439` 的 `la=0` 是我们读错 alpha 语义，或 motion 系统写坏了 | 若 `SetAlpha` 的每次 0 都能追到 AIN 某个有意义的调用点，且时间参数确实为 0，则是 AIN 的真实意图 | **成立（D277）**：`XSYS4_ALPHA_DBG` + `XSYS4_ALPHA_STACK` 抓到三条 `alpha=0` 的**不同来源**——① `BattleCardButton@Init`（初始化就藏，正常）；② ③ `PartsMotionTask@UpdateByKey`（motion 动画驱动）。后者是关键：`PartsMotionTask@OnUpdate` 的字节码里 `r = ITOF(Time) / ITOF(Span)`，而 `CASTask@Execute → ExecuteImp` 传入的 `OverTime/ScaledOverTime` 实参是**字面量 0** ——AIN 的时间推进不在引擎侧，`motion_t` 永远 0，于是 `r=0`、alpha 取 begin 值 0。这与 `[D119-MOTION]`（动作系统没有上游驱动）是同一件事的两个面 | 成立（D277）。**新债**：motion 时间基准需按 AIN 的 `Time/Span` 口径驱动，而不是靠引擎自己的 `motion_t`。判据：`PartsMotionTask` 驱动的淡入能让 `la` 从 0 走到 255 |
| `H020` | `GetQuestEventList` 的 `S_NOTE` 判空或 `Array.PushBack` 在引擎侧有偏差，导致明明查到了值也没进事件列表 | `EX_String` 对 `.战斗` 返回非空串，但 `28578` 的 `result` 仍 `nr=0` | 给 `EX_String`(5467) 加返回探针，或 `XSYS4_LVAR_DBG=28578:3` 看 `b` 的槽号是否 >0 | 待验 |

## 已证伪（保留，别回头查）

| ID | 假设 | 为什么错 | 证据位置 |
|---|---|---|---|
| `H043` | 让「不可点击且 alpha=0」的部件不参与命中，就能让点击落到下面的卡片命中区，从而推进战斗 | 改完后点击能到达卡片（`GetClickNumber` 返回卡片部件号）且战斗推进 → 成立 | **D229 证伪**：改完连开场回想对话框都点不动了 —— 那个「以后再看」按钮正是 `ck=0 / alpha≈0` 的用户组件（`900000511`，`[inp] ... click=0 pass=0 uc=TextButton`），活动数 104 → 51、卡在 `ReplayMemoDialog`。**alpha 不能当「是否可见」的判据**；代码已回退 |
| `H034` | 残留文字是 `cover_layer` 在淡出层 `show=0` 时回落造成的，去掉 `parts_update_cover_layer()` 里的 `if (!ctrl_stack.show[c]) continue;`（D128 注释原意）即可挡住 | 改动后同配方截图应变化 | `h033fix` 对 `h025fix2` **全帧差异 0 / 2073600 像素**，残留区域亮像素同为 2340；基线 `d220` 非黑 71.16%、四数与 D219 一致（无 D190 回归）→ 改动根本没打到可见路径 | 证伪，代码已按纪律回退 |
| `H001` | ADV 背景不出是 `cover_layer` / 背景快照 / `message_window_show` 遮挡 | 真正原因是 `PartsEngine.UpdateComponent`(f12) 空桩，帧心跳根本没跑；修后截图 10910B→680564B | D203 / `PartsEngine.c`；`archive/` 取证链 |
| `H028` | 正文不出是被 z=1000+ 的 ADV 背景 CG 覆盖（正文部件 z=2，名牌 z=6003） | B 组把 `message_window` 一族抬到 z=100000 后截图与 A 组完全一致，正文仍不出现；真因是 EX 比率坐标未换算，正文在屏幕外 | `XSYS4_MSGWIN_Z=1` / `logs/live_advtextD.log`、`/tmp/advtextD_05_wait.png`；实验代码已按纪律回退 |
| `H030` | 消息框不画是 `ＣＧ名` 没加载（`type==ACT_COMP_CG` 门限）或底板部件没进渲染 | 独立 CG 部件 `set=1`、`1920x330 tex=1 vis=1`；`[frame]` 探针实测 `tex=320 off=(0,-330) vis=1 link=-1 a=255` 准入全过，红色 add_color 仍 0 像素 → 问题在合成/背景，不在加载 | D217 / `logs/live_msgwinB.log`、`live_msgwinD.log`、`/tmp/msgwinC_05_wait.png`；红色 PoC 已删 |
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
| `H024` | 战斗链剩下的缺口是两组**跨库** HLL：`MainEXFile.Save/Load`（战斗前快照存档）与 `AFAFactory` 搜索族 6 个导出（战斗 UI 的 `Parts@ResolveFlatName`）；静态清单只扫 `Battle*` 所以漏判 | 补齐后 `23489 → 26388 → Game/Battle*` 32 个活动装载、VS 画面出图、零 `Unimplemented HLL` / 零崩溃 | D209 / `logs/live_battle3.log`；结论见 `docs/TECH_DEBT.md`，阶段性清单归档 工作区根 `archive/Rance10_HLL-GAP-全文-20261115.md` |
| `H023` | KeyBase 推进不需要改字符串：真实流程点選択肢 → `StartMoveRoute` → `SetIndex` 自己走到 `００２`；此前「剧情没播完」判断是错的 | 默认零开关、无后门下 `23559 → 23624 → 23606 → 23618 → 23620 → 23489 → 26388` 全命中，`Game/Battle/*` 26 活动装载、VS 画面出图 | D214/D215 / `logs/live_lastmile2.log` |
| `H026` | 开场脚本 `０２／ＳＴＡＲＴ`(32194) 只有 53 条 MSG（128960–129012），剧情早已播完；真卡点是收尾的 `ReplayMemoDialog` 按钮被布局重排到屏幕外 | `XSYS4_FN_DUMP=6100` 实测 53 句后即 RETURN；`ReplayMemoDialog` 子部件 y=1080/1306/2016/2056 恰好是高度累加 | D214 / `logs/live_msgseq.log`、`live_uidump.log` |

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
                                        │      └──> H023 成立（真实流程自然进战斗）
                                        └──> H020 / H014 待验

H050 成立（D270）：释放活动时把委托下标交还 AIN，消掉号复用后的错委托

H001 证伪 ──> H017 成立 的另一条证据：标题按钮命中区在 (0,40)-(196,80)
```

## 维护约定

- 每轮开工先看「当前待验」，选定 1–2 条再动手，不要同时开四条。
- 验证完**当轮**改状态：成立就写进 `README.md` / `TECH_DEBT.md` 的结论，
  证伪就留在本表并补证据位置。
- 台账随仓库推送（`origin/rance10-native`），但**不随项目状态频繁重写**；
  方法论沉淀进 `docs/GUIDE/`，完整取证进工作区 `archive/`。
