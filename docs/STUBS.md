# HLL 未实现桩（工单，脚本生成）

> `python3 tools/gen-stubs.py` 生成，**不要手改**。`--logs ../logs` 会额外标注「日志里真的被调用过」。

三种桩的形态：

| 形态 | 含义 | 危险度 |
|---|---|---|
| `HLL_TODO_EXPORT` | 导出表里 `fun = NULL`，**链接失败** ⇒ 每次调用报未实现并静默返回 0 | 中（会刷屏、返回值可能是垃圾） |
| `HLL_WARN_UNIMPLEMENTED` | 有函数，但**返回写死的默认值** | **高**：`SetXxx` 返回 `false` 等于告诉 AIN「设置失败」，会把它带进降级分支 |
| `SS_Stub_*` | 手写空桩（D91 时代） | 高（同上，且没人看得出是假的） |

总计 **1319** 个。按文件：

| 文件 | 总数 | 被调用过 | 其中 TODO / WARN / STUB |
|---|---|---|---|
| `GUIEngine.c` | 336 | 72 | 336 / 0 / 0 |
| `Array.c` | 212 | 0 | 212 / 0 / 0 |
| `ReignEngine.c` | 200 | 86 | 181 / 19 / 0 |
| `PartsEngine.c` | 166 | 97 | 166 / 0 / 0 |
| `DrawDungeon.c` | 66 | 0 | 64 / 2 / 0 |
| `StoatSpriteEngine.c` | 42 | 1 | 35 / 7 / 0 |
| `SACT2.c` | 36 | 0 | 26 / 10 / 0 |
| `SystemService.c` | 27 | 2 | 5 / 2 / 20 |
| `ChipmunkSpriteEngine.c` | 24 | 1 | 22 / 2 / 0 |
| `vmGraph.c` | 24 | 0 | 23 / 1 / 0 |
| `vmArray.c` | 16 | 0 | 16 / 0 / 0 |
| `vmFile.c` | 12 | 0 | 12 / 0 / 0 |
| `PastelChime2.c` | 8 | 0 | 6 / 2 / 0 |
| `CGManager.c` | 7 | 0 | 7 / 0 / 0 |
| `DrawGraph.c` | 7 | 0 | 6 / 1 / 0 |
| `HTTPDownloader.c` | 7 | 4 | 5 / 2 / 0 |
| `MsgLogManager.c` | 7 | 0 | 0 / 7 / 0 |
| `vmGraphQuake.c` | 7 | 0 | 7 / 0 / 0 |
| `vmSound.c` | 7 | 1 | 7 / 0 / 0 |
| `vmString.c` | 7 | 0 | 7 / 0 / 0 |
| `GoatGUIEngine.c` | 6 | 0 | 6 / 0 / 0 |
| `vmDialog.c` | 6 | 0 | 6 / 0 / 0 |
| `vmMusic.c` | 6 | 0 | 6 / 0 / 0 |
| `FileOperation.c` | 4 | 4 | 4 / 0 / 0 |
| `KiwiSoundEngine.c` | 4 | 2 | 4 / 0 / 0 |
| `OutputLog.c` | 4 | 0 | 0 / 4 / 0 |
| `String.c` | 4 | 3 | 0 / 4 / 0 |
| `AliceLogo.c` | 3 | 0 | 0 / 3 / 0 |
| `AliceLogo2.c` | 3 | 0 | 0 / 3 / 0 |
| `AliceLogo3.c` | 3 | 0 | 0 / 3 / 0 |
| `AliceLogo4.c` | 3 | 0 | 0 / 3 / 0 |
| `DALKEDemo.c` | 3 | 0 | 2 / 1 / 0 |
| `IbisInputEngine.c` | 3 | 1 | 1 / 2 / 0 |
| `MamanyoSDemo.c` | 3 | 0 | 2 / 1 / 0 |
| `MsgSkip.c` | 3 | 0 | 0 / 3 / 0 |
| `vmCG.c` | 3 | 0 | 2 / 1 / 0 |
| `vmSprite.c` | 3 | 0 | 3 / 0 / 0 |
| `AliceLogo5.c` | 2 | 0 | 0 / 2 / 0 |
| `BanMisc.c` | 2 | 0 | 0 / 2 / 0 |
| `Confirm.c` | 2 | 0 | 0 / 2 / 0 |
| `Confirm2.c` | 2 | 0 | 0 / 2 / 0 |
| `DrawPluginManager.c` | 2 | 0 | 0 / 2 / 0 |
| `MamanyoDemo.c` | 2 | 0 | 0 / 2 / 0 |
| `NewFont.c` | 2 | 0 | 2 / 0 / 0 |
| `PixelRestore.c` | 2 | 0 | 0 / 2 / 0 |
| `vmCursor.c` | 2 | 0 | 2 / 0 / 0 |
| `vmData.c` | 2 | 0 | 2 / 0 / 0 |
| `AFAFactory.c` | 1 | 0 | 0 / 1 / 0 |
| `CrayfishLogViewer.c` | 1 | 0 | 0 / 1 / 0 |
| `Cursor.c` | 1 | 0 | 0 / 1 / 0 |
| `DrawRipple.c` | 1 | 0 | 1 / 0 / 0 |
| `Gpx2Plus.c` | 1 | 0 | 0 / 1 / 0 |
| `InputDevice.c` | 1 | 0 | 0 / 1 / 0 |
| `MainEXFile.c` | 1 | 0 | 0 / 1 / 0 |
| `MenuMsg.c` | 1 | 0 | 0 / 1 / 0 |
| `MsgLogViewer.c` | 1 | 0 | 0 / 1 / 0 |
| `PlayDemo.c` | 1 | 0 | 0 / 1 / 0 |
| `SengokuRanceFont.c` | 1 | 0 | 0 / 1 / 0 |
| `StretchHelper.c` | 1 | 0 | 0 / 1 / 0 |
| `SystemServiceEx.c` | 1 | 0 | 0 / 1 / 0 |
| `Toushin3Loader.c` | 1 | 0 | 1 / 0 / 0 |
| `vmKey.c` | 1 | 0 | 1 / 0 / 0 |
| `vmSurface.c` | 1 | 0 | 1 / 0 / 0 |
| `vmSystem.c` | 1 | 0 | 0 / 1 / 0 |

## 优先填哪些

排序只看两条，不看数量：

1. **`WARN` / `STUB` 优先于 `TODO`** —— `TODO` 至少会报错，而假成功/假失败会让 AIN 的判定**看起来成立**，错得更安静；
2. **同一组 set/get 里只要有一个是桩，整组都要补** —— `Set` 存不住时 `Get` 返回什么都是错的（D393 的变量系统就是这个形状）。

填完一批的验收一律是 `tools/run/smoke.sh quick`：
退出码 0，且日志里该函数不再出现在 `Unimplemented library function` 后面。

