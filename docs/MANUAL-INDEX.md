# 官方手册索引与验收判据

> **权威源**：`Rance10_Port/game/Manual/`（原 HTML + `img/`，官方中文说明书，**有版权、不入库**）。
> 本文件只是索引与判据，不复述正文。全文检索用生成物 `game/Manual/MANUAL.md`
> （`tools/gen-manual.py` 生成，同样不入库）。
>
> **本文件不写「当前是否已验证」** —— 状态唯一源是工作区根 `README.md` / `TECH_DEBT.md`。
> 验收表只写「怎么验」，不写「验到哪一步」，避免变成第二份会过期的状态副本。

## 手册构成

| 部分 | 文件 | 内容 | 图 |
|---|---|---|---|
| 封面 / 教程 | `index.html`、`startup.html` | 封面、新手教程 | `sum06*.png` |
| 游戏指南 | `01.html`–`12.html` | 系统说明：存档、基本玩法、回合与环节、卡片/角色、部队数据、准备作战、任务、战斗与计算公式、自动战斗、挑战模式 | `pm*.jpg/png`（62 张） |
| 参考手册 | `r01.html`–`r14.html` | 逐界面说明：基本操作、启动、MENU、收藏、LOAD、事件剧情、回合开场、准备作战、任务、部队编制、战斗开始、战斗、餐券、设置 | `rm*.jpg/gif`（16 张） |
| 其它 | `staff.html`、`pub.html` | 开发人员、版权 | — |

官方手册为 UTF-8；文件与图片数量**不写死**（官方资料虽不变，但手写数字会腐化）：
以 `ls game/Manual/`、`tools/gen-manual.py` 的 `PAGES` 与生成脚本输出为准。
生成物 `MANUAL.md` 保留章节 / 表格 / 图片相对链接，剥掉导航菜单。
所有 `r*.html` 都配一张界面基准图。

## 图片陷阱（纯文本转换拿不到，必须回看原图）

| 图 | 承载的关键信息 |
|---|---|
| `img/rm01.gif` | **完整按键表**（该页正文只有两句话，表全在图里）：Enter / ↓ 下一段文本；**按住 Ctrl 不论是否已读强制快进**；`A` 切 AUTO；`S` 切 SKIP；Esc 设置；Home / PageUp 回顾；右键关闭对话框；滚轮按下切 SKIP |
| `img/rm02.jpg` | 启动界面按钮布局（标题出画的客观基准，D141 据此重绘） |
| `img/rm06.jpg` | 事件・剧情界面布局（对话框、人名牌、消息窗口） |
| `img/rm10*.jpg` | 部队编制 / 部队加成 / 角色信息三个界面 |
| `img/rm11.jpg`、`img/rm12.jpg` | 战斗开始（VS）/ 战斗界面 |
| `img/pm10_*.png` | 战斗计算公式、异常状态、支援状态等数值资料 |

> **教训（D205）**：靠人工读 `rm01.gif` 才找到「按住 Ctrl 强制快进」，把长剧情验证
> 从逐点十几分钟压到按住一分钟。**转 md 不能替代看图**；遇到操作 / 界面问题，
> 先回原图，不要只 grep 生成物 —— 这正是「观测工具会骗人」的另一种形式。

## 验收判据（手册描述 → 可执行检查）

| 手册章节 | 期望（手册） | 可执行判据 |
|---|---|---|
| 参考 01 基本操作 | 对话框可用键盘 / 鼠标推进、按住 Ctrl 强制快进 | `keydown:Control_L; wait; keyup:Control_L` 序列下，`SetMessageWindowText` 在短时间内达到与逐句点击相同的进度；数值以当次跑测为准 |
| 参考 02 启动界面 | 标题按钮进入游戏 | 真实流程点标题按钮后日志出现 `SceneQuestMap`；截图含 LOGO 与菜单按钮 |
| 参考 03 ＭＥＮＵ | AUTO / SKIP / 回顾 / 设置各项存在 | `XSYS4_HLL_TRACE` + `[act] loaded` 核对 MENU 活动与子界面；按键映射回看 `rm01.gif` |
| 参考 06 事件・剧情 | 对话正文逐句显示、可推进 | `SetMessageWindowText` 收到非空文本；`_MSG` 编号递增；截图出现消息窗口与人名牌 |
| 参考 10 部队编制 | 编成进出闭环、右键返回 | `[call]` 出现 `SceneParty@OpenParty` 与 `OnRClick → SceneStack@Exit`；退出后地图重绘 |
| 参考 11 / 12 战斗 | 战斗开始与战斗界面 | `23489 RunBattleEvent → 26388 RunBattleSceneSet`；`[act] loaded 'Game/Battle/…'`；VS 对阵画面 |
| 指南 04 回合与环节 | 节点 / 回合推进 | `EX_String` 查询的 KeyBase 从起始节点变为后续节点（**不带诊断后门**） |
| 指南 10 战斗 | 数值与公式 | 需对照 `pm10_*.png` 公式图逐项验算；暂不在自动判据范围 |

> 判据里凡涉及数量 / 次数，一律「以当次跑测与 `docs/STATS.md` 为准」，不在本表写死。

## 用法

```sh
tools/gen-manual.py             # 重新生成 game/Manual/MANUAL.md
tools/gen-manual.py --check     # 源 HTML 变化后校验生成物
grep -n "战斗计算公式" ../game/Manual/MANUAL.md
```

生成物与权威源同目录，图片用相对链接，本地 Markdown 阅读器打开即可看到图。
原 HTML / 图片保持只读，不要按转换结果反向修改官方资料。
