/* Copyright (C) 2026 Rance10 Android port
 *
 * Activity (.pactex) loading: System4 EX tree -> engine parts tree.
 *
 * This file is part of xsystem4.
 *
 * xsystem4 is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

/*
 * D89: the Activity subsystem used to be a block of hand written stubs in
 * hll/PartsEngine.c (f[22..52]), so ReadActivityFile() always returned 0,
 * the parts table stayed empty and nothing was ever rendered.
 *
 * The file format is System4 EX (see libsys4/src/ex.c: ex_read()). One
 * activity file holds a tree:
 *
 *   アクティビティ
 *     └─ ルートパーツ            (49 children = 47 field leaves + 2 sub-nodes)
 *          ├─ 座標 / 原点座標 / ... (47 shared fields)
 *          ├─ 种类別情报          (type discriminator + type specific fields)
 *          └─ 子パーツ            (one node per child part, recursive)
 *
 * Strings inside the file are GBK (this is the Chinese release), and they must
 * stay GBK in memory: the AIN compares its own GBK constants against them, e.g.
 * GetActivityPartsNumber(name, "ルートパーツ"). Field *names* are therefore
 * converted to UTF-8 only for matching, never for storage.
 *
 * The activity parts are materialized here, in C, because the AIN never does
 * it: the whole game script only calls Parts_SetPartsCG three times, which is
 * far too few to build a parts tree. ReadActivityFile must parse, build and
 * register.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "asset_manager.h"
#include "parts.h"

#define ACT_DEBUG(...) do { if (activity_debug) NOTICE(__VA_ARGS__); } while (0)

/* Component type numbers accepted by PE_SetComponentType(). */
enum act_component {
	ACT_COMP_LAYOUT_BOX = 8,
	ACT_COMP_CG         = 11,
	ACT_COMP_TEXT       = 13,
	ACT_COMP_RECT       = 17,
	ACT_COMP_FLAT       = 20,
};

struct activity_part {
	char *name;            /* GBK bytes, as stored in the file */
	int type;              /* ACT_COMP_* */
	int parts_no;          /* engine parts number assigned at build time */

	/* shared fields (see Rance10_AIN底层研究-20261110.md §5.5) */
	float x, y;
	int z;
	int origin_x, origin_y, origin_mode;
	bool flip_x, flip_y, show;
	int alpha;
	int add_rgb[3];
	int mul_rgb[3];
	float scale_x, scale_y;
	float rot_x, rot_y, rot_z;
	int surface_area[4];

	/* 种类別情报 / 一般状态 */
	char *cg_name;         /* GBK bytes, empty when none */
	int deform;

	int nr_children;
	struct activity_part **children;
};

struct activity {
	char *name;            /* activity name (the "Name" argument) */
	int version;
	int parts_version;
	char *background;      /* 背景图片, GBK bytes */

	struct activity_part *root;
	int nr_parts;
	struct activity_part **flat;

	struct activity *next;
};

static bool activity_debug = false;
static bool activity_debug_checked = false;

static struct activity *activity_list = NULL;

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static bool name_is(struct string *s, const char *utf8)
{
	if (!s || !s->text)
		return false;
	char *u = sjis2utf(s->text, strlen(s->text));
	bool eq = u && !strcmp(u, utf8);
	free(u);
	return eq;
}

static char *dup_text(struct string *s)
{
	if (!s || !s->text)
		return NULL;
	return strdup(s->text);
}

static struct ex_tree *get_child(struct ex_tree *t, const char *utf8_name)
{
	if (!t || t->is_leaf)
		return NULL;
	for (uint32_t i = 0; i < t->nr_children; i++) {
		if (name_is(t->children[i].name, utf8_name))
			return &t->children[i];
	}
	return NULL;
}

static bool get_int(struct ex_tree *t, const char *name, int *out)
{
	struct ex_tree *c = get_child(t, name);
	if (!c || !c->is_leaf || c->leaf.value.type != EX_INT)
		return false;
	*out = c->leaf.value.i;
	return true;
}

static char *get_string(struct ex_tree *t, const char *name)
{
	struct ex_tree *c = get_child(t, name);
	if (!c || !c->is_leaf || c->leaf.value.type != EX_STRING)
		return NULL;
	return dup_text(c->leaf.value.s);
}

/* list<int|float> field -> int array */
static bool get_int_list(struct ex_tree *t, const char *name, int *out, int n)
{
	struct ex_tree *c = get_child(t, name);
	if (!c || !c->is_leaf || c->leaf.value.type != EX_LIST)
		return false;
	struct ex_list *l = c->leaf.value.list;
	for (uint32_t i = 0; i < l->nr_items && (int)i < n; i++) {
		struct ex_value *v = &l->items[i].value;
		if (v->type == EX_INT)
			out[i] = v->i;
		else if (v->type == EX_FLOAT)
			out[i] = (int)v->f;
	}
	return true;
}

static bool get_float_list(struct ex_tree *t, const char *name, float *out, int n)
{
	struct ex_tree *c = get_child(t, name);
	if (!c || !c->is_leaf || c->leaf.value.type != EX_LIST)
		return false;
	struct ex_list *l = c->leaf.value.list;
	for (uint32_t i = 0; i < l->nr_items && (int)i < n; i++) {
		struct ex_value *v = &l->items[i].value;
		if (v->type == EX_FLOAT)
			out[i] = v->f;
		else if (v->type == EX_INT)
			out[i] = (float)v->i;
	}
	return true;
}

static int component_type_from_name(const char *type_name)
{
	if (!type_name)
		return ACT_COMP_CG;
	/* type strings are GBK; convert for the comparison only */
	char *u = sjis2utf(type_name, strlen(type_name));
	int type = ACT_COMP_CG;
	if (u) {
		if (!strcmp(u, "レイアウトボックス"))
			type = ACT_COMP_LAYOUT_BOX;
		else if (!strcmp(u, "文本パーツ"))
			type = ACT_COMP_TEXT;
		else if (!strcmp(u, "矩形パーツ"))
			type = ACT_COMP_RECT;
		else if (!strcmp(u, "フラットパーツ"))
			type = ACT_COMP_FLAT;
		else if (!strcmp(u, "ＣＧパーツ") || !strcmp(u, "低レベルパーツ") ||
			 !strcmp(u, "按钮") || !strcmp(u, "复选框") ||
			 !strcmp(u, "消息窗口") || !strcmp(u, "用户组件") ||
			 !strcmp(u, "リストボックス"))
			type = ACT_COMP_CG;
		free(u);
	}
	return type;
}

/* ------------------------------------------------------------------ */
/* parsing                                                            */
/* ------------------------------------------------------------------ */

static void parse_part_fields(struct ex_tree *node, struct activity_part *part)
{
	int ivals[5];
	float fvals[3];

	memset(ivals, 0, sizeof(ivals));
	memset(fvals, 0, sizeof(fvals));

	part->x = 0;
	part->y = 0;
	part->z = 0;
	if (get_float_list(node, "座標", fvals, 2)) {
		part->x = fvals[0];
		part->y = fvals[1];
	}
	if (get_int_list(node, "座標", ivals, 3))
		part->z = ivals[2];

	part->origin_x = 0;
	part->origin_y = 0;
	if (get_int_list(node, "原点座標", ivals, 2)) {
		part->origin_x = ivals[0];
		part->origin_y = ivals[1];
	}
	get_int(node, "原点座標模式", &part->origin_mode);

	int b = 0;
	part->flip_x = get_int(node, "左右反转", &b) ? b != 0 : false;
	b = 0;
	part->flip_y = get_int(node, "上下反转", &b) ? b != 0 : false;
	b = 1;
	part->show = get_int(node, "表示", &b) ? b != 0 : true;

	b = 255;
	part->alpha = get_int(node, "アルファ", &b) ? b : 255;

	if (get_int_list(node, "加法色", part->add_rgb, 3) != true)
		memset(part->add_rgb, 0, sizeof(part->add_rgb));
	part->mul_rgb[0] = part->mul_rgb[1] = part->mul_rgb[2] = 255;
	get_int_list(node, "乘法色", part->mul_rgb, 3);

	part->scale_x = 1.0f;
	part->scale_y = 1.0f;
	if (get_float_list(node, "拡大縮小", fvals, 2)) {
		part->scale_x = fvals[0];
		part->scale_y = fvals[1];
	}

	memset(fvals, 0, sizeof(fvals));
	if (get_float_list(node, "旋转", fvals, 3)) {
		part->rot_x = fvals[0];
		part->rot_y = fvals[1];
		part->rot_z = fvals[2];
	}

	get_int_list(node, "表示領域", part->surface_area, 4);

	/* 种类別情报: part type + type specific payload */
	struct ex_tree *type_info = get_child(node, "种类別情报");
	if (type_info) {
		char *tn = get_string(type_info, "パーツ类型");
		part->type = component_type_from_name(tn);
		free(tn);

		/* "低レベルパーツ" keeps the CG inside its general state */
		struct ex_tree *state = get_child(type_info, "一般状态");
		struct ex_tree *src = state ? state : type_info;
		if (!part->cg_name)
			part->cg_name = get_string(src, "ＣＧ名");
		if (!part->cg_name)
			part->cg_name = get_string(type_info, "ＣＧ名");
		get_int(src, "变形", &part->deform);
	} else {
		part->type = ACT_COMP_CG;
	}

	if (!part->cg_name)
		part->cg_name = strdup("");
}

static struct activity_part *parse_part(struct ex_tree *node)
{
	struct activity_part *part = xcalloc(1, sizeof(struct activity_part));
	part->name = dup_text(node->name);
	parse_part_fields(node, part);

	struct ex_tree *children = get_child(node, "子パーツ");
	if (children && !children->is_leaf) {
		part->children = xcalloc(children->nr_children, sizeof(struct activity_part *));
		for (uint32_t i = 0; i < children->nr_children; i++) {
			struct ex_tree *c = &children->children[i];
			if (c->is_leaf)
				continue;
			part->children[part->nr_children++] = parse_part(c);
		}
	}
	return part;
}

static void count_parts(struct activity_part *part, int *n)
{
	(*n)++;
	for (int i = 0; i < part->nr_children; i++)
		count_parts(part->children[i], n);
}

static void flatten_parts(struct activity_part *part, struct activity_part **flat, int *n)
{
	flat[(*n)++] = part;
	for (int i = 0; i < part->nr_children; i++)
		flatten_parts(part->children[i], flat, n);
}

/* ------------------------------------------------------------------ */
/* building the engine parts tree                                     */
/* ------------------------------------------------------------------ */

static int build_part(struct activity_part *part, int parent_no)
{
	part->parts_no = PE_GetFreeNumber();

	PE_SetComponentType(part->parts_no, part->type, 1);
	PE_SetPos(part->parts_no, (int)part->x, (int)part->y);
	PE_SetZ(part->parts_no, part->z);
	PE_SetShow(part->parts_no, part->show);
	PE_SetAlpha(part->parts_no, part->alpha);
	PE_SetAddColor(part->parts_no, part->add_rgb[0], part->add_rgb[1], part->add_rgb[2]);
	PE_SetMultiplyColor(part->parts_no, part->mul_rgb[0], part->mul_rgb[1], part->mul_rgb[2]);
	PE_SetPartsOriginPosMode(part->parts_no, part->origin_mode);
	PE_SetPartsMagX(part->parts_no, part->scale_x);
	PE_SetPartsMagY(part->parts_no, part->scale_y);
	PE_SetPartsRotateX(part->parts_no, part->rot_x);
	PE_SetPartsRotateY(part->parts_no, part->rot_y);
	PE_SetPartsRotateZ(part->parts_no, part->rot_z);
	if (part->type == ACT_COMP_LAYOUT_BOX) {
		/* the layout box owns its children's placement */
	}

	if (part->cg_name && *part->cg_name && part->type == ACT_COMP_CG) {
		struct string *cg = cstr_to_string(part->cg_name);
		bool ok = PE_SetPartsCG(part->parts_no, cg, part->deform, 1);
		ACT_DEBUG("[act] %s parts_no=%d pos=(%d,%d,%d) cg='%s' set=%d size=%dx%d",
			  part->name, part->parts_no, (int)part->x, (int)part->y, part->z,
			  part->cg_name, ok,
			  PE_GetPartsWidth(part->parts_no, 1),
			  PE_GetPartsHeight(part->parts_no, 1));
		free_string(cg);
	} else {
		ACT_DEBUG("[act] %s parts_no=%d type=%d (no cg)", part->name,
			  part->parts_no, part->type);
	}

	if (parent_no >= 0)
		PE_SetParentPartsNumber(part->parts_no, parent_no);

	for (int i = 0; i < part->nr_children; i++)
		build_part(part->children[i], part->parts_no);

	return part->parts_no;
}

/* ------------------------------------------------------------------ */
/* public API                                                         */
/* ------------------------------------------------------------------ */

static struct activity *find_activity(const char *name)
{
	for (struct activity *a = activity_list; a; a = a->next) {
		if (a->name && name && !strcmp(a->name, name))
			return a;
	}
	return NULL;
}

bool activity_exists(const char *name)
{
	return find_activity(name) != NULL;
}

struct activity *activity_get(const char *name)
{
	return find_activity(name);
}

bool activity_is_exist_file(const char *file)
{
	if (!file || !*file)
		return false;
	int id = -1;
	return asset_exists_by_name(ASSET_PACT, file, &id);
}

static void free_part(struct activity_part *part)
{
	for (int i = 0; i < part->nr_children; i++)
		free_part(part->children[i]);
	free(part->children);
	free(part->name);
	free(part->cg_name);
	free(part);
}

void activity_free(struct activity *act)
{
	if (!act)
		return;
	if (act->root)
		free_part(act->root);
	free(act->flat);
	free(act->name);
	free(act->background);
	free(act);
}

bool activity_release(const char *name)
{
	struct activity **p = &activity_list;
	while (*p) {
		if ((*p)->name && name && !strcmp((*p)->name, name)) {
			struct activity *dead = *p;
			*p = dead->next;
			for (int i = 0; i < dead->nr_parts; i++)
				PE_ReleaseParts(dead->flat[i]->parts_no);
			activity_free(dead);
			return true;
		}
		p = &(*p)->next;
	}
	return false;
}

void activity_release_all(void)
{
	while (activity_list) {
		struct activity *dead = activity_list;
		activity_list = dead->next;
		for (int i = 0; i < dead->nr_parts; i++)
			PE_ReleaseParts(dead->flat[i]->parts_no);
		activity_free(dead);
	}
}

int activity_nr_parts(struct activity *act)
{
	return act ? act->nr_parts : 0;
}

struct activity_part *activity_find_parts(struct activity *act, const char *parts_name)
{
	if (!act || !parts_name)
		return NULL;
	for (int i = 0; i < act->nr_parts; i++) {
		if (act->flat[i]->name && !strcmp(act->flat[i]->name, parts_name))
			return act->flat[i];
	}
	return NULL;
}

int activity_find_parts_no(struct activity *act, const char *parts_name)
{
	struct activity_part *p = activity_find_parts(act, parts_name);
	return p ? p->parts_no : 0;
}

int activity_part_no_at(struct activity *act, int index)
{
	if (!act || index < 0 || index >= act->nr_parts)
		return 0;
	return act->flat[index]->parts_no;
}

const char *activity_part_name_at(struct activity *act, int index)
{
	if (!act || index < 0 || index >= act->nr_parts)
		return "";
	return act->flat[index]->name;
}

int activity_part_cg_name_at(struct activity *act, int index, const char **out)
{
	if (!act || index < 0 || index >= act->nr_parts) {
		if (out) *out = "";
		return 0;
	}
	if (out) *out = act->flat[index]->cg_name;
	return 1;
}

/*
 * Read an activity file from the PACT archive and build the parts tree.
 * `name` is the activity name the AIN will use afterwards, `file` the archive
 * entry name (both without extension in practice).
 */
bool activity_read(struct string *name, struct string *file)
{
	if (!activity_debug_checked) {
		activity_debug = getenv("XSYS4_ACT_DBG") != NULL;
		activity_debug_checked = true;
	}
	if (!name || !name->text || !file || !file->text)
		return false;

	int id = -1;
	struct archive_data *ad = asset_get_by_name(ASSET_PACT, file->text, &id);
	if (!ad || !ad->data) {
		ACT_DEBUG("[act] '%s' not found in PACT archive", file->text);
		return false;
	}

	struct ex *ex = ex_read((const uint8_t *)ad->data, ad->size);
	if (!ex) {
		ACT_DEBUG("[act] ex_read failed for '%s'", file->text);
		return false;
	}
	if (ex->nr_blocks < 1 || ex->blocks[0].val.type != EX_TREE) {
		ACT_DEBUG("[act] block 0 of '%s' is not a tree", file->text);
		ex_free(ex);
		return false;
	}

	struct activity *act = xcalloc(1, sizeof(struct activity));
	act->name = strdup(name->text);
	if (!act->name[0])
		act->name = strdup(file->text);

	/* Metadata blocks are positional-ish but the set varies (4/5/6 blocks),
	 * so walk them by type instead of by index. */
	int nr_ints = 0, nr_strs = 0;
	for (uint32_t i = 1; i < ex->nr_blocks; i++) {
		struct ex_value *v = &ex->blocks[i].val;
		if (v->type == EX_INT) {
			if (nr_ints == 0)
				act->version = v->i;
			else if (nr_ints == 1)
				act->parts_version = v->i;
			nr_ints++;
		} else if (v->type == EX_STRING) {
			/* first string block is 拡大, second is 背景图片 */
			if (nr_strs == 1)
				act->background = dup_text(v->s);
			nr_strs++;
		}
	}

	struct ex_tree *top = ex->blocks[0].val.tree;
	struct ex_tree *root = top->is_leaf ? NULL : &top->children[0];
	if (!root || root->is_leaf) {
		ACT_DEBUG("[act] '%s' has no root parts", file->text);
		ex_free(ex);
		activity_free(act);
		return false;
	}

	act->root = parse_part(root);
	count_parts(act->root, &act->nr_parts);
	act->flat = xcalloc(act->nr_parts, sizeof(struct activity_part *));
	int n = 0;
	flatten_parts(act->root, act->flat, &n);

	ex_free(ex);

	/* replace a previous load of the same name */
	struct activity *old = find_activity(act->name);
	if (old) {
		activity_release(act->name);
	}

	build_part(act->root, -1);

	act->next = activity_list;
	activity_list = act;

	NOTICE("[act] loaded '%s' from '%s' (version %d/%d, %d parts)",
	       act->name, file->text, act->version, act->parts_version, act->nr_parts);
	return true;
}

struct activity *activity_create(const char *name)
{
	struct activity *act = find_activity(name);
	if (act)
		return act;
	act = xcalloc(1, sizeof(struct activity));
	act->name = strdup(name ? name : "");
	act->next = activity_list;
	activity_list = act;
	return act;
}

bool activity_add_parts(struct string *name, struct string *parts_name, int parts_no)
{
	struct activity *act = find_activity(name ? name->text : NULL);
	if (!act || !parts_name)
		return false;
	/* already registered (e.g. it came from the file) */
	for (int i = 0; i < act->nr_parts; i++) {
		if (act->flat[i]->name && !strcmp(act->flat[i]->name, parts_name->text))
			return true;
	}
	struct activity_part *p = xcalloc(1, sizeof(struct activity_part));
	p->name = strdup(parts_name->text);
	p->parts_no = parts_no;
	act->flat = xrealloc(act->flat, sizeof(struct activity_part *) * (act->nr_parts + 1));
	act->flat[act->nr_parts++] = p;
	return true;
}

bool activity_remove_parts(struct string *name, struct string *parts_name)
{
	struct activity *act = find_activity(name ? name->text : NULL);
	if (!act || !parts_name)
		return false;
	for (int i = 0; i < act->nr_parts; i++) {
		if (act->flat[i]->name && !strcmp(act->flat[i]->name, parts_name->text)) {
			PE_ReleaseParts(act->flat[i]->parts_no);
			memmove(&act->flat[i], &act->flat[i + 1],
				sizeof(struct activity_part *) * (act->nr_parts - i - 1));
			act->nr_parts--;
			return true;
		}
	}
	return false;
}

void activity_remove_all_parts(struct string *name)
{
	struct activity *act = find_activity(name ? name->text : NULL);
	if (!act)
		return;
	for (int i = 0; i < act->nr_parts; i++)
		PE_ReleaseParts(act->flat[i]->parts_no);
	act->nr_parts = 0;
}

const char *activity_background(struct activity *act)
{
	return act && act->background ? act->background : "";
}
