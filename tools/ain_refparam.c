/* ain_refparam.c - census of AIN_REF_HLL_PARAM (t75) CALLHLL call sites.
 *
 * Static evidence for D31: how the compiler consumes a generic ref-param
 * result, grouped by the CALLHLL third immediate (call-site value type).
 *
 * Build (from xsystem4 repo root):
 *   gcc -O1 -I subprojects/libsys4/include tools/ain_refparam.c \
 *       build/subprojects/libsys4/libsys4.a -o build/ain_refparam \
 *       -lm -lz -ldeflate -lpthread
 * Usage:
 *   build/ain_refparam <file.ain> [samples_per_tag]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <system4/ain.h>
#include <system4/instructions.h>

static struct ain *A;
static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int32_t r32(const uint8_t *p) {
	return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

#define MAX_TAG 256
#define WINDOW 14
struct tally { long long total, refpair, method, assign, other; int samples; };
static struct tally T[MAX_TAG];

static const char *classify(const int *ops, int n)
{
	int has_rassign = 0, has_method = 0, has_assign = 0;
	for (int i = 0; i < n; i++) {
		if (ops[i] == R_ASSIGN) has_rassign = 1;
		if (ops[i] == CALLMETHOD) has_method = 1;
		if (ops[i] == ASSIGN || ops[i] == F_ASSIGN) has_assign = 1;
	}
	if (has_rassign) return "refpair";   /* two-value [page,var] reference */
	if (has_method)  return "method";    /* result used as CALLMETHOD this */
	if (has_assign)  return "assign";    /* result written through ASSIGN */
	return "other";
}

static void dump_window(size_t ip, int n)
{
	size_t p = ip;
	for (int i = 0; i < n && p + 2 <= A->code_size; i++) {
		int op = r16(A->code + p) & ~0xC000;
		if (op < 0 || op >= NR_OPCODES) break;
		int na = instructions[op].nr_args;
		printf("      0x%08zx 0x%03x %-28s", p, op,
		       instructions[op].name ? instructions[op].name : "?");
		for (int k = 0; k < na && k < 4; k++) printf(" %d", r32(A->code + p + 2 + k * 4));
		printf("\n");
		int w = instruction_width(op);
		if (w < 2) break;
		p += (size_t)w;
	}
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <file.ain> [samples_per_tag]\n", argv[0]); return 1; }
	int samples = argc > 2 ? atoi(argv[2]) : 2;
	int err = 0;
	A = ain_open(argv[1], &err);
	if (!A) { fprintf(stderr, "ain_open failed %d\n", err); return 1; }
	initialize_instructions(A->version);
	printf("AIN version=%d code_size=%zu libraries=%d\n", A->version, A->code_size, A->nr_libraries);
	printf("== CALLHLL sites whose AIN declaration returns AIN_REF_HLL_PARAM (t75)\n");

	long long sites = 0, skipped = 0;
	size_t ip = 0;
	while (ip + 2 <= A->code_size) {
		int op = r16(A->code + ip) & ~0xC000;
		if (op < 0 || op >= NR_OPCODES) { ip += 2; continue; }
		int w = instruction_width(op);
		if (w < 2 || ip + (size_t)w > A->code_size) break;
		if (op == CALLHLL) {
			int na = instructions[op].nr_args;
			if (na >= 3) {
				int lib = r32(A->code + ip + 2);
				int fno = r32(A->code + ip + 6);
				int tag = r32(A->code + ip + 10);
				if (lib >= 0 && lib < A->nr_libraries &&
				    fno >= 0 && fno < A->libraries[lib].nr_functions &&
				    A->libraries[lib].functions[fno].return_type.data == AIN_REF_HLL_PARAM) {
					sites++;
					int ti = tag >= -1 && tag < MAX_TAG - 1 ? tag + 1 : MAX_TAG - 1;
					int ops[WINDOW];
					int n = 0;
					size_t p = ip + (size_t)w;
					while (n < WINDOW && p + 2 <= A->code_size) {
						int o = r16(A->code + p) & ~0xC000;
						if (o < 0 || o >= NR_OPCODES) break;
						ops[n++] = o;
						int ow = instruction_width(o);
						if (ow < 2) break;
						p += (size_t)ow;
					}
					const char *kind = classify(ops, n);
					struct tally *t = &T[ti];
					t->total++;
					if (!strcmp(kind, "refpair")) t->refpair++;
					else if (!strcmp(kind, "method")) t->method++;
					else if (!strcmp(kind, "assign")) t->assign++;
					else t->other++;
					if (t->samples < samples) {
						t->samples++;
						printf("  [%s] tag=%d lib=%d(%s) fno=%d(%s) at 0x%08zx\n",
						       kind, tag, lib, A->libraries[lib].name,
						       fno, A->libraries[lib].functions[fno].name, ip);
						dump_window(ip + (size_t)w, WINDOW);
					}
				}
			} else skipped++;
		}
		ip += (size_t)w;
	}
	printf("\n== summary by call-site tag (next %d instructions)\n", WINDOW);
	printf("%6s %9s %9s %9s %9s %9s\n", "tag", "total", "refpair", "method", "assign", "other");
	for (int i = 0; i < MAX_TAG; i++) if (T[i].total)
		printf("%6d %9lld %9lld %9lld %9lld %9lld\n", i - 1, T[i].total,
		       T[i].refpair, T[i].method, T[i].assign, T[i].other);
	printf("\nsites=%lld skipped_untyped_or_short=%lld\n", sites, skipped);
	return 0;
}
