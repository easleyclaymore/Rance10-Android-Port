# tools/ 地图

> 这一页回答「东西在哪、该用哪个、为什么这么分」。
>
> 目录按**角色**分层，不按"谁先造出来"堆在一起。分层的前提是
> `lib/common.sh` 这个统一锚点：所有脚本从它取路径，所以脚本本身
> 放在哪个子目录都不影响运行。

## 结构

```
tools/
├── xsys4                ← 唯一入口（新人只需要记这一个）
├── MAP.md               ← 本文
├── lib/common.sh        ← 路径锚点 + 共用函数（die / PY / need_* / find_free_display）
│
├── run/                 ← 跑测（8 个）
│   ├── run-headless.sh       Xvfb 壳：隔离 + 限内存 + 调 run-live
│   ├── run-live.sh           动作序列引擎（340 行，核心）
│   ├── run-local.sh          本机真实窗口（能看到鼠标动）
│   ├── run-rance10.sh        早期入口，保留兼容
│   ├── baseline.sh           「全默认配置」基线
│   ├── storm.sh              地图装载风暴现场
│   ├── safe_run.sh           兼容层，被 run-local / run-rance10 调
│   └── safe_run_click.sh     兼容层，被 run-local 调
│
├── build/               ← 构建（2 个）
│   ├── build.sh              离线工具：src/*.c → bin/
│   └── build-xsys.sh         从零重建（rm -rf build + meson + ninja）
│
├── env/                 ← 环境（2 个）
│   ├── dep.sh                查依赖
│   └── inst-deps.sh          装依赖（Debian 系）
│
├── src/                 ← 离线工具源码（14 个 .c）
├── offline/             ← 离线辅助（不在构建链里）
│   ├── afa_ls.py            列 .afa 压缩包条目（GBK 感知）
│   ├── pactex_dump.py       dump .pactex 全字段
│   └── alice.sh             包装外部 alice-tools，固定 GBK 编码参数
├── bin/                 ← 离线工具产物（已 gitignore）
├── dev/                 ← 开发辅助（不参与构建/跑测）
│   ├── ain_arity_check.py    批量校验 AIN 函数 arity
│   ├── rebuild_hll_table.py  重生成 HLL 表
│   └── patches/              历史补丁脚本（**只读参照，不要重跑**）
│
├── check_core_deps.py   ← 门禁（xsys4 check 调）
├── gen-switches.py      ← 生成 docs/SWITCHES.md
└── switch-desc.txt      ← 开关语义（人工维护，生成器读它）
```

**顶层只放「入口和它的直接依赖」**。判断标准：如果 `xsys4` 不直接调用它，
就不该在顶层。

## 依赖关系

```
xsys4
 ├─→ build/build.sh
 ├─→ run/run-headless.sh ─→ run/run-live.sh ─→ xdotool / Xvfb
 ├─→ check_core_deps.py + gen-switches.py
 └─→ ain_at（离线工具）

baseline.sh    ─→ run/run-headless.sh
storm.sh       ─→ run/run-headless.sh
run-local.sh   ─→ safe_run_click.sh ─→ safe_run.sh
run-rance10.sh ─→ safe_run.sh
```

## 决策表

| 我要… | 用这个 |
|---|---|
| 跑一次、出日志和截图 | `xsys4 run <tag>` |
| 跑一次带点击 | `xsys4 seq <tag> "<序列>"` |
| 在某个指令地址停下看状态 | `xsys4 dump <地址> <tag>` |
| 确认修改没破坏基线 | `run/baseline.sh` |
| 复现地图装载风暴 | `run/storm.sh` |
| 在本机真实窗口里点（要看到鼠标） | `run/run-local.sh` |
| 编离线工具 | `xsys4 build tools` |
| 换机器了，先查依赖 | `env/dep.sh` → `env/inst-deps.sh` |
| 从零重建（连 meson 一起） | `build/build-xsys.sh`（**会 `rm -rf build`**） |
| 查某个开关干什么 | `xsys4 switches <关键词>` |

## 为什么这样分

**不按功能切到 10 个目录**：跑测 / 构建 / 环境三类已覆盖全部，再细分只会
让人多点两层。

**不合并那 4 个「看着重复」的跑测脚本**：它们是不同**场景** ——
`headless`（无头）/ `local`（有窗口）/ `baseline`（固定配置）/ `storm`（压测现场）。
合并成一个带 8 个参数的脚本，调用方反而记不住。

**兼容层保留**：`safe_run.sh`、`safe_run_click.sh` 零文档引用，看着像死代码，
但被 `run-local.sh` / `run-rance10.sh` 调用。**不要因为看起来没人用就删。**

## 约定

1. **路径一律从 `lib/common.sh` 取**，不要自己算 `dirname "$0"/../..`。
   脚本里的标准开头（向上找锚点，放任何深度都不用改）：

   ```bash
   _d="$(dirname "$(realpath "$0")")"
   while [ ! -f "$_d/lib/common.sh" ] && [ "$_d" != "/" ]; do _d="$(dirname "$_d")"; done
   . "$_d/lib/common.sh"
   ```

   之后直接用 `$TOOLS_DIR` `$XSYS4_DIR` `$PORT_DIR` `$ENGINE_BIN` `$GAME_AIN`
   `$LOGS_DIR` `$OFFLINE_BIN` `$AIN_AT`。

2. **新增脚本前先问**：能不能做成 `xsys4` 的一个 verb？8 个跑测脚本里已经有
   2 个是兼容层了。

3. **不许写死绝对路径**（含家目录、架构名）。multiarch 用
   `$(uname -m)-linux-gnu`。踩过：`build-xsys.sh` 曾写死 `/home/alin/...`。

4. **调 python 前清 `PYTHONPATH`**：我们的 python 脚本只用标准库，但外部
   环境可能往里塞坏掉的 shim（实测裸 `python3` 直接 abort）。用 `PY()`。

5. **产物不入库**：`build/` 和 `tools/bin/` 都在 `.gitignore` 里。
   换机器要重跑 `xsys4 build tools`。
