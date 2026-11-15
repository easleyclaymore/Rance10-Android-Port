/* 版权所有 (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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
#include "system4/archive.h"
#include "system4/buffer.h"
#include "system4/little_endian.h"
#include "system4/string.h"
#include "system4/zlib.h"

#include "hll.h"
#include "asset_manager.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

static struct buffer reader;

static int Data_Exist(int num)
{
	return asset_exists(ASSET_DATA, num);
}

static int Data_Open(int num)
{
	struct archive_data *dfile = asset_get(ASSET_DATA, num);
	if (!dfile)
		return 0;
	size_t uncompressed_size = LittleEndian_getDW(dfile->data, 0);
	uint8_t *buf = xmalloc(uncompressed_size);
	if (!zlib_decompress_exact(buf, uncompressed_size,
			(uint8_t *)dfile->data + 4, dfile->size - 4)) {
		WARNING("%s: uncompress failed", dfile->name);
		archive_free_data(dfile);
		free(buf);
		return 0;
	}
	archive_free_data(dfile);
	if (reader.buf)
		free(reader.buf);
	buffer_init(&reader, buf, uncompressed_size);
	return 1;
}

static int Data_Close(void)
{
	if (!reader.buf)
		return 0;
	free(reader.buf);
	reader.buf = NULL;
	return 1;
}

static void read_struct(struct page *page)
{
	if (page->type != STRUCT_PAGE) {
		VM_ERROR("Data.Read of non-struct");
	}

	for (int i = 0; i < page->nr_vars; i++) {
		int struct_type;
		enum ain_data_type type = variable_type(page, i, &struct_type, NULL);
		union vm_value val;
		switch (type) {
		case AIN_INT:
			val = vm_int(buffer_read_int32(&reader));
			break;
		case AIN_FLOAT:
			val = vm_float(buffer_read_float(&reader));
			break;
		case AIN_STRING:
			{
				struct string *s = cstr_to_string(buffer_strdata(&reader));
				buffer_skip(&reader, s->size + 1);
				val = vm_int(heap_alloc_string(s));
			}
			break;
		case AIN_STRUCT:
			{
				int slot = alloc_struct(struct_type);
				read_struct(heap[slot].page);
				val = vm_int(slot);
			}
			break;
		default:
			VM_ERROR("Data.Read: unsupported data type");
		}
		variable_set(page, i, type, val);
	}
}

static int Data_Read(struct page **page)
{
	if (!reader.buf || buffer_remaining(&reader) == 0) {
		return 0;
	}
	read_struct(*page);
	return 1;
}

HLL_LIBRARY(Data,
	    HLL_EXPORT(Exist, Data_Exist),
	    HLL_EXPORT(Open, Data_Open),
	    HLL_EXPORT(Close, Data_Close),
	    HLL_EXPORT(Read, Data_Read)
	    );
