/* Dump every HLL function's declared argument count, for arity auditing. */
#include <stdio.h>
#include <stdlib.h>
#include <system4/ain.h>

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <file.ain>\n", argv[0]); return 1; }
	int err = 0;
	struct ain *a = ain_open(argv[1], &err);
	if (!a) { fprintf(stderr, "ain_open failed: %d\n", err); return 1; }
	for (int l = 0; l < a->nr_libraries; l++) {
		struct ain_library *lib = &a->libraries[l];
		for (int f = 0; f < lib->nr_functions; f++) {
			printf("%d\t%s\t%d\t%s\t%d\n", l, lib->name, f,
			       lib->functions[f].name,
			       lib->functions[f].nr_arguments);
		}
	}
	return 0;
}
