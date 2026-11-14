# xsystem4 移植工具链（SDK 参考）

> 这份文档回答「我要做 X，该用哪条命令」。
> 离线工具的详细用法看 [`OFFLINE.md`](OFFLINE.md)，开关总表看 `SWITCHES.md`（自动生成）。
> 方法论在项目根的 `GUIDE/`，**不要往这份文档里写踩坑经过**。

## 这是什么

一套用于**黑盒字节码引擎的逆向移植**工具链：从 `.ain` 反汇编、到运行时诊断、
到无头自动化跑测。厂商没有提供 SDK，这些工具是逆向出来的，所以每一条
能力都对应"当初为了回答某个问题才造它"。

**它不是什么**：不是游戏 mod 工具，不修改 `.ain` 内容，不含游戏资源。

## 三层结构

| 层 | 工具 | 特点 | 何时用 |
|---|---|---|---|
| 离线分析 | `ain_at` `ain_audit` `ain_xref` `exdump` `gsave_dump` | 不启动游戏、不需要 X、秒级 | 读代码、查符号、对签名（详见 [`OFFLINE.md`](OFFLINE.md)） |
| 运行时 | `build/src/xsystem4` + `XSYS4_*` 开关 | 需要 X（或 Xvfb）、能看真实状态 | 查"跑起来为什么不对" |
| 自动化 | `run-headless.sh` + `RANCE10_LIVE_SEQ` | Xvfb 里跑、出日志和截图 | 回归验证、复现点击流程 |

## 五分钟上手

```bash
cd Rance10_Port/xsystem4

./tools/xsys4 doctor            # 环境是否齐（引擎 / 数据 / Xvfb / xdotool）
./tools/xsys4 build             # 构建引擎
./tools/xsys4 build tools       # 构建离线工具（ain_at 等）

./tools/xsys4 sym FindEmptyIndex          # 它叫什么号、多大
./tools/xsys4 disas Party@FindEmptyIndex  # 它长什么样

./tools/xsys4 run first 100 /tmp/first.png
ls /tmp/first_*_wait.png                   # 每一步都会截图
```

跑测日志在 `Rance10_Port/logs/live_<tag>.log`。

## 配置：动作序列

`xsys4 seq` 的第二个参数是一个用分号分隔的小语言。坐标是 **1920×1080 下的绝对坐标**
（脚本内部按窗口缩放换算）。

| 动作 | 语义 | 备注 |
|---|---|---|
| `wait:3000` | 等 3000 **毫秒** | 注意单位是毫秒，不是秒 |
| `click:135,576` | 左键点 | 每步之后自动截图 |
| `rclick:135,576` | 右键点 | |
| `move:135,576` | 移动鼠标 | |
| `key:Return` | 按键 | X11 keysym |
| `until:SceneParty:20000` | 等到日志出现该字符串 | 默认超时 60 秒；等到后再多等 1.5 秒 |
| `clickunless:135,576:SceneParty` | **日志里没有该字符串时才点** | 命中率低时的重试链 |

`clickunless` 是这套语言里最有用的一步。标题画面的「开始」按钮出画时刻
受贴图加载影响，固定 `wait` + 单次点击的实测命中率只有约 30%；改成
"没进就再点一次"能把这个数拉到接近 100%，而一次跑测要两分钟，值得。

```bash
# 从标题到任务地图，稳健版
./tools/xsys4 seq map1 \
  "clickunless:634,734:SceneQuestMap;wait:3000;clickunless:300,300:SceneQuestMap;until:SceneQuestMap:40000" \
  110
```

## 配方

### 我要知道「这个函数到底有没有被调用」

```bash
XSYS4_CALL_TRACE=24317,24300 ./tools/xsys4 seq probe1 "..." 110
grep '\[call\]' ../logs/live_probe1.log
```

输出 `[call] #n 函数名 (fno) ret=返回地址 csp=调用点`。
统计次数：

```bash
for f in 24317 24300; do echo "$f=$(grep -ac "($f)" ../logs/live_probe1.log)"; done
```

**判据**：0 次不等于"没跑"，见下面「诊断输出的契约」里关于探针限流的警告。

### 我要知道「某个变量此刻是什么值」

在判定点下断点，不要猜返回值：

```bash
# 442eb2 是 FindEmptyIndex 里 LT 指令的地址
./tools/xsys4 dump 442eb2,4 ise1 110     # verb 自己会设 XSYS4_DUMP_AT
```

输出帧名、`this` 每个成员、局部变量、操作数栈、以及 AIN 调用栈。

字符串成员会连文本一起打（`-> str ""`）—— 判断"空串还是 null"时这是决定性的。

### 我要知道「这个地址属于谁」

```bash
./tools/xsys4 at 0x3750d4 6        # 从任意地址反汇编（可未对齐）
./tools/xsys4 disas QuestMapObjectLoader@Init   # 对整个函数
./tools/xsys4 sym CheckNoLeader    # 按子串找
```

### 我要知道「部件为什么不显示」

```bash
XSYS4_PARTS_DUMP=1 XSYS4_CTRL_DBG=1 ./tools/xsys4 seq p1 "..." 110
grep '\[parts\]' ../logs/live_p1.log | tail -20
grep '\[ctrl\]'  ../logs/live_p1.log | tail -5
```

一行里带全了判断所需的一切：

```
[parts] <活动名> no=900000458 ctrl=4 z=35 pos=(0,507) 512x91 a=255 ps=0 st=1 tex=1 uc=-
```

`a`=alpha、`ps`=当前状态、`st`=内容类型（1=CG 2=TEXT 9=FLAT）、`tex`=纹理是否存在。
五种"显示了却看不见"的成因各有对应字段，逐个排除比猜架构快（见 GUIDE 4.7）。

### 我要复现一个点击流程

```bash
XSYS4_INPUT_DBG=1 ./tools/xsys4 seq c1 "wait:16000;click:135,576" 100
grep 'click=1' ../logs/live_c1.log | head
```

`[inp]` 会列出点击位置的候选部件（`hb` 命中盒、`global` 绝对坐标、`click/pass/show`）。
**先证明点击落在了目标上，再讨论功能对不对。**

### 我要验证「这次修改没破坏基线」

```bash
./tools/xsys4 check                     # 门禁 + 开关标注
./tools/xsys4 seq base1 "<已知能跑通的序列>" 110
```

基线判据（以任务地图为例）：`0 ERROR`、活动数稳定、关键 `[call]` 命中数稳定。
三个数任何一个变了，先解释清楚再继续。

### 我要加一个新开关

1. 代码里 `getenv("XSYS4_XXX")`，默认关闭
2. 在 `tools/switch-desc.txt` 加一行（名字 `TAB` 一句话用途）
3. `./tools/xsys4 check` —— 它会重新生成 `docs/SWITCHES.md`

没写说明也能跑，但 `SWITCHES.md` 里会显示 `—`，且 `check` 的返回值会带着这个数。

### 两个使用边界

**`xsys4 switches <词>` 是整行 grep**，不是只匹配开关名。查 `CTRL` 会连
描述里含 "controller" 的条目一起出来。想要精确，用完整名字。

**`xsys4 debt` 读的是项目根的 `TECH_DEBT.md`**，它在 git 仓库之外。
clone 仓库的人只有引擎和工具，`debt` 会报"找不到"。同理 `xsys4 check`
里的"未标注开关数"依赖仓库内的 `tools/switch-desc.txt`（这个在）。

## 诊断输出的契约

**日志前缀就是这套系统的 API**，脚本（含 GUIDE 里的所有判据）都依赖它。
改格式等于破坏兼容，新增字段只能往后加。

| 前缀 | 含义 | 谁打的 |
|---|---|---|
| `[call]` | AIN 函数调用/返回 | `XSYS4_CALL_TRACE` |
| `[hll]` | HLL 调用 | `XSYS4_HLL_TRACE` |
| `[dump]` | 断点快照 | `XSYS4_DUMP_AT` |
| `[parts]` | 部件快照 | `XSYS4_PARTS_DUMP` |
| `[ctrl]` | controller 栈 / 视觉地板 | `XSYS4_CTRL_DBG` |
| `[inp]` | 输入命中候选 | `XSYS4_INPUT_DBG` |
| `[act]` | 活动装载 | 常驻 |
| `[ARR]` | 数组操作 | `XSYS4_ARR_DBG` |
| `[stub]` | 软桩命中 | `XSYS4_SOFT_STUB` / `XSYS4_STUB_LOG` |

### 两条纪律

**一、探针限流会骗人。** 带"前 N 次"或"每 N 次"闸门的打印，
**没有输出 ≠ 没有发生**。判断某功能是否坏，必须至少有第二个独立证据
（截图 / 外部工具 / 返回值）。

**二、开关默认全关。** 交付前 `./tools/xsys4 check` 必须 PASS；
常驻诊断只用默认关闭的 `XSYS4_*`，临时探针用完删掉。

## 目录职责

```
兰斯10/
├── README.md            现状 / 阶段 / 卡点（随项目推进更新）
├── TECH_DEBT.md         未结债（A 结构性 / B 会炸的 / C 长尾）
├── GUIDE/               方法论（不随项目状态更新，对外开源）
└── Rance10_Port/
    ├── game/            游戏数据（不入库）
    ├── logs/            跑测日志（不入库）
    ├── tools/           离线工具 + 构建脚本
    └── xsystem4/
        ├── tools/
        │   ├── xsys4        统一入口
        │   ├── MAP.md       脚本分工地图（谁调谁、该用哪个）
        │   ├── lib/common.sh 路径锚点（所有脚本从这里取路径）
        │   ├── run/         跑测（8 个）
        │   ├── build/       构建（2 个）
        │   ├── env/         依赖（2 个）
        │   ├── src/         离线工具源码（*.c）
        │   ├── offline/     离线辅助（afa_ls / pactex_dump / alice.sh）
        │   └── bin/         离线工具产物（已 gitignore）
        ├── docs/SDK.md      本文档
        ├── docs/SWITCHES.md 开关总表（生成）
        └── src/             引擎源码
```

**文档只维护三处**（README / TECH_DEBT / GUIDE），加这份 SDK 参考和生成的
开关表。其余一律 `archive/`，只读。

## 还没做到的

诚实记录，避免下一个人重复踩：

- `SWITCHES.md` 里有 24 个开关没有用途说明（`./tools/xsys4 check` 会报这个数）
- 12 个跑测脚本里还有 6 个是历史遗留（`safe_run*` `storm` 等），职责与
  `run-headless.sh` 重叠，尚未合并
- 动作序列不支持"读屏决策"（例如"看到红色才点"），只能靠 `until` / `clickunless`
- 离线工具之间没有共享的符号解析缓存，每次都重新读 `.ain`（约 0.3 秒，可接受）
