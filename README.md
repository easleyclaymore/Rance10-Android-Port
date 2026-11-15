# xsystem4 · Rance10 原生移植 fork

[nunuhara/xsystem4](https://github.com/nunuhara/xsystem4) 的下游 fork：让《ランス10》
在 Linux / Android 上**原生**运行，不走 proot / 转译层。上游是 AliceSoft System4 引擎的
类 Unix 开源实现（C + SDL2 + OpenGL）。Rance10 用 **AIN v12** 字节码，在值表示、
接口派发、泛型容器、存档格式上都有上游未覆盖的语义，本 fork 用字节码与存档文件
逐条取证补齐。目标交付 arm64 APK（验收设备 realme RMX3700），当前在 Linux 桌面推进。

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

## 文档地图（每类信息只有一个源）

| 想知道 | 唯一源 |
|---|---|
| 现状 / 阶段 / 卡点 / 必读契约 / 纪律 | `../README.md`（仓库外，工作区根） |
| 未结技术债 / 已清零索引 | `../TECH_DEBT.md`（仓库外，工作区根） |
| 工具链入口 / 动作序列 / 跑测配方 / 日志前缀 | `docs/SDK.md` |
| 离线工具参数与模式 | `docs/OFFLINE.md` |
| 诊断开关总表（生成） | `docs/SWITCHES.md` |
| 项目数字（生成，禁止手写） | `docs/STATS.md` |
| 假设台账 / 变更影响面清单 | `docs/HYPOTHESES.md` / `docs/IMPACT.md` |
| HLL 缺口与战斗路径风险评估 | `docs/HLL-GAP.md` |
| 引擎移植方法论（对外成书） | `docs/GUIDE/` |
| 工程管理体系复盘（面向管理者，历史快照） | `docs/PROJECT-MANAGEMENT.md` |
| `tools/` 目录分层 | `tools/MAP.md` |
| 历史全文 / 取证链 / 阶段报告 | `../archive/`（只读） |

> **单一信息源规则**：任何当前状态、数字、契约、配置**只在上表对应的那一份里定义**；
> 其它文档需要引用时只能写链接或一句指针，不得复制一份会过期的副本。
> `PROJECT.md` 与仓库内 `README.md` 曾各维护一份状态，均已收敛（全文存 `../archive/`）。

## 上游与许可

- 上游：[nunuhara/xsystem4](https://github.com/nunuhara/xsystem4)
- 许可：**GPL-2.0**（见 `COPYING`）。本 fork 同样以 GPL-2.0 发布。
- 游戏本体、图像、音频等素材版权归 AliceSoft，**不包含在本仓库内**。
