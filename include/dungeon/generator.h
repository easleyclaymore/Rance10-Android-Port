/* 版权所有 (C) 2025 kichikuou <KichikuouChrome@gmail.com>
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
#ifndef SYSTEM4_DUNGEON_GENERATOR_H
#define SYSTEM4_DUNGEON_GENERATOR_H

struct dgn;

enum drawfield_cell_flag {
	DF_FLAG_ITEM          = 1 << 0,
	DF_FLAG_ENEMY         = 1 << 1,
	DF_FLAG_TRAP          = 1 << 2,
	DF_FLAG_NO_TREASURE   = 1 << 3,
	DF_FLAG_NO_MONSTER    = 1 << 4,
	DF_FLAG_NO_TRAP       = 1 << 5,
	DF_FLAG_TREASURE_ROOM = 1 << 6
};

struct dgn *dgn_generate_drawdungeon2(int level);
void dgn_paint_step(struct dgn *dgn, int x, int y);

struct dgn *dgn_generate_drawfield(
	int floor, int complexity, int wall_arrange_method, int floor_arrange_method,
	int field_size_x, int field_size_y, int door_lock_percent, uint32_t seed,
	uint8_t *cell_flags);

#endif /* SYSTEM4_DUNGEON_GENERATOR_H */
