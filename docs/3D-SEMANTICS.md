# 3D 设置与预览渲染 —— 语义规格

> 来源：2026-11-24 对 3D 设置页（`Asra\设置／０８／３Ｄ页面`，pact version 5/30）
> 的取证。**结论先行**：设置项的「改 → 存 → 读回」链路**已经能用**（引擎无需改动）；
> 缺的只有**预览画面**（实时 3D 渲染）。本文件是将来做渲染器时的接口规格。
>
> 相关编号：D517 / D517d / D517h。状态与卡点只在 `docs/STATUS.md` 定义。

## 一、结论（三条）

1. **3D 预览不经过任何 HLL**。3D 页 AIN 全程只调 `PartsEngine`（d662 实测 75 个
   函数全是它），没有任何 3D 渲染调用。预览由原版 PartsEngine **内部渲染路径**完成。
2. **设置项走 `config::detail::CAS3DSetting` 类的 AIN 方法**，不是 HLL。
   读写通过 `VSFile` 库写 `3DConfig.vsf`（该库已完整实现）。
3. **闭环已验证**（d684）：点选项 → `SetRenderMode` → `SetParam` →
   点「返回游戏」→ `Save` → `3DConfig.vsf` 字节变化。
   即：设置能改、能存、下次启动能读回（`Load` 在进 3D 页时调用一次，d683 实测）。

## 二、设置项清单（AIN 方法，非 HLL）

`config::detail::CAS3DSetting` 的成员，`Get*`/`Set*` 成对出现：

| # | 项 | Get | Set | 界面对应 |
|---|---|---|---|---|
| 1 | RenderMode | 2751 | 2768 | 绘制模式（软件/硬件） |
| 2 | LightingMode | 2752 | 2769 | 亮度 |
| 3 | AntiAliasingMode | 2753 | 2770 | 抗锯齿 |
| 4 | Edge | 2754 | 2771 | 边缘 |
| 5 | ShadowMode | 2755 | 2772 | 阴影 |
| 6 | ShadowMapResolutionLevel | 2756 | 2773 | 阴影MAP分辨率 |
| 7 | BumpMode | 2757 | 2774 | 凹凸映射 |
| 8 | BloomMode | 2758 | 2775 | 光晕 |
| 9 | GlareMode | 2759 | 2776 | 眩光 |
| 10 | FogMode | 2760 | 2777 | 雾化 |
| 11 | SpecularMode | 2761 | 2778 | 镜面 |
| 12 | LightMapMode | 2762 | 2779 | 光照MAP |
| 13 | SoftFogEdgeMode | 2763 | 2780 | 软雾化边缘 |
| 14 | SSAOMode | 2764 | 2781 | 环境光遮蔽 |
| 15 | TextureResolutionLevel | 2765 | 2782 | 纹理分辨率 |
| 16 | TextureFilterMode | 2766 | 2783 | 纹理滤镜链接 |
| 17 | DOFMode | 2767 | 2784 | 被写界深度 |

生命周期方法：`@Load`(2745) / `@Save`(2746) / `@Bind`(2747) / `@SetParam`(2748) /
`@ExistReignSaveFile`(2749) / `@GetSaveFileName`(2750)。

`Get3DHelpTextFor*`(2572–2587) 提供每项的说明文本（界面上的帮助文字来源）。

## 三、存储：`3DConfig.vsf`

- 位置：`Rance10_Port/game/3DConfig.vsf`（**用户的真实配置，实验后必须还原**）
- 格式：`VSF\0` + 5 个 `\0`（共 8 字节头），之后是「类型字节 + 值」流：
  `0`=BYTE(1B)、`1`=INT(4B LE)、`2`=FLOAT(4B LE)、`3`=STRING(NUL 结尾)
- 实现：`src/hll/VSFile.c`，HLL 库 `VSFile`（OpenForRead/OpenForWrite/Close +
  Read/Write{Byte,Int,Float,String}），**全部已实现**
- 实测内容（108 字节）：`"3CO"` + 19 个 INT
- **19 个 INT 与 17 项的对应关系尚未穷举**：批量实验（d685，3 个不同页面各改一项）
  只命中 index 17（`0→4`）与 18（`1→0`），说明至少有一项是**位域打包**
  （`0→4` 是置 bit2）。渲染器实现前若要精确映射，需逐项单变量实验。

## 四、渲染层对象（原版语义，本引擎均未接）

| 对象 | 作用 | 方法 |
|---|---|---|
| `config::detail::CAS3DSetting` | 设置模型 + 存取 | 见上表 |
| `stageeditor::detail::CAS3DStage` | 3D 舞台 | `Create`(17522) `Release`(17523) `ClipRect`(17524) `AddElement`(17525) `AddObject`(17526) `AddGroup`(17527) `AddBrush`(17528) |
| `stageeditor::detail::CAS3DObject` | 舞台内对象 | `LoadMotionThread`(17520) `Move`(17521) `Name/Instance` get/set |
| `elkeditor::detail::CSealEngine` | Seal（粒子/特效）引擎 | `GetAS3DLayer`(3681) |
| `parts::detail::CParts3DLayerManager` | 部件侧 3D 层管理 | `Add`(7900) `Remove`(7901) `SetShow`(7902) `Suspend/ResumeShowState`(7903/7904) `Numof`(7906) `Get`(7907) |

`CParts3DLayerManager` 在引擎侧对应 `parts::PARTS_3DLAYER` 状态 +
`ReignEngine_create_plugin(RE_SEAL_PLUGIN)`，`src/parts/parts.c` 的
`parts_3dlayer_create_plugin()` 已实现（创建 sprite 并绑定插件）。
**但 3D 设置页的 EX 里没有任何 `PARTS_3DLAYER` 部件** —— 预览不是部件，
是引擎在渲染配置页时自己开的一路渲染。

## 五、HLL 面（全部只有 7 个）

| 库 | 函数 | 状态 |
|---|---|---|
| DrawDungeon | `SetDirect3DMode` / `GetDirect3DMode` | 已实现 |
| DrawDungeon | `SetDrawShadowMap` / `SetPerspective` / `SetSSEFlag` | 已实现 |
| DrawField | `TransPos2DToPos3DOnPlane`(911) / `TransPos3DToPos2D`(912) | **TODO（空桩）** |
| PartsEngine | `Parts_CreateParts3DLayerPluginID` / `Get` / `Release` | 已实现 |

`DrawDungeon` 库整体 312 个函数，248 已实现、64 TODO。它是**3D 迷宫**（游戏内
地下城）的渲染库，与配置页预览是两套东西；但有 `SetDrawShadowMap` 这类设置项
直通，说明迷宫渲染会读同一份 `CAS3DSetting`。

## 六、怎么复验

```bash
# 1) 进 3D 页并观察 Save（进 3D 页 → 点选项 → 点「返回游戏」）
XSYS4_AUTO_CALL=2803:20000 \
XSYS4_AUTO_CLICK="3000:1778,864;440,133;924,705" \
XSYS4_AUTO_CLICK_DELAY=26000 XSYS4_AUTO_CLICK_N=3 \
XSYS4_CALL_TRACE=2745,2746,2748,2768 XSYS4_CALL_TRACE_TOTAL=1 \
ENGINE_BIN=$PWD/build-sdl3/src/xsystem4 tools/run/safe_run_click.sh dx 8 50 /tmp/dx.png

# 2) 对比配置文件（务必先 cp 备份）
md5sum ../game/3DConfig.vsf
```

判据：`[calltotal] fno=2746 hits=1`（Save 被调）且 `3DConfig.vsf` md5 变化。
`2798?` 号段见 `XSYS4_FN_LIST=3D` 的完整导出。

界面按钮坐标（1920×1080 逻辑坐标）：3D 页签 `(1778,864)`；
底部三键「关闭游戏/回到标题界面/返回游戏」= `(524,705)` / `(724,705)` / `(924,705)`。
