/* ex_dump.c —— 把一个 System4 EX 文件（.pactex 解密后的明文 / .ex）的
 * 结构打出来：块数、每块类型、标量值、列表/表格的长度与元素。
 *
 * 用途：活动文件（activity）的元数据块没有固定块序，块数也会变
 * （4/5/6 块）。`src/parts/activity.c` 只按类型认 INT/STRING，
 * 其余块被静默忽略——「结束键（EndKey）列表」就是这样丢的。
 * 这个工具用来回答「那个块到底在第几块、是什么类型、内容是什么」。
 *
 * 编译（与 flat_talt_dump 同一套）：
 *   gcc -O1 -I subprojects/libsys4/include -o /tmp/exdump \
 *       tools/offline/ex_dump.c build-sdl3/subprojects/libsys4/libsys4.a \
 *       -ldeflate -lz -lm
 * 用法：
 *   /tmp/exdump <file.ex>            # 全部块
 *   /tmp/exdump <file.ex> --ints     # 只打整数标量与整数列表
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

static void print_value(struct ex_value *v, int depth);

static void indent(int depth)
{
	for (int i = 0; i < depth; i++)
		fputs("  ", stdout);
}

static const char *type_name(enum ex_value_type t)
{
	switch (t) {
	case EX_INT:    return "INT";
	case EX_FLOAT:  return "FLOAT";
	case EX_STRING: return "STRING";
	case EX_TABLE:  return "TABLE";
	case EX_LIST:   return "LIST";
	case EX_TREE:   return "TREE";
	}
	return "?";
}

/* EX_TREE 的节点：叶子有 (name, value)，非叶子有 children。 */
static void print_tree(struct ex_tree *t, int depth)
{
	if (t->is_leaf) {
		indent(depth);
		printf("leaf '%.60s' ", t->leaf.name ? t->leaf.name->text : "");
		print_value(&t->leaf.value, 0);
		putchar('\n');
		return;
	}
	for (uint32_t i = 0; i < t->nr_children; i++)
		print_tree(&t->children[i], depth);
}

static void print_value(struct ex_value *v, int depth)
{
	switch (v->type) {
	case EX_INT:    printf("= %d\n", v->i); break;
	case EX_FLOAT:  printf("= %f\n", v->f); break;
	case EX_STRING: printf("= \"%.80s\"\n", v->s ? v->s->text : ""); break;
	case EX_LIST: {
		printf("list[%u]\n", v->list->nr_items);
		for (uint32_t i = 0; i < v->list->nr_items && i < 64; i++) {
			indent(depth + 1);
			printf("[%u] ", i);
			print_value(&v->list->items[i].value, depth + 1);
		}
		break;
	}
	case EX_TABLE: {
		struct ex_table *t = v->t;
		printf("table %ux%u\n", t->nr_columns, t->nr_rows);
		for (uint32_t r = 0; r < t->nr_rows && r < 64; r++) {
			indent(depth + 1);
			printf("row %u:", r);
			for (uint32_t c = 0; c < t->nr_columns; c++) {
				struct ex_value *cv = &t->rows[r][c];
				if (cv->type == EX_INT)
					printf(" %d", cv->i);
				else if (cv->type == EX_STRING)
					printf(" \"%.24s\"", cv->s ? cv->s->text : "");
				else
					printf(" <%s>", type_name(cv->type));
			}
			putchar('\n');
		}
		break;
	}
	case EX_TREE:
		printf("tree\n");
		print_tree(v->tree, depth + 1);
		break;
	}
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "用法: %s <file.ex> [--ints]\n", argv[0]);
		return 2;
	}
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = malloc(sz);
	if (fread(buf, 1, sz, f) != (size_t)sz) { fclose(f); return 1; }
	fclose(f);

	struct ex *ex = ex_read(buf, sz);
	if (!ex) {
		fprintf(stderr, "ex_read 失败\n");
		return 1;
	}
	printf("nr_blocks = %u\n", ex->nr_blocks);
	for (uint32_t i = 0; i < ex->nr_blocks; i++) {
		struct ex_value *v = &ex->blocks[i].val;
		printf("block[%u] %s ", i, type_name(v->type));
		print_value(v, 1);
	}
	ex_free(ex);
	free(buf);
	return 0;
}
