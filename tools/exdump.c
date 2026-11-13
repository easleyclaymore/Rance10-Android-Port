/* exdump: print a System4 .ex / .pactex structure tree using libsys4's ex_read().
 *
 *   exdump <file.pactex> [max-depth]
 *
 * Used to reverse-engineer activity files field by field. Diagnostic tool only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <system4/ex.h>
#include <system4/string.h>
#include <system4/utfsjis.h>

static int depth_max = 12;
static int lines = 0;

/* Rance10 ships a GBK (Chinese) build: EX strings are GBK bytes, like the
 * rest of the engine's internal strings. use_gbk makes sjis2utf do GBK->UTF-8. */
static const char *U(struct string *s)
{
	static char buf[8][2048];
	static int k;
	if (!s) return "(null)";
	char *b = buf[(k++) & 7];
	char *u = sjis2utf(s->text, strlen(s->text));
	snprintf(b, 2048, "%s", u ? u : s->text);
	free(u);
	return b;
}

static void indent(int d)
{
	for (int i = 0; i < d; i++) fputs("  ", stdout);
}

static void dump_value(struct ex_value *v, int d);

static void dump_table(struct ex_table *t, int d)
{
	indent(d);
	printf("table %ux%u fields=%u\n", t->nr_columns, t->nr_rows, t->nr_fields);
	for (unsigned f = 0; f < t->nr_fields; f++) {
		indent(d + 1);
		printf("[col %u] %s : %s%s\n", f, U(t->fields[f].name),
		       ex_strtype(t->fields[f].type),
		       t->fields[f].is_index ? " (index)" : "");
	}
	for (unsigned r = 0; r < t->nr_rows && r < 200; r++) {
		indent(d + 1);
		printf("row %u:", r);
		for (unsigned c = 0; c < t->nr_columns; c++) {
			struct ex_value *v = ex_table_get(t, r, c);
			if (!v) { printf(" [null]"); continue; }
			if (v->type == EX_INT) printf(" %d", v->i);
			else if (v->type == EX_FLOAT) printf(" %g", v->f);
			else if (v->type == EX_STRING) printf(" \"%s\"", U(v->s));
			else printf(" <%s>", ex_strtype(v->type));
		}
		printf("\n");
	}
}

static void dump_list(struct ex_list *l, int d)
{
	indent(d);
	printf("list n=%u\n", l->nr_items);
	for (unsigned i = 0; i < l->nr_items && i < 400; i++) {
		struct ex_value *v = &l->items[i].value;
		indent(d + 1);
		switch (v->type) {
		case EX_INT: printf("int %d\n", v->i); break;
		case EX_FLOAT: printf("float %g\n", v->f); break;
		case EX_STRING: printf("str \"%s\"\n", U(v->s)); break;
		default:
			printf("<%s>\n", ex_strtype(v->type));
			dump_value(v, d + 1);
			break;
		}
	}
}

static void dump_tree(struct ex_tree *t, int d)
{
	indent(d);
	if (t->is_leaf) {
		printf("leaf \"%s\"", U(t->name));
		if (t->leaf.value.type == EX_STRING)
			printf(" = \"%s\"\n", U(t->leaf.value.s));
		else if (t->leaf.value.type == EX_INT)
			printf(" = %d\n", t->leaf.value.i);
		else if (t->leaf.value.type == EX_FLOAT)
			printf(" = %g\n", t->leaf.value.f);
		else {
			printf(" = <%s>\n", ex_strtype(t->leaf.value.type));
			dump_value(&t->leaf.value, d + 1);
		}
	} else {
		printf("node \"%s\" children=%u\n", U(t->name), t->nr_children);
		for (unsigned i = 0; i < t->nr_children; i++)
			dump_tree(&t->children[i], d + 1);
	}
}

static void dump_value(struct ex_value *v, int d)
{
	switch (v->type) {
	case EX_INT: indent(d); printf("%d\n", v->i); break;
	case EX_FLOAT: indent(d); printf("%g\n", v->f); break;
	case EX_STRING: indent(d); printf("\"%s\"\n", U(v->s)); break;
	case EX_TABLE: dump_table(v->t, d); break;
	case EX_LIST: dump_list(v->list, d); break;
	case EX_TREE: dump_tree(v->tree, d); break;
	default: indent(d); printf("<%s>\n", ex_strtype(v->type)); break;
	}
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <file.pactex> [max-depth]\n", argv[0]);
		return 1;
	}
	if (argc > 2) depth_max = atoi(argv[2]);
	use_gbk = true;
	struct ex *ex = ex_read_file(argv[1]);
	if (!ex) { fprintf(stderr, "ex_read_file failed\n"); return 1; }
	printf("nr_blocks=%u\n", ex->nr_blocks);
	for (unsigned i = 0; i < ex->nr_blocks; i++) {
		printf("\n=== block %u \"%s\" type=%s size=%zu ===\n", i,
		       U(ex->blocks[i].name), ex_strtype(ex->blocks[i].val.type),
		       ex->blocks[i].size);
		dump_value(&ex->blocks[i].val, 0);
	}
	return 0;
}
