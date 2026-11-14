# HLL 缺口清单与战斗路径风险评估

**结论先说**：不需要「提前实现一批战斗专用库」——**不存在战斗专用 HLL 库**。
战斗逻辑跑在 AIN 内部（941 个 `Battle*` 函数），只调用 4 个通用库
（`Array` / `String` / `Math` / `Int` / `Delegate`），这些库都已实现。
全量普查后，战斗路径上真正会撞上的缺口只有 `String.ReplaceRegex`，**已补齐**。

## 复现命令

```bash
AIN=../game/Rance10.ain

# 全量 HLL 调用统计（线性扫整个 code，无截断）
tools/bin/ain_xref "$AIN" hlltop                    > /tmp/hlltop.txt

# 每个调用点 + 所属函数（AIN_XREF_MAX=0 默认全量；旧版硬限 400）
tools/bin/ain_xref "$AIN" calls 0x5a 0              > /tmp/hll_all.txt

# lib 编号 → 名字 / 导出表（用于把 lib,func 编号映射回名字）
tools/bin/ain_xref "$AIN" libs
tools/bin/ain_xref "$AIN" lib 27                    # PartsEngine

# 引擎侧未实现导出（链接期警告，每次跑测都会全量打一遍）
grep -a "Unimplemented library function" ../logs/live_*.log \
  | sed 's/.*Unimplemented library function: //' | sort -u
```

把 `hlltop` 的 `库名 函数名` 与链接警告的 `库名.函数名` 求交集，
就是「AIN 真的会调用、但引擎没实现」的清单。

## 全局数字

| 项 | 数量 |
|---|---|
| AIN 里的 HLL 库 | 36 |
| CALLHLL 调用点 | 11653 |
| 不同的 `(lib,func)` | 1443 |
| 引擎未实现导出 | 587 |
| **会被调用但没实现** | **553 个函数 / 785 处调用点** |

按库分组（命中函数数 / 调用点数）：

| 库 | 函数数 | 调用点 | 说明 |
|---|---|---|---|
| `PartsEngine` | 398 | 502 | 大部分是 getter / anchor 查询 / 编辑器用 API |
| `SealEngine` | 112 | 177 | **3D 多边形插件引擎**，不是卡牌战斗库；`Tool_*` 多为编辑器工具 |
| `MainEXFile` / `AFAFactory` | 6 / 6 | 20 / 18 | EX 归档 / 标题检索 |
| `SystemService` / `HTTPDownloader` | 9 / 8 | 16 / 16 | 平台与网络 |
| `Math` | 2 | 14 | 含 `MTRandFInclude1` 13 处 |
| 其余 | 合计 12 | 22 | 声音 / 文件 / 输入法 / 字符串 |

## 战斗路径（两种口径交叉验证）

| 口径 | 函数数 | CALLHLL 点 | 未实现命中 |
|---|---|---|---|
| 函数名含 `Battle`/`戦闘`/`Combat` | 174 | 415 | **2**（`String.ReplaceRegex`） |
| fno 区段 `24800-25600` / `26600-27300` / `36900-37100` | 328 | 722 | **2**（同一个函数） |

用到的库完全一致：`Array` 586、`String` 101、`Math` 30、`Delegate` 2、`Int` 2、`system` 1。

`ReplaceRegex` 的 4 个调用点：`BattleLog@Width::get`(27228/27229)、
`PlayerCard@ViewName::get`(28089/28090)。它们用正则剔除标记后算文本宽度，
不会阻塞流程，但会显示异常。已用 POSIX regex 实现并注册（D197）。

> **注意 `HLL_WARN_UNIMPLEMENTED` 的陷阱**：它会生成一个真实函数体，
> 但如果没登记进 `HLL_LIBRARY(...)`，C 侧仍然是 `fun = NULL`，
> 运行时照样 `VM_ERROR`。`String.ReplaceRegex` 之前正是这种状态：
> 函数体在 `String.c`，导出表里却没有它。
> 同类还有 `String.Search/SearchAll/Match/Match2` 四个（返回 `false` 的空桩，
> 不在战斗路径上，**没有**为了掩盖警告而注册）。

## 优先级建议

- **P0（已做）**：`String.ReplaceRegex` —— 战斗日志宽度的唯一真缺口。
- **P1**：`Math.MTRandFInclude1`（13 处）—— 随机种子/掉落若走它会拿到错误值，
  目前是空桩；建议在进战斗前补。
- **P2**：`PartsEngine.GetActiveParts`(12)、`Parts_GetFlatBeginSeekFrame`(7) 等
  getter —— 大量是「读回刚设过的值」，可用旁路表实现；但当前路径 `err=0`，
  说明还没被调用，可等真撞上再补。
- **不用做**：`SealEngine` 112 个、`HTTPDownloader` 8 个 —— 前者是 3D 插件
  编辑器 API，后者是下载器，正常游戏流程不会走。

## 两个容易踩的坑

1. **`ain_xref … calls` 曾硬限 400 个 site**，只扫到 `fno=833`，
   战斗函数（24000+）一个都进不来，统计结果会显示「战斗路径 0 个 HLL」。
   已改成 `AIN_XREF_MAX` 控制、默认不截断（D197）。
2. **「未实现」有两个不同的前缀**：链接期 `Unimplemented library function:`
   （`fun=NULL` 的清单）与运行期 `Unimplemented HLL function:`
   （真的被调用到了）。前者是**全集**，后者才是**当前路径实际命中**。
   排查时别把二者混为一谈。
