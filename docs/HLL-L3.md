# 第一章 HLL 全量达标计划（L2 / L3）

> 目标不是「把 4495 个都写一遍」，而是**把第一章真正调用的 121 个，逐个做到有语义证据**。
> 「解决」的定义见 `docs/GUIDE/03-semantics.md` §3.7，此处只引用不重复。

---

## 0. 完成定义

| 类型 | 达标级别 | 判据 |
|---|---|---|
| getter（纯查询） | **L2** | 调用点反汇编读出的语义 ＝ 实现返回的语义，且有一处场景或格式证据 |
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
| 批次进度 | B1（层与活动，8 个）已完成：L2×7、L3×1，无需改代码 |

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
| L0 | 107 | 注册 |
| L2 | 10 | 单点语义 |
| L3 | 2 | 副作用联动 |
| NA | 2 | 豁免 |
| **合计** | **121** | 本章路径调用的 HLL |

| 库 | 函数 | 调用次数 | 调用点 | 级 | 契约 / 证据 |
|---|---|---:|---:|:--:|---|
| AFAFactory | `SearchTitle` | 172 | 3 | L0 |  |
| AFAFactory | `LoadArchive` | 12 | 3 | L0 |  |
| AnteaterADVLogList | `AddText` | 7 | 2 | L0 |  |
| AnteaterADVLogList | `AddNewPage` | 5 | 2 | L0 |  |
| Array | `Numof` | 150397 | 1822 | L0 |  |
| Array | `PushBack` | 126870 | 1392 | L0 |  |
| Array | `At` | 13034 | 242 | L0 |  |
| Array | `Free` | 12819 | 884 | L0 |  |
| Array | `Realloc` | 3223 | 56 | L0 |  |
| Array | `Copy` | 3053 | 7 | L0 |  |
| Array | `BinarySearch` | 2890 | 5 | L0 |  |
| Array | `Empty` | 1694 | 153 | L0 |  |
| Array | `Any` | 1572 | 80 | L0 |  |
| Array | `Insert` | 1394 | 36 | L0 |  |
| Array | `LowerBound` | 1333 | 8 | L0 |  |
| Array | `Find` | 1210 | 131 | L0 |  |
| Array | `Alloc` | 849 | 155 | L0 |  |
| Array | `EmplaceBack` | 785 | 19 | L0 |  |
| Array | `EraseAll` | 660 | 54 | L0 |  |
| Array | `Unique` | 574 | 12 | L0 |  |
| Array | `Last` | 502 | 96 | L0 |  |
| Array | `First` | 357 | 75 | L0 |  |
| Array | `Concat` | 247 | 54 | L0 |  |
| Array | `PopBack` | 198 | 23 | L0 |  |
| Array | `ShallowCopy` | 141 | 56 | L0 |  |
| Array | `Where` | 127 | 75 | L0 |  |
| Array | `Reverse` | 81 | 5 | L0 |  |
| Array | `Erase` | 50 | 119 | L0 |  |
| Array | `IsExist` | 10 | 65 | L0 |  |
| Array | `Sort` | 6 | 43 | L0 |  |
| CGManager | `IsExist` | 3420 | 1 | L0 |  |
| CGManager | `GetSize` | 16 | 1 | L0 |  |
| ChipmunkSpriteEngine | `Sleep` | 1515 | 1 | L0 |  |
| ChipmunkSpriteEngine | `Update` | 1484 | 1 | L0 |  |
| ChipmunkSpriteEngine | `TRANS_Begin` | 20 | 1 | L0 |  |
| CommonSystemData | `SetDataInt` | 4 | 1 | L0 |  |
| Delegate | `Empty` | 6387 | 754 | L0 |  |
| Delegate | `Numof` | 316 | 8 | L0 |  |
| Delegate | `Clear` | 96 | 157 | L0 |  |
| Delegate | `Set` | 11 | 358 | L0 |  |
| IbisInputEngine | `Key_IsDown` | 15333 | 1 | L0 |  |
| IbisInputEngine | `Joystick_IsKeyDown` | 9500 | 1 | NA | 键鼠路径不需要手柄；接 SDL 手柄属可选功能 |
| IbisInputEngine | `Joystick_ClearCaptureFlag` | 1514 | 1 | L0 |  |
| IbisInputEngine | `MouseWheel_GetCount` | 600 | 1 | L0 |  |
| MainEXFile | `AString` | 3460 | 1 | L0 |  |
| MainEXFile | `GetColAtFormatName` | 1888 | 15 | L0 |  |
| MainEXFile | `String` | 1046 | 1 | L0 |  |
| MainEXFile | `A2Int` | 1021 | 1 | L0 |  |
| MainEXFile | `A2String` | 851 | 1 | L0 |  |
| MainEXFile | `GetNodeNameList` | 552 | 1 | L0 |  |
| MainEXFile | `GetEXNameList` | 481 | 1 | L0 |  |
| MainEXFile | `AInt` | 472 | 1 | L0 |  |
| MainEXFile | `Row` | 416 | 1 | L0 |  |
| MainEXFile | `Int` | 143 | 1 | L0 |  |
| MainEXFile | `GetRowAtStringKey` | 139 | 6 | L0 |  |
| MainEXFile | `GetRowAtIntKey` | 109 | 6 | L0 |  |
| MainEXFile | `A2Exists` | 22 | 1 | L0 |  |
| Math | `RandF` | 67582 | 6 | L0 |  |
| Math | `Rand` | 5002 | 6 | L0 |  |
| Math | `Min` | 3361 | 123 | L0 |  |
| Math | `Clamp` | 3030 | 62 | L0 |  |
| Math | `Max` | 263 | 148 | L0 |  |
| PartsEngine | `GetMessageVariableType` | 25259 | 1 | L0 |  |
| PartsEngine | `GetActiveController` | 15036 | 1 | L2 | GetActiveLayer 薄封装直属返回；值=层稳定 id（D129） |
| PartsEngine | `GetMessageType` | 9944 | 1 | L0 |  |
| PartsEngine | `GetControllerID` | 8984 | 1 | L2 | GetLayerID 薄封装；调用方只在 0..NumofLayer-1 用，越界返 -1 |
| PartsEngine | `PopMessage` | 8430 | 2 | L0 |  |
| PartsEngine | `GetMessageDelegateIndex` | 8427 | 1 | L0 |  |
| PartsEngine | `SetActiveController` | 5970 | 1 | L3 | h3 闭环：SetActive id=1→idx=0 后 #2632 active=0；id=4→idx=2 后 active=2。连带修 PE_AddController 的「active 必须在栈顶」断言（b1r2 VM_ERROR → b1r3 通过） |
| PartsEngine | `GetControllerLength` | 2940 | 1 | L2 | NumofLayer 薄封装，返回层数 |
| PartsEngine | `GetClickNumber` | 1429 | 1 | L2 | s141：EndInput clicked=N 与 GetClickNumber 返回值一致 |
| PartsEngine | `GetActivityPartsNumber` | 1102 | 1 | L2 | 0=未找到，与本引擎「0 即无效部件号」一致 |
| PartsEngine | `GetComponentPosX` | 686 | 1 | L0 |  |
| PartsEngine | `GetComponentType` | 578 | 1 | L0 |  |
| PartsEngine | `GetDelegateIndex` | 496 | 1 | L0 |  |
| PartsEngine | `SetDelegateIndex` | 418 | 1 | L0 |  |
| PartsEngine | `NumofChild` | 411 | 1 | L0 |  |
| PartsEngine | `GetChild` | 391 | 1 | L0 |  |
| PartsEngine | `GetActivityParts` | 350 | 1 | L2 | 调用点紧跟 IFZ 当布尔用；miss 必须返 0，否则跳错误分支 |
| PartsEngine | `SetComponentShow` | 305 | 1 | L0 |  |
| PartsEngine | `SetKeyWaitShow` | 267 | 2 | L0 |  |
| PartsEngine | `SetComponentPosZ` | 260 | 1 | L0 |  |
| PartsEngine | `Parts_SetParentPartsNumber` | 256 | 1 | L3 | D194：运行时挂父立即 RefreshGlobalTree，防子树 z 停在建树瞬间 |
| PartsEngine | `SetComponentPos` | 254 | 3 | L0 |  |
| PartsEngine | `Parts_SetComment` | 245 | 2 | L0 |  |
| PartsEngine | `Parts_SetPassCursor` | 233 | 1 | L0 |  |
| PartsEngine | `SetEnableInputProcess` | 226 | 1 | L0 |  |
| PartsEngine | `CreateUserPartsNumber` | 215 | 1 | L0 |  |
| PartsEngine | `Parts_GetPartsWidth` | 212 | 1 | L0 |  |
| PartsEngine | `Parts_GetPartsHeight` | 208 | 1 | L0 |  |
| PartsEngine | `Parts_SetPartsCG` | 170 | 3 | L2 | D310：纹理转存证明挂图正确（1798x680 即目标图） |
| PartsEngine | `IsComponentShow` | 111 | 1 | L0 |  |
| PartsEngine | `AddPartsConstructionProcess` | 86 | 1 | L0 |  |
| PartsEngine | `NumofActivityParts` | 72 | 1 | L2 | Activity@GetList 用它定长；返 0 会导致按钮对象不创建（D119） |
| PartsEngine | `SetComponentAlpha` | 65 | 1 | L0 |  |
| PartsEngine | `SetComponentType` | 54 | 1 | L0 |  |
| PartsEngine | `ReadActivityFile` | 39 | 1 | L0 |  |
| PartsEngine | `Parts_SetSoundName` | 38 | 2 | L0 |  |
| PartsEngine | `SetWantSaveBackScene` | 30 | 1 | L0 |  |
| PartsEngine | `GetFreeNumber` | 28 | 1 | L0 |  |
| PartsEngine | `Parts_SetText` | 26 | 1 | L0 |  |
| PartsEngine | `GetActivityPartsName` | 21 | 1 | L2 | 未找到返空串；调用方随后与字面量 S_EQUALE 比较 |
| PartsEngine | `Parts_GetParentPartsNumber` | 18 | 1 | L2 | D312：返真实父号（含 pending_parent，0=无父） |
| PartsEngine | `Parts_IsPartsFlatEnd` | 17 | 1 | L0 |  |
| PartsEngine | `Parts_SetTextCharSpace` | 15 | 1 | L0 |  |
| PartsEngine | `BeginInput` | 14 | 1 | L0 |  |
| PartsEngine | `EndInput` | 13 | 1 | L0 |  |
| PassRegister | `RegistText` | 175 | 3 | L0 |  |
| String | `Empty` | 1219 | 304 | L0 |  |
| String | `Contains` | 470 | 106 | L0 |  |
| String | `GetPart` | 189 | 101 | L0 |  |
| String | `StartsWith` | 83 | 14 | L0 |  |
| String | `Length` | 57 | 116 | L0 |  |
| system | `GetTime` | 5027 | 4 | L0 |  |
| system | `IsDebugMode` | 1767 | 41 | NA | 发行环境应恒 false，属正确行为，无需实现 |
| system | `Peek` | 1516 | 8 | L0 |  |
| system | `GetFuncStackName` | 221 | 10 | L0 |  |
| system | `GroupSave` | 9 | 4 | L0 |  |
| system | `DeleteSaveFile` | 8 | 5 | L0 |  |
| system | `GroupLoad` | 3 | 4 | L0 |  |
| system | `GetSaveFolderName` | 1 | 10 | L0 |  |

<!-- END:LEDGER -->
