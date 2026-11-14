# Rance10 Android 原生移植 · 专用仓库

本仓库是 [nunuhara/xsystem4](https://github.com/nunuhara/xsystem4) 的**下游 fork**，
目标：让《Rance10》在 Android 上以**原生**方式运行（不走 proot / 转译层），
最终交付 arm64 APK，验收设备 RMX3700。

## 状态

**进行中，尚未通关。** 启动链、标题界面、第一章任务地图（含对象装载）都已跑通
并出画；存档写读闭环；内存长跑稳定；主线无 `*ERROR*`。当前在做界面与剧情的正确性。

| 环节 | 状态 |
|---|---|
| 启动链 / 平台 / 登录 | ✅ `*ERROR*=0`，零未实现 HLL |
| 标题 → 第一章 | ✅ 可用后门直接调用 `Rance10@RunGameChapter1`(fno **22579**；注意 22543 是 `GameChapter1@Run`，不是同一个) |
| 第一章任务地图 `SceneQuestMap` | ✅ 场景装载、`QuestMapObjectLoader@Load`、出画全通；推进到 `SceneInformationDialog` |
| 部队编成界面 `SceneParty` | ✅ 真实点击进出闭环（进：地图 (1819,1002)；出：右键 `OnRClick` → `Close` → `SceneStack@Exit`），退出后地图正确重绘 |
| 编队加卡 | ✅ `OnClickCard` → `RemoveCardFromFormations` / `FindEmptyIndex` + `AddCardToFormations` |
| 地图回合推进 | ⏳ 選択肢按钮能点、推进 4 级选择、已触发 `PreBattleAutoSave`；按钮整组坐标出屏，尚未进战斗 |
| 存档 `GroupSave / GroupLoad` | ✅ 泛型数组写读对称，离线检查器可验；支持官方 gsave v8 |
| 内存 | ✅ 地图装载 RSS 约 370 MB，不再失控 |

默认基线：`acts=107 / err=0 / core=0 / party=101`。

**当前工作重点**：任务地图回合推进的坐标合成 —— 父 `global.pos` 与子 `local.pos`
都是绝对屏幕坐标，`parts_update_global_pos` 无条件相加导致按钮出屏。
详见根目录 `TECH_DEBT.md` 的 `[SELECTION-BUTTON-GEOM]`。

**2026-11-14 解决的几个大问题**：

- **D188 卡片立绘**：`cmd=29` 补上 + 776 空桩转交 flat 版 + `Array.Alloc` 引用型元素清 0
  翻正 + 合成类部件 `alpha=0` 提升为 255，编队界面首次显示真实立绘。
- **D191 右键消息**：引擎从不发右键 —— AIN 的 `CallDelegateMouseClick` 靠消息第三变量
  KeyCode 挑 delegate 列表（1→this[7]、2→this[8]），`AddWholeMouseRClickEvent` 注册的是
  index 8。补上后编队界面的「戻る／決定」才通（那是**右键**，不是界面按钮）。
- **D190 `opaque` 不复位**：`ctrl_stack.opaque[]` 只在 `RemoveController` 里清，于是
  「编队层曾有不透明全屏部件」永久成立，退回地图后 `cover_layer` 卡在 2、整屏黑。
- **D194 运行时挂父不刷树 / 静态 flat 不 advance**：动态创建的 Activity 在
  `activity_load` 的那次 `PE_RefreshGlobalTree()` 之后才挂父，`global.z` 停在建树瞬间的值；
  `parts_flat_update` 的 `fps<=0` 又把首次 `needs_advance` 一起挡掉，静态 flat 画不出像素。

**2026-11-13 解决的几个大问题**：

- **D136 接口元素数组**：`AIN_IFACE` 元素是 2 槽、字节码按 `i*2` 索引，
  分配/增长/计数三处必须一致 —— 只补一处会出现"数组翻倍"式内存雪崩。
- **D138 内存风暴**：`Array.Free` 曾是空桩（当 Clear 用则不生效），
  加上 `PushBack` 传槽数而 `realloc_array` 按元素数再乘 2，进地图 RSS 涨到 9.5 GB
  触发系统 OOM。修后稳定在 370 MB。
- **D139 存档 v8**：官方通关档是 gsave version 8（解析器只认 4/5/7），
  且泛型 `AIN_ARRAY`(79) 与 `-1` 空值都被 `gsave_validate_value` 判非法，
  整个文件被拒。纳入后可直接载入 PC 原版存档。
- **D140 接口派发**：`A_REF` → `copy_page()` 复制接口数组时，奇槽（vtable 偏移）
  被当成结构体引用深拷贝，导致 `0x37CBCA` 派发失败 —— 改为奇槽原样复制，
  主线推进到信息对话框。
- **D141 开始界面**：按官方说明书 `game/Manual/img/rm02.jpg` 重绘
  （红色圆按钮 / 金色小按钮），并消除重复文字层。**按钮是 `TextButton`
  内建控件，不是未加载的 CG**（离线核对 EX 后纠正）。
- **跑测夹具**：存档目录改为显式 `--save-folder`；跑测强制独立 Xvfb + 内存上限
  （`run-live.sh` 现在拒绝在真实桌面运行），截图/`xdotool --sync` 加超时。

累计修复点 >110 个；`[HACK]` 标记集中在 `src/hll/PartsEngine.c`
（Activity 子系统整段 stub）。**主分支交付物不含 `[HACK]` 代码。**

开发文档的**唯一来源**是工作区：

- `../README.md` —— 现状、跑测、必读契约、卡点（改 VM 前先读第 4 节）
- `../TECH_DEBT.md` —— 未结债务（A 结构性 / B 会炸的 / C 长尾）
- `../GUIDE/` —— 《黑盒字节码引擎移植实战指南》，独立成书
- `docs/OFFLINE.md` —— 离线工具用法（`ain_at` / `gsave_dump` / `ain_xref`）

`notes/` 下是字节码规范取证（AIN v12 引用槽宽等静态事实）。

按项目纪律，本仓库**不复制开发文档**，只保留指针；所有捷径验证都在
主文档 `TECH_DEBT` 章节登记，主分支交付物不含 `[HACK]` 代码。

## 内容边界

| 目录 | 内容 | 是否入库 |
|---|---|---|
| `src/`, `include/`, `meson.build` | xsystem4 VM / 渲染 / HLL 源码（含本移植改动） | 是 |
| `subprojects/libsys4` | 上游 submodule，见 `.gitmodules` | 是（gitlink） |
| `tools/` | AIN 静态审计、反汇编、构建脚本、Core Layer 可移植性门禁 | 是 |
| `PROJECT.md` | 本说明 | 是 |
| `COPYING` | 上游 GPL 许可证 | 是 |
| `build/` | 本地编译产物 | 否 |
| `game/` | 游戏本体（约 3.8 GB，有版权） | 否 |
| `logs/` | 运行日志（当前约 24 MB；曾因裸 trace + 孤儿进程涨到 124 GB，已清理） | 否 |

## 获取源码

`libsys4` 是 submodule，必须递归拉取：

```sh
git clone --recurse-submodules https://github.com/easleyclaymore/Rance10-Android-Port.git
```

## 构建（Linux / chroot Debian 13，aarch64）

```sh
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
meson setup build && ninja -C build
```

依赖：SDL2、GLESv2、libffi、freetype、cglm、libdeflate、chibi-scheme 等
（见 `meson.build`；`tools/env/inst-deps.sh` 是本机环境备忘）。

无头运行 —— **必须走 `tools/run/run-headless.sh`**（自带独立 Xvfb 与内存上限）：

```sh
tools/run/run-headless.sh tagA 150 /tmp/a.png
```

也可用 `tools/run/baseline.sh tagA 120`（六项指标基线）或 `tools/run/storm.sh tagA 60`
（后门直冲地图）。三条纪律：

1. **不要在真实桌面跑**：`run-live.sh` 会 `xdotool` 点鼠标、`x11grab` 截屏，
   现在只接受 `:90..:129` 的独立 Xvfb，其余 `DISPLAY` 直接 `exit 3`。
2. **不要在无上限的情况下跑**：任一入口都默认
   `systemd-run -p MemoryMax=3G -p MemorySwapMax=0`；直接调最内层脚本曾绕过上限，
   实测把游戏放到 9.5 GB 并触发系统 OOM。
3. **不要裸跑** `timeout ... xvfb-run ... > logs/*.log`：2026-11-09 曾因此产生
   124 GB 日志（`xvfb-run` fork 的引擎成为孤儿进程，继续写数小时）。

## 可移植性门禁

```sh
python3 tools/check_core_deps.py
```

检查 Core Layer（`vm.c`/`page.c`/`heap.c`/`ffi.c`/`pal.c` 及对应头文件）
不含 Linux-only 头与硬编码绝对路径；退出码 1 表示失败。
平台服务通过 `include/pal.h` 的 PAL 接口注入，Linux 实现在 `src/pal_linux.c`，
Android 阶段提供 JNI 实现。

## 分支

- `main`（**远端默认分支**）：单提交基线（上行链路限制，历史已扁平化）。
  推送方式特殊：用 `git commit-tree` 基于 `public-baseline` 生成提交后
  `git push origin $(git rev-parse public-baseline):main`，详见接手指南 §9。
- `public-baseline`：本地单提交基线指针。
- `rance10-native`（**本地工作分支**）：完整移植历史（105 个修复点）。
- `upstream`：上游 `nunuhara/xsystem4` 基线，仅用于对照。

## 许可

沿用上游 `COPYING`（GPL）。本仓库只含引擎源码与自写工具，
**不含任何游戏资源**；运行所需游戏文件须由使用者自行提供。
