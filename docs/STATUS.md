# 兰斯10 · Linux 移植

把 System4 / AIN v12 的《ランス10》跑在 GNU/Linux 上。核心工作不是改游戏，而是补齐
`xsystem4` 这个 C 解释器缺失的**字节码语义**与 **HLL 绑定**。
代码 `Rance10_Port/xsystem4/`，游戏数据 `Rance10_Port/game/`。

---

## 0. 现在在哪（每次会话先读这一屏）

| | |
|---|---|
| **阶段** | **主线 `main`（SDL3 后端）第一章可通到第一场战斗**：标题 → 第一章 ADV（37 句）→ 回想对话 `ReplayMemoDialog`（D337 修好「過後查看」）→ `QuestMapSelectionButton` 选项 → `Game/Battle/*` **战斗画面出图**（Round 1、HP、技能按钮、戦斗结果ルート）。战斗尚未打透 |
| **最大卡点** | `[BATTLE-STALL]` 战斗界面已出、可点，但一回合推进不下去：点击派发（D337）已通，问题在战斗侧自己的点击捕获/技能按钮命中，见台账 A 组 |
| **本轮做过的** | **D337 修掉 `[TEXTBUTTON-CLICK]`**：反汇编证明点击委托存在 AIN `Parts` 对象字段 7 上（`Parts@SetOnClickEvent`），派发者是 `Parts@OnClick`(29541)，而它在静态 xref 里**一个 AIN 调用点都没有** ⇒ 这一拍本该由引擎回调、移植时整条漏掉了。新增 `vm_request_parts_click()`/`vm_pump_parts_click()`：点击登记、帧泵派发；`TextButton` 走它 `Init` 里生成的 lambda；精确匹配不到时按「点在容器上」取容器内 TextButton（实测点 574 是容器、575 才是按钮）。`XSYS4_PARTS_EVENT=0` 可关 |
| **判据** | 画面一律用引擎自截图（`XSYS4_SHOT_DIR` + Alt+S，且 alt+s 前空 ≥1.5s，否则会丢帧）；层/归属看 `[ctrl] SetActive id=N -> idx=M` 与 `[ctrl] layer … parts:` 清单；HLL 进展看 `python3 tools/offline/hll_hotness.py --todo ../logs/live_<tag>.log` 的输出是否为 0 |

> **卡点必须挂债标签**：§0「最大卡点」里的 `` `[TAG]` `` 必须**逐字存在**于
> `docs/TECH_DEBT.md` 的未结区（A/B/C 组），否则 `gen-doc-numbers.py --check` 报 STALE。
> 这条是为了治「卡点过期」这类误判——状态文档声称卡在 X，而 X 早已在台账里清零，
> 于是后续会话照着过期的卡点白跑几轮（D264–D269 就是这么发生的）。
> 卡点换方向时，**先改台账再改这一行**，别只改这里。

基线四数**以 §4 生成块与 `docs/STATS.md` 为准**（禁止手写）；D214 默认零开关跑测与 D207 逐项一致、无回归。`check_core_deps.py` PASS。

编队「戻る／決定」是**右键**（不是空按钮 `PartyModeButton`）；`Party._Leaders` 是 7 元素数组（D166），改内容不改槽号。本文件只放**结论**：过程在 `archive/`，方法论在 `docs/GUIDE/`。

---

## 1. 快速开始

```sh
cd Rance10_Port/xsystem4
env -u PYTHONPATH -u PYTHONHOME -u PYTHONDONTWRITEBYTECODE ninja -C build   # 构建
python3 tools/check_core_deps.py      # 必须 RESULT: PASS
```

不摘 `PYTHONHOME`/`PYTHONPATH` 时 meson 会死在 `ModuleNotFoundError: encodings`。

**跑测只有一条入口**（不许在真实桌面跑）：

```sh
cd Rance10_Port
# <tag> <秒> <截图路径>
RANCE10_LIVE_SEQ="wait:16000;click:1819,1002;wait:7000" \
  xsystem4/tools/run/run-headless.sh tag 85 /tmp/tag.png
# 直接进第一章地图（跳过点标题）
XSYS4_AUTO_MAP=1 xsystem4/tools/run/run-headless.sh map 60 /tmp/map.png
```

`run-headless.sh` 自起独立 Xvfb（`:90..:120`）+ `MemoryMax=3G`；`run-live.sh` 拒绝在真实桌面运行。

**坐标（视图坐标，脚本自动按窗口实际几何换算）**：

| 位置 | 坐标 |
|---|---|
| 网络确认 OK / 标题 Chapter1 | `963,817` / **`646,738`**（EX 声明坐标；D207 的 `100,60` 是布局盒被错误 VERTICAL 重排后的产物，D214 已修，见 `docs/HYPOTHESES.md` H018/D214） |
| 地图右下「部队编制」 | `1819,1002`（部件 1721,982，196×40） |
| 对话框推进 / 关闭 | `960,540` / `300,300` |
| 選択肢按钮（D193 实测） | `1600,450`（部件 `900000308`，`428×170`，`global=(1438,362)`） |
| 回想对话框两个按钮（D238 `PARTS_DUMP`） | 「现在查看」`(736,880) 196×40`｜「过后查看」`(984,880) 196×40` —— 点 `1077,895`；`960,540` 只会命中 710×710 的大 CG |

**官方操作（`game/Manual/r01.html` 按键表，图 `img/rm01.gif`）**：Enter/↓ 下一段文本、
**按住 Ctrl 强制快进（不论是否已读）**、`A` AUTO、`S` SKIP、Esc 设置、Home/PageUp 回顾、
右键关对话框。SKIP 只跳已读，首次跑剧情必须用 Ctrl。

Ctrl 快进是长剧情跑测唯一实用入口：`０２／ＳＴＡＲＴ`(32194) 只有 **53 条 MSG**
（128960–129012；旧记「221 条」是把后续函数一起数了，D214 纠正）。按住 Ctrl
60 秒可跑完开场剧情并走到回想对话框；逐点推进需十几分钟。

```sh
RANCE10_CLICK_SLEEP=0.3 XSYS4_AUTO_MAP=1 \
  RANCE10_LIVE_SEQ="wait:15000;keydown:Control_L;wait:60000;keyup:Control_L" \
  xsystem4/tools/run/run-headless.sh ctrlskip 100 /tmp/ctrlskip.png
```

`keydown:`/`keyup:` 必须成对（AIN 用 `key_is_down(VK_CONTROL)` 检测按住状态，短促 `key:` 无效）；
`RANCE10_CLICK_SLEEP` 控制这两个动作的停顿（默认 1.2 不变）。

序列语法：`wait:<ms>` `click:<x,y>` `rclick:<x,y>` `move:<x,y>` `key:<keysym>` `keydown:<keysym>` `keyup:<keysym>` `until:<日志子串>:<超时ms>` `clickunless:<x,y>:<日志子串>`。
**别用固定 sleep 等场景切换**，用 `until:`。

**离线工具**（只读 AIN/存档，优先用它们，不必起游戏）：

```sh
cd Rance10_Port
Rance10_Port/xsystem4/tools/bin/ain_at game/Rance10.ain findsym:Chapter1@Run   # 函数号/地址
Rance10_Port/xsystem4/tools/bin/ain_at game/Rance10.ain fno:23612              # 反汇编 + 局部变量
Rance10_Port/xsystem4/tools/bin/ain_at game/Rance10.ain atf:0x375098           # 地址属于谁
tools/bin/ain_xref game/Rance10.ain call 23595           # 谁调了它
tools/bin/gsave_dump logs/savedir/<tag>/GlobalSave.asd   # 存档能否读回
Rance10_Port/xsystem4/tools/offline/alice.sh ain dump --function "X@Y" -o f.x game/Rance10.ain   # 带符号反汇编
Rance10_Port/xsystem4/tools/offline/alice.sh ex dump -o x.x /tmp/a.pactex                        # EX 全字段
```

`tools/offline/alice.sh` 是外部 alice-tools 的包装（自动补中文版必需的
`--input-encoding gbk --output-encoding utf-8`）；分工见 `docs/OFFLINE.md`。

**两套现成夹具**（都在 `Rance10_Port/game/`）：

- `全CG周目继承存档/rance10/*.asd` —— PC 原版通关档（gsave **v8**）。
  用法：`cp -a` 到 `logs/savedir/<tag>/` 后加 `XSYS4_SAVE_GROUP_ANY=1`；
  判据 `gsave_dump` 报 `OK`（359 全局 / 349 数组 / 12813 记录）。
- `CE数据表/Rance10.CT` —— 原版 cheat 表，14 条构成一块 15 项 int32 战斗参数区。
  **裸地址是 Windows 进程快照，不能用于本 VM**；要做 cheat 得改 AIN 全局或离线编辑 `.asd`。


---

## 2. 现状

启动链 / 活动子系统 / 场景栈 / 帧循环、标题界面（D131/D141）、第一章任务地图、
任务信息面板（D142/D143/D149）、部队编成完整渲染（D177，0 内部错误 / 0 double free）、
存档 gsave 写读 + 原版 v8 读入（`AFConfig.asd` 34 全局）、剧情 ADV 与第一章战斗
（D214/D215）、ADV 正文渲染（D216/D217）、VS 对阵与战斗 UI（D224）均通。
第一章链路 + 画面全验收完成，无已知卡点；剩余 H025 观感项与图鉴，
下一阶段首个卡点 H038（战斗回合切换淡出层）见 §0。
详见 §0 与 `TECH_DEBT.md`。

---

## 3. 必读契约（改 VM 前）

按"错一处炸一片"排序。只写规则与后果，取证在 `docs/GUIDE/`。

1. **`AIN_IFACE(89)` 占两槽** `[object, vtable offset]`，object 在低槽；`nr_args` 只数逻辑参数。
   弹参/填参/栈重排必须按槽数走 `function_argument_slots()`（见 27/28）。
2. **`AIN_WRAP(82)` 的 `postset` 例外**：声明 `vars=[WRAP, VOID, 临时…]、nr_args=2`，
   这个 2 已含 WRAP 第二槽。**跳过 void 时必须同时 `args++`**，否则多吃一槽、弹掉
   `[obj,fno]`，`this` 变成栈上残留值。判据：`XSYS4_MC_SLOT_DBG` 里 `nargs != slots`。
3. **泛型 `array` 由首个元素定型**（`array_settle_type()`）。默认成 `AIN_ARRAY_INT` 会让
   字符串元素不参与引用计数，`DELETE` 后留悬空槽号。
4. **HLL 返回槽宽由 CALLHLL 第三立即数决定**，不是声明类型；未解析到元素也要压同样多槽。
5. **HLL 按名字绑定**（`link_static_library`），不是按位置；同名重载（`Array.First/Any`）
   是否带谓词要查**被调函数声明**。
6. **组件类型号以游戏自身 EX 为准**：`0=按钮 8=布局BOX 15=用户组件 17=CG 19=文本
   26=flat 28=动画`。照抄 upstream 会得到 SpinBox。
7. **flat v≥15** 的图形键是 `graphic.frames[f].keys[i]`（每 SWF 图层一个）；v<15 才是
   `count + keys[]`。只写 v<15 → 全部 UI 不画。
8. **编码**：游戏是 GBK；`.pactex` 条目名是 GBK 字节；flat 变体用**全角 `／`**。
9. **0 号 heap 槽是保留 GLOBAL_PAGE**（`PUSHGLOBALPAGE` 压字面量 0）。页类型
   `GLOBAL=0 LOCAL=1 STRUCT=2 ARRAY=3 DELEGATE=4 IFACE=5`。
10. **`DELEGATE_PAGE` 每 3 槽一条 `[obj, fun, seq]`**，有效性 = `heap_get_seq(obj)==seq`；
    只能走 `delegate_append/clear`，不能自造结构。
11. **v12 版本分派坑**：`S_MOD` / `OBJSWAP` / `DG_STR_TO_METHOD`（v≥11 带立即数）/
    `SR_ASSIGN`（数组元素三值形式）。
12. **动画时钟是帧心跳的一部分**：`flat/animation/flash` 的帧号只在
    `parts_advance_animation()` 里推进；纯 AIN 自旋时不推进 → "有部件、无变化"。
13. **纯 AIN 等待循环必须由帧心跳驱动**：`WaitForClick` 自旋回不到 `sact_Update()`，
    既无 `handle_events()` 也无 `scene_render()`。`vm_frame_pump()`（每 4096 指令，
    `XSYS4_NO_PUMP=1` 关）解决；命中测试与菜单消息另需 `PE_PumpInput()`。
14. **`SceneStack@Join`(29656) 是「每点一次刷新一帧」的循环**：
    `init; loop { if (IsFinish()) break; Ｐ＿点击実行(0); }`，而 `Ｐ＿点击実行`(7659)
    通体只有 `return parts::detail::WaitForClick()`，后者每轮做一次 `AFL_View_Update`。
    所以一段 N 帧的演出要 N 次**有效**点击才播完（"点在不在"不影响）。
    判据必须用 `[call] 29658`(Exit) / `[inp] EndInput` 的计数，不是"我点了几次"。
15. **帧率先看渲染后端，再看管线**：headless 走 Xvfb（无 DRI3）→ `llvmpipe`；
    `XSYS4_REAL_DISPLAY=1` + 真实桌面 → `Mesa Intel(R) Graphics`。
    实测同场景 **13.5 fps vs 99.7 fps**（7 倍）。`XSYS4_GL_DBG=1` 打 `[gl] renderer`。
    性能类结论（帧率/演出时长/点击命中率）必须注明后端，否则两次测量互相矛盾。
16. **`Array.Alloc(n)` 对结构体数组必须构造出 n 个元素**（含构造函数）。否则元素是 0，
    读 vtable 直接越界。
17. **`VM_ERROR` 是 `_Noreturn`**：写在它之后的探针会被当死代码删掉。探针放前面，或改
    `WARNING` + `return`。
18. **字形纹理（单通道 rmap）必须展开成 RGBA 再上传**：`GL_R8/GL_RED` 会让按 vec4 采样的
    着色器读到 `rgb=0,a=1`（文字整块黑或透明）。
19. **文字走精灵路径**（`gfx_render_texture`），不要走 `parts_render_texture`
    （对字形纹理一个像素都不出，见 `[UC-TEXSHADER]`）。
20. **`SetComponentShow` 的编号可能是部件号也可能是图层号**：部件号 ≥ `1000001000`，
    图层号是小下标 → `<100000000` 一律按图层处理。
21. **遮挡不要写成"压栈即盖住下层"**（会一弹对话框就整片黑）。规则是"视觉地板"：某层
    出现过铺满视图且不透明的部件就算地板，比它低的层不画，且**认定后记住**。
22. **用户组件的 EX `数据` 必须原样写回引擎**（`PE_SetUserComponentData`）。不写回则
    `GetUserComponentData(no,"参数")` 恒 `(nil)`，`TextButton@Init` 在"参数≥2"处直接
    return，整条点击链断在最上游。
23. **死掉的字符串槽降级成空串**，不要 `VM_ERROR`：越界/类型不符 → 限流告警 + 空串。
    这是引擎与游戏数据的边界，宁可让剧情走下去。
24. **图层 ID ≠ 栈下标**：`AddLayer` 返回单调 ID；`GetActiveLayer/SetActiveLayer/
    SetComponentShow` 入参是 ID，内部渲染与命中用下标；`RemoveController` 后要同步左移
    各部件的 `controller_no`。
25. **码表正确 ≠ 字形存在**：GBK 模式要用 CJK 字体（fontconfig 动态查，不写死路径）；
    VL-Gothic 不含简体字，表现为方框而纹理尺寸正常。
26. **数组浅拷贝必须给引用元素加自己的引用**（`array_copy_values()` 按 `variable_type()`
    判断）。否则副本销毁时把原件元素一起释放 → 成片 double free。
27. **"哪些类型算引用"只有一个答案**：`variable_owns_ref()`（`include/vm/page.h`）。
    `function_call()` 只给 `AIN_REF_TYPE` 加引用是**有意的**，别再"顺手统一"。
28. **短路分支必须让调用方知道它短路了**：`function_call(0)` 不建帧，`method_call` 若照常
    `set_struct_page()` 就会改掉调用者的 `this`。用 `call_stack_ptr` 前后对比识别。
29. **"元素是接口"的数组 = 2 槽/元素**：分配/增长/计数**三处**一致；`nr_vars` 是槽数、
    `Count()` 返回元素数。`is_interface` 只能靠 `ain->structures[n].is_interface` 识别。
30. **接口值跨页传递时第二槽是小整数**：`copy_page()` 对接口数组奇槽必须原样拷贝 →
    契约 27 的"三处"扩为**四处**（分配/增长/计数/复制）。
31. **泛型容器别拿声明当真相**：元素种类只在页的 `a_type` 里，未定型是 `AIN_ARRAY`
    （`array_type()` 返回 VOID）。写档取自实例，读档从 `flat_array->type` 反推。
32. **存档的 `-1` 是"未设置"，两侧都要认**；三张表索引一律先过 `gsave_index_ok()`。
    `libsys4` 的 `gsave_validate_value()` 是**本地补丁**，升级子模块必须重打。
33. **游戏用"名字后缀"当下标**：`String.GetPart(ref string, Index)` 是**从 Index 截到末尾**
    （三参版才带 Length），且 0 基。曾两次踩：差一位 → `Tab1` 得 -1；按"取 1 字符"实现 →
    `Tab10` 得 `"1"` → 当成 Tab1，`m_btn[9]` 永不初始化。判据：`XSYS4_STR_INT_DBG=1`。
34. **空引用上的方法调用**（`fno==-1`）当前降级为"跳过并记 `[nullcall]`"。这是**缓解**，
    见 `[NULL-IFACE-CALL]`；`XSYS4_NULL_METHOD=0` 复现中止。
35. **带谓词的 HLL 不许"忽略谓词、返回整份拷贝"**。这类 stage-1 桩把"过滤"变成"恒真"，而调用方把它当筛选器用：`Array.Where` 曾因此让 `CompletedTrophy@GetNewCompleted()` 返回**全部**成就，进而叠出 40+ 个全屏模态把编队界面盖住（D158）。实现不了就返回空数组，别返回全集。
36. **部件号段是版本相关的，别照抄旧引擎**。`PE_GetFreeNumber` 曾沿用 Rance 01 的 `1000001000`，而兰斯10 的 AIN 校验 `1～999999999` → `Parts@Release` 的 `Ｐ＿削除` 每次拒绝删除，单局 213 次部件残留（D161）。**旧日志里的 `100000xxxx` 已作废**，换算 `新号 = 旧号 - 100001000`。
37. **「部件被点中」≠「按钮被按下」——引擎必须自己把点击派发给 AIN 按钮对象**（D337）。
    委托存在 AIN `Parts` 对象字段 7（`Parts@SetOnClickEvent`）或 `TextButton` 自身字段 5，
    派发者是 `Parts@OnClick`(29541)，而它**零个 AIN 调用点** —— 这一拍没有任何 AIN 代码负责。
    映射只能扫堆：`Parts.m0` / `TextButton.m3.m0` 等于被点部件号。别再去 `CPartsFunctionSet.m7`
    里找缺口（那条路本作运行期为 0 调用）。点击落在容器上时按 `PE_PartsIsUnder()` 下钻一层。

---

## 4. 诊断开关（全部默认关，门禁会检查）

<!-- STATS:BEGIN 由 tools/gen-doc-numbers.py 生成，不要手改 -->
**自动生成数字**（禁止手写；完整口径见 `docs/STATS.md`）：诊断开关 **233**（未标注 31）｜ HLL 源码口径 **3315 / 4505（73.6%）** ｜ 假设台账 / 根因编号 / 未结债 **64 / 109 / 36** ｜ 影响面 / 子模块补丁 **33 / 2** ｜ 默认基线 `d292fix2`: `acts=36 / err=0 / core=0 / quest_map=1`。
<!-- STATS:END -->

最常用的：

| 开关 | 用途 |
|---|---|
| `XSYS4_AUTO_MAP=1` | 启动 4s 后直接 `Rance10@RunGameChapter1`（`:DELAY` 可调），跳过点标题 |
| `XSYS4_AUTO_PARTY=1` | 地图装载后直接 `SceneQuestMap@OpenParty`（备用；正常走真实点击） |
| `XSYS4_DUMP_AT=<hex>[,n]` | **原生断点**：停在该 ip，打印局部变量 / `this` 页（含成员数组）/ 字符串内容 / 8 帧栈 |
| `XSYS4_MC_WATCH=<hex>` | 只在一个 `CALLMETHOD` 站点打印 nargs/fno/obj/接收者成员 |
| `XSYS4_MC_SLOT_DBG=1` | `nargs` vs `function_argument_slots()` 不符时报警（契约 2 的判据） |
| `XSYS4_BADSTRUCT_DBG=1` | `this` 结构号 ≠ 函数所属结构号，打 `[badstruct]` / `[rebuild]` |
| `XSYS4_INPUT_DBG=1` | 命中候选表 + `[inp]`。**限流**：前 40 帧 + 点击帧 |
| `XSYS4_PARTS_NO_BASE=<n>` | 改动态部件号起始基址（默认 `900000000`；`=1000001000` 回退到旧的越界行为，用于 A/B）|
| `XSYS4_MSG_TRACE=<type>` | 队头消息类型等于该值时，打印 AIN 问过的每个消息访问器（`[msgt]`）|
| `XSYS4_ICAST_DBG=1` | 只打**失败**的 `X_ICAST`：目标类型号 + 源对象的真实页类型/结构号（`[icast]`）|
| `XSYS4_CLICK_DBG=1` | 跑测脚本侧：每次点击的 view/win 坐标、窗口几何、落点窗口 |
| `XSYS4_PARTS_DUMP=1` | 每次活动装载后快照"当前显示的部件"（no/ctrl/z/pos/尺寸/UC 名） |
| `XSYS4_CTRL_DBG=1` / `XSYS4_LAYER_TRACE=1` | 图层 show / 遮挡判定 / 栈操作 |
| `XSYS4_MEM_STAT=1` | 每 2s 一行 `[mem]`：heap_slots/live/pages/strings/rss/textures |
| `XSYS4_HEAP_LIVE_LIMIT=<n>` / `XSYS4_RSS_LIMIT=<MB>` | 存活对象 / RSS 硬闸（默认 50 万 / 关） |
| `XSYS4_HLL_TRACE=<lib>,<fno>` | 某 HLL 调用的实参页形状 |
| `XSYS4_FN_DUMP=<fno>` | 某 AIN 函数被调用时打印 `this` 与实参（比 `CALL_TRACE` 省得多） |
| `XSYS4_DG_PAGE_DBG=2` | delegate 页槽类型 / seq 全量跟踪（`[DG-PAGE-TYPE]` 用） |
| `XSYS4_DG_DRIFT=1` | 委托循环：`DG_CALLBEGIN` 记 `sp`，每次 `DG_CALL` 比对，差格即报 `[dgdrift]` + 上一次派发的回调名 |
| `XSYS4_STR_INT_DBG=1` | `String.GetPart` 输入/输出 + `Int()` 读到的文本（**按内容过滤，不限流**） |
| `XSYS4_NULL_RET=1` | 空引用调用降级时补一个 `-1` 返回值，让净栈效果与正常调用一致（默认关，A/B 用） |
| `XSYS4_SP_TRACE=<fno>` | 该函数逐指令 `(ip,sp)` 进环形缓冲，**退出时栈不平才倒出来**（`[spr]`） |
| `XSYS4_GSAVE_DBG=1` | 读档逐条打印数组类型全局 |

**探针纪律**：限流探针的"没有输出"不等于"没有发生"。判断输入是否丢，要同时看
`xdotool getmouselocation` 的 `window` 字段、点击前后截图、`PE_GetClickPartsNumber()`。
（债 `[PROBE-GATED]`）

---

## 5. 纪律

1. **跑测只在独立 Xvfb**（`:90+`）。真实桌面跑会打断用户操作，也可能吃满内存。
2. **跑测必须限内存**（`run-headless.sh` 自带 3G）。历史上两次把整机推到 OOM。
3. **单一信息源**：每类信息只在一处定义 —— 状态/契约/卡点=本文件（`docs/STATUS.md`），
   债务=`docs/TECH_DEBT.md`，
   假设=`docs/HYPOTHESES.md`，影响面=`docs/IMPACT.md`，工具链=`docs/SDK.md`，
   离线工具参数=`docs/OFFLINE.md`，数字=`docs/STATS.md`（生成）。
   **其余文档只能写链接或一句指针，不得复制会过期的副本**；`STATS.md`/`SWITCHES.md`
   由脚本生成；历史进 `archive/`（只读）。
4. **文档只写结论**：STATUS/TECH_DEBT 单条不超过 3 行排障叙述，过程进 `archive/`。
5. **重构完成 = 编译通过 + `check_core_deps.py` PASS + 端到端跑一次**；改代码/文档后跑 `gen-switches.py` 与 `gen-doc-numbers.py --check`，数字腐化即失败。
   `--check` 现在覆盖**五类**腐化，不只是数字：① 数字/开关/台账计数漂移；② 文档结构（契约编号重复、孤儿表格行、已结项留在未结区）；③ **卡点挂的债标签是否真实存在且未清偿**（防「卡点过期」）；④ **文档地图是否声明了每个 `.md`**（防「文档悄悄游离在治理之外」）；⑤ **工作区根 `AGENTS.md` 是否存在且指向本文件**（防「压缩上下文后丢核心目标」，D291）。
6. Core Layer 不得依赖 Linux 头文件、不得写死绝对路径。
7. **分支**：`main` 是唯一主线（SDL3 后端），日常提交与推送都在这里；`sdl3` 只是它的等价指针，
   `rance10-native` 是 SDL2 时代历史（只读）。**不合并上游**——本 fork 已越过上游覆盖范围，
   上游改动只在需要查格式语义时参考。切主线前的旧快照链在 `main-archive-20261119`（只读，可回滚）。
7. **状态文档必须在版本控制内**（D273）。此前 `README.md`/`TECH_DEBT.md` 放在工作区根，
   而根目录不是 git 仓库 —— 项目最重要的两份文档改了无法提交、也没法回滚。
   现在它们是 `docs/STATUS.md` 与 `docs/TECH_DEBT.md`，随仓库走。
   **新增状态类文档时先确认它落在仓库内**，否则等于没纳管。
8. **工作区根必须有 `AGENTS.md`，且它只许是指针 + 纪律**（D291）。
   原因：agent harness 只把 `AGENTS.md`/`CLAUDE.md` 注入**系统提示**，而本文件是
   普通文件 —— 读过才进对话，一压缩就被摘要掉，于是「压缩后忘了核心目标是移植兰斯10」。
   它**不得复制** §0 卡点（写拷贝必然过期，D264–D269 就是这么白跑几轮的），
   卡点唯一定义仍是 §0 这一屏；`gen-doc-numbers.py --check` 兼管存在性与体积。

---

## 6. 文档地图（四层信息架构）

> **单一信息源**：每行「职责」就是该类信息唯一的定义处，其它文档引用时只能链接，不得复制。
> ① **入口层**＝每次会话开场必读；② **工作流层**＝攻当前卡点时读；
> ③ **知识层**＝遇到具体问题查字典；④ **生成层**＝脚本维护，禁止手写。
> 仓库级指针页与上游自带文件不承载状态；历史全文进 `archive/`（只读）。

| 层 | 文件 | 职责 | 读它的时机 |
|---|---|---|---|
| ① 入口 | `docs/STATUS.md` | **状态唯一源**：现状 + 契约 + 卡点 + 纪律（D273 前叫工作区根 `README.md`） | 每次会话开场 |
| ① 入口 | `docs/TECH_DEBT.md` | 未结债（A 结构性 / B 会炸 / C 长尾）+ 已清零索引 | 决定下一步做什么 |
| ① 入口 | `README.md` | 仓库指针页：定位/构建/补丁/门禁/分支；**不写状态** | 单独 clone / 要构建时 |
| ② 工作流 | `docs/HYPOTHESES.md` | 假设台账：可证伪判据 + 证据位置 + 改判链，证伪不删 | 开工前看「当前待验」 |
| ② 工作流 | `docs/IMPACT.md` | 影响面清单：影响场景 / 必重跑闭环 / 回退方式 | 改跨模块代码之前 |
| ② 工作流 | `docs/HLL-L3.md` | 第一章 121 个 HLL 的 L2/L3 达标计划（8 批）+ 逐函数台账（台账块脚本生成） | 做 HLL 实现前 |
| ③ 知识 | `docs/SDK.md` | 工具链入口：`xsys4` 命令、动作序列、跑测配方、日志前缀 | 动手之前 |
| ③ 知识 | `docs/RENDER-BACKEND.md` | 渲染后端决策记录：SDL3 / Vulkan / SDL_GPU / GLES3 的可行性与成本 | 想动图形层之前 |
| ③ 知识 | `docs/OFFLINE.md` | 离线工具参数/模式 + HLL 全量普查方法 | 用离线工具时 |
| ③ 知识 | `docs/GUIDE/` | 深层方法论与契约，对外成书。**定位是「判据与陷阱」，不是状态**：它只在**修订记录**里引用 D 号（说明"这一版补了哪条判据"），正文不写当前进度/卡点。读它要判据，读 `STATUS.md` 要现状 —— 两者不要互相替代 | 要判据 / 方法论时 |
| ③ 知识 | `tools/MAP.md` | `tools/` 目录分层与脚本分工 | 加 / 找脚本时 |
| ③ 知识 | `docs/PROJECT-MANAGEMENT.md` | 工程管理复盘，**面向用户/管理者；AI 会话不读** | 带团队 / 立规矩 / 复盘 |
| ③ 知识 | `docs/MANUAL-INDEX.md` | 官方手册索引 + 图片陷阱 + 验收判据（**不含当前状态**） | 对照手册验收界面 / 查按键时 |
| ③ 附件 | `game_compatibility.md` | **上游自带**英文兼容性表，与本项目无关；原位保留、不纳管 | 不读 |
| ③ 附件 | `notes/` | **上游自带**英文技术笔记（`system4` / `parts-engine` / `language-update`，来自 upstream 提交 `cc199dc Add notes directory`，2023-04）；讲的是 System4/PartsEngine **格式与接口的跨游戏演进**，不是兰斯10 移植结论。原位保留、**不纳管**（不随本项目管理，也不得当作结论引用） | 查格式语义时可参考，**结论一律以 `docs/GUIDE/` 为准** |
| ③ 附件 | `test/Source/`、`tools/patches/`、`tools/dev/patches/` 下的 `README.md` | **上游自带**占位说明；原位保留、不纳管（`tools/MAP.md` 已说明 `tools/dev/patches` 是「只读参照，不要重跑」） | 不读 |
| ④ 生成 | `docs/MANUAL/` | 官方手册全文 md + 随附图片（`gen-manual.py` 生成，禁止手改） | 全文检索 / 对照界面基准时 |
| ④ 生成 | `docs/SWITCHES.md` | 开关总表（`gen-switches.py` 生成） | 查开关用途 |
| ④ 生成 | `docs/STATS.md` | 项目数字（`gen-doc-numbers.py` 生成，`--check` 校验） | 核对数字 |
| 历史 | `archive/` | 全文 / 取证链 / 阶段报告（含原 `PROJECT.md`、`HLL-GAP.md`），**只读** | 需要完整证据时 |
