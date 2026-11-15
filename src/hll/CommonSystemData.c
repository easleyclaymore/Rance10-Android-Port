/* 版权所有 (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <stdint.h>

#include "system4.h"
#include "system4/hashtable.h"
#include "system4/string.h"

#include "hll.h"

static struct string *save_path = NULL;

union csd_datum {
	void *p;
	intptr_t i;
	float f;
	struct string *s;
};

static struct hash_table *csd_table(void)
{
	static struct hash_table *table = NULL;
	if (!table) {
		table = ht_create(64);
	}
	return table;
}

static void csd_put_datum(struct string *name, union csd_datum datum)
{
	struct ht_slot *slot = ht_put(csd_table(), name->text, NULL);
	slot->value = datum.p;
}

static bool csd_get_datum(struct string *name, union csd_datum *out)
{
	return _ht_get(csd_table(), name->text, &out->p);
}

static void CommonSystemData_SetFullPathSaveFileName(struct string *filename)
{
	if (save_path)
		free_string(save_path);
	save_path = string_dup(filename);
	NOTICE("SetFullPathSaveFileName(%s)", filename->text);
}

/* AIN f[0]：bool LoadAtStartup()。_system::detail::Init 里是
 * CALLHLL 9 0; NOT; IFNZ —— 返回 false 就会走「装载失败」分支。
 * 这个移植没有持久化的系统数据，所以报成功（D64）。 */
static bool CommonSystemData_LoadAtStartup(void)
{
	return true;
}
//static bool CommonSystemData_LoadAtStartup(void);

static bool CommonSystemData_SetDataInt(struct string *name, int value)
{
	csd_put_datum(name, (union csd_datum) { .i = value });
	return true;
}

static bool CommonSystemData_SetDataFloat(struct string *name, float value)
{
	csd_put_datum(name, (union csd_datum) { .f = value });
	return true;
}

static bool CommonSystemData_SetDataString(struct string *name, struct string *value)
{
	union csd_datum datum = { .s = string_dup(value) };
	struct ht_slot *slot = ht_put(csd_table(), name->text, NULL);
	if (slot->value)
		free_string(slot->value);
	slot->value = datum.p;
	return true;
}

static bool CommonSystemData_SetDataBool(struct string *name, bool value)
{
	csd_put_datum(name, (union csd_datum) { .i = value });
	return true;
}

static bool CommonSystemData_GetDataInt(struct string *name, int *value)
{
	union csd_datum datum;
	if (csd_get_datum(name, &datum)) {
		*value = datum.i;
		return true;
	}
	return false;
}

static bool CommonSystemData_GetDataFloat(struct string *name, float *value)
{
	union csd_datum datum;
	if (csd_get_datum(name, &datum)) {
		*value = datum.f;
		return true;
	}
	return false;
}

static bool CommonSystemData_GetDataString(struct string *name, struct string **value)
{
	union csd_datum datum;
	if (csd_get_datum(name, &datum)) {
		if (*value)
			free_string(*value);
		*value = string_dup(datum.s);
		return true;
	}
	return false;
}

static bool CommonSystemData_GetDataBool(struct string *name, bool *value)
{
	union csd_datum datum;
	if (csd_get_datum(name, &datum)) {
		*value = datum.i;
		return true;
	}
	return false;
}

HLL_LIBRARY(CommonSystemData,
	    HLL_EXPORT(SetFullPathSaveFileName, CommonSystemData_SetFullPathSaveFileName),
	    HLL_EXPORT(LoadAtStartup, CommonSystemData_LoadAtStartup),
	    HLL_EXPORT(SetDataInt, CommonSystemData_SetDataInt),
	    HLL_EXPORT(SetDataFloat, CommonSystemData_SetDataFloat),
	    HLL_EXPORT(SetDataString, CommonSystemData_SetDataString),
	    HLL_EXPORT(SetDataBool, CommonSystemData_SetDataBool),
	    HLL_EXPORT(GetDataInt, CommonSystemData_GetDataInt),
	    HLL_EXPORT(GetDataFloat, CommonSystemData_GetDataFloat),
	    HLL_EXPORT(GetDataString, CommonSystemData_GetDataString),
	    HLL_EXPORT(GetDataBool, CommonSystemData_GetDataBool));
