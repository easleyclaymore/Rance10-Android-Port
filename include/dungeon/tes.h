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

#ifndef SYSTEM4_TES_H
#define SYSTEM4_TES_H

#include <stdint.h>
#include <stddef.h>

struct tes {
	int nr_columns;
	int nr_rows;
	int *entries;
};

struct tes *tes_parse(uint8_t *data, size_t size);
void tes_free(struct tes *tes);

static inline int tes_get(struct tes *tes, int type, int index)
{
	if (type < 0 || type >= tes->nr_rows || index < 0 || index >= tes->nr_columns)
		return -1;
	return tes->entries[type * tes->nr_columns + index];
}

#endif /* SYSTEM4_TES_H */
