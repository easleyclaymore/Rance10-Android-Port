# 渲染后端与 SDL 版本：迁移可行性评估

> 评估日期 2026-11-18。针对「SDL2 → SDL3.4、OpenGL → Vulkan」的提案。
> 结论先行：**SDL3 可以做、但收益与代价都不在图形上；Vulkan 直接迁移会堵死 Web，
> 不建议。若目标是 Web，唯一现成路径是 GLES3/WebGL2。**

## 1. 现状量化（本仓库实测）

| 维度 | 数字 | 说明 |
|---|---:|---|
| 源码文件 | 250 | `src/` + `include/` |
| 直接用到 SDL 的文件 | 63 | 多数只用到 `SDL_Color`/`SDL_Rect`/scancode |
| SDL 符号去重数 | 227 | 其中 `SDL_GetTicks` 41 处、`SDL_Color` 114 处 |
| GL 相关文件 / 行数 | **8 / 5654** | `video.c` 824、`draw.c` 1236、`parts/render.c` 1330 为大头 |
| GL 函数去重数 | 87 | 无固定管线残留（`glBegin` 等 0 处） |
| 绘制调用点 | `glDrawArrays` 10 / `glDrawElements` 4 | 已经批次化，集中在一个 helper |
| 着色器 | **2 个**（`#version 300 es` / `#version 140`） | 都在 `src/video.c` |
| GL 上下文 | GLES 3.0 **或** GL 3.1 core | `meson -Dopengles` 已是双路径 |
| `libsys4` 子模块的 SDL 依赖 | **0 个文件** | 迁移不触碰 submodule |
| 上游 `nunuhara/xsystem4` master | 仍是 `dependency('sdl2')` | 迁移即长期偏离上游 |

结论：**图形层是一个小而现代的 2D 渲染器**（无固定管线、有 VAO/VBO、只有 2 个着色器），
不是那种需要彻底重写的遗留 GL 代码。这一点决定了下面的成本估计。

## 2. 四个方案 × 五个目标

| 方案 | Linux | Android | **Web** | macOS/Win | 代码量变化 |
|---|:--:|:--:|:--:|:--:|---|
| A. 现状（SDL2 + GL/GLES3） | ✅ | ✅ GLES3 | ✅ WebGL2（需 Emscripten 适配） | ✅ | 0 |
| B. SDL3 + 现有 GL | ✅ | ✅ | ✅（同上） | ✅ | 小（见 §3） |
| C. SDL3 + `SDL_GPU` | ✅ | ✅ | ❌ **无 Web 后端** | ✅ | 大（重写绘制层） |
| D. 裸 Vulkan | ✅ | ✅ | ❌ **浏览器不提供 Vulkan** | ⚠️ 需 MoltenVK | 最大 |

- 浏览器只暴露 **WebGL2（≈GLES3）** 与 **WebGPU**。Vulkan 在 Web 上没有任何实现路径。
- `SDL_GPU` 当前只实现了 **Vulkan / D3D12 / Metal** 三个后端，**不覆盖 Web**
  （这也是它"只抽象 2026 年值得抽象的三个 API"的设计取舍）。
- 因此：**「迁到 Vulkan 以便上 Web」这个前提不成立**——它正好相反。

## 3. SDL3 迁移的真实代价：不在图形，在音频

SDL3 删除了回调式音频模型：`SDL_LockAudioDevice` / `SDL_UnlockAudioDevice` **已移除**
（没有用户回调线程了），必须改走 `SDL_AudioStream` / `SDL_OpenAudioDeviceStream`。

| API | 本仓库用量 | SDL3 状态 |
|---|---:|---|
| `SDL_LockAudioDevice` | 17 | **已移除** |
| `SDL_UnlockAudioDevice` | 19 | **已移除** |
| `SDL_OpenAudioDevice` | 2 | 语义改为「逻辑设备 + stream」 |
| `SDL_AudioSpec` | 4 | 已移除 |
| `SDL_CreateThread` / `SDL_mutex` | 1 / 6 | 签名有变，改动小 |
| `SDL_WINDOWEVENT` | 2 | 改为顶层事件 |
| `SDL_GetTicks` / `SDL_GL_SetAttribute` | 41 / 6 | 不变 |

`libsys4` 完全不依赖 SDL，所以工作量集中在 `src/audio_mixer.c` 与 6 个左右的事件/线程调用点。
**估计 3–5 个工作日**（含一遍 12 屏 ADV 回归 + 音频听测）。

## 4. SDL_GPU 或 Vulkan 的代价

重写范围 = 8 个文件 / 5654 行里真正碰绘制的部分（`video.c` `draw.c` `parts/render.c`
`sprite.c` `effect.c` `movie_plmpeg.c` + `dungeon/*`）。

难点不是"画三角形"，而是这三点：

1. **纹理即状态**：引擎现在靠 `glBindTexture` + 逐部件 uniform 表达「这张图 + 这组 alpha/加法色」，
   换成 Vulkan 要变成描述符集与管线状态，绘制前必须按管线分批。
   `parts/render.c` 里 `alpha_clipper` 的实现（用另一张纹理做遮罩）在 Vulkan 下要重做成第二组采样器。
2. **帧缓冲**：现有 3 个 FBO（合成/截图/效果）要重做成 render pass + 离屏附件。
3. **基础设施**：裸 Vulkan 还要自己写 ~3–5k 行（instance/device/swapchain/pipeline/descriptor/
   frames-in-flight）。`SDL_GPU` 把这部分省掉，但仍然要重写上面 1、2 两条。

估计：**SDL_GPU 路线 2–4 周；裸 Vulkan 4–8 周**，且两者都把 Web 排除在外。

## 5. 更硬的事实：Web 的真正门槛不在图形

- 游戏数据目录 **3.8 GB**（`Rance10CG.afa` 等）。Web 上不可能整包预载，必须做
  按需 range-request + 缓存；这是**内容分发**问题，换图形 API 一点忙都帮不上。
- 运行时 RSS 约 **305 MB**，wasm32 勉强够用，但没有余量。
- 视频用了外挂 `libavcodec`（`movie_ffmpeg.c`），Web 上要换成 `<video>` 或 wasm 解码器。
- AIN 解释器是**热路径**，wasm 下的性能尚未测过。

## 6. 建议

**现在做：什么都不做。**

理由：当前第一章卡在 `[TEXTBUTTON-CLICK]`（AIN 委托派发）与战斗入口，
**瓶颈 100% 在字节码语义层，不在渲染层**。这一周已实测：headless 软件渲染 13.5 fps、
真机 GL 99.7 fps —— 图形从来不是性能瓶颈。此时把 5654 行渲染层推倒重来，
只会让取证节奏停摆，而且不解决任何一个当前 bug。

**如果要做现代化，顺序应当是：**

1. **把 GL 收敛成 GLES3 单一路径**（删掉桌面 `#version 140` 分支，只留 `300 es`）。
   代价极小（`video.c` 里两个着色器二选一），收益是**同一份代码同时覆盖 Android 与 WebGL2**。
   这是通向 Web 的唯一低成本动作。
2. **等第一章可玩、战斗能进之后**，再评估 SDL3（为了 SDL2 进入维护模式的长期风险）。
   届时应连带把音频改成 stream 模型（§3）。
3. **只有在明确放弃 Web、且要做 3D/战斗特效重活时**，才考虑 `SDL_GPU`。
   那时它比裸 Vulkan 便宜得多，且能同时覆盖 Linux/Android/macOS/Win。**永远不要裸写 Vulkan。**

**改主意的信号**（满足任一才值得动）：

- 真机上出现 GL 驱动兼容性问题（尤其国产 Android 机型），且 GLES2/3 兜底失效；
- 战斗特效需要 compute / 大量 draw call，GL 实测撑不住（要有帧率数据，不是感觉）；
- 官方明确 Web 版是交付目标之一 —— 那时应当**立刻做第 1 步**，而不是换图形 API。

## 7. 一句话给决策者

> 换 Vulkan 是把「未来可移植」换成「现在就不可移植到 Web」；
> 想上 Web，正确动作是把 OpenGL 收敛到 GLES3，而不是升级图形 API。
> SDL3 值得做，但它是音频工程，不是图形工程。
