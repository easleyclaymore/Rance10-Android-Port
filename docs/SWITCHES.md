# 诊断开关总表

> 由 `tools/gen-switches.py` 生成，**不要手改**。改代码后重跑一次。

所有开关默认都是**关闭**的；只有显式设置才生效。
新增开关必须带一行用途说明，否则本表会显示 `—`，
它同时是一个可量化的债：`tools/gen-switches.py --check` 的返回值就是未标注数量。

共 **181** 个开关，其中 **30** 个没有就近注释。

**用途一栏的两种来源**：

- 正常文字 = `tools/switch-desc.txt` 里人工写的一句话（可信）
- 带 `〔摘〕` = 从源码就近注释摘的片段，**可能是背景说明而不是用途**，用前请核对源码

| 分组 | 开关 | 位置 | 用途 |
|---|---|---|---|
| 断点与快照 | `XSYS4_CALL_TRACE` | `src/vm.c:785` | 逗号分隔的 AIN 函数号；命中时打印调用点与返回地址（"有没有被调到"的第一手证据） |
| 断点与快照 | `XSYS4_CALL_TRACE_TOTAL` | `src/vm.c:5376` | [诊断] 退出时汇总 `XSYS4_CALL_TRACE` 各函数的**真实调用次数**（不受"每 fno 8 站点 + 每 200 次一条"的打印限流影响）。**凡是要断言"某函数跑了 N 次"，必须用这个** —— D280/D281 两次把日志条数当次数，把 `View_Update` 的 1504 次误读成 10 次、把 `OnUpdate` 的 150 次误读成 0 次 |
| 断点与快照 | `XSYS4_CENSUS` | `src/vm.c:1355` | 按函数做调用普查（与 FN_DUMP 互补） |
| 断点与快照 | `XSYS4_DUMP_AT` | `src/vm.c:2403` | 在 `<十六进制地址>[,最多几次]` 处停下，打印帧名、this 各成员、局部变量和操作数栈 |
| 断点与快照 | `XSYS4_FN_DUMP` | `src/vm.c:887` | 函数调用次数统计与慢函数排行 |
| 断点与快照 | `XSYS4_FN_DUMP_ALL` | `src/vm.c:903` | 统计时不做前 N 次闸门，低频函数也全打 |
| 断点与快照 | `XSYS4_FN_DUMP_SKIP` | `src/vm.c:895` | 统计时跳过指定函数号 |
| 断点与快照 | `XSYS4_HLL_TRACE` | `src/ffi.c:520` | 打印每次 HLL 调用（`lib= fno= nr_args= sp= ret=`），不设 FILTER 会很吵 |
| 断点与快照 | `XSYS4_HLL_TRACE_FILTER` | `src/ffi.c:527` | 收窄 HLL_TRACE：`PartsEngine` 按库名、`27:73` 按库:序号、其它按函数名子串，逗号分隔 |
| 容器与内存 | `XSYS4_ALLOC0_DBG` | `src/page.c:445` | 0 号堆槽分配的诊断 |
| 容器与内存 | `XSYS4_ARRAY_DBG` | `src/hll/Array.c:1213` | Array 诊断的旧开关（与 ARR_DBG 同类，逐步统一） |
| 容器与内存 | `XSYS4_ARRAY_FREE_FIX` | `src/hll/Array.c:527` | 数组释放路径的实验修复（默认关，会改行为） |
| 容器与内存 | `XSYS4_ARRAY_REF_FIX` | `src/hll/Array.c:892` | 数组元素引用处理的实验修复（默认关，会改行为） |
| 容器与内存 | `XSYS4_ARR_ALLOC_DBG` | `src/ffi.c:722` | 只打数组分配 |
| 容器与内存 | `XSYS4_ARR_DBG` | `src/ffi.c:634` | Array 操作诊断：alloc/find/any/emp/free/subst 逐条打 |
| 容器与内存 | `XSYS4_ARR_MULTIDIM` | `src/hll/Array.c:351` | Array.Alloc 构造多维内层数组；默认开启，=0 回退旧行为做 A/B |
| 容器与内存 | `XSYS4_ARR_TYPE_DBG` | `src/hll/Array.c:1087` | 数组元素类型推导诊断（泛型数组"由首个元素定型"出问题时用） |
| 容器与内存 | `XSYS4_ARR_UNSET_FIX` | `src/hll/Array.c:454` | 未初始化槽读取的实验修复（默认关，会改行为） |
| 容器与内存 | `XSYS4_BADSTRUCT_DBG` | `src/vm.c:1286` | 结构体成员访问越界诊断 |
| 容器与内存 | `XSYS4_BADTHIS` | `src/vm.c:1255` | 非法 this（类型不匹配/越界）时打回溯 |
| 容器与内存 | `XSYS4_CTOR_DBG` | `src/page.c:461` | 构造函数调用诊断 |
| 容器与内存 | `XSYS4_DF_DBG` | `src/heap.c:216` | 委托释放路径诊断 |
| 容器与内存 | `XSYS4_DF_FREE_DBG` | `src/heap.c:325` | 委托释放的逐次释放诊断 |
| 容器与内存 | `XSYS4_DF_MAX` | `src/heap.c:219` | 委托释放的最大迭代次数（防死循环） |
| 容器与内存 | `XSYS4_DGPLUSA_DBG` | `src/vm.c:4638` | — |
| 容器与内存 | `XSYS4_DGPLUSA_MAX` | `src/vm.c:4642` | 〔摘〕前 400 条会被 startup 的 CParts@Attach 占满。 |
| 容器与内存 | `XSYS4_DG_ASSIGN_DBG` | `src/vm.c:4614` | 委托赋值诊断 |
| 容器与内存 | `XSYS4_DG_CALL_DBG` | `src/vm.c:1595` | 委托调用诊断 |
| 容器与内存 | `XSYS4_DG_DBG` | `src/hll/Delegate.c:65` | 委托（Delegate）诊断总开关 |
| 容器与内存 | `XSYS4_DG_DRIFT` | `src/vm.c:1462` | 委托页"漂移"检测（指向了不该指的对象） |
| 容器与内存 | `XSYS4_DG_GET_DBG` | `src/page.c:1205` | 委托取值诊断 |
| 容器与内存 | `XSYS4_DG_KEEPALIVE` | `src/page.c:1054` | 委托页保活（防止被提前回收） |
| 容器与内存 | `XSYS4_DG_LAZY` | `src/vm.c:4677` | 成员里的委托槽为空时惰性建页（NetworkConfirmation 的 OK/Cancel 靠它） |
| 容器与内存 | `XSYS4_DG_MISS_DBG` | `src/vm.c:1565` | 委托 miss（找不到目标）时打印 |
| 容器与内存 | `XSYS4_DG_MISS_FILTER` | `src/vm.c:1569` | 收窄 DG_MISS 输出 |
| 容器与内存 | `XSYS4_DG_PAGE_DBG` | `src/vm.c:1506` | 委托页生命周期诊断 |
| 容器与内存 | `XSYS4_MEM_STAT` | `src/vm.c:4893` | 堆内存与 RSS 实时统计 |
| 呈现与输入 | `XSYS4_ACT_BUILD_DBG` | `src/parts/activity.c:1260` | [诊断] 打印每个 activity 部件的 `no/name/parent/type`（前 3000 条）。用来把运行时部件号对号到 XML 里的名字，例如 `900000397 = ButtonDeffence` |
| 呈现与输入 | `XSYS4_ACT_CLICKABLE_ALL` | `src/parts/activity.c:1077` | 把所有**活动文件里声明**的部件标为可点（运行时建的 900000xxx 不受影响） |
| 呈现与输入 | `XSYS4_ACT_DBG` | `src/parts/activity.c:1379` | 活动装载与点击派发诊断 |
| 呈现与输入 | `XSYS4_ACT_DEBUG` | `src/parts/activity.c:1551` | 活动诊断的旧开关 |
| 呈现与输入 | `XSYS4_ACT_PART_DBG` | `src/parts/parts.c:2202` | 打印活动部件的建造解析（名字/类型/尺寸/表示領域/flat 名）与 300 号段之后的 SetPos/SetZ |
| 呈现与输入 | `XSYS4_ACT_RELEASE_DGLIST` | `src/parts/activity.c:1371` | [D270 已转正] ReleaseActivity 的 EraseNumberList 出参不再丢弃：把被释放部件的 delegate_index 写回 AIN，供 CPartsMessageManager@ReleaseFunctionSetList 销掉消息分发表里的委托（否则释放后的委托仍可命中，号复用时调到上一次活动的处理器）；默认生效，=0 回退做 A/B |
| 呈现与输入 | `XSYS4_ACT_RELEASE_DGLIST_DBG` | `src/parts/activity.c:1360` | [诊断] 打印每次 ReleaseActivity 释放的活动名、部件数与实际写回 EraseNumberList 的委托下标个数；用来证明 D270 的修复不是死代码（个数恒 0 就说明出参本来没被 AIN 用） |
| 呈现与输入 | `XSYS4_CLICK_ANY` | `src/parts/input.c:260` | [诊断/已证否] 点击捕获改为「非 flat 一律可捕获」。实测 EndInput 直接变 0（无效部件先消费点击）——保留仅作反例 |
| 呈现与输入 | `XSYS4_CLICK_BUBBLE` | `src/parts/input.c:373` | [诊断/已证否] 子部件被点时向父链再发一份 CLICK。s98 实测对面板关闭无效（12 次点击仍只 1 次捕获），说明断点是捕获而非冒泡 |
| 呈现与输入 | `XSYS4_CLICK_ONLY_CLICKABLE` | `src/parts/input.c:173` | [诊断] 点击捕获与 hover 消费都只认「点击許可=1」。这是唯一让 EndInput 恢复正常的判据（点到整屏点击层 700/701） |
| 呈现与输入 | `XSYS4_CLICK_PASS` | `src/parts/input.c:177` | [诊断/已证否] 把「光标选中透過=1」当正向判据。实测 314(1920x693) 抢在技能按钮前，EndInput 变 0 |
| 呈现与输入 | `XSYS4_CLICK_RETARGET` | `src/parts/message.c:69` | 点击被遮挡后重定向到下层部件 |
| 呈现与输入 | `XSYS4_CLICK_SKIP_FLAT` | `src/parts/input.c:183` | [诊断] flat 类型部件不参与点击捕获。战斗里全屏背景 flat 705 的 hb 覆盖整屏、gz 置顶、pass=0，会把技能按钮的点击全部截胡；flat 在原版是「平面图形/演出层」，按钮是 按钮(0) 或 CG |
| 呈现与输入 | `XSYS4_CLICK_SKIP_FULLSCREEN` | `src/parts/input.c:188` | [诊断] 把「hb 覆盖整个视图」的部件排除出点击捕获与 hover 消费。依据：真按钮不会铺满整屏，而演出/背景/整屏点击层会；s93 实测能让点击落到 `ButtonDeffence`(900000397) |
| 呈现与输入 | `XSYS4_CLICK_SKIP_NONCLICKABLE` | `src/parts/input.c:238` | [诊断] 跑测专用：`clickable=0` 的部件不参与点击捕获（回到 D113 之前的行为）。用来验证「全屏背景 flat 705 截胡技能按钮点击」这一条；默认关，开始画面这类「点击任意处继续」的场景开了会卡住 |
| 呈现与输入 | `XSYS4_CTRL_DBG` | `src/parts/parts.c:1485` | controller 栈、视觉地板（cover_layer）、hide/remove 事件 |
| 呈现与输入 | `XSYS4_INPUT_ALL` | `src/parts/input.c:444` | [诊断] 配合 INPUT_DBG 连"没覆盖光标"的部件也打印（带 cover= 标记）；避免把几何算错的按钮误读成"部件不存在" |
| 呈现与输入 | `XSYS4_INPUT_DBG` | `src/parts/input.c:411` | 输入命中的候选部件表与点击状态（每 60 帧一条心跳，非沉默） |
| 呈现与输入 | `XSYS4_PARTS_DUMP` | `src/parts/activity.c:1575` | 活动装载后快照所有显示中的部件（no/ctrl/z/pos/尺寸/a/ps/st/tex/uc） |
| 呈现与输入 | `XSYS4_PARTS_DUMP_MAX` | `src/parts/parts.c:3128` | — |
| 呈现与输入 | `XSYS4_PARTS_DUMP_MIN` | `src/parts/parts.c:3075` | [诊断] 只 dump 部件号 >= 此值的部件；部件表按插入序遍历，地图号段会吃光 PARTS_DUMP_MAX 名额，设 900000300 才能看到动态号段 |
| 呈现与输入 | `XSYS4_PARTS_NO_BASE` | `src/parts/parts.c:2938` | — |
| 呈现与输入 | `XSYS4_UPD_DBG` | `src/parts/parts.c:1530` | PE_Update 的每帧诊断 |
| 自动化后门 | `XSYS4_AUTO_CARD_CLICK` | `src/vm.c:1166` | [后门] 编队卡片视图初始化完成后直调 SceneParty@OnClickCard(idx)；值=卡片索引 |
| 自动化后门 | `XSYS4_AUTO_CARD_CLICK_DELAY` | `src/vm.c:1169` | [后门] 最后一次 InitCardView 之后再等多久点卡（毫秒，默认 1500） |
| 自动化后门 | `XSYS4_AUTO_CARD_ORG` | `src/vm.c:1171` | [后门] 点卡前先调 PartyCardGroup@ChangeOrganizationId(id) 切组织；0=不切 |
| 自动化后门 | `XSYS4_AUTO_CARD_REPEAT` | `src/vm.c:1173` | [后门] 连点同一张卡几次（默认 1）；第一击移除、第二击验证 AddCardToFormations |
| 自动化后门 | `XSYS4_AUTO_MAP` | `src/vm.c:4947` | 无头跑测后门：自动推进到任务地图 |
| 自动化后门 | `XSYS4_AUTO_MAP_DELAY` | `src/vm.c:4948` | 后门延迟（毫秒） |
| 自动化后门 | `XSYS4_AUTO_MAP_ROUTE` | `src/vm.c:1144` | [后门] 地图稳定后直调 SceneQuestMap@StartMoveRoute(23606)，毫秒=延迟 |
| 自动化后门 | `XSYS4_AUTO_PARTY` | `src/vm.c:1139` | 无头跑测后门：自动推进到部队编成 |
| 自动化后门 | `XSYS4_AUTO_PARTY_CLOSE` | `src/vm.c:1164` | [后门] 卡片视图就绪后直调 SceneParty@Close(24301)，验证编队出口；=1 开启 |
| 自动化后门 | `XSYS4_AUTO_PARTY_DELAY` | `src/vm.c:1140` | 后门延迟（毫秒） |
| 自动化后门 | `XSYS4_AUTO_RUNPARTY` | `src/vm.c:5024` | [后门] 地图起来后直接调 Scenes::RunParty(1)，绕开地图 UI 直进编队界面；值=延迟毫秒 |
| 自动化后门 | `XSYS4_NO_LAYER_COVER` | `src/parts/parts.c:1437` | 关闭「视觉地板」优化（怀疑遮挡算错时用） |
| 运行环境 | `XSYS4_DATA_DIR` | `src/pal.c:43` | 数据目录（缺省用编译期默认值） |
| 运行环境 | `XSYS4_SOFT_STUB` | `src/ffi.c:56` | 未实现的导出用零桩顶替（诊断用：判断"缺实现"还是"走错路"） |
| 一次性探针 | `XSYS4_DBG_EIP` | `src/hll/PartsEngine.c:590` | 打印 SetEnableInputProcess 的每次调用与参数（实测 242 次里 238 次 enable=0） |
| 其它 | `XSYS4_ALPHA_DBG` | `src/parts/parts.c:2264` | 打印 SetComponentAlpha 与 AddMotionAlpha 的 parts/alpha 序列（查部件为何全透明）。[D277] 现在同时打 `fno~` 提示来源；注意该值只是参考（见下条） |
| 其它 | `XSYS4_ALPHA_STACK` | `src/parts/parts.c:2267` | [诊断] 配合 ALPHA_DBG，在每次 SetAlpha 时打 AIN 调用栈。定位 alpha 被谁写成 0 必须用它 —— `hll_current_fno()` 返回的是上次 HLL 的值、返回后不复原，实测会打出 `fno=107`(CASConstructionProcess@SetSrcX) 这种完全无关的函数 |
| 其它 | `XSYS4_BACKSCENE_SHOW` | `src/hll/PartsEngine.c:2095` | [诊断] 让 `SetShowForBackScene(no,show)` 真正生效（默认空桩）。战斗里全屏背景 flat 705 挡住技能按钮点击，这个 setter 是 AIN 本来要用来按场景隐藏背景的手段；开它可验证 `[backscene] no= show=` 的实参 |
| 其它 | `XSYS4_CONSTRUCT_776` | `src/hll/PartsEngine.c:502` | =0 把 AddPartsConstructionProcess(776) 退回空桩，用于卡片合成 A/B |
| 其它 | `XSYS4_CONSTRUCT_DBG` | `src/parts/construction.c:338` | 打印部件合成操作：PartsFunc(case 162)、776 批量入口、Build 的 parts/cmd/CG 名与 op 数 |
| 其它 | `XSYS4_CP_DBG` | `src/vm.c:512` | 〔摘〕Parts(1114) 的 m_partsNumber 若在拷贝时还是 0，副本就永远是 0。 |
| 其它 | `XSYS4_DUMP_EVERY` | `src/video.c:460` | 每隔 N 次执行 dump 一次，用于观察循环里的状态漂移 |
| 其它 | `XSYS4_DUMP_FRAME` | `src/video.c:456` | 指定帧的完整快照（配合 DUMP_AT 的扩展模式） |
| 其它 | `XSYS4_DUMP_TOPS` | `src/vm.c:2418` | 〔摘〕「条件是真是假」，而这类「条件永远为假」正是最难查的一类。 |
| 其它 | `XSYS4_ENV_DBG` | `src/vm.c:2977` | — |
| 其它 | `XSYS4_EX_DBG` | `src/hll/MainEXFile.c:527` | 〔摘〕所有任务都会被判成地图任务、剧情整段跳过——所以必须能看见。 |
| 其它 | `XSYS4_EX_DUMP` | `src/parts/activity.c:860` | 按部件名 dump 整棵 EX 子树（查「尺寸」「表示領域」写在哪个键上）；值=部件名 |
| 其它 | `XSYS4_EX_LAYOUT_BY_CONFIG` | `src/parts/activity.c:952` | [诊断] 真布局盒（EX 声明 `レイアウトボックス`）按 `配置` 字段重排（0=FREE/1=纵向/2=横向），而不是 D214 的一律 FREE。用于解决 LargeSkillButton/CardApView 这类「子部件座标全在 (0,0)」的容器底板不显示 |
| 其它 | `XSYS4_EX_LAYOUT_FREE` | `src/parts/activity.c:942` | [D214 已转正] EX 装载的布局盒/用户组件初值改为 FREE（保留 EX 绝对坐标），避免 VERTICAL 自动重排把子部件推到屏幕外；默认生效，=0 回退做 A/B |
| 其它 | `XSYS4_EX_RATIO_POS` | `src/parts/activity.c:1056` | [D216 已转正] EX「座標を率として認識する=1」的 座標 按视图宽高换算成像素；消息窗口 (0,1) 由此变成 (0,1080)，正文才从屏幕外进画面；默认生效，=0 回退做 A/B |
| 其它 | `XSYS4_EX_RECT_DBG` | `src/parts/activity.c:1041` | [诊断] 打印每个 `矩形パーツ` 的四角换算结果与是否被保留（已有尺寸时 REPORT 而非覆盖）；用来确认 Ain 运行时 SetRectangleDetectionSize 的优先级没被 EX 静态值盖掉 |
| 其它 | `XSYS4_EX_RECT_SIZE` | `src/parts/activity.c:1026` | [D271 已转正] EX 装载时为 `矩形パーツ`(组件类型 23) 按四角坐标 左上/右上/左下/右下 算出宽高并 PE_SetPartsSize；此前一个字段都没解析，实测 64 个 PARTS_RECT_DETECTION 部件里 60 个是 0x0（ApBase 因此既画不出也点不到）；默认生效，=0 回退做 A/B |
| 其它 | `XSYS4_FLATEND_DBG` | `src/parts/flat.c:1324` | — |
| 其它 | `XSYS4_FLATEND_MIN_END` | `src/parts/flat.c:1334` | [诊断] 只打印 end_frame >= N 的长演出 flat 的未播完查询（推荐用这个，不受号段复用影响） |
| 其它 | `XSYS4_FLATEND_ONLY` | `src/parts/flat.c:1331` | [诊断] 只跟一个 flat 部件号的 IsPartsFlatEnd 查询。**注意号段会复用**，跨跑测用同一号不可靠，长演出改用 FLATEND_MIN_END |
| 其它 | `XSYS4_FLATEND_STOP_IS_END` | `src/parts/flat.c:1313` | [诊断] 把「flat 停在 stop 键/被 StopPartsFlat 停住」也算作「这段演完了」。用于验证「AIN 等 IsPartsFlatEnd 而 flat 卡在 73/75」的死锁 |
| 其它 | `XSYS4_FLAT_DBG` | `src/parts/flat.c:1235` | 打印 flat 的加载名/hdr 尺寸/库数，以及渲染时的 flat 指针与部件几何（空 flat 会让部件退化成 0x0） |
| 其它 | `XSYS4_FLAT_DELTA_DBG` | `src/parts/flat.c:733` | 〔摘〕pt << 1000 说明被限流（指向①）。 |
| 其它 | `XSYS4_FLAT_FRAME_DBG` | `src/parts/parts.c:1152` | [诊断] 轻量 flat 帧探针：打印 no/ctrl/frame/root_state.stopped/fps/passed_time。值=1 打全部，值=部件号只跟一个。用来区分「帧没推进」是 parts_flat.stopped、root_state.stopped（脚本 stop 键）还是 passed_time 为 0 |
| 其它 | `XSYS4_FLAT_TICK_DBG` | `src/parts/flat.c:220` | [诊断] 逐 tick 打印 flat 推进前后（prev/next/cf/cap/inc/stop/sup/jump），值 = 只跟 end_frame >= N 的长演出（默认 70）。用来分辨「帧不涨」是 increment=0（stopped）还是被封顶 |
| 其它 | `XSYS4_FLAT_TREE` | `src/parts/flat.c:668` | — |
| 其它 | `XSYS4_FPS_DBG` | `src/parts/render.c:1115` | [诊断] 每秒打印一次实际渲染帧数。用来区分「演出推进慢」是逻辑卡死还是 llvmpipe 低帧率（D92 全量标脏） |
| 其它 | `XSYS4_G14_WATCH` | `src/vm.c:2891` | [诊断] 只在 global[14] 的值变化时打印 ip/旧值/新值/栈/调用栈（抓单例被覆写） |
| 其它 | `XSYS4_GL_DBG` | `src/video.c:267` | [诊断] 启动时打印实际 GL vendor/renderer/version —— 判断有没有用上硬件加速的唯一硬证据（llvmpipe=软件，Mesa Intel=硬件） |
| 其它 | `XSYS4_GSAVE_DBG` | `src/savedata.c:762` | 〔摘〕所以按可打印的类型名分类。 |
| 其它 | `XSYS4_HEAP_LIVE_LIMIT` | `src/heap.c:107` | 堆存活槽上限，超了报警 |
| 其它 | `XSYS4_HLL_ARGDBG` | `src/ffi.c:910` | 打印 HLL 调用的实际参数值（排查"参数顺序/位宽错"时用） |
| 其它 | `XSYS4_HLL_HIST` | `src/vm.c:146` | — |
| 其它 | `XSYS4_HLL_REFDBG` | `src/ffi.c:857` | [诊断] 引用型 HLL 返回：打印反解 (page_slot,index) 是否成功；默认只打失败项 |
| 其它 | `XSYS4_HLL_REFDBG_FILTER` | `src/ffi.c:860` | [诊断] 配合 REFDBG，额外打印函数名含该子串的调用（如 At） |
| 其它 | `XSYS4_ICAST_DBG` | `src/vm.c:3070` | 〔摘〕"对象类型确实不对" 和 "我们的转型判定太严"。 |
| 其它 | `XSYS4_IFACE_ARR_FIX` | `src/page.c:563` | — |
| 其它 | `XSYS4_IFACE_COPY_DBG` | `src/page.c:421` | — |
| 其它 | `XSYS4_IFACE_COPY_FIX` | `src/page.c:397` | — |
| 其它 | `XSYS4_IFACE_PUSH_DBG` | `src/hll/Array.c:1131` | 〔摘〕若这里从不出现，说明该数组不是 Array.PushBack 建的。 |
| 其它 | `XSYS4_IFACE_REALLOC_FIX` | `src/page.c:635` | — |
| 其它 | `XSYS4_IP_WATCH` | `src/vm.c:2830` | 监视指令指针，抓"跑到不该跑的地方" |
| 其它 | `XSYS4_KEYCODE_PROBE` | `src/parts/input.c:43` | 打印收到的原始键码 |
| 其它 | `XSYS4_KEY_DBG` | `src/hll/SACT2.c:852` | — |
| 其它 | `XSYS4_LAYER_TRACE` | `src/parts/parts.c:2232` | 图层顺序追踪 |
| 其它 | `XSYS4_LVAR_DBG` | `src/vm.c:188` | — |
| 其它 | `XSYS4_MC_SLOT_DBG` | `src/vm.c:1208` | — |
| 其它 | `XSYS4_MC_WATCH` | `src/vm.c:1022` | 〔摘〕时（callee 声明 N 槽、call site 只压了 1 槽）只能靠猜。 |
| 其它 | `XSYS4_MGRBT` | `src/vm.c:2674` | — |
| 其它 | `XSYS4_MOUSE_DBG` | `src/input.c:385` | [诊断] 打印 SDL 按钮事件（btn / state / clicks / pos / windowID）。用来二分「X 层没把 ButtonPress 交给 SDL」和「SDL 收到了但引擎丢边沿」：同一配方下无 `key:Shift_L` 激活时 `[mouse]` 一条都没有（`s27`），加激活后立即出现并点中 `900000716`（`s26`） |
| 其它 | `XSYS4_MSGF_FIX` | `src/hacks.c:328` | [D204] AIN v12 无 MSGF 段时按函数名把 msgf 解析到 "message"/3（Rance10: fno 6100）；默认开启，=0 关闭做 A/B |
| 其它 | `XSYS4_MSGQ_DBG` | `src/parts/message.c:179` | 〔摘〕对其它值走 member1[delegate_index]。分派打空炮时必须先知道走的哪条。 |
| 其它 | `XSYS4_MSGWIN_DBG` | `src/hll/PartsEngine.c:46` | [诊断] 打印消息窗口配置族 setter 的触达与实参（Active/Text/CGName/FlatName/TextArea/OriginPosMode/Space/Speed），每个函数限 20 条；用来判定「空桩是不是真被调用」 |
| 其它 | `XSYS4_MSGWIN_FRAME_DBG` | `src/parts/render.c:999` | [诊断] 打印消息窗口底板 CG（1920x300~500）是否进入 parts_render 及其 origin_offset / 准入字段；用来区分「没到渲染」与「画了看不见」 |
| 其它 | `XSYS4_MSGWIN_FULL` | `src/parts/activity.c:1162` | [D217 已转正] EX 消息窗口组件补齐 39px 字体/字色/字距/行距，并用独立 CG 部件装载 种类別情报 的 底板ＣＧ名；默认生效，=0 回退做 A/B |
| 其它 | `XSYS4_MSG_DBG` | `src/parts/message.c:45` | 〔摘〕只打非悬停消息，否则前 60 条全被 MOUSE_ON 占满，看不到点击。 |
| 其它 | `XSYS4_MSG_NO_SENTINEL` | `src/parts/message.c:175` | 〔摘〕这就是那个把 SceneStack@WaitFrame 卡死的忙循环。 |
| 其它 | `XSYS4_MSG_POPDBG` | `src/parts/message.c:139` | 〔摘〕这里能看到 type=5 被 pop 而没有对应 dispatch。 |
| 其它 | `XSYS4_MSG_TRACE` | `src/parts/message.c:154` | — |
| 其它 | `XSYS4_NODE_DIGIT` | `src/vm.c:2318` | [D209 诊断后门] 把 QuestMapParamAccessor 的 KeyBase 末尾全角编号末位改成 0-9（００１→００２），仅在 23486 事件链生效；默认关，用于验证战斗链，不是正常游戏路径 |
| 其它 | `XSYS4_NO_AUDIO` | `src/audio_mixer.c:701` | 关闭音频输出 |
| 其它 | `XSYS4_NO_AUTO_MOTION` | `src/parts/motion.c:102` | =1 关闭「首次加 motion 自动开表」（D186），用于 motion 自动启动的 A/B |
| 其它 | `XSYS4_NO_CP_ALPHA_FIX` | `src/parts/parts.c:2292` | =1 关闭「合成类部件 alpha=0 提升为 255」（卡片立绘 A/B 回退开关）。[D224 收窄] 提升现在只作用于 >=8x8 的真实合成纹理，2x2 全屏淡出遮罩不再被误抬 |
| 其它 | `XSYS4_NO_DI_FALLBACK` | `src/parts/message.c:99` | [回退闸] 关闭 D260 的委托下标回退（消息 delegate_index<0 时不再回退到 0）。用于 A/B：开着它战斗会退回「点击打空炮、停 Round 1/10」的旧行为 |
| 其它 | `XSYS4_NO_PUMP` | `src/vm.c:4849` | 事件泵空转诊断 |
| 其它 | `XSYS4_NULLMETHOD_FIX` | `src/vm.c:1230` | — |
| 其它 | `XSYS4_NULLREF_DBG` | `src/vm.c:322` | 〔摘〕从栈上弹出一个引用，返回被引用对象的地址。 |
| 其它 | `XSYS4_NULL_METHOD` | `src/vm.c:1076` | — |
| 其它 | `XSYS4_NULL_RET` | `src/vm.c:1116` | 〔摘〕XSYS4_NULL_RET=0 可回退，用于定位"到底跳过了哪些调用"。 |
| 其它 | `XSYS4_OBJ_WATCH` | `src/vm.c:2850` | 监视指定堆槽的读写 |
| 其它 | `XSYS4_PART_TYPE_DBG` | `src/parts/activity.c:695` | 未识别的部件类型：打印 种类別情报 的全部子键名（定位「真实类型藏在哪个子节点」） |
| 其它 | `XSYS4_PE_DBG` | `src/hll/PartsEngine.c:36` | PartsEngine 调用诊断 |
| 其它 | `XSYS4_PHANTOM_REUSE` | `src/vm.c:1309` | 幽灵槽复用诊断 |
| 其它 | `XSYS4_PRED_BOUND` | `src/hll/Array.c:694` | — |
| 其它 | `XSYS4_PRED_DBG` | `src/hll/Array.c:665` | — |
| 其它 | `XSYS4_PUMP_MASK` | `src/vm.c:4861` | — |
| 其它 | `XSYS4_RA_DBG` | `src/vm.c:3146` | — |
| 其它 | `XSYS4_RECYCLE_SLOTS` | `src/heap.c:199` | 复用已释放的堆槽（默认关） |
| 其它 | `XSYS4_REFDBG` | `src/vm.c:383` | 〔摘〕它之后的代码全部当作不可达删掉。 |
| 其它 | `XSYS4_REFREF_DBG` | `src/vm.c:2737` | — |
| 其它 | `XSYS4_REF_AT` | `src/vm.c:2653` | — |
| 其它 | `XSYS4_RELEASE_RECURSIVE` | `src/parts/parts.c:2146` | [D219 已转正] PE_ReleaseParts 递归释放整棵部件子树（原 parts_release 只把子部件摘链不释放，对话框关掉后文字层残留，H025/[LAYER-LEAK]）；默认生效，=0 回退做 A/B |
| 其它 | `XSYS4_RSS_LIMIT` | `src/vm.c:2379` | 进程 RSS 上限，超了报警 |
| 其它 | `XSYS4_SAVE_GROUP_ANY` | `src/savedata.c:736` | — |
| 其它 | `XSYS4_SHOT_DIR` | `src/screenshot.c:178` | [诊断] 指定目录后按 Alt+S 让引擎用 glReadPixels 直接 dump GL main surface 为 shot_NNN.png，绕过 zenity 文件框与 X11 合成；GL 直绘下 x11grab/import 只能拿到全黑 framebuffer，画面判据以此为准（GUIDE 1.13） |
| 其它 | `XSYS4_SHOT_PARTS_DUMP` | `src/screenshot.c:184` | [诊断] 配合 XSYS4_SHOT_DIR：每次引擎自截图时打一次部件快照（layer_show / cover / 每部件 vis）+ 同帧 CG 绘制日志（最近 12 次调用的 alpha / multiply_color / scale / 纹理尺寸、以及两次截图之间的绘制次数）；按截图时刻触发而非全程探针，避免拖慢帧率改变剧情时序；需配 XSYS4_PARTS_DUMP_MIN=900000300 / MAX=1000 |
| 其它 | `XSYS4_SLOT_ID` | `src/heap.c:250` | 打印稳定槽 ID，便于跨日志对照同一对象 |
| 其它 | `XSYS4_SP_BALANCE` | `src/vm.c:1950` | 每次函数返回校验栈平衡（失败即报） |
| 其它 | `XSYS4_SP_TRACE` | `src/vm.c:2619` | 环形缓冲记录栈指针变化，抓"玄学栈漂移" |
| 其它 | `XSYS4_SP_WATCH` | `src/vm.c:5136` | 监视指定帧的栈指针 |
| 其它 | `XSYS4_SRDBG` | `src/vm.c:3988` | 〔摘〕绝不会是一张活着的 ARRAY_PAGE。 |
| 其它 | `XSYS4_STACK_SAMPLE` | `src/vm.c:5160` | 周期性采样调用栈（抓谁在长时间占栈） |
| 其它 | `XSYS4_STRICT_REF` | `src/vm.c:407` | 严格引用计数检查（会把"漏 ref/unref"变成显式报错） |
| 其它 | `XSYS4_STR_INT_DBG` | `src/hll/String.c:59` | 〔摘〕"取对了但字符串已经死了"。 |
| 其它 | `XSYS4_STUB_BT` | `src/ffi.c:160` | 软桩命中时附带回溯 |
| 其它 | `XSYS4_STUB_LOG` | `src/ffi.c:102` | 软桩命中清单写到该文件 |
| 其它 | `XSYS4_SUBST_BT` | `src/ffi.c:638` | 静态库替换（subst）时打印回溯 |
| 其它 | `XSYS4_SWAP_LOG` | `src/video.c:446` | 〔摘〕这样启动器可以在第一帧呈现之后立刻冻结进程。 |
| 其它 | `XSYS4_TALT_EXTRA_FORMATS` | `src/parts/flat.c:400` | [D275 已转正] D200 的「LIBL=0 时追加 TALT 图集」只认 QNT 魔数，而 `战斗画面/シス／战斗／技能按钮／一般` 的 TALT 是 **AJP**，于是零纹理库、底板一个像素不画。默认改成交给 `cg_check_format()` 判定；=qnt 回退成旧行为做 A/B |
| 其它 | `XSYS4_TEXT_RENDER_DBG` | `src/parts/render.c:155` | =1 列全部非 UC 文字层；=<部件号下限> 只盯该号段（如 900000300），每号最多 3 条并附 show/alpha/vis/mw/mwshow 准入字段；修掉了「全局 80 条被地图吃光」的假阴性 |
| 其它 | `XSYS4_TITLE_UI_FIX` | `src/parts/render.c:124` | — |
| 其它 | `XSYS4_TRACE` | `src/vm.c:2358` | — |
| 其它 | `XSYS4_UC_RENDER_DBG` | `src/text.c:274` | 用户组件（TextButton 等）渲染诊断 |
| 其它 | `XSYS4_USERCOMP_DBG` | `src/parts/activity.c:849` | — |
| 其它 | `XSYS4_WATCH_IFACE` | `src/vm.c:342` | — |
| 其它 | `XSYS4_XSET_DBG` | `src/vm.c:3041` | — |
