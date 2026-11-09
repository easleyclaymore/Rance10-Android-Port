/* ain_locals.c - compare AIN function nr_vars with the local-page indices
 * actually used by the bytecode.
 *
 * Motivation (D34): debug::detail::CDebugMemorySize@0 declares nr_vars=4 while
 * its body reads/writes local[4..10], corrupting the heap. This tool checks
 * whether that is an isolated case or a systematic v12 slot-width issue.
 *
 * Build (from the xsystem4 repo root):
 *   gcc -O1 -I subprojects/libsys4/include tools/ain_locals.c \
 *       build/subprojects/libsys4/libsys4.a -o build/ain_locals \
 *       -lm -lz -ldeflate -lpthread
 * Usage:
 *   build/ain_locals <file.ain> [fno_to_detail]
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

struct ins { uint32_t ip; uint16_t op; int32_t a0; };
static struct ins *code;
static size_t ncode, cap;

struct fref { uint32_t addr; int fno; };
static struct fref *frefs;
static int nfrefs;

static int cmp_fref(const void *a, const void *b)
{
	const struct fref *x = a, *y = b;
	return x->addr < y->addr ? -1 : x->addr > y->addr ? 1 : 0;
}

static int fno_at(uint32_t ip)
{
	int lo = 0, hi = nfrefs - 1, best = -1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (frefs[mid].addr <= ip) { best = mid; lo = mid + 1; }
		else hi = mid - 1;
	}
	return best < 0 ? -1 : frefs[best].fno;
}

static void decode(void)
{
	size_t ip = 0;
	while (ip + 2 <= A->code_size) {
		int op = r16(A->code + ip) & ~0xC000;
		if (op < 0 || op >= NR_OPCODES) { ip += 2; continue; }
		int w = instruction_width(op);
		if (w < 2) break;
		if (ncode == cap) {
			cap = cap ? cap * 2 : 1 << 20;
			code = realloc(code, cap * sizeof(*code));
			if (!code) { fprintf(stderr, "oom\n"); exit(1); }
		}
		code[ncode].ip = (uint32_t)ip;
		code[ncode].op = (uint16_t)op;
		code[ncode].a0 = instructions[op].nr_args ? r32(A->code + ip + 2) : 0;
		ncode++;
		ip += (size_t)w;
	}
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <file.ain> [fno]\n", argv[0]); return 1; }
	int err = 0;
	A = ain_open(argv[1], &err);
	if (!A) { fprintf(stderr, "ain_open failed: %d\n", err); return 1; }
	initialize_instructions(A->version);
	decode();
	printf("AIN v%d decoded=%zu functions=%d\n", A->version, ncode, A->nr_functions);

	frefs = calloc(A->nr_functions, sizeof(*frefs));
	for (int i = 0; i < A->nr_functions; i++) {
		if (A->functions[i].address <= 0) continue;
		frefs[nfrefs].addr = (uint32_t)A->functions[i].address;
		frefs[nfrefs].fno = i;
		nfrefs++;
	}
	qsort(frefs, nfrefs, sizeof(*frefs), cmp_fref);

	int32_t *maxlocal = malloc(A->nr_functions * sizeof(*maxlocal));
	int32_t *maxstruct = malloc(A->nr_functions * sizeof(*maxstruct));
	for (int i = 0; i < A->nr_functions; i++) { maxlocal[i] = -1; maxstruct[i] = -1; }

	for (size_t i = 0; i + 1 < ncode; i++) {
		struct ins *p = &code[i];
		struct ins *q = &code[i + 1];
		if (q->op != PUSH || q->a0 < 0 || q->a0 > 4096) continue;
		int fno = fno_at(p->ip);
		if (fno < 0) continue;
		if (p->op == PUSHLOCALPAGE && q->a0 > maxlocal[fno]) maxlocal[fno] = q->a0;
		if (p->op == PUSHSTRUCTPAGE && q->a0 > maxstruct[fno]) maxstruct[fno] = q->a0;
	}

	int with_refs = 0, exceeding = 0, worst = 0, worst_fno = -1;
	long long excess_sum = 0;
	for (int i = 0; i < A->nr_functions; i++) {
		if (maxlocal[i] < 0) continue;
		with_refs++;
		int excess = maxlocal[i] - (A->functions[i].nr_vars - 1);
		if (excess > 0) {
			exceeding++;
			excess_sum += excess;
			if (excess > worst) { worst = excess; worst_fno = i; }
		}
	}
	printf("functions_with_local_refs=%d exceeding_nr_vars=%d worst_excess=%d (fno=%d %s)\n",
	       with_refs, exceeding, worst, worst_fno,
	       worst_fno >= 0 && A->functions[worst_fno].name
	         ? A->functions[worst_fno].name : "?");
	printf("mean_excess_over_exceeding=%.2f\n",
	       exceeding ? (double)excess_sum / exceeding : 0.0);

	printf("\n== top 20 exceedances ==\n");
	int shown = 0;
	for (int i = 0; i < A->nr_functions && shown < 20; i++) {
		if (maxlocal[i] < 0) continue;
		int excess = maxlocal[i] - (A->functions[i].nr_vars - 1);
		if (excess <= 0) continue;
		printf("  fno=%-6d nr_vars=%-5d maxlocal=%-5d excess=%-5d nr_args=%d %s\n",
		       i, A->functions[i].nr_vars, maxlocal[i], excess,
		       A->functions[i].nr_args,
		       A->functions[i].name ? A->functions[i].name : "?");
		shown++;
	}

	int detail = argc > 2 ? atoi(argv[2]) : 3065;
	if (detail >= 0 && detail < A->nr_functions) {
		printf("\n== detail fno=%d ==\n", detail);
		printf("  name=%s addr=0x%x nr_args=%d nr_vars=%d maxlocal=%d maxstruct=%d\n",
		       A->functions[detail].name ? A->functions[detail].name : "?",
		       A->functions[detail].address, A->functions[detail].nr_args,
		       A->functions[detail].nr_vars, maxlocal[detail], maxstruct[detail]);
		for (int v = 0; v < A->functions[detail].nr_vars; v++)
			printf("    var[%d] type=%d struc=%d %s\n", v,
			       A->functions[detail].vars[v].type.data,
			       A->functions[detail].vars[v].type.struc,
			       A->functions[detail].vars[v].name
			         ? A->functions[detail].vars[v].name : "");
	}
	return 0;
}
