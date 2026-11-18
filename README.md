# xsystem4 · Rance10 原生移植 fork

[nunuhara/xsystem4](https://github.com/nunuhara/xsystem4) 的下游 fork：让《ランス10》
在 Linux / Android 上**原生**运行，不走 proot / 转译层。上游是 AliceSoft System4 引擎的
类 Unix 开源实现（C + SDL2 + OpenGL）。Rance10 用 **AIN v12** 字节码，在值表示、
接口派发、泛型容器、存档格式上都有上游未覆盖的语义，本 fork 用字节码与存档文件
逐条取证补齐。目标交付 arm64 APK（验收设备 realme RMX3700），当前在 Linux 桌面推进。

![第一章 ADV 实机画面](docs/screenshots/ch01-adv.png)

> **第一章开场剧情已经在 Linux 原生构建上跑通。** 上面这张是**未经修图的实机画面**
> （Xvfb 内 `import -window` 抓的 1920×1080 原始帧，缩放到 1280×720 入库）：
> 背景随剧情从「世界地图」切到「宇宙船／通道」，立绘、名牌、消息窗口、
> 任务信息面板与 SKIP 按钮全部按预期渲染。
>
> 之前这里卡了很久：地图那几张 CG 一直盖在新的背景上。根因是 AIN 把**全屏情报地图**
> （`AdvInfoMapCg`）和 ADV 窗口放进了同一层，收起时只关「地图层」，于是永远关不掉；
> 现在在背景换图时按「根部件」整组收起（`D317`，见 `docs/` 台账与 `../TECH_DEBT.md`）。
>
> 说明：这是**桌面 x86_64 构建**的画面，arm64 APK 尚未交付；战斗部分仍在攻。

> **本仓库不维护项目状态。**
> 现状 / 阶段 / 卡点 / 必读契约 / 技术债的**唯一信息源**是工作区根
> `../README.md` 与 `../TECH_DEBT.md` —— 它们在 git 仓库之外，单独 clone 本仓库时拿不到。
> 本文件只回答「这个仓库是什么、怎么构建、边界在哪」，其余按下方文档地图找。
> 开关数 / HLL 覆盖率 / 基线 KPI 等数字以 `docs/STATS.md` 为准（脚本生成，禁止手写）。

## 获取源码

`libsys4` 是 submodule，remote 指向上游（我们无推送权限）：必须递归拉取，
再把 `patches/*.patch` 应用到工作树。**绝不用 gitlink 记一个别人拉不到的本地提交。**

```sh
git clone --recurse-submodules https://github.com/easleyclaymore/Rance10-Android-Port.git
git -C subprojects/libsys4 apply ../../patches/0001-libsys4-gsave-v8.patch
git -C subprojects/libsys4 apply ../../patches/0002-libsys4-ajp-metrics.patch
```

补丁内容：gsave v8 / 泛型数组、AJP metrics。已应用过会提示 `already applied`，忽略即可。

## 构建

依赖（Debian/Ubuntu 包名）：`meson` `ninja-build`、`libsdl2-dev`、`libglew-dev`、
`libfreetype-dev`、`libpng-dev` `libwebp-dev` `libturbojpeg0-dev`、`libsndfile-dev`、
`libffi-dev` `zlib1g-dev` `libdeflate-dev`、`bison` `flex`、`libcglm-dev`（可缺省，
meson 自动拉取）、chibi-scheme（可选调试器）。完整清单见 `meson.build`，
本机备忘见 `tools/env/inst-deps.sh`。

```bash
mkdir build && meson build && ninja -C build
```

> **踩坑**：桌面环境里 `PYTHONHOME` / `PYTHONPATH` 指向别的 Python 时，meson 会死在
> `ModuleNotFoundError: encodings`。构建前把变量摘掉：
> `env -u PYTHONPATH -u PYTHONHOME -u PYTHONDONTWRITEBYTECODE ninja -C build`

## 运行与跑测

```bash
build/src/xsystem4 /path/to/game_directory     # 或进入游戏目录后直接执行
```

跑测**只走** `tools/run/run-headless.sh`（独立 Xvfb + 3G 内存上限）。
命令、动作序列语法、跑测配方、日志前缀契约、诊断开关的总入口见
**[`docs/SDK.md`](docs/SDK.md)**；离线工具（不启动游戏、不需要 X）的参数与模式细节见
**[`docs/OFFLINE.md`](docs/OFFLINE.md)**；`tools/` 目录分层见 `tools/MAP.md`。

## 门禁与分支

```sh
python3 tools/check_core_deps.py    # Core Layer 无 Linux-only 头 / 硬编码路径 + 文档数字校验
```

Core Layer = `vm.c` / `page.c` / `heap.c` / `ffi.c` / `pal.c` 及对应头文件；平台服务经
`include/pal.h` 注入，Linux 实现在 `src/pal_linux.c`，Android 阶段提供 JNI 实现。

| 分支 | 用途 |
|---|---|
| `main` | 远端交付基线（单提交，历史已扁平化）；`git push origin $(git rev-parse public-baseline):main` |
| `public-baseline` | 本地单提交基线指针 |
| `rance10-native` | 开发分支，完整移植历史；日常提交与推送都在这里 |
| `upstream` | 上游基线，仅用于对照 |

## 内容边界

| 路径 | 内容 | 入库 |
|---|---|---|
| `src/`, `include/`, `meson.build` | VM / 渲染 / HLL 源码 | 是 |
| `subprojects/libsys4` | 上游 submodule（gitlink） | 是 |
| `tools/`, `patches/` | 离线工具、构建/跑测脚本、门禁、子模块补丁 | 是 |
| `docs/` | 方法论 / 工具链 / 台账 / 生成物 | 是 |
| `build/`, `game/`, `logs/` | 编译产物、游戏本体（有版权）、跑测日志 | 否 |

## 文档地图（四层信息架构，每类信息只有一个源）

> ① 入口＝会话必读（**状态在仓库外**）｜② 工作流＝攻当前卡点｜③ 知识＝查字典｜④ 生成＝脚本维护。
> 完整分层见工作区根 `../README.md` §6。

| 层 | 想知道 | 唯一源 |
|---|---|---|
| ① 入口 | 现状 / 阶段 / 卡点 / 必读契约 / 纪律 | `../README.md`（仓库外，工作区根） |
| ① 入口 | 未结技术债 / 已清零索引 | `../TECH_DEBT.md`（仓库外，工作区根） |
| ① 入口 | 仓库定位 / 构建 / 门禁 / 分支 | 本文件（**不写状态**） |
| ② 工作流 | 假设台账 / 变更影响面清单 | `docs/HYPOTHESES.md` / `docs/IMPACT.md` |
| ② 工作流 | **第一章 121 个 HLL 的 L2/L3 达标计划与台账** | `docs/HLL-L3.md`（台账块由 `hll_hotness.py --emit` 生成） |
| ③ 知识 | 工具链入口 / 动作序列 / 跑测配方 / 日志前缀 | `docs/SDK.md` |
| ③ 知识 | 渲染后端决策（含 SDL2→SDL3 迁移评估） | `docs/RENDER-BACKEND.md` |
| ③ 知识 | 离线工具参数与模式 / HLL 全量普查方法 | `docs/OFFLINE.md` |
| ③ 知识 | 引擎移植方法论（对外成书） | `docs/GUIDE/` |
| ③ 知识 | `tools/` 目录分层 | `tools/MAP.md` |
| ③ 知识 | 工程管理复盘（**面向管理者，AI 会话不读**） | `docs/PROJECT-MANAGEMENT.md` |
| ③ 知识 | 官方手册索引 / 图片陷阱 / 验收判据 | `docs/MANUAL-INDEX.md` |
| ③ 附件 | 上游自带英文兼容性表（与本项目无关，不纳管） | `game_compatibility.md`（本仓库根） |
| ④ 生成 | 官方手册全文 md + 随附图片（禁止手改） | `docs/MANUAL/` |
| ④ 生成 | 诊断开关总表 / 项目数字（禁止手写） | `docs/SWITCHES.md` / `docs/STATS.md` |
| 历史 | 全文 / 取证链 / 阶段性清单（含 `HLL-GAP.md`） | `../archive/`（只读） |

> **单一信息源规则**：任何当前状态、数字、契约、配置**只在上表对应的那一份里定义**；
> 其它文档需要引用时只能写链接或一句指针，不得复制一份会过期的副本。

## 上游与许可

- 上游：[nunuhara/xsystem4](https://github.com/nunuhara/xsystem4)
- 许可：**GPL-2.0**（见 `COPYING`）。本 fork 同样以 GPL-2.0 发布。
- 游戏本体、图像、音频等素材版权归 AliceSoft，**不包含在本仓库内**。
