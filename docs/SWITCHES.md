# 诊断开关总表

> 由 `tools/gen-switches.py` 生成，**不要手改**。改代码后重跑一次。

所有开关默认都是**关闭**的；只有显式设置才生效。
新增开关必须带一行用途说明，否则本表会显示 `—`，
它同时是一个可量化的债：`tools/gen-switches.py --check` 的返回值就是未标注数量。

共 **117** 个开关，其中 **24** 个没有就近注释。

**用途一栏的两种来源**：

- 正常文字 = `tools/switch-desc.txt` 里人工写的一句话（可信）
- 带 `〔摘〕` = 从源码就近注释摘的片段，**可能是背景说明而不是用途**，用前请核对源码

| 分组 | 开关 | 位置 | 用途 |
|---|---|---|---|
| 断点与快照 | `XSYS4_CALL_TRACE` | `src/vm.c:755` | 逗号分隔的 AIN 函数号；命中时打印调用点与返回地址（"有没有被调到"的第一手证据） |
| 断点与快照 | `XSYS4_CENSUS` | `src/vm.c:1242` | 按函数做调用普查（与 FN_DUMP 互补） |
| 断点与快照 | `XSYS4_DUMP_AT` | `src/vm.c:2199` | 在 `<十六进制地址>[,最多几次]` 处停下，打印帧名、this 各成员、局部变量和操作数栈 |
| 断点与快照 | `XSYS4_FN_DUMP` | `src/vm.c:853` | 函数调用次数统计与慢函数排行 |
| 断点与快照 | `XSYS4_FN_DUMP_ALL` | `src/vm.c:869` | 统计时不做前 N 次闸门，低频函数也全打 |
| 断点与快照 | `XSYS4_FN_DUMP_SKIP` | `src/vm.c:861` | 统计时跳过指定函数号 |
| 断点与快照 | `XSYS4_HLL_TRACE` | `src/ffi.c:524` | 打印每次 HLL 调用（`lib= fno= nr_args= sp= ret=`），不设 FILTER 会很吵 |
| 断点与快照 | `XSYS4_HLL_TRACE_FILTER` | `src/ffi.c:531` | 收窄 HLL_TRACE：`PartsEngine` 按库名、`27:73` 按库:序号、其它按函数名子串，逗号分隔 |
| 容器与内存 | `XSYS4_ALLOC0_DBG` | `src/page.c:449` | 0 号堆槽分配的诊断 |
| 容器与内存 | `XSYS4_ARRAY_DBG` | `src/hll/Array.c:1142` | Array 诊断的旧开关（与 ARR_DBG 同类，逐步统一） |
| 容器与内存 | `XSYS4_ARRAY_FREE_FIX` | `src/hll/Array.c:472` | 数组释放路径的实验修复（默认关，会改行为） |
| 容器与内存 | `XSYS4_ARRAY_REF_FIX` | `src/hll/Array.c:821` | 数组元素引用处理的实验修复（默认关，会改行为） |
| 容器与内存 | `XSYS4_ARR_ALLOC_DBG` | `src/ffi.c:726` | 只打数组分配 |
| 容器与内存 | `XSYS4_ARR_DBG` | `src/ffi.c:638` | Array 操作诊断：alloc/find/any/emp/free/subst 逐条打 |
| 容器与内存 | `XSYS4_ARR_TYPE_DBG` | `src/hll/Array.c:1016` | 数组元素类型推导诊断（泛型数组"由首个元素定型"出问题时用） |
| 容器与内存 | `XSYS4_ARR_UNSET_FIX` | `src/hll/Array.c:399` | 未初始化槽读取的实验修复（默认关，会改行为） |
| 容器与内存 | `XSYS4_BADSTRUCT_DBG` | `src/vm.c:1173` | 结构体成员访问越界诊断 |
| 容器与内存 | `XSYS4_BADTHIS` | `src/vm.c:1142` | 非法 this（类型不匹配/越界）时打回溯 |
| 容器与内存 | `XSYS4_CTOR_DBG` | `src/page.c:465` | 构造函数调用诊断 |
| 容器与内存 | `XSYS4_DF_DBG` | `src/heap.c:220` | 委托释放路径诊断 |
| 容器与内存 | `XSYS4_DF_FREE_DBG` | `src/heap.c:315` | 委托释放的逐次释放诊断 |
| 容器与内存 | `XSYS4_DF_MAX` | `src/heap.c:223` | 委托释放的最大迭代次数（防死循环） |
| 容器与内存 | `XSYS4_DGPLUSA_DBG` | `src/vm.c:4333` | — |
| 容器与内存 | `XSYS4_DGPLUSA_MAX` | `src/vm.c:4337` | 〔摘〕前 400 条会被 startup 的 CParts@Attach 占满。 |
| 容器与内存 | `XSYS4_DG_ASSIGN_DBG` | `src/vm.c:4309` | 委托赋值诊断 |
| 容器与内存 | `XSYS4_DG_CALL_DBG` | `src/vm.c:1482` | 委托调用诊断 |
| 容器与内存 | `XSYS4_DG_DBG` | `src/hll/Delegate.c:65` | 委托（Delegate）诊断总开关 |
| 容器与内存 | `XSYS4_DG_DRIFT` | `src/vm.c:1349` | 委托页"漂移"检测（指向了不该指的对象） |
| 容器与内存 | `XSYS4_DG_GET_DBG` | `src/page.c:1215` | 委托取值诊断 |
| 容器与内存 | `XSYS4_DG_KEEPALIVE` | `src/page.c:1064` | 委托页保活（防止被提前回收） |
| 容器与内存 | `XSYS4_DG_LAZY` | `src/vm.c:4372` | 成员里的委托槽为空时惰性建页（NetworkConfirmation 的 OK/Cancel 靠它） |
| 容器与内存 | `XSYS4_DG_MISS_DBG` | `src/vm.c:1452` | 委托 miss（找不到目标）时打印 |
| 容器与内存 | `XSYS4_DG_MISS_FILTER` | `src/vm.c:1456` | 收窄 DG_MISS 输出 |
| 容器与内存 | `XSYS4_DG_PAGE_DBG` | `src/vm.c:1393` | 委托页生命周期诊断 |
| 容器与内存 | `XSYS4_MEM_STAT` | `src/vm.c:4575` | 堆内存与 RSS 实时统计 |
| 呈现与输入 | `XSYS4_ACT_CLICKABLE_ALL` | `src/parts/activity.c:702` | 把所有**活动文件里声明**的部件标为可点（运行时建的 900000xxx 不受影响） |
| 呈现与输入 | `XSYS4_ACT_DBG` | `src/parts/activity.c:856` | 活动装载与点击派发诊断 |
| 呈现与输入 | `XSYS4_ACT_DEBUG` | `src/parts/activity.c:1021` | 活动诊断的旧开关 |
| 呈现与输入 | `XSYS4_CLICK_RETARGET` | `src/parts/message.c:73` | 点击被遮挡后重定向到下层部件 |
| 呈现与输入 | `XSYS4_CTRL_DBG` | `src/parts/parts.c:1441` | controller 栈、视觉地板（cover_layer）、hide/remove 事件 |
| 呈现与输入 | `XSYS4_INPUT_DBG` | `src/parts/input.c:296` | 输入命中的候选部件表与点击状态 |
| 呈现与输入 | `XSYS4_PARTS_DUMP` | `src/parts/activity.c:1045` | 活动装载后快照所有显示中的部件（no/ctrl/z/pos/尺寸/a/ps/st/tex/uc） |
| 呈现与输入 | `XSYS4_PARTS_NO_BASE` | `src/parts/parts.c:2741` | — |
| 呈现与输入 | `XSYS4_UPD_DBG` | `src/parts/parts.c:1458` | PE_Update 的每帧诊断 |
| 自动化后门 | `XSYS4_AUTO_MAP` | `src/vm.c:4629` | 无头跑测后门：自动推进到任务地图 |
| 自动化后门 | `XSYS4_AUTO_MAP_DELAY` | `src/vm.c:4630` | 后门延迟（毫秒） |
| 自动化后门 | `XSYS4_AUTO_PARTY` | `src/vm.c:1071` | 无头跑测后门：自动推进到部队编成 |
| 自动化后门 | `XSYS4_AUTO_PARTY_DELAY` | `src/vm.c:1072` | 后门延迟（毫秒） |
| 自动化后门 | `XSYS4_AUTO_RUNPARTY` | `src/vm.c:4677` | [后门] 地图起来后直接调 Scenes::RunParty(1)，绕开地图 UI 直进编队界面；值=延迟毫秒 |
| 自动化后门 | `XSYS4_NO_LAYER_COVER` | `src/parts/parts.c:1409` | 关闭「视觉地板」优化（怀疑遮挡算错时用） |
| 运行环境 | `XSYS4_DATA_DIR` | `src/pal.c:43` | 数据目录（缺省用编译期默认值） |
| 运行环境 | `XSYS4_SOFT_STUB` | `src/ffi.c:60` | 未实现的导出用零桩顶替（诊断用：判断"缺实现"还是"走错路"） |
| 一次性探针 | `XSYS4_DBG_EIP` | `src/hll/PartsEngine.c:489` | 打印 SetEnableInputProcess 的每次调用与参数（实测 242 次里 238 次 enable=0） |
| 其它 | `XSYS4_CP_DBG` | `src/vm.c:482` | 〔摘〕Parts(1114) 的 m_partsNumber 若在拷贝时还是 0，副本就永远是 0。 |
| 其它 | `XSYS4_DI_FALLBACK` | `src/parts/message.c:87` | 〔摘〕Controlled experiment: fall back to index 0 when explicitly enabled. |
| 其它 | `XSYS4_DUMP_EVERY` | `src/video.c:452` | 每隔 N 次执行 dump 一次，用于观察循环里的状态漂移 |
| 其它 | `XSYS4_DUMP_FRAME` | `src/video.c:448` | 指定帧的完整快照（配合 DUMP_AT 的扩展模式） |
| 其它 | `XSYS4_ENV_DBG` | `src/vm.c:2673` | — |
| 其它 | `XSYS4_EX_DBG` | `src/hll/MainEXFile.c:531` | 〔摘〕所有任务都会被判成地图任务、剧情整段跳过——所以必须能看见。 |
| 其它 | `XSYS4_GSAVE_DBG` | `src/savedata.c:755` | 〔摘〕the printable type name instead. |
| 其它 | `XSYS4_HEAP_LIVE_LIMIT` | `src/heap.c:111` | 堆存活槽上限，超了报警 |
| 其它 | `XSYS4_HLL_ARGDBG` | `src/ffi.c:895` | 打印 HLL 调用的实际参数值（排查"参数顺序/位宽错"时用） |
| 其它 | `XSYS4_ICAST_DBG` | `src/vm.c:2766` | 〔摘〕"对象类型确实不对" 和 "我们的转型判定太严"。 |
| 其它 | `XSYS4_IFACE_ARR_FIX` | `src/page.c:567` | — |
| 其它 | `XSYS4_IFACE_COPY_DBG` | `src/page.c:425` | — |
| 其它 | `XSYS4_IFACE_COPY_FIX` | `src/page.c:401` | — |
| 其它 | `XSYS4_IFACE_PUSH_DBG` | `src/hll/Array.c:1060` | 〔摘〕若这里从不出现，说明该数组不是 Array.PushBack 建的。 |
| 其它 | `XSYS4_IFACE_REALLOC_FIX` | `src/page.c:641` | — |
| 其它 | `XSYS4_IP_WATCH` | `src/vm.c:2551` | 监视指令指针，抓"跑到不该跑的地方" |
| 其它 | `XSYS4_KEYCODE_PROBE` | `src/parts/input.c:47` | 打印收到的原始键码 |
| 其它 | `XSYS4_LAYER_TRACE` | `src/parts/parts.c:2111` | 图层顺序追踪 |
| 其它 | `XSYS4_LVAR_DBG` | `src/vm.c:158` | — |
| 其它 | `XSYS4_MC_SLOT_DBG` | `src/vm.c:1095` | — |
| 其它 | `XSYS4_MC_WATCH` | `src/vm.c:954` | 〔摘〕时（callee 声明 N 槽、call site 只压了 1 槽）只能靠猜。 |
| 其它 | `XSYS4_MGRBT` | `src/vm.c:2395` | — |
| 其它 | `XSYS4_MSGQ_DBG` | `src/parts/message.c:167` | 〔摘〕对其它值走 member1[delegate_index]。分派打空炮时必须先知道走的哪条。 |
| 其它 | `XSYS4_MSG_DBG` | `src/parts/message.c:49` | 〔摘〕只打非悬停消息，否则前 60 条全被 MOUSE_ON 占满，看不到点击。 |
| 其它 | `XSYS4_MSG_NO_SENTINEL` | `src/parts/message.c:163` | 〔摘〕busy loop that froze SceneStack@WaitFrame. |
| 其它 | `XSYS4_MSG_POPDBG` | `src/parts/message.c:127` | 〔摘〕这里能看到 type=5 被 pop 而没有对应 dispatch。 |
| 其它 | `XSYS4_MSG_TRACE` | `src/parts/message.c:142` | — |
| 其它 | `XSYS4_NO_AUDIO` | `src/audio_mixer.c:705` | 关闭音频输出 |
| 其它 | `XSYS4_NO_PUMP` | `src/vm.c:4544` | 事件泵空转诊断 |
| 其它 | `XSYS4_NULLMETHOD_FIX` | `src/vm.c:1117` | — |
| 其它 | `XSYS4_NULLREF_DBG` | `src/vm.c:292` | 〔摘〕Pop a reference off the stack, returning the address of the referen… |
| 其它 | `XSYS4_NULL_METHOD` | `src/vm.c:1008` | — |
| 其它 | `XSYS4_NULL_RET` | `src/vm.c:1048` | 〔摘〕XSYS4_NULL_RET=0 可回退，用于定位"到底跳过了哪些调用"。 |
| 其它 | `XSYS4_OBJ_WATCH` | `src/vm.c:2571` | 监视指定堆槽的读写 |
| 其它 | `XSYS4_PE_DBG` | `src/hll/PartsEngine.c:39` | PartsEngine 调用诊断 |
| 其它 | `XSYS4_PHANTOM_REUSE` | `src/vm.c:1196` | 幽灵槽复用诊断 |
| 其它 | `XSYS4_PRED_BOUND` | `src/hll/Array.c:623` | — |
| 其它 | `XSYS4_PRED_DBG` | `src/hll/Array.c:594` | — |
| 其它 | `XSYS4_RA_DBG` | `src/vm.c:2842` | — |
| 其它 | `XSYS4_RECYCLE_SLOTS` | `src/heap.c:203` | 复用已释放的堆槽（默认关） |
| 其它 | `XSYS4_REFDBG` | `src/vm.c:353` | 〔摘〕它之后的代码全部当作不可达删掉。 |
| 其它 | `XSYS4_REFREF_DBG` | `src/vm.c:2458` | — |
| 其它 | `XSYS4_REF_AT` | `src/vm.c:2374` | — |
| 其它 | `XSYS4_RSS_LIMIT` | `src/vm.c:2175` | 进程 RSS 上限，超了报警 |
| 其它 | `XSYS4_SAVE_GROUP_ANY` | `src/savedata.c:729` | — |
| 其它 | `XSYS4_SLOT_ID` | `src/heap.c:254` | 打印稳定槽 ID，便于跨日志对照同一对象 |
| 其它 | `XSYS4_SP_BALANCE` | `src/vm.c:1790` | 每次函数返回校验栈平衡（失败即报） |
| 其它 | `XSYS4_SP_TRACE` | `src/vm.c:2340` | 环形缓冲记录栈指针变化，抓"玄学栈漂移" |
| 其它 | `XSYS4_SP_WATCH` | `src/vm.c:4720` | 监视指定帧的栈指针 |
| 其它 | `XSYS4_SRDBG` | `src/vm.c:3683` | 〔摘〕the bottom of the triple, never a live ARRAY_PAGE. |
| 其它 | `XSYS4_STACK_SAMPLE` | `src/vm.c:4734` | 周期性采样调用栈（抓谁在长时间占栈） |
| 其它 | `XSYS4_STRICT_REF` | `src/vm.c:377` | 严格引用计数检查（会把"漏 ref/unref"变成显式报错） |
| 其它 | `XSYS4_STR_INT_DBG` | `src/hll/String.c:58` | 〔摘〕"取对了但字符串已经死了"。 |
| 其它 | `XSYS4_STUB_BT` | `src/ffi.c:164` | 软桩命中时附带回溯 |
| 其它 | `XSYS4_STUB_LOG` | `src/ffi.c:106` | 软桩命中清单写到该文件 |
| 其它 | `XSYS4_SUBST_BT` | `src/ffi.c:642` | 静态库替换（subst）时打印回溯 |
| 其它 | `XSYS4_SWAP_LOG` | `src/video.c:438` | 〔摘〕launcher can freeze the process right after the first presented frame. |
| 其它 | `XSYS4_TEXT_RENDER_DBG` | `src/parts/render.c:148` | 文字渲染诊断（字体/码表/基线） |
| 其它 | `XSYS4_TITLE_UI_FIX` | `src/parts/render.c:126` | — |
| 其它 | `XSYS4_TRACE` | `src/vm.c:2154` | — |
| 其它 | `XSYS4_UC_RENDER_DBG` | `src/text.c:278` | 用户组件（TextButton 等）渲染诊断 |
| 其它 | `XSYS4_USERCOMP_DBG` | `src/parts/activity.c:599` | — |
| 其它 | `XSYS4_WATCH_IFACE` | `src/vm.c:312` | — |
| 其它 | `XSYS4_XSET_DBG` | `src/vm.c:2737` | — |
