# Rance10 Android 原生移植 · 专用仓库

本仓库是 [nunuhara/xsystem4](https://github.com/nunuhara/xsystem4) 的**下游 fork**，
目标：让《Rance10》在 Android 上以**原生**方式运行（不走 proot / 转译层），
最终交付 arm64 APK，验收设备 RMX3700。

## 状态

**进行中，尚未成功。** 当前执行流已进入 Parts UI 框架，仍会因
`AIN_IFACE` / 0x10x 扩展指令 / 对象生命周期问题中断。
开发与调试文档**唯一来源**是阿林笔记：

`/sdcard/htdocs/阿林笔记/workspace/Rance10_Android原生移植_xsystem4-20261105.md`
（仓库：<https://github.com/easleyclaymore/alinnotes>）

按项目纪律，本仓库**不复制开发文档**，只保留指针；所有捷径验证都在
主文档 `TECH_DEBT` 章节登记 `[HACK]`，主分支交付物不含 `[HACK]` 代码。

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
| `logs/` | 运行日志（含 300 MB+ trace） | 否 |

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

无头运行（Xvfb）：

```sh
xvfb-run -a -s "-screen 0 1280x720x24" \
  build/src/xsystem4 --nodebug --encoding gbk /path/to/game
```

## 可移植性门禁

```sh
python3 tools/check_core_deps.py
```

检查 Core Layer（`vm.c`/`page.c`/`heap.c`/`ffi.c`/`pal.c` 及对应头文件）
不含 Linux-only 头与硬编码绝对路径；退出码 1 表示失败。
平台服务通过 `include/pal.h` 的 PAL 接口注入，Linux 实现在 `src/pal_linux.c`，
Android 阶段提供 JNI 实现。

## 分支

- `rance10-native`：本移植主线（默认工作分支）。
- `master`：上游基线快照，仅用于对照，不接收移植改动。

## 许可

沿用上游 `COPYING`（GPL）。本仓库只含引擎源码与自写工具，
**不含任何游戏资源**；运行所需游戏文件须由使用者自行提供。
