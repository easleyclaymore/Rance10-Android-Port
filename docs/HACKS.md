# 默认开启的 hack（清单与删除条件）

## 为什么单列一页

`XSYS4_*` 开关本身不是债 —— **可回退是好事**。债是「**默认就是 hack**」这一类：

- 默认关的实验，你能随时回答「不 hack 时语义是什么」；
- 默认开的 hack，**你回答不了**。基线已经被改掉了，A/B 失去参照。

更麻烦的是它们会互相掩盖：`BG_SWEEP_MAP` 替 AIN 收起地图，等真正的层管理修好那天，
这个 hack 会反过来把该显示的地图收掉 —— 而届时的现象（地图不见）与当初修的那个
（地图不消失）长得一模一样。

这类条目在 `TECH_DEBT.md` 里是分散的；本页只做一件事：**把「默认开的 hack」集中登记**，
每条写清「它假设了什么 / 真语义应该是什么 / 怎么判定可以删」。

新增一个默认开启的 hack 时，必须在本页加一行。

## 内容级 hack（用「兰斯10 的内容特征」做判断，最危险的一类）

这些条目读 CG 名、部件号、中文前缀来决定行为。对兰斯10 有效，对任何别的 System4
游戏都是错误结论 —— 但它们同时也是「引擎语义缺口」的活标本，删掉之前要先补语义。

| 开关 | 位置 | 它假设了什么 | 真语义应该是什么 | 删除条件 |
|---|---|---|---|---|
| `XSYS4_BG_SWEEP_MAP`（默认开） | `parts.c:pe_sweep_map_overlay` | 「根级 `背景／*` 换图时，同层可见的 `地图／*` 必须收起」—— 靠 **CG 名中文前缀** 判定 | AIN 侧本应用层（`SetShow`/`EraseLayer`）收起旧覆盖层；现在是两件被放进同一层、AIN 从不 `SetShow`，我们替它收 | 出现「背景换图后地图该留着」的场景（或另一个游戏的同类结构），或 AIN 侧开始自己收起。判据：`XSYS4_BG_SWEEP_MAP=0` 时该屏仍然正确 |
| `XSYS4_EX_LAYOUT_FREE`（默认开，D214 转正） | `activity.c:978` | 「EX 里布局盒的 `配置=1`（纵向）不该真的重排，应保留 EX 绝对坐标」 | 布局盒应该按 `配置`/`wrap`/`align` 真正排版 —— 需要 `GetLayoutBoxPadding*`（D385 已补）+ `GetLayoutBoxReturnSizeForRate`（仍是空桩）都到位 | `SetLayoutBoxReturnSizeForRate` 实现后，`=0` 时标题菜单与战线面板的渲染/命中仍一致 |
| `XSYS4_CLICK_NEEDS_SHAPE`（默认开，D338 转正） | `input.c` | 「`w/h<=0`、未初始化、铺满全屏的布局盒不参与 hover 与捕获」 | 全屏布局盒本不该是点击接收者；根因是活动关闭后残留件仍在树上（`[LAYER-LEAK]`） | `[LAYER-LEAK]` 清零后，`=0` 时战斗按钮与选项按钮都能点中 |
| `XSYS4_PARTS_EVENT_GEOM`（默认开，D338 转正） | `vm.c` / `parts.c` | 「命中号找不到按钮对象时，按坐标找覆盖该点的最上层局部 `ck=1` 件改派」 | AIN 的 `Parts@OnClick` 应能按部件号直达，不该需要坐标兜底 | 技能卡/角色卡的 `m7`/`m3` 委托链修好后（见 `[BATTLE-STALL]`），`=0` 时点击仍能派发 |
| `XSYS4_COVER_HYST`（默认开，D343） | `parts.c:parts_update_cover_layer` | 「`cover_layer` 单帧尖峰是噪声，连续 3 帧同值才生效」 | 全屏不透明件的判定本身要正确（`PARTS_RECT_DETECTION`/布局盒已排除，但 `900000644/631` 那类短命全屏 CG 仍未查清） | 短命全屏 CG 的来源查清并排除后，`=0` 时不再出现 4/7 逐帧跳 |

## 语义修复类（默认开，但不是「猜内容」）

这 20 个改的是**引擎行为本身**（引用计数、槽回收、空方法降级、层号迁移等），
默认开是对的；列在这里只是因为它们同样「没有干净基线可对照」，回归时要知道自己开着什么。

`XSYS4_ACTIVE_CTRL_SET`、`XSYS4_ACTIVE_PARTS`、`XSYS4_ACT_RELEASE_DGLIST`、
`XSYS4_ARRAY_REF_FIX`、`XSYS4_CTRL_PENDING`、`XSYS4_DG_KEEPALIVE`、
`XSYS4_EX_RATIO_HEURISTIC`、`XSYS4_EX_RATIO_POS`、`XSYS4_EX_RECT_DET_SIZE`、
`XSYS4_EX_RECT_FILL_PARENT`、`XSYS4_EX_RECT_MIN1X1`、`XSYS4_EX_RECT_SIZE`、
`XSYS4_LAYER_IDX`、`XSYS4_MSGWIN_FULL`、`XSYS4_MSGWIN_TEXT_BASELINE`、
`XSYS4_NULLMETHOD_FIX`、`XSYS4_PARENT_REAL`、`XSYS4_PARTS_EVENT`、
`XSYS4_RECYCLE_SLOTS`、`XSYS4_RELEASE_RECURSIVE`

## 已知的能力缺口（不是 hack，但解释 hack 为何存在）

`HLL_TODO_EXPORT` 桩 1192 个 —— 即引擎语义还有 1000+ 个入口没实现。
「桩返回 0 / 空」和「hack 补行为」是同一件事的两面：桩让 AIN 拿到 0，
hack 再用内容特征把 0 解释成合理值。所以**删 hack 的前提通常是先填桩**。

判据模板（用于任何一条）：

```
1) 关掉 hack：XSYS4_XXX=0
2) 跑 tools/run/smoke.sh full
3) 若 FAIL，看它 FAIL 在哪一项 —— 那一项就是真语义缺口的位置
```

本页不追求「全部删掉」，只保证**每一条都有人知道它的假设与删除条件**。
