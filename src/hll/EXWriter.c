/* EXWriter —— 兰斯10 启动用的最小内存实现。
 *
 * 一个基于句柄的键值写入器。SetInt/SetString/AddTable* 都写进
 * 内存里的 map；Save/SaveToString 序列化出一份扁平清单。
 * 暂不追求和原版格式完全兼容。
 *
 * [D497] 全族按 AIN 声明顺序重排（arity 审计揪出）：声明一律是
 * **句柄在前**（h, key, val, …），旧实现写成 (key, val, …, h) ——
 * 个数对但顺序错，libffi 按声明传参时整族都在错位读写。
 * AddArrayInt 13 次/轮没崩纯属侥幸（h==0 时 key=NULL 走了空串守卫，
 * 实际写的是垃圾键）。AddTableFormat* 按声明第三个参是 string、
 * 尾参 bool —— 语义解释为「声明表的列格式」（本实现不建模列，
 * 记成 schema 条目），AddTable* 本体才写值。
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

static void EXWriter_Load(int h, struct string *path)
{
	WARNING("EXWriter.Load: not implemented (%s)", path ? path->text : "");
	(void)h;
}

static void EXWriter_Save(int h, struct string *path)
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

static void EXWriter_LoadFromString(int h, struct string *s)
{
	WARNING("EXWriter.LoadFromString: not implemented");
	(void)s;
	(void)h;
}

static void EXWriter_SaveToString(int h, struct string **out)
{
	struct exw_handle *p = h_get(h);
	if (!out)
		return;
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

static void EXWriter_EraseDefine(int h, struct string *key)
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

static void EXWriter_Rename(int h, struct string *from, struct string *to)
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

/* [D497] SetPartial(h, key, bool)：第三参按声明是 bool，语义无运行期
 * 证据（0 命中）——按最小实现记成 key 的 partial 标记（"1"/"0"）。 */
static void EXWriter_SetPartial(int h, struct string *key, int flag)
{
	h_put(h_get(h), key ? key->text : "", flag ? "1" : "0");
}

static void EXWriter_SetInt(int h, struct string *key, int val)
{
	char b[32];
	snprintf(b, sizeof(b), "%d", val);
	h_put(h_get(h), key ? key->text : "", b);
}

static void EXWriter_SetFloat(int h, struct string *key, float val)
{
	char b[64];
	snprintf(b, sizeof(b), "%g", val);
	h_put(h_get(h), key ? key->text : "", b);
}

static void EXWriter_SetString(int h, struct string *key, struct string *val)
{
	h_put(h_get(h), key ? key->text : "", val ? val->text : "");
}

static void EXWriter_AddArrayInt(int h, struct string *key, int val)
{
	char k[256];
	snprintf(k, sizeof(k), "%s[]", key ? key->text : "");
	char b[32];
	snprintf(b, sizeof(b), "%d", val);
	h_put(h_get(h), k, b);
}

static void EXWriter_AddArrayFloat(int h, struct string *key, float val)
{
	char k[256];
	snprintf(k, sizeof(k), "%s[]", key ? key->text : "");
	char b[64];
	snprintf(b, sizeof(b), "%g", val);
	h_put(h_get(h), k, b);
}

static void EXWriter_AddArrayString(int h, struct string *key, struct string *val)
{
	char k[256];
	snprintf(k, sizeof(k), "%s[]", key ? key->text : "");
	h_put(h_get(h), k, val ? val->text : "");
}

/* [D497] AddTableFormat*(h, key, 列名, [bool])：按声明第三参是 string
 * ——解释为「声明 key 表的一列」（Int/Float/String = 列类型；FormatInt/
 * FormatString 尾参 bool 语义无证据，记进 schema 条目）。本实现不建模
 * 列，记成 `<key>#<列名>:<类型>:<flag>` 的 schema 行。 */
static void exw_add_column(int h, struct string *key, struct string *col,
		const char *type, int flag)
{
	char k[512];
	snprintf(k, sizeof(k), "%s#%s:%s:%d",
		 key ? key->text : "", col ? col->text : "", type, flag);
	h_put(h_get(h), k, "");
}

static void EXWriter_AddTableFormatInt(int h, struct string *key,
		struct string *col, int flag)
{
	exw_add_column(h, key, col, "int", flag);
}

static void EXWriter_AddTableFormatFloat(int h, struct string *key,
		struct string *col)
{
	exw_add_column(h, key, col, "float", 0);
}

static void EXWriter_AddTableFormatString(int h, struct string *key,
		struct string *col, int flag)
{
	exw_add_column(h, key, col, "string", flag);
}

/* AddTable*(h, key, val)：往 key 表追加一行值（列按 AddTableFormat*
 * 声明的顺序对齐——本实现扁平存键值，重复 key 自然形成多行）。 */
static void EXWriter_AddTableInt(int h, struct string *key, int val)
{
	char b[32];
	snprintf(b, sizeof(b), "%d", val);
	h_put(h_get(h), key ? key->text : "", b);
}

static void EXWriter_AddTableFloat(int h, struct string *key, float val)
{
	char b[64];
	snprintf(b, sizeof(b), "%g", val);
	h_put(h_get(h), key ? key->text : "", b);
}

static void EXWriter_AddTableString(int h, struct string *key, struct string *val)
{
	h_put(h_get(h), key ? key->text : "", val ? val->text : "");
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
