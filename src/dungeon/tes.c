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

#include <stdlib.h>
#include <string.h>
#include "system4.h"
#include "system4/buffer.h"

#include "dungeon/tes.h"

struct tes *tes_parse(uint8_t *data, size_t size)
{
	struct buffer r;
	buffer_init(&r, data, size);
	if (memcmp(buffer_strdata(&r), "TES\0\0\0\0\0", 8))
		return NULL;
	buffer_skip(&r, 8);

	struct tes *tes = xcalloc(1, sizeof(struct tes));
	tes->nr_columns = buffer_read_int32(&r);
	tes->nr_rows = buffer_read_int32(&r);
	int nr_items = tes->nr_rows * tes->nr_columns;
	tes->entries = xcalloc(nr_items, sizeof(int));
	for (int i = 0; i < nr_items; i++)
		tes->entries[i] = buffer_read_int32(&r);
	return tes;
}

void tes_free(struct tes *tes)
{
	free(tes->entries);
	free(tes);
}
