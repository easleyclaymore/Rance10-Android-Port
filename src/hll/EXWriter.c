/* EXWriter - minimal in-memory implementation for Rance X bring-up.
 *
 * A handle-based key/value writer. SetInt/SetString/AddTable* store into an
 * in-memory map; Save/SaveToString serialize a flat listing. Real format
 * compatibility with the original is not attempted yet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/string.h"

#include "hll.h"
#include "vm.h"

#define MAX_HANDLES 32

struct kv {
	char *key;
	char *val;
};

struct exw_handle {
	int used;
	struct kv *items;
	int nr;
	int cap;
};

static struct exw_handle handles[MAX_HANDLES];

static struct exw_handle *h_get(int h)
{
	if (h < 0 || h >= MAX_HANDLES || !handles[h].used)
		return NULL;
	return &handles[h];
}

static void h_put(struct exw_handle *p, const char *key, const char *val)
{
	if (!p || !key)
		return;
	if (p->nr >= p->cap) {
		p->cap = p->cap ? p->cap * 2 : 16;
		p->items = realloc(p->items, p->cap * sizeof(struct kv));
	}
	p->items[p->nr].key = strdup(key);
	p->items[p->nr].val = strdup(val ? val : "");
	p->nr++;
}

static int EXWriter_CreateHandle(void)
{
	for (int i = 0; i < MAX_HANDLES; i++) {
		if (!handles[i].used) {
			memset(&handles[i], 0, sizeof(handles[i]));
			handles[i].used = 1;
			return i;
		}
	}
	return -1;
}

static void EXWriter_ReleaseHandle(int h)
{
	struct exw_handle *p = h_get(h);
	if (!p)
		return;
	for (int i = 0; i < p->nr; i++) {
		free(p->items[i].key);
		free(p->items[i].val);
	}
	free(p->items);
	memset(p, 0, sizeof(*p));
}

static void EXWriter_Load(struct string *path, int h)
{
	WARNING("EXWriter.Load: not implemented (%s)", path ? path->text : "");
}

static void EXWriter_Save(struct string *path, int h)
{
	struct exw_handle *p = h_get(h);
	if (!p || !path)
		return;
	FILE *fp = fopen(path->text, "w");
	if (!fp) {
		WARNING("EXWriter.Save: cannot open %s", path->text);
		return;
	}
	for (int i = 0; i < p->nr; i++)
		fprintf(fp, "%s=%s\n", p->items[i].key, p->items[i].val);
	fclose(fp);
}

static void EXWriter_LoadFromString(struct string *s, int h)
{
	WARNING("EXWriter.LoadFromString: not implemented");
}

static void EXWriter_SaveToString(struct string **out, int h)
{
	struct exw_handle *p = h_get(h);
	if (!p) {
		*out = string_dup(&EMPTY_STRING);
		return;
	}
	size_t len = 0;
	for (int i = 0; i < p->nr; i++)
		len += strlen(p->items[i].key) + strlen(p->items[i].val) + 2;
	char *buf = calloc(1, len + 1);
	for (int i = 0; i < p->nr; i++) {
		strcat(buf, p->items[i].key);
		strcat(buf, "=");
		strcat(buf, p->items[i].val);
		strcat(buf, "\n");
	}
	*out = make_string(buf, strlen(buf));
	free(buf);
}

static void EXWriter_EraseDefine(struct string *key, int h)
{
	struct exw_handle *p = h_get(h);
	if (!p || !key)
		return;
	for (int i = 0; i < p->nr; i++) {
		if (!strcmp(p->items[i].key, key->text)) {
			free(p->items[i].key);
			free(p->items[i].val);
			memmove(&p->items[i], &p->items[i + 1], (p->nr - i - 1) * sizeof(struct kv));
			p->nr--;
			return;
		}
	}
}

static void EXWriter_Rename(struct string *from, struct string *to, int h)
{
	struct exw_handle *p = h_get(h);
	if (!p || !from || !to)
		return;
	for (int i = 0; i < p->nr; i++) {
		if (!strcmp(p->items[i].key, from->text)) {
			free(p->items[i].key);
			p->items[i].key = strdup(to->text);
			return;
		}
	}
}

static void EXWriter_SetPartial(struct string *key, struct string *val, int h)
{
	h_put(h_get(h), key ? key->text : "", val ? val->text : "");
}

static void EXWriter_SetInt(struct string *key, int val, int h)
{
	char b[32];
	snprintf(b, sizeof(b), "%d", val);
	h_put(h_get(h), key ? key->text : "", b);
}

static void EXWriter_SetFloat(struct string *key, float val, int h)
{
	char b[64];
	snprintf(b, sizeof(b), "%g", val);
	h_put(h_get(h), key ? key->text : "", b);
}

static void EXWriter_SetString(struct string *key, struct string *val, int h)
{
	h_put(h_get(h), key ? key->text : "", val ? val->text : "");
}

static void EXWriter_AddArrayInt(struct string *key, int val, int h)
{
	char k[256];
	snprintf(k, sizeof(k), "%s[]", key ? key->text : "");
	char b[32];
	snprintf(b, sizeof(b), "%d", val);
	h_put(h_get(h), k, b);
}

static void EXWriter_AddArrayFloat(struct string *key, float val, int h)
{
	char k[256];
	snprintf(k, sizeof(k), "%s[]", key ? key->text : "");
	char b[64];
	snprintf(b, sizeof(b), "%g", val);
	h_put(h_get(h), k, b);
}

static void EXWriter_AddArrayString(struct string *key, struct string *val, int h)
{
	char k[256];
	snprintf(k, sizeof(k), "%s[]", key ? key->text : "");
	h_put(h_get(h), k, val ? val->text : "");
}

static void EXWriter_AddTableFormatInt(struct string *key, int idx, int val, int h)
{
	char k[256];
	snprintf(k, sizeof(k), "%s[%d]", key ? key->text : "", idx);
	char b[32];
	snprintf(b, sizeof(b), "%d", val);
	h_put(h_get(h), k, b);
}

static void EXWriter_AddTableFormatFloat(struct string *key, int idx, float val, int h)
{
	char k[256];
	snprintf(k, sizeof(k), "%s[%d]", key ? key->text : "", idx);
	char b[64];
	snprintf(b, sizeof(b), "%g", val);
	h_put(h_get(h), k, b);
}

static void EXWriter_AddTableFormatString(struct string *key, int idx, struct string *val, int h)
{
	char k[256];
	snprintf(k, sizeof(k), "%s[%d]", key ? key->text : "", idx);
	h_put(h_get(h), k, val ? val->text : "");
}

static void EXWriter_AddTableInt(struct string *key, int idx, int val, int h)
{
	EXWriter_AddTableFormatInt(key, idx, val, h);
}

static void EXWriter_AddTableFloat(struct string *key, int idx, float val, int h)
{
	EXWriter_AddTableFormatFloat(key, idx, val, h);
}

static void EXWriter_AddTableString(struct string *key, int idx, struct string *val, int h)
{
	EXWriter_AddTableFormatString(key, idx, val, h);
}

HLL_LIBRARY(EXWriter,
	HLL_EXPORT(CreateHandle, EXWriter_CreateHandle),
	HLL_EXPORT(ReleaseHandle, EXWriter_ReleaseHandle),
	HLL_EXPORT(Load, EXWriter_Load),
	HLL_EXPORT(Save, EXWriter_Save),
	HLL_EXPORT(LoadFromString, EXWriter_LoadFromString),
	HLL_EXPORT(SaveToString, EXWriter_SaveToString),
	HLL_EXPORT(EraseDefine, EXWriter_EraseDefine),
	HLL_EXPORT(Rename, EXWriter_Rename),
	HLL_EXPORT(SetPartial, EXWriter_SetPartial),
	HLL_EXPORT(SetInt, EXWriter_SetInt),
	HLL_EXPORT(SetFloat, EXWriter_SetFloat),
	HLL_EXPORT(SetString, EXWriter_SetString),
	HLL_EXPORT(AddArrayInt, EXWriter_AddArrayInt),
	HLL_EXPORT(AddArrayFloat, EXWriter_AddArrayFloat),
	HLL_EXPORT(AddArrayString, EXWriter_AddArrayString),
	HLL_EXPORT(AddTableFormatInt, EXWriter_AddTableFormatInt),
	HLL_EXPORT(AddTableFormatFloat, EXWriter_AddTableFormatFloat),
	HLL_EXPORT(AddTableFormatString, EXWriter_AddTableFormatString),
	HLL_EXPORT(AddTableInt, EXWriter_AddTableInt),
	HLL_EXPORT(AddTableFloat, EXWriter_AddTableFloat),
	HLL_EXPORT(AddTableString, EXWriter_AddTableString));
