<div align="center">

# xsystem4 · Rance10 原生移植 fork

**让《ランス10》（Rance10）在 Linux / Android 上以原生方式运行**，不走 proot、不套转译层。

本仓库是 [nunuhara/xsystem4](https://github.com/nunuhara/xsystem4) 的下游 fork。
上游是 AliceSoft **System4** 引擎在类 Unix 系统上的开源实现（C + SDL2 + OpenGL）。

</div>

---

## 这是什么

上游 `xsystem4` 能跑一部分 System4 游戏；Rance10 用的是 **AIN v12** 字节码，
在值表示、接口派发、泛型容器、存档格式上都有上游没覆盖的情况。
本 fork 的工作是：**只用字节码和存档文件，把 v12 的语义一条条取证并补齐**，
直到游戏能真正跑起来。

目标交付物是 arm64 APK（验收设备 realme RMX3700），当前先在 Linux 桌面推进。

## 当前状态

**进行中，尚未通关。** 启动链、标题界面、第一章任务地图均已跑通并出画。

| 环节 | 状态 |
|---|---|
| 构建 / 启动 | ✅ `ninja` 无 error，门禁 `check_core_deps.py` PASS |
| 平台 / 登录 / 网络确认 | ✅ 自动化跑测可稳定通过 |
| 标题界面 | ✅ 按官方说明书 `game/Manual/img/rm02.jpg` 绘制：红色圆形按钮 + 金色小按钮 + MENU（D141） |
| 标题 → 章节 | ✅ 可进第一章（后门直接调用 `Rance10@RunGameChapter1`） |
| 第一章任务地图 `SceneQuestMap` | ✅ 场景装载、对象装载、出画全通；主线推进到信息对话框（`*ERROR*=0`，32 个活动） |
| 存档 `GroupSave / GroupLoad` | ✅ 写读闭环，含泛型 `array`；支持官方 **gsave v8**，可载入 PC 原版通关档；离线检查器可验 |
| 内存 | ✅ 长跑稳定（地图装载约 370 MB，不再失控） |

**当前工作重点**：开场剧情未播（见 `[STORY-PHASE]`）与 `PartsEngine`
剩余桩函数。主线已无致命错误；开始界面已按官方说明书重绘（D141，
按钮是 `TextButton` 内建控件、不是缺失的 CG），接口派发
（`CALLMETHOD invalid function index -1`）已于 D140 修掉，根因是
`A_REF` 复制接口数组时把 vtable 偏移当成结构体引用深拷贝。

未结债务与已证伪的假设都记在根目录的 `TECH_DEBT.md`（不在本仓库内，见「文档」）。

## 这套 fork 主要改了什么

- **值表示**：`AIN_IFACE` / `AIN_WRAP` 是**两槽** `[对象, vtable 偏移]`，`nr_args`
  只数逻辑参数；泛型 `array` 由首个存入的元素定型，不能默认成 int 数组。
- **HLL 返回槽宽**：由 `CALLHLL` 第三立即数（具体元素类型）决定，**不是**声明类型。
  判错的后果是操作数栈整体错位，几层调用之外才炸。
- **接口元素数组**：2 槽/元素，字节码用 `i*2` 索引，所以**分配 / 增长 / 计数**
  三处必须同时按槽处理（少一处就会出现"数组翻倍"式的内存雪崩）。
- **存档子系统**：`system.GroupSave / GroupLoad / ExistSaveFile / DeleteSaveFile /
  CopySaveFile` 接到 `src/savedata.c`，实现 gsave 读写。
- **内存**：堆槽回收、存活对象硬上限、`XSYS4_MEM_STAT` 构成统计。
- **诊断**：一批默认关闭的 `XSYS4_*` 开关（见根目录 README 第 5 节）。

## 构建

依赖（括号内为 Debian/Ubuntu 包名）：

| 库 | 包 |
|---|---|
| meson / ninja | `meson` `ninja-build` |
| SDL2 | `libsdl2-dev` |
| GLEW | `libglew-dev` |
| freetype | `libfreetype-dev` |
| libpng / libwebp / libturbojpeg | `libpng-dev` `libwebp-dev` `libturbojpeg0-dev` |
| libsndfile | `libsndfile-dev` |
| libffi / zlib / libdeflate | `libffi-dev` `zlib1g-dev` `libdeflate-dev` |
| bison / flex | `bison` `flex` |
| cglm（可缺省） | `libcglm-dev`，缺省时 meson 自动拉取 |
| chibi-scheme（可选，调试器） | — |

```bash
git submodule init && git submodule update     # 或 clone 时加 --recurse-submodules
mkdir build && meson build && ninja -C build
```

> **构建踩坑**：桌面环境里 `PYTHONHOME` / `PYTHONPATH` 指向别的 Python 时，
> meson 会直接死在 `ModuleNotFoundError: encodings`。构建命令请把变量摘掉：
>
> ```bash
> env -u PYTHONPATH -u PYTHONHOME -u PYTHONDONTWRITEBYTECODE ninja -C build
> ```

## 运行

```bash
build/src/xsystem4 /path/to/game_directory
# 或者进入游戏目录后直接
build/src/xsystem4
```

### 跑测脚本（`tools/`）

| 脚本 | 用途 |
|---|---|
| `tools/run-headless.sh tag 150 /tmp/a.png` | **首选**：独立 Xvfb + 完整点击序列 |
| `tools/baseline.sh tag 120` | 全默认配置基线，输出六项指标 |
| `tools/storm.sh tag 60` | 直冲地图（后门调 `RunGameChapter1`，不点菜单） |

**两条硬纪律**（都是踩过 OOM 换来的）：

1. **不要在真实桌面上跑。** `run-live.sh` 会 `xdotool` 移动鼠标、`x11grab` 截屏；
   它只接受 `:90..:129` 的独立 Xvfb，其余 `DISPLAY` 直接 `exit 3`。
2. **每个入口都有内存上限。** `run-headless.sh` 与 `run-live.sh` 都默认
   `systemd-run -p MemoryMax=3G -p MemorySwapMax=0` 自包一层。

## 离线工具

`Rance10_Port/tools/`（相对本仓库的 `../tools/`）里有只读 AIN / 存档的小工具：
`ain_at`（反汇编、按名字查函数、库函数签名、结构体成员）、`gsave_dump`（存档能否读回）、
`ain_xref`（交叉引用）等。它们**不启动游戏、不需要 X**，秒级返回 —— 排查问题应当先用它们。
子模块的本地改动固化在 `tools/patches/`，`git submodule update` 后需重新应用。

```bash
tools/build.sh
tools/bin/ain_at game/Rance10.ain findsym:Chapter1@Run
tools/bin/gsave_dump logs/savedir/<tag>/GlobalSave.asd
```

## 文档

| 位置 | 内容 |
|---|---|
| `PROJECT.md` | 移植进度与里程碑（本仓库） |
| `../README.md` | 主文档：现状、跑测、必读契约、卡点 |
| `../TECH_DEBT.md` | 未结债务（A 结构性 / B 会炸的 / C 长尾） |
| `../GUIDE/` | 《黑盒字节码引擎移植实战指南》，独立成书 |
| `game/Manual/`（工作区） | 官方中文说明书；参考手册 `r01..r14.html` + `img/rm*.jpg` 是界面出画的客观基准 |
| `notes/` | 字节码规范取证笔记（AIN v12 引用槽宽等） |

## 上游与许可

- 上游：[nunuhara/xsystem4](https://github.com/nunuhara/xsystem4)
- 许可：**GPL-2.0**（见 `COPYING`）。本 fork 同样以 GPL-2.0 发布。
- 游戏本体、图像、音频等素材版权归 AliceSoft 所有，**不包含在本仓库内**。
