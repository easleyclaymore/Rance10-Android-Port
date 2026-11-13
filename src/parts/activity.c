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

#include "xsystem4.h"
#include "asset_manager.h"
#include "parts.h"
#include "parts_internal.h"

#define ACT_DEBUG(...) do { if (activity_debug) NOTICE(__VA_ARGS__); } while (0)

/* Component type numbers accepted by PE_SetComponentType() / returned by
 * PE_GetComponentType().
 *
 * These are Rance10's own numbers, recovered from the game's
 * activityeditor::detail::CBatchRenameStack@GetPartsTypeName switch (AIN
 * switch #31), which maps every type number to its display name. They are
 * NOT the upstream xsystem4 numbering: Rance10 shifted most CG/gauge/flat
 * slots by a few values, so reusing the upstream constants silently made
 * every Activity component the wrong class (e.g. buttons came back as
 * CG type 11, which Activity@CreateComponent turns into a SpinBox, and
 * Activity@GetButton's ICAST then failed). */
enum act_component {
	ACT_COMP_BUTTON          = 0,
	ACT_COMP_CHECKBOX        = 1,
	ACT_COMP_VSCROLLBAR      = 2,
	ACT_COMP_HSCROLLBAR      = 3,
	ACT_COMP_TEXTBOX         = 4,
	ACT_COMP_LISTBOX         = 5,
	ACT_COMP_COMBOBOX        = 6,
	ACT_COMP_MLTEXTBOX       = 7,
	ACT_COMP_LAYOUT_BOX      = 8,
	ACT_COMP_RADIOBUTTONBOX  = 9,
	ACT_COMP_MESSAGE_WINDOW  = 10,
	ACT_COMP_SPINBOX         = 11,
	ACT_COMP_PANEL           = 12,
	ACT_COMP_FORM            = 13,
	ACT_COMP_USER_COMPONENT  = 15,
	ACT_COMP_CG              = 17,
	ACT_COMP_ANIMATION_CG    = 18,
	ACT_COMP_TEXT            = 19,
	ACT_COMP_HGAUGE          = 20,
	ACT_COMP_VGAUGE          = 21,
	ACT_COMP_NUMERAL         = 22,
	ACT_COMP_RECT            = 23,
	ACT_COMP_CONSTRUCTION    = 24,
	ACT_COMP_RECT_DETECTION  = 25,
	ACT_COMP_FLAT            = 26,
	ACT_COMP_3DLAYER         = 27,
	ACT_COMP_MOVIE           = 28,
	/* EX-only: the editor's generic base part, whose real type lives in
	 * its 一般状態 child. Resolved during parsing, never passed to the
	 * engine. */
	ACT_COMP_LOW_LEVEL       = -2,
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
	int clickable;
	int pass_cursor;

	/* 种类別情报 / 一般状态 */
	char *cg_name;         /* GBK bytes, empty when none */
	int deform;
	/* [D119] Button / flat parts carry フラット名 + 尺寸 instead of a CG
	 * name (see exdump of NetworkConfirmation.pactex). Without these the
	 * parts stay 0x0, hitbox 0x0, and buttons can never be clicked. */
	char *flat_name;       /* GBK bytes, NULL when absent */
	int w, h;              /* explicit 尺寸, 0 when absent */

	/* 用户组件 (component type 15): the EX payload is
	 *   数据 = ['オブジェクト名', 'TextButton', '参数', '标题,从头开始']
	 * i.e. an engine built-in widget plus its comma-separated parameters.
	 * Rance10's title menu is entirely made of these. */
	char *uc_object;       /* GBK bytes: 'TextButton', ... */
	char *uc_params;       /* GBK bytes: '标题,从头开始' */
	/* [D122] EX 的 数据 是 [键,值,键,值,...]。此前只抽取了
	 * 'オブジェクト名'/'参数' 供 C 侧画文字，没有把原始键值写回引擎的
	 * 用户组件数据袋，于是 AIN 的 ActivityUserObject::GetParam(22526) 调
	 * PartsEngine.GetUserComponentData(no, "参数") 拿到 (nil)，
	 * TextButton@Init 第一步（参数个数 >= 2）就 return，8 个菜单按钮的
	 * 点击委托全都没注册。原样留存整个列表，build_part 时逐项写回。 */
	char **uc_data;        /* flat [key, value, key, value, ...], GBK bytes */
	int uc_data_n;

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

static struct ex_tree *get_child(struct ex_tree *t, const char *utf8_name);

/* Look up one key in a 用户组件 payload list
 * (数据 = ['オブジェクト名','TextButton','参数','...']), returning its value as
 * GBK bytes. `key` is UTF-8 in source and converted to the encoding used by the
 * EX file. */
static char *uc_param(struct ex_tree *type_info, const char *key)
{
	struct ex_tree *c = get_child(type_info, "数据");
	char *gbk = utf2sjis(key, strlen(key));
	if (!c || !c->is_leaf || c->leaf.value.type != EX_LIST) {
		free(gbk);
		return NULL;
	}
	struct ex_list *l = c->leaf.value.list;
	if (!l) {
		free(gbk);
		return NULL;
	}
	if (!gbk)
		return NULL;
	char *out = NULL;
	for (uint32_t i = 0; i + 1 < l->nr_items; i += 2) {
		struct ex_value *k = &l->items[i].value;
		struct ex_value *v = &l->items[i + 1].value;
		if (k->type != EX_STRING || !k->s || !k->s->text)
			continue;
		if (strcmp(k->s->text, gbk) != 0)
			continue;
		if (v->type == EX_STRING && v->s)
			out = strdup(v->s->text);
		break;
	}
	free(gbk);
	return out;
}

/* [D122] 把 数据 列表原样拷进 part->uc_data，供 build_part 写回引擎。
 * 键和值都是 EX 文件自带的 GBK/SJIS 字节，不做转码——AIN 侧查询时用的
 * 也是同一套字节。非字符串项跳过（游戏的用户组件 数据 全是字符串）。 */
static void uc_read_data(struct activity_part *part, struct ex_tree *type_info)
{
	struct ex_tree *c = get_child(type_info, "数据");
	if (!c || !c->is_leaf || c->leaf.value.type != EX_LIST)
		return;
	struct ex_list *l = c->leaf.value.list;
	if (!l || l->nr_items < 2)
		return;
	part->uc_data = xcalloc(l->nr_items, sizeof(char *));
	for (uint32_t i = 0; i + 1 < l->nr_items; i += 2) {
		struct ex_value *k = &l->items[i].value;
		struct ex_value *v = &l->items[i + 1].value;
		if (k->type != EX_STRING || !k->s || !k->s->text)
			continue;
		part->uc_data[part->uc_data_n++] = strdup(k->s->text);
		part->uc_data[part->uc_data_n++] =
			(v->type == EX_STRING && v->s && v->s->text)
				? strdup(v->s->text) : strdup("");
	}
}

/* 用户组件的 参数 形如 "幅１９６,ＭＥＮＵ" 或 "标题,从头开始"。
 * 前半段若以「幅」开头，后面那串（全角或半角）数字就是按钮宽度。 */
static int uc_parse_width(const char *params)
{
	if (!params)
		return 0;
	char *fu = utf2sjis("幅", strlen("幅"));
	if (!fu)
		return 0;
	bool has = strncmp(params, fu, strlen(fu)) == 0;
	free(fu);
	if (!has)
		return 0;
	const unsigned char *p = (const unsigned char *)params + 2; /* skip 幅 */
	int w = 0, digits = 0;
	while (*p && *p != ',') {
		if (*p >= '0' && *p <= '9') {          /* half width */
			w = w * 10 + (*p - '0');
			digits++;
			p++;
		} else if (p[0] == 0xA3 && p[1] >= 0xB0 && p[1] <= 0xB9) {
			w = w * 10 + (p[1] - 0xB0);        /* full width ０..９ */
			digits++;
			p += 2;
		} else {
			return 0;
		}
	}
	return digits ? w : 0;
}

/* 用户组件的 参数 形如 "幅１９６,ＭＥＮＵ" / "标题,从头开始"：
 * 逗号前那段是宽度声明（`幅Ｗ`）或控件名，逗号后那段才是显示文字。
 * 没有逗号时整串就是显示文字。返回值是 GBK 指针（指向 params 内部）。 */
static const char *uc_parse_label(const char *params)
{
	if (!params)
		return NULL;
	const char *comma = strchr(params, ',');
	return comma ? comma + 1 : params;
}

/* [D141] 参数 的第一个逗号字段是按钮样式（标题 / 幅１９６ ...）。
 * 「标题」对应说明书 r02 里三枚红色圆形主按钮，其余对应金色小按钮。
 * 样式名是 GBK 字节串，这里只按字节前缀判断，不做编码转换。 */
static char *uc_parse_style(const char *params)
{
	if (!params)
		return NULL;
	const char *comma = strchr(params, ',');
	size_t n = comma ? (size_t)(comma - params) : strlen(params);
	return strndup(params, n);
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

/* Map a パーツ类型 name from the EX file to a Rance10 component type number.
 * Returns -1 when the name is not recognised. */
static int component_type_from_name(const char *type_name)
{
	if (!type_name)
		return -1;
	/* EX strings are stored in the game's native encoding (GBK for this
	 * build); convert once so the comparisons below use UTF-8 literals. */
	char *u = sjis2utf(type_name, strlen(type_name));
	if (!u)
		return -1;

	static const struct { const char *name; int type; } table[] = {
		{ "按钮",              ACT_COMP_BUTTON },
		{ "检查ＢＯＸ",         ACT_COMP_CHECKBOX },
		{ "检查BOX",           ACT_COMP_CHECKBOX },
		{ "复选框",             ACT_COMP_CHECKBOX },
		{ "纵向滚动条栏",        ACT_COMP_VSCROLLBAR },
		{ "横向滚动条栏",        ACT_COMP_HSCROLLBAR },
		{ "文本ＢＯＸ",         ACT_COMP_TEXTBOX },
		{ "文本BOX",           ACT_COMP_TEXTBOX },
		{ "リストＢＯＸ",        ACT_COMP_LISTBOX },
		{ "リストBOX",         ACT_COMP_LISTBOX },
		{ "ＣＯＭＢＯＢＯＸ",     ACT_COMP_COMBOBOX },
		{ "ＭＬ文本ＢＯＸ",      ACT_COMP_MLTEXTBOX },
		{ "レイアウトＢＯＸ",     ACT_COMP_LAYOUT_BOX },
		{ "レイアウトボックス",    ACT_COMP_LAYOUT_BOX },
		{ "ラジオ按钮ＢＯＸ",     ACT_COMP_RADIOBUTTONBOX },
		{ "消息窗口",            ACT_COMP_MESSAGE_WINDOW },
		{ "スピンＢＯＸ",        ACT_COMP_SPINBOX },
		{ "面板",               ACT_COMP_PANEL },
		{ "フォーム",            ACT_COMP_FORM },
		{ "用户组件",            ACT_COMP_USER_COMPONENT },
		{ "ＣＧパーツ",          ACT_COMP_CG },
		{ "ＣＧ",               ACT_COMP_CG },
		{ "循环ＣＧ",            ACT_COMP_ANIMATION_CG },
		{ "文本パーツ",          ACT_COMP_TEXT },
		{ "文本",               ACT_COMP_TEXT },
		{ "横向计量",            ACT_COMP_HGAUGE },
		{ "纵向计量",            ACT_COMP_VGAUGE },
		{ "数字",               ACT_COMP_NUMERAL },
		{ "矩形パーツ",          ACT_COMP_RECT },
		{ "矩形",               ACT_COMP_RECT },
		{ "構築",               ACT_COMP_CONSTRUCTION },
		{ "ＣＧ判定",            ACT_COMP_RECT_DETECTION },
		{ "フラットパーツ",       ACT_COMP_FLAT },
		{ "フラット",            ACT_COMP_FLAT },
		{ "３Ｄ层",             ACT_COMP_3DLAYER },
		{ "动画",               ACT_COMP_MOVIE },
		{ "低レベルパーツ",       ACT_COMP_LOW_LEVEL },
		{ NULL, 0 },
	};

	int type = -1;
	for (int i = 0; table[i].name; i++) {
		if (!strcmp(u, table[i].name)) {
			type = table[i].type;
			break;
		}
	}
	free(u);
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
	b = 0;
	part->clickable = get_int(node, "点击許可", &b) ? b : 0;
	b = 0;
	part->pass_cursor = get_int(node, "光标选中透過", &b) ? b : 0;

	/* 种类別情报: part type + type specific payload */
	struct ex_tree *type_info = get_child(node, "种类別情报");
	if (type_info) {
		char *tn = get_string(type_info, "パーツ类型");
		part->type = component_type_from_name(tn);
		free(tn);

		/* "低レベルパーツ" (the editor's generic base part) carries its
		 * real rendering type in the 一般状態 child. */
		struct ex_tree *state = get_child(type_info, "一般状态");
		if (part->type == ACT_COMP_LOW_LEVEL && state) {
			char *sn = get_string(state, "パーツ类型");
			int state_type = component_type_from_name(sn);
			free(sn);
			if (state_type >= 0)
				part->type = state_type;
		}
		if (part->type < 0) {
			WARNING("Unknown part type for '%s', treating as CG",
				display_sjis0(part->name));
			part->type = ACT_COMP_CG;
		}

		/* 用户组件: 数据 里带 'オブジェクト名'（内置控件名）与 '参数'。 */
		if (part->type == ACT_COMP_USER_COMPONENT) {
			part->uc_object = uc_param(type_info, "オブジェクト名");
			part->uc_params = uc_param(type_info, "参数");
			uc_read_data(part, type_info);
		}

		struct ex_tree *src = state ? state : type_info;
		if (!part->cg_name)
			part->cg_name = get_string(src, "ＣＧ名");
		if (!part->cg_name)
			part->cg_name = get_string(type_info, "ＣＧ名");
		/* [D119] flat/button parts: フラット名 lives in 一般状态 or 种类別情报 */
		if (!part->flat_name)
			part->flat_name = get_string(src, "フラット名");
		if (!part->flat_name)
			part->flat_name = get_string(type_info, "フラット名");
		int dims[2] = { 0, 0 };
		if (get_int_list(type_info, "尺寸", dims, 2) ||
		    get_int_list(src, "尺寸", dims, 2)) {
			part->w = dims[0];
			part->h = dims[1];
		}
		get_int(src, "变形", &part->deform);
	} else {
		part->type = ACT_COMP_CG;
	}

	if (!part->cg_name)
		part->cg_name = strdup("");
}

/* XSYS4_USERCOMP_DBG: dump the EX subtree of a user-component part.
 * Rance10's title menu (Menu / Chapter1 / Load / Enquite / Yakata / CardList)
 * is entirely made of 用户组件 (component type 15). Activity@CreateComponent's
 * switch has no case 15, so the engine is supposed to instantiate a child
 * activity named somewhere in this subtree -- and we currently map type 15 to
 * PARTS_CG, which draws nothing because no texture is ever set. */
static void dump_ex_tree(struct ex_tree *t, int depth)
{
	if (!t || depth > 4)
		return;
	for (uint32_t i = 0; i < t->nr_children; i++) {
		struct ex_tree *c = &t->children[i];
		const char *nm = c->name && c->name->text
			? display_sjis0(c->name->text) : "?";
		char namebuf[128];
		snprintf(namebuf, sizeof namebuf, "%s", nm);
		if (!c->is_leaf) {
			NOTICE("[uc] %*s%s/", depth * 2, "", namebuf);
			dump_ex_tree(c, depth + 1);
			continue;
		}
		struct ex_value *v = &c->leaf.value;
		if (v->type == EX_STRING && v->s)
			NOTICE("[uc] %*s%s = '%s'", depth * 2, "", namebuf,
			       display_sjis0(v->s->text));
		else if (v->type == EX_INT)
			NOTICE("[uc] %*s%s = %d", depth * 2, "", namebuf, v->i);
		else if (v->type == EX_LIST) {
			NOTICE("[uc] %*s%s = list[%u]", depth * 2, "", namebuf,
			       v->list ? v->list->nr_items : 0);
			for (uint32_t k = 0; v->list && k < v->list->nr_items && k < 8; k++) {
				struct ex_value *e = &v->list->items[k].value;
				if (e->type == EX_STRING && e->s)
					NOTICE("[uc] %*s  [%u] '%s'", depth * 2, "", k,
					       display_sjis0(e->s->text));
				else if (e->type == EX_INT)
					NOTICE("[uc] %*s  [%u] %d", depth * 2, "", k, e->i);
				else
					NOTICE("[uc] %*s  [%u] <type %d>", depth * 2, "", k,
					       e->type);
			}
		} else
			NOTICE("[uc] %*s%s = <type %d>", depth * 2, "", namebuf,
			       v->type);
	}
}

static struct activity_part *parse_part(struct ex_tree *node)
{
	struct activity_part *part = xcalloc(1, sizeof(struct activity_part));
	part->name = dup_text(node->name);
	parse_part_fields(node, part);
	if (part->type == ACT_COMP_USER_COMPONENT && getenv("XSYS4_USERCOMP_DBG")) {
		static int n;
		if (n++ < 3) {
			NOTICE("[uc] ==== part '%s' (type 15) ====",
			       display_sjis0(part->name));
			dump_ex_tree(node, 0);
		}
	}

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

/* Load flat artwork for one state. Returns false when the archive has no such
 * asset (a normal condition: the EX group names and the archive layout differ
 * per part kind). */
static bool set_part_flat(int parts_no, const char *name, int state)
{
	struct string *s = cstr_to_string(name);
	bool ok = PE_SetPartsFlat(parts_no, s, state);
	free_string(s);
	return ok;
}

/* ------------------------------------------------------------------ */
/* building the engine parts tree                                     */
/* ------------------------------------------------------------------ */

static int build_part(struct activity_part *part, int parent_no)
{
	part->parts_no = PE_GetFreeNumber();

	PE_SetComponentType(part->parts_no, part->type, 1);
	/* 用户组件：先给一个真实尺寸，命中矩形才有面积。
	 * 宽度优先取 参数 里的「幅Ｗ」，其次 表示領域，最后用 196（Rance10 的
	 * 菜单默认宽度）。高度暂用 40；控件层做出来后再由文字度量决定。 */
	if (part->type == ACT_COMP_USER_COMPONENT) {
		int w = uc_parse_width(part->uc_params);
		if (w <= 0 && part->surface_area[2] > 0)
			w = part->surface_area[2];
		if (w <= 0)
			w = 196;
		int h = part->surface_area[3] > 0 ? part->surface_area[3] : 40;
		PE_SetPartsSize(part->parts_no, w, h, 1);
		/* 控件种类与显示文字交给 parts 层，绘制时用它画底板和居中文字。 */
		PE_SetUserComponent(part->parts_no, part->uc_object,
				uc_parse_label(part->uc_params));
		/* 样式供绘制层区分主按钮/小按钮；用内部 key，避免与 AIN 侧
		 * GetUserComponentData("参数") 使用的键冲突。 */
		char *uc_style = uc_parse_style(part->uc_params);
		if (uc_style) {
			/* EX 字符串是 GBK；用 utf2sjis 转换后再比较，别拿源码里的
			 * UTF-8 字面量直接比字节（uc_parse_width 也是这么做的）。
			 * 归一化成 ASCII 标记，绘制层就不必关心编码。 */
			char *title = utf2sjis("标题", strlen("标题"));
			bool round = title && strncmp(uc_style, title, strlen(title)) == 0;
			free(title);
			PE_SetUserComponentData(part->parts_no, "__style",
					round ? "round" : "flat");
			free(uc_style);
		}
		/* [D122] 原始 数据 键值写回引擎。AIN 的 TextButton@Init 靠
		 * GetUserComponentData(no, "参数") 取参数并按逗号 Split；
		 * 缺了这一步 8 个标题菜单按钮全都不会注册点击委托。 */
		for (int i = 0; i + 1 < part->uc_data_n; i += 2)
			PE_SetUserComponentData(part->parts_no,
					part->uc_data[i], part->uc_data[i + 1]);
	}
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
	if (getenv("XSYS4_ACT_CLICKABLE_ALL"))
		part->clickable = 1;
	if (part->clickable)
		PE_SetClickable(part->parts_no, true);
	if (part->pass_cursor)
		PE_SetPassCursor(part->parts_no, true);

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

	/* [D119] flat/button parts load their artwork from the FLAT archive.
	 * PE_SetPartsFlat() sets the part dimensions (parts_set_dims), which in
	 * turn builds a non-empty hitbox; that is what makes the OK/Cancel
	 * buttons reachable by mouse input. */
	/* Flat artwork. A widget part (button, checkbox, ...) stores a *flat
	 * group* name (e.g. "共通素材\シス／按钮／幅４７６"); the archive holds
	 * one asset per visual state, joined with the fullwidth solidus:
	 * "<group>／一般", "<group>／オン", "<group>／DOWN", ... Plain picture
	 * parts store a full name instead, so try the exact name first and only
	 * then the 一般 variant. */
	if (part->flat_name && *part->flat_name) {
		char *variant = NULL;
		bool flat_ok = set_part_flat(part->parts_no, part->flat_name, 1);
		if (!flat_ok) {
			/* Both the separator and the state name are written in the
			 * game's own encoding. */
			char *sep = utf2sjis("／", strlen("／"));
			char *suffix = utf2sjis("一般", strlen("一般"));
			if (sep && suffix) {
				variant = xmalloc(strlen(part->flat_name)
						  + strlen(sep) + strlen(suffix) + 1);
				sprintf(variant, "%s%s%s", part->flat_name, sep, suffix);
				flat_ok = set_part_flat(part->parts_no, variant, 1);
			}
			free(sep);
			free(suffix);
		}
		ACT_DEBUG("[act] %s parts_no=%d pos=(%d,%d,%d) gpos=(%d,%d) show=%d alpha=%d "
			  "mag=(%.2f,%.2f) click=%d flat='%s' set=%d size=%dx%d",
			  part->name, part->parts_no, (int)part->x, (int)part->y,
			  part->z,
			  PE_GetPartsGlobalX(part->parts_no),
			  PE_GetPartsGlobalY(part->parts_no),
			  part->show, part->alpha, part->scale_x,
			  part->scale_y, part->clickable, part->flat_name,
			  flat_ok, PE_GetPartsWidth(part->parts_no, 1),
			  PE_GetPartsHeight(part->parts_no, 1));
		free(variant);
	}

	if (part->uc_object && getenv("XSYS4_USERCOMP_DBG")) {
		char obj[128], par[256];
		snprintf(obj, sizeof obj, "%s", display_sjis0(part->uc_object));
		snprintf(par, sizeof par, "%s",
			 part->uc_params ? display_sjis0(part->uc_params) : "");
		int px = parent_no >= 0 ? PE_GetPartsX(parent_no) : 0;
		int py = parent_no >= 0 ? PE_GetPartsY(parent_no) : 0;
		NOTICE("[act] %s 用户组件 no=%d '%s' params='%s' rel=(%d,%d) "
		       "parent=(%d,%d) abs=(%d,%d) size=%dx%d show=%d alpha=%d di=%d",
		       part->name, part->parts_no, obj, par,
		       PE_GetPartsX(part->parts_no), PE_GetPartsY(part->parts_no),
		       px, py,
		       px + PE_GetPartsX(part->parts_no),
		       py + PE_GetPartsY(part->parts_no),
		       PE_GetPartsWidth(part->parts_no, 1),
		       PE_GetPartsHeight(part->parts_no, 1),
		       part->show, part->alpha, PE_GetDelegateIndex(part->parts_no));
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
	free(part->flat_name);
	free(part->uc_object);
	free(part->uc_params);
	for (int i = 0; i < part->uc_data_n; i++)
		free(part->uc_data[i]);
	free(part->uc_data);
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
	if (getenv("XSYS4_ACT_DBG"))
		NOTICE("[act] release '%s'", name ? name : "(null)");
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

	/* 建树时部件还没有父节点，PE_SetPos() 只能算出 global = local，
	 * 之后父节点是 pending 状态、父链只在 PE_UpdateComponent() 里才挂上，
	 * 而它又会跳过"父节点还脏"的子部件 —— 于是子部件的 global.pos 永远
	 * 停在建树瞬间。渲染与命中都用 global.pos，整棵子树就会画错位置。
	 * PE_RefreshGlobalTree() 只挂父链 + 重算参数，不触发 layout
	 * （layout 会覆盖 EX 给的绝对位置）。详见 parts.c。 */
	PE_RefreshGlobalTree();

	/* 建树后的权威快照：此时父链与 global 参数都已成型，
	 * gpos 就是渲染/命中真正用的坐标。 */
	if (getenv("XSYS4_ACT_DEBUG")) {
		int i;
		for (i = 0; i < act->nr_parts; i++) {
			struct activity_part *p = act->flat[i];
			NOTICE("[act] layout %-14s no=%d gpos=(%d,%d) size=%dx%d "
			       "show=%d alpha=%d click=%d",
			       p->name, p->parts_no,
			       PE_GetPartsGlobalX(p->parts_no),
			       PE_GetPartsGlobalY(p->parts_no),
			       PE_GetPartsWidth(p->parts_no, 1),
			       PE_GetPartsHeight(p->parts_no, 1),
			       PE_GetPartsShow(p->parts_no),
			       PE_GetPartsAlpha(p->parts_no),
			       PE_GetPartsClickable(p->parts_no));
		}
	}

	act->next = activity_list;
	activity_list = act;

	NOTICE("[act] loaded '%s' from '%s' (version %d/%d, %d parts)",
	       act->name, file->text, act->version, act->parts_version, act->nr_parts);
	/* [D128] XSYS4_PARTS_DUMP=1：每次活动装载后拍一张"当前在显示的部件"快照，
	 * 用来确认旧场景的部件是否还活着、挂在哪个 controller 上。 */
	if (getenv("XSYS4_PARTS_DUMP"))
		PE_DebugDumpParts(act->name);
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
