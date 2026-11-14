/* ain_spec.c - reproducible static fact collector for AIN v12 bytecode.
 *
 * Build (from xsystem4 repo root):
 *   gcc -O1 -I subprojects/libsys4/include tools/ain_spec.c \
 *       build/subprojects/libsys4/libsys4.a -o /tmp/ain_spec \
 *       -lm -lz -ldeflate -lpthread
 *
 * Modes:
 *   opcodes                 full opcode frequency + unimplemented/TODO census
 *   xops [max_per_op]       0x10x extension call sites with local disassembly
 *   slots                   variable-slot width / void-placeholder census
 *   types                   function_types / interface / vmethod census
 *   calls <hex-opcode> [n]  all call sites of one opcode
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
static const char *opname(int op) { return instructions[op].name ? instructions[op].name : "?"; }
static int width(int op) { return instruction_width(op); }

static void disasm_at(size_t ip, int count, const char *mark)
{
	for (int i = 0; i < count && ip + 2 <= A->code_size; i++) {
		int raw = r16(A->code + ip);
		int op = raw & ~0xC000;
		if (op < 0 || op >= NR_OPCODES) { printf("  %s 0x%08zx  BAD 0x%04x\n", mark, ip, raw);
			break; }
		int n = instructions[op].nr_args;
		printf("  %s 0x%08zx  0x%03x %-32s", mark, ip, op, opname(op));
		for (int k = 0; k < n && k < 4; k++) printf(" %d", r32(A->code + ip + 2 + k * 4));
		printf("\n");
		int w = width(op);
		if (w < 2) break;
		ip += w;
	}
}

struct rec { size_t ip; int op; int32_t a[4]; int n; };
static struct rec win[8];
static int wn = 0;
static void push_win(struct rec r) { win[wn++ & 7] = r; }
static void print_window(int back)
{
	int start = wn > back ? wn - back : 0;
	for (int i = start; i < wn; i++) {
		struct rec *r = &win[i & 7];
		printf("    -0x%08zx 0x%03x %-30s", r->ip, r->op, opname(r->op));
		for (int k = 0; k < r->n && k < 4; k++) printf(" %d", r->a[k]);
		printf("\n");
	}
}

static void mode_opcodes(void)
{
	long long *cnt = calloc(NR_OPCODES, sizeof(long long));
	long long bad = 0, total = 0;
	size_t ip = 0;
	while (ip + 2 <= A->code_size) {
		int op = r16(A->code + ip) & ~0xC000;
		if (op < 0 || op >= NR_OPCODES) { bad++; ip += 2; continue; }
		int w = width(op);
		if (w < 2 || ip + (size_t)w > A->code_size) { bad++; break; }
		cnt[op]++; total++; ip += w;
	}
	printf("AIN version      : %d\ncode_size        : %zu\ndecoded          : %lld\nbad              : %lld\n\n", A->version, A->code_size, total, bad);
	printf("== unimplemented (TODO) opcodes USED by this AIN\n");
	long long todo_total = 0;
	for (int op = 0; op < NR_OPCODES; op++) {
		if (cnt[op] && !instructions[op].implemented) {
			printf("  TODO 0x%03x %-30s nr_args=%d count=%lld\n", op, opname(op), instructions[op].nr_args, cnt[op]);
			todo_total += cnt[op];
		}
	}
	printf("  TODO total = %lld\n\n== 0x100+ extension opcodes\n", todo_total);
	for (int op = 0x100; op < NR_OPCODES; op++) if (cnt[op])
		printf("  0x%03x %-30s nr_args=%d count=%lld\n", op, opname(op), instructions[op].nr_args, cnt[op]);
	printf("\n== v12-relevant core opcodes\n");
	static const int key[] = { CALLMETHOD, CALLFUNC, CALLHLL, NEW, SR_ASSIGN, R_ASSIGN,
	                           REF, REFREF, ASSIGN, DELETE, SP_INC, DG_PLUSA, PUSHSTRUCTPAGE };
	for (unsigned i = 0; i < sizeof(key)/sizeof(key[0]); i++) {
		int op = key[i];
		printf("  0x%03x %-30s count=%lld\n", op, opname(op), cnt[op]);
	}
	free(cnt);
}

static void mode_xops(int limit)
{
	int *seen = calloc(NR_OPCODES, sizeof(int));
	size_t ip = 0; wn = 0;
	while (ip + 2 <= A->code_size) {
		int raw = r16(A->code + ip);
		int op = raw & ~0xC000;
		if (op < 0 || op >= NR_OPCODES) { ip += 2; continue; }
		int w = width(op);
		if (w < 2 || ip + (size_t)w > A->code_size) break;
		struct rec r = { .ip = ip, .op = op, .n = instructions[op].nr_args };
		for (int k = 0; k < r.n && k < 4; k++) r.a[k] = r32(A->code + ip + 2 + k*4);
		int is_x = (op >= 0x100);
		if (is_x && seen[op] < limit) {
			seen[op]++;
			printf("== 0x%03x %s at 0x%08zx args:", op, opname(op), ip);
			for (int k = 0; k < r.n && k < 4; k++) printf(" %d", r.a[k]);
			printf("\n  -- before --\n");
			print_window(4);
			printf("  -- at/after --\n");
			disasm_at(ip, 5, " ");
		}
		push_win(r);
		ip += w;
	}
	free(seen);
}

static void one_var_table(const char *where, struct ain_variable *v, int n,
                          long long *hist, long long *follow, long long *total)
{
	for (int i = 0; i < n; i++) {
		int t = v[i].type.data;
		if (t >= 0 && t < 256) { hist[t]++; (*total)++; }
		if (i + 1 < n && v[i+1].type.data == AIN_VOID && t >= 0 && t < 256) follow[t]++;
	}
}

static void mode_slots(void)
{
	long long hist[256] = {0}, follow[256] = {0}, total = 0;
	one_var_table("globals", A->globals, A->nr_globals, hist, follow, &total);
	for (int f = 0; f < A->nr_functions; f++)
		one_var_table(A->functions[f].name, A->functions[f].vars, A->functions[f].nr_vars, hist, follow, &total);
	for (int s = 0; s < A->nr_structures; s++)
		one_var_table(A->structures[s].name, A->structures[s].members, A->structures[s].nr_members, hist, follow, &total);
	printf("variable slots total = %lld\n\n%-6s %-12s %-16s %s\n", total, "type", "count", "followed_by_void", "ratio");
	for (int t = 0; t < 256; t++) if (hist[t]) {
		printf("%-6d %-12lld %-16lld %.3f\n", t, hist[t], follow[t], (double)follow[t]/hist[t]);
	}
	printf("\n== AIN_VOID placeholder contexts (first 25)\n");
	int shown = 0;
	for (int f = 0; f < A->nr_functions && shown < 25; f++) {
		struct ain_function *fn = &A->functions[f];
		for (int i = 0; i + 1 < fn->nr_vars && shown < 25; i++) {
			if (fn->vars[i+1].type.data == AIN_VOID) {
				printf("  %s var[%d]=%d(%s) var[%d]=VOID\n", fn->name, i,
				       fn->vars[i].type.data, fn->vars[i].name, i+1);
				shown++;
			}
		}
	}
}

static void mode_types(void)
{
	long long iface_structs = 0, vmethods = 0, iface_entries = 0;
	printf("nr_function_types=%d fnct_size=%d nr_structures=%d nr_globals=%d nr_functions=%d\n",
	       A->nr_function_types, A->fnct_size, A->nr_structures, A->nr_globals, A->nr_functions);
	for (int s = 0; s < A->nr_structures; s++) {
		struct ain_struct *st = &A->structures[s];
		if (st->nr_interfaces) iface_structs++;
		vmethods += st->nr_vmethods;
		iface_entries += st->nr_interfaces;
	}
	printf("structs_with_interfaces=%lld interface_entries=%lld vmethod_entries=%lld\n",
	       iface_structs, iface_entries, vmethods);
	printf("\n== structs with interfaces (first 15)\n");
	int shown = 0;
	for (int s = 0; s < A->nr_structures && shown < 15; s++) {
		struct ain_struct *st = &A->structures[s];
		if (!st->nr_interfaces) continue;
		printf("  struct[%d] %-46s nr_interfaces=%d nr_members=%d ctor=%d\n",
		       s, st->name, st->nr_interfaces, st->nr_members, st->constructor);
		for (int j = 0; j < st->nr_interfaces && j < 5; j++)
			printf("     iface[%d] struct_type=%d vtable_offset=%d\n", j,
			       st->interfaces[j].struct_type, st->interfaces[j].vtable_offset);
		shown++;
	}
}

static void mode_calls(int op, int limit)
{
	long long n = 0;
	size_t ip = 0;
	printf("== call sites of 0x%03x %s (limit %d)\n", op, opname(op), limit);
	while (ip + 2 <= A->code_size) {
		int raw = r16(A->code + ip);
		int o = raw & ~0xC000;
		if (o < 0 || o >= NR_OPCODES) { ip += 2; continue; }
		int w = width(o);
		if (w < 2 || ip + (size_t)w > A->code_size) break;
		if (o == op && (limit < 0 || n < limit)) {
			printf("  0x%08zx", ip);
			for (int k = 0; k < instructions[op].nr_args && k < 4; k++) printf(" %d", r32(A->code + ip + 2 + k*4));
			printf("\n");
		}
		if (o == op) n++;
		ip += w;
	}
	printf("total=%lld\n", n);
}


struct tagkey { int lib, fn, tag; long long n; };
static void mode_hlltags(void)
{
	long long tag_hist[512] = {0};
	struct tagkey *k = calloc(20000, sizeof(struct tagkey));
	int nk = 0;
	size_t ip = 0;
	while (ip + 2 <= A->code_size) {
		int op = r16(A->code + ip) & ~0xC000;
		if (op < 0 || op >= NR_OPCODES) { ip += 2; continue; }
		int w = width(op);
		if (w < 2 || ip + (size_t)w > A->code_size) break;
		if (op == CALLHLL && w >= 14) {
			int lib = r32(A->code + ip + 2);
			int fn  = r32(A->code + ip + 6);
			int tag = r32(A->code + ip + 10);
			if (tag >= -1 && tag < 256) tag_hist[tag + 1]++;
			int i;
			for (i = 0; i < nk; i++)
				if (k[i].lib == lib && k[i].fn == fn && k[i].tag == tag) break;
			if (i == nk && nk < 20000) { k[nk] = (struct tagkey){lib, fn, tag, 0}; nk++; }
			if (i < nk) k[i].n++;
		}
		ip += w;
	}
	printf("CALLHLL sites with 3 immediates; distinct (lib,fn,tag)=%d\n== tag value histogram\n", nk);
	for (int t = -1; t < 256; t++) if (tag_hist[t+1])
		printf("  tag=%-4d count=%lld\n", t, tag_hist[t+1]);
	printf("\n== top (lib,fn,tag) triples\n");
	for (int a = 0; a < nk; a++) {
		int best = a;
		for (int b = a+1; b < nk; b++) if (k[b].n > k[best].n) best = b;
		if (best != a) { struct tagkey t = k[a]; k[a] = k[best]; k[best] = t; }
	}
	for (int i = 0; i < nk && i < 35; i++) {
		const char *ln = k[i].lib >= 0 && k[i].lib < A->nr_libraries && A->libraries[k[i].lib].name ? A->libraries[k[i].lib].name : "?";
		const char *fn = "?";
		if (k[i].lib >= 0 && k[i].lib < A->nr_libraries && k[i].fn >= 0 && k[i].fn < A->libraries[k[i].lib].nr_functions)
			fn = A->libraries[k[i].lib].functions[k[i].fn].name;
		printf("  lib=%-2d %-16s fn=%-4d %-30s tag=%-4d count=%lld\n",
		       k[i].lib, ln, k[i].fn, fn, k[i].tag, k[i].n);
	}
	free(k);
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s <file.ain> opcodes|xops|slots|types|hlltags|calls <hex> [limit]\n", argv[0]); return 1; }
	int err = 0;
	A = ain_open(argv[1], &err);
	if (!A) { fprintf(stderr, "ain_open failed: %d\n", err); return 1; }
	initialize_instructions(A->version);
	const char *mode = argv[2];
	if (!strcmp(mode, "opcodes")) mode_opcodes();
	else if (!strcmp(mode, "xops")) mode_xops(argc > 3 ? atoi(argv[3]) : 3);
	else if (!strcmp(mode, "slots")) mode_slots();
	else if (!strcmp(mode, "types")) mode_types();
	else if (!strcmp(mode, "hlltags")) mode_hlltags();
	else if (!strcmp(mode, "calls")) {
		if (argc < 4) { fprintf(stderr, "calls needs hex opcode\n"); return 1; }
		mode_calls((int)strtol(argv[3], NULL, 0), argc > 4 ? atoi(argv[4]) : 40);
	} else { fprintf(stderr, "unknown mode %s\n", mode); return 1; }
	return 0;
}
