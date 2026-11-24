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

兼容头的价值：让"照着 SDL2 写法改"的迁移成本降到最低（62 个文件的 diff 若逐个改 API，
每处都要查 SDL3 语义）。**注意：本 fork 已不跟进上游**（2026-11-19 定），所以这层的收益
只剩"降低本仓库内部后续改动的摩擦"，不再是"方便跟上游"。

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


---

## 决策二：上游跟进 vs 自主（SDL3 的前提问题）

**先纠正一个事实**：`nunuhara/xsystem4` **没有停更** —— 最近一次 push 是 `2026-11-14`（本次核查时 4 天前），
而且我们**落后 14 个提交**。所以「得不到上游支持」这个前提不成立。

但「上游的支持不解决我们的痛处」成立。逐条判定（本次实测）：

| 上游提交 | 规模 | 与本仓库的关系 | 判定 |
|---|---|---|---|
| `parts: Implement activity system` | 1054 行（`activity.c` 922） | 他们是 `.pact` 二进制解析器；我们 `activity.c` **1728 行**是另一套（`ex_tree` + `uc_*`，为兰斯10 手写） | 两套实现，**不建议整体并** |
| `PartsEngine: Accept controller IDs in Add/RemoveController` | 19 / 29 行 | **方向相反**：他们判 `id < nr_controllers`（按下标），Rance10 实测传稳定 id（`SetActive no=10` 而只有 6 层），采纳会 `VM_ERROR` | **不采纳** |
| `parts: change origin mode 0 to top-left` | **1 行** | 我们确实是旧逻辑：`default: return { mode, (3*h)/4 }`（mode 0 会往下推 0.75 个高度） | 已加 `XSYS4_ORIGIN_MODE0_TL` 开关；**首测画面无差异**，待更多场景再判 |
| `parts: Implement PartsFunc 46 (GetAbsoluteMaxZ)` | 18 行 | 我们没实现；但 Rance10 的 lib 表里**没有这个名字**（启动无 `Unimplemented` 警报） | 备着，当前零收益 |
| `parts: Implement PartsFunc 158 (SetFlatUpdateSpeedRate)` | 18 行 | 同上 | 备着 |
| `parts: Add PE_add_construct_process` | 160 / 119 行 | 动 `construction.c`，而我们在这块有 D224 特判 | 需单独评估 |
| 字体 3 个提交 | 10 行 | ①比例字宽是 **opt-in**，默认仍等宽 → 默认零影响；②`CharSpriteManager` 用 extent 不裁宽字 → 只影响角色精灵 | 低优先 |
| `PartsEngine: Add Evenicle exports` | 93 行 | 纯增量导出，服务 Evenicle | 无收益 |

**关键结论（决定 SDL3 时机）**：这 14 个提交动的文件是 `parts*.c` / `activity.c` / `font*.c` / `text.c` / `system4.c`，
**没有一个是 SDL 接线文件**（`video.c` / `input.c` / `audio_mixer.c` / `draw.c` 全未涉及）。
也就是说：**换 SDL3 与跟上游同步，这两件事在文件层面不重叠** —— 上一版「先吸收上游再换 SDL」的顾虑可以撤销。

至于 `[TEXTBUTTON-CLICK]`：上游代码里 `TextButton` / `MouseLClick` / `CallEvent` **一个引用都没有**
（`git grep` 全空），所以这条卡点**不能指望上游**，只能我们自己查。

---

## 决策三：SDL3 迁移已实施到「能起窗」（分支 `sdl3`）

**决定依据（2026-11-18 用户确认）**：上游明确表示**只维护现有项目修 bug**，
不会往兰斯10 及之后的作品方向发展，也不提供技术支持。于是「跟上游一致」不再是
保留 SDL2 的理由 —— 这条前提撤销后，迁移 SDL3 从「不划算」变成「划算」。

**隔离方式**：独立 worktree `../xsystem4-sdl3`（当时分支 `sdl3`），`main` / `rance10-native` 全程未动。
> **2026-11-19 更新**：`sdl3` 已并入 `main` 并成为主线（`main` 直接指向该历史，旧快照链存于
> `main-archive-20261119`）。下面这段是迁移当天的记录，保留原样。

### 实测进度

| 阶段 | 结果 |
|---|---|
| `#include <SDL3/...>` + `dependency('sdl3')` | **179 错误 / 40 目标**（11 秒报完） |
| 打开 `SDL_ENABLE_OLD_NAMES`（SDL3 自带旧名兼容层） | **99 错误**（mutex 类型一类全消失） |
| 逐类修完 + 音频改 stream 模型 | **0 错误，链接通过（6.2 MB 二进制）** |
| 在隔离 Xvfb 里启动 | **成功起窗**，标题画面（字体/立绘/按钮/背景）与 SDL2 版**目视无差异** |
| 输入等价性 | 同一条跑测序列下，SDL2 与 SDL3 **收到的鼠标事件逐条一致**（`[mouse]` 各 14 条，坐标分布相同） |

### 改了哪 12 个文件（`git show --stat`）

`meson.build`（`sdl3` + `-DSDL_ENABLE_OLD_NAMES` + `-include src/sdl3_compat.h`）、
`src/sdl3_compat.h`（新增 21 行：补 `math.h`/`stdlib.h`/`ctype.h`）、
`src/input.c`（92 行改动：`keysym` 拍平、`state`→`down`、窗口事件改区间、
触摸/手柄成员改名、文本输入包一层）、`src/video.c`（30 行：像素格式 details、
`CreateWindow` 4 参、`FULLSCREEN`、`GetWindowSizeInPixels`、`MapRGBA` 加 palette）、
`src/audio_mixer.c`（122 行：**唯一的实质重写**，见下）、
`include/msgqueue.h` + `src/msgqueue.c`（cond → condition）、
`include/gfx/private.h`、`include/input.h`、`src/icon.c`、`src/hll/InputString.c`、`src/debugger_dap.c`。

### 音频为什么是唯一「实质」改动（D330）

SDL3 删除了回调式音频：`SDL_LockAudioDevice` / `SDL_UnlockAudioDevice` /
`SDL_PauseAudioDevice(dev, 0)` / `SDL_AudioSpec.callback` / `.samples` 全部消失。
映射成 SDL3 的 stream 模型（保持混音线程模型不变，把风险压到最小）：

| SDL2 | SDL3 |
|---|---|
| `SDL_OpenAudioDevice(NULL,0,&want,&have,0)` | `SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&want,cb,NULL)` |
| `SDL_PauseAudioDevice(dev, 0)` | `SDL_ResumeAudioStreamDevice(stream)` |
| `SDL_LockAudioDevice` / `UnlockAudioDevice` | `SDL_LockAudioStream` / `UnlockAudioStream`（封装成 `AUDIO_LOCK/UNLOCK`，无设备时空转） |
| 回调 `(userdata, out, len)` 往缓冲写 | 回调 `(userdata, stream, additional_amount, total_amount)` 里 `SDL_PutAudioStreamData` 推流 |

**未验收**：本机无音频设备，只验证了「不开音频不崩」与「编译链接通过」。
音色/时序必须在有声环境里听一遍（尤其演出节奏与战斗音效），这是合并前的前置条件。

### 迁移中丢掉的唯一行为（已记录）

`SDL_MULTIGESTURE`（两指位移手势）在 SDL3 被删除，替代品 PINCH 语义是**缩放**不是平移，
无法直接换算滚轮。影响面只有「Android 上两指滑动滚列表」（滚轮/拖动仍在）。
补回来需要按 `SDL_EVENT_PINCH_UPDATE` 的 scale 变化重新设计 `SCROLL_GESTURE_SENSITIVITY`。

### 落地状态（2026-11-19 更新）

| 待办 | 状态 |
|---|---|
| 有声环境听音（演出 / 战斗音效时序） | **未做** —— 本机无音频设备，仍只有「不开音频不崩」这一条证据 |
| Android 打包：`SDL_AndroidSendMessage` 没有 SDL3 版本（`SystemService.c:336`，仅 1 处） | **未做**，需换 JNI 或 `SDL_OpenURL` |
| ADV 端到端 | **已通过**（D332/D333 修点击捕获与释放沿，D337 补上引擎→AIN 按钮对象的派发；第一章能从标题走到第一场战斗） |
| 合入策略 | **已变更两次**：① 不再压提交回 SDL2 的 `rance10-native`；② 2026-11-19 起 `main` 直接指向 SDL3 这条历史（不再用同树快照），成为唯一主线（见 `README.md` 分支表） |

累计改动的文件与逐条理由见上方「改了哪 12 个文件」+ `git log --oneline origin/main..origin/sdl3`。
D331–D337 的排障过程在 `docs/TECH_DEBT.md` / `archive/`，可复用方法论已回流 `docs/GUIDE/`。

---

## 决策二：Vulkan 支持（D517k）

> 结论先行：**路径已搭好，但当前环境还没跑通**。代码与工具链就绪
> （`XSYS4_RENDER_BACKEND=vulkan`），失败时安全回退 GL，不影响默认构建。
> 卡点在 `SDL_CreateGPUDevice`：引擎里返回 `unsupported`，而**同环境
> 同库的裸测试程序能成功** —— 待续排查。

### 一、现状量化（决定为什么不能"顺手换"）

| 项目 | 实测值 |
|---|---:|
| `gfx` 公开 API | **111 个** |
| `src/video.c`（GL 核心） | 932 行 |
| `src/draw.c`（混合/旋转/3D） | 1236 行 |
| `gl*` 调用点 | 约 300 处，其中 `glBlendFuncSeparate` **87 处** |
| 离屏渲染 | `main_surface` + `main_surface_fb`（视觉特效靠它） |

**结论**：2D 渲染是裸 GL 紧耦合，全量迁 Vulkan 是 **3000+ 行新代码**，
不是一两天的活。

### 二、两条路线

| | SDL_GPU（已选，先做） | 原生 Vulkan |
|---|---|---|
| 代码量 | 小 5–10 倍 | 3000+ 行 |
| 覆盖 | Vulkan / Metal / D3D12（一份代码） | 只有 Vulkan |
| 依赖 | SDL3 自带（`SDL_gpu.h`） | vulkan-loader + 手写 instance/swapchain/pipeline |
| 风险 | 新 API，仍有 2D 迁移成本 | 巨量样板代码 |

**选 SDL_GPU**：它能同时满足"支持 Vulkan"和将来 Android/Metal 的诉求，
且已在 `SDL_CreateGPUDevice(..., "vulkan")` 上验证过驱动选择逻辑。

### 三、已落地（本次）

1. **SPIR-V 工具链**：`shaders/preview3d_vk.{vert,frag}` +
   `tools/gen-spv.sh` → 生成 `src/preview3d_vk_spv.h`（嵌入字节数组，
   运行期不依赖 shader 文件）。本机 `glslc` / `spirv-val` 均可用。
2. **`src/gfx_vk.c`**（SDL_GPU，Vulkan 驱动）：instance/device 探测、
   图形管线（顶点色、TRIANGLELIST/STRIP、alpha 混合）、动态顶点缓冲、
   render pass 清屏、`gfx_vk_draw_faces()`（每面一次 push uniform）。
3. **`src/parts/preview3d.c` 双路径**：相机/面矩阵/颜色/雾化算一次
   （`pq_build_faces`），GL 走 `parts_draw_solid_quad`、Vulkan 走
   `gfx_vk_draw_faces`。
4. **`video.c` 后端切换**：`XSYS4_RENDER_BACKEND=vulkan` 时窗口用
   `SDL_WINDOW_VULKAN`、跳过 GL context 与所有 GL 初始化；
   **先建隐藏窗口探测**，拿不到设备就回退 GL（窗口只能属于一个 API，
   回退必须重建）。
5. **GL 调用的 guard**：`gfx_clear` / `gfx_swap` / `init_texture` /
   `gfx_init_texture_with_pixels` / `gfx_fini` 在 Vulkan 模式下跳过或 no-op
   （纹理 handle 留 0，让所有 GL 绘制 early-return）。

### 四、卡点（下次接着查的第一件事）

```
[vk] SDL 可用 GPU 驱动数=1
[vk]   driver[0]=vulkan
[vk] SDL_CreateGPUDevice(NULL) 失败：No supported SDL_GPU backend found!
[vk] 点名 vulkan 也失败：SDL_HINT_GPU_DRIVER vulkan unsupported!（放弃）
```

**但**：同环境（`DISPLAY=:96`，同一个 `/usr/lib/libSDL3.so.0`）下，
裸程序 `SDL_Init(VIDEO|AUDIO|GAMEPAD)` + `SDL_CreateWindow(1920x1080,
VULKAN|RESIZABLE)` + `SDL_CreateGPUDevice(SPIRV, false, NULL)` → 成功，
`driver=vulkan`。

已排除（都是实测，不是猜）：
- `SDL_GL_SetAttribute` 干扰（已放进 `if (!vk_mode)`）
- `SDL_INIT_AUDIO` / `SDL_INIT_GAMEPAD` / 窗口尺寸 1920×1080 / `RESIZABLE`
  （裸程序 mode 0–4 全排列均成功）
- 跑测脚本的 `SDL_VIDEODRIVER=x11` / `LIBGL_ALWAYS_SOFTWARE` / `GALLIUM_DRIVER`
- `src/sdl3_compat.h`（21 行，只有标准库 include）
- 是否显式 `SDL_Vulkan_LoadLibrary`（加与不加都失败）
- **video driver 不是 x11**（引擎里实测 `current video driver = 'x11'`）
- **surface 扩展缺失**（引擎里实测 `instance extensions = 2`）
- **库冲突**（把引擎 `ldd` 的 139 个库全链接进测试程序，仍 `driver=vulkan`）
- **Xvfb 参数不同**（在跑测**进行中**的同一个 `:96`、`1920x1080x24` 上跑
  测试程序，仍成功）
- **主线程状态**（`SDL_IsMainThread()` 通过，代码走到了 `SDL_CreateGPUDevice`）

**因此失败点在 SDL 的 VULKAN_Init 内部、`SDL_Vulkan_GetInstanceExtensions`
之后**（即 `vkCreateInstance` / `vkEnumeratePhysicalDevices` 一带），
且只在引擎进程里复现。裸程序与引擎的差异收敛到「进程已运行若干秒、
已做过配置解析与 HLL 链接」这一层。

**下一步（按性价比排序）**：
1. **gdb 断点**：`break VULKAN_Init`（SDL 内部符号，需要 debug 版 SDL 或
   `dlopen` 后在 `SDL_gpu_vulkan.c` 的行号下断），单步看 return false 的那一行。
   这是唯一能一次定位的办法。
2. **二分 `main()`**：把引擎的初始化逐段注释（配置解析 → HLL 链接 → AEL 加载
   → `gfx_init`），找出哪一步之后 `SDL_CreateGPUDevice` 开始失败。
3. **绕开 SDL_GPU**：直接用原生 Vulkan（`vkCreateInstance` + `SDL_Vulkan_CreateSurface`）。
   代码量 3000+ 行但不受 SDL 内部状态影响 —— 若 1/2 都卡住，走这条。

### 四·五、卡点破案（D521，2026-11-25）

**根因链（全部实证）**：
1. `VK_LOADER_DEBUG=all` + gdb（`vkEnumeratePhysicalDevices`/`SDL_LogWarn`/`GetPresentationSupport` 三断点）+ 裸程序探测三路交叉：
   - vkCreateInstance ✓、vkEnumeratePhysicalDevices ✓（Intel ADL-N 枚举成功）
   - features 7 项全 1 ✓、VK_KHR_swapchain/maintenance1 ✓（裸程序 /tmp/vkfeat 实证）
   - 失败在 **queue family 的 present support 全 false**（`SDL_Vulkan_GetPresentationSupport`→X11 钩子→`vkGetPhysicalDeviceXlibPresentationSupportKHR`）
2. **present=false 的原因**：`MESA: info: vulkan: No DRI3 support detected - required for presentation`——**Xvfb 没有 DRI3 扩展**，mesa 的 WSI 检查直接拒绝 → 无 suitable queue → 「Failed to determine a suitable physical device」→ 「No supported SDL_GPU backend found」→ 回退 GL。
3. **决定性对照**：gdb 会话未 unset `WAYLAND_DISPLAY` → SDL 选 **wayland** 驱动 → **`[vk] Vulkan 路径就绪：driver=vulkan swapchain_fmt=12`**——Vulkan 探测直接成功！代码路径完全可用，x11/Xvfb 失败纯粹是**环境限制**（Xvfb 无 DRI3）。
4. 裸程序复跑差异的教训：手工 gdb 时 WAYLAND_DISPLAY 残留改变了 SDL 的 video 后端选择——run-headless 的 `unset` 纪律（D249）正是为了消除这种变量。

**解锁路径**：
- **用户真实桌面（wayland，WAYLAND_DISPLAY=wayland-1）**：Vulkan 应直接可用——`run-local.sh` 授权后一轮即可验证 P1（3D 预览出图）。
- **headless 跑测**：装 `vulkan-swrast`（lavapipe，软件 Vulkan；ICD `libvulkan_lvp.so` 当前缺失）——lavapipe 的 present 不依赖 DRI3，可让 Vulkan 全链在 Xvfb 可测（性能同 llvmpipe 量级，仅验证渲染正确性）。
- gdb 下曾见 SIGSEGV @ `VULKAN_INTERNAL_OnWindowResize`（wayland 探测成功后、GL 回退流程中）——独立小 bug，待 Vulkan 主链通后处理。

### 四·六、全链打通 + headless present 硬限制（D521b，2026-11-25 续）

装 `vulkan-swrast`（lavapipe）后 headless 复跑：
1. **`[vk] probe: driver='vulkan' -> OK`**——无窗口纯设备探测成功（d810 曾核心转储的
   probe-claim-destroy 竞态已修：probe 与 init 分离，probe 不 claim 任何窗口；
   SIGSEGV @ `VULKAN_INTERNAL_OnWindowResize` 就此消失）。
2. **`[vk] Vulkan 路径就绪：driver=vulkan swapchain_fmt=12`**——主窗口 CreateGPUDevice +
   ClaimWindowForGPUDevice 成功，swapchain 格式拿到。**渲染初始化链全部打通**。
3. **剩余硬限制**：引擎卡死（d812 与 d811 日志一字不差）在**第一次 present**——
   mesa 的 wsi_x11 swapchain present 走 **DRI3**（`MESA-EGL: DRI3 error: Could not get
   DRI3 device`），Xvfb 无 DRI3 → AcquireNextImage/present 阻塞。lavapipe 只把**渲染**
   软件化，present 仍要过 X 的 DRI3。
4. **结论**：Xvfb（无 DRI3）下 Vulkan 窗口呈现不可行；Vulkan 渲染验证必须在**真实桌面**
   （wayland 会话 DRI3 正常，用户桌面 WAYLAND_DISPLAY=wayland-1 即此）。

**下一步**：用户授权 `run-local.sh`（真实桌面开窗）即可验证 P1（3D 预览 Vulkan 出图）；
或研究离屏渲染路径（VK_EXT_headless_surface，SDL_GPU 不直接暴露，需绕行）。

### 五、分期计划（Vulkan 跑通探测之后）

| 期 | 内容 | 判据 |
|---|---|---|
| P1 | 探测通 + 3D 预览在 Vulkan 下出图 | X11 截图能看到立方体 |
| P2 | 纹理四边形 + 普通/加法混合（覆盖精灵与 CG） | 标题画面在 Vulkan 下可读 |
| P3 | 离屏 framebuffer（`main_surface`）+ 特效混合 | 视觉特效不黑 |
| P4 | 文字渲染（freetype 位图 → GPUTexture） | ADV 正文可见 |
| P5 | 全量回归 + 默认后端切换 | 冒烟 quick 全绿；`XSYS4_RENDER_BACKEND` 可双向切 |
