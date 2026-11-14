#!/usr/bin/env python3
# [D110f] 用 libsys4 的 ex_read 把 pactex 打成结构树（env 门控，只 dump 一次）
import io, sys

PATH = "/home/alin/Rance10_Port/xsystem4/src/hll/PartsEngine.c"
src = io.open(PATH, encoding="utf-8").read()

if "D110f" in src:
    print("ALREADY_PATCHED")
    sys.exit(0)


def sub_once(text, old, new, tag):
    n = text.count(old)
    if n != 1:
        print("FAIL[%s]: matched %d" % (tag, n))
        sys.exit(2)
    return text.replace(old, new, 1)


src = sub_once(src, '#include "system4/archive.h"',
                    '#include "system4/archive.h"\n#include "system4/ex.h"', "inc")

helper = r'''/* [D110f] pactex == System4 EX 格式（HEAD/EXTF/DATA + 字节置换表 + zlib）。
 * libsys4 的 ex_read() 已经能完整解析，这里只把结构打出来，
 * 用来确定 Activity 需要哪些字段，再决定数据结构。
 */
static void pe_act_dump_value(struct ex_value *v, int d);

static void pe_act_pad(char *buf, int d)
{
	int n = d * 2;
	if (n > 60)
		n = 60;
	memset(buf, ' ', n);
	buf[n] = 0;
}

static void pe_act_dump_tree(struct ex_tree *t, int d)
{
	char pad[64];
	pe_act_pad(pad, d);
	NOTICE("%s- '%s' leaf=%d", pad, t->name ? t->name->text : "?", t->is_leaf);
	if (t->is_leaf) {
		pe_act_dump_value(&t->leaf.value, d + 1);
	} else if (d < 6) {
		for (uint32_t i = 0; i < t->nr_children && i < 24; i++)
			pe_act_dump_tree(&t->children[i], d + 1);
		if (t->nr_children > 24)
			NOTICE("%s  ... %u children total", pad, t->nr_children);
	} else {
		NOTICE("%s  ... %u children (depth limit)", pad, t->nr_children);
	}
}

static void pe_act_dump_value(struct ex_value *v, int d)
{
	char pad[64];
	pe_act_pad(pad, d);
	switch (v->type) {
	case EX_INT:
		NOTICE("%sint %d", pad, v->i);
		break;
	case EX_FLOAT:
		NOTICE("%sfloat %.4f", pad, v->f);
		break;
	case EX_STRING:
		NOTICE("%sstr '%s'", pad, v->s ? v->s->text : "");
		break;
	case EX_TABLE: {
		struct ex_table *t = v->t;
		NOTICE("%stable %ux%u", pad, t->nr_rows, t->nr_columns);
		for (uint32_t c = 0; c < t->nr_columns && c < 20; c++)
			NOTICE("%s  col[%u] '%s' %s", pad, c,
			       t->fields[c].name ? t->fields[c].name->text : "?",
			       ex_strtype(t->fields[c].type));
		if (d > 5)
			break;
		for (uint32_t r = 0; r < t->nr_rows && r < 6; r++) {
			NOTICE("%s  row[%u]", pad, r);
			for (uint32_t c = 0; c < t->nr_columns && c < 20; c++) {
				struct ex_value *cv = ex_table_get(t, r, c);
				if (cv)
					pe_act_dump_value(cv, d + 2);
			}
		}
		if (t->nr_rows > 6)
			NOTICE("%s  ... %u rows total", pad, t->nr_rows);
		break;
	}
	case EX_LIST: {
		struct ex_list *l = v->list;
		NOTICE("%slist %u", pad, l->nr_items);
		if (d > 5)
			break;
		for (uint32_t i = 0; i < l->nr_items && i < 6; i++) {
			struct ex_value *iv = ex_list_get(l, i);
			if (iv)
				pe_act_dump_value(iv, d + 1);
		}
		if (l->nr_items > 6)
			NOTICE("%s  ... %u items total", pad, l->nr_items);
		break;
	}
	case EX_TREE:
		pe_act_dump_tree(v->tree, d);
		break;
	default:
		NOTICE("%s? unknown type=%d", pad, v->type);
		break;
	}
}

static void pe_act_dump_ex(struct archive_data *ad)
{
	static int done = 0;
	if (done)
		return;
	done = 1;
	struct ex *ex = ex_read((const uint8_t *)ad->data, ad->size);
	if (!ex) {
		NOTICE("[pe-ex] ex_read failed");
		return;
	}
	NOTICE("[pe-ex] '%s' blocks=%u", ad->name ? ad->name : "?", ex->nr_blocks);
	for (uint32_t i = 0; i < ex->nr_blocks; i++) {
		NOTICE("[pe-ex] block[%u] '%s'", i,
		       ex->blocks[i].name ? ex->blocks[i].name->text : "?");
		pe_act_dump_value(&ex->blocks[i].val, 1);
	}
	ex_free(ex);
}

'''
src = sub_once(src, "static int PE_Act_ReadActivityFile(struct string * a0, struct string * a1, int a2)",
                    helper + "static int PE_Act_ReadActivityFile(struct string * a0, struct string * a1, int a2)",
                    "helper")

old = '''\t\t\t\t\t\tNOTICE("[pe-hack]     dumped '%s' size=%d -> /tmp/act_dump.bin",
\t\t\t\t\t\t       ad->name ? ad->name : "(?)", (int)ad->size);'''
new = '''\t\t\t\t\t\tNOTICE("[pe-hack]     dumped '%s' size=%d -> /tmp/act_dump.bin",
\t\t\t\t\t\t       ad->name ? ad->name : "(?)", (int)ad->size);
\t\t\t\t\t\tif (getenv("XSYS4_PE_DUMP_EX"))
\t\t\t\t\t\t\tpe_act_dump_ex(ad);'''
src = sub_once(src, old, new, "hook")

io.open(PATH, "w", encoding="utf-8").write(src)
print("PATCH_OK")
