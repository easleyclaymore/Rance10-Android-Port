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

#ifndef SYSTEM4_DTX_H
#define SYSTEM4_DTX_H

#include <stdint.h>
#include <stddef.h>

struct cg;

enum dtx_texture_type {
	// 单元格纹理
	DTX_WALL,
	DTX_FLOOR,
	DTX_CEILING,
	DTX_STAIRS,
	DTX_DOOR,
	DTX_LIGHTMAP,
	// 天空盒纹理
	DTX_SKYBOX,
};

#define DTX_NR_CELL_TEXTURE_TYPES (DTX_LIGHTMAP + 1)

struct dtx_entry {
	uint32_t size;
	uint8_t *data;
};

struct dtx {
	uint32_t version;  // 0: Rance IV, 1: GALZOO Island
	int nr_rows;
	int nr_columns;
	struct dtx_entry *entries;
};

struct dtx *dtx_parse(uint8_t *data, size_t size);
void dtx_free(struct dtx *dtx);
struct cg *dtx_create_cg(struct dtx *dtx, enum dtx_texture_type type, int index);
struct dtx *dtx_load_from_dtl(const char *path, uint32_t seed);

#endif /* SYSTEM4_DTX_H */
