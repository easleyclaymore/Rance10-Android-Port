/* 版权所有 (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>

#include "system4/file.h"
#include "system4/mt19937int.h"
#include "system4/little_endian.h"

#include "hll.h"
#include "xsystem4.h"

/*
 * struct MapHeader {
 *   char magic[4];  // "Map\0"
 *   // the below are encrypted
 *   uint32_t width;
 *   uint32_t height;
 *   uint32_t cg_table_offset;
 *   uint32_t move_table_offset;
 *   uint32_t figure_table_offset;
 *   uint32_t info_table_offset;
 *   uint32_t map_no;
 * };
 *
 * struct CGTable {
 *   uint32_t count;
 *   uint32_t cg[count];
 * };
 *
 * struct MoveTable {
 *   bool movable[height][width];
 * };
 *
 * struct FigureTable {
 *   uint32_t count;
 *   struct {
 *     uint8_t x;
 *     uint8_t y;
 *     uint16_t unit;
 *   } figures[count];
 * };
 *
 * struct InfoTable {
 *   uint32_t item;
 *   uint32_t gold;
 *   uint32_t turn;
 *   uint32_t level;
 *   uint32_t unknown;  // checksum?
 * };
 */

static uint8_t *map_data;
static size_t map_size;

static char *mapfile_path(int map_no)
{
	char buf[32];
	sprintf(buf, "Map/%04d.map", map_no);
	return gamedir_path(buf);
}

HLL_QUIET_UNIMPLEMENTED(, void, vmMapLoader,Init, void *imainsystem, struct string *string);

static int vmMapLoader_Exist(int map_no)
{
	char *path = mapfile_path(map_no);
	bool result = file_exists(path);
	free(path);
	return result;
}

static int vmMapLoader_New(int map_no)
{
	if (map_data)
		free(map_data);

	char *path = mapfile_path(map_no);
	map_data = file_read(path, &map_size);
	if (!map_data) {
		WARNING("Cannot read %s", path);
	} else if (memcmp(map_data, "Map", 4)) {
		WARNING("%s: Wrong magic bytes", path);
		free(map_data);
		map_data = NULL;
	} else {
		mt19937_xorcode(map_data + 4, map_size - 4, 12753);
	}

	free(path);
	return !!map_data;
}

void vmMapLoader_Release(void)
{
	if (map_data) {
		free(map_data);
		map_data = NULL;
	}
}

static int vmMapLoader_GetWidth(void)
{
	if (!map_data)
		return -1;
	return LittleEndian_getDW(map_data, 4);
}

static int vmMapLoader_GetHeight(void)
{
	if (!map_data)
		return -1;
	return LittleEndian_getDW(map_data, 8);
}

static int vmMapLoader_GetBaseCG(int index)
{
	if (!map_data)
		return -1;
	int nr_base_cg = LittleEndian_getDW(map_data, 32);
	if (index < 0 || index >= nr_base_cg)
		return -1;
	return LittleEndian_getDW(map_data, 36 + index * 4);
}

static int vmMapLoader_GetNumofBaseCG(void)
{
	if (!map_data)
		return -1;
	return LittleEndian_getDW(map_data, 32);
}

static int vmMapLoader_IsMove(int x, int y)
{
	if (!map_data)
		return -1;
	int w = LittleEndian_getDW(map_data, 4);
	int h = LittleEndian_getDW(map_data, 8);
	if (x < 0 || x >= w || y < 0 || y > h)
		return 0;
	uint8_t *table = map_data + LittleEndian_getDW(map_data, 16);
	return table[y * w + x];
}

static int vmMapLoader_GetFigure(int x, int y)
{
	if (!map_data)
		return 0;

	int offset = LittleEndian_getDW(map_data, 20);
	int num = LittleEndian_getDW(map_data, offset);
	uint8_t *table = map_data + offset + 4;

	for (int i = 0; i < num; i++) {
		if (x == table[i * 4] && y == table[i * 4 + 1])
			return LittleEndian_getW(table, i * 4 + 2);
	}
	return 0;
}

static int vmMapLoader_GetItemNum(void)
{
	if (!map_data)
		return -1;
	int offset = LittleEndian_getDW(map_data, 24);
	return LittleEndian_getDW(map_data, offset + 0);
}

static int vmMapLoader_GetGoldNum(void)
{
	if (!map_data)
		return -1;
	int offset = LittleEndian_getDW(map_data, 24);
	return LittleEndian_getDW(map_data, offset + 4);
}

static int vmMapLoader_GetTurnNum(void)
{
	if (!map_data)
		return -1;
	int offset = LittleEndian_getDW(map_data, 24);
	return LittleEndian_getDW(map_data, offset + 8);
}

static int vmMapLoader_GetLevel(void)
{
	if (!map_data)
		return -1;
	int offset = LittleEndian_getDW(map_data, 24);
	return LittleEndian_getDW(map_data, offset + 12);
}


static int vmMapLoader_GetID(void)
{
	if (!map_data)
		return -1;
	return LittleEndian_getDW(map_data, 28);
}

HLL_LIBRARY(vmMapLoader,
	    HLL_EXPORT(Init, vmMapLoader_Init),
	    HLL_EXPORT(Exist, vmMapLoader_Exist),
	    HLL_EXPORT(New, vmMapLoader_New),
	    HLL_EXPORT(Release, vmMapLoader_Release),
	    HLL_EXPORT(GetWidth, vmMapLoader_GetWidth),
	    HLL_EXPORT(GetHeight, vmMapLoader_GetHeight),
	    HLL_EXPORT(GetBaseCG, vmMapLoader_GetBaseCG),
	    HLL_EXPORT(GetNumofBaseCG, vmMapLoader_GetNumofBaseCG),
	    HLL_EXPORT(IsMove, vmMapLoader_IsMove),
	    HLL_EXPORT(GetFigure, vmMapLoader_GetFigure),
	    HLL_EXPORT(GetItemNum, vmMapLoader_GetItemNum),
	    HLL_EXPORT(GetGoldNum, vmMapLoader_GetGoldNum),
	    HLL_EXPORT(GetTurnNum, vmMapLoader_GetTurnNum),
	    HLL_EXPORT(GetLevel, vmMapLoader_GetLevel),
	    HLL_EXPORT(GetID, vmMapLoader_GetID)
	    );
