# 渲染后端决策记录（SDL2 / SDL3 / Vulkan / SDL_GPU / GLES3）

> 本文是 `STATUS.md` §6 声明的那份「想动图形层之前先读」的决策记录。
> 曾经被声明但文件缺失（D327 修复），现在内容以 **SDL2→SDL3 迁移** 为首个正式评估。

## 决策一：SDL2 → SDL3 迁移可行性


> 结论先行：**可行，工作量中等偏小**（桌面出画面 ~1.5–3 人日；Android 打包另加 1–2 天）。
> 难点不在「改不完」，而在 **音频要重写** 和 **与上游分叉** 这两件事上。
> 本文的数字全部来自本仓库实测（`grep` 统计 + 逐个核对 SDL3 头文件），不是估算。

## 一、现状盘点

| 项目 | 实测值 | 说明 |
|---|---:|---|
| 使用 SDL 的源文件 | **62** | `src/` + `include/` |
| 不同的 SDL 函数 | **67** | 剩下的 200 多个符号是 `SDL_SCANCODE_*` / `SDL_CONTROLLER_*` 枚举 |
| `SDL_Renderer` / `SDL_Texture` | **0 / 0** | 渲染走**裸 OpenGL**（`GL_*` 共 291 行） |
| `SDL_RWops` | **0** | 已全部走自有文件层 |
| SDL_ttf / SDL_image / SDL_mixer | **0 / 0 / 0** | 字体用 freetype，音频用 libsndfile + 自研 mixer |
| 构建依赖 | `dependency('sdl2')` 一处 | 无子库、无扩展包 |
| `SDL_*(...) < 0` 这类「负值判错」 | **5 处** | SDL3 改 bool 返回，需要翻转的检查点很少 |

**这是本次评估里最重要的数字**：SDL3 最伤筋动骨的三块（渲染器 API 重写、`RWops`→`IOStream`、扩展库改名）在这里**全是零**。剩下的是核心库层面的改名与少量语义调整。

拉取方式（本次核对用）：

```sh
# SDL3 的权威头文件，逐符号比对
curl -sSL https://raw.githubusercontent.com/libsdl-org/SDL/main/include/SDL3/SDL_timer.h
curl -sSL https://raw.githubusercontent.com/libsdl-org/SDL/main/include/SDL3/SDL_rect.h
```

## 二、变更分四类

### A. 机械改名（脚本可批量，风险≈0）

| 现在 | SDL3 | 备注 |
|---|---|---|
| `#include <SDL.h>` / `<SDL2/...>` | `<SDL3/SDL.h>` / `<SDL3/...>` | 含 `main()` 的文件另加 `<SDL3/SDL_main.h>` |
| `SDL_GetTicks64` | `SDL_GetTicks` | SDL3 的 `SDL_GetTicks` 就是 64 位；`GetTicks64` 已删（12 处调用） |
| `SDL_UnionRect` | `SDL_GetRectUnion` | 改返回 bool |
| `SDL_IntersectRect` | `SDL_GetRectIntersection` | 改返回 bool |
| `SDL_AllocFormat` / `SDL_FreeFormat` | `SDL_GetPixelFormatDetails` / 无 | 返回共享数据，**不需要释放** |
| `SDL_CreateRGBSurfaceWithFormatFrom` | `SDL_CreateSurfaceFrom` | 参数表变了，需手改 |
| `SDL_MapRGBA` | `SDL_MapSurfaceRGBA` | 需要 surface 而不是 format |
| `SDL_CondWaitTimeout` | `SDL_WaitConditionTimeout` | 改返回 bool |
| `SDL_GameController*` | `SDL_Gamepad*` | 头文件 `SDL_gamepad.h`；`SDL_INIT_GAMECONTROLLER`→`SDL_INIT_GAMEPAD` |
| `SDL_ShowCursor(1/0)` | `SDL_ShowCursor()` / `SDL_HideCursor()` | 拆成两个函数 |
| `SDL_StartTextInput()` / `StopTextInput()` | 都要传 `SDL_Window*` | |

`SDL_PointInRect` **不用改**（SDL3 仍保留整型版本；我们另用的 `SDL_PointInRectFloat` 也在）。

### B. 语义漂移（改一行，但要读清楚）

- `SDL_PauseAudioDevice(dev, 1)` → `SDL_PauseAudioDevice(dev)`，恢复要用 `SDL_ResumeAudioDevice()`。
- `SDL_WarpMouseInWindow` 收 **float** 坐标（原来是 int）。
- `SDL_CreateThread(fn, name, data)` → 需补 `pfnBegin/pfnEnd` 两个参数，或改用 `SDL_CreateThreadWithProperties`。
- `SDL_AddEventWatch` 回调返回值 `int` → `bool`。
- `SDL_Init` 的 `SDL_INIT_EVERYTHING` 已删；`SDL_INIT_GAMECONTROLLER` 改名 `SDL_INIT_GAMEPAD`。我们的旗标是 `SDL_INIT_VIDEO | SDL_INIT_AUDIO (| GAMECONTROLLER)`，只改最后一个。

### C. 必须重写（真正的工作量）

**C1 音频（最大一块）**

`SDL_LockAudioDevice` / `SDL_UnlockAudioDevice` **在 SDL3 里已删除**（回调不再跑在别的线程）。
实测命中：设备锁 17 + 19 处、`SDL_OpenAudioDevice` 2 处、`Pause` 3 处、`Close` 1 处，共 **36 处**，
加上 `src/audio_mixer.c`（807 行）与 `include/sts_mixer.h` 的回调结构，需要改成 stream 模型：

```
SDL_OpenAudioDevice(...)      →  SDL_OpenAudioDeviceStream(dev, &spec, cb, user)
回调里写缓冲                  →  SDL_PutAudioStreamData(stream, buf, len)   （或 pull 模型）
SDL_Lock/UnlockAudioDevice    →  SDL_LockAudioStream / SDL_UnlockAudioStream（多数情况不需要了）
```

风险：本引擎对时序敏感（`[BATTLE-FLAT-CLOCK]`、演出节奏都靠帧/时钟），音频重写后**必须重听**
关键场景（开场演出、点击音、战斗音效），不能只验「有声音」。

**C2 事件结构**

`SDL_KeyboardEvent.keysym` 字段被删：`e.key.keysym.scancode` → `e.key.scancode`、`.mod` → `e.key.mod`；
`SDL_Keysym` 类型不存在；`state` 改 `down`（bool）。鼠标事件的坐标改 float。
`src/input.c` 899 行是主战场，但改动形态统一（机械替换 + 编译报错兜底）。

**C3 Android 专用调用**

`SDL_AndroidSendMessage` **在 SDL3 里已不存在**（`SDL_system.h` 里已核，0 命中）。
我们只有 1 处（`src/hll/SystemService.c:336`，`#ifdef __ANDROID__` 分支，用于打开说明书）。
替代：直接 JNI（`SDL_AndroidGetJNIEnv` + `SDL_AndroidGetActivity`）或 `SDL_OpenURL`。

### D. 构建与打包

- `meson.build`：`dependency('sdl2')` → `dependency('sdl3')`，并加最低版本（建议 `>=3.2`）。
- 静态链接：`static_libs` 开关在 SDL3 仍然可用（`libSDL3.a`）。
- Android：SDL3 官方模板用 **prefab + Gradle**，要求 `minSdk ≥ 21`；我们的 APK 流水线要跟着改，
  且 SDL3 的 Android 入口点仍由 `SDL_main` 接管，`AndroidManifest` 的 Activity 名不变。

## 三、风险清单（按严重度）

| # | 风险 | 影响 | 缓解 |
|---|---|---|---|
| 1 | 音频重写引入时序/爆音回归 | 高 | 单独一个提交，A/B 开关 `XSYS4_SDL2_AUDIO=1` 暂时保留旧路径 |
| 2 | 上游分叉成本 | 中高 | `nunuhara/xsystem4` 至今 **0 个 SDL3 提交**（本次已查），我们改的 62 个文件后续每次同步上游都要解冲突 |
| 3 | bool 返回值翻转漏改 | 中 | 只有 5 处 `< 0`/`== -1`，逐个改成 `!SDL_...()`；编译告警 + 跑测兜底 |
| 4 | Android 打包链 | 中 | 先做桌面，APK 单独一个阶段，不和逻辑修复混在一起 |
| 5 | 验证成本 | 中 | 本项目每轮真流程跑测 5 分钟起，SDL3 后需要重跑全部基线（12 屏 ADV + 场景序列） |

## 四、建议路径（若要动）

**不要一次性替换**。分四步，每步都有可判定的完成条件：

| 阶段 | 做什么 | 判据 |
|---|---|---|
| 0 | 用 SDL 官方 `rename_symbols.py` 干跑一遍，产出改名清单 | 清单能覆盖我们 67 个函数里的绝大多数 |
| 1 | 新建分支 `sdl3`，加一层 `src/sdl_compat.h`（只做**改名映射**，不改行为） | 桌面能编过、能起窗、ADV 第一屏与 SDL2 版**逐像素可比** |
| 2 | 音频改 stream 模型（单提交 + 开关） | 开场演出/点击音/战斗音效听感正常，且 `[BATTLE-FLAT-CLOCK]` 类时序指标不变差 |
| 3 | Android 打包（prefab/Gradle + JNI 替代 `SDL_AndroidSendMessage`） | arm64 APK 在 realme RMX3700 上起窗、能操作 |

兼容头的价值：**上游仍是 SDL2**，有了这层映射，将来上游改 SDL2 代码时可以低成本跟；
没有它，62 个文件的 diff 会让每次上游同步都变成手工活。

## 五、我的判断（什么时候值得做）

**现在不值得。** 理由不是技术难，而是**收益为零**：

- 当前阶段最大的卡点是 `[TEXTBUTTON-CLICK]`（整类 TextButton 点击注入链路缺失）和战斗侧那几条，
  这些与 SDL 版本**没有关系**；迁移完 SDL3，第一章依然卡在第 37 句。
- 交付目标是 arm64 APK，SDL2 在 Android 上是成熟路径；SDL3 在 Android 的收益（更好 IME、HiDPI、
  高刷）要等 UI 真跑起来才体现得出来。

**值得做的时机**（满足任一）：

1. 第一章 + 战斗在 SDL2 上跑通，进入「上架/长期维护」阶段（那时 SDL3 的 Android 与 Wayland 收益才兑现）；
2. 上游 `nunuhara/xsystem4` 自己迁到 SDL3（那时我们从「分叉」变成「跟随」，成本反转）；
3. 遇到 SDL2 无法解决的硬问题（例如某个 Android 输入/HiDPI 缺陷）。

## 附：本次核对过的 SDL3 符号（避免下次重复查）

已确认 **SDL3 仍存在**：`SDL_GetTicks`、`SDL_GetTicksNS`、`SDL_OpenURL`、`SDL_ShowSimpleMessageBox`、
`SDL_LoadFile`、`SDL_CreateThread`、`SDL_CreateSurfaceFrom`、`SDL_PointInRect`、`SDL_PointInRectFloat`、
`SDL_GetRectUnion`、`SDL_GetRectIntersection`、`SDL_WaitConditionTimeout`。

已确认 **SDL3 不存在**：`SDL_GetTicks64`、`SDL_UnionRect`、`SDL_IntersectRect`、`SDL_FreeFormat`、
`SDL_LockAudioDevice`、`SDL_UnlockAudioDevice`、`SDL_AndroidSendMessage`、`SDL_Keysym`。

补充：`SDL_Renderer` / `SDL_Texture` 在 SDL3 **仍然存在**，只是 API 有较多调整 —— 本项目两个都没用
（渲染走裸 OpenGL），所以这一大块改动对我们不适用，这也是本次评估判定「工作量小」的主要依据。
