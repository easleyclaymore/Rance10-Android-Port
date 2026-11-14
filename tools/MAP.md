# tools/ 地图

> 这一页回答「我该用哪个脚本」，以及「为什么它们不合并」。
>
> **位置故意不动**：`run-live.sh` 被 2 处文档、4 个脚本引用，改名或搬家的
> 破坏面远大于收益。整理的价值在"找得到"，不在"摆得齐"。

## 全部文件按角色分四类

### 入口（唯一）

| 文件 | 作用 |
|---|---|
| `xsys4` | 统一入口。`build / check / run / seq / dump / disas / switches / debt / log / doctor` |

**新人不该直接调下面任何脚本**，先用 `xsys4`。下面的分类是给改脚本的人看的。

### 跑测核心（2 个，不要动）

| 文件 | 行数 | 作用 |
|---|---|---|
| `run-live.sh` | 340 | 动作序列引擎：解析 `RANCE10_LIVE_SEQ`、xdotool 点击、逐步截图 |
| `run-headless.sh` | 91 | Xvfb 壳：找空闲 display、限内存、切段与本机图形会话的联系，然后调 `run-live.sh` |

`run-headless.sh` 的存在理由是**隔离**：它先 `unset DISPLAY/WAYLAND_DISPLAY/XAUTHORITY`，
所以跑测不可能碰到你正在看的桌面。要跑测就用它，不要直接 `./build/src/xsystem4`。

### 跑测变体（4 个，按场景选）

| 文件 | 什么时候用 |
|---|---|
| `baseline.sh` | 验证"这次修改没破坏基线"：清掉所有 `XSYS4_*`，用固定序列跑一次 |
| `storm.sh` | 复现「地图装载风暴」（内存/页数暴涨），不依赖点击，直接进现场 |
| `run-local.sh` | 要在**本机真实窗口**里看（能看到鼠标动），调 `safe_run_click.sh` |
| `run-rance10.sh` | 早期入口，调 `safe_run.sh`，保留兼容 |

### 兼容层（2 个，看着多余但有人调）

| 文件 | 被谁调 | 为什么留着 |
|---|---|---|
| `safe_run_click.sh` | `run-local.sh` | 带点击保护的执行封装 |
| `safe_run.sh` | `run-local.sh`、`run-rance10.sh` | 带超时/清理的执行封装 |

**不要因为"看起来没人用"就删** —— 它们的调用方是另外两个脚本，不是文档。

### 构建与环境（4 个）

| 文件 | 作用 |
|---|---|
| `build.sh` | 编译离线工具（`*.c` → `../tools/bin/`）。**这是真实现** |
| `dep.sh` | 查依赖（pkg-config + gcc/bison/flex/meson/ninja 版本） |
| `inst-deps.sh` | 装依赖（Debian 系，apt） |
| `build-xsys.sh` | 从零重建：`rm -rf build` + `meson setup` + `ninja`。**注意它会删构建目录** |

> `Rance10_Port/tools/build.sh` 是同名的一层薄壳，只有 6 行，转发到这里。
> 两个同名文件容易混，但它们是**转发关系**不是冲突。

## 依赖关系

```
xsys4
 ├─→ build.sh                          （离线工具）
 ├─→ run-headless.sh ─→ run-live.sh    （跑测主链）
 │                        └─→ xdotool / Xvfb
 └─→ ain_at ...                        （离线查询）

baseline.sh   ─→ run-headless.sh
storm.sh      ─→ run-headless.sh
run-local.sh  ─→ safe_run_click.sh ─→ safe_run.sh
run-rance10.sh ─→ safe_run.sh
```

## 决策表

| 我要… | 用这个 |
|---|---|
| 跑一次、出日志和截图 | `xsys4 run <tag>` |
| 跑一次带点击 | `xsys4 seq <tag> "<序列>"` |
| 在某个指令地址停下看状态 | `xsys4 dump <地址> <tag>` |
| 确认修改没破坏基线 | `baseline.sh` |
| 复现地图装载风暴 | `storm.sh` |
| 在本机真实窗口里点（要看到鼠标） | `run-local.sh` |
| 编离线工具 | `xsys4 build tools` |
| 换机器了，先查依赖 | `dep.sh` → `inst-deps.sh` |
| 从零重建（连 meson 一起） | `build-xsys.sh`（会 `rm -rf build`） |
| 查某个开关干什么 | `xsys4 switches <关键词>` |

## 约定

1. **新增脚本前先问**：是不是能做成 `run-live.sh` 的一个动作或 `xsys4` 的一个 verb？
   现在 11 个脚本里已经有 4 个是"同一件事的不同包装"。
2. **脚本不许写死绝对路径**（含家目录、架构名）。按 `$(dirname "$0")` 定位。
   踩过的坑：`build-xsys.sh` 曾写死 `/home/alin/...` 和 `aarch64-linux-gnu`。
3. **调 python 前清 `PYTHONPATH`**：我们的 python 脚本只用标准库，但外部环境
   可能往 `PYTHONPATH` 塞坏掉的 shim（实测裸 `python3` 直接 abort）。
   `xsys4` 里的 `PY()` 函数就是干这个的，新脚本照抄。
4. **产物不入库**：`build/` 已在 `.gitignore` 里；`../tools/bin/` 在仓库外，
   天然不会被提交（但也意味着换机器要重跑 `xsys4 build tools`）。
