#ifndef SYSTEM4_PREVIEW3D_VK_H
#define SYSTEM4_PREVIEW3D_VK_H

#include <stdbool.h>
#include <SDL3/SDL.h>

/* [D517k] 3D 预览的 Vulkan 路径（src/gfx_vk.c，经 SDL_GPU 的 Vulkan 驱动）。
 *
 * 与 GL 路径**互斥**：SDL 窗口只能属于一个图形 API。启用后整帧由
 * Vulkan 负责，GL 的 gfx 绘制全部跳过（画面 = 清屏色 + 预览），
 * 用于验证 Vulkan 通路；2D 迁移见 docs/RENDER-BACKEND.md 的分期。 */
bool gfx_vk_active(void);
/* 在已创建好的窗口上初始化（会挑选 vulkan 驱动，拿不到就返回 false）。 */
bool gfx_vk_init(SDL_Window *win);
void gfx_vk_fini(void);
/* 只清屏（无绘制的 Vulkan 帧）。 */
void gfx_vk_clear_screen(void);

/* 画 n（<=6）个纯色四边形面：world[f] 把单位方形映射到该面，
 * colors[f] 是顶点色，view 是透视矩阵。 */
bool gfx_vk_draw_faces(const float (*world)[16], const float (*colors)[3],
		       int n, const float view[16]);

#endif
