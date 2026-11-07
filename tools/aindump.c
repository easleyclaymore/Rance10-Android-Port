#include <stdio.h>
#include <string.h>
#include <system4/ain.h>

static const char *tyname(int t)
{
	switch (t) {
	case 0:  return "void";
	case 10: return "int";
	case 11: return "float";
	case 12: return "string";
	case 13: return "struct";
	case 14: return "int[]";
	case 15: return "float[]";
	case 16: return "string[]";
	case 17: return "struct[]";
	case 18: return "ref int";
	case 19: return "ref float";
	case 20: return "ref string";
	case 21: return "ref struct";
	case 22: return "ref int[]";
	case 23: return "ref float[]";
	case 24: return "ref string[]";
	case 25: return "ref struct[]";
	case 26: return "imain_system";
	case 27: return "func_type";
	case 30: return "func_type[]";
	case 31: return "ref func_type";
	case 32: return "ref func_type[]";
	case 47: return "bool";
	case 50: return "bool[]";
	case 51: return "ref bool";
	case 52: return "ref bool[]";
	case 55: return "long";
	case 58: return "long[]";
	case 63: return "ref long";
	default: {
		static char buf[24];
		snprintf(buf, sizeof(buf), "t%d", t);
		return buf;
	}
	}
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <file.ain>\n", argv[0]);
		return 1;
	}

	int err = 0;
	struct ain *ain = ain_open(argv[1], &err);
	if (!ain) {
		fprintf(stderr, "ain_open failed: error=%d\n", err);
		return 1;
	}

	const char *targets[] = {
		"system", "ADVEngine", "Delegate", "EXWriter", "FileDialog", NULL
	};

	for (int i = 0; i < ain->nr_libraries; i++) {
		struct ain_library *lib = &ain->libraries[i];
		if (!lib->name)
			continue;
		int wanted = 0;
		for (int t = 0; targets[t]; t++) {
			if (!strcmp(lib->name, targets[t])) {
				wanted = 1;
				break;
			}
		}
		if (!wanted)
			continue;

		printf("\n### %s  (%d functions)\n", lib->name, lib->nr_functions);
		for (int j = 0; j < lib->nr_functions; j++) {
			struct ain_hll_function *f = &lib->functions[j];
			printf("  %s(", f->name ? f->name : "(null)");
			for (int a = 0; a < f->nr_arguments; a++) {
				struct ain_hll_argument *arg = &f->arguments[a];
				if (a) printf(", ");
				printf("%s", tyname(arg->type.data));
			}
			printf(") -> %s\n", tyname(f->return_type.data));
		}
	}
	return 0;
}
