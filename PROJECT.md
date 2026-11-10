# Rance10 Android 原生移植 · 专用仓库

本仓库是 [nunuhara/xsystem4](https://github.com/nunuhara/xsystem4) 的**下游 fork**，
目标：让《Rance10》在 Android 上以**原生**方式运行（不走 proot / 转译层），
最终交付 arm64 APK，验收设备 RMX3700。

## 状态

**进行中，尚未成功。** 启动链已跑通：`*ERROR*=0`、零未实现 HLL，
VM 进入持续主循环（`Rance10@Run → InitGlobalObject`），**但画面仍全黑**
（当前卡点 D81：第一帧渲染）。累计 105 个修复点，`[HACK]` 源码标记为 0。

开发与调试文档**唯一来源**是私有笔记仓库
<https://github.com/easleyclaymore/alinnotes>，目录
`/sdcard/htdocs/阿林笔记/workspace/兰斯10/`：

- **`Rance10_接手指南-20261110.md`** —— 新协作者**先读这份**：通道实测、日志纪律、
  复现基线、工具箱、根因索引、D81 排查步骤、提交姿势、交接检查清单。
- `Rance10_Android原生移植_xsystem4-20261105.md` —— 主文档（修复、取证、技术债）。
- `Rance10_AINv12_字节码规范-20261107.md` —— AIN v12 静态事实。

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
（见 `meson.build`；`tools/inst-deps.sh` 是本机环境备忘）。

无头运行（Xvfb）—— **必须走 `tools/safe_run.sh`**：

```sh
tools/safe_run.sh baseline 18
```

该脚本提供 20 MB 日志硬上限、独立 Xvfb 回收、`timeout -k` 直接管引擎二进制，
并在跑完自动清理 >50 MB 的历史日志。**禁止**裸跑
`timeout ... xvfb-run ... > logs/*.log`：2026-11-09 曾因此产生 124 GB 日志
（`xvfb-run` fork 的 `xsystem4` 成为孤儿进程，会话结束后继续写数小时）。
完整纪律与抓屏命令见接手指南 §3 / §4。

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
