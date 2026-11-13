/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "cJSON.h"

#include "system4.h"
#include "system4/ain.h"
#include "system4/file.h"
#include "system4/savefile.h"
#include "system4/string.h"

#include "savedata.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "xsystem4.h"

static int current_global;

#define invalid_save_data(msg, data) {					\
		char *str = data ? cJSON_Print(data) : strdup("NULL");	\
		if (!str) str = strdup("PRINTING FAILED");		\
		WARNING("Invalid save data (%d): " msg ": %s", current_global, data); \
		free(str);						\
	}

int save_json(const char *filename, cJSON *json)
{
	char *path = savedir_path(filename);
	FILE *f = file_open_utf8(path, "w");
	if (!f) {
		WARNING("Failed to open save file: %s: %s", display_utf0(filename), strerror(errno));
		free(path);
		return 0;
	}
	free(path);

	char *str = cJSON_Print(json);

	if (fwrite(str, strlen(str), 1, f) != 1) {
		WARNING("Failed to write save file: %s", strerror(errno));
		free(str);
		return 0;
	}
	if (fclose(f)) {
		WARNING("Error writing save to file: %s: %s", display_utf0(filename), strerror(errno));
		free(str);
		return 0;
	}
	free(str);
	return 1;
}

cJSON *load_json(const char *filename)
{
	char *path = savedir_path(filename);
	char *json = file_read(path, NULL);
	free(path);
	if (!json)
		return NULL;
	cJSON *root = cJSON_Parse(json);
	free(json);
	return root;
}

static int get_group_index(const char *name)
{
	for (int i = 0; i < ain->nr_global_groups; i++) {
		if (!strcmp(ain->global_group_names[i], name))
			return i;
	}
	return -1;
}

static int32_t add_value_to_gsave(enum ain_data_type type, union vm_value val, struct gsave *save);

/* [L2] "Where am I in the object graph" for write-path diagnostics: the
 * assertion used to fire with only a slot number, which is untraceable
 * once the value came from a nested array/struct. Callers set this right
 * before descending; it is the innermost owner seen so far. */
static const char *gsave_ctx = "?";
static const char *gsave_root = "?";
static char gsave_ctx_buf[160];
static char gsave_root_buf[160];

static struct gsave_flat_array *collect_flat_arrays(struct page *page, struct gsave_flat_array *fa, struct gsave *save)
{
	if (page->array.rank > 1) {
		for (int i = 0; i < page->nr_vars; i++)
			fa = collect_flat_arrays(heap_get_page(page->values[i].i), fa, save);
		return fa;
	}
	enum ain_data_type type = variable_type(page, 0, NULL, NULL);
	fa->nr_values = page->nr_vars;
	fa->type = type;
	fa->values = xcalloc(fa->nr_values, sizeof(struct gsave_array_value));
	for (int i = 0; i < page->nr_vars; i++) {
		fa->values[i].type = type;
		snprintf(gsave_ctx_buf, sizeof(gsave_ctx_buf), "array<%s>[%d/%d]",
			 ain_strtype(ain, type, -1), i, page->nr_vars);
		gsave_ctx = gsave_ctx_buf;
		fa->values[i].value = add_value_to_gsave(type, page->values[i], save);
	}
	return ++fa;
}

static int32_t add_value_to_gsave(enum ain_data_type type, union vm_value val, struct gsave *save)
{
	switch (type) {
	case AIN_VOID:
	case AIN_INT:
	case AIN_BOOL:
	case AIN_FUNC_TYPE:
	case AIN_DELEGATE:
	case AIN_LONG_INT:
	case AIN_FLOAT:
		return val.i;
	case AIN_STRING:
		/* [L2] -1 is the "unset" value in the gsave value space (it is also
		 * what variable_initval() puts in a fresh reference slot). Writing
		 * it as an empty string keeps write and read symmetric: the reader
		 * maps both back to an empty string. */
		if (val.i < 0)
			return GSAVE7_EMPTY_STRING;
		return gsave_add_string(save, heap[val.i].s);
	case AIN_STRUCT:
		{
			/* [L2] A never-initialised struct (array element, optional
			 * member) is -1; serialise it as "unset" instead of asserting.
			 * The reader turns any out-of-range record index back into -1
			 * (see gsave_index_ok), so the round trip is lossless. */
			if (val.i < 0)
				return -1;
			struct page *page = heap_get_page(val.i);
			/* [L2] A struct-typed variable whose slot is not a STRUCT_PAGE
			 * means the value was never a struct to begin with (or points
			 * at a released slot). Say WHICH slot and type instead of
			 * dying on a bare assert. */
			if (!page || page->type != STRUCT_PAGE) {
				/* [L2] A struct-typed slot that does not hold a struct page
				 * cannot be represented in the save format; record it as
				 * "unset" (the reader maps -1 back to a null struct) instead
				 * of aborting the whole save. Bounded warning, not silent --
				 * the underlying cause is tracked as [ARR-UNSET]. */
				static int nr_bad;
				if (nr_bad++ < 5)
					WARNING("save: struct slot %d holds heap type %d, not a"
						" STRUCT_PAGE; writing as unset (at %s, root %s)",
						val.i,
						(size_t) val.i < heap_size && heap_index_valid(val.i)
						? heap[val.i].type : -1, gsave_ctx, gsave_root);
				return -1;
			}
			struct ain_struct *st = &ain->structures[page->index];
			if (st->nr_members != page->nr_vars)
				VM_ERROR("save: struct %s has %d members but page has %d slots"
					 " (in %s)", display_sjis0(st->name), st->nr_members,
					 page->nr_vars, gsave_ctx);
			struct gsave_record rec = {
				.type = GSAVE_RECORD_STRUCT,
				.struct_name = strdup(st->name),
				.nr_indices = st->nr_members,
				.indices = xcalloc(st->nr_members, sizeof(int32_t)),
			};
			if (save->version >= 7) {
				rec.struct_index = gsave_get_struct_def(save, st->name);
				if (rec.struct_index < 0)
					rec.struct_index = gsave_add_struct_def(save, st);
			}
			for (int i = 0; i < page->nr_vars; i++) {
				enum ain_data_type type = st->members[i].type.data;
				snprintf(gsave_ctx_buf, sizeof(gsave_ctx_buf), "%s.%s",
					 display_sjis0(st->name), display_sjis1(st->members[i].name));
				gsave_ctx = gsave_ctx_buf;
				int32_t value = add_value_to_gsave(type, page->values[i], save);
				struct gsave_keyval kv = {
					.name = strdup(st->members[i].name),
					.type = type,
					.value = value,
				};
				rec.indices[i] = gsave_add_keyval(save, &kv);
			}
			return gsave_add_record(save, &rec);
		}
	/* [L2] AIN_ARRAY_TYPE is a macro over 8 concrete array types; v12's
	 * generic AIN_ARRAY(79) is NOT included (see add_value_to_gsave). */
	case AIN_ARRAY:
	case AIN_ARRAY_TYPE:
		{
			struct page *page = heap_get_page(val.i);
			if (!page) {
				struct gsave_array array = { .rank = -1 };
				return gsave_add_array(save, &array);
			}
			assert(page->type == ARRAY_PAGE);
			/* [L2] A generic array keeps a_type == AIN_ARRAY until the first
			 * store settles the element kind; array_type() then reports VOID.
			 * Writing its elements as VOID produces a file the reader can
			 * never accept (it allocates the array from the saved element
			 * kind and then compares it against the type it got). "No kind"
			 * means "cannot represent" -> record it as unset. */
			if (array_type(page->a_type) == AIN_VOID) {
				static int nr_untyped;
				if (nr_untyped++ < 3)
					WARNING("save: array with unsettled element kind"
						" (a_type=%d, %d elements) written as unset"
						" at %s (root %s)", page->a_type, page->nr_vars,
						gsave_ctx, gsave_root);
				struct gsave_array array = { .rank = -1 };
				return gsave_add_array(save, &array);
			}
			struct gsave_array array = {
				.rank = page->array.rank,
				.dimensions = xcalloc(page->array.rank, sizeof(int32_t)),
				.nr_flat_arrays = 1
			};
			for (struct page *p = page;; p = heap_get_page(page->values[0].i)) {
				array.dimensions[p->array.rank - 1] = p->nr_vars;
				if (p->array.rank == 1)
					break;
				array.nr_flat_arrays *= p->nr_vars;
			}
			array.flat_arrays = xcalloc(array.nr_flat_arrays, sizeof(struct gsave_flat_array));
			collect_flat_arrays(page, array.flat_arrays, save);
			return gsave_add_array(save, &array);
		}
	case AIN_REF_TYPE:
		return -1;
	default:
		WARNING("Unhandled type: %s", ain_strtype(ain, type, -1));
		return -1;
	}
}

static int get_gsave_version(void)
{
	if (AIN_VERSION_GTE(ain, 6, 0)) {
		return config.vm_name ? 5 : 7;
	}
	return AIN_VERSION_GTE(ain, 5, 0) ? 5 : 4;
}

int save_globals(const char *keyname, const char *filename, const char *group_name, int *n_out)
{
	int group = -1;
	int nr_vars = 0;
	if (group_name) {
		if ((group = get_group_index(group_name)) < 0) {
			WARNING("Unregistered global group: %s", display_sjis0(group_name));
		} else {
			for (int i = 0; i < ain->nr_globals; i++) {
				if (ain->globals[i].group_index == group)
					nr_vars++;
			}
		}
	} else {
		nr_vars = ain->nr_globals;
	}

	struct gsave *save = gsave_create(get_gsave_version(), keyname, ain->nr_globals, group_name);
	gsave_add_globals_record(save, nr_vars);

	if (nr_vars > 0) {
		struct gsave_global *global = save->globals;
		for (int i = 0; i < ain->nr_globals; i++) {
			if (group >= 0 && ain->globals[i].group_index != group)
				continue;
			global->name = strdup(ain->globals[i].name);
			global->type = ain->globals[i].type.data;
			snprintf(gsave_ctx_buf, sizeof(gsave_ctx_buf), "global %s",
				 display_sjis0(ain->globals[i].name));
			gsave_ctx = gsave_ctx_buf;
			snprintf(gsave_root_buf, sizeof(gsave_root_buf), "%s",
				 ain->globals[i].name ? ain->globals[i].name : "?");
			gsave_root = gsave_root_buf;
			global->value = add_value_to_gsave(global->type, global_get(i), save);
			global++;
		}
	}

	char *path = savedir_path(filename);
	FILE *fp = file_open_utf8(path, "wb");
	if (!fp) {
		WARNING("Failed to open save file %s: %s", display_utf0(path), strerror(errno));
		free(path);
		gsave_free(save);
		return 0;
	}
	free(path);

	bool encrypt = !AIN_VERSION_GTE(ain, 6, 0);
	int compression_level = AIN_VERSION_GTE(ain, 6, 0) ? 1 : 9;
	enum savefile_error error = gsave_write(save, fp, encrypt, compression_level);
	if (error != SAVEFILE_SUCCESS)
		WARNING("Failed to write save file: %s", savefile_strerror(error));
	fclose(fp);
	gsave_free(save);
	if (n_out)
		*n_out = nr_vars;
	return error == SAVEFILE_SUCCESS;
}

static union vm_value json_to_vm_value(enum ain_data_type type, enum ain_data_type struct_type, int array_rank, cJSON *json);

void get_array_dims(cJSON *json, int rank, union vm_value *dims)
{
	cJSON *array = json;
	for (int i = 0; i < rank; i++) {
		dims[i].i = cJSON_GetArraySize(array);
		array = cJSON_GetArrayItem(array, 0);
	}
}

void json_load_page(struct page *page, cJSON *vars, bool call_dtors)
{
	int i = 0;
	cJSON *v;
	cJSON_ArrayForEach(v, vars) {
		int struct_type, array_rank;
		enum ain_data_type data_type = variable_type(page, i, &struct_type, &array_rank);
		union vm_value val = json_to_vm_value(data_type, struct_type, array_rank, v);
		if (call_dtors)
			variable_set(page, i, data_type, val);
		else
			page->values[i] = val;
		i++;
	}
}

static union vm_value json_to_vm_value(enum ain_data_type type, enum ain_data_type struct_type, int array_rank, cJSON *json)
{
	char *str;
	int slot;
	union vm_value *dims;
	struct page *page;
	switch (type) {
	case AIN_INT:
	case AIN_BOOL:
	case AIN_LONG_INT:
		if (!cJSON_IsNumber(json)) {
			invalid_save_data("Not a number", json);
			return vm_int(0);
		}
		return vm_int(json->valueint);
	case AIN_FLOAT:
		if (!cJSON_IsNumber(json)) {
			invalid_save_data("Not a number", json);
			return vm_float(0);
		}
		return vm_float(json->valuedouble);
	case AIN_STRING:
		slot = heap_alloc_slot(VM_STRING);
		if (!cJSON_IsString(json)) {
			invalid_save_data("Not a string", json);
			heap[slot].s = string_ref(&EMPTY_STRING);
			return vm_int(slot);
		}
		str = cJSON_GetStringValue(json);
		if (!str[0]) {
			heap[slot].s = string_ref(&EMPTY_STRING);
		} else {
			heap[slot].s = make_string(str, strlen(str));
		}
		return vm_int(slot);
	case AIN_STRUCT:
		slot = alloc_struct(struct_type);
		if (!cJSON_IsArray(json) || cJSON_GetArraySize(json) != ain->structures[struct_type].nr_members) {
			invalid_save_data("Not an array", json);
		} else {
			json_load_page(heap[slot].page, json, false);
		}
		return vm_int(slot);
	/* [L2] AIN_ARRAY_TYPE is a macro over 8 concrete array types; v12's
	 * generic AIN_ARRAY(79) is NOT included (see add_value_to_gsave). */
	case AIN_ARRAY:
	case AIN_ARRAY_TYPE:
		slot = heap_alloc_slot(VM_PAGE);
		if (cJSON_IsNull(json)) {
			heap[slot].page = NULL;
			return vm_int(slot);
		}
		if (!cJSON_IsArray(json)) {
			invalid_save_data("Not an array", json);
			heap[slot].page = NULL;
			return vm_int(slot);
		}
		dims = xmalloc(sizeof(union vm_value) * array_rank);
		get_array_dims(json, array_rank, dims);
		page = alloc_array(array_rank, dims, type, struct_type, false);
		heap[slot].page = page;
		free(dims);
		json_load_page(heap[slot].page, json, false);
		return vm_int(slot);
	case AIN_REF_TYPE:
		return vm_int(-1);
	default:
		WARNING("Unhandled data type: %s", ain_strtype(ain, type, -1));
		return vm_int(-1);
	}
}

static cJSON *read_save_file(const char *path)
{
	FILE *f;
	long len;
	char *buf;

	if (!(f = file_open_utf8(path, "rb"))) {
		WARNING("Failed to open save file: %s: %s", display_utf0(path), strerror(errno));
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);

	buf = xmalloc(len+1);
	buf[len] = '\0';
	if (fread(buf, len, 1, f) != 1) {
		WARNING("Failed to read save file: %s", display_utf0(path));
		free(buf);
		return 0;
	}
	fclose(f);

	cJSON *r = cJSON_Parse(buf);
	free(buf);
	return r;
}

static int load_globals_from_json(const char *path, const char *keyname, const char *group_name, int *n)
{
	int retval = 0;
	cJSON *save = read_save_file(path);
	if (!save)
		return 0;
	if (!cJSON_IsObject(save)) {
		invalid_save_data("Not an object", save);
		goto cleanup;
	}

	cJSON *key = cJSON_GetObjectItem(save, "key");
	if (!key || strcmp(keyname, cJSON_GetStringValue(key)))
		VM_ERROR("Attempted to load save data with wrong key: %s", display_sjis0(keyname));

	if (group_name) {
		// TODO?
	}

	cJSON *globals = cJSON_GetObjectItem(save, "globals");
	if (!globals || !cJSON_IsArray(globals)) {
		invalid_save_data("Not an array", globals);
		goto cleanup;
	}

	cJSON *g;
	cJSON_ArrayForEach(g, globals) {
		cJSON *index, *value;
		if (!(index = cJSON_GetObjectItem(g, "index"))) {
			invalid_save_data("Missing index", g);
			goto cleanup;
		}
		if (!(value = cJSON_GetObjectItem(g, "value"))) {
			invalid_save_data("Missing value", g);
			goto cleanup;
		}
		if (!cJSON_IsNumber(index)) {
			invalid_save_data("Not a number", index);
			goto cleanup;
		}

		int i = index->valueint;
		if (i < 0 || i > ain->nr_globals) {
			invalid_save_data("Invalid global index", index);
			goto cleanup;
		}
		current_global = i;

		bool call_dtors = false; // Destructors for old objects are not called.
		global_set(i, json_to_vm_value(ain->globals[i].type.data, ain->globals[i].type.struc, ain->globals[i].type.rank, value), call_dtors);
		if (n)
			(*n)++;
	}

	retval = 1;
cleanup:
	cJSON_Delete(save);
	return retval;
}

static int struct_member_index(struct ain_struct *st, const char *name)
{
	for (int i = 0; i < st->nr_members; i++) {
		if (!strcmp(st->members[i].name, name))
			return i;
	}
	return -1;
}

/* [L2] Table indices come from the save file and must never be trusted.
 * An older writer stored -1 for generic arrays it could not serialise, and
 * any truncated/corrupt file can point outside the table. Both mean "no
 * value", which every caller already knows how to handle for rank == -1.
 * Returns true when `value` is a usable index into `n` entries. */
static bool gsave_index_ok(int32_t value, int32_t n)
{
	return value >= 0 && value < n;
}

/* [L2] Reverse of array_type(): turn a saved element kind back into the
 * concrete array type that alloc_array()/variable_type() expect. Returns
 * AIN_VOID when the element kind cannot be typed (empty generic array). */
static enum ain_data_type array_type_of_elem(enum ain_data_type elem)
{
	switch (elem) {
	case AIN_INT:       return AIN_ARRAY_INT;
	case AIN_FLOAT:     return AIN_ARRAY_FLOAT;
	case AIN_STRING:    return AIN_ARRAY_STRING;
	case AIN_STRUCT:    return AIN_ARRAY_STRUCT;
	case AIN_FUNC_TYPE: return AIN_ARRAY_FUNC_TYPE;
	case AIN_BOOL:      return AIN_ARRAY_BOOL;
	case AIN_LONG_INT:  return AIN_ARRAY_LONG_INT;
	case AIN_DELEGATE:  return AIN_ARRAY_DELEGATE;
	default:            return AIN_VOID;
	}
}

static union vm_value gsave_to_vm_value(struct gsave *save, enum ain_data_type type, int struct_type, int array_rank, int32_t value);

static struct gsave_flat_array *gsave_load_array(struct gsave *save, struct page *page, struct gsave_flat_array *flat_array)
{
	assert(page->type == ARRAY_PAGE);
	if (page->array.rank > 1) {
		for (int i = 0; i < page->nr_vars; i++)
			flat_array = gsave_load_array(save, heap_get_page(page->values[i].i), flat_array);
		return flat_array;
	}

	if (page->nr_vars != flat_array->nr_values)
		VM_ERROR("Bad save file: unexpected number of array elements");
	for (int i = 0; i < page->nr_vars; i++) {
		int struct_type, array_rank;
		enum ain_data_type data_type = variable_type(page, i, &struct_type, &array_rank);
		if (flat_array->values[i].type == AIN_VOID) {
			/* [L2] Files written before the writer learned to skip untyped
			 * arrays carry VOID-typed values. They cannot be interpreted,
			 * so leave the element at its "unset" value instead of failing
			 * the whole load. */
			static int nr_void;
			if (nr_void++ < 3)
				WARNING("save: array element %d has no recorded type;"
					" leaving it unset", i);
			page->values[i] = variable_initval(data_type);
			continue;
		}
		if (data_type != flat_array->values[i].type)
			VM_ERROR("Bad save file: unexpected array element type"
				 " (element %d/%d: page says %s(%d), file says %s(%d);"
				 " page a_type=%d rank=%d struc=%d)",
				 i, page->nr_vars,
				 ain_strtype(ain, data_type, struct_type), data_type,
				 ain_strtype(ain, flat_array->values[i].type, -1),
				 flat_array->values[i].type,
				 page->a_type, page->array.rank, page->array.struct_type);
		union vm_value val = gsave_to_vm_value(save, data_type, struct_type, array_rank, flat_array->values[i].value);
		page->values[i] = val;
	}
	return flat_array + 1;
}

static union vm_value gsave_to_vm_value(struct gsave *save, enum ain_data_type type, int struct_type, int array_rank, int32_t value)
{
	switch (type) {
	case AIN_VOID:
	case AIN_INT:
	case AIN_BOOL:
	case AIN_LONG_INT:
	case AIN_FLOAT:
		return vm_int(value);
	case AIN_STRING:
		{
			/* [L2] Same bounds rule as struct/array: a file index outside
			 * the string table means "empty string", not a trap. */
			const struct string *s = &EMPTY_STRING;
			if (value != GSAVE7_EMPTY_STRING && gsave_index_ok(value, save->nr_strings))
				s = save->strings[value];
			int slot = heap_alloc_slot(VM_STRING);
			heap[slot].s = string_ref((struct string *) s);
			return vm_int(slot);
		}
	case AIN_STRUCT:
		{
			if (!gsave_index_ok(value, save->nr_records)) {
				static int nr_oob;
				if (nr_oob++ < 3)
					WARNING("save record index %d out of range (%d records);"
						" treating as unset", value, save->nr_records);
				return vm_int(-1);
			}
			struct gsave_record *rec = &save->records[value];
			struct ain_struct *st = &ain->structures[struct_type];
			struct gsave_struct_def *sd =
				save->version >= 7 ? &save->struct_defs[rec->struct_index] : NULL;
			const char *struct_name = sd ? sd->name : rec->struct_name;
			if (strcmp(struct_name, st->name))
				VM_ERROR("Bad save file: structure name mismatch");
			int slot = alloc_struct(struct_type);
			struct page *page = heap[slot].page;
			for (int i = 0; i < rec->nr_indices; i++) {
				struct gsave_keyval *kv = &save->keyvals[rec->indices[i]];
				const char *field_name = sd ? sd->fields[i].name : kv->name;
				enum ain_data_type field_type = sd ? sd->fields[i].type : kv->type;
				int index = struct_member_index(st, field_name);
				if (index < 0) {
					WARNING("structure %s has no member named %s", display_sjis0(struct_name), display_sjis1(field_name));
					continue;
				}
				int struct_type, array_rank;
				enum ain_data_type data_type = variable_type(page, index, &struct_type, &array_rank);
				if (data_type != field_type)
					VM_ERROR("Bad save file: structure member type mismatch");
				union vm_value val = gsave_to_vm_value(save, data_type, struct_type, array_rank, kv->value);
				page->values[index] = val;
			}
			return vm_int(slot);
		}
	/* [L2] AIN_ARRAY_TYPE is a macro over 8 concrete array types; v12's
	 * generic AIN_ARRAY(79) is NOT included (see add_value_to_gsave). */
	case AIN_ARRAY:
	case AIN_ARRAY_TYPE:
		{
			int slot = heap_alloc_slot(VM_PAGE);
			if (!gsave_index_ok(value, save->nr_arrays)) {
				/* Not a table entry: no data was ever written for this
				 * variable (old files serialised generic arrays as -1). */
				static int nr_oob;
				if (nr_oob++ < 3)
					WARNING("save array index %d out of range (%d arrays);"
						" treating as unset", value, save->nr_arrays);
				heap[slot].page = NULL;
				return vm_int(slot);
			}
			struct gsave_array *array = &save->arrays[value];
			if (array->rank == -1) {
				heap[slot].page = NULL;
				return vm_int(slot);
			}
			int rank = array->rank;
			enum ain_data_type data_type = type;
			if (type == AIN_ARRAY) {
				/* [L2] A generic array carries NO element kind in its
				 * declaration (that is what "array<?>" means), and its
				 * declared rank is a placeholder: 51 globals in this AIN
				 * declare rank=1 while the instances are whatever the
				 * script built. So for generic arrays the saved instance is
				 * authoritative for both rank and element kind -- trusting
				 * the declaration produced "array rank mismatch" on every
				 * generic global at load time.
				 * The element kind is recoverable from the save itself:
				 * collect_flat_arrays() stores it per flat array (v7+) and
				 * per value (v<=5), so it is present in both formats. */
				if (rank < 1 || rank > 8)
					VM_ERROR("Bad save file: generic array rank out of range");
				enum ain_data_type elem = AIN_VOID;
				if (array->nr_flat_arrays > 0 && array->flat_arrays)
					elem = array->flat_arrays[0].type;
				data_type = array_type_of_elem(elem);
				if (data_type == AIN_VOID) {
					/* Empty generic array: no element to type it from.
					 * struct_type is the only surviving hint. */
					data_type = struct_type >= 0 ? AIN_ARRAY_STRUCT : AIN_ARRAY_INT;
					WARNING("generic array with rank=%d has no elements; "
						"retyping as %s", rank,
						struct_type >= 0 ? "array<struct>" : "array<int>");
				}
			} else if (array_rank != rank) {
				VM_ERROR("Bad save file: array rank mismatch");
			}
			union vm_value *dims = xmalloc(sizeof(union vm_value) * rank);
			for (int i = 0; i < rank; i++)
				dims[i].i = array->dimensions[rank - 1 - i];
			struct page *page = alloc_array(rank, dims, data_type, struct_type, false);
			heap[slot].page = page;
			free(dims);
			gsave_load_array(save, page, array->flat_arrays);
			return vm_int(slot);
		}
	case AIN_REF_TYPE:
		return vm_int(-1);
	default:
		WARNING("Unhandled data type: %s", ain_strtype(ain, type, -1));
		return vm_int(-1);
	}
}

int load_globals_from_gsave(struct gsave *save, const char *keyname, const char *group_name, int *n)
{
	if (strcmp(keyname, save->key))
		VM_ERROR("Attempted to load save data with wrong key: %s", display_sjis0(keyname));
	if (!group_name)
		group_name = "";
	if (!save->group)
		save->group = strdup("");
	if (strcmp(group_name, save->group)) {
		/* [D139] 原版日文存档的 group 是 Shift-JIS（コレクション），
		 * 本地化 AIN 的 global group 是 GBK（收藏），两者按字节永远不等。
		 * 这是语言差异，不是存档损坏。默认仍严格拒绝；做「导入 PC 原版
		 * 通关档」这类实验时可设 XSYS4_SAVE_GROUP_ANY=1 降为告警。 */
		static int group_any = -1;
		if (group_any < 0)
			group_any = getenv("XSYS4_SAVE_GROUP_ANY") ? 1 : 0;
		if (!group_any)
			VM_ERROR("Attempted to load save data with wrong group name: '%s'",
				 display_sjis0(group_name));
		WARNING("save group mismatch (expected '%s', file has '%s'); "
			"accepted because XSYS4_SAVE_GROUP_ANY=1",
			display_sjis0(group_name), display_sjis0(save->group));
	}

	for (struct gsave_global *g = save->globals; g < save->globals + save->nr_globals; g++) {
		int global_index = ain_get_global(ain, g->name);
		if (global_index < 0) {
			WARNING("invalid global name %s", display_sjis0(g->name));
			return 0;
		}
		struct ain_type *type = &ain->globals[global_index].type;
		if (type->data != g->type) {
			WARNING("%s: type mismatch", display_sjis0(g->name));
			return 0;
		}
		/* [L2] XSYS4_GSAVE_DBG=1: name the globals whose saved array index
		 * is out of range BEFORE gsave_to_vm_value() dereferences it. The
		 * crash site only knows `rank`, not which global owned it.
		 * AIN_ARRAY_TYPE is a macro (cannot be compared), so classify by
		 * the printable type name instead. */
		const char *tname = ain_strtype(ain, type->data, -1);
		if (tname && strstr(tname, "array") && getenv("XSYS4_GSAVE_DBG")) {
			int ok = g->value >= 0 && g->value < save->nr_arrays;
			NOTICE("[gsave] %s type=%d rank=%d value=%d nr_arrays=%d in_range=%d"
			       " rank_saved=%d flats=%d",
			       display_sjis0(g->name), type->data, type->rank, g->value,
			       save->nr_arrays, ok,
			       ok ? save->arrays[g->value].rank : -99,
			       ok ? save->arrays[g->value].nr_flat_arrays : -99);
		}
		bool call_dtors = false; // Destructors for old objects should not be called.
		global_set(global_index, gsave_to_vm_value(save, type->data, type->struc, type->rank, g->value), call_dtors);
		if (n)
			(*n)++;
	}

	return 1;
}

int load_globals(const char *keyname, const char *filename, const char *group_name, int *n)
{
	char *path = savedir_path(filename);
	int retval;

	// First, try reading as a gsave.
	enum savefile_error error;
	struct gsave *save = gsave_read(path, &error);
	switch (error) {
	case SAVEFILE_SUCCESS:
		free(path);
		retval = load_globals_from_gsave(save, keyname, group_name, n);
		gsave_free(save);
		return retval;
	case SAVEFILE_INVALID_SIGNATURE:
		// If not a System4 save file, try reading as a json.
		retval = load_globals_from_json(path, keyname, group_name, n);
		free(path);
		return retval;
	default:
		WARNING("%s: %s", display_utf0(path), savefile_strerror(error));
		free(path);
		return 0;
	}
}

int delete_save_file(const char *filename)
{
	char *path = savedir_path(filename);
	if (!file_exists(path)) {
		free(path);
		return 0;
	}
	if (remove_utf8(path)) {
		WARNING("remove(\"%s\"): %s", display_utf0(path), strerror(errno));
		free(path);
		return 0;
	}
	free(path);
	return 1;
}
