/* 按 fno 顺序转储 AIN HLL 库的函数签名。
 *
 * 构建（在 xsystem4 仓库根目录）：
 *   gcc -O1 -I subprojects/libsys4/include tools/hllsig.c \
 *       build/subprojects/libsys4/libsys4.a -o /tmp/hllsig \
 *       -lm -lz -ldeflate -lpthread
 * 用法：
 *   hllsig <file.ain> <LibraryName> [FunctionName]
 *
 * HLL_LIBRARY 表是按 fno 索引的，不是按名字；用它来核对
 * 某张表是否仍然和链接的 AIN 匹配。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <system4/ain.h>

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <file.ain> <library> [function]\n", argv[0]);
		return 1;
	}
	int err = 0;
	struct ain *a = ain_open(argv[1], &err);
	if (!a) { fprintf(stderr, "ain_open failed: %d\n", err); return 1; }
	const char *lib = argv[2], *fn = argc > 3 ? argv[3] : NULL;
	for (int i = 0; i < a->nr_libraries; i++) {
		struct ain_library *L = &a->libraries[i];
		if (!L->name || strcmp(L->name, lib)) continue;
		for (int j = 0; j < L->nr_functions; j++) {
			struct ain_hll_function *f = &L->functions[j];
			if (fn && (!f->name || strcmp(f->name, fn))) continue;
			printf("f[%d] %s(", j, f->name ? f->name : "?");
			for (int k = 0; k < f->nr_arguments; k++)
				printf("%st%d/s%d", k ? ", " : "",
				       f->arguments[k].type.data, f->arguments[k].type.struc);
			printf(") ret=t%d/s%d\n", f->return_type.data, f->return_type.struc);
			if (fn) return 0;
		}
	}
	return 0;
}
