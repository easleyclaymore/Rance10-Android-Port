/* ain_icast.c - census of X_ICAST (0x105) call sites and AIN interface metadata.
 *
 * Static evidence for D32/D28: what the X_ICAST operand is, how the following
 * bytecode consumes the result, and what vtable_offset values the AIN carries.
 *
 * Build (from the xsystem4 repo root):
 *   gcc -O1 -I subprojects/libsys4/include tools/ain_icast.c \
 *       build/subprojects/libsys4/libsys4.a -o build/ain_icast \
 *       -lm -lz -ldeflate -lpthread
 * Usage:
 *   build/ain_icast <file.ain> [sample_sites]
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
static const char *opname(int op) {
	return (op >= 0 && op < NR_OPCODES && instructions[op].name)
		? instructions[op].name : "?";
}

struct ins { size_t ip; int op; int32_t a0; };
static struct ins *code = NULL;
static size_t ncode = 0, cap = 0;

static void append_ins(size_t ip, int op, int32_t a0)
{
	if (ncode == cap) {
		cap = cap ? cap * 2 : 1 << 20;
		code = realloc(code, cap * sizeof(*code));
		if (!code) { fprintf(stderr, "oom\n"); exit(1); }
	}
	code[ncode].ip = ip;
	code[ncode].op = op;
	code[ncode].a0 = a0;
	ncode++;
}

static void decode(void)
{
	size_t ip = 0;
	while (ip + 2 <= A->code_size) {
		int op = r16(A->code + ip) & ~0xC000;
		if (op < 0 || op >= NR_OPCODES) { ip += 2; continue; }
		int w = instruction_width(op);
		if (w < 2) break;
		append_ins(ip, op, instructions[op].nr_args ? r32(A->code + ip + 2) : 0);
		ip += (size_t)w;
	}
}

static void print_ins(const struct ins *p, const char *mark)
{
	printf("   %s 0x%08zx 0x%03x %-28s", mark, p->ip, p->op, opname(p->op));
	if (instructions[p->op].nr_args) printf(" %d", p->a0);
	printf("\n");
}

static void mode_interfaces(void)
{
	long long hist[64];
	memset(hist, 0, sizeof(hist));
	int total = 0, oor = 0;
	printf("== structs that implement interfaces ==\n");
	for (int s = 0; s < A->nr_structures; s++) {
		struct ain_struct *st = &A->structures[s];
		if (!st->nr_interfaces) continue;
		printf("  struct[%d] %-48s nr_interfaces=%d\n", s,
		       st->name ? st->name : "?", st->nr_interfaces);
		for (int j = 0; j < st->nr_interfaces; j++) {
			int it = st->interfaces[j].struct_type;
			int vo = st->interfaces[j].vtable_offset;
			printf("      -> iface %d (%-28s) vtable_offset=%d\n", it,
			       it >= 0 && it < A->nr_structures && A->structures[it].name
			         ? A->structures[it].name : "?", vo);
			total++;
			if (vo >= 0 && vo < 64) hist[vo]++; else oor++;
		}
	}
	printf("== vtable_offset histogram (entries=%d out_of_range=%d) ==\n", total, oor);
	for (int i = 0; i < 64; i++) if (hist[i]) printf("  offset %2d : %lld\n", i, hist[i]);
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <file.ain> [sample_sites]\n", argv[0]); return 1; }
	int err = 0;
	A = ain_open(argv[1], &err);
	if (!A) { fprintf(stderr, "ain_open failed: %d\n", err); return 1; }
	initialize_instructions(A->version);
	decode();
	printf("AIN version=%d decoded_instructions=%zu\n", A->version, ncode);
	mode_interfaces();

	int samples = argc > 2 ? atoi(argv[2]) : 8;
	long long operand[2048];
	memset(operand, 0, sizeof(operand));
	long long klass[8];
	memset(klass, 0, sizeof(klass));
	int sites = 0, shown = 0;
	for (size_t i = 0; i < ncode; i++) {
		if (code[i].op != X_ICAST) continue;
		sites++;
		int iface = code[i].a0;
		if (iface >= 0 && iface < 2048) operand[iface]++;

		/* classify by how the following 12 instructions consume the result */
		int has_pop = 0, has_cmpm1 = 0, has_return = 0, has_rassign = 0;
		for (size_t j = i + 1; j < ncode && j <= i + 12; j++) {
			if (code[j].op == POP) has_pop = 1;
			if (code[j].op == EQUALE && j > i + 1 && code[j-1].op == PUSH
			    && code[j-1].a0 == -1) has_cmpm1 = 1;
			if (code[j].op == RETURN) has_return = 1;
			if (code[j].op == R_ASSIGN) has_rassign = 1;
		}
		if (has_cmpm1) klass[0]++;
		else if (has_return) klass[1]++;
		else if (has_pop) klass[2]++;
		else if (has_rassign) klass[3]++;
		else klass[4]++;

		if (shown < samples) {
			printf("\n== X_ICAST site 0x%08zx operand=%d (%s) ==\n", code[i].ip, iface,
			       iface >= 0 && iface < A->nr_structures && A->structures[iface].name
			         ? A->structures[iface].name : "?");
			for (size_t j = i > 8 ? i - 8 : 0; j < ncode && j <= i + 12; j++)
				print_ins(&code[j], j == i ? ">>" : "  ");
			shown++;
		}
	}
	printf("\n== X_ICAST operand histogram ==\n");
	for (int i = 0; i < 2048; i++) if (operand[i])
		printf("  iface %4d (%-34s is_interface=%d) : %lld\n", i,
		       i < A->nr_structures && A->structures[i].name ? A->structures[i].name : "?",
		       i < A->nr_structures ? A->structures[i].is_interface : -1, operand[i]);
	printf("== call-site classes ==\n");
	printf("  compare-with--1 : %lld\n  followed-by-RETURN : %lld\n  POP discarded : %lld\n"
	       "  R_ASSIGN : %lld\n  other : %lld\n  total_sites : %d\n",
	       klass[0], klass[1], klass[2], klass[3], klass[4], sites);
	return 0;
}
