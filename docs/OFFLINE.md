# 离线工具

> **先看 [`SDK.md`](SDK.md)** —— 那是整套工具链的统一入口
> （`xsys4 build/run/dump/disas/switches`）。本文档只讲**离线工具**的细节：
> 它们不启动游戏、不需要 X，适合读代码和查符号。


只读 `Rance10.ain` 与存档 `.asd` 的小工具集合：**不启动游戏、不需要 X、不需要网络**，
秒级返回。所有源码在 `../tools/src/*.c`（随 git 版本管理），
产物编译到 `../tools/bin/`。

```bash
../tools/xsys4 build tools            # 编译全部
../tools/xsys4 build tools ain_at     # 只编一个
```

需要先构建过引擎（`build/subprojects/libsys4/libsys4.a`）。
把游戏路径设为 `GAME=../../game/Rance10.ain` 后，下面命令可直接复制。

---

## ain_at — AIN 查询/反汇编（主力工具）

用它回答"这个地址属于谁、这个函数有哪些局部变量、这个库函数的签名是什么"。
**所有反汇编结论都应该从这里出，而不是凭日志猜。**

| 模式 | 用途 |
|---|---|
| `findsym:<子串>` | 按名字找函数：`fno / 地址 / 参数数 / 局部数 / 返回类型` |
| `func:<全名>` | 打印函数头 + 局部变量 + 从函数头反汇编 |
| `at:<hex> [n]` | 从任意偏移反汇编 n 条（可未对齐，用于看崩溃点前后） |
| `atf:<hex>` | 定位包含该地址的函数，从头**对齐**反汇编到目标之后 |
| `lib:<n>[:<子串>]` | 打印库 n 的函数表与完整签名 |
| `struct:<名字>` / `memb:<n>` | 结构体成员类型 / 接口表 / vtable |
| `fno:<n>` | 按函数号查 |
| `dl:<子串>` | 按名字找委托类型（`DELEGATE [n] 全名`） |
| `sno:<n>` | 按编号看结构体（同 `struct:`，少一次按名查找） |
| `str:<n>` / `dno:<n>` | 字符串 / 委托类型 |
| `callhll` | 全代码 CALLHLL 的 tag 直方图 |

真实用例：

```bash
# 进章节的后门要调谁？
../tools/bin/ain_at $GAME findsym:Chapter1@Run
#   fno=22543 addr=0x33ec30 args=0 vars=0 ret=void  GameChapter1@Run

# Array.PushBack 是第几个？签名是什么？
../tools/bin/ain_at $GAME lib:5:PushBack
#   lib[5] Array nr_functions=74
#   [7] PushBack  args=2 ret=void  self:ref array<?>  Value:hll_param

# 崩溃点 0x37CBCA 在哪个函数里、局部变量是什么？
../tools/bin/ain_at $GAME atf:0x37CBCA
#   FUNC QuestMapObjectLoader@Load fno=23770 addr=0x37ca4c nr_args=0 nr_vars=10
#     var[0] type=t10 struc=-1 rank=0 rootX
#     ...

# 618 是接口还是结构体？（0 成员 + is_interface=1 → 接口，不能按成员数猜）
../tools/bin/ain_at $GAME memb:618
#   STRUCT [618] ILoadableQuestMapObject members=0 iface=1
```

## gsave_dump — 存档检查器

不跑游戏就能判断"我们写出去的档，自己能不能读回来"。

```bash
tools/bin/gsave_dump logs/savedir/<tag>/GlobalSave.asd
# == .../GlobalSave.asd: OK
#    globals=… arrays=… records=… strings=…
#    g[12] g_replayInfo  type=79 value=3  ar_rank=1 flats=1 elem=17(n=567)
```

判据是 `== 文件: OK`；出现 `Invalid save file` 说明写读不对称
（历史上两次都是解析器只认具体数组类型、不认泛型 `array`）。

## ain_xref — 交叉引用

"谁读/写了这个全局、谁调了这个函数"。定位"这个值是谁改的"时先用它缩小范围，
再上运行时探针。用法见 `tools/bin/ain_xref`（无参数时打印用法）。

## ain_audit / ain_spec / ain_locals / ain_icast / ain_refparam / ain_hllarity

一次性普查类工具，用来回答"整个 AIN 里有多少处满足某个形状"：

- `ain_audit`：功能表/结构对齐体检
- `ain_spec`：字节码规范取证（各类指令的槽宽/参数统计）
- `ain_locals`：局部变量槽宽普查
- `ain_icast`：`ICAST` 分布
- `ain_refparam`：`AIN_REF_HLL_PARAM` 调用点的槽宽分类（本项目契约里 10/47/89/92 与 12/13/21 的分界就来自它）
- `ain_hllarity`：HLL 调用参数个数体查
- `aindump`：导出整体结构

## 外部工具：alice-tools（`../tools/offline/alice.sh` 包装）

`nunuhara/alice-tools` 是 AliceSoft 文件格式的官方级工具集（同一作者的 libsys4 与我们
子模块是同一份代码，所以解析结果可以直接当"基准答案"用）。**不在仓库内**，只在开发机装：

```sh
git clone --recurse-submodules https://github.com/nunuhara/alice-tools
cd alice-tools && env -u PYTHONPATH -u PYTHONHOME meson setup build && ninja -C build
```

**中文版必须加编码参数**，否则 `ar list` 会 `ERROR: iconv: Invalid or incomplete
multibyte` 并截断、`ar extract -n "<名字>"` 永远匹配不到。`../tools/offline/alice.sh` 就是干这个的：
它把 `--input-encoding gbk --output-encoding utf-8` 插到子命令后面。

### 它补上了自家工具缺的两块信息

1. **反汇编带符号**（`ain_at` 只给编号，要人肉查）：

   ```sh
   ../tools/offline/alice.sh ain dump --function "SceneQuestMap@CheckNoLeader" -o /tmp/f.x game/Rance10.ain
   ```

   ```
   .GLOBALREF g_party              ← 全局变量名（自家工具只有 PUSHGLOBALPAGE 260）
   PUSH 27828
   CALLMETHOD 0
   S_PUSH "没有领队。请进行编组"     ← GBK 字符串直接解出来
   CALLFUNC DialogScenes::RunInformationDialog
   .STRUCTREF SceneQuestMap m_sceneStack   ← 成员名
   ```

   回答"这个 `PUSH 260` 是哪个全局""这条 CALLMETHOD 到底调谁"时，
   比 `ain_at` 快一个数量级。`--function` 只导出一个函数，不用怕生成整份大文件。

2. **EX 全字段**（自家 `pactex_dump.py` 只抽字符串，部件字段是半猜的，
   日志里那些 `Unknown part type for 'Atk'` 就是猜漏的后果）：

   ```sh
   ../tools/offline/alice.sh ar extract -i 155 -o /tmp/ocv.pactex game/Rance10Pact.afa   # 155 = afa_ls.py 查到的序号
   ../tools/offline/alice.sh ex dump -o /tmp/ocv.x /tmp/ocv.pactex
   ```

   输出是 `tree / list` 的 C 风格文本，每个部件都有 `パーツ类型 / サーフェイスエリア /
   ＣＧ名 / 座標 / アルファ …` 全套字段。实测 `OrganizationCardView` 里出现的类型名：

   ```
   ＣＧパーツ / 用户组件 / 低レベルパーツ / 按钮 / 文本パーツ / 矩形パーツ /
   レイアウトボックス / フラットパーツ / 纵向滚动条栏
   ```

   注意这份 EX 是**汉化残留的混合体**（有的键译成中文、有的还是日文片假名），
   解析表两边都得认。查"某个部件到底该画什么 CG"用这个最直接。

### 对照表：什么时候用哪个

| 问题 | 用哪个 |
|---|---|
| 这个地址属于哪个函数、局部变量叫什么 | `bin/ain_at atf:/func:/fno:` |
| 这条 `PUSH 260` 是哪个全局、这条 CALLMETHOD 调谁 | `alice.sh ain dump --function` |
| 整个 AIN 里谁调了某函数 | `bin/ain_xref call <fno>` |
| 这个 EX 部件的每个字段/类型名 | `alice.sh ex dump` |
| 归档里有没有某个 CG / 某条目序号 | `afa_ls.py` 列表，`alice.sh ar extract -i` 取 |
| 存档能不能读回、里面有什么 | `bin/gsave_dump` |
| 运行时的 this/栈/断点 | `XSYS4_DUMP_AT` 或 DAP（见根 README §5） |

## 与跑测脚本的分工

跑测脚本（`baseline.sh` / `storm.sh` / `run-headless.sh` / `run-live.sh`）留在
`../tools/run/` —— 它们依赖引擎目录结构，且**必须在独立 Xvfb 里跑**：
`run-live.sh` 会 `xdotool` 点鼠标、`x11grab` 截屏，跑在真实桌面上会干扰你正在用的会话。
详见项目根 `README.md` 的「两条硬纪律」。
