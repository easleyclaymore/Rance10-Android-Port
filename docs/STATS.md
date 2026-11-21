# 项目数字（自动生成）

> 由 `tools/gen-doc-numbers.py` 生成，**不要手改**。改代码 / 文档后重跑一次；
> 校验用 `tools/gen-doc-numbers.py --check`（腐化即返回非 0）。
> 对应《复杂底层项目的工程管理》§6 ⑪ / 缺口 6：手写数字会腐化。

## 1. 唯一权威数字

| 指标 | 值 | 口径 | 来源 |
|---|---|---|---|
| 诊断开关 | 247 | `getenv("XSYS4_*")` 去重，与 `gen-switches.py` 同口径 | `src/**/*.c,*.h` |
| 未标注开关 | 31 | 无人工说明且无就近注释 | `docs/SWITCHES.md` |
| HLL 已实现 | 3520 | 4 个导出宏行首计数（含 RT/NRT 重载） | `src/hll/*.c` |
| HLL 缺口 | 1094 | `HLL_TODO_EXPORT(` 行首计数 | `src/hll/*.c` |
| HLL 源码覆盖率 | 76.3% (4614 个导出) | 已实现 /（已实现 + 缺口） | 计算 |
| 假设台账 | 64（待验 38 / 成立 9 / 证伪 17） | 表格行首 `H<编号>` 去重，按 section 归类 | `docs/HYPOTHESES.md` |
| 根因编号 | 111 | `D<2-3 位>` 去重 | README + TECH_DEBT + HYPOTHESES |
| 未结技术债 | 38 | 表格行首 `` `[标签]` `` 去重 | `TECH_DEBT.md` |
| 影响面清单 | 33 | 表格行首 `I<编号>` | `docs/IMPACT.md` |
| 子模块补丁 | 2 | `patches/*.patch` | `patches/` |
| 默认基线 | `d292fix2`: acts=36 / err=0 / core=0 / quest_map=1 / rc=0 | 最新 `baseline_*.txt` | `logs/` |

> **HLL 口径警告**：本表是**源码宏口径**；`PROJECT-MANAGEMENT.md` 里的
> 「2,151 / 3,351（64%）」是 **AIN 声明口径**（另一天的快照），两者不可混用。
> 对外汇报只引用本表，且注明生成日期口径。

## 2. 文档体积（不含 README 与 STATS.md，避免自指）

| 文档 | 行数 |
|---|---|
| `TECH_DEBT.md` | 157 |
| `docs/HYPOTHESES.md` | 128 |
| `docs/IMPACT.md` | 61 |
| `docs/SWITCHES.md` | 264 |
| `docs/PROJECT-MANAGEMENT.md` | 503 |

## 3. 已知腐化数字（历史，勿再引用）

| 出处 | 曾手写 | 性质 |
|---|---|---|
| `README.md` §4 | 96 个开关 | 已由本脚本注入块取代 |
| `SWITCHES.md` 历史快照 | 116 / 136 个开关 | 生成前快照；`--check` 会抓过期 |
| `PROJECT-MANAGEMENT.md` §1 | 2,151 / 3,351（64%） | AIN 声明口径，且与 §9 不一致 |
| `PROJECT-MANAGEMENT.md` §9 | 2,123 / 3,323 | 同书另一处，自相矛盾 |

> 复盘文档保留历史数字是合理的（它记录当时状态），但**状态文档与汇报禁止再引用**。
