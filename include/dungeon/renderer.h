/* 版权所有 (C) 2021 kichikuou <KichikuouChrome@gmail.com>
 *
 * 本程序是自由软件：你可以依据自由软件基金会发布的 GNU 通用公共许可证
 * （第 2 版，或你选择的任何更高版本）的条款，重新分发和/或修改本程序。
 *
 * 分发本程序是希望它有用，但不作任何担保，甚至不担保适销性或
 * 适用于特定用途。详见 GNU 通用公共许可证。
 *
 * 你应当已经随本程序收到一份 GNU 通用公共许可证的副本；
 * 如果没有，见 <http://gnu.org/licenses/>。
 */

#ifndef SYSTEM4_DUNGEON_RENDERER_H
#define SYSTEM4_DUNGEON_RENDERER_H

#include <cglm/cglm.h>
#include "gfx/gl.h"

struct texture;
struct dgn_cell;
struct dtx;
struct polyobj;
struct dungeon_renderer;

struct drawfield_character {
	vec3 pos;
	int sprite;
	int rows;
	int cols;
	int cg_index;
	bool show;
};

struct dungeon_renderer *dungeon_renderer_create(enum draw_dungeon_version version, int num, struct dtx *dtx, GLuint *event_textures, int nr_event_textures, struct polyobj *po);
void dungeon_renderer_free(struct dungeon_renderer *r);
void dungeon_renderer_render(struct dungeon_renderer *r, struct dgn_cell **cells, int nr_cells, struct drawfield_character *characters, mat4 view_transform, mat4 proj_transform);
void dungeon_renderer_enable_event_markers(struct dungeon_renderer *r, bool enable);
bool dungeon_renderer_event_markers_enabled(struct dungeon_renderer *r);
bool dungeon_renderer_is_floor_opaque(struct dungeon_renderer *r, struct dgn_cell *cell);
void dungeon_renderer_run_post_processing(struct dungeon_renderer *r, struct texture *src, struct texture *dst);
void dungeon_renderer_set_raster_scroll(struct dungeon_renderer *r, int type);
void dungeon_renderer_set_raster_amp(struct dungeon_renderer *r, float amp);
void dungeon_renderer_enable_lightmap(struct dungeon_renderer *r, bool enable);
bool dungeon_renderer_get_draw_obj_flag(struct dungeon_renderer *r, int type);
void dungeon_renderer_set_draw_obj_flag(struct dungeon_renderer *r, int type, bool flag);

#endif /* SYSTEM4_DUNGEON_RENDERER_H */
