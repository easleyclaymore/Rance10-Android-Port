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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "system4.h"
#include "system4/file.h"
#include "system4/little_endian.h"
#include "system4/string.h"

#include "hll.h"
#include "xsystem4.h"

struct inflate_stream {
	z_stream z;
	void *data;
};

static FILE *current_file = NULL;
static struct inflate_stream *current_stream = NULL;

static struct inflate_stream *inflate_stream_new(FILE *fp, unsigned len)
{
	void *buf = xmalloc(len);
	if (fread(buf, len, 1, fp) != 1) {
		free(buf);
		return NULL;
	}

	struct inflate_stream *strm = calloc(1, sizeof(struct inflate_stream));
	strm->data = buf;
	strm->z.next_in = buf;
	strm->z.avail_in = len;
	strm->z.zalloc = Z_NULL;
	strm->z.zfree = Z_NULL;
	int r = inflateInit(&strm->z);
	if (r != Z_OK) {
		WARNING("inflateInit error: %d", r);
		free(buf);
		free(strm);
		return NULL;
	}
	return strm;
}

static void inflate_stream_free(struct inflate_stream *strm)
{
	inflateEnd(&strm->z);
	free(strm->data);
	free(strm);
}

static bool inflate_stream_read(void *dst, size_t size, struct inflate_stream *strm)
{
	strm->z.next_out = dst;
	strm->z.avail_out = size;
	int r = inflate(&strm->z, Z_SYNC_FLUSH);
	return strm->z.avail_out == 0 && (r == Z_OK || r == Z_STREAM_END);
}

/*
 * Rea 压缩包的结构：
 *
 * struct rea_file {
 *     char     signature[4];      // "R8HF"
 *     uint32_t unknown[5];
 *     char     mdlf_signature[4]; // "MDLF"
 *     uint32_t mdlf_length;
 *     uint8_t  mdlf[mdlf_length];
 *     char     mdcd_signature[4]; // "MDCD"
 *     uint32_t mdcd_length;
 *     uint8_t  mdcd[mdcd_length];
 * };
 *
 * `mdlf` 是一个 rea_entry 结构数组，按 zlib 格式压缩。
 *
 * struct rea_entry {
 *     uint32_t name_len;
 *     char     name[name_len];
 *     uint32_t offset;
 *     uint32_t length;
 * };
 *
 * 条目的内容按 zlib 压缩，存放在
 * `mdcd` 数组里 (offset, length) 指定的位置。
 */

#define REA_ENTRY_NAME_MAX 255

struct rea_entry {
	char name[REA_ENTRY_NAME_MAX + 1];
	uint32_t offset;
	uint32_t length;
};

struct rea {
	FILE *fp;
	struct inflate_stream *index_stream;
	long mdcd_offset;
	uint32_t mdcd_size;
};

static void rea_close(struct rea *rea)
{
	if (rea->fp)
		fclose(rea->fp);
	if (rea->index_stream)
		inflate_stream_free(rea->index_stream);
	free(rea);
}

static struct rea *rea_open(const char *path)
{
	struct rea *rea = xcalloc(1, sizeof(struct rea));
	rea->fp = file_open_utf8(path, "rb");
	if (!rea->fp) {
		WARNING("Failed to open file '%s': %s", display_utf0(path), strerror(errno));
		return NULL;
	}

	char header[32];
	if (fread(header, sizeof(header), 1, rea->fp) != 1) {
		WARNING("%s: fread failed: %s", display_utf0(path), strerror(errno));
		goto err;
	}
	if (strncmp(header, "R8HF", 4) != 0) {
		WARNING("%s: file signature mismatch", display_utf0(path));
		goto err;
	}
	if (strncmp(header + 24, "MDLF", 4) != 0) {
		WARNING("%s: cannot find MDLF section", display_utf0(path));
		goto err;
	}

	uint32_t mdlf_size = LittleEndian_getDW((uint8_t*)header, 28);
	rea->index_stream = inflate_stream_new(rea->fp, mdlf_size);
	if (!rea->index_stream)
		goto err;

	char mdcd_header[8];
	if (fread(mdcd_header, sizeof(mdcd_header), 1, rea->fp) != 1) {
		WARNING("%s: fread failed: %s", display_utf0(path), strerror(errno));
		goto err;
	}

	if (strncmp(mdcd_header, "MDCD", 4) != 0) {
		WARNING("%s: cannot find MDCD section", display_utf0(path));
		goto err;
	}
	rea->mdcd_size = LittleEndian_getDW((uint8_t*)mdcd_header, 4);
	rea->mdcd_offset = ftell(rea->fp);
	return rea;

 err:
	rea_close(rea);
	return NULL;
}

static bool rea_read_entry(struct rea_entry *entry, struct rea *rea)
{
	uint8_t buf[8];
	if (!inflate_stream_read(buf, 4, rea->index_stream))
		return false;
	int name_len = LittleEndian_getDW(buf, 0);
	if (name_len > REA_ENTRY_NAME_MAX)
		return false;
	if (!inflate_stream_read(entry->name, name_len, rea->index_stream))
		return false;
	entry->name[name_len] = '\0';
	if (!inflate_stream_read(buf, 8, rea->index_stream))
		return false;
	entry->offset = LittleEndian_getDW(buf, 0);
	entry->length = LittleEndian_getDW(buf, 0);
	return true;
}

static struct inflate_stream *rea_open_entry(struct rea_entry *entry, struct rea *rea)
{
	if (fseek(rea->fp, rea->mdcd_offset + entry->offset, SEEK_SET))
		return NULL;
	return inflate_stream_new(rea->fp, entry->length);
}

static bool file2_open(struct string *filename, const char *mode)
{
	if (current_file) {
		WARNING("Previously opened file wasn't closed");
		fclose(current_file);
	}
	if (current_stream) {
		WARNING("Previously opened archive wasn't closed");
		inflate_stream_free(current_stream);
		current_stream = NULL;
	}

	char *path = unix_path(filename->text);
	current_file = file_open_utf8(path, mode);
	if (!current_file) {
		WARNING("Failed to open file '%s': %s", display_utf0(path), strerror(errno));
	}

	free(path);
	return !!current_file;
}

static bool file2_write(const void *ptr, size_t size)
{
	if (!current_file) {
		WARNING("File2.Write* called, but no open file");
		return false;
	}
	if (fwrite(ptr, size, 1, current_file) != 1) {
		WARNING("fwrite failed: %s", strerror(errno));
		return false;
	}
	return true;
}

static bool file2_read(void *dst, size_t size)
{
	if (current_file) {
		if (fread(dst, size, 1, current_file) != 1) {
			WARNING("fread failed: %s", strerror(errno));
			return false;
		}
		return true;
	} else if (current_stream) {
		return inflate_stream_read(dst, size, current_stream);
	} else {
		WARNING("File2.Read* called, but no open file");
		return false;
	}
}

static bool File2_OpenForWrite(struct string *filename)
{
	return file2_open(filename, "wb");
}

static bool File2_OpenForRead(struct string *filename)
{
	return file2_open(filename, "rb");
}

static bool File2_OpenArchiveForRead(struct string *archive_name, struct string *filename)
{
	if (current_file) {
		WARNING("Previously opened file wasn't closed");
		fclose(current_file);
		current_file = NULL;
	}
	if (current_stream) {
		WARNING("Previously opened archive wasn't closed");
		inflate_stream_free(current_stream);
		current_stream = NULL;
	}

	char *fname = xmalloc(strlen(archive_name->text) + 5);
	strcpy(fname, archive_name->text);
	strcat(fname, ".rea");
	char *archive_path = gamedir_path(fname);
	free(fname);

	struct rea *rea = rea_open(archive_path);
	free(archive_path);
	if (!rea)
		return false;

	struct rea_entry entry;
	while (rea_read_entry(&entry, rea)) {
		if (!strcmp(entry.name, filename->text)) {
			current_stream = rea_open_entry(&entry, rea);
			break;
		}
	}
	rea_close(rea);
	return !!current_stream;
}

static bool File2_Close(void)
{
	if (current_file) {
		int r = !fclose(current_file);
		current_file = NULL;
		return r;
	} else if (current_stream) {
		inflate_stream_free(current_stream);
		current_stream = NULL;
		return true;
	} else {
		VM_ERROR("File2.Close called, but no open file");
	}
}

static bool File2_WriteByte(int data)
{
	uint8_t b = data;
	return file2_write(&b, 1);
}

static bool File2_WriteInt(int data)
{
	uint8_t b[4];
	LittleEndian_putDW(b, 0, data);
	return file2_write(b, 4);
}

static bool File2_WriteFloat(float data)
{
	uint8_t b[4];
	union {
		uint32_t i;
		float f;
	} cast;
	cast.f = data;
	LittleEndian_putDW(b, 0, cast.i);
	return file2_write(b, 4);
}

static bool File2_WriteString(struct string *str)
{
	return file2_write(str->text, str->size+1);
}

static bool File2_ReadByte(int *data)
{
	uint8_t b;
	if (!file2_read(&b, 1))
		return false;
	*data = b;
	return true;
}

static bool File2_ReadInt(int *data)
{
	uint8_t b[4];
	if (!file2_read(b, 4))
		return false;
	*data = LittleEndian_getDW(b, 0);
	return true;
}

static bool File2_ReadFloat(float *data)
{
	uint8_t b[4];
	union {
		uint32_t i;
		float f;
	} cast;
	if (!file2_read(b, 4))
		return false;
	cast.i = LittleEndian_getDW(b, 0);
	*data = cast.f;
	return true;
}

static bool File2_ReadString(struct string **str)
{
	char b;
	struct string *s = make_string("", 0);
	while (true) {
		if (!file2_read((uint8_t*)&b, 1)) {
			free_string(s);
			return false;
		}
		if (!b)
			break;
		string_append_cstr(&s, &b, 1);
	}

	if (*str)
		free_string(*str);
	*str = s;
	return true;
}

HLL_LIBRARY(File2,
	    HLL_EXPORT(OpenForWrite, File2_OpenForWrite),
	    HLL_EXPORT(OpenForRead, File2_OpenForRead),
	    HLL_EXPORT(OpenArchiveForRead, File2_OpenArchiveForRead),
	    HLL_EXPORT(Close, File2_Close),
	    HLL_EXPORT(WriteByte, File2_WriteByte),
	    HLL_EXPORT(WriteInt, File2_WriteInt),
	    HLL_EXPORT(WriteFloat, File2_WriteFloat),
	    HLL_EXPORT(WriteString, File2_WriteString),
	    HLL_EXPORT(ReadByte, File2_ReadByte),
	    HLL_EXPORT(ReadInt, File2_ReadInt),
	    HLL_EXPORT(ReadFloat, File2_ReadFloat),
	    HLL_EXPORT(ReadString, File2_ReadString)
	    );
