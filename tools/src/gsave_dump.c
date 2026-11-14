/* gsave_dump -- offline inspector for the .asd save files this engine writes.
 *
 * Why it exists: the save subsystem is the one place where a bug is silent.
 * A wrong array rank or an out-of-range table index does not crash at write
 * time; it produces a file that the next run either rejects ("Invalid save
 * file") or loads as garbage. Reading the file with the SAME parser the
 * engine uses, but outside the game loop, turns that into a two-second check.
 *
 * Build (from xsystem4/):
 *   cc -O1 -I subprojects/libsys4/include tools/gsave_dump.c \
 *      build/subprojects/libsys4/libsys4.a -o /tmp/gsave_dump \
 *      -lz -lm -ldeflate
 *
 * Use:
 *   /tmp/gsave_dump logs/savedir/<tag>/<file>.asd
 *
 * Read the output as:
 *   - "OK" / "Invalid save file" per file        -> parser agrees with writer
 *   - every array line rank>=1 with plausible dims -> generic arrays serialised
 *   - no "ARRAY-INDEX-OOB" markers                -> global indices in range
 *
 * With -a it also prints every global, which is how you spot a value that
 * points at the wrong table entry.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <system4/savefile.h>

static bool is_array_type(enum ain_data_type t)
{
	/* Do NOT use a numeric range: the array ids are scattered (14..17,
	 * 30, 50, 58, 66, 79) and e.g. AIN_BOOL=47 sits between them. */
	switch (t) {
	case AIN_ARRAY_INT:
	case AIN_ARRAY_FLOAT:
	case AIN_ARRAY_STRING:
	case AIN_ARRAY_STRUCT:
	case AIN_ARRAY_FUNC_TYPE:
	case AIN_ARRAY_BOOL:
	case AIN_ARRAY_LONG_INT:
	case AIN_ARRAY_DELEGATE:
	case AIN_ARRAY:            /* v12 generic `array` */
		return true;
	default:
		return false;
	}
}

int main(int argc, char **argv)
{
	bool all = false;
	int first = 1;
	if (argc > 1 && !strcmp(argv[1], "-a")) {
		all = true;
		first = 2;
	}
	if (first >= argc) {
		fprintf(stderr, "usage: %s [-a] file.asd...\n", argv[0]);
		return 2;
	}

	for (int i = first; i < argc; i++) {
		enum savefile_error err = 0;
		struct gsave *g = gsave_read(argv[i], &err);
		printf("== %s: %s\n", argv[i], err ? savefile_strerror(err) : "OK");
		if (!g)
			continue;

		printf("   globals=%d arrays=%d records=%d strings=%d keyvals=%d group=%s\n",
		       g->nr_globals, g->nr_arrays, g->nr_records, g->nr_strings,
		       g->nr_keyvals, g->group ? g->group : "");

		for (int k = 0; k < g->nr_globals; k++) {
			struct gsave_global *gl = &g->globals[k];
			const char *flag = "";
			if (is_array_type(gl->type) &&
			    !(gl->value >= 0 && gl->value < g->nr_arrays))
				flag = "  <-- ARRAY-INDEX-OOB";
			if (all || *flag)
				printf("   g[%d] %-44s type=%-3d value=%d%s\n", k,
				       gl->name ? gl->name : "?", gl->type, gl->value, flag);
		}

		for (int k = 0; k < g->nr_arrays; k++) {
			struct gsave_array *a = &g->arrays[k];
			printf("   a[%d] rank=%d flats=%d dims=", k, a->rank, a->nr_flat_arrays);
			if (a->rank > 0)
				for (int d = 0; d < a->rank; d++)
					printf("%d,", a->dimensions[d]);
			printf(" elem=");
			for (int f = 0; f < a->nr_flat_arrays && f < 4; f++)
				printf("%d(n=%d) ", a->flat_arrays[f].type,
				       a->flat_arrays[f].nr_values);
			printf("\n");
		}
		gsave_free(g);
	}
	return 0;
}
