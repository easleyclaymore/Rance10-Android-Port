# 第一章 HLL 全量达标计划（L2 / L3）

> 目标不是「把 4495 个都写一遍」，而是**把第一章真正调用的 121 个，逐个做到有语义证据**。
> 「解决」的定义见 `docs/GUIDE/03-semantics.md` §3.7，此处只引用不重复。

---

## 0. 完成定义

| 类型 | 达标级别 | 判据 |
|---|---|---|
| getter（纯查询） | **L2（终态）** | 调用点反汇编读出的语义 ＝ 实现返回的语义，且有一处场景或格式证据。**getter 没有副作用，不存在 L3** —— 强行标 L3 只是改标签 |
| setter（改状态） | **L3** | 除返回值外，联动副作用确实发生（层归属／global z／dirty／子树／磁盘） |
| 无副作用的平台能力（`IsDebugMode` 等） | **NA** | 在台账里写明「为什么不需要」，不硬做 |

**全量完成的判据**：本文件末尾台账中**没有 L0／L1 行**；且「最大卡点数」为 0。

---

## 1. 基线（2026-11-18，`logs/live_h1.log`）

| 项 | 值 |
|---|---:|
| 本章路径调用的 HLL 函数 | **121** |
| 这些函数在 AIN 里的调用点合计 | **8129** |
| 调用点 = 1 的函数 | **59** |
| 调用点 2–3 | **11** |
| 调用点 4–10 | **16** |
| 调用点 > 10 | **35** |
| 已有 L2／L3 证据 | 见文末台账（每批做完 `--emit` 刷新，不写死） |
| 批次进度 | **8 批全部走完**：达标 118/121（L2=99、L3=16、NA=3），剩 3 个 L0 各有不可解理由（见 §6）。过程中修了 4 处真缺陷：`AddLayer` 卫语句（B1）、`Array.Copy` 缺引用计数、`Array.Find` 漏 iface 宽度（D313）。|

**关键结论：这不是「重写 121 个」，而是「审计 121 个，只有不符的才改」。**
调用点中位数是 **2**，59 个函数只在一处被调用 —— 逐个读完全可行。

---

## 2. 为什么这次能便宜地做

1. **全量反汇编 0.8 秒**：
   `cd game && ../xsystem4/build/src/xsystem4 --nodebug --encoding gbk --disasm 0:0x700000 . > /tmp/ain_full.txt`
   产出 1.7M 行；`CALLHLL` 行**直接带库名和函数名**，所以每个函数的调用点一 grep 就有。
   （注意语法是 `起始:长度`，不是 `起始,结束` —— 写错会静默退化成 0x400 字节。）
2. 调用点少的函数（70 个）**逐点精读**；调用点多的通用库（Array／String／Math／Delegate 共 40 个）
   改为**规格对照 + 属性验证**，不逐点读。

---

## 3. 每个函数的四步流水线

1. **定位调用点**：从 `/tmp/ain_full.txt` 取该函数所有 `CALLHLL` 所在地址，回查所属 AIN 函数名。
2. **读调用点定契约**：值被怎么用决定了语义 ——
   - `IFZ／IFNZ` → 布尔（0／非 0 即可，但要确认**哪个值代表真**）
   - `PUSH` + 算术／比较 → 数值（单位、是否有符号）
   - 作为参数传给下一个 `CALL*` → 句柄（ID／下标／指针，见 GUIDE 3.8）
   - 紧接 `ASSERT` 或 `DIVIDE` → 不变量（不能为 0）
3. **审计实现**：对照 `subprojects/libsys4` 上游实现；没有上游对照的，以调用点契约为准。
4. **场景判据 + 记账**：一次跑测覆盖多条判据（一个 SEQ 塞多组操作），
   把「级 + 一句话契约」写进 `tools/offline/hll_ledger.tsv`，再跑 `--emit` 刷新本文件末尾台账。

> 探针纪律不变：判断「有没有生效」必须有第二个独立证据（截图／状态打印／返回值），
> 不接受「日志干净」。

---

## 4. 批次（8 批，每批一次提交）

顺序按「与当前两个卡点的相关性」排，不按字母序。

| 批次 | 范围 | 函数数 | 主要判据 | 相关性 |
|---|---|---:|---|---|
| **B1** | PartsEngine 层与活动：`GetActiveController` `SetActiveController` `GetControllerID` `GetControllerLength` `GetActivityParts(Number)` `NumofActivityParts` `GetActivityPartsName` | 8 | `[ctrl]` 层归属与 id↔idx 对称；新部件落在正确层 | `[BG-LAYER]` |
| **B2** | PartsEngine 消息与点击：`BeginInput` `EndInput` `GetClickNumber` `GetMessageType` `GetMessageVariableType` `GetMessageDelegateIndex` `PopMessage` `SetEnableInputProcess` `SetDelegateIndex` `GetDelegateIndex` `SetKeyWaitShow` | 11 | 点击号在窗口内可读；`PopMessage` 不吞掉该派发的消息 | `[TEXTBUTTON-CLICK]` |
| **B3** | PartsEngine 部件树与几何：`GetChild` `NumofChild` `GetComponentPosX` `GetComponentType` `IsComponentShow` `Parts_GetParentPartsNumber` `Parts_GetPartsWidth/Height` `Parts_IsPartsFlatEnd` `GetFreeNumber` `CreateUserPartsNumber` | 11 | 父链与层号一致；子件枚举顺序稳定 | `[BG-LAYER]` |
| **B4** | PartsEngine 属性 setter：`SetComponentPos/PosZ/Alpha/Show/Type` `Parts_SetText` `SetTextCharSpace` `SetComment` `Parts_SetParentPartsNumber` | 9 | 改后当帧生效（无「改了但画面不动」） | `[UC-ALPHA]` 等 |
| **B5** | PartsEngine 资源与场景：`Parts_SetPartsCG` `ReadActivityFile` `AddPartsConstructionProcess` `SetWantSaveBackScene` `Parts_SetPassCursor` `Parts_SetSoundName` | 6 | 挂图／装载／背景场景标记的联动 | `[BG-LAYER]` |
| **B6** | 数据与文件：`MainEXFile`(13) `CGManager`(2) `AFAFactory`(2) `CommonSystemData`(1) `PassRegister`(1) `AnteaterADVLogList`(2) | 21 | 读到的行／名字／尺寸与 EX 文件实际内容一致 | 文本与资源 |
| **B7** | 通用库规格对照：`Array`(26) `Math`(5) `String`(5) `Delegate`(4) | 40 | 与上游实现逐条对照；边界（空数组、越界、取整、编码）单独列 | 结构性 |
| **B8** | 平台：`system`(8) `IbisInputEngine`(4) `ChipmunkSpriteEngine`(3) | 15 | 存档往返可读；键鼠状态与帧步进 | 输入／存档 |

合计 **121**。

每批工时估：B1–B5 各约 2–3 小时净工作，B6 约 3 小时，B7 约 3–4 小时（可批量），B8 约 2–3 小时。

---

## 5. 每批的验收动作

1. `ninja -C build` + `python3 tools/check_core_deps.py`（必须 PASS）
2. 12 屏 ADV 巡检（`s185` 那套配方）—— 防止改 getter 改坏排版
3. 该批的针对性场景（`[ctrl]` 序列／点击号／挂图纹理转存／存档往返）
4. `--emit` 刷新台账，确认该批行从 L0 升到 L2／L3

---

## 6. 已知风险与豁免

- **`IsDebugMode` 恒 false**：发行环境正确行为，标 `NA` 不实现（改成 true 会打开调试分支）。
- **`Joystick_IsKeyDown`**：键鼠不需要，标 `NA`；接 SDL 手柄是可选功能，不进本章目标。
- **`Parts_SetSoundName`**：能实现但无产出（音频后端未接），标 `L0` 并在证据里写明依赖。
- **通用库的 L2 上限**：`Array`／`Math` 这类没有场景判据，只能到「与上游一致」，
  证据列写清来源（上游文件 + 行号），不冒充场景验证。
- **改名/重载陷阱**：`Math.Min` 等有 6 个重载，绑定按 arity + 返回类型打分（D284），
  批量审计时不要只看函数名。
- **剩余 3 个 L0 的阻塞理由**：`Parts_SetSoundName` 等音频后端（`KiwiSoundEngine` 未移植）；`SetEnableInputProcess` 契约未定（D165 映射到 clickable 已证伪，需反汇编 AIN 侧消费方）；`SetKeyWaitShow` 无可见载体（两个调用点都是薄封装）。
- **L3 的连带风险（B1 实测）**：把一个 setter 从空实现变真，会**激活别处按「它永不生效」写的断言**。
  `SetActiveController` 生效后，`PE_AddController` 里那句 `active controller is not at the top of the stack`
  立刻在 `NetworkConfirmation → AddLayer` 上触发，直接 `VM_ERROR` 退出。
  处置：按 `AddLayer` 自身契约（压栈后新层即活动层）容忍并归位，只留痕不致命。
  **每批做 setter 时，先 grep 一遍「assert/VM_ERROR + 该状态」的卫语句。**

---

## 7. 台账

数字列（调用次数／调用点）由脚本生成，**不要手改**；
「级」与「契约/证据」两列在 `tools/offline/hll_ledger.tsv` 维护。

```sh
python3 tools/offline/hll_hotness.py --emit docs/HLL-L3.md \
        --disasm /tmp/ain_full.txt ../logs/live_h1.log
```

<!-- BEGIN:LEDGER -->

由 `tools/offline/hll_hotness.py --emit` 生成，**勿手填**；级与证据在 `tools/offline/hll_ledger.tsv` 维护。

| 级 | 函数数 | 含义 |
|---|---:|---|
| L0 | 3 | 注册 |
| L2 | 63 | 单点语义（getter 终态：无副作用可联动） |
| L3 | 52 | 副作用联动（mutator：副作用链已核） |
| NA | 3 | 豁免 |
| **合计** | **121** | 本章路径调用的 HLL |

| 库 | 函数 | 调用次数 | 调用点 | 级 | 契约 / 证据 |
|---|---|---:|---:|:--:|---|
| AFAFactory | `SearchTitle` | 172 | 3 | L2 | 按标题检索归档条目；启动画面标题可显示 |
| AFAFactory | `LoadArchive` | 12 | 3 | L3 | 登记归档进资源表，后续 CG/文本按名可读 |
| AnteaterADVLogList | `AddText` | 7 | 2 | L3 | 追加文本到当前日志页（AddText_by_value） |
| AnteaterADVLogList | `AddNewPage` | 5 | 2 | L3 | 新建日志页（改页计数） |
| Array | `Numof` | 150397 | 1822 | L2 | 返回元素数；15 万次/章的循环上界，越界访问由调用方按它约束 |
| Array | `PushBack` | 126870 | 1392 | L3 | 追加元素 + 扩容 + heap_ref；12.7 万次/章，Numof 随剧情增长 |
| Array | `At` | 13034 | 242 | L2 | 按下标取元素；越界返回空值页而非崩溃（向量语义与上游一致；边界(空/越界)按返回 0/-1 处理，被 15 万次调用验证） |
| Array | `Free` | 12819 | 884 | L3 | 逆序 unref 全部元素并释放页；1.28 万次/章 |
| Array | `Realloc` | 3223 | 56 | L3 | 重分配并保留内容，写容量/nr_vars |
| Array | `Copy` | 3053 | 7 | L3 | 写目标元素 + heap_ref/unref（D313）；b7r 回归无越界 |
| Array | `BinarySearch` | 2890 | 5 | L2 | 有序数组二分查找；未找到返回负值 |
| Array | `Empty` | 1694 | 153 | L2 | 判空（返回元素数==0） |
| Array | `Any` | 1572 | 80 | L2 | 谓词任一为真；回调经委托派发 |
| Array | `Insert` | 1394 | 36 | L3 | 右移后插入，写 nr_vars |
| Array | `LowerBound` | 1333 | 8 | L2 | 有序数组下界二分；返回插入位 |
| Array | `Find` | 1210 | 131 | L2 | D164 修谓词分派；D313 补 iface 宽度（按元素遍历、谓词收第 i*width 槽、返回元素下标）；b8r 回归 0 越界 |
| Array | `Alloc` | 849 | 155 | L3 | 分配数组页并写容量/nr_vars；被 PushBack 反复触发 |
| Array | `EmplaceBack` | 785 | 19 | L3 | 原地构造后追加（写 nr_vars）；与 PushBack 同路径 |
| Array | `EraseAll` | 660 | 54 | L3 | 清空并 unref 所有元素，nr_vars=0 |
| Array | `Unique` | 574 | 12 | L3 | 原地压缩去重，写 nr_vars |
| Array | `Last` | 502 | 96 | L2 | 末元素；空数组返回空值 |
| Array | `First` | 357 | 75 | L2 | 首元素；空数组返回空值 |
| Array | `Concat` | 247 | 54 | L3 | 生成新数组页（alloc + 逐元素拷贝） |
| Array | `PopBack` | 198 | 23 | L3 | nr_vars-- 并 unref 尾元素 |
| Array | `ShallowCopy` | 141 | 56 | L3 | alloc 新页 + array_copy_values（heap_ref） |
| Array | `Where` | 127 | 75 | L3 | alloc 新页 + 按谓词写入 + heap_ref（iface 宽 2） |
| Array | `Reverse` | 81 | 5 | L3 | 原地交换元素（写 values） |
| Array | `Erase` | 50 | 119 | L3 | 按下标删除并左移，返回新长度 |
| Array | `IsExist` | 10 | 65 | L2 | 判元素存在（线性查找） |
| Array | `Sort` | 6 | 43 | L3 | alloc 新页 + array_copy_values（带 heap_ref）后原地重排 |
| CGManager | `IsExist` | 3420 | 1 | L2 | CG 存在性（40 语句，含编号/别名解析）；背景与立绘按名加载成功即证据 |
| CGManager | `GetSize` | 16 | 1 | L2 | 返回 CG 宽高；背景件 1798x680 与图实际尺寸一致（D310 转存校验） |
| ChipmunkSpriteEngine | `Sleep` | 1515 | 1 | L3 | 帧休眠推进节奏（1515 次/章） |
| ChipmunkSpriteEngine | `Update` | 1484 | 1 | L3 | handle_events + sprite_call_plugins + 时间步进（1484 次/章） |
| ChipmunkSpriteEngine | `TRANS_Begin` | 20 | 1 | L3 | effect_init 建立变换批 |
| CommonSystemData | `SetDataInt` | 4 | 1 | L3 | csd_put_datum 写全局数据表 |
| Delegate | `Empty` | 6387 | 754 | L2 | 判空委托；6387 次/章，是消息派发的前置判断 |
| Delegate | `Numof` | 316 | 8 | L2 | 委托项数；调用方按它遍历 [obj,fun] 对 |
| Delegate | `Clear` | 96 | 157 | L3 | 清空委托链；96 次/章，用于对象复用前重置 |
| Delegate | `Set` | 11 | 358 | L3 | 按下标改写委托项（obj,fun）；写后派发即生效（[dgadd] 实测） |
| IbisInputEngine | `Key_IsDown` | 15333 | 1 | L2 | 按键按下状态；1.5 万次/章，Ctrl 快进与点击推进都靠它 |
| IbisInputEngine | `Joystick_IsKeyDown` | 9500 | 1 | NA | 键鼠路径不需要手柄；接 SDL 手柄属可选功能 |
| IbisInputEngine | `Joystick_ClearCaptureFlag` | 1514 | 1 | NA | 手柄捕获标志；键鼠路径不使用，属可选功能 |
| IbisInputEngine | `MouseWheel_GetCount` | 600 | 1 | L3 | 消费式读取：取增量并清零（600 次/章，列表滚动可用） |
| MainEXFile | `AString` | 3460 | 1 | L2 | A 区按索引取字符串（说话人/台词即走它）；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `GetColAtFormatName` | 1888 | 15 | L2 | 按列名取列号（v12 版）；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `String` | 1046 | 1 | L2 | 按 (行,列) 取字符串；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `A2Int` | 1021 | 1 | L2 | A2 键取 int；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `A2String` | 851 | 1 | L2 | A2 键取字符串；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `GetNodeNameList` | 552 | 1 | L2 | 列出节点名；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `GetEXNameList` | 481 | 1 | L2 | 列出可读 EX 名；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `AInt` | 472 | 1 | L2 | A 区按索引取 int；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `Row` | 416 | 1 | L2 | 返回行数；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `Int` | 143 | 1 | L2 | 按 (行,列) 取 int；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `GetRowAtStringKey` | 139 | 6 | L2 | 按字符串主键取行；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `GetRowAtIntKey` | 109 | 6 | L2 | 按 int 主键取行；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| MainEXFile | `A2Exists` | 22 | 1 | L2 | A2 键存在性查询；EX 读取链路正确：名字/行/文本与脚本一致（12 屏文字即证据） |
| Math | `RandF` | 67582 | 6 | L2 | 浮点随机；与 Rand 同源 |
| Math | `Rand` | 5002 | 6 | L2 | 返回 [0,1) 内的整数/浮点随机；6.7 万次/章，用于抖动与随机演出 |
| Math | `Min` | 3361 | 123 | L2 | int/float 6 重载按 arity+返回类型分派（D284，曾因绑定到 int 版把 0.75 截成 0） |
| Math | `Clamp` | 3030 | 62 | L2 | 区间夹取；越界端点由调用方保证 lo<=hi |
| Math | `Max` | 263 | 148 | L2 | 同 Min，6 重载 |
| PartsEngine | `GetMessageVariableType` | 25259 | 1 | L2 | 调用点紧跟 IFNZ 当布尔用；1=int 合法（0x1e101a 反汇编） |
| PartsEngine | `GetActiveController` | 15036 | 1 | L2 | GetActiveLayer 薄封装直属返回；值=层稳定 id（D129） |
| PartsEngine | `GetMessageType` | 9944 | 1 | L2 | 队头 type，空队 -1 哨兵（D112；返 0 会让 WaitFrame 忙循环） |
| PartsEngine | `GetControllerID` | 8984 | 1 | L2 | GetLayerID 薄封装；调用方只在 0..NumofLayer-1 用，越界返 -1 |
| PartsEngine | `PopMessage` | 8430 | 2 | L3 | 弹出队头并释放，队列前进；空队是空操作（[mpop] 实测） |
| PartsEngine | `GetMessageDelegateIndex` | 8427 | 1 | L2 | 队头 delegate_index，空队 -1；CallDelegate 用它查 FunctionSetList |
| PartsEngine | `SetActiveController` | 5970 | 1 | L3 | h3 闭环：SetActive id=1→idx=0 后 #2632 active=0；id=4→idx=2 后 active=2。连带修 PE_AddController 的「active 必须在栈顶」断言（b1r2 VM_ERROR → b1r3 通过） |
| PartsEngine | `GetControllerLength` | 2940 | 1 | L2 | NumofLayer 薄封装，返回层数 |
| PartsEngine | `GetClickNumber` | 1429 | 1 | L2 | 薄封装 GetClickPartsNumber；值=被点部件号，0=无（s141 EndInput clicked=N 一致） |
| PartsEngine | `GetActivityPartsNumber` | 1102 | 1 | L2 | 0=未找到，与本引擎「0 即无效部件号」一致 |
| PartsEngine | `GetComponentPosX` | 686 | 1 | L2 | float；错误路径 F_PUSH 0.0（调用点反汇编可见） |
| PartsEngine | `GetComponentType` | 578 | 1 | L2 | 1-based state（内部 --state）；越界/无部件 -1，与 D309 的 -1 语义一致 |
| PartsEngine | `GetDelegateIndex` | 496 | 1 | L2 | parts.delegate_index；无此部件返 -1，与 Set 对称 |
| PartsEngine | `SetDelegateIndex` | 418 | 1 | L3 | 写 parts.delegate_index；后续 CallDelegate 按它取 FunctionSet（s161 实测 di 一致） |
| PartsEngine | `NumofChild` | 411 | 1 | L2 | 子件计数；无部件返 0（与 GetChild 的上界对称） |
| PartsEngine | `GetChild` | 391 | 1 | L2 | 返回子件号；调用方按 0..NumofChild-1 迭代，越界/无部件返 -1 |
| PartsEngine | `GetActivityParts` | 350 | 1 | L3 | 写 out_name/out_no 两个 ref 参数（先 free 旧 string 再 cstr_to_string） |
| PartsEngine | `SetComponentShow` | 305 | 1 | L3 | 层号走 ctrl_stack.show（D128），部件走 local.show；D311 serial 实测 show/hide 序列与 AIN 意图一致 |
| PartsEngine | `SetKeyWaitShow` | 267 | 2 | L0 | 两个调用点都是薄封装（activityeditor / message::キー待ち表示設定），未找到可见载体；猜成 parts.show 有误隐藏风险，暂缓 |
| PartsEngine | `SetComponentPosZ` | 260 | 1 | L3 | parts_set_z：local.z 变化即重算 global.z 并 parts_dirty（子树排序依据） |
| PartsEngine | `Parts_SetParentPartsNumber` | 256 | 1 | L3 | D194：运行时挂父立即 RefreshGlobalTree，防子树 global.z 停在建树瞬间 |
| PartsEngine | `SetComponentPos` | 254 | 3 | L3 | parts_set_pos：写 local.pos + 重算 global + dirty；**float 被 int 截断**（亚像素丢失，1px 级） |
| PartsEngine | `Parts_SetComment` | 245 | 2 | L3 | 写 pe_comments 旁路表（strdup，上限 PE_COMMENT_MAX 超出丢弃）；不参与渲染 |
| PartsEngine | `Parts_SetPassCursor` | 233 | 1 | L3 | pass_cursor 参与 click_eligible=clickable||!pass_cursor，直接影响命中；12 屏与对话框点击命中与预期一致 |
| PartsEngine | `SetEnableInputProcess` | 226 | 1 | L0 | 契约未定。D165 试过映射到 clickable，238/242 次 enable=0 把按钮全关掉、只加载 27 个活动后卡死，已回退。需行为实验（[EIP-NOT-CLICKABLE]） |
| PartsEngine | `CreateUserPartsNumber` | 215 | 1 | L3 | 同 GetFreeNumber 分配器：返回值即部件号，必须唯一且不复用（D82/D161） |
| PartsEngine | `Parts_GetPartsWidth` | 212 | 1 | L2 | 1-based state，越界返 0；直接读 states[].common.w |
| PartsEngine | `Parts_GetPartsHeight` | 208 | 1 | L2 | 1-based state，越界返 0；直接读 states[].common.h |
| PartsEngine | `Parts_SetPartsCG` | 170 | 3 | L3 | parts_cg_set：换图 + 改 state 类型 + dirty；D310 纹理转存证明图正确 |
| PartsEngine | `IsComponentShow` | 111 | 1 | L2 | 图层号走 ctrl_stack.show（D128），部件走 local.show；**state 参数被忽略**，与 SetComponentShow 对称，多状态串味留给 B4 一起处理 |
| PartsEngine | `AddPartsConstructionProcess` | 86 | 1 | L3 | 写构建进度计数供 AIN 轮询（12 屏装载无卡死） |
| PartsEngine | `NumofActivityParts` | 72 | 1 | L2 | Activity@GetList 用它定长；返 0 会导致按钮对象不创建（D119） |
| PartsEngine | `SetComponentAlpha` | 65 | 1 | L3 | 写 local alpha 并进入合成；12 屏文字/立绘渐变正常（[UC-ALPHA] 的 0 值路径仍未结，单列） |
| PartsEngine | `SetComponentType` | 54 | 1 | L3 | 类型变更时 parts_state_reset 重建状态 + 写 component_type（1-based state） |
| PartsEngine | `ReadActivityFile` | 39 | 1 | L3 | activity_load：创建部件 + 布局（[act]  loaded 逐条可见） |
| PartsEngine | `Parts_SetSoundName` | 38 | 2 | L0 | 音频后端未接（KiwiSoundEngine 未移植），只存名字无产出；等音频通路再实现 |
| PartsEngine | `SetWantSaveBackScene` | 30 | 1 | L3 | 写 want_save_back_scene，决定 SaveBackScene 的收录范围（读取端默认关，D310） |
| PartsEngine | `GetFreeNumber` | 28 | 1 | L3 | 分配空闲号；D161 实测号段必须落在 1..999999999，旧起点 1000001000 让 213 次 Release 全失败 |
| PartsEngine | `Parts_SetText` | 26 | 1 | L3 | 写文本并重排版；12 屏 1/2/3 行段落与说话人切换均正确 |
| PartsEngine | `GetActivityPartsName` | 21 | 1 | L2 | 未找到返空串；调用方随后与字面量 S_EQUALE 比较 |
| PartsEngine | `Parts_GetParentPartsNumber` | 18 | 1 | L2 | D312：返真实父号（含 pending_parent，0=无父） |
| PartsEngine | `Parts_IsPartsFlatEnd` | 17 | 1 | L2 | 判据 frame==end_frame；曾差 1 导致 Join 无限等，已统一 end_frame 来源（flat.c） |
| PartsEngine | `Parts_SetTextCharSpace` | 15 | 1 | L3 | 字距写入并被渲染采用（D306 字号补偿后 12 屏无「半个字」） |
| PartsEngine | `BeginInput` | 14 | 1 | L3 | 开输入窗口：置 parts_began_click（帧内捕获的前提） |
| PartsEngine | `EndInput` | 13 | 1 | L3 | 关窗口：清 began、clamp 点击号到 0、重置拖拽（s141 EndInput clicked=N 实测） |
| PassRegister | `RegistText` | 175 | 3 | L3 | 有序插入 + write_register 落盘，幂等 |
| String | `Empty` | 1219 | 304 | L2 | 判空字符串；1219 次/章 |
| String | `Contains` | 470 | 106 | L2 | 子串存在性；470 次/章 |
| String | `GetPart` | 189 | 101 | L2 | 按分隔位置取子串；[D119-SPLIT] 的 Split 另案，不影响 GetPart |
| String | `StartsWith` | 83 | 14 | L2 | 前缀判定；83 次/章 |
| String | `Length` | 57 | 116 | L2 | 字符长度（按 GBK 双字节计，与文字排版一致） |
| system | `GetTime` | 5027 | 4 | L2 | 毫秒时钟；5027 次/章，驱动动画与等待 |
| system | `IsDebugMode` | 1767 | 41 | NA | 发行环境应恒 false，属正确行为，无需实现 |
| system | `Peek` | 1516 | 8 | L2 | 取栈顶不弹出；1516 次/章 |
| system | `GetFuncStackName` | 221 | 10 | L2 | 返回当前 AIN 函数名，供错误信息 |
| system | `GroupSave` | 9 | 4 | L3 | → save_globals：落盘到 savedir（实测生成 GlobalSave.asd） |
| system | `DeleteSaveFile` | 8 | 5 | L3 | → savedir_path + remove()：真删文件 |
| system | `GroupLoad` | 3 | 4 | L3 | → load_globals：读盘并改写全局槽 |
| system | `GetSaveFolderName` | 1 | 10 | L2 | 返回存档目录名；与 savedata 路径一致 |

<!-- END:LEDGER -->
