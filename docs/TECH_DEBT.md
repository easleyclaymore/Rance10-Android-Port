# 技术债

只列**未结**项。每条一行：**现象（≤2 行）→ 清偿条件**。排查过程在 `archive/`，方法论在 `Rance10_Port/xsystem4/docs/GUIDE/`。

分级：**A 结构性**（错一处污染所有模块）→ **B 会炸的**（当前不挡路但随时引爆）→ **C 长尾**（冻结记账）。

---

## A. 结构性

| ID | 位置 | 现象 | 清偿条件 |
|---|---|---|---|
| `[MEM-LIVE-LEAK]` | 引用计数族（未定位到单一函数） | 槽回收打开后 `live` 仍 ~300 对象/秒线性涨（`pages` +150/s）。单个很小，RSS 平稳在 ~305MB，**不是 OOM 主因** | 按 `XSYS4_MEM_STAT` 的 `live` 斜率做基线；用 `XSYS4_SLOT_ID` + `[mem]` 按 page 类型分组，先缩到"结构体页还是局部页" |
| `[POS-SEMANTICS]` | **已证伪（D199）** | `[flat] render` 探针「每号只打一次」把容器**滑入动画的第一帧**当成稳态值：`305` 的 pos 走 0→585→-73→0，`306` 相应 1920→1262→**1335**、`308` 2023→1365→**1438**。默认模型的稳态坐标本来就是对的。D193–D199 建立的三种坐标模型（`ABS_ALL` / `POS_MARK` / `CONTAINER_ROOT`）全部源于这个错误快照，均无必要 | 探针已改为每号最多打 8 次。`nr_libraries=0` 的真因已由 D200 解决（TALT 段 QNT 图集）。|

## B. 会炸的

| ID | 位置 | 现象 | 清偿条件 |
|---|---|---|---|
| `[STRUCT-LAYOUT]` | `method_call` + `alloc_struct` | D135/D145/D154 修完后 `this=1`（struct #0）与委托帧漂移都不再出现；`XSYS4_STRICT_REF=1` 尚未做整体回归 | 编队路径稳定后跑一次 `XSYS4_STRICT_REF=1` 全量回归，再清账 |
| `[NULL-IFACE-CALL]` | `src/vm.c` `method_call()` | 空引用/无效 `this` 的调用降级为「跳过」，并补一个 `-1` 返回值保持净栈平衡。**`obj == -1` 是全局现象**（`Admiral@*` 36 次、`InstantTimer@*`、`Rance10@RestoreTakeOverInfo` 全是），不是图鉴特有；补建对象已按 struct 复用（D155，活跃对象 -39%） | ① 40+ 次 `[nullcall] ip=0x4643C2` 让图鉴列表为空 → 编队被模态挡（见 `[PARTY-TROPHY-MODAL]`）；② 为什么这么多 `this` 是 `-1`：`Admiral@Init` 那 9 次是最好的入口——看 `g_admiral` 在哪被赋值 |
| `[AUTO_PARTY-INPUT]` | `XSYS4_AUTO_PARTY` / `AUTO_RUNPARTY` 后门 | 后门从帧泵里重入 `vm_call`，与 AIN 自己的 `AddLayer/Join` 叠层：**用它验退出会得到「图层残留、画面黑」的假故障**（D190 已证明那是后门的锅）。真实进出路径 D191 已通 | 进/出编队一律走真实点击（`docs/SDK.md` 配方）；后门仅留作调用链对照，交付前删除 |
| `[PROBE-GATED]` | `src/parts/input.c` 等限流探针 | `[inp]` 的 `dbg_n` 只在打印时自增，第 40 条后永久停住 → "没有输出"被误读成"没有输入"。**D238 已修这一处**：计数器改为每帧自增，"每 60 帧一条心跳"现在真的会打。**第三种变体（同样骗人，且这次骗了我）**：跑测还没跑到那一步就去读日志 —— h89 在 10:56 读到 0 条 `[fn]`，75 秒后同一个文件里是 33 条 | ①其余限流探针同样改成"每 N 帧必打"；②**读日志前先确认跑测已结束或已到达目标场景**（`until:` 或 `[act] loaded` 目标活动），否则"没有"只是"还没到" |
| `[LAYER-LEAK]` | `parts.c:PE_RemoveController` + AIN `SceneStack@EraseLayer`(29666) | **D218 实测定位**：`XSYS4_CTRL_DBG` 下 9 次 `RemoveController` 里 **7 次 `released=0`** —— AIN 传的稳定 ID（0,2,4…16 → ctrl 0…8）指向刚 `AddLayer`、还没挂部件的空层；直方图 `0:3/4 1:7/7 2:35/54 3:252/265 4:37/50 5:34/34 6:8/10 7:227/388` 共 812 个部件全挂在 ctrl 0..7。`Release parts` 164 次里回想对话框的 20+ 个部件**一个都没被释放**，最后一次 dump 仍在表里（`ctrl=3`）。`parts_release()` 只把子部件摘链（`parent=NULL`）而不释放，是第二个残留源。**D190** 修掉了 cover_layer 永久卡死，但战斗 UI 装载期 `cover=4` 让 `ctrl=4` 的旧 ADV 消息层仍可见 → H025 | **D219 已做一半**：`PE_ReleaseParts` 递归释放整棵子树（`XSYS4_RELEASE_RECURSIVE=0` 回退）；`h025fix2` 同配方截图里多层重叠的旧文字被清掉，基线 `d219b` 与 D217 逐项一致、`Game/Battle/*` 32 不变、零错误零崩溃。**仍缺**：让 `EraseLayer` 的 ID 落到实际挂部件的层（H033）—— 当前 ADV 场景文字 / 名牌仍残留；判据 `h025clean` 同配方截图无残留文字且 `Game/Battle/*` 不变 |
| `[UC-TEXSHADER]` | `render.c` `parts_render_text` | 用 `parts_render_texture` 画字形纹理一个像素都不出（同一纹理走矩形路径就正常）。已用精灵路径绕开 | 对比两条路径的 GL 状态（VAO/attrib/blend equation），或用 RenderDoc 抓两帧 |
| `[MENU-INVISIBLE]`（已收窄，非渲染问题） | 任务地图菜单 `MenuContext:7..13` 的可见性 | **D175 排除「被挡住」**：菜单部件实测 `vis=1`（`parts_is_visible()` 为真）、`layer_show` 全 1、`cover=1`；上一轮「`parts_layer_visible()` 没有调用点」是误判——它被内联进 `parts_is_visible()` 了。打开菜单后画面整片黑，但**没有全屏可见部件**（所有 1920x1080 部件都是正常 CG，`tex=1`），说明是地图场景撤掉后只剩 clear color | 改用「点菜单项后看活动是否装载」验收，不再纠结像素；判据：菜单打开后 `Game/CommonMenu/MenuContext` 装载且能触发下一个场景 |
| `[UC-ALPHA]` | `render.c` 用户组件绘制 | `(uint8_t)(X * parts->global.alpha)` 在该点算出 0，hover 底板整块消失，而同帧日志 `alpha=255` | 确认 `parts_params.alpha` 的类型与写入时机（`parts_update_global_alpha` 是否先跑） |
| `[D119-MOTION]` | `parts/motion.c` | motion 系统没有调用者：**全库表只有 `SetMotionData`/`GetMotionBeginFrame`/`GetMotionEndFrame` 三个 motion 导出，`AddMotion*` 全是死条目**（库表里不存在，AIN 永远调不到）。D186 已让「首次 `parts_add_motion` 自动开表」，但没有上游调用 | 要么按 `SetMotionData` 的命名 motion 数据实现播放，要么确认 Rance10 2D 部件动画确实不用 motion 后删掉死条目。判据：某个真实淡入/滑入在日志里 `motion_t` 递增 |
| `[D119-SPLIT]` | `hll/String.c` `String.Split` | 用"伪堆槽"返回数组，靠调用方约定识别，凡依赖它切数据的地方长度都可能错 | 给 `hll_function` 加 `returns_page` 标志，按 AIN lib30 索引重排 String 表 |
| `[ARRAY-COPY]` | `Array_Copy_` / `array_copy()` / `A_COPY` | D134 只覆盖了三个"返回新数组页"的桩；其余复制路径未核对是否 `heap_ref`、旧值是否 `unref`。启动期仍有 2 条 `double free of slot 78/92` | 逐个核对写入端；用 `XSYS4_DF_FREE_DBG=1 XSYS4_SLOT_ID=78` 复现 |
| `[D119-LAZY]` | `src/vm.c` `last_ref_*` + `XSYS4_DG_LAZY` | 惰性求值试探留下的全局状态，与正常求值路径并存 | 删全局与开关，确认无调用方依赖 |
| `[R_ASSIGN-NULL]` | `src/vm.c` `R_ASSIGN` | 目标页为 NULL 时静默跳过并保留栈——是容错，不是语义 | 找到产生非法目标引用的上游（通常是槽宽错位），从源头修掉后改成断言 |
| `[ZERO-STUB]` | 多处 | 一批 HLL 是"返回 0 的桩"，尚未被调用到 | 用 `XSYS4_CALL_TRACE` 确认是否在真实路径上；在则实现，不在则标注 |
| `[CLICK-FLAKY]` | `tools/run/run-live.sh` 的 `click:` | 同配方有时推进有时不推进。**已定位到三层，都不是引擎丢事件**：① 坐标误点（`(1055,843)` 落在按钮上方 37px 的面板 CG；`s1` 的 `1668,230/330` 落在 `900000505`；`s2` 改点 `1600,450` 后立刻进战斗）；② **按住时长太短**（D245）：`mousedown→mouseup` 只有 `0.12s`，而实测 13.5fps=74ms/帧，按下态落在帧边界就整帧采样不到，40 次点击只有 9 次 `EndInput`，改 `0.2s` 后升到 15 次；③ **脚本卡死**（D246）：无 WM 时 `xdotool windowfocus --sync` 会永久阻塞，`s41/s42` 的 `click:1845,818` 一次都没执行（`pos=(1845` 出现 0 次）而进程还活着、日志还在涨 | 配方判据：以 `[inp] EndInput clicked=N` 的条数与部件号为准，不看"我点了几次"；点击前必须 `key:Shift_L`；`click()` 内所有 xdotool 调用都要带 `timeout` |
| `[BAD-SLOT-REF]` | `Array.Find` 谓词族 → `heap_unref` | `Array.Find(ref array<string>, 谓词)` 的某个元素是垃圾值 **`821768443`**，被当槽号 unref → 越界 SIGSEGV（gdb：`heap_unref(slot=821768443)`）。崩溃前日志同号 `Invalid string index: 821768443 (type=-1)`。**D240（给 REF 类补引用）实测无效**——崩溃栈一字未变；D241（`heap_unref` 加上界检查）只是不再崩 | 用 `XSYS4_DF_FREE_DBG=1 XSYS4_ARR_DBG=1` 抓「谁把 821768443 写进引用字段」：判据是能复现该值被写入的指令地址 / 槽号。值 **`821768443` = `0x30FB30FB`**，即两个 UTF-16 的 `・`（U+30FB）—— **不是随机垃圾，是字符串内容被当成了槽号**（类型混淆）。嫌疑：`[ARRAY-COPY]` 的浅拷贝、`[D119-SPLIT]` 的伪堆槽、或 `S_EQUALE` 按值传参 |
| `[BATTLE-STALL]` | `SceneStack@Join`(29656) + `Ｐ＿点击実行`(7659) | **不是卡死，是「演出靠点击刷新帧」+ 点击捕获率不足**（初判「全屏 flat 705 吞掉点击」已证伪）。机制（反汇编确认）：`Join` = `init; loop { if (IsFinish()) break; Ｐ＿点击実行(0); } done`，而 `Ｐ＿点击実行` 通体只有一句 `return parts::detail::WaitForClick(キー取消)`，`WaitForClick` 每轮做一次 `AFL_View_Update` —— **所以每点一次只刷新一帧画面**，一段 N 帧的演出要 N 次有效点击。`s44` 实测：`SceneStack@Exit`(29658) 5 次、`IsFinish::set` 5 次、`SceneBattle@Run`(24882) **仅 1 次**（不存在「每帧重建」）。瓶颈在捕获率：`s45` 30 次点击只有 **14 次** 按下边沿，`WaitForClick` 的退出条件 `GetClickPartsNumber() > 0` 每次点击都满足，但 14 次里只有 5 次真正让子场景结束 | 配方：密集点击（去掉每次前的 `key:Shift_L`，首点后焦点已稳），判据看 `[inp] EndInput` 条数与 `[call] 29658` 计数同增。另见 D245（按住 0.2s）/ D246（xdotool 全加 timeout） |
| `[BATTLE-UI-BLANK]` | `activity.c` 布局盒 FREE + `SetShowForBackScene` 空桩 | **两个独立原因**：① **底板不画**——`LargeSkillButton` 的 5 个子部件（`Button/SkillIcon/SkillName/Desc/ApBase`）座标**全是 `(0,0,1)`**，完全靠活动根的 `レイアウトボックス`（`配置=1`）重排，而 D214 把布局盒一刀切改成 `FREE`（保留绝对坐标）→ 五个部件全叠在原点，画面上只剩文字；② **按钮点不到**——`LargeSkillButton.Button` 与全屏 `BattleInfoView.Flat`(705) 的 `点击許可` **都是 0**，唯一区别是 `光标选中透過`（按钮 **1**、背景 **0**），而 705 ctrl=2 + `gz=1<<30+1125` 排第一且 `pass=0`，于是点 (1845,818)（「防御 1Ap」）命中的是 705（`s47` 候选表实测）| ① **已修（D275）**：底板不画的真因不是布局盒，而是 flat 零纹理库 —— D200 的 TALT 兜底只认 `QNT` 魔数，`战斗画面/シス／战斗／技能按钮／一般` 是 **AJP**，`talt_extra=0` → `libs=0` → `render_flat_item` 直接 return。改用 `cg_check_format()` 后 `libs` 0→1、底板出图，`XSYS4_TALT_EXTRA_FORMATS=qnt` 可回退（D214 的 FREE 判定经 A/B 确认是对的，勿再动，见 H049）；② `SetShowForBackScene` 从空桩改为可生效（D248，开关 `XSYS4_BACKSCENE_SHOW`），观察 AIN 是否用它隐藏 705（判据：`[backscene]` 实参指向 705 且 `EndInput clicked=` 落到技能按钮）。**已证伪的三条旁路**（都实测过，别再走）：① `XSYS4_CLICK_SKIP_NONCLICKABLE=1` 过滤 `clickable=0` → `EndInput` 直接变 0（`s48`）——Rance10 里所有可点部件 `点击許可` 都是 0；② `XSYS4_CLICK_SKIP_FLAT=1` 过滤 flat 类型 → `EndInput` 同样变 0（`s56`）——**开始按钮 716 自己也是用 `フラット名` 画的**，`PE_SetPartsFlat` 会把它变成 `PARTS_FLAT`，一滤就把按钮滤没了；③ `XSYS4_EX_LAYOUT_BY_CONFIG=1` 让真布局盒按 `配置` 重排 → 画面严重错乱（`Round`/`1`/`/`/`10` 竖排、AP 球消失、Battle 活动 45→32、`EndInput` 0，见 `H049`）。**新线索**：705 与技能按钮的 `gz` 不在同一量级（705 = `0x40000000+1125` 置顶，技能按钮仅 2102），而同族的 `BattleInfoView.Button`(716) 是 `0x40000000+1136` —— 说明 AIN 给 `BattleInfoView` 一族设了「置顶 z」，却没给 `SkillButton` 一族设，这条差异比 `pass_cursor` 更值得查。另：`900000760/766/772` 的 hitbox 是 `(965,662,1920,1080)`，x/y 对但**宽高被写成了视图尺寸**，也是待查项 |
| `[BATTLE-FLAT-CLOCK]` | `parts_flat_update` 的 `passed_time` + `SceneStack@Join` 自旋 | **AIN 卡在哪已经确定**：`s58` 的 `[flatend] no=900000813 frame=66 end=75 -> 0` —— 一直在等这个 **75 帧**演出 flat 播完（ctrl=5 全屏）。而帧推进是**点击驱动**的：`20` 次点击只让它从 63 走到 66（**+3 帧**），`[flatf]` 显示每轮 `pt` 只有 6~19ms，而 fps=30 需要 33ms/帧。`Join` 的循环每轮只 `WaitForClick → AFL_View_Update` 一次，所以「播完 75 帧」要几十上百次有效点击。硬件后端（85fps）没有改善这一点，说明**不是渲染性能，是时序模型** | 判据：让 813 在自旋期间按**墙钟**推进（而不是靠每次点击推一帧）后，`[flatend] 813` 能在 2.5 秒内到 `end=75` 并返回 1；同时观察 `705` 是否随之被隐藏。改动点在 `parts_flat_update` 收到的 `passed_time` 来源（`view::detail::CalcPassedTime`）与 `vm_frame_pump` 的配合 |
| `[HACK-POC D92]` | `render.c` `parts_render_update()` + `vm_frame_pump()` | 每帧无条件把所有部件精灵标脏、再 `scene_render()+gfx_swap()`（parts 写操作没普遍 `parts_dirty()`）。**帧率主要取决于后端而不是这条 hack**（D249）：同一场景软件 `llvmpipe` **13.5fps**、硬件 `Mesa Intel(ADL-N)` **99.7fps**，战斗里 85fps | 让每个 parts 写操作都走 `parts_dirty()`，帧心跳只在脏时提交。**降级为可选优化** —— 硬件后端下已不是瓶颈；但 headless（Xvfb）跑测要提速，仍需先走 `XSYS4_REAL_DISPLAY=1` |
| `[D85-MOTIONDATA]` | `PE_Noop_SetMotionData` | 空桩；Rance10 只有 1 处调用（fno 7662），一旦走到就没有 motion 数据 | 按 `GetMotionBeginFrame/EndFrame` 语义从 motion 资源读命名数据 |
| `[VIEWPORT-RESET]` | `video.c` `gfx_reset_framebuffer()` | 把 viewport 重置成窗口尺寸，但绑定的是视图尺寸的 FBO；非 1080p 屏幕下不一致 | 改为重置为 `config.view_width/height`；确认没有调用点依赖"画面缩到窗口大小" |
| `[ADV-BATTLE-ENTRY]` | `QuestEventProcess@RunAdvAndBattleEvent`(23486) | 开场脚本 `０２／ＳＴＡＲＴ`(32194) 只有 **53 条 MSG**（128960–129012）；旧记「221 条」是把后续函数 32195+ 一起数了（D214 纠正），每句需 ~2s 逐字动画。实测 0.5s/击 350 击→52 句（效率 0.15）；2.2s/击 320 击跑满 14 分钟 → `SetMessageWindowText` 333 次（约 95 句），全程零崩溃/零运行期未实现 HLL，仍未到 `23489 RunBattleEvent → 26388 RunBattleSceneSet → Game/Battle*` **时长问题已解决（D205）**：官方 `Manual/r01.html` 的「按住 Ctrl 不论是否已读强制快进」有效，60 秒 ≈ 逐点 14 分钟进度；`keydown:/keyup:` 已加进 `run-live.sh`。硬置 `global[28]`（`IsSkip`）证伪并回退。**根因已改判（D206，不是 UI 问题）**：`CreateSelection`(23603) 开头 `CALLHLL 5 19`（`Array.Empty(this.m3)`）为真时走 `SceneQuestMap@DebugForceExit`(23627) 直接返回 —— 调用轨迹实测 **`23627` 命中、`QuestMapSelectionButton` 活动 0 次装载**，所以任何坐标都点不到按钮。同一时刻 `23486` 第 2 次也因事件列表为空而不进 `23487/23489`。`GetQuestEventList`(28578) 靠 `GetStoryFunctionName`(28568)/`GetBattleEventName`(28569)/`GetBattleMovieEventName`(28570) 三个 getter 拼事件名，非空才 PushBack —— 三者全空 ⇒ 空列表。判据改为：`m3 nr>0` + 三个 getter 至少一个非空 → `23489` → `Game/Battle*`。 |

---

**硬编码 3 处**（违反"不依赖绝对路径"）：`src/ffi.c:108` → `/tmp/xsys4_stubs.txt`；
脚本里的 `/data/data/com.termux` 检测；`AliceStart.ini` 的 1920×1080。

---


**已排除**：坐标 / `clickable` / `ACT_CLICKABLE_ALL` / `parts_is_visible` 四个 UI 方向（D206）、「查询错」（`XSYS4_FN_DUMP=5467` 实测 key 正确）、「后门跳过节点选择」（真实流程同结果）、「点节点能推进」（`H021` 证伪）。详见 `docs/HYPOTHESES.md`。

**D209 已打通战斗**：`XSYS4_DUMP_AT=45c8aa` 读出 `this.m0 = "任务数据.荷鲁斯的宇宙飞船.００１"`；`index` 是同节点内 `.战斗２` 序号后缀，循环逻辑正确。`００１` 无 `战斗` 字段、`００２` 才有 `m11杂鱼／１`。诊断后门 `XSYS4_NODE_DIGIT=2`（仅 `23486` 链生效）→ `23489 → 26388` 首次命中；补 `MainEXFile.Save/Load` 与 `AFAFactory` 搜索族 6 个导出后，`Game/Battle/*` 32 个活动装载、VS 画面出图、零错误零崩溃。全局早改会在 `savedata.c:213 add_value_to_gsave` 断言崩溃，故后门必须限定事件链。**核心结论**（原 HLL-GAP 清单，全文已归档至工作区根 `archive/Rance10_HLL-GAP-全文-20261115.md`）：不存在战斗专用 HLL 库（静态普查仍成立），但**只扫 `Battle*` 函数会漏掉跨库调用链** —— 战斗前快照存档（`MainEXFile.Save/Load`）与战斗 UI flat 名解析（`AFAFactory` 搜索族）都不在战斗函数里。3 组缺口全部补齐、实测进战斗零未实现 HLL。**风险评估必须配一次真实跑测**；普查命令与两个前缀坑见 `docs/OFFLINE.md`。

**`H023` 已清偿（D214/D215）**：默认零开关、无任何后门下真实流程自然跑通
`23559 OnClick → 23624 → 23606 StartMoveRoute → 23618 → 23620 → 23486 #3 → 23489 → 26388`，
`Game/Battle/*` 26 个活动装载、VS 画面出图，零错误零崩溃。两处真修复：
① **D214** `activity.c`：EX 布局盒/用户组件被 `PE_SetComponentType` 映射成
`PARTS_LAYOUT_BOX`，`parts_state_reset` 默认 `VERTICAL`，`parts_do_layout` 把
EX 静态绝对坐标按子部件高度累加垂直重排（实测 `0→1080→1306→2016→2056`），
回想对话框按钮被推到屏幕外，AIN 卡在剧情收尾；EX 装载时初值改 `FREE`
（保留绝对坐标），`XSYS4_EX_LAYOUT_FREE=0` 可回退。连带把标题 Chapter1
命中区从错误的 `100,60` 纠正回 EX 声明的 `646,738`，`baseline.sh` 已同步。
② **D215** `savedata.c`：`add_value_to_gsave` 的 ARRAY 分支还是裸
`assert(page->type == ARRAY_PAGE)`，而同文件 STRUCT 分支早有 L2 容错；真实流程
战斗前自动存档时 `g_gameContext.QuestMapRestoreInfo.MarkerErased` 槽不是数组页，
直接 abort。按同一口径补 `unset`(rank=-1) + 限流告警，读侧往返无损。
**剩余**：回想对话框文字残留未清理（渲染层）、`[ADV-TEXT-RENDER]` 未清。 |

## C. 长尾（冻结记账）

| 项 | 数量 | 处置 |
|---|---|---|
| `[HACK-POC]` 标记 | **232 处**（`PartsEngine.c` 200、`SystemService.c` 27…） | 只加断言与日志，不改逻辑；等能跑到结局后集中清算 |
| `[VIEW-CFG]` | 1 | 渲染分辨率写死 1920×1080（`AliceStart.ini`），改为平台配置注入 |
| `[D119-FLATFPS]` | 1 | `SetFlatFPSRate` 收了不存，flat 动画速度不生效 |
| `[D119-MIXER]` | 1 | `KiwiSoundEngine` 没有 per-mixer mute 状态 |
| `[DI-FALLBACK]` / `[KEYCODE-PROBE]` | 2 | 输入映射的兜底与探针 |

## 已清零（现象 → 根因 → 契约/位置）

| 现象 | 根因 | 契约/位置 |
|---|---|---|
| `[QUEST-EVENT-EMPTY]` | 两个根因：① `string_format` 把 `%%` 当格式前缀跳过，`ResolveStringNodeId`(28494) 的 `%%0%dD` 产出 `3D` 而非 `%03D`，编号 `００１` 生不出来；② `Array.Find` 对 `a_type=AIN_ARRAY_STRING` 走整数分支比 heap slot，`００１` 明明在列表里却恒 -1。二者叠加使 KeyBase 变成 `任务数据.<地图>.`（缺编号段），`GetQuestEventList` 查不到事件 → `23486` 提前返回 | **已清零（D202）** |
| `Array.Find` 谓词 unref 越界 SIGSEGV（`s20/s22`） | 元素里是垃圾槽号 `821768443`，而 `heap_unref` 只挡 `slot <= 0`、**没挡上界** → `heap[slot].ref` 越界读 | D241 / `heap.c` 加上界检查 + 限流告警；**`s26` 实测生效**：`heap_unref: slot 821768443 越界（heap_size=110592），忽略`，同配方不再段错误；坏槽来源见 `[BAD-SLOT-REF]` |
| 战斗开始演出 flat 卡在 frame 2/15，战斗永远进不了可操作状态 | `PE_GoFramePartsFlat()` 的**向前定位**分支（`frame_no >= current`）没清 `root_state->stopped`，而 `flat_advance_layer()` 的增量是 `stopped ? 0 : 1` —— `pending_seek_delta` 明明被消费了，帧却原地不动（`is_stop` 是 flat 文件 op=2 的正常语义，停住后必须靠 AIN 调 GoFrame 继续）。附带：这张演出 flat 的 `global.z` 被 AIN 置顶到 `1<<30+1125`，会压住 `开始按钮`（`z=13`），所以演出不播完就点不到任何东西 | D239 / `flat.c` 向前分支补 `stopped=false`。**已验证生效**：`s32` 的 `[flatf] no=900000705 frame=14`（end=15，末帧），修复前是 2 | 
| 選択肢点掉后不进战斗（`[SELECTION-STALL]`） | 仍是点击坐标：`s1` 的 `1668,230/330` 落在 `900000505` 面板 CG；`s2` 改点 `1600,450` 命中 `900000526 → 23486 #2/#3`，确认层 `900000505 → 23489 → 26388 → Game/Battle/* 32` | `logs/live_s2.log`；配方 `click:1055,900` → `click:1600,450` |
| 单局 13720 次 double free | 三个数组浅拷贝桩裸复制引用元素、不给 `heap_ref` | 24 / `array_copy_values()` |
| `this` 变成堆槽 1（struct #0，3 成员） | `fno=0` 不建帧却仍 `set_struct_page()`；WRAP postset 多算一槽 | 26 / 2 |
| 地图装载 RSS 冲 9.5GB → OOM | `Array.Free` 是空桩 + `PushBack` 传槽数被当元素数再 ×2 | — / `arr_want_elems()` |
| 堆槽 4k→557k/118s | 槽号只分配不回收 | — / `heap_free_slot()` |
| `CALLMETHOD: invalid function index -1`（地图对象装载） | `A_REF` 复制接口数组时把 vtable 偏移当对象深拷贝 | 28 / `copy_page()` |
| 同一句柄失效、界面点不动 | 接口元素数组按 1 槽/元素分配，字节码用 `i*2` 索引 | 27 / 三处一致 |
| 开始界面蓝框 + 标签重复 | 按钮是 `TextButton` 内建控件，不是 CG；EX 预览文字层又画一遍 | — / `render.c` + `text_plain` 比对 |
| 任务面板有框没字 | 消息窗口三函数是空桩；去重判据拿 `h==16` 当语义 | — / `PE_SetText` |
| 标题菜单点不动 | 用户组件 `数据` 没写回引擎 → `TextButton@Init` 直接 return | 20 / `PE_SetUserComponentData` |
| 点击随机生效 | 帧心跳缺输入阶段；输入窗口未开时消费了按键脉冲 | 13 / `PE_PumpInput()` |
| 画面"有部件、无变化" | 帧心跳不推进动画时钟 | 12 / `parts_advance_animation()` |
| 结构体数组元素全是 0 | `Array.Alloc` 不构造元素 | 14 / `Array_Alloc_()` |
| 越界读直接杀进程 | `stack_pop_var` / `heap_get_string` 用 `VM_ERROR` | 15 / 21 |
| 切场景后标题整层还在 | 图层号被当部件号；遮挡靠"压栈即盖住" | 18 / 19 |
| 汉字显示方框 | 码表对、字体缺简体字形 | 23 / fontconfig 查 CJK |
| 界面点击被容器抢走 | 命中遍历用插入时刻的 z 快照 | — / `PE_UpdateInputState()` 内排序 |
| 存档写出去读不回 | 泛型 `array` 三处 switch 缺分支；解析器校验不认 `-1` | 29 / 30 |
| 原版 gsave v8 读不了 | 版本集合与 group 前两个 int 未跳过 | 30 / `libsys4` 本地补丁 |
| `PartsEngine.SuspendTexture` 中止 | 函数表里没有 | — / `PartsEngine.c` |
| 卡片正面合成中止 | `Parts_BuildPartsConstructionProcessThread` 等未导出 | — / `construction.c` |
| 页签下标 -1 → 空引用 | `String.GetPart` 差一位（1 基 vs 0 基） | 31 / `String_GetPart()` |
| 部队编制按钮点不中 | 跑测脚本假设窗口固定在 (0,0)，引擎中途重设几何 | — / `run-live.sh` `click()` |
| 点击卡片不进 `OnClickCard`（`[PARTY-CLICK-NOT-REACHED]`） | **不是 VM bug，是测试坐标选错**：一直点的 `(349,157)` 落在顶部图鉴/成就卡上；真正的人物卡 cursor `900001474` 在 `global=(31,420) 208x312`（另一张 `900001476` 在 `(251,420)`），`click=1 show=1`。前几轮据此推出的「UC 派发器不处理 Party」「注册被覆盖」全部是误判 | 点 `(135,576)`；判据 `XSYS4_CALL_TRACE=24300` 有输出 |
| `SetEnableInputProcess` 不能映射成可点击（`[EIP-NOT-CLICKABLE]`） | 试过又退回：`XSYS4_DBG_EIP=1` 实测 242 次调用里 **238 次 `enable=0`**（活动初始化统一先关一遍），映射到 `parts->clickable` 会把活动文件 `点击許可` 打开的按钮全关掉，整局从 107 活动掉到 27 | 先弄清 AIN 侧怎么用这个开关（它管的是「要不要派发输入」，不是「这个部件是不是按钮」）。判据：关掉的部件里哪些随后又靠别的 API 打开 |
| 工具链的已知缺口（`[SDK-GAPS]`） | 详见 `xsystem4/docs/SDK.md` 末节。三个可量化项：① **24 个开关没有用途说明**（`xsys4 check --strict` 的返回值就是它）；② 6 个历史跑测脚本（`safe_run*` `storm`）与 `run-headless.sh` 职责重叠，未合并；③ 动作序列不支持读屏决策，只能靠 `until` / `clickunless` | ①看 `tools/switch-desc.txt`，补一行就少一个；②先确认无人引用再删；③真需要时再设计，不要提前造 |
| `Party._Leaders` 判「不空」但看着像没卡（已澄清） | 已澄清：`m_leaders` 是 **7 个元素**（不是 24），`ARRDBG` 实测 `any nr=7 pred=1 fno=22 -> 1` —— 里面确实有 `m_cardId != ""` 的真卡。所以 `CheckNoLeader` 不弹「没有领队」是**正确行为**；`m_org` 空是懒构建，`IsExistPartyAvailable` 走不到它 | 以后别再从这个方向找「为什么进不了编队」 |
| numeral 导出名缺 `Parts_` 前缀（已清零） | 已清零：整套 `SetNumeral*`/`GetNumeral*` 在 AIN 里叫 `Parts_SetNumeral*`，strcmp 一个都不等 —— 以为实现的都是死条目，真被调用的在 595 条未实现清单里。D164 已并列注册正确名 | 以后新增导出必须拿 `libs.txt` 的原文名字核对，不能按 C 函数名猜 |
| 菜单一进就 `IsCheckBoxChecked` 未实现（`[CHECKBOX-UNIMPL]`，已清零） | 复选框状态族全是空桩/TODO，`CommonMenu@SetCheckBoxState` 一读就断。不改 `struct parts`（`save.c` 顺序序列化，插字段会错位存档）→ 用 64 槽旁路表，`parts_get` 新建时清该号旧记录防复用串味 | D174 / `parts.c` `PE_*CheckBox*` |
| 编队/结算场景 `CASTimerManager - 句柄错误` + double free（`[CASTEMER-HANDLE]`、`[RESULT-DOUBLE-FREE]`，已清零） | **根因是 `Array.Alloc` 没构造多维内层数组**：`m1[X]` 取到 0 = GLOBAL_PAGE，`m1[X][Y]=index` 覆写 `global[14]`（CASTimerManager 单例）。D106 早在 variable_initval 侧见过同一形状 | D177 / `Array.c:Array_Alloc_` 复用 `alloc_array(rank, dims, …)`；默认开启，`XSYS4_ARR_MULTIDIM=0` 回退 |
| `SceneQuestMap@AutoSave` 撞 `PartsEngine.SaveThumbnail` 未实现（已清零） | 它是库表里的独立导出（libs.txt:438），不是 `PartsFunc` 的 func_id；`PE_save_thumbnail()` 本来就有实现，只差注册 | D177 / `PartsEngine.c` `HLL_EXPORT(SaveThumbnail, …)` |
| 导出名与 AIN 库表系统性对不上（`[HLL-NAME-DRIFT]`） | **同 numeral 一类，但面更广。** 自查：把 `HLL_EXPORT` 的名字与 `ain dump -l` 的成员名求差集，3194 个注册名里 2077 个在库表里找不到同名。抽样确认是真的：`AnteaterADVLogList` 库里叫 `AddNewLine`，我们注册成 `ADVLogList_AddNewLine` —— 无前缀的库名当成了符号前缀 | 按库逐个核对：`ain dump -l` 的 `--- LibName ---` 分段就是命名空间，符号名**不带**库名前缀。判据：某库的未实现条数从 N 降到 0，且该库功能真的生效 |
| `Parts@Release` 213 次失败（部件号超范围） | `PE_GetFreeNumber` 沿用 **Rance 01** 的 `1000001000`，而兰斯10 的 AIN 校验 `1～999999999` → `Ｐ＿削除` 每次拒绝删除，部件残留 | `PE_GetFreeNumber()` 默认基址改 `900000000`（D161，A/B 报错 213→0、两条路径零回归） |
| 编队界面被全屏「新成就」效果盖住（`[PARTY-TROPHY-MODAL]`） | `Array.Where` 桩**忽略谓词**、原样返回整份数组 → `CompletedTrophy@GetNewCompleted()` 变成"全部成就" → 每个成就开一个模态 | `Array_Where_()`（D158 真实现谓词过滤） |
| 图鉴列表空白 / `TrophyCardSet@GetList` 拿空 `this`（`[TROPHY-VTABLE]`） | 同一根因：40+ 个模态各取一次卡列表；**不是** vtable 坏 | 同上 |
| 委托帧少一格 → `Invalid page index (type=1)` → core | 空引用调用降级只弹不压，调用点后的 `POP` 吃掉调用者一格 | 32 / `method_call()`（D154 补 `-1` 返回值） |
| `Tab10` 被当成 `Tab1`（`m_btn[9]` 永不初始化） | 两参 `GetPart` 被实现成「取 1 字符」 | 31 / `String_GetPart()` |
| 信息对话框点不动（`[DIALOG-STUCK]`） | **不再复现，非独立 bug。** 分两步修的：D142/D143（消息窗口三函数是空桩 + 去重判据吞正文）、D149（跑测点击落在别的窗口上）。判据：点 `960,540` → `Game/Party/SceneParty` 装载（`logs/live_dlg1.log:10292`） | — / `PE_SetText` + `run-live.sh` `click()` |
| 自动存档缩略图写不进目录（`[THUMBNAIL-SAVE]`） | D177 后走到 `SceneQuestMap@AutoSave`，`PE_save_thumbnail` 报 `Failed to open logs/savedir/<tag>/SaveData/Thumb9999.qnt: No such file or directory`；只是 warning，不阻断流程 | 确认 AIN 传入的存档目录是否已建；判据：该 warning 消失且 qnt 文件生成 |
| `OnClickCard` 拿到空 cardId、加卡链路看着像断了（`[AUTO-CLICK-EARLY]`，已清零） | **不是 VM bug，是后门时序**：`AUTO_CARD_CLICK` 固定 12000ms 把 `vm_call` 重入插进 `Activity@Load` 缝隙，此时 `OrganizationCardView` 未装载、`PartyCardGroup.m_cards nr=0`；日志里 `Init / InitCardView / CreateLoadingQueue` 全在点击之后 | 触发改成「最后一次 `InitCardView` 后 1500ms」（D179 / `vm.c`）。修复后 cardId 有真值、`l7` 是有效 PlayerCard 页、调用栈回到 `WaitForClick` 帧泵 |
| `AddCardToFormations`(24317) 不命中（`[PARTY-ADD-CARD-UNVERIFIED]`，已清零） | **不是数据缺失**：开局有卡的组织都已编入编队，单次点击只走 Remove；未编入的组织卡片列表为空。用游戏自己的逻辑造状态即可 | D180 / `XSYS4_AUTO_CARD_REPEAT=2`：第一击 24315 移除 → `fIndex` 0→-1 → 第二击 `FindEmptyIndex` + 24317。判据 `err=0` 且两击都有日志 |
| 连点后 349 层递归、栈溢出 core（`[AUTO-CLICK-RECURSE]`，已清零） | 把 `acc_done = true` 从 `vm_call` **前**移到之后，失去防重入屏障；`vm_call` 内的 `WaitForClick` 帧泵看到条件仍满足就再点一次 | `acc_in_call` 闸门 + 最后一击在 `vm_call` 前封门（D180 / `vm.c`）。判据：`click` 行数等于 `AUTO_CARD_REPEAT`，无「段错误/core dumped」 |
| Xvfb 上 SDL 窗口「运行中从 X 树消失」（已澄清） | **不是引擎/SDL 问题**：外部每秒 X 树采样显示窗口一直存在到跑测结束；`click()` 看到 `0 children` / `window:1135`(root) 是因为那些点击发生在游戏进程 timeout **之后** | D181 / `run-live.sh` 序列时序；判据 `XSYS4_CLICK_DBG=1` 下两次点击均为 `ptr=... window:2097204` + `2 children` |
| 序列点击被推到 timeout 后、必须给 120 秒（`[SEQ-CLICK-TIMEOUT]`，已清零） | `AUTO_MAP` 路径仍跑满 8 次无效右键标题循环（`SceneTitle` 永不出现），序列每步 ffmpeg 截图各等满 6 秒超时 | `AUTO_MAP` 时跳过右键循环、逐步截图默认关（`RANCE10_STEP_SHOT=1` 开回）（D182 / `run-live.sh`）；判据 SECS=55 两击闭环且默认路径 `SEQ_LAST` 不变 |
| 编成界面卡片正面无立绘（`[CARD-CG-COMPOSITE]`，已清零） | 合成链四处断点：776 空桩、`cmd=29` 被当 unknown、`ArrayString` 元素为 0、合成类部件 alpha=0 | D183/D184/D188 / `PartsEngine.c`、`construction.c`、`Array.c`、`parts.c` |
| `Array.Alloc` 引用型元素清 0、CG 名恒空（`[ARR-UNSET]`，已清零） | 0 是堆槽 0（全局页），AIN 对该槽的写入被静默丢弃 → `Invalid CG name` 113 条 | D188 / `Array.c` 默认翻正，`XSYS4_ARR_UNSET_FIX=0` 回退 |
| 编队界面「戻る／決定」点不动、退出后整屏黑（已清零） | **右键消息引擎从未发送**：`SceneStack@RegisterEvent` 注册 `AFL_Parts_AddWholeMouseRClickEvent`，而 AIN 的 `CallDelegateMouseClick` 靠消息第三变量 KeyCode 挑 delegate 列表（1→this[7] 左键、2→this[8] 右键）。引擎只发左键 | D191 / `parts/input.c` + `input.c` `mouse_rclick_pulse`；判据 `SceneParty@OnRClick(24290)` 命中 |
| 退回地图后 `cover_layer` 卡在 2、ctrl 0/1 全被滤掉（已清零） | `ctrl_stack.opaque[]` 只增不减，只在 `RemoveController` 里清；「编队层曾不透明」永久成立 | D190 / `parts.c` `parts_update_cover_layer` 每帧 `memset` |
| ADV 背景纯黑（已清零） | `PartsEngine.UpdateComponent`(f12) 是 AIN 每帧主更新入口却为空桩；真正调 `PE_Update` 的导出注册在库表不存在的 `Update` 名上 | D203 / `PartsEngine.c` |
| 剧情消息恒空、`AddMessageText` 0 次（`[MSG-TEXT-EMPTY]`，已清零） | ① AIN v12 无 `MSGF` 段，`ain_open_conv` 的 `xcalloc` 留 `msgf=0`（NULL function），`_MSG` 三参数被吞；② `AnteaterADVLogList.AddText(Text,WindowName)` 两参数皆 `AIN_STRING` 按值，库表却绑 `struct string**` ref 实现，`string_append` 收到 0x100000800000008 而 core | D204 / `hacks.c:resolve_v12_msgf()`（按名解析裸 `message`/3 → Rance10 fno 6100，默认开、`XSYS4_MSGF_FIX=0` 回退）+ `AnteaterADVEngine.c:_PreLink` 的 `AddText` by-value 适配 |
| `[ADV-TEXT-RENDER]` 剧情正文 / 消息框不出（已清零） | 两处真因：① EX「座標を率として認識する=1」从未实现，`座標=(0,1,1)` 被 `(int)` 当像素 → 正文画在 `(0,-15)` 屏幕外（D216）；② 消息窗口 `种类別情报` 的 `字体尺寸=39`/字色/字距/行距没读、底板 `ＣＧ名` 被 `type==ACT_COMP_CG` 门限挡掉（D217）。`parts_state` 是 union，CG 与 TEXT 不能共存 → 底板改用独立 CG 部件承载 | D216/D217 / `activity.c`；判据：默认零开关真实流程 `logs/live_d217full.log`、`/tmp/d217full_16_wait.png` 出现背景 CG + 消息框 + 名牌 + 正文 |
| `AUTO_MAP` 路径 ADV 全黑、底板「一像素没画」（已澄清） | **不是渲染 bug**：`XSYS4_AUTO_MAP=1` 直调 `RunGameChapter1`，跳过了背景层初始化；半透明黑底板叠在纯黑背景上自然看不出。同一份代码走真实流程时背景 CG / 底板 / 名牌 / 正文全部出图，`[frame]` 探针也证实底板进了 `parts_render` 且准入全过 | 渲染验收一律用默认零开关真实流程；后门路径只用于快速抵达场景，**不能当画面判据**（见 `docs/GUIDE/01-evidence.md`）。`s3/s4/s5` 补证：自建序列缺 `960,540` 剧情推进链 + 每 tag 独立 `savedir`，连 `ReplayMemoDialog` 都不装载（`s5` 用 `until` 等 90s 也没有；`EndInput` 0 次、`Game/Battle` 0），末尾帧自然拿不到 |
| H025「战斗画面回想 / ADV 文字残留」（已澄清） | **不是旧层泄漏**：装载入口 1136-1140 行本就有同名替换 → `activity_release()` 逐个 `PE_ReleaseParts`；`[txtr]` 254 条是全程采样而非末帧快照；D225 同帧快照 `vis=1 by ctrl {7: 262}` 更直接证明只有当前战斗层可见、ADV 的 ctrl=3 全 `vis=0`。那些文字是**当前层内 ADV 消息窗口未及时卸载** | 观感项，不影响第一章链路与可玩 |
| 回想对话框/回合场景「不结束」（**H044 已结案：装置问题**） | 不是引擎 bug：点击链整条通（`EndInput clicked=…` → `CallFunctionMouseClick`(7942) → `SceneReplayMemoDialog@<lambda>`(36563) → `SceneStack@Exit`(29658)），`IsFinish` 实测 0→1。真因是**点击坐标高了 37px**：`(1055,843)` 命中 862x226 的面板 CG 而非按钮；按钮命中盒是 `(984,880) 196x40`，改点 `(1055,900)` 后三次全中 | 已结案；配方统一用 `click:1055,900` |
| 第二次点击进战斗后全屏变黑（**已清零 D232**） | cover 地板判定把「子时间线容器关键帧的 alpha=255」当成真的画出了像素，而那张 1920x1080 演出 flat 在最后一帧所有子层都超范围或 a=0；于是 `cover=8` 把 ctrl<8 的 83 个 UI 部件全滤掉 → 全黑 | D232 / `flat_layer_paint_alpha()` 容器不贡献 alpha、只递归子层；判据 `XSYS4_NO_LAYER_COVER=1` A/B：10579B（黑）→ 2157158B（有画面） |
| 回合切换偶发 SIGSEGV（`heap_unref(slot)` 越界，**已清零 D227**） | `Array.Find(ref array<string>, 谓词)` 走 HLL→AIN 回调时把元素**副本**交给被调帧，按值参数是「所有权转移」，被调帧 `S_EQUALE` 用完 unref 就吞掉了数组那份引用 → 悬空字符串 | D227 / `vm.c` `vm_call_hll_closure()` 对按值且拥有引用的实参补 `heap_ref`；判据 core 栈消失 + 基线 `d227` 四数同 `d224` |
| 合成点击"点了没反应" + 动画慢 30 倍造成的假死（**已清零 D233/D236/D237**） | 三件事：① 合成点击的 press+release 落在同一次事件泵里，AIN 轮询 `IsDown` 永远是 0（`CASClick` 是边沿检测）；② 帧泵按「每 4096 条指令」触发，可指令稀疏的等待循环 4 秒才泵一次 → 76 帧演出跑 40 秒；③ `PE_IsPartsFlatEnd()` 未装载时返回 true =「一创建就播完」 | D233 / `input.c`「先被看见再抬起」；D236 / `vm.c` 帧泵改按墙钟；D237 / `flat.c` 未装载返回 false |
| 效果音 `[系统／战斗插入敌方行动]` 加载失败（资源缺失） | `e2e` 日志 5 次 `警告 效果音[系统／战斗插入敌方行动]加载失败`，对应 5 个 `EnemyActionIconBase:0..3` 装载；是 AIN 自己的 GBK 警告，不是引擎错误，不影响画面 / 逻辑 | 补该 SE 资源或确认 asset 查找路径；属音效打磨 |
| **数字 / 计量部件整条 CG 被当一张图画出**（`Round` 显示成 `0123456789` 重叠） | ① EX 类型名带 `パーツ` 后缀（`数字パーツ` 或 `横向计量パーツ`），`component_type_from_name()` 只认短名 → 未知类型回退成 CG；② 数字/计量负载（`幅リスト/数值/字间距`、`分子/分母`）从未解析 | `activity.c`：通用后缀兜底 + `get_float()` + 数字走 **COMBINED** 的 `PE_SetNumeralLinkedCGNumberWidthWidthList`（不是 SEPARATE 的 `SetNumeralCG`）/ 计量走 `PE_SetHGaugeCG`+`Rate`。判据 `s13` 引擎自截图：`Round 1 / 10` + 红蓝 HP 条（`Gauge (1055,597) 841x82`）出图；`Hp` 的 99999 在右边缘 `(1895,628)`，可能被 `Name` 遮挡，属观感 |
| 「长时间等待后整屏变黑」（D224 已清零） | **不是等输入淡出、也不是渲染准入或重绘问题**：`h025long` 的 390 字节末帧是抓屏空图（与纯黑 Xvfb 对照图 md5 一致），据此做的像素差结论无效。同帧 CG 绘制日志显示黑屏期 `since_last=17313` 次绘制、`mul` / `scale` 全正常，最后一次绘制是 `no=900000613` 全屏淡出遮罩（2x2 纹理 × `scale=(960,540)`、`a=255`、`z=INT_MAX`）。**真因是 D188 兜底修复**：`PE_SetAlpha()` 把 `PARTS_CONSTRUCTION_PROCESS` 的 `alpha<=0` 无条件抬回 255，误把 AIN 用来隐藏遮罩的 0 抬成不透明 | 已修：`parts.c` 收窄为 `common.w>=8 && h>=8` 才提升，`XSYS4_NO_CP_ALPHA_FIX=1` 可整体回退。判据 `h037fix` 引擎自截图 10579B→1207952B 出战斗 UI、基线 `d224` 四数一致；卡片号段 alpha 分布前后不变 |
