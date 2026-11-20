# xsystem4 · Rance10 原生移植 fork

[nunuhara/xsystem4](https://github.com/nunuhara/xsystem4) 的下游 fork：让《ランス10》
在 Linux / Android 上**原生**运行，不走 proot / 转译层。上游是 AliceSoft System4 引擎的
类 Unix 开源实现（C + SDL2 + OpenGL）；**本 fork 的主线已把渲染/输入/音频层迁到 SDL3**
（`main` 就是这条线，见 `docs/RENDER-BACKEND.md` 决策三）。Rance10 用 **AIN v12** 字节码，在值表示、
接口派发、泛型容器、存档格式上都有上游未覆盖的语义，本 fork 用字节码与存档文件
逐条取证补齐。目标交付 arm64 APK（验收设备 realme RMX3700），当前在 Linux 桌面推进。

![第一章 ADV 实机画面](docs/screenshots/ch01-adv.png)

> **第一章已经能从标题一路走到第一场战斗。** 上面这张是**未经修图的实机画面**
> （引擎 `glReadPixels` 直接从 GL 主表面 dump 的 1920×1080 原始帧，缩放到 1280×720 入库）：
> 背景随剧情从「世界地图」切到「宇宙船／通道」，立绘、名牌、消息窗口、
> 任务信息面板与 SKIP 按钮全部按预期渲染。
>
> 链路依次跑通：标题 → 第一章 ADV（37 句）→ 回想对话 `ReplayMemoDialog` →
> 选项 `QuestMapSelectionButton` → `Game/Battle/*`。最后一跳是本轮修的：
> 点击在引擎里被正确捕获，但**没有任何 AIN 代码负责把它派发给按钮对象**
> （`Parts@OnClick` 静态 xref 为 0），现在由引擎回调（`D337`）。
>
> 说明：这是**桌面 x86_64 构建**的画面，arm64 APK 尚未交付；战斗画得出、
> 但指令按钮的命中优先级仍在攻（见 `docs/TECH_DEBT.md` 的 `[BATTLE-STALL]`）。

> **本文件不维护项目状态。**
> 现状 / 阶段 / 卡点 / 必读契约的**唯一信息源**是 [`docs/STATUS.md`](docs/STATUS.md)，
> 未结技术债是 [`docs/TECH_DEBT.md`](docs/TECH_DEBT.md)。
> 本文件只回答「这个仓库是什么、怎么构建、边界在哪」，其余按下方文档地图找。
> 开关数 / HLL 覆盖率 / 基线 KPI 等数字以 `docs/STATS.md` 为准（脚本生成，禁止手写）。

## 下载预编译二进制

不想自己编译就看 [Releases](../../releases)（当前最新：
[`v0.1.0-preview.2`](../../releases/tag/v0.1.0-preview.2)）。每个 `v*` tag 由
GitHub Actions 构建出 `xsystem4-<tag>-linux-x86_64.tar.gz`（release 构建、已 strip），
包内自带 **SDL3 + GLEW** 与 `run.sh`，解压即可跑：

```sh
tar xzf xsystem4-*.tar.gz && cd xsystem4-*
./run.sh /path/to/ランス10        # 目录里要有 AliceStart.ini / Rance10.ain
```

Release 正文写了当前进度、已修问题与已知问题；包内 `README-RUN.txt` 写依赖与字体设置。

> 早期阶段只发**二进制**，不做 deb / AppImage —— 现在的接口和依赖还会变，
> 发行版打包只会变成负担。

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

依赖（Debian/Ubuntu 包名）：`meson` `ninja-build`、**`libsdl3-dev`（≥ 3.2）**、`libglew-dev`、
`libfreetype-dev`、`libpng-dev` `libwebp-dev` `libturbojpeg0-dev`、`libsndfile-dev`、
`libffi-dev` `zlib1g-dev` `libdeflate-dev`、`bison` `flex`、`libcglm-dev`（可缺省，
meson 自动拉取）、chibi-scheme（可选调试器）。完整清单见 `meson.build`，
本机备忘见 `tools/env/inst-deps.sh`。

> `libsdl3-dev` 只在较新的发行版（Debian trixie+ / Ubuntu 25.04+）里有；老系统请按
> [SDL3 官方说明](https://github.com/libsdl-org/SDL/blob/main/docs/README-cmake.md) 从源码安装，
> 确保 `pkg-config --modversion sdl3` 有输出（本项目实测 3.4.20）。
> 上游 xsystem4 仍是 SDL2，但**本 fork 已不跟进上游**（移植深度已超出上游覆盖范围，
> 见下方分支表）。`meson.build` 用 `dependency('sdl3')` + `-DSDL_ENABLE_OLD_NAMES`，
> 只为降低"照着 SDL2 写法改"的迁移成本，不是为了跟上游合并。

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
| `main` | **唯一主线**：SDL3 后端 + 第一章推进，日常提交与推送都在这里 |
| `sdl3` | 已并入 `main` 的等价指针（同一提交），保留只为旧链接不失效，**不再单独推进** |
| `rance10-native` | SDL2 时代的完整移植史，只读对照（不再推进） |
| `main-archive-20261119` | 切主线前的旧快照链存档（只读，可回滚） |
| `upstream` | 上游基线，**只读参考**：不再合并上游改动（本分支已走到上游前面） |

## 内容边界

| 路径 | 内容 | 入库 |
|---|---|---|
| `src/`, `include/`, `meson.build` | VM / 渲染 / HLL 源码 | 是 |
| `subprojects/libsys4` | 上游 submodule（gitlink） | 是 |
| `tools/`, `patches/` | 离线工具、构建/跑测脚本、门禁、子模块补丁 | 是 |
| `docs/` | 方法论 / 工具链 / 台账 / 生成物 | 是 |
| `build/`, `game/`, `logs/` | 编译产物、游戏本体（有版权）、跑测日志 | 否 |

## 文档地图（四层信息架构，每类信息只有一个源）

> ① 入口＝会话必读｜② 工作流＝攻当前卡点｜③ 知识＝查字典｜④ 生成＝脚本维护。
> 完整分层见 [`docs/STATUS.md`](docs/STATUS.md) §6。

| 层 | 想知道 | 唯一源 |
|---|---|---|
| ① 入口 | 现状 / 阶段 / 卡点 / 必读契约 / 纪律 | `docs/STATUS.md` |
| ① 入口 | 未结技术债 / 已清零索引 | `docs/TECH_DEBT.md` |
| ① 入口 | 仓库定位 / 构建 / 门禁 / 分支 | 本文件（**不写状态**） |
| ② 工作流 | 假设台账 / 变更影响面清单 | `docs/HYPOTHESES.md` / `docs/IMPACT.md` |
| ② 工作流 | **第一章 121 个 HLL 的 L2/L3 达标计划与台账** | `docs/HLL-L3.md`（台账块由 `hll_hotness.py --emit` 生成） |
| ③ 知识 | 工具链入口 / 动作序列 / 跑测配方 / 日志前缀 | `docs/SDK.md` |
| ③ 知识 | 渲染后端决策（SDL2→SDL3 迁移已落地；Vulkan/Web 评估结论） | `docs/RENDER-BACKEND.md` |
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

- 上游：[nunuhara/xsystem4](https://github.com/nunuhara/xsystem4)（**仅作来源标注**；本 fork 不再跟进上游更新）
- 许可：**GPL-2.0**（见 `COPYING`）。本 fork 同样以 GPL-2.0 发布。
- 游戏本体、图像、音频等素材版权归 AliceSoft，**不包含在本仓库内**。
