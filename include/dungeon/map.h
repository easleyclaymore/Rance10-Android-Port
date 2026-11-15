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

#ifndef SYSTEM4_DUNGEON_MAP_H
#define SYSTEM4_DUNGEON_MAP_H

struct dungeon_context;
struct dungeon_map;

struct dungeon_map *dungeon_map_create(enum draw_dungeon_version version);
void dungeon_map_free(struct dungeon_map *map);
void dungeon_map_init(struct dungeon_context *ctx);
void dungeon_map_update_cell(struct dungeon_context *ctx, int x, int y, int z);

void dungeon_map_draw(int surface, int sprite);
void dungeon_map_draw_lmap(int surface, int sprite);
void dungeon_map_set_radar(int surface, int flag);
void dungeon_map_set_all_view(int surface, int flag);
void dungeon_map_set_cg(int surface, int index, int sprite);
void dungeon_map_set_small_map_floor(int surface, int floor);
void dungeon_map_set_large_map_floor(int surface, int floor);
void dungeon_map_reveal(struct dungeon_context *ctx, int x, int y, int z, bool from_walk_data);
void dungeon_map_hide(struct dungeon_context *ctx, int x, int y, int z);

#endif /* SYSTEM4_DUNGEON_MAP_H */
