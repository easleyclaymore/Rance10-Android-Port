# 诊断开关总表

> 由 `tools/gen-switches.py` 生成，**不要手改**。改代码后重跑一次。

所有开关默认都是**关闭**的；只有显式设置才生效。
新增开关必须带一行用途说明，否则本表会显示 `—`，
它同时是一个可量化的债：`tools/gen-switches.py --check` 的返回值就是未标注数量。

共 **280** 个开关，其中 **0** 个没有就近注释。

**用途一栏的两种来源**：

- 正常文字 = `tools/switch-desc.txt` 里人工写的一句话（可信）
- 带 `〔摘〕` = 从源码就近注释摘的片段，**可能是背景说明而不是用途**，用前请核对源码

| 分组 | 开关 | 位置 | 用途 |
|---|---|---|---|
| 断点与快照 | `XSYS4_CALL_TRACE` | `src/vm.c:917` | 逗号分隔的 AIN 函数号；命中时打印调用点与返回地址（"有没有被调到"的第一手证据） |
| 断点与快照 | `XSYS4_CALL_TRACE_TOTAL` | `src/vm.c:7594` | [诊断] 退出时汇总 `XSYS4_CALL_TRACE` 各函数的**真实调用次数**（不受"每 fno 8 站点 + 每 200 次一条"的打印限流影响）。**凡是要断言"某函数跑了 N 次"，必须用这个** —— D280/D281 两次把日志条数当次数，把 `View_Update` 的 1504 次误读成 10 次、把 `OnUpdate` 的 150 次误读成 0 次 |
| 断点与快照 | `XSYS4_CENSUS` | `src/vm.c:1636` | 按函数做调用普查（与 FN_DUMP 互补） |
| 断点与快照 | `XSYS4_DUMP_AT` | `src/vm.c:4372` | 在 `<十六进制地址>[,最多几次]` 处停下，打印帧名、this 各成员、局部变量和操作数栈 |
| 断点与快照 | `XSYS4_FN_DUMP` | `src/vm.c:1019` | 函数调用次数统计与慢函数排行 |
| 断点与快照 | `XSYS4_FN_DUMP_ALL` | `src/vm.c:1035` | 统计时不做前 N 次闸门，低频函数也全打 |
| 断点与快照 | `XSYS4_FN_DUMP_FSALL` | `src/vm.c:1151` | [诊断] 配合 FN_DUMP_FSSCAN：连点击委托为空的 FunctionSet 也打出来 |
| 断点与快照 | `XSYS4_FN_DUMP_FSSCAN` | `src/vm.c:1140` | [诊断] 配合 FN_DUMP：扫描堆里所有 CPartsFunctionSet，打印有点击委托的 parts 号与委托函数名 |
| 断点与快照 | `XSYS4_FN_DUMP_SKIP` | `src/vm.c:1027` | 统计时跳过指定函数号 |
| 断点与快照 | `XSYS4_HLL_TRACE` | `src/ffi.c:595` | 打印每次 HLL 调用（`lib= fno= nr_args= sp= ret=`），不设 FILTER 会很吵 |
| 断点与快照 | `XSYS4_HLL_TRACE_FILTER` | `src/ffi.c:602` | 收窄 HLL_TRACE：`PartsEngine` 按库名、`27:73` 按库:序号、其它按函数名子串，逗号分隔 |
| 容器与内存 | `XSYS4_ALLOC0_DBG` | `src/page.c:445` | 0 号堆槽分配的诊断 |
| 容器与内存 | `XSYS4_ARRAY_DBG` | `src/hll/Array.c:1247` | Array 诊断的旧开关（与 ARR_DBG 同类，逐步统一） |
| 容器与内存 | `XSYS4_ARRAY_FREE_FIX` | `src/hll/Array.c:527` | 数组释放路径的实验修复（默认关，会改行为） |
| 容器与内存 | `XSYS4_ARRAY_REF_FIX` | `src/hll/Array.c:915` | 数组元素引用处理的实验修复（默认关，会改行为） |
| 容器与内存 | `XSYS4_ARR_ALLOC_DBG` | `src/ffi.c:797` | 只打数组分配 |
| 容器与内存 | `XSYS4_ARR_DBG` | `src/ffi.c:709` | Array 操作诊断：alloc/find/any/emp/free/subst 逐条打 |
| 容器与内存 | `XSYS4_ARR_MULTIDIM` | `src/hll/Array.c:351` | Array.Alloc 构造多维内层数组；默认开启，=0 回退旧行为做 A/B |
| 容器与内存 | `XSYS4_ARR_TYPE_DBG` | `src/hll/Array.c:1121` | 数组元素类型推导诊断（泛型数组"由首个元素定型"出问题时用） |
| 容器与内存 | `XSYS4_ARR_UNSET_FIX` | `src/hll/Array.c:454` | 未初始化槽读取的实验修复（默认关，会改行为） |
| 容器与内存 | `XSYS4_BADSTRUCT_DBG` | `src/vm.c:1567` | 结构体成员访问越界诊断 |
| 容器与内存 | `XSYS4_BADTHIS` | `src/vm.c:1536` | 非法 this（类型不匹配/越界）时打回溯 |
| 容器与内存 | `XSYS4_CTOR_DBG` | `src/page.c:461` | 构造函数调用诊断 |
| 容器与内存 | `XSYS4_DF_DBG` | `src/heap.c:216` | 委托释放路径诊断 |
| 容器与内存 | `XSYS4_DF_FREE_DBG` | `src/heap.c:325` | 委托释放的逐次释放诊断 |
| 容器与内存 | `XSYS4_DF_MAX` | `src/heap.c:219` | 委托释放的最大迭代次数（防死循环） |
| 容器与内存 | `XSYS4_DGA_DROP` | `src/vm.c:6651` | [诊断] 只在「DG_ASSIGN 目标是空槽、而源委托里有条目」时打 `[dgadrop]`（D324，实测 0 次 = 没有委托被丢） |
| 容器与内存 | `XSYS4_DGIDX_DBG` | `src/parts/parts.c:201` | [诊断] 逐次打印 `SetDelegateIndex` / `GetDelegateIndex`（部件↔委托索引绑定），用来判「绑定过没有、读回还是不是原值」（D320） |
| 容器与内存 | `XSYS4_DGNEW_DBG` | `src/vm.c:6795` | [诊断] 打印每次 `DG_NEW_FROM_METHOD` 的 (obj, fun, ip)。判「某个事件委托到底有没有被建出来」（D325） |
| 容器与内存 | `XSYS4_DGPLUSA_DBG` | `src/vm.c:6707` | 〔摘〕[D119 探针] 看看到底是哪张委托页收到了 OK/Cancel 两段 lambda |
| 容器与内存 | `XSYS4_DGPLUSA_MAX` | `src/vm.c:6711` | 〔摘〕前 400 条会被 startup 的 CParts@Attach 占满。 |
| 容器与内存 | `XSYS4_DG_APPEND_DBG` | `src/page.c:1087` | [诊断] 委托追加/丢弃各打一条（obj/fun/dst_nr）。用来分辨「没注册」和「注册被静默丢掉」 |
| 容器与内存 | `XSYS4_DG_ASSIGN_DBG` | `src/vm.c:6635` | 委托赋值诊断 |
| 容器与内存 | `XSYS4_DG_ASSIGN_NULLALLOC` | `src/vm.c:6676` | `=1` 让 `DG_ASSIGN` 在目标为空槽时分配新页并写回成员（D324 的 A/B，**已证伪**，默认关） |
| 容器与内存 | `XSYS4_DG_CALL_DBG` | `src/vm.c:1880` | 委托调用诊断 |
| 容器与内存 | `XSYS4_DG_DBG` | `src/hll/Delegate.c:65` | 委托（Delegate）诊断总开关 |
| 容器与内存 | `XSYS4_DG_DRIFT` | `src/vm.c:1743` | 委托页"漂移"检测（指向了不该指的对象） |
| 容器与内存 | `XSYS4_DG_GC_DBG` | `src/page.c:1121` | [诊断] 委托被 delegate_numof 的 GC 丢弃时打一条（obj/fun/seq）。分辨「没注册」与「注册了又被丢掉」 |
| 容器与内存 | `XSYS4_DG_GET_DBG` | `src/page.c:1221` | 委托取值诊断 |
| 容器与内存 | `XSYS4_DG_KEEPALIVE` | `src/page.c:1054` | 委托页保活（防止被提前回收） |
| 容器与内存 | `XSYS4_DG_LAZY` | `src/vm.c:6746` | 成员里的委托槽为空时惰性建页（NetworkConfirmation 的 OK/Cancel 靠它） |
| 容器与内存 | `XSYS4_DG_MISS_DBG` | `src/vm.c:1850` | 委托 miss（找不到目标）时打印 |
| 容器与内存 | `XSYS4_DG_MISS_FILTER` | `src/vm.c:1854` | 收窄 DG_MISS 输出 |
| 容器与内存 | `XSYS4_DG_PAGE_DBG` | `src/vm.c:1791` | 委托页生命周期诊断 |
| 容器与内存 | `XSYS4_MEM_STAT` | `src/vm.c:7068` | 堆内存与 RSS 实时统计 |
| 呈现与输入 | `XSYS4_ACTIVE_CTRL_SET` | `src/parts/parts.c:4552` | `=0` 把 `PartsEngine.SetActiveController` 退回空实现（D312 前行为）。默认真设置活动层 |
| 呈现与输入 | `XSYS4_ACTIVE_PARTS` | `src/parts/input.c:100` | [诊断] `PartsEngine.GetActiveParts` 返回光标指向的部件（默认开，D309）。`=0` 回退到旧桩（恒 0） |
| 呈现与输入 | `XSYS4_ACT_BUILD_DBG` | `src/parts/activity.c:1552` | [诊断] 打印每个 activity 部件的 `no/name/parent/type`（前 3000 条）。用来把运行时部件号对号到 XML 里的名字，例如 `900000397 = ButtonDeffence` |
| 呈现与输入 | `XSYS4_ACT_CLICKABLE_ALL` | `src/parts/activity.c:1229` | 把所有**活动文件里声明**的部件标为可点（运行时建的 900000xxx 不受影响） |
| 呈现与输入 | `XSYS4_ACT_DBG` | `src/parts/activity.c:814` | 活动装载与点击派发诊断 |
| 呈现与输入 | `XSYS4_ACT_DEBUG` | `src/parts/activity.c:1905` | 活动诊断的旧开关 |
| 呈现与输入 | `XSYS4_ACT_GEOM` | `src/hll/PartsEngine.c:4952` | [D360 诊断，默认关] `AFL_Activity_GetPartsNumber` 每查到一个部件号就把它的矩形/可见性打出来 —— 一次跑测即可拿到「按名字找到的部件到底在屏幕哪里」 |
| 呈现与输入 | `XSYS4_ACT_PART_DBG` | `src/parts/parts.c:3229` | 打印活动部件的建造解析（名字/类型/尺寸/表示領域/flat 名）与 300 号段之后的 SetPos/SetZ |
| 呈现与输入 | `XSYS4_ACT_RELEASE_DGLIST` | `src/parts/activity.c:1698` | [D270 已转正] ReleaseActivity 的 EraseNumberList 出参不再丢弃：把被释放部件的 delegate_index 写回 AIN，供 CPartsMessageManager@ReleaseFunctionSetList 销掉消息分发表里的委托（否则释放后的委托仍可命中，号复用时调到上一次活动的处理器）；默认生效，=0 回退做 A/B |
| 呈现与输入 | `XSYS4_ACT_RELEASE_DGLIST_DBG` | `src/parts/activity.c:1687` | [诊断] 打印每次 ReleaseActivity 释放的活动名、部件数与实际写回 EraseNumberList 的委托下标个数；用来证明 D270 的修复不是死代码（个数恒 0 就说明出参本来没被 AIN 用） |
| 呈现与输入 | `XSYS4_ACT_STEP` | `src/vm.c:1213` | 〔摘〕十几个，TextButton 等也是「活动」）。 |
| 呈现与输入 | `XSYS4_CLICK_ANY` | `src/parts/input.c:666` | [诊断/已证否] 点击捕获改为「非 flat 一律可捕获」。实测 EndInput 直接变 0（无效部件先消费点击）——保留仅作反例 |
| 呈现与输入 | `XSYS4_CLICK_BUBBLE` | `src/parts/input.c:579` | [诊断/已证否] 子部件被点时向父链再发一份 CLICK。s98 实测对面板关闭无效（12 次点击仍只 1 次捕获），说明断点是捕获而非冒泡 |
| 呈现与输入 | `XSYS4_CLICK_FLOW` | `src/parts/input.c:543` | 〔摘〕「点击号被读到之前是不是已经被 EndInput 清掉了」。 |
| 呈现与输入 | `XSYS4_CLICK_NEEDS_SHAPE` | `src/parts/input.c:390` | [D338 默认生效，=0 关闭] 让「画不出像素」的部件退出 hover 拦截与点击捕获（w/h<=0 / PARTS_UNINITIALIZED / 铺满全屏的 PARTS_LAYOUT_BOX）。转正依据：bt30/bt31 下第一章照常到战斗，点角色卡能落到真正按钮件上，AIN 随即弹出技能选择列表。注意只排除**全屏**布局盒——一刀切排除布局盒会让选项按钮失效（bt23 进不了战斗） |
| 呈现与输入 | `XSYS4_CLICK_ONLY_CLICKABLE` | `src/parts/input.c:424` | [诊断] 点击捕获与 hover 消费都只认「点击許可=1」。这是唯一让 EndInput 恢复正常的判据（点到整屏点击层 700/701） |
| 呈现与输入 | `XSYS4_CLICK_PASS` | `src/parts/input.c:428` | [诊断/已证否] 把「光标选中透過=1」当正向判据。实测 314(1920x693) 抢在技能按钮前，EndInput 变 0 |
| 呈现与输入 | `XSYS4_CLICK_RETARGET` | `src/parts/message.c:86` | 点击被遮挡后重定向到下层部件 |
| 呈现与输入 | `XSYS4_CLICK_SKIP_FLAT` | `src/parts/input.c:434` | [诊断] flat 类型部件不参与点击捕获。战斗里全屏背景 flat 705 的 hb 覆盖整屏、gz 置顶、pass=0，会把技能按钮的点击全部截胡；flat 在原版是「平面图形/演出层」，按钮是 按钮(0) 或 CG |
| 呈现与输入 | `XSYS4_CLICK_SKIP_FULLSCREEN` | `src/parts/input.c:439` | [诊断] 把「hb 覆盖整个视图」的部件排除出点击捕获与 hover 消费。依据：真按钮不会铺满整屏，而演出/背景/整屏点击层会；s93 实测能让点击落到 `ButtonDeffence`(900000397) |
| 呈现与输入 | `XSYS4_CLICK_SKIP_INVISIBLE` | `src/parts/input.c:459` | [诊断] 把 `global.alpha<=0`（完全透明、一个像素不画）的部件排除出 hover 消费与点击捕获。依据是 `parts_is_visible()` 不检查 alpha，导致全屏 flat `900000705`（a=0、z 比接收器还高）总先吞点击；d301 实测三次点击全落它 | |
| 呈现与输入 | `XSYS4_CLICK_SKIP_NONCLICKABLE` | `src/parts/input.c:644` | [诊断] 跑测专用：`clickable=0` 的部件不参与点击捕获（回到 D113 之前的行为）。用来验证「全屏背景 flat 705 截胡技能按钮点击」这一条；默认关，开始画面这类「点击任意处继续」的场景开了会卡住 |
| 呈现与输入 | `XSYS4_CTRL_ACTIVE_RESTORE` | `src/parts/parts.c:4605` | `=1` `EraseLayer` 后把活动层恢复到**建层前那一层**；默认回栈顶（上游语义）。D316 的 A/B，实测不改变 [BG-LAYER] 结果 |
| 呈现与输入 | `XSYS4_CTRL_DBG` | `src/parts/parts.c:2053` | controller 栈、视觉地板（cover_layer）、hide/remove 事件 |
| 呈现与输入 | `XSYS4_CTRL_PENDING` | `src/parts/parts.c:4494` | [D289] 默认开。`Activity@Load` 建出的部件归到「即将由 AddLayer 创建的那一层」，而不是当前 active。AIN 的顺序是 Load→AddLayer，旧行为让部件全落在上一层，之后按新层 ID 的 EraseLayer 必然 `released=0`（旧场景部件永久残留，标题 Logo 跟到战斗里吃点击）。设 0 回退旧行为做 A/B |
| 呈现与输入 | `XSYS4_INPUT_ACTIVE_LAYER` | `src/parts/input.c:211` | 〔摘〕注意：`PARTS_CONTROLLER_SYSTEM_OVERLAY` 是引擎自己的覆盖层，永远参与。 |
| 呈现与输入 | `XSYS4_INPUT_ALL` | `src/parts/input.c:925` | [诊断] 配合 INPUT_DBG 连"没覆盖光标"的部件也打印（带 cover= 标记）；避免把几何算错的按钮误读成"部件不存在" |
| 呈现与输入 | `XSYS4_INPUT_DBG` | `src/parts/input.c:624` | 输入命中的候选部件表与点击状态（每 60 帧一条心跳，非沉默） |
| 呈现与输入 | `XSYS4_INPUT_GATE` | `src/parts/input.c:43` | =1 恢复旧输入门：只有 AIN BeginInput 窗口内才处理按下沿（默认 0=等价原版，恒开）。菜单界面 AIN 不开窗口，开旧门会让点击全被吞 |
| 呈现与输入 | `XSYS4_PARTS_DUMP` | `src/parts/activity.c:1929` | 活动装载后快照所有显示中的部件（no/ctrl/z/pos/尺寸/a/ps/st/tex/uc） |
| 呈现与输入 | `XSYS4_PARTS_DUMP_MAX` | `src/parts/parts.c:4445` | parts dump 的条数上限（默认 400）。部件太多时调小，只看前 N 个 |
| 呈现与输入 | `XSYS4_PARTS_DUMP_MIN` | `src/parts/parts.c:4374` | [诊断] 只 dump 部件号 >= 此值的部件；部件表按插入序遍历，地图号段会吃光 PARTS_DUMP_MAX 名额，设 900000300 才能看到动态号段 |
| 呈现与输入 | `XSYS4_PARTS_EVENT` | `src/vm.c:2075` | [D337 默认生效] 引擎捕获到点击后，把「部件号」派发给 AIN 侧的按钮对象：AIN `Parts` 实例字段 7 上的委托走 `Parts@OnClick(no,x,y)`，`TextButton` 实例走它自己在 `Init` 里生成的 lambda（自带 IsClickable 判定）；精确匹配不到时按「点在容器上」处理，取容器内的 TextButton。这是原版由引擎完成、移植时整条漏掉的一拍，是 `[TEXTBUTTON-CLICK]` 的根因；=0 关闭做 A/B |
| 呈现与输入 | `XSYS4_PARTS_EVENT_DBG` | `src/vm.c:2752` | [诊断] 打印每次点击派发的候选对象数、TextButton 实例的 m3→部件号映射，以及没有匹配时的现场勘查；用来区分「点击没到」「映射断了」「委托为空」三种情况 |
| 呈现与输入 | `XSYS4_PARTS_EVENT_GEOM` | `src/vm.c:3332` | [D338 默认生效，=0 关闭] 点击派发时若命中号没有对应按钮对象，按点击坐标 PE_FindClickableAt() 找「覆盖该点的最上层局部 ck=1 件」改派。转正依据：bt30 里把 900001014（全屏件）改派到 900000749（角色卡）后，AIN 的 Parts@OnClick 真的跑起来；bt25 的「改派导致进不了战斗」是当时并存的变量遮蔽 bug，已修 |
| 呈现与输入 | `XSYS4_PARTS_NO_BASE` | `src/parts/parts.c:4111` | 〔摘〕历史文档/脚本里那些号不能再拿去 grep。 |
| 呈现与输入 | `XSYS4_UPD_DBG` | `src/parts/parts.c:2098` | PE_Update 的每帧诊断 |
| 自动化后门 | `XSYS4_AUTO_BATTLE_END` | `src/vm.c:2512` | [D344 诊断，默认关] 值=帧数。到点后直接调 SceneBattle@ProcessPostPlayerTurn(24886)，绕开「角色卡点不中」，单独验证回合收尾逻辑。abe1 实测：能跑、0 ERROR、画面进入回合切换演出 |
| 自动化后门 | `XSYS4_AUTO_CALL` | `src/vm.c:2669` | [D388 诊断，默认关] `<fno>[:<frames>]` 到第 frames 帧（缺省 600）直接调一次无参 AIN 函数 —— 把「跑到某屏要 10 分钟」压到几十秒，例如 `26415` = Scenes::RunTurnStart |
| 自动化后门 | `XSYS4_AUTO_CARD_CLICK` | `src/vm.c:1437` | [后门] 编队卡片视图初始化完成后直调 SceneParty@OnClickCard(idx)；值=卡片索引 |
| 自动化后门 | `XSYS4_AUTO_CARD_CLICK_DELAY` | `src/vm.c:1440` | [后门] 最后一次 InitCardView 之后再等多久点卡（毫秒，默认 1500） |
| 自动化后门 | `XSYS4_AUTO_CARD_ORG` | `src/vm.c:1442` | [后门] 点卡前先调 PartyCardGroup@ChangeOrganizationId(id) 切组织；0=不切 |
| 自动化后门 | `XSYS4_AUTO_CARD_REPEAT` | `src/vm.c:1444` | [后门] 连点同一张卡几次（默认 1）；第一击移除、第二击验证 AddCardToFormations |
| 自动化后门 | `XSYS4_AUTO_CLICK` | `src/vm.c:2554` | [D378 诊断，默认关] `<ms>[:<x>,<y>][:r]` 每隔 ms 在**引擎内**合成一次点击（mouse_inject_click，不经过 xdotool）。位置缺省为视图中心，`:r` 发右键。给「靠点击喂帧」的 ADV/战斗演出用 |
| 自动化后门 | `XSYS4_AUTO_CLICK_DELAY` | `src/vm.c:2612` | [D378 诊断，默认关] `XSYS4_AUTO_CLICK` 的启动延迟（毫秒）；标题那几步必须靠 RANCE10_LIVE_SEQ 精确点击，自动点击在旁边乱点会把标题菜单开开关关 |
| 自动化后门 | `XSYS4_AUTO_CLICK_N` | `src/vm.c:2566` | 〔摘〕不收敛。设 N=位置数 即「每个位置点一次」，序列完全确定。 |
| 自动化后门 | `XSYS4_AUTO_MAP` | `src/vm.c:7122` | 无头跑测后门：自动推进到任务地图 |
| 自动化后门 | `XSYS4_AUTO_MAP_DELAY` | `src/vm.c:7123` | 后门延迟（毫秒） |
| 自动化后门 | `XSYS4_AUTO_MAP_ROUTE` | `src/vm.c:1415` | [后门] 地图稳定后直调 SceneQuestMap@StartMoveRoute(23606)，毫秒=延迟；写成 `<ms>:<repeat_ms>` 则周期性反复推路线（一章的剧情/战斗都是「地图→移动→事件」，单次只能推一个事件） |
| 自动化后门 | `XSYS4_AUTO_PARTY` | `src/vm.c:1410` | 无头跑测后门：自动推进到部队编成 |
| 自动化后门 | `XSYS4_AUTO_PARTY_CLOSE` | `src/vm.c:1435` | [后门] 卡片视图就绪后直调 SceneParty@Close(24301)，验证编队出口；=1 开启 |
| 自动化后门 | `XSYS4_AUTO_PARTY_DELAY` | `src/vm.c:1411` | 后门延迟（毫秒） |
| 自动化后门 | `XSYS4_AUTO_RUNPARTY` | `src/vm.c:7210` | [后门] 地图起来后直接调 Scenes::RunParty(1)，绕开地图 UI 直进编队界面；值=延迟毫秒 |
| 自动化后门 | `XSYS4_AUTO_SKILL` | `src/vm.c:2716` | [D349/D373 诊断，默认关] 值=`<frames>[:<x>,<y>|:#<index>|:card|:cards]`。到点后派发技能卡：给坐标按子树选卡、`#n` 按序号、`card` 用第一个 BattleCardButton 的 (cardId,skillIndex) 直接调 SceneBattle@OnClickSkill、`cards` 每 600 帧轮流让每个卡放一次（最多 8 次）。用于把「链路问题」和「命中问题」分开验证 |
| 自动化后门 | `XSYS4_AUTO_SKILL_MODE` | `src/vm.c:3025` | [D368 诊断，默认关] =lambda 时 `XSYS4_AUTO_SKILL` 用原来的 lambda 派发（37029）而不是直接调 OnClickSkill，用来 A/B 对比两条路 |
| 自动化后门 | `XSYS4_NO_LAYER_COVER` | `src/parts/parts.c:1948` | 关闭「视觉地板」优化（怀疑遮挡算错时用） |
| 运行环境 | `XSYS4_DATA_DIR` | `src/pal.c:43` | 数据目录（缺省用编译期默认值） |
| 运行环境 | `XSYS4_SOFT_STUB` | `src/ffi.c:56` | 未实现的导出用零桩顶替（诊断用：判断"缺实现"还是"走错路"） |
| 一次性探针 | `XSYS4_DBG_EIP` | `src/parts/parts.c:551` | 打印 SetEnableInputProcess 的每次调用与参数（实测 242 次里 238 次 enable=0） |
| 其它 | `XSYS4_ALPHA_DBG` | `src/parts/parts.c:3332` | 打印 SetComponentAlpha 与 AddMotionAlpha 的 parts/alpha 序列（查部件为何全透明）。[D277] 现在同时打 `fno~` 提示来源；注意该值只是参考（见下条） |
| 其它 | `XSYS4_ALPHA_STACK` | `src/parts/parts.c:3343` | [诊断] 配合 ALPHA_DBG，在每次 SetAlpha 时打 AIN 调用栈。定位 alpha 被谁写成 0 必须用它 —— `hll_current_fno()` 返回的是上次 HLL 的值、返回后不复原，实测会打出 `fno=107`(CASConstructionProcess@SetSrcX) 这种完全无关的函数 |
| 其它 | `XSYS4_BACKSCENE_DBG` | `src/parts/save.c:756` | [诊断] 打印 SetWantSaveBackScene / SaveBackScene / LoadBackScene 的时机与规模（背景场景快照链） |
| 其它 | `XSYS4_BACKSCENE_SHOW` | `src/hll/PartsEngine.c:4530` | [诊断] 让 `SetShowForBackScene(no,show)` 真正生效（默认空桩）。战斗里全屏背景 flat 705 挡住技能按钮点击，这个 setter 是 AIN 本来要用来按场景隐藏背景的手段；开它可验证 `[backscene] no= show=` 的实参 |
| 其它 | `XSYS4_BACKSCENE_WANT` | `src/hll/PartsEngine.c:2318` | [诊断] `=1` 让 `IsWantSaveBackScene` 返回真实标记（默认仍恒 false）。实测对开场剧情背景无影响 |
| 其它 | `XSYS4_BCB_DBG` | `src/vm.c:6618` | [D376 诊断，默认关] `DG_ASSIGN` 写 `BattleCardButton` 的点击委托（37011/37016/37029/37568）时打目标实例页与成员号 —— 回答「赋值有没有落到被点中的那个实例上」 |
| 其它 | `XSYS4_BG_SWEEP_MAP` | `src/parts/parts.c:2509` | `=0` 关闭「根级 `背景／*` 换图时收起同层 `地图／*` 覆盖件的根」。默认开（D317，[BG-LAYER] 的收敛） |
| 其它 | `XSYS4_CAPTION` | `src/parts/activity.c:1002` | =0 关掉「非 TEXT 件的 EX 文本当标签画」（单选组选项标签，D514）。某界面出现多余文字时回退 |
| 其它 | `XSYS4_CAP_DBG` | `src/parts/input.c:320` | [诊断] 点击捕获归因：只在「按下那一帧、且命中盒覆盖光标」时打印每个部件的 vis/show/ck/pc/hover-consumed/began，用来判「为什么没人拿到 click_down_parts」（D332） |
| 其它 | `XSYS4_CGSET_DBG` | `src/parts/parts.c:2556` | [诊断] 打印每次 `SetPartsCG`（部件号 / 状态 / 图名），回答「AIN 到底给谁换过图」 |
| 其它 | `XSYS4_CG_AREA_DBG` | `src/parts/render.c:627` | [D292 诊断] 只对面积够大的部件（立绘 1920x1080 量级，w 或 h >= 400）打印纹理尺寸 / 部件 w,h / surface_area / 最终采样 r；用来区分「立绘只画出一部分」是采样区域退化（r.w 或 r.h 为 0）还是纹理本身不对 |
| 其它 | `XSYS4_CG_EXIST_DBG` | `src/hll/CGManager.c:49` | [D292 诊断] 打印 `CGManager.IsExist` 的每次询问与结果；值=1 打全部，值=子串只打含该 UTF-8 子串的名字（如 `=希露`）。用来回答「AIN 到底问过哪个 CG 名、引擎答了什么」——立绘回退成 `立／？？？` 时必须先确认 AIN 问过 `立／希露／基本` |
| 其它 | `XSYS4_CG_MISS_DBG` | `src/asset_manager.c:159` | [D292 诊断] 只打印 **未命中** 的 CG 名（`[cgmiss]`，按名字去重）与按名装载/解码失败（`[cgload] LOAD FAILED` / `DECODE FAILED`）。这是定位「立绘缺图」第二条路径的探针：渲染层已经看不到原名，只有这里留着 |
| 其它 | `XSYS4_CG_MISS_STACK` | `src/hll/CGManager.c:83` | [D292 诊断] 配合 CG_MISS_DBG，在每个未命中的 CG 名后面打一次 AIN 调用栈；用来抓「是谁在问这个不存在的名字」（占位图 `？？？` 的调用方） |
| 其它 | `XSYS4_CG_SPRITE_DBG` | `src/parts/render.c:680` | [诊断] 配合 CG_SPRITE_PATH：打印走精灵路径的大矩形（no/di/pr/filt/a） |
| 其它 | `XSYS4_CG_SPRITE_PATH` | `src/parts/render.c:676` | [诊断] 宽≥1900 且高 250..600 的 CG 改走精灵路径（`gfx_render_texture`）。实测与 parts 着色器路径**结果一致**（`s92` vs `s104` 逐像素几乎相同），说明底板一直在正常绘制，保留供以后遇到真·着色器问题时对照 |
| 其它 | `XSYS4_CONSTRUCT_776` | `src/hll/PartsEngine.c:593` | =0 把 AddPartsConstructionProcess(776) 退回空桩，用于卡片合成 A/B |
| 其它 | `XSYS4_CONSTRUCT_DBG` | `src/parts/construction.c:358` | 打印部件合成操作：PartsFunc(case 162)、776 批量入口、Build 的 parts/cmd/CG 名与 op 数 |
| 其它 | `XSYS4_COVER_HYST` | `src/parts/parts.c:2027` | [D343 默认生效，=0 关闭] cover_layer 单帧滞回：同一个值连续 3 帧才生效，抹掉「某层偶发全屏不透明件」造成的整层闪烁（实测变化次数 17→9，且不再在 4/7 之间逐帧跳） |
| 其它 | `XSYS4_CPROC_DBG` | `src/hll/PartsEngine.c:4342` | GetPartsConstructionProcess 合成读回挂账的触达轨迹（D497） |
| 其它 | `XSYS4_CP_DBG` | `src/vm.c:537` | 〔摘〕Parts(1114) 的 m_partsNumber 若在拷贝时还是 0，副本就永远是 0。 |
| 其它 | `XSYS4_DECOR_PASS` | `src/parts/parts.c:2422` | 〔摘〕点击于是又打空。 |
| 其它 | `XSYS4_DUMP_ABTN` | `src/vm.c:3217` | 〔摘〕往往能被容器兜底命中，于是看不到清单。 |
| 其它 | `XSYS4_DUMP_BTN` | `src/vm.c:3263` | 〔摘〕上限 40 次，避免长跑测刷屏。 |
| 其它 | `XSYS4_DUMP_CG` | `src/parts/parts.c:1222` | [诊断] 把刚上传的 CG 纹理另存成 /tmp/texdump_<no>_<w>x<h>.png（最多 8 张，全宽高 250..600）。判断「纹理内容有没有图」的唯一决定性证据 |
| 其它 | `XSYS4_DUMP_CG_SIZE` | `src/parts/parts.c:1224` | [诊断] 配合 DUMP_CG：只转存宽度等于该值的 CG（按尺寸触发，避开号段复用） |
| 其它 | `XSYS4_DUMP_EVERY` | `src/video.c:544` | 每隔 N 次执行 dump 一次，用于观察循环里的状态漂移 |
| 其它 | `XSYS4_DUMP_FRAME` | `src/video.c:540` | 指定帧的完整快照（配合 DUMP_AT 的扩展模式） |
| 其它 | `XSYS4_DUMP_GLYPH` | `src/parts/render.c:253` | [诊断] 把指定文本部件的字形纹理逐个转存成 /tmp/glyph_<no>_<i>.png（最多 6 个）。判断「字被裁」是位图层还是绘制层的唯一硬证据 |
| 其它 | `XSYS4_DUMP_TOPS` | `src/vm.c:4387` | 〔摘〕「条件是真是假」，而这类「条件永远为假」正是最难查的一类。 |
| 其它 | `XSYS4_EIP_GATE` | `src/parts/input.c:183` | SetEnableInputProcess 门控总闸；=0 回退到"输入组件不门控"（D496） |
| 其它 | `XSYS4_ENV_DBG` | `src/vm.c:4946` | 〔摘〕同一边（实测 `KeyValueMap@Find` 的 lambda 恒返回 -1）。 |
| 其它 | `XSYS4_EXIT_DBG` | `src/vm.c:7589` | 〔摘〕走完了。 |
| 其它 | `XSYS4_EX_DBG` | `src/hll/MainEXFile.c:527` | 〔摘〕所有任务都会被判成地图任务、剧情整段跳过——所以必须能看见。 |
| 其它 | `XSYS4_EX_DUMP` | `src/parts/activity.c:948` | 按部件名 dump 整棵 EX 子树（查「尺寸」「表示領域」写在哪个键上）；值=部件名 |
| 其它 | `XSYS4_EX_LAYOUT_BY_CONFIG` | `src/parts/activity.c:1055` | [诊断] 真布局盒（EX 声明 `レイアウトボックス`）按 `配置` 字段重排（0=FREE/1=纵向/2=横向），而不是 D214 的一律 FREE。用于解决 LargeSkillButton/CardApView 这类「子部件座标全在 (0,0)」的容器底板不显示 |
| 其它 | `XSYS4_EX_LAYOUT_FREE` | `src/parts/activity.c:1041` | [D214 已转正] EX 装载的布局盒/用户组件初值改为 FREE（保留 EX 绝对坐标），避免 VERTICAL 自动重排把子部件推到屏幕外；默认生效，=0 回退做 A/B |
| 其它 | `XSYS4_EX_RATIO_HEURISTIC` | `src/parts/activity.c:1204` | 〔摘〕而比例坐标在对齐模式下必然是这种形态。`XSYS4_EX_RATIO_HEURISTIC=0` 回退。 |
| 其它 | `XSYS4_EX_RATIO_POS` | `src/parts/activity.c:1185` | [D216 已转正] EX「座標を率として認識する=1」的 座標 按视图宽高换算成像素；消息窗口 (0,1) 由此变成 (0,1080)，正文才从屏幕外进画面；默认生效，=0 回退做 A/B |
| 其它 | `XSYS4_EX_RECT_DBG` | `src/parts/activity.c:1170` | [诊断] 打印每个 `矩形パーツ` 的四角换算结果与是否被保留（已有尺寸时 REPORT 而非覆盖）；用来确认 Ain 运行时 SetRectangleDetectionSize 的优先级没被 EX 静态值盖掉 |
| 其它 | `XSYS4_EX_RECT_DET_SIZE` | `src/parts/activity.c:459` | [D375 默认生效，=0 关闭] `矩形検出`(ACT_COMP_RECT_DETECTION) 也按 EX 的四角负载定尺寸（原来只对 `矩形パーツ` 读）。实测技能卡的 Normal/Alter 没有四角字段，此开关对它无效，但对其它活动可能有用 |
| 其它 | `XSYS4_EX_RECT_FILL_PARENT` | `src/parts/activity.c:471` | [D387 默认开] `矩形検出` 既无 EX 四角负载、尺寸又是 1×1 占位时，按父件尺寸铺满（技能卡命中盒 1×1 → 206×147）；`=0` 回退做 A/B |
| 其它 | `XSYS4_EX_RECT_MIN1X1` | `src/parts/activity.c:1159` | [D366 默认生效，=0 关闭] 把 `1x1` 的矩形检测件当成"没设过尺寸"，允许套用 EX 的四角矩形负载。兰斯10 的技能卡 `Normal/Alter/Stack` 就是 1x1 占位，原判据只看 `<=0` 于是永远拿不到真实命中盒 |
| 其它 | `XSYS4_EX_RECT_SIZE` | `src/parts/activity.c:1138` | [D271 已转正] EX 装载时为 `矩形パーツ`(组件类型 23) 按四角坐标 左上/右上/左下/右下 算出宽高并 PE_SetPartsSize；此前一个字段都没解析，实测 64 个 PARTS_RECT_DETECTION 部件里 60 个是 0x0（ApBase 因此既画不出也点不到）；默认生效，=0 回退做 A/B |
| 其它 | `XSYS4_EX_TEXT_DBG` | `src/parts/activity.c:762` | 名字子串：打印匹配部件的 EX 文本字段（文本(ti)/文本(src)）。查「文字为什么没显示」时先看 EX 里到底有没有 |
| 其它 | `XSYS4_FLATEND_DBG` | `src/parts/flat.c:1324` | 〔摘〕回合切换的等待完全押在这个返回值上，前 40 次足够看清对象。 |
| 其它 | `XSYS4_FLATEND_MIN_END` | `src/parts/flat.c:1334` | [诊断] 只打印 end_frame >= N 的长演出 flat 的未播完查询（推荐用这个，不受号段复用影响） |
| 其它 | `XSYS4_FLATEND_ONLY` | `src/parts/flat.c:1331` | [诊断] 只跟一个 flat 部件号的 IsPartsFlatEnd 查询。**注意号段会复用**，跨跑测用同一号不可靠，长演出改用 FLATEND_MIN_END |
| 其它 | `XSYS4_FLATEND_STOP_IS_END` | `src/parts/flat.c:1313` | [诊断] 把「flat 停在 stop 键/被 StopPartsFlat 停住」也算作「这段演完了」。用于验证「AIN 等 IsPartsFlatEnd 而 flat 卡在 73/75」的死锁 |
| 其它 | `XSYS4_FLAT_DBG` | `src/parts/flat.c:1235` | 打印 flat 的加载名/hdr 尺寸/库数，以及渲染时的 flat 指针与部件几何（空 flat 会让部件退化成 0x0） |
| 其它 | `XSYS4_FLAT_DELTA_DBG` | `src/parts/flat.c:733` | 〔摘〕pt << 1000 说明被限流（指向①）。 |
| 其它 | `XSYS4_FLAT_FRAME_DBG` | `src/parts/parts.c:1536` | [诊断] 轻量 flat 帧探针：打印 no/ctrl/frame/root_state.stopped/fps/passed_time。值=1 打全部，值=部件号只跟一个。用来区分「帧没推进」是 parts_flat.stopped、root_state.stopped（脚本 stop 键）还是 passed_time 为 0 |
| 其它 | `XSYS4_FLAT_TICK_DBG` | `src/parts/flat.c:220` | [诊断] 逐 tick 打印 flat 推进前后（prev/next/cf/cap/inc/stop/sup/jump），值 = 只跟 end_frame >= N 的长演出（默认 70）。用来分辨「帧不涨」是 increment=0（stopped）还是被封顶 |
| 其它 | `XSYS4_FLAT_TREE` | `src/parts/flat.c:668` | 部件号：dump 该 flat 的 timeline/状态树（帧、图层、库名）。flat 动画对不齐时先看它的声明形态 |
| 其它 | `XSYS4_FN_COVER` | `src/vm.c:720` | 文件名：把 AIN 函数调用覆盖情况写成文件（每个 fno 是否被调到）。回答「哪些函数这辈子没跑过」 |
| 其它 | `XSYS4_FN_LIST` | `src/vm.c:6969` | [诊断] 按名字子串列出 AIN 函数（号 / 地址 / 名字）；`#29096` 按号查名；`*` 导出全表（供 tools/offline/callsite.py 把地址翻成函数名）（反汇编里 CALLMETHOD 的被调号在栈上，只能先认出 `PUSH <号>` 再回查）。`CallDelegate`、`TextButton@Motion` 这类只存在于内存表的函数号，靠它才能上 CALL_TRACE / DUMP_AT |
| 其它 | `XSYS4_FN_WATCH` | `src/vm.c:816` | `=22908,22909` 按**解析后的函数号**打方法调用（`CALLMETHOD` 在运行期才解析出 fno，静态 grep `arg=<fno>` 数不出来）。判「某方法到底跑没跑」只能靠它（D323） |
| 其它 | `XSYS4_FN_WATCH_MAX` | `src/vm.c:840` | 〔摘〕汇总。上限可用 `XSYS4_FN_WATCH_MAX` 调。 |
| 其它 | `XSYS4_FN_WATCH_NAME` | `src/vm.c:804` | `=CADVEngine` 按**函数名子串**过滤方法调用，每个 fno 只打第一次。用来一次拿到「这个类到底有哪些方法真的在跑」（D334） |
| 其它 | `XSYS4_FORCE_DI0` | `src/parts/message.c:74` | 强制消息的 delegate_index 回退到 0（前 12 次）。诊断委托下标失效时的兜底行为，A/B 用 |
| 其它 | `XSYS4_FORCE_HIDE` | `src/parts/parts.c:4282` | `=900000400,900000408`：每帧强制隐藏这些部件。**判定性实验用**，不是修复手段。用来把「背景不切换」缩成「隐藏这几个号画面是否就对」（D314） |
| 其它 | `XSYS4_FORCE_MOVE` | `src/parts/parts.c:4309` | `=900000400:3`：每帧把部件强制改到指定层（号:层号，逗号分隔）。**判定性实验用**，验证「部件创建时落错了层」（D315） |
| 其它 | `XSYS4_FPS_DBG` | `src/parts/render.c:1455` | [诊断] 每秒打印一次实际渲染帧数。用来区分「演出推进慢」是逻辑卡死还是 llvmpipe 低帧率（D92 全量标脏） |
| 其它 | `XSYS4_FSL_DBG` | `src/parts/message.c:69` | 〔摘〕（全屏 m0 的整屏委托 = 菜单/按钮回调的总入口）。只在前 4 次打。 |
| 其它 | `XSYS4_FULLSCREEN` | `src/video.c:293` | [D377 默认关] `=1` 启动即真全屏（无边框/无标题栏），`=0` 强制窗口化（覆盖 .xsys4rc 的 fullscreen）。运行期仍可用 F11 切换 |
| 其它 | `XSYS4_G14_WATCH` | `src/vm.c:4860` | [诊断] 只在 global[14] 的值变化时打印 ip/旧值/新值/栈/调用栈（抓单例被覆写） |
| 其它 | `XSYS4_G15_DBG` | `src/vm.c:264` | 〔摘〕实测它恒 0 ⇒ 菜单的按钮检测永不执行。 |
| 其它 | `XSYS4_GDELAY_DBG` | `src/vm.c:276` | 〔摘〕的标签全在这个队列里出不来。XSYS4_GDELAY_DBG=1 追写入。 |
| 其它 | `XSYS4_GL_DBG` | `src/video.c:269` | [诊断] 启动时打印实际 GL vendor/renderer/version —— 判断有没有用上硬件加速的唯一硬证据（llvmpipe=软件，Mesa Intel=硬件） |
| 其它 | `XSYS4_GSAVE_DBG` | `src/savedata.c:829` | 〔摘〕所以按可打印的类型名分类。 |
| 其它 | `XSYS4_GSAVE_TIME` | `src/savedata.c:579` | gsave 装载四级节拍探针（enter/数组/记录/全局计时，D498 大存档挂起定位） |
| 其它 | `XSYS4_HANG_DBG` | `src/vm.c:7051` | 〔摘〕也会触发，别设成 3 次。 |
| 其它 | `XSYS4_HEAP_LIVE_LIMIT` | `src/heap.c:107` | 堆存活槽上限，超了报警 |
| 其它 | `XSYS4_HLL_ARGDBG` | `src/ffi.c:985` | 打印 HLL 调用的实际参数值（排查"参数顺序/位宽错"时用） |
| 其它 | `XSYS4_HLL_HIST` | `src/vm.c:146` | HLL 库号：按实际调用统计该库各函数的热力（谁被调了多少次），配合 xsys4 hot 读 |
| 其它 | `XSYS4_HLL_HOT` | `src/ffi.c:113` | [诊断] 全库 HLL 调用热力表：进程退出时按次数倒序打印 `[hllhot] 库 函数 次数`。用来决定「544 个未实现里先补哪个」—— 按实际调用排，不按缺口字母顺序。GUIEngine 的 336 个缺口运行期零调用（菜单系统没进过），补它是纯浪费 |
| 其它 | `XSYS4_HLL_REFDBG` | `src/ffi.c:932` | [诊断] 引用型 HLL 返回：打印反解 (page_slot,index) 是否成功；默认只打失败项 |
| 其它 | `XSYS4_HLL_REFDBG_FILTER` | `src/ffi.c:935` | [诊断] 配合 REFDBG，额外打印函数名含该子串的调用（如 At） |
| 其它 | `XSYS4_ICAST_DBG` | `src/vm.c:5039` | 〔摘〕"对象类型确实不对" 和 "我们的转型判定太严"。 |
| 其它 | `XSYS4_IFACE_ARR_FIX` | `src/page.c:563` | 〔摘〕想继续查就把 XSYS4_IFACE_ARR_FIX=1 打开。 |
| 其它 | `XSYS4_IFACE_COPY_DBG` | `src/page.c:421` | dump 前 4 次 iface 页拷贝的逐槽值。接口值占两槽（对象+虚表偏移），拷贝丢槽时看这里 |
| 其它 | `XSYS4_IFACE_COPY_FIX` | `src/page.c:397` | 〔摘〕XSYS4_IFACE_COPY_FIX=0 可回退到旧行为，用于 A/B。 |
| 其它 | `XSYS4_IFACE_PUSH_DBG` | `src/hll/Array.c:1165` | 〔摘〕若这里从不出现，说明该数组不是 Array.PushBack 建的。 |
| 其它 | `XSYS4_IFACE_REALLOC_FIX` | `src/page.c:635` | 〔摘〕XSYS4_IFACE_REALLOC_FIX=1 可打开对称布局做实验。 |
| 其它 | `XSYS4_IP_WATCH` | `src/vm.c:4799` | 监视指令指针，抓"跑到不该跑的地方" |
| 其它 | `XSYS4_KEYCODE_PROBE` | `src/parts/input.c:114` | 打印收到的原始键码 |
| 其它 | `XSYS4_KEY_DBG` | `src/hll/SACT2.c:852` | 〔摘〕key_state 就永远是 false，游戏会一直等下去。 |
| 其它 | `XSYS4_LAYER_DBG` | `src/vm.c:3155` | 〔摘〕而不是「派发成功那一刻」（菜单打开那次点击的采样点会落在建层之前）。 |
| 其它 | `XSYS4_LAYER_IDX` | `src/parts/parts.c:4485` | `=1` 层身份改用**栈下标**（上游语义）；默认用稳定 id（D129）。两种口径的 AddController 返回值 / SetActive / Remove / GetActive 一起切（D316） |
| 其它 | `XSYS4_LAYER_TRACE` | `src/parts/parts.c:3280` | 图层顺序追踪 |
| 其它 | `XSYS4_LAYOUT_DBG` | `src/parts/layoutbox.c:289` | [诊断] parts_do_layout 逐盒打印判据（state/layout_type/子件数/尺寸/位置，前 400 条）。查「容器子件全叠 (0,0)」与排版覆盖 EX 座标问题 |
| 其它 | `XSYS4_LVAR_DBG` | `src/vm.c:188` | fno:var 形式：监视某个函数某个局部变量每次取值。回答「这个变量到底是不是 -1」 |
| 其它 | `XSYS4_MC_SLOT_DBG` | `src/vm.c:1479` | 〔摘〕下面两次 stack_pop() 真正取到的东西就整体错位了。 |
| 其它 | `XSYS4_MC_WATCH` | `src/vm.c:1282` | 〔摘〕时（callee 声明 N 槽、call site 只压了 1 槽）只能靠猜。 |
| 其它 | `XSYS4_MGRBT` | `src/vm.c:4643` | 定点探针：在 ip 0x53f0-0x5424 区间打印句柄/this/成员值（D 系列调查残留，按需复用） |
| 其它 | `XSYS4_MOTION_DBG` | `src/hll/PartsEngine.c:4130` | 〔摘〕探针（`XSYS4_MOTION_DBG=1`）确认它到底被调多勤、参数长什么样。 |
| 其它 | `XSYS4_MOUSE_DBG` | `src/input.c:436` | [诊断] 打印 SDL 按钮事件（btn / state / clicks / pos / windowID）。用来二分「X 层没把 ButtonPress 交给 SDL」和「SDL 收到了但引擎丢边沿」：同一配方下无 `key:Shift_L` 激活时 `[mouse]` 一条都没有（`s27`），加激活后立即出现并点中 `900000716`（`s26`） |
| 其它 | `XSYS4_MOVIE_DBG` | `src/hll/PartsEngine.c:4147` | PartsMovie 族（部件内嵌视频）的创建/轮询轨迹（D497） |
| 其它 | `XSYS4_MSGF_FIX` | `src/hacks.c:328` | [D204] AIN v12 无 MSGF 段时按函数名把 msgf 解析到 "message"/3（Rance10: fno 6100）；默认开启，=0 关闭做 A/B |
| 其它 | `XSYS4_MSGQ_DBG` | `src/parts/message.c:199` | 〔摘〕对其它值走 member1[delegate_index]。分派打空炮时必须先知道走的哪条。 |
| 其它 | `XSYS4_MSGWIN_DBG` | `src/hll/PartsEngine.c:50` | [诊断] 打印消息窗口配置族 setter 的触达与实参（Active/Text/CGName/FlatName/TextArea/OriginPosMode/Space/Speed），每个函数限 20 条；用来判定「空桩是不是真被调用」 |
| 其它 | `XSYS4_MSGWIN_FRAME_DBG` | `src/parts/render.c:1292` | [诊断] 打印消息窗口底板 CG（1920x300~500）是否进入 parts_render 及其 origin_offset / 准入字段；用来区分「没到渲染」与「画了看不见」 |
| 其它 | `XSYS4_MSGWIN_FRAME_Z` | `src/parts/activity.c:1373` | [诊断] 消息窗口底板 frame 部件的 z 基线（默认 4900）。用来 A/B「底板到底画没画」：设 1 会让它排到地图后面，底部像素会明显变化 |
| 其它 | `XSYS4_MSGWIN_FULL` | `src/parts/activity.c:1346` | [D217 已转正] EX 消息窗口组件补齐 39px 字体/字色/字距/行距，并用独立 CG 部件装载 种类別情报 的 底板ＣＧ名；默认生效，=0 回退做 A/B |
| 其它 | `XSYS4_MSGWIN_TEXT_BASELINE` | `src/parts/activity.c:1403` | [诊断] 消息窗口正文 y 补偿一个字号（默认开）。`文本エリア.y` 是「首行顶边距窗口顶」，而文本件的 `origin_offset.y` 已是 `-字形高`，不补偿会高出整一个字，正文上半被名牌盖住（`s126` 的「半个字」）；`=0` 回退 |
| 其它 | `XSYS4_MSG_DBG` | `src/parts/message.c:45` | 〔摘〕只打非悬停消息，否则前 60 条全被 MOUSE_ON 占满，看不到点击。 |
| 其它 | `XSYS4_MSG_LINES` | `src/hll/PartsEngine.c:4016` | [诊断] 每开始一句新台词打一行 `[msgline] N`。剧情推进的唯一可量化指标，也是「卡住了没」的判据（D319） |
| 其它 | `XSYS4_MSG_NO_SENTINEL` | `src/parts/message.c:195` | 〔摘〕这就是那个把 SceneStack@WaitFrame 卡死的忙循环。 |
| 其它 | `XSYS4_MSG_POPDBG` | `src/parts/message.c:158` | 〔摘〕pop」时必须看到它）。配 XSYS4_MSG_POPDBG_ON=1 才打印 MOUSE_ON。 |
| 其它 | `XSYS4_MSG_POPDBG_ON` | `src/parts/message.c:159` | 〔摘〕pop」时必须看到它）。配 XSYS4_MSG_POPDBG_ON=1 才打印 MOUSE_ON。 |
| 其它 | `XSYS4_MSG_TRACE` | `src/parts/message.c:174` | 消息类型：跟踪消息派发的中间步骤（AIN 查表/过滤/放弃的位置），比只看 [mpop] 结果更有信息量 |
| 其它 | `XSYS4_NODE_DIGIT` | `src/vm.c:4267` | [D209 诊断后门] 把 QuestMapParamAccessor 的 KeyBase 末尾全角编号末位改成 0-9（００１→００２），仅在 23486 事件链生效；默认关，用于验证战斗链，不是正常游戏路径 |
| 其它 | `XSYS4_NO_AUDIO` | `src/audio_mixer.c:727` | 关闭音频输出 |
| 其它 | `XSYS4_NO_AUTO_MOTION` | `src/parts/motion.c:102` | =1 关闭「首次加 motion 自动开表」（D186），用于 motion 自动启动的 A/B |
| 其它 | `XSYS4_NO_CP_ALPHA_FIX` | `src/parts/parts.c:3368` | =1 关闭「合成类部件 alpha=0 提升为 255」（卡片立绘 A/B 回退开关）。[D224 收窄] 提升现在只作用于 >=8x8 的真实合成纹理，2x2 全屏淡出遮罩不再被误抬 |
| 其它 | `XSYS4_NO_DI_FALLBACK` | `src/parts/message.c:116` | [回退闸] 关闭 D260 的委托下标回退（消息 delegate_index<0 时不再回退到 0）。用于 A/B：开着它战斗会退回「点击打空炮、停 Round 1/10」的旧行为 |
| 其它 | `XSYS4_NO_PUMP` | `src/vm.c:6927` | 事件泵空转诊断 |
| 其它 | `XSYS4_NULLMETHOD_FIX` | `src/vm.c:1501` | 〔摘〕XSYS4_NULLMETHOD_FIX=0 可恢复旧行为，用于 A/B。 |
| 其它 | `XSYS4_NULLREF_DBG` | `src/vm.c:347` | 〔摘〕从栈上弹出一个引用，返回被引用对象的地址。 |
| 其它 | `XSYS4_NULL_METHOD` | `src/vm.c:1336` | 〔摘〕XSYS4_NULL_METHOD=0 可恢复旧的直接中止行为，便于 A/B。 |
| 其它 | `XSYS4_NULL_RET` | `src/vm.c:1376` | 〔摘〕XSYS4_NULL_RET=0 可回退，用于定位"到底跳过了哪些调用"。 |
| 其它 | `XSYS4_OBJ_WATCH` | `src/vm.c:4819` | 监视指定堆槽的读写 |
| 其它 | `XSYS4_ORIGIN_MODE0_TL` | `src/parts/parts.c:824` | `=1` 把 origin mode 0 与未映射模式按**左上角 `{0,0}`** 处理（上游 77f7f02 语义）。默认保持旧的 `{mode, 3h/4}`（D328 的 A/B） |
| 其它 | `XSYS4_PARENT_REAL` | `src/hll/PartsEngine.c:656` | `=0` 把 `Parts_GetParentPartsNumber` 退回恒 0 桩（D80/D81 行为）。默认返回真实父号 |
| 其它 | `XSYS4_PART_LIFE` | `src/parts/parts.c:241` | [诊断] 打印 900000560..900000600（回想对话框段）部件的 create/release，用于判「绑定是不是随部件重建丢掉」（D320） |
| 其它 | `XSYS4_PART_TYPE_DBG` | `src/parts/activity.c:725` | 未识别的部件类型：打印 种类別情报 的全部子键名（定位「真实类型藏在哪个子节点」） |
| 其它 | `XSYS4_PE_DBG` | `src/hll/PartsEngine.c:40` | PartsEngine 调用诊断 |
| 其它 | `XSYS4_PHANTOM_REUSE` | `src/vm.c:1590` | 幽灵槽复用诊断 |
| 其它 | `XSYS4_PRED_BOUND` | `src/hll/Array.c:694` | =0 关闭 Array Predicate 的越界保护（回退用）；默认开启，防止 Predicate 越过数组末尾 |
| 其它 | `XSYS4_PRED_DBG` | `src/hll/Array.c:665` | 〔摘〕以及它是否随元素变化（恒定 = 约定不对，搜索永远走同一边）。 |
| 其它 | `XSYS4_PUMP_MASK` | `src/vm.c:6939` | 帧泵的指令间隔掩码（默认 0x3F=每 64 条指令检查一次时钟）。调小更细但更慢，A/B 帧率用 |
| 其它 | `XSYS4_RA_DBG` | `src/vm.c:5115` | 定点探针：在 ip 0x408f00-0x408f40 打印 R_ASSIGN 的源/目标页现场（D 系列调查残留） |
| 其它 | `XSYS4_RECYCLE_SLOTS` | `src/heap.c:199` | 复用已释放的堆槽（默认关） |
| 其它 | `XSYS4_REFDBG` | `src/vm.c:408` | 〔摘〕它之后的代码全部当作不可达删掉。 |
| 其它 | `XSYS4_REFREF_DBG` | `src/vm.c:4706` | 〔摘〕所以代码里不写死任何地址。 |
| 其它 | `XSYS4_REF_AT` | `src/vm.c:4622` | 〔摘〕恰恰是最常见的失败形态。 |
| 其它 | `XSYS4_RELEASE_RECURSIVE` | `src/parts/parts.c:3173` | [D219 已转正] PE_ReleaseParts 递归释放整棵部件子树（原 parts_release 只把子部件摘链不释放，对话框关掉后文字层残留，H025/[LAYER-LEAK]）；默认生效，=0 回退做 A/B |
| 其它 | `XSYS4_RENDER_SKIP_DBG` | `src/parts/render.c:1316` | 按部件号过滤的渲染准入探针：打印可见性/消息窗口/联动/show/z/类型/尺寸/alpha/文字行数/颜色/字号。判断「这个件没进渲染 vs 进了被挡」 |
| 其它 | `XSYS4_RSS_LIMIT` | `src/vm.c:4348` | 进程 RSS 上限，超了报警 |
| 其它 | `XSYS4_SAVE_DBG` | `src/savedata.c:279` | 〔摘〕XSYS4_SAVE_DBG=1 时打 AIN 调用栈（每次写档一条，量很小）。 |
| 其它 | `XSYS4_SAVE_GROUP_ANY` | `src/savedata.c:796` | 〔摘〕XSYS4_SAVE_GROUP_ANY 保留为无害兼容开关。 |
| 其它 | `XSYS4_SHOT_DIR` | `src/screenshot.c:178` | [诊断] 指定目录后按 Alt+S 让引擎用 glReadPixels 直接 dump GL main surface 为 shot_NNN.png，绕过 zenity 文件框与 X11 合成；GL 直绘下 x11grab/import 只能拿到全黑 framebuffer，画面判据以此为准（GUIDE 1.13） |
| 其它 | `XSYS4_SHOT_PARTS_DUMP` | `src/screenshot.c:184` | [诊断] 配合 XSYS4_SHOT_DIR：每次引擎自截图时打一次部件快照（layer_show / cover / 每部件 vis）+ 同帧 CG 绘制日志（最近 12 次调用的 alpha / multiply_color / scale / 纹理尺寸、以及两次截图之间的绘制次数）；按截图时刻触发而非全程探针，避免拖慢帧率改变剧情时序；需配 XSYS4_PARTS_DUMP_MIN=900000300 / MAX=1000 |
| 其它 | `XSYS4_SHOW_DBG` | `src/parts/parts.c:3294` | 〔摘〕于是「它有没有被 AIN 调过 SetShow」这个问题一直没法回答。 |
| 其它 | `XSYS4_SIZE_1X1_TRACE` | `src/parts/parts.c:4033` | [D375 诊断，默认关] `SetPartsSize` 收到 w<=1 且 h<=1 时打调用栈 —— 用来回答「这个 1x1 到底是 AIN 算的还是活动数据写死的」 |
| 其它 | `XSYS4_SLOT_ID` | `src/heap.c:250` | 打印稳定槽 ID，便于跨日志对照同一对象 |
| 其它 | `XSYS4_SPLIT_DBG` | `src/hll/String.c:655` | [D292 诊断] 打印 `String.Split` 前 60 次调用的入参（原串 / 分隔符 / contain 标志）与切出的每一段；判据是「切开再拼回是否恒等」——立绘名少一个 `／` 就是这里 contain=1 没生效 |
| 其它 | `XSYS4_SP_BALANCE` | `src/vm.c:3893` | 每次函数返回校验栈平衡（失败即报） |
| 其它 | `XSYS4_SP_TRACE` | `src/vm.c:4588` | 环形缓冲记录栈指针变化，抓"玄学栈漂移" |
| 其它 | `XSYS4_SP_WATCH` | `src/vm.c:7322` | 监视指定帧的栈指针 |
| 其它 | `XSYS4_SRDBG` | `src/vm.c:5986` | 〔摘〕绝不会是一张活着的 ARRAY_PAGE。 |
| 其它 | `XSYS4_STACK_SAMPLE` | `src/vm.c:7371` | 周期性采样调用栈（抓谁在长时间占栈） |
| 其它 | `XSYS4_STRICT_REF` | `src/vm.c:432` | 严格引用计数检查（会把"漏 ref/unref"变成显式报错） |
| 其它 | `XSYS4_STRUCT_LIST` | `src/vm.c:7011` | [诊断] 按名字子串列出结构体及成员名。查「m7 是委托表还是哈希表」时，成员名比任何猜测都直接 |
| 其它 | `XSYS4_STR_INT_DBG` | `src/hll/String.c:59` | 〔摘〕"取对了但字符串已经死了"。 |
| 其它 | `XSYS4_STUB_BT` | `src/ffi.c:224` | 软桩命中时附带回溯 |
| 其它 | `XSYS4_STUB_LOG` | `src/ffi.c:166` | 软桩命中清单写到该文件 |
| 其它 | `XSYS4_SUBST_BT` | `src/ffi.c:713` | 静态库替换（subst）时打印回溯 |
| 其它 | `XSYS4_SWAP_LOG` | `src/video.c:530` | 〔摘〕这样启动器可以在第一帧呈现之后立刻冻结进程。 |
| 其它 | `XSYS4_SWITCH_DBG` | `src/vm.c:5327` | [诊断] 按开关号打印 SWITCH 的实参与落点（`[switch] no=.. val=.. -> 0x..`）。查「某个 key 为什么没派发到预期方法」用它：`SWITCH 496` 按 `m_key` 分派到 `Parts@SetX/SetAlpha/…`，只看 case 表不知道实际弹进来的值是多少 |
| 其它 | `XSYS4_TALT_EXTRA_FORMATS` | `src/parts/flat.c:400` | [D275 已转正] D200 的「LIBL=0 时追加 TALT 图集」只认 QNT 魔数，而 `战斗画面/シス／战斗／技能按钮／一般` 的 TALT 是 **AJP**，于是零纹理库、底板一个像素不画。默认改成交给 `cg_check_format()` 判定；=qnt 回退成旧行为做 A/B |
| 其它 | `XSYS4_TEXT_CHAR_DBG` | `src/parts/render.c:196` | [诊断] 逐个文本部件打印行数/行高/逐字符 advance 与字形尺寸，用来区分「advance 太小」与「两段文本叠在一起」 |
| 其它 | `XSYS4_TEXT_DIM_DBG` | `src/parts/text.c:119` | dump 文字度量结果为 0 的部件（字号/面/行数/首字符数）。判断「设置项没文字」是度量失败还是绘制/遮挡 |
| 其它 | `XSYS4_TEXT_DRAW_DBG` | `src/parts/render.c:279` | 〔摘〕这里直接量 pr，确认差异出在 x/y 计算还是绘制层。 |
| 其它 | `XSYS4_TEXT_RENDER_DBG` | `src/parts/render.c:156` | =1 列全部非 UC 文字层；=<部件号下限> 只盯该号段（如 900000300），每号最多 3 条并附 show/alpha/vis/mw/mwshow 准入字段；修掉了「全局 80 条被地图吃光」的假阴性 |
| 其它 | `XSYS4_TEXT_SET_DBG` | `src/parts/text.c:220` | 〔摘〕看每笔 SetText 写给了哪个件、内容、以及该件当前的可见性/位置。 |
| 其它 | `XSYS4_TITLE_UI_FIX` | `src/parts/render.c:125` | 〔摘〕当前帧存在可见 UC 部件就跳过它。XSYS4_TITLE_UI_FIX=0 回退。 |
| 其它 | `XSYS4_TRACE` | `src/vm.c:4327` | 〔摘〕XSYS4_TRACE=起始:结束（十六进制）：运行时指令跟踪，带栈顶值 |
| 其它 | `XSYS4_TRACK_PARTS` | `src/parts/parts.c:4240` | `=900000400,900000408` 只在 (层 / local.show / global.show / z / 父) 变化时打印，给出部件的完整归属变迁史（D316） |
| 其它 | `XSYS4_UC_RENDER_DBG` | `src/text.c:274` | 用户组件（TextButton 等）渲染诊断 |
| 其它 | `XSYS4_USERCOMP_DBG` | `src/parts/activity.c:937` | dump 前 3 个 USER_COMPONENT 部件的解析字段（组件类型/参数），排查自定义组件装载 |
| 其它 | `XSYS4_VIS_IDX` | `src/parts/parts.c:1888` | 〔摘〕`AddLayer` 之后 `id[i] != i` 是常态，不修就永远错位。 |
| 其它 | `XSYS4_WATCH_IFACE` | `src/vm.c:367` | 〔摘〕page_set_var，所以只盯 page_set_var 会漏掉真正的写入者。 |
| 其它 | `XSYS4_WIDGET_SELFTEST` | `src/vm.c:2689` | [D396 诊断，默认关] `=1` 启动 3 秒后跑一次控件状态自检（set→get 往返断言），打 `[wtest] RESULT: PASS/FAIL` —— 填桩的验收用，证明的是语义往返而不是「不再报未实现」 |
| 其它 | `XSYS4_WIN_DBG` | `src/input.c:784` | 〔摘〕还是「被隐藏/移动」，只能靠 SDL 事件本身回答。 |
| 其它 | `XSYS4_XSET_DBG` | `src/vm.c:5010` | dump X 赋值拷贝前 20 次的现场（源/目标页），排查赋值语义 |
