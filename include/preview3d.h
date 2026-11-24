#ifndef SYSTEM4_PREVIEW3D_H
#define SYSTEM4_PREVIEW3D_H

#include <stdbool.h>
#include <cglm/cglm.h>
#include <SDL3/SDL.h>

/* 绘制原语（定义在 src/parts/render.c，复用 parts_shader 的 multiply_color） */
void parts_draw_solid_quad(mat4 world, mat4 proj, SDL_Color mul, float alpha);
void parts_draw_screen_rect(int x, int y, int w, int h, SDL_Color c, float alpha);

/* 3D 设置页的预览场景（src/parts/preview3d.c）。
 *
 * 语义来源见 docs/3D-SEMANTICS.md：原版预览由 PartsEngine 内部渲染，
 * 不经任何 HLL，也不是 PARTS_3DLAYER 部件。这里用「独立绘制原语 +
 * painter 排序」实现等价效果，插在 ctrl=1（标题/背景）与 ctrl=2
 * （配置页内容）之间。 */
void preview3d_set_active(bool on);
bool preview3d_active(void);
/* 每帧推进（ms 为经过时间）。 */
void preview3d_tick(int ms);
/* 在 (0,0,w,h) 上绘制预览。调用方负责保证 active。 */
void preview3d_render(int w, int h);
/* Vulkan(SDL_GPU) 路径：整帧（含清屏）由 gfx_vk 负责，无 GL 调用。 */
bool preview3d_render_vk(int w, int h);

/* 从当前 3D 页的勾选态刷新参数（定义在 activity.c，能按部件号读 checkbox）。 */
void preview3d_sync_params(void);

/* 设置项：直接对应 CAS3DSetting 的 Get*（见 docs/3D-SEMANTICS.md §二）。 */
void preview3d_set_param(int render_mode, int fog_mode, int edge, int dof);

#endif
