## 这是什么

`xsystem4` 的兰斯10（ランス10）下游分支，目标是把 AliceSoft System4 引擎的这作
**原生**跑在 Linux / Android 上，不走 Wine / proot / 转译层。

这个 Release 提供的是**引擎二进制**（Linux x86_64），**不含任何游戏素材** ——
需要你自备正版游戏目录。

> 项目处于早期阶段。这一版的重点是「把链路一步步打通并留下可复现的取证」，
> 不是「能通关」。下面写清楚走到哪、卡在哪。

---

## 当前进度（按实际跑通的位置）

```
标题画面
  → 第一章开场 ADV（连续 37 句，含 Ctrl 快进）
  → 回想对话「過後查看」（可点，会关闭）
  → 选项「下一步」
  → 进入第一场战斗：背景 / 我方与敌方 HP / Round 1/10 / AP 球 /
     技能卡 / 角色卡 / 防御 · 部队编成 全部画出来
  → 点角色卡 → 技能列表展开
  → 选技能 → 伤害结算（敌人 HP 9000 → 8303 → 7984 …）
  ✗ 技能卡「用鼠标点」这一下仍然点不中（见下）
```

### 已经解决的问题（每条都有反汇编 + 跑测取证）

| 问题 | 结论 |
|---|---|
| 背景 CG 换图后地图盖住新背景 | AIN 把全屏情报地图和 ADV 放同一层，换图时按根部件整组收起（D317） |
| 标题按钮「按十次中一次」 | 释放沿被输入窗口吞掉，且释放收尾排在提前 return 之后（D332/D333） |
| 回想对话按钮点了没反应 | 引擎缺少「点击 → AIN 按钮对象」这一拍，补 `vm_request_parts_click()` + 帧泵派发（D337） |
| 战斗里按钮全点不中 | 全屏「点击接收器 / 布局盒」被当成可点件，改为不参与命中（D338） |
| 技能列表展开了却突然消失 / 立绘缺块 | `cover_layer` 把不绘制的矩形检测件当视觉地板，整层被判定不可见（D342–D343） |
| 战斗推进不了 | 回合收尾逻辑本身没问题，是「四人各行动一次」这个前提没满足（D344） |

### 还没解决的问题（不藏着）

1. **技能卡的鼠标点击**：卡片的点击委托在 AIN 侧是 `LargeSkillButton.m3 →
   collection → BattleCardButton.m9 → SceneBattle@OnClickSkill`，运行期实测
   `m9` 是**空委托页**，最后一跳静默不执行；同时卡片的命中矩形是 `1×1`。
   用确定性驱动直接调 `OnClickSkill(cardId, skillIndex)` 时整条链是通的
   （`RunSelectedSkill` / `ProcessPlayerAction` 都执行、HP 正常扣），
   所以缺的是「UI 到 AIN」这一小段，不是战斗逻辑。
2. **敌人立绘**：目前多数情况下是红色剪影，真实立绘未出。
3. **回合收尾未实测通过**：逻辑验证过能跑，但没有在正常操作下走完一个完整回合。
4. **Android APK**：尚未交付。SDL3 迁移已完成，但打包与运行验证还没做。

---

## 这一版的技术栈

* 渲染 / 输入 / 音频 / 消息队列已从 SDL2 迁到 **SDL3**（`dependency('sdl3')`，
  配合 `-DSDL_ENABLE_OLD_NAMES` 降低改写量）。上游仍是 SDL2。
* 音频采用 SDL3 的 **stream 模型**（回调式音频在 SDL3 被移除）。
* 构建：Meson + Ninja，`--buildtype=release`。

## 包里有什么

```
xsystem4-<tag>-linux-x86_64/
├── xsystem4      引擎二进制
├── run.sh        启动包装（把自带的 libSDL3 加进库搜索路径）
├── lib/          libSDL3.so（系统源里通常还没有 SDL3，所以自带一份）
├── README-RUN.txt 运行方法 + 已知问题
└── LICENSE
```

## 怎么跑

```sh
tar xzf xsystem4-*-linux-x86_64.tar.gz
cd xsystem4-*-linux-x86_64
./run.sh /path/to/ランス10        # 目录里要有 AliceStart.ini / Rance10.ain
```

常见依赖（Debian/Ubuntu）：

```sh
sudo apt install libfreetype6 libpng16-16 libwebp7 libturbojpeg0 \
                 libsndfile1 libffi8 zlib1g libdeflate0 libgl1
```

细节（字体、存档目录、日志位置）见包内 `README-RUN.txt`。

## 构建

由 GitHub Actions 在打 tag 时构建：拉取源码 + 子模块，应用
`patches/libsys4-rance10.patch`（`ajp_get_metrics` 等 4 个文件），
SDL3 缺失时从源码编译安装，然后 Meson/Ninja 出包。
校验值见同页 `.sha256` 文件。

## 声明

本项目只提供引擎实现，**不包含也不分发任何游戏素材**。
请使用你合法持有的游戏副本。与 AliceSoft 及上游 xsystem4 作者无隶属关系。
