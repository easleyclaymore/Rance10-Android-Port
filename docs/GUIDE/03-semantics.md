# 第 3 章 语义库

> 值表示解决"数据对不对"，这一层解决"函数调没调对"。

宿主提供的能力（画图、放音、计时、读表）通过**导入函数**暴露给字节码。
这一层的错误通常报得很明确（"未实现的函数"），但也有一类完全不报错的：
**你实现了一个签名不对的函数，它跑得好好的，只是结果全错。**

## 3.1 绑定是按名字的，不是按位置

开源实现里通常有一张函数表，第 N 项对应第 N 个导入槽。**但这张表几乎总是和你的版本对不上。**

原因：引擎每加一个函数，如果插在中间，后面所有索引全移位。

判定方法：

1. 从字节码的导入表里取出每个槽的**名字字符串**
2. 用名字去查你的实现表
3. 查不到的，才是真的缺

```c
/* 正确做法：按名绑定 */
for (int i = 0; i < lib->nr_functions; i++)
    bind(lib->functions[i].name, table_lookup(lib->functions[i].name));
```

而不是：

```c
/* 错误做法：按位绑定 —— 版本一变全错，且不报错 */
for (int i = 0; i < nr; i++)
    bind(i, table[i]);
```

按位绑定的后果极难发现：函数**能调用**，参数**能传**，只是每个槽调到了隔壁的函数。
表现为"画图函数把坐标当成了颜色"这种荒谬现象。

## 3.2 一个符号常承载多种语义

为了让"泛型"写法可行，引擎会把多个同名声明绑到**同一个 C 符号**上：

```
Array.First(ref array)              -> Array_First_
Array.First(ref array, predicate)   -> Array_First_   ← 同一个符号
```

C 侧的 `Array_First_(struct page **a, uintptr_t predicate)` 只看到"有个第二参数"，
但第二次实参在只有 1 个参数时是**寄存器里的垃圾**。

判定方法：**看被调函数的声明，不看 C 签名。**

```c
/* 是否带谓词，由当前被调函数的声明决定 */
static bool has_predicate(void) {
    int fno = hll_current_fno();               /* 谁在调我 */
    decl = &lib->functions[fno];
    return decl->nr_arguments > 0 &&
           decl->arguments[decl->nr_arguments - 1].type == HLL_FUNC;
}
```

这类重载在数组/容器库里非常密集（`First/Last/Any/All/Min/Max/Sort/Find/EraseAll`），
**一个都不能漏**。漏一个的症状是"某个界面偶尔显示第一条数据"。

### 兰斯10 的实锤：同名重载被绑到了错的那个实现

`lib[30] String` 有两个 `GetPart`：

```
[21] GetPart(ref string, Index)            → 从 Index 截到末尾
[22] GetPart(ref string, Index, Length)    → 才带长度
```

而导出表里两条都叫 `GetPart`，链接器"按名字取第一个"：

```c
HLL_EXPORT(GetPart, String_GetPartLen),      /* 只有这一条，是 3 参实现 */
HLL_EXPORT(GetPartLen, String_GetPartLen),
```

于是 2 参调用把第三个参数读成**寄存器里的垃圾**。它的症状不是崩溃，是：

```
[getpart] in="Tab10" pos=3 -> "1"        ← 应该是 "10"
```

`Tab10` 被当成 `Tab1`，`m_btn[0]` 初始化两次、`m_btn[9]` 永远为空。
界面上看起来只是"第 10 个页签没反应"。

修法是给导出表加参数个数，按 arity 匹配：

```c
HLL_EXPORT_N(GetPart, String_GetPart,    2),
HLL_EXPORT_N(GetPart, String_GetPartLen, 3),
```

```c
/* 链接时打分：arity 相等 2 分，未标注 1 分，明确不等 0 分；取最高分 */
```

**教训**：绑定按名字（3.1）是对的，但**同名不同 arity 必须再分一层**。
只按名字绑，会把"3 参实现"接到"2 参调用"上，而 C 侧完全无从知道自己多读了一个参数。
判据：`XSYS4_STR_INT_DBG=1`，打印每个 `GetPart` 的输入/输出。

## 3.3 签名错配：三种最常见的形态

### ① out-param vs 返回值

开源实现（多为编辑器/工具链写的）常用 out-param：

```c
void GetName(int handle, struct string **out, int state);
```

而游戏里声明是返回值：

```
string GetName(int handle, int state)
```

`libffi` 会把你传的 `state` 当成 `out` 指针，往一个整数里写指针 —— 然后返回 `0x1` 当字符串。

**症状**：某个字符串字段是 `0x1`，第一次 `%s` 时崩。

**判定**：声明返回 string 却拿到小于 0x1000 的"指针"，一律判为签名错配。
把这条检查写进返回值的公共路径，它会自动帮你点名所有错配的函数。

### ② 参数少一个 / 多一个

`GetPartsFlatName(parts_no, state)` vs `GetName(handle)` —— 少一个参数，第二个实参被忽略，
函数返回"上一次的状态"，看起来偶尔对偶尔错。

### ③ 宽窄不符

声明 `int`（32 位）但 C 侧是 `float`，或者反过来。在 x86_64 上 `libffi` 会按声明类型打包，
`float` 走 SSE 寄存器，`int` 走通用寄存器 —— 拿到的是完全无关的值。

**这三类的共同点：不会报错。** 所以要在返回值/出参上加类型校验，而不是等它崩。

## 3.4 版本分派：同一指令，两种语义

引擎升级时常给指令加立即数，但老指令的字节布局不变，于是**同一条指令在不同版本里语义不同**：

```
S_MOD       v<=8: 弹 2 个     v>8:  弹 3 个（多一个模数字面量）
OBJSWAP     v<=8: 弹 3 个     v>8:  弹 5 个
DG_STR_TO_METHOD  v<11: 弹 2 个     v>=11: 有立即数，弹 1 个
```

写法：

```c
if (ain->version >= 11) {
    dg_no = arg(0); str = pop();
} else {
    dg_no = pop();  str = pop();
}
```

**判定方法**：当某个函数的行为"像是少读/多读了一个值"时，先查这条指令有没有版本分支。

一个具体信号：栈平衡探针在**特定几个函数**上稳定差 1，而这几个函数都用同一条指令 —— 基本可以确定。

## 3.5 数据与编码

配置表、资源包、字符串表，往往有几个坑：

| 坑 | 表现 | 判定 |
|---|---|---|
| 编码不是你以为的那个 | 名字查找全部失败，但控制台打印是乱码 | 用十六进制对比"你要找的名字"和"表里的名字" |
| 路径分隔符是**全角**的 | `a\b` 找不到，`a／b` 能找到 | 打印条目名的字节 |
| 表名是容器名而非叶子名 | 顶层查得到，子节点查不到 | 逐层 dump |

案例：某引擎的 flat 素材变体用**全角 `／`** 连接（`<组名>／一般`），
而且整串是 GBK 字节。用 `\` 试了两次都失败，改用全角后一次命中。

**这类问题的排查成本几乎全部在"想到这一层"**，一旦想到，两分钟解决。
所以：**遇到"按名字找不到"，先把两边的字节都打出来。**

## 3.6 "未实现"的判定：是没实现，还是没调用到

日志里出现"未实现的函数"时，先别急着写实现。分三种情况：

| 情况 | 判据 | 处理 |
|---|---|---|
| 真的缺 | 每次执行到该路径都报 | 实现 |
| 只在异常分支用 | 正常流程从不报，只在错误处理时报 | 可以返回默认值 + 警告 |
| 名字对但签名错 | 不报"未实现"，但结果不对 | 回看 3.3 |

区分方法：给这个函数加 `XSYS4_CALL_TRACE` 之类的打点，观察它在**正常流程**里被调用多少次。
0 次 = 你现在不需要它。

这一条能省掉大量"实现了但没人用"的浪费。我在项目里实现过 6 个数组函数，
其中 3 个（`Concat/Duplicate/Shuffle`）至今调用次数为 0 —— 它们不亏，但不该优先做。

## 3.7 这一层的验收标准：「解决」的四级定义

「这个函数算解决了吗」有四个互不包含的层级。把它们混成一个数字，
就会得到「覆盖率 73%，卡点却全是已实现的函数」这种局面。

| 级 | 名字 | 含义 | 可观测判据 |
|---|---|---|---|
| **L0** | 注册 | 名字绑上了，能调用、不崩、栈净变化正确 | 启动无 `Unimplemented library function`；调用前后 VM 栈槽数差 = 返回值槽宽 |
| **L1** | 类型合法 | 返回值类目与声明一致，引用不悬空 | 无 `BAD-SLOT-REF`／类型告警；ref 返回后对象序号仍有效 |
| **L2** | 单点语义 | 对同一输入，返回值符合**调用方**的期望 | 反汇编调用点看值被怎么用；或对照上游 C 实现 |
| **L3** | 副作用联动 | setter 不只存值，还触发该有的联动（重排层、置脏、刷新子树、发消息） | 调用后相关状态确实变了：层归属／global z／dirty／子树 |

本项目三次踩坑刚好各占一层，且**都不影响 L0/L1**，所以覆盖率一直显示正常：

- `GetActiveParts` 恒返 0 —— **L2 错**（覆盖表里算"已实现"）
- `SetActiveController` 空实现 —— **L3 缺**（调用 6741 次，连存值都没做）
- `Parts_GetParentPartsNumber` 恒返 0 —— **L2 + L3**

L2 的判据只有三种来源，缺一不可：

1. **调用点反汇编** —— 同一个值当终止符、当 ID、当下标，语义完全不同（见 3.8）
2. **上游实现** —— system4 是开源的，能对照就别猜
3. **场景判别** —— 一次跑测里该变的东西变了（截图／状态），否则不算

getter 到 L2 即止；**setter 必须做到 L3**，因为调用方等的是副作用。

L0 的工程细则仍需满足：

1. 没有"未实现"报错，且每条缺失都经过"是否在真实路径上"的判定
2. 返回字符串的函数全部经过指针合法性校验
3. 按名绑定，且绑定失败会打印缺哪个名字
4. 数组／容器库的重载全部按声明分派

**路径级与函数级是两件事。** 121 个函数全部 L0，不代表一章能跑通；
反过来，一个 L3 错的 setter 就能让整章卡死。所以对外报两个数：
「本章路径调用的函数里，有 L2／L3 证据的占比」+「当前卡点数」——
不是「已实现 / 总导出」。

## 3.8 成对出现的 ID / index API 是一条隐式契约

引擎若同时暴露 `GetXxxID(index)` 和 `GetXxxIndex(id)`，就说明**两者不是同一个数**。
把它实现成恒等映射，短期内一切正常，直到容器中间被删过一次：下标整体左移，
而程序记住的 ID 还是旧值。

识别这类债的方法不是读实现，而是找一处**「记住返回值、过一会儿再比较」**的代码：

```
IsActive = (GetActiveLayer() == this.LayerPartsNumber)
```

一旦看到这种写法，就要问：这两个值在中间发生删除/插入后还相等吗？
答案若是否定的，ID 就必须是单调递增、不复用、与物理位置无关的编号。

修的时候把边界划清楚，不要改成「到处都用 ID」：

- **跨 ABI 边界**（创建时返回、查询时返回、按编号设置/删除/显示隐藏）一律用 ID；
- **引擎内部**（渲染排序、命中遍历、可见性数组索引）继续用 index；
- 删除中间项后，内部记的物理下标要同步左移，否则分层渲染会错位；
- 对已经失效的迟到 ID **降级忽略，不要抛致命错误**——场景重建时这是正常事件。

还要接受一件事：修好 ID 漂移往往**不会**顺带解决你最初追的那个现象。
它是结构性正确性，不是那个 bug 的直接根因。把两件事分开记录，别合并成一条「已修复」。

## 3.9 提前 return 的分支，调用方还在等副作用吗

重实现 VM 时，调用约定里最容易漏的不是"参数怎么传"，而是
**"这次调用到底有没有发生"**。

兰斯10 的 AIN v12 里，遇到 `fno=0` 空函数会走 no-op 分支：把指令指针设成
返回地址就直接 return，**不创建栈帧**。但方法调用的公共收尾代码
并不知道这件事，照常弹出 `this`、设置当前帧的对象页、把 `this`
写进局部变量——这些操作全部落在了**调用者的帧**上，它的 `this`
被悄悄换成了另一个对象。

症状很难直接联想到原因：后面成片"访问结构体第 N 个成员越界"，
而且越界对象总指向同一个早期分配的单例。取证时很容易一路追到
"对象生命周期"或"槽宽错位"上去（本案两轮都走错了），最后靠
"在方法调用处打印 nargs / 实际槽数 / obj / 栈快照"才看到
`fno=0 NULL` 这条调用。

通用做法：

- 凡是短路（no-op / 空实现 / 提前 return）的分支，**必须给调用方一个
  可观测的状态**：帧计数有没有变、句柄是否有效，别让调用方靠猜
- 调用方在收尾前先确认"副作用确实发生了"，再写共享状态
- 证伪顺序：先打印调用点的栈布局与槽数，确认参数没问题；
  再检查"这次调用有没有建帧"。别一上来就怀疑对象生命周期

判据也有坑：这类修复做同二进制 A/B 时，该看的是**流程推进到哪一步**
（装载的组件数、到达的场景），不是越界告警条数——推进得更深时，
告警反而可能更多（本案 16 条变 20 条，但装载的组件从 107 涨到 118）。

---

## 3.10 同名重载只实现了一个，比不实现更危险

兰斯10 的 `Array.Find` 在库表里有 **8 个重载**：

```
int Find(ref array<?> self, hll_param Search);            ← 按值
int Find(ref array<?> self, int Begin, int End, hll_param Search);
int Find(ref array<?> self, hll_func Func);               ← 按谓词
int Find(ref array<?> self, int Begin, int End, hll_func Func);
        （FindLast 同样四个）
```

我们的 C 实现只认前两个（还靠硬编码 `fno == 37 || fno == 41` 区分是否带区间）。
谓词版没有分支，于是闭包参数 `[this, fno]` 被当成普通值丢进 `elem_eq`，
跟数组元素永远比不上 —— **返回 -1，而且不报任何错**。

后果不是"编队点不动"，而是：

```
LeaderCardCollection@GetEmptyIndex()          = m_leaders.Find(l => !l.IsExist())  → -1
Party@FindEmptyIndex()                        → index(-1) < m_availableCount(24) 成立
                                                但返回的就是 -1
SceneParty@OnClickCard()                      → if (emptyIndex != -1) AddCardToFormations(...)
                                                                  ↑ 永远为假
```

上层每个 `!= -1` 判断都走了「没找到」分支，日志干净、0 ERROR、
活动数 107、点击回调各命中一次 —— 所有指标都"正常"，只有功能是坏的。

**判据一**：实现任何同名函数前，先把库表里同名项全列出来逐个对签名：

```bash
grep -n "int Find(" libs.txt      # 8 条，一眼看到 hll_func 那两版
```

**判据二**：用一个不报错的默认值（-1、0、空串）兜底之前，先问
"上层是拿它跟什么比" —— 如果上层写的是 `if (x != -1)`，那这个默认值
等于替上层决定了业务分支。

**判据三**：`-1` 和「真的找不到」在日志里长得一模一样。要区分，
必须在**判定点**打断点（`XSYS4_DUMP_AT=<LT 指令地址>`），而不是看返回值。

## 3.11 注册的名字，和库表里的名字

这一条是同一天踩的第二个坑，形态完全一样：

我们注册了 `HLL_EXPORT(SetNumeralNumber, PE_SetNumeralNumber)`、
`GetNumeralNumber`、`SetNumeralShowComma`、`SetNumeralSpace`……一整套，
看起来 numeral 系统"已经实现了"。

但 `libs.txt` 里它们的真名是：

```c
bool Parts_SetNumeralNumber(int Number, int Number, int State);
int  Parts_GetNumeralNumber(int Number, int State);
bool Parts_SetNumeralShowComma(int Number, bool ShowComma, int State);
```

**全带 `Parts_` 前缀。** 绑定是 `strcmp` 精确匹配，所以这十几条注册
一个都没生效 —— 它们是**死条目**，真正被调用的那几个一直躺在
"595 个未实现导出"的清单里。系统看起来能用，只是因为调用它们的路径
（任务地图的 box 视图）从来没有被真正走到过。

**判据**：新增导出时，名字必须从库表原文复制，不能按 C 函数名去猜。
已经写了一整套导出之后，用一条命令自查：

```bash
# 我们注册的每个名字，是否真的在库表里
grep -o 'HLL_EXPORT([A-Za-z_0-9]*, *PE_[A-Za-z_0-9]*)' src/hll/*.c \
  | sed 's/HLL_EXPORT(\([^,]*\).*/\1/' | sort -u > /tmp/ours.txt
```

再和 `ain dump -l` 的成员名求差集。差集里那些，就是"以为实现了、
其实一条都没绑上"的条目。这个自查花两分钟，能省掉几天"为什么这个
setter 好像没效果"。

## 3.12 把一个开关实现成另一个开关，比留空桩更糟

`PartsEngine.SetEnableInputProcess(int Number, bool enable)` 一直是空桩。
名字看着就是「这个按钮能不能点」，而我们的输入层判的是：

```c
bool click_eligible = parts->clickable || !parts->pass_cursor;
```

于是很自然地把空桩换成 `PE_SetClickable(no, enable)` —— 一行，语义看着天衣无缝。

**结果整局从 107 个活动掉到 27 个，编队界面再也进不去。**

加一个默认关的探针数一下参数的分布，一眼就清楚了：

```bash
XSYS4_DBG_EIP=1 ...
# [eip] no=900000395 enable=0
# [eip] no=900000396 enable=0
```

242 次调用里 **238 次是 `enable=0`** —— AIN 在活动初始化时把所有部件
「先关一遍」，之后靠别的 API 逐个打开（或者根本不打开，因为它只是想
让某些部件不参与派发）。我们把这一遍照单全收地写进 `clickable`，
等于把活动文件里 `点击許可` 声明过的按钮全部关掉。

**这两件事不是一回事**：

| 概念 | 谁拥有 | 作用 |
|---|---|---|
| `点击許可`（`clickable`） | 活动文件 | 这个部件**是按钮** |
| `SetEnableInputProcess` | AIN 运行时 | AIN **要不要往它派发输入** |

一个按钮可以「是按钮但现在不响应」；一个非按钮也可以被临时打开输入处理。
把后者写成前者，就把两种状态压成了一种，必然丢掉信息。

**判据一**：实现一个"看起来很明显"的开关之前，先数参数分布。
加一个 `XSYS4_DBG_xxx` 默认关的探针，跑一遍，看 true/false 的比例。
如果某一侧占 95% 以上，先怀疑自己理解错了极性或语义，而不是先高兴"实现好了"。

**判据二**：空桩不是零成本，但**猜错的实现是负成本**。空桩保持既有状态；
猜错的实现会把原本能用的功能一起关掉，而且症状出现在很远的地方
（"编队进不去了"），排查成本远高于一开始就留着 `(void)no; (void)enable;`。

**判据三**：判断一个 HLL 函数该不该实现，看**有没有人在别处补偿它**。
如果留着空桩时功能是好的，说明有别的路径在兜底；这时"补上实现"反而
是在两条路之间打架。

这一轮的最终决定是把空桩**恢复**，并把探针留着（默认关）——
下次有人再觉得"这条明明可以一行实现"时，能在两分钟内看到 238:4 的分布。

## 3.14 委托 foreach 是「两条指令 + 一个回跳」

AIN 里遍历一个委托链的标准写法只有三行：

```
DG_CALLBEGIN n          ; [dg_page, args…] → [args…, dg_page, 0]
DG_CALL      n, END     ; 调第 index 条，index++；越界时 ip = END
JUMP         DG_CALL
```

`DG_CALLBEGIN` 把 `dg_index` 压成 0；`DG_CALL` 每执行一次把栈上的 index 加一；
`delegate_call()` 在 `delegate_get()` 取不到条目（index 越界）时执行
`instr_ptr = get_argument(1)` —— **第二个操作数就是循环出口**。
所以那行 `JUMP` 回到 `DG_CALL` 不是死循环，是 foreach 的下一轮。

**这里有个陷阱。** `DG_CALL` 宽 10、有两个操作数，而 `--disasm` 曾只打第一个
（`arg=1034`），肉眼很容易把它读成「无限重复调用同一个委托」。
`Scenes::RunISceneWithAssistant` 就是这么被误读的 —— 它实际的形状是：

```
stack = new SceneStack()
foreach (f in lambda) f(stack)     ; 建场景
stack.WaitFrame()                  ; 节流 4 帧
…                                  ; 场景运行
```

真正把控制权交还给玩家、并且一直阻塞到场景结束的，是 lambda 里那句
`stack.Join()`（见 4.5.1）。**`RunISceneWithAssistant` 自己不检查 `IsFinish`。**
所以「场景结束」之后要发生什么，取决于它返回后调用方的那条 AIN 语句，
不在执行器里 —— 找错地方会一直盯着一个不该负责的函数。


**名字勘误**：这个项目里**没有**名为 `Scenes::Run` 的函数。26379 与 26385 都是
`Scenes::RunIScene`（后者是带返回值的那份），26386 才是
`Scenes::RunISceneWithAssistant` —— 它开头那句 `22687()`
（`AssistantView@ParentPartsNumber::get`）就是在给助手视图找父容器。
把 26386 当成「场景执行器本体」会一直找不到 `IsFinish` 的消费点。

## 3.15 主线不是一条链，是「章节状态机 + 地图事件分发」

从字节码扫出来的入口层只有四层：

| 层 | 函数 | 作用 |
|---|---|---|
| 主循环 | `Rance10@RunLocalGame` | 最外层 |
| 章节 | `GameChapter1/2/3@Run` | 相位 `switch`，每轮挑一个界面进去 |
| 地图 | `QuestMapSystemEventDispatcher::Run` | 地图上的事件（战斗/剧情/选关） |
| 场景 | `Scenes::RunXxx` / `QuestScenes::RunXxx` | 单个界面，内部 `Join` 阻塞 |

`GameChapter1@Run` 能进的界面只有五个：`RunResult`、`RunTurnStart`、
`RunFoodTicket`、`RunBrave`、`RunStackedMapQuest`。**它没有直接调
`RunBattleSceneSet`** —— 战斗走的是
`GameChapter1@SelectQuest` → `QuestMapSystemEventDispatcher::Run` 这条支线。

所以「点完回合开始之后去哪」，答案在 `GameChapterN@Run` 的下一轮 switch，
既不在 `RunISceneWithAssistant` 里，也不在 `SceneTurnStart` 自己身上。

## 3.16 第一章的相位表：回合开始是 3，选关是 4/5

`GameChapter1@Run` 的分派是 `SWITCH 313`，case 表 17 项、无默认分支。
用 `--dump-switch 313` 把表倒出来，再按 **case 地址**切段，得到的是权威版本：

| phase | case 地址 | 这一段做什么 |
|---|---|---|
| 0 | `33ec56` | 初始化：`Turn=0`，转 1 |
| 1 | `33ec8c` | `RunStackedMapQuest` + `OpenQuest`（地图） |
| 2 | `33ecf0` | `RunResult`（战况） |
| **3** | `33ed2c` | **`RunTurnStart`**（回合开始屏） |
| 4 | `33ed7e` | `QuestList.Load(3)` + `RunMultiQuest`（多选关） |
| 5 | `33edd6` | `UpdateCountryAdmiral` + `SelectQuest(0)`（单选关） |
| 6 / 8 / 9 / 11–13 / 15 | — | 账务、判定之类，无场景切换 |
| 7 / 10 | `33ee5e` / `33ef0a` | `RunFoodTicket`（食券） |
| 14 | `33f00e` | `RunResult`（战况） |
| 16 | `33f08a` | `RunBrave`（勇者），之后回 1 |

**上一版这张表整体错位了一格**，因为我是按「`Phase = N` 赋值语句的地址」切段的。
`Phase = N` 是 case 的**结尾**，case 的**开头**在 switch 表里 ——
两者差一个 case。判据是 `--dump-switch`，不是赋值语句的位置。

这张表也把「卡在回合开始屏」的范围说清了：phase 3 的 `RunTurnStart` 不返回，
phase 4 之后一行都跑不到 —— 没有选关、没有地图、没有战斗。
不是某个界面坏了，是整条链停在原地。

**复用的读法**：`--dump-switch <no>` 拿 case 表 → 按 case 地址升序切段 →
逐段列 `CALLFUNC` 的 `Scenes::Run*` / `QuestScenes::Run*`。
`SWITCH` 的操作数是**编号**（`ain->switches[no]`），不是 case 数，也不是地址。

## 3.17 一回合的闭合点：`Turn++` 之后回到 phase 3

`GameChapter1@Run` 里 `Turn` 只在两处被写：

| 地址 | 写什么 |
|---|---|
| `0x33ec58`（case 0） | `Turn = 0`（初始化） |
| `0x33f0a8`（case 16 末尾） | `Turn = Turn + 1` |

`Phase` 的写入序列是 `1,2,…,16,3` —— 最后那个 `3` 就在 `Turn++` 之后两条指令。
所以一回合的闭环只有这一条：

```
3 回合开始 → 4/5 选关 → 7/10 食券 → 14 战况 → 16 勇者 → Turn++ → Phase = 3
```

没有第二个入口能回到回合开始，也没有第二个地方能改回合数。这解释了两件事：

- 卡在 phase 3 时 `Turn` 永远不涨 —— 跑测里「回合数不动」不是显示问题，是真没走
- 存档里的 `Turn` 与这里的 `Turn::set` 是同一份数据；「改存档跳关」等于直接改它的结果

case 6 / 8 / 9 / 11–13 / 15 里既没有 `Scenes::Run*` 也没有 `Turn::set`，
只有 `CALLMETHOD` 的字段更新，所以它们是账务与判定段，不会切换场景、也不推进回合。

## 3.18 三章的相位空间：共享骨架 + 各自菜单区

| 章 | switch | case 数 | Phase 取值 |
|---|---|---|---|
| 第一章 | `313` | 17 | `1..16`，回环到 `3` |
| 第二章 | `314` | 7 | `3, 4, 1000, 1001, 1002, 1003, 13` |
| 第三章 | `315` | 2 | `2000, 2001` |

**`3` / `4` / `13` 是共享骨架**：回合开始（`RunTurnStart`）、回合后处理、结算。
各章自己的菜单用自己的号段：

| 章 | 地图 | 商店 | 好友 | 挑战 |
|---|---|---|---|---|
| 1 | phase 1 | — | — | — |
| 2 | 1001 | 1002 | 1003 | — |
| 3 | 2001 | — | — | 2000 |

第一章因为是首个章节，把 `5-16` 整段拿去做了线性流水（选关 / 食券 / 战况 / 勇者），商店与好友是第二章才有的入口。

**上一版把第一章的商店/好友标成「未定位」是扫描区间划错了**：`0x33f0fa` 之后不是`GameChapter1@Run` 的收尾，而是 `RunMultiQuest`(22544) 与 `RunSingleQuest`(22545) 两个独立函数。把 0x33f61a 之后也算进第一章，就会把第二章的 `RunShop`/`RunFriend` 误记到第一章。

所以找「某个界面从哪进」时，第一步是**判断它属于哪一章**，第二歩才看那章的
switch 表；`switch 313 / 314 / 315` 分别对应 `GameChapter1/2/3@Run`。
对着错的 switch 表找 case，会得到一张看似合理但完全错位的图。

## 3.19 选关：两个入口，一处分叉

第一章 phase 4/5 分别调两个入口，它们只在开头一句不同：

```
RunSingleQuest(id):
    if (!GameContext.IsExistRestoreInfo()) RunStackedMapQuest()
    else                                   OpenQuest(id)

RunMultiQuest():
    if (!GameContext.IsExistRestoreInfo()) RunStackedMapQuest()
    while (true) {
        QuestList.EraseFront()
        if (QuestList.IsEmpty()) break
        OpenQuest(QuestList.Front)
    }
```

`GameContext@IsExistRestoreInfo` 就是「读档后有没有待恢复的进度」。
**有就跳过地图、直接进上次那一关；没有就先跑地图。** 这是「续玩」与「新回合」
唯一的分叉点，也是为什么读档能绕过地图直接落回原处。

`OpenQuest(quest, pre, post)` 也不是「打开界面」，而是先拼委托再交给场景模板：

```
scene = new SceneQuestSceneSet()
scene.m0 += pre              ; DG_PLUSA —— 委托数组拼接
scene.Run(quest, post)
```

`SceneQuestSceneSet@Run`(23645) 是**标准场景 Run 模板**：建 stack → 跑前置委托 →
场景体 → `Join`。每个场景 `Run` 结尾那句 `X.Join()` 都是这个模板的产物。

两处细节：

- `SceneQuestSelect@GetResult` = `return m4[m5]`（选中项数组 + 索引）
- `SceneQuestSelect@Run` 的 stack 在 **m15**，而 `SceneTurnStart` 在 **m1** ——
  每个场景类自己决定把 `SceneStack` 放哪个字段，不能假设「统一在 m1」

## 3.20 `SceneQuestSceneSet` 的单跑与堆叠跑

场景模板有两个入口，区别只是「跑一次」还是「把队列跑空」：

```
Run(quest, reset, post):                     ; 23645
    if (reset) ResetParm()                   ; 23654
    if (!IsRegisterQuest(quest)) return 1    ; 23649 —— 任务未解锁
    r = RunRegisteredQuest(quest, -1)        ; 23648
    if (reset) ResetParm()
    return r

RunStacked():                                ; 23646
    loop:
        if (!GameContext.IsExistRestoreInfo()) break
        q = GameContext.GetRestoreInfo()     ; 26140 取队首
        GameContext.PopRestoreInfo()         ; 26142 弹出
        RunRegisteredQuest(q, …)
    ResetParm()
```

`RestoreInfo` 是一个**队列**：`GetRestoreInfo` 取队首、`PopRestoreInfo` 弹出。
`RunStacked` 就是「只要队列非空就接着跑下一条」。

所以两条路分工明确：

- `OpenQuest` → `Run` → 跑**一个**任务
- `RunStackedMapQuest` → `RunStacked` → 把**待恢复队列**跑到空

`IsRegisterQuest` 为假时 `Run` 直接返回 `1`，**不抛错、也不开界面**。
这就是「点了一个没解锁的任务、屏幕上什么也没发生」的来源 ——
不是点击丢了，是这一层静默返回了。

## 3.21 一个任务的三路分发，以及战斗的唯一入口

`RunRegisteredQuest(quest)`(23648) 是「一个任务怎么跑」的分发点：

```
q = findQuestFromId(quest)                  ; 26467
if (q.IsQuestDataExist()) {                 ; 28367
    if (q.IsMapQuest() && q.IsMapExist())   ; 28365 / 28366
        RunMapQuest(…)                      ; 26314 —— 地图任务
    else
        RunAdvQuest(…)                      ; 26315 —— 剧情任务
} else {
    AddProcessList(ShowDialog)              ; 23647 ——「任务数据不存在」
}
ProcessPostEvent(…)                         ; 23651 —— 收尾
```

两条腿各跑一轮场景：

- `RunMapQuest` 跑 `SceneQuestMap`，结束后读 `FinishType`(23583) 决定下一步
- `RunAdvQuest` 跑剧情，用 `SceneAdvQuestSelection@GetResult`(23871) 取玩家选择

**战斗只有一条入口**：

```
Scenes::RunBattleSceneSet(a, b)             ; 26388
  → new BattleSceneSet().Open(a, b)         ; 27582
    → BattleSceneSet.RunBattle()            ; 27585
      → RunIScene(new SceneBattle)
```

`SceneBattle@Run`(24882) 与 `SceneTurnStart@Run` 一样：**零 CALLMETHOD、零 PUSH**。
它是活动方法，只能由 `RunIScene` 的工厂 lambda 造出来。所以「进战斗」在 AIN 层
**只有 `RunBattleSceneSet` 一个入口**，不存在第二条路 —— 排查「为什么没进战斗」时
只要看这一条链有没有走到，不必去找别的分支。

## 3.22 战斗：从 Open 到 GameOver

`BattleSceneSet@Open`(27582) → `RunBattle`(27585) → 按类型分两条：

```
RunBattle():
    if (…)
        return new BattleSceneSetSimple().Run()   ; 类 979
    else
        return new BattleSceneSetNormal().Run()   ; 类 978
```

`BattleSceneSetNormal@Process`(27588) 是一场完整战斗的全过程：

```
SnapShotSaveManager.Save(…)          ; 26444 —— 开局存快照（胜负后可重试）
BattleScenes.RunBattle()             ; 25945 —— 战斗主循环
BattleBonusCalculator.Calc()         ; 24818 —— 战利品
Music.FadeOut()
switch (BattleResult.Type) {         ; SWITCH 431
    胜: if (章节匹配) BattleScenes.RunBattleResult()
    负: Scenes.RunGameOver()         ; 26411
}
```

**`Open` 里有一条显式的失败分支**：`BattleInitializer::Init`(24823) 返回假时走
`DialogScenes::RunTestDialog`(26046)。也就是说**战斗打不开会弹对话框，而不是静默卡住**。

这条判据很实用：如果「战斗没进、也没有弹框」，那说明连 `Open` 都没走到，
问题在它上游（`RunBattleSceneSet` → `BattleSceneSet.Open` 这一段），
不必去怀疑战斗内部。

`BattleInitializer::Init` 本体很短：`Init`(24821) → `EnemyCreator::Create`(24853) →
读 `BattleResult.Type`(27289) / `BattleContext.GetPlayer().Hp`(26640) 判胜负初值。

## 3.23 `BattleScenes`：战斗被拆成八个场景

`BattleScenes::RunBattle`(25945) 本身只有十几字节，是一个 `RunIScene` 包装：

```
RunBattle() = RunIScene(dg(lambda: new 733))
```

战斗的每个阶段各自是一个独立场景，各有自己的 `RunXxx`：

| fno | 入口 | 作用 |
|---|---|---|
| 25945 | `RunBattle` | 战斗本体（`SceneBattle`，类 733） |
| 25946 | `RunBattlePhase` | 回合阶段推进 |
| 25947 | `RunBattleResult` | 战斗结果画面 |
| 25948 | `RunLowerLog` | 战报滚动 |
| 25949 | `RunWinLoseView` | 胜 / 负画面 |
| 25950 | `RunBattleInformation` | 战斗情报 |
| 25951 | `RunSimpleBattleInformation` | 简易战斗情报 |
| 25952 | `RunSimpleBattle` | 简易战斗本体 |
| 25953 | `RunSimpleBattleResult` | 简易战斗结果 |

`BattleResult.Type` 只有两个值（`SWITCH 431`）：`1` = 胜、`2` = 负。
`Normal@Process` 的 `switch` 就两项 —— **没有平局、撤退或超时之类的第三态**。
所以「战斗结束但两个 case 都没进」这种情况不存在，出现异常只可能是
`BattleResult.Type` 本身没被写。

## 3.24 `SceneBattle@Run` 的启动序列

`SceneBattle@Run`(24882) 的初始化是「先建视图、再开交互、最后重置卡」：

```
BattleSkillSelector.FadeIn(dg)            ; 25164
BattleApView.FadeIn(dg)                   ; 25013
BattleRoundView.FadeIn(…)                 ; 24835
BattleHpBar.FadeIn(dg) ×2                 ; 25111
Overlay.FadeIn(…)                         ; 22639
BattleSkillSelector.SetControlEnable(1)   ; 25173  ← 先允许操作
SnapShotSaveManager.Save()                ; 26444
BattleSkillSelector.ResetCard()           ; 25174  ← 后重置技能卡
BattleHpBar.ResetBaseValue()              ; 25120
```

**顺序值得注意**：`SetControlEnable(1)` 在 `ResetCard()` **之前**。
也就是说「允许操作」这一刻，技能卡还是**上一轮的实例**。在这两者之间点击，
点到的是旧按钮，委托也可能还是旧的。

这不一定是 bug，但它是「开局第一次点技能没反应」的一条可排除解释 ——
排查时先确认点击落点时间是不是早于 `ResetCard` 完成。

`BattleSkillSelector.ResetCard`(25174) 本体：清空 → 循环重建 →
`SelectorFocusCardCollection.Run`(25243) → `waitTime`(29822)。
真正的按钮实例在 `SelectorFocusCardCollection` 那一侧建。

## 3.25 `BattleCardButton` 的两个委托字段：m8 与 m9

| 成员 | 写入者 | 动作 |
|---|---|---|
| `m8` | `SetOnClickCardEvent`(25255) | `this.m8 = dg`，只有一句 `DG_ASSIGN` |
| `m9` | `SetOnClickSkillEvent`(25256) | `this.m9 = dg`，同样只有 `DG_ASSIGN` |
| — | `@0()`(25244) | 只初始化 `m0 = 0` |
| — | `Init`(25248) | 建子件、`InitAsDummy`、设 alpha，**不碰 m8/m9** |
| — | `InitSkillButtonCollection`(25266) | `DG_CALLBEGIN 1021 / DG_CALL 1021` —— 只**遍历** m9 |

`REF` 对未初始化字段直接返回 `-1`，**不做懒创建**（`case REF` 就是 `stack_push(_v->i)`）。
而 `DG_ASSIGN` 在 `dst_i <= 0` 时会**整个丢弃赋值**。

所以 `m8` / `m9` 的**第一次** `SetOnClickXxxEvent`，如果目标还是初始的 `-1`，
这次赋值就落不进去。这两个字段必须先被别处实体化成真实委托页，赋值才生效。

对照实测 `[dgm] MISS dg_no=1021 … nr=0`：这说明派发那一刻 `m9` 是一个
**已存在但 0 条目**的委托页，而不是 `-1`（`-1` 会打印 `nr=-1`）。
所以 m9 确实被实体化过，只是条目没进去 ——
这与「赋值被丢弃」（先 -1 后实体化）和「实体化晚于赋值」两种情形都相容。
要分辨只需要一次针对性观测：在 `DG_ASSIGN` 打到 `m9` 时打印 `dst_i` 的实值。

## 3.26 `Unimplemented library function` 不是调用计数

`ffi.c` 里那行 `WARNING("Unimplemented library function: %s.%s", …)` 位于
`link_static_library()` 的**绑定阶段**：每个未实现的导出在启动时打**一次**，
与它后来有没有被调用、被调多少次**毫无关系**。

由此有两个直接后果：

- 把日志里这行名字出现次数当「热度」是错的 —— 那只是「跑过多少轮」；
- 按缺口字母序补实现是纯浪费。`GUIEngine` 有 336 个缺口（71.9%，全库最高），
  但运行期**零调用**：兰斯10 走的是 `PartsEngine`，那套老 GUI 控件根本没启用。

要回答「哪些缺失函数真的被调到」，只有一条路：

```
XSYS4_SOFT_STUB=1 XSYS4_STUB_LOG=/tmp/stub.log
```

它让未实现函数按返回类型返回零值继续执行，日志末尾给出
`distinct unimplemented HLL functions hit` 清单，每条带 `lib.func args ret`。
配合 `XSYS4_HLL_HOT=1`（**已实现**函数的调用次数）一起看，
才知道该先补谁 —— 前者告诉你「缺什么且真在用」，后者告诉你「热路径在哪」。

## 3.27 能跑到的位置，就是「零 HLL 缺口」的证明

不开 `XSYS4_SOFT_STUB` 时，未实现函数被调用会直接 `VM_ERROR` 退出进程。
所以有一条不需要额外工具、也不会骗人的推论：

> **跑测能停在哪一屏，就说明那一屏之前的路径上没有任何缺失 HLL 函数被真正调用。**

目前能稳定跑到 `Game/TurnStart`（回合开始屏）并停在那里，
由此可以确定：**卡点不是 HLL 缺口**。补 `String.*` / `Array.*` 对它是无效的，
因为这条路上根本没调到那些函数。

同理，`XSYS4_SOFT_STUB` 的诊断价值只在「想进入**尚未打通的**区域」时才体现：
先零值化硬闯过去，看清单里谁被命中，再决定补谁。对已经能走通的路段，
它只会改变行为（实测中连网络确认框的重试节奏都变了），没有诊断收益。

**判断顺序应该是：先看能走到哪 → 再决定补 HLL 还是查引擎语义。**
反过来做，会花大量时间在根本不会被调用的函数上。

## 3.28 `CALLMETHOD` 的静态还原，以及三个必须知道的坑

`CALLMETHOD n` 的 `n` 是**逻辑实参个数**，函数号压在栈上（`stack_peek(n)`）。
静态还原只能靠回溯常量 `PUSH`。这条路上有三个坑，每个都真踩过：

| 坑 | 症状 | 正确规则 |
|---|---|---|
| 把立即数当函数号 | 全表名字是假的，`nargs=1` 恰好等于某个真 fno | 立即数是 nargs |
| 排除「`PUSHSTRUCTPAGE` 后的 `PUSH`」 | 方法调用几乎全变 `?` | `PUSHSTRUCTPAGE` 是**零操作数**指令，后面那条 `PUSH` 是独立指令，正是 fno |
| 顺手把 `SWAP/DUP2` 也算进「fno 后面可能跟的指令」 | `PUSH 2; SWAP` 被认成 `fno=2` | 只认 `CALLMETHOD / PUSH / F_PUSH / S_PUSH / SWAP / PUSHLOCALPAGE / PUSHGLOBALPAGE / PUSHSTRUCTPAGE`；成员号靠「下一条是 `REF`」挡 |
| 漏掉 `PUSHLOCALPAGE` | `obj.Method(localVar)` 全部定不出名，`CreateButton` 0 个调用者 | `PUSH <fno>; PUSHLOCALPAGE; PUSH k; REF; CALLMETHOD n` 是传局部变量实参的标准编码 |

四处都修对之后，`Rance10.ain` 全量 50338 条 `CALLMETHOD` 里 **37969 条**能静态定名
（75.4%），其余 12369 条是「fno 来自变量」的动态派发，只能打印 `?`。

**代码区实际有 14.5MB**（`code_size=0xde56f2`，`nr_functions=37698`），
而 `--disasm 0:0x700000` 那种写法只覆盖前一半。5MB 之后是**剧本脚本段**：
整段只有 `MSG <串号>; CALLFUNC <handler>`，所以那一段的 `CALLMETHOD` 是 0 条 ——
看到「后 9MB 没有一条方法调用」不是 dump 坏了，是那里本来就只有台词。

## 3.29 五个委托套起来的点击链：从部件到技能卡

兰斯10 的「点技能卡」不是一次调用，是**五层委托依次转发**。每一层都是
`delegate` 字段 + `foreach`，没有任何一层直接调业务函数：

| 层 | 谁是入口 | 它做什么 |
|---|---|---|
| ① 部件 | `AFL_Parts_AddMouseLClickEvent(partsNo, dg)` | 引擎在部件被左键点时回调 `dg` |
| ② 按钮 | `ActivityButton@0` 生成的 lambda 37568 | `SystemSound::Click(); foreach (f in m6) f()` |
| ③ 技能按钮 | `LargeSkillButton@SetOnClickEvent` 写 `m3` | lambda 37029 = `foreach (f in m3) f(m0)` |
| ④ 集合 | `LargeSkillButtonCollection@SetOnClickEvent` | lambda 37032 = `X_GETENV → 转调` |
| ⑤ 卡 | `BattleCardButton@InitSkillButtonCollection` | lambda 37011 = `foreach (f in m9) f(cardId, skillIndex)` |

对应的委托签名（`--dump-delegate` 可查）：

```
1015 DG_SkillButtonOnClick          (string, int)
1020 DG_BattleCardButton_OnClick    (int)
1021 DG_BattleCardButton_OnClickSkill (string, int)
1022 DG_BattleCardButton_OnHoverSkill (bool, string, int)
```

两条容易读错的细节：

- **`ActivityButton@SetOnClickEvent` 用的是 `DG_PLUSA`（追加），不是 `DG_ASSIGN`。**
  所以 `m6` 是累加的，同一个按钮可以有多个订阅者；而 `LargeSkillButton@SetOnClickEvent`
  用的是 `DG_ASSIGN`（覆盖）。两种写法混在同一族类里，看名字看不出来。
- **`BattleCardButton@Init` 在 `SetOnClickSkillEvent` 之前跑。** `CreateButton` 的顺序是
  `new 772 → SetPos/SetZ → Init(leaderCard) → SetOnClickCardEvent → SetOnClickSkillEvent`。
  `Init` 里的 `InitSkillButtonCollection` 会 `foreach (f in m9)`，而那一刻 `m9` 还是
  默认的空委托页 —— 所以 `[dgm] MISS dg_no=1021 nr=0 ip=0x3D9DAE` **在构造期出现是必然的**，
  一次 `CreateButton` 一条。它不是故障，是「先广播（0 个订阅者）后订阅」的顺序。

`CreateButton` 全项目只有两个调用者：`BattleCardButtonCollection@Init` 与
`BattleCardButtonCollection@UpdateCardOne`。**两条创建路径**意味着同一个位置上的按钮
可能被重建；排查「赋值到底落在哪个实例上」时，必须比 `slot` 而不只是比成员号。

## 3.30 `CPartsFunctionSet`：另一套点击运输层

`Parts` / `TextButton` 之外，AIN 还有第三条注册路径，走的是
`CPartsFunctionSet`（struct 299，42 个成员）：

```
partsNo → GetFunctionSet(partsNo) → fs
fs.m7 DG_PLUSA dg          ; 追加到 <MouseLClickEvent>
```

`fs` 的成员按事件类型分槽：

| 成员 | 事件 | 委托类型 |
|---|---|---|
| `m1` | `m_PartsNumber` | int（**匹配键**）|
| `m3` | `<MouseEnterEvent>` | `DG_MouseEnterHandler` |
| `m6` | `<MouseWheelEvent>` | — |
| `m7` | `<MouseLClickEvent>` | `DG_MouseClickHandler (int partsNo, int x, int y)` |
| `m8` | `<MouseRClickEvent>` | 同上 |
| `m9` | `<MouseMClickEvent>` | 同上 |

派发器是 `CPartsFunctionSet@CallFunctionMouseClick(btn, x, y)`(7942)：

```
if (btn == 1) foreach (f in m7) f(m1, x, y)
else if (btn == 2) foreach (f in m8) f(m1, x, y)
...
```

而它的上游是 `CPartsMessageManager@CallDelegateMouseClick(fs)`(7978)，
再上游是消息队列（`PopMessage`）。所以「点了一个部件」在 AIN 里是
**消息 → CallDelegateMouseClick → CallFunctionMouseClick → m7 foreach** 这条链。

**`AFL_Parts_AddMouseLClickEvent`(6892) 运行期是在被调用的** ——
2026-11-21 实测 27 次（`XSYS4_FN_WATCH=6892`），D337 那条「本作运行期 0 次调用」
是错的。用它的正是 `ActivityButton@0`（老式按钮）：构造时把自己生成的
lambda 37568 挂到部件上。所以**技能按钮这类 `ActivityButton` 走的不是
`Parts@OnClick`，而是 `CPartsFunctionSet.m7`**。

## 3.13 一个提醒

这一层最容易陷入"补函数"的自我感动：日志报一个未实现，就写一个实现，看起来很勤奋。

但真正卡住你的从来不是"缺一个函数"，而是"某个函数的返回槽宽错了"。

**先保证已有的函数都调对了，再补新的。**
