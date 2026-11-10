/* ain_xref: cross-reference helper for Rance10 AIN research.
 *
 *   ain_xref <file.ain> hll <libno> <fno> [ctx]   CALLHLL sites for one HLL function
 *   ain_xref <file.ain> calls <opcode> [ctx]      any opcode sites (e.g. 0x103 X_GETENV)
 *   ain_xref <file.ain> str <str-index> [ctx]     S_PUSH sites for a string constant
 *   ain_xref <file.ain> funcs <substr>            list functions whose name matches
 *
 * Every site prints: enclosing function (fno/name/addr), the site address and a
 * decoded window around it. ctx = instructions before/after (default 4).
 *
 * Diagnostic tool only; link against libsys4.a like the other tools/ helpers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <system4/ain.h>
#include <system4/instructions.h>
#include <system4/string.h>
#include <system4/utfsjis.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int32_t rd32(const uint8_t *p)
{
	return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
			 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

struct dec { size_t ip; int op; int a[3]; int w; };

static int decode(struct ain *ain, size_t ip, struct dec *d)
{
	if (ip + 2 > ain->code_size) return 0;
	int op = rd16(ain->code + ip) & ~0xC000;
	if (op < 0 || op >= NR_OPCODES) return 0;
	int w = instruction_width(op);
	if (w < 2) return 0;
	d->ip = ip; d->op = op; d->w = w;
	d->a[0] = d->a[1] = d->a[2] = 0;
	if (w >= 6) d->a[0] = rd32(ain->code + ip + 2);
	if (w >= 10) d->a[1] = rd32(ain->code + ip + 6);
	if (w >= 14) d->a[2] = rd32(ain->code + ip + 10);
	return 1;
}

static const char *opname(int op)
{
	const char *n = instructions[op].name;
	return n ? n : "?";
}

/* enclosing function for an address: last function whose address <= ip */
static struct ain_function *owner(struct ain *ain, size_t ip, int *fno)
{
	struct ain_function *best = NULL;
	int bestno = -1;
	for (int i = 0; i < ain->nr_functions; i++) {
		struct ain_function *f = &ain->functions[i];
		if (f->is_label) continue;
		if ((size_t)f->address <= ip && (!best || f->address > best->address)) {
			best = f; bestno = i;
		}
	}
	if (fno) *fno = bestno;
	return best;
}

static void print_window(struct ain *ain, size_t ip, int ctx)
{
	size_t start = ip;
	/* walk back ctx instructions */
	for (int k = 0; k < ctx; k++) {
		size_t p = start;
		int guard = 0;
		while (p > 0 && guard++ < 40) {
			p--;
			struct dec d;
			if (!decode(ain, p, &d)) continue;
			if (p + d.w == start) { start = p; break; }
		}
	}
	size_t cur = start;
	for (int k = 0; k <= ctx * 2; k++) {
		struct dec d;
		if (!decode(ain, cur, &d)) break;
		printf("   %s0x%08zx  %-22s", d.ip == ip ? "=> " : "   ", d.ip, opname(d.op));
		switch (d.op) {
		case CALLHLL: printf("lib=%d func=%d tag=%d", d.a[0], d.a[1], d.a[2]); break;
		case CALLMETHOD: printf("nargs=%d", d.a[0]); break;
		case CALLFUNC: case CALLFUNC2: case FUNC: case NEW:
		case SR_REF: case SR_ASSIGN: case SH_STRUCTREF: case PUSHSTRUCTPAGE:
			printf("%d %d %d", d.a[0], d.a[1], d.a[2]); break;
		case S_PUSH: {
			if (d.a[0] >= 0 && d.a[0] < ain->nr_strings) {
				struct string *s = ain->strings[d.a[0]];
				char *u = sjis2utf(s->text, strlen(s->text));
				printf("STR[%d]=\"%s\"", d.a[0], u ? u : s->text);
				free(u);
			} else {
				printf("STR[%d]", d.a[0]);
			}
			break;
		}
		default:
			if (d.w >= 6) printf("%d %d %d", d.a[0], d.a[1], d.a[2]);
		}
		printf("\n");
		cur += d.w;
		if (cur > ain->code_size) break;
	}
}


#define DIS_MAX 400000
static char dis_seen[DIS_MAX];

static void dis_print(struct ain *ain, struct dec *d)
{
	printf("  0x%08zx: %-22s", d->ip, opname(d->op));
	switch (d->op) {
	case CALLHLL: printf("lib=%d func=%d tag=%d", d->a[0], d->a[1], d->a[2]); break;
	case CALLMETHOD: printf("nargs=%d", d->a[0]); break;
	case PUSH: case PUSHSTRUCTPAGE: case S_ASSIGN: case SR_ASSIGN:
	case CALLFUNC: case CALLFUNC2: case NEW: case SR_REF: case FUNC: case ENDFUNC:
	case SH_LOCALREF: case SH_LOCALINC: case SH_LOCALDEC: case SH_LOCALCREATE:
	case SH_GLOBALREF: case SH_STRUCTREF: case SH_LOCALASSIGN: case DG_STR_TO_METHOD:
		printf("%d", d->a[0]); break;
	case S_PUSH: {
		if (d->a[0] >= 0 && d->a[0] < ain->nr_strings) {
			struct string *st = ain->strings[d->a[0]];
			char *u = sjis2utf(st->text, strlen(st->text));
			printf("STR[%d]=\"%s\"", d->a[0], u ? u : st->text);
			free(u);
		} else {
			printf("STR[%d]", d->a[0]);
		}
		break;
	}
	case JUMP: case IFZ: case IFNZ: case SJUMP: case DG_CALL: case DG_CALLBEGIN:
	case DG_NEW_FROM_METHOD:
		printf("-> 0x%08x", d->a[0]); break;
	default:
		if (d->w >= 6) printf("%d %d %d", d->a[0], d->a[1], d->a[2]);
	}
	printf("\n");
}

/* Walk the control-flow graph instead of the linear code stream: the compiler
 * interleaves inline lambda bodies, so a function's instructions are not
 * contiguous. Visited addresses are printed in address order. */
static void dis_func(struct ain *ain, int fno)
{
	if (fno < 0 || fno >= ain->nr_functions) return;
	struct ain_function *f = &ain->functions[fno];
	printf("\n==== fno=%d %s addr=0x%08x nargs=%d nvars=%d ret=t%d ====\n",
	       fno, f->name, f->address, f->nr_args, f->nr_vars, f->return_type.data);
	for (int i = 0; i < f->nr_vars; i++)
		printf("  var[%d] t%d struc=%d rank=%d %s\n", i, f->vars[i].type.data,
		       f->vars[i].type.struc, f->vars[i].type.rank, f->vars[i].name);

	size_t visited[8192];
	int nv = 0;
	size_t work[8192];
	int nw = 0;
	if ((size_t)f->address >= (size_t)ain->code_size) return;
	work[nw++] = f->address;
	while (nw > 0 && nv < 8192) {
		size_t ip = work[--nw];
		struct dec d;
		if (!decode(ain, ip, &d)) continue;
		int dup = 0;
		for (int i = 0; i < nv; i++) if (visited[i] == ip) { dup = 1; break; }
		if (dup) continue;
		visited[nv++] = ip;
		if (d.op == RETURN || d.op == ENDFUNC)
			continue;
		if (d.op == JUMP) {
			if (d.a[0] > 0) work[nw++ < 8192 ? nw : 0] = (size_t)(uint32_t)d.a[0];
			if (nw > 0) nw = nw > 8192 ? 8192 : nw;
			if (nw <= 8192) { work[nw - 1] = (size_t)(uint32_t)d.a[0]; }
			continue;
		}
		if ((d.op == IFZ || d.op == IFNZ || d.op == SJUMP ||
		     d.op == SWITCH || d.op == STRSWITCH) && nw + 2 <= 8192) {
			work[nw++] = (size_t)(uint32_t)d.a[0];
			work[nw++] = ip + d.w;
			continue;
		}
		if (nw < 8192) work[nw++] = ip + d.w;
	}
	/* print in address order */
	for (int i = 0; i < nv; i++) {
		for (int j = i + 1; j < nv; j++) {
			if (visited[j] < visited[i]) { size_t t = visited[i]; visited[i] = visited[j]; visited[j] = t; }
		}
	}
	for (int i = 0; i < nv; i++) {
		struct dec d;
		if (decode(ain, visited[i], &d)) dis_print(ain, &d);
	}
}



static int find_str(struct ain *ain, const char *needle)
{
	int n = 0;
	for (int i = 0; i < ain->nr_strings; i++) {
		struct string *st = ain->strings[i];
		if (!st || !st->text) continue;
		char *u = sjis2utf(st->text, strlen(st->text));
		if ((u && strstr(u, needle)) || strstr(st->text, needle)) {
			printf("STR[%d] = \"%s\"\n", i, u ? u : st->text);
			n++;
		}
		free(u);
	}
	printf("== %d matches for \"%s\"\n", n, needle);
	return 0;
}

static int hll_top(struct ain *ain, int lib_filter)
{
	static int count[256][1024];
	for (size_t ip = 0; ip + 2 <= ain->code_size;) {
		struct dec d;
		if (!decode(ain, ip, &d)) { ip += 2; continue; }
		if (d.op == CALLHLL && d.a[0] >= 0 && d.a[0] < 256 && d.a[1] >= 0 && d.a[1] < 1024)
			count[d.a[0]][d.a[1]]++;
		ip += d.w;
	}
	struct { int lib, func, n; } top[4096];
	int nt = 0, total = 0, distinct = 0;
	for (int l = 0; l < ain->nr_libraries && l < 256; l++) {
		for (int f = 0; f < ain->libraries[l].nr_functions && f < 1024; f++) {
			if (!count[l][f]) continue;
			distinct++;
			total += count[l][f];
			if (lib_filter >= 0 && l != lib_filter) continue;
			if (nt < 4096) { top[nt].lib = l; top[nt].func = f; top[nt].n = count[l][f]; nt++; }
		}
	}
	for (int i = 0; i < nt; i++)
		for (int j = i + 1; j < nt; j++)
			if (top[j].n > top[i].n) { typeof(top[0]) t = top[i]; top[i] = top[j]; top[j] = t; }
	printf("total CALLHLL sites=%d distinct (lib,func)=%d libraries=%d\n", total, distinct, ain->nr_libraries);
	for (int i = 0; i < nt; i++) {
		struct ain_library *L = &ain->libraries[top[i].lib];
		struct ain_hll_function *F = &L->functions[top[i].func];
		printf("%6d  %-24s %-36s nr_args=%d ret=t%d\n", top[i].n, L->name, F->name,
		       F->nr_arguments, F->return_type.data);
	}
	return 0;
}

static int sites_call(struct ain *ain, int target, int ctx)
{
	int n = 0;
	struct dec win[48];
	int nw = 0;
	for (size_t ip = 0; ip + 2 <= ain->code_size;) {
		struct dec d;
		if (!decode(ain, ip, &d)) { ip += 2; continue; }
		int hit = 0;
		if ((d.op == CALLFUNC || d.op == CALLFUNC2) && d.a[0] == target) {
			hit = 1;
		} else if (d.op == CALLMETHOD) {
			/* v12 CALLMETHOD: nargs in the immediate, fno pushed on the
			 * stack. The compiler always emits PUSHSTRUCTPAGE; PUSH <fno>
			 * before the argument pushes. */
			for (int k = 0; k + 1 < nw; k++) {
				if (win[k].op == PUSHSTRUCTPAGE && win[k + 1].op == PUSH
				    && win[k + 1].a[0] == target) {
					hit = 1;
					break;
				}
			}
		}
		if (hit) {
			int fno; struct ain_function *f = owner(ain, ip, &fno);
			printf("---- site #%d  %s(fno=%d) 0x%08zx  %s ----\n", ++n,
			       f ? f->name : "?", fno, ip, opname(d.op));
			print_window(ain, ip, ctx);
		}
		if (nw < 48) win[nw++] = d;
		else { memmove(win, win + 1, sizeof(win[0]) * 47); win[47] = d; }
		ip += d.w;
	}
	printf("== total %d call sites (fno=%d)\n", n, target);
	return n;
}

static int sites_hll(struct ain *ain, int lib, int func, int ctx)
{
	int n = 0;
	for (size_t ip = 0; ip + 2 <= ain->code_size;) {
		struct dec d;
		if (!decode(ain, ip, &d)) { ip += 2; continue; }
		if (d.op == CALLHLL && d.a[0] == lib && d.a[1] == func) {
			int fno; struct ain_function *f = owner(ain, ip, &fno);
			printf("---- site #%d  %s(fno=%d) 0x%08zx  tag=%d ----\n",
			       ++n, f ? f->name : "?", fno, ip, d.a[2]);
			print_window(ain, ip, ctx);
		}
		ip += d.w;
	}
	printf("== total %d sites (lib=%d func=%d)\n", n, lib, func);
	return n;
}

static int sites_op(struct ain *ain, int opcode, int ctx)
{
	int n = 0;
	for (size_t ip = 0; ip + 2 <= ain->code_size;) {
		struct dec d;
		if (!decode(ain, ip, &d)) { ip += 2; continue; }
		if (d.op == opcode) {
			int fno; struct ain_function *f = owner(ain, ip, &fno);
			printf("---- site #%d  %s(fno=%d) 0x%08zx ----\n", ++n,
			       f ? f->name : "?", fno, ip);
			print_window(ain, ip, ctx);
			if (n >= 400) break;
		}
		ip += d.w;
	}
	printf("== total %d sites (opcode 0x%x %s)\n", n, opcode, opname(opcode));
	return n;
}

static int sites_str(struct ain *ain, int sn, int ctx)
{
	int n = 0;
	for (size_t ip = 0; ip + 2 <= ain->code_size;) {
		struct dec d;
		if (!decode(ain, ip, &d)) { ip += 2; continue; }
		if (d.op == S_PUSH && d.a[0] == sn) {
			int fno; struct ain_function *f = owner(ain, ip, &fno);
			printf("---- site #%d  %s(fno=%d) 0x%08zx ----\n", ++n,
			       f ? f->name : "?", fno, ip);
			print_window(ain, ip, ctx);
		}
		ip += d.w;
	}
	printf("== total %d sites (STR[%d])\n", n, sn);
	return n;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <ain> hll <lib> <func> [ctx] | call <fno> [ctx] | dis <fno> | calls <opcode> [ctx] | str <n> [ctx] | funcs <substr>\n", argv[0]);
		return 1;
	}
	int err = 0;
	struct ain *ain = ain_open(argv[1], &err);
	if (!ain) { fprintf(stderr, "ain_open failed: %d\n", err); return 1; }
	initialize_instructions(ain->version);

	if (!strcmp(argv[2], "findstr") && argc >= 4) return find_str(ain, argv[3]);
	if (!strcmp(argv[2], "at") && argc >= 4) {
		size_t ip = (size_t)strtoul(argv[3], NULL, 0);
		int n = argc > 4 ? atoi(argv[4]) : 40;
		while (n-- > 0 && ip < ain->code_size) {
			struct dec d;
			if (!decode(ain, ip, &d)) { printf("0x%08zx ???\n", ip); break; }
			dis_print(ain, &d);
			if (d.op == JUMP || d.op == RETURN) { /* keep printing, caller decides */ }
			ip += d.w;
		}
		return 0;
	}
	if (!strcmp(argv[2], "globals")) {
		for (int i = 0; i < ain->nr_globals; i++) {
			struct ain_variable *g = &ain->globals[i];
			char *u = sjis2utf(g->name ? g->name : "", g->name ? strlen(g->name) : 0);
			printf("g[%d] t%d struc=%d rank=%d %s\n", i, g->type.data, g->type.struc,
			       g->type.rank, u ? u : g->name);
			free(u);
		}
		return 0;
	}
	if (!strcmp(argv[2], "hlltop")) return hll_top(ain, argc > 3 ? atoi(argv[3]) : -1);
	if (!strcmp(argv[2], "dis") && argc >= 4) {
		char *p = argv[3];
		int n = (p[0] == 'f' && p[1] == 'n' && p[2] == 'o' && p[3] == ':') ? atoi(p + 4) : atoi(p);
		dis_func(ain, n);
		return 0;
	}
	if (!strcmp(argv[2], "call") && argc >= 4)
		return sites_call(ain, atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 4) ? 0 : 0;
	if (!strcmp(argv[2], "hll") && argc >= 5)
		return sites_hll(ain, atoi(argv[3]), atoi(argv[4]), argc > 5 ? atoi(argv[5]) : 4) ? 0 : 0;
	if (!strcmp(argv[2], "calls") && argc >= 4)
		return sites_op(ain, (int)strtol(argv[3], NULL, 0), argc > 4 ? atoi(argv[4]) : 4) ? 0 : 0;
	if (!strcmp(argv[2], "str") && argc >= 4)
		return sites_str(ain, atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 4) ? 0 : 0;
	if (!strcmp(argv[2], "funcs") && argc >= 4) {
		int n = 0;
		for (int i = 0; i < ain->nr_functions; i++) {
			struct ain_function *f = &ain->functions[i];
			if (strstr(f->name, argv[3])) {
				printf("fno=%d addr=0x%08x nargs=%d nvars=%d ret=t%d%c %s\n",
				       i, f->address, f->nr_args, f->nr_vars,
				       f->return_type.data,
				       f->is_lambda ? 'L' : ' ', f->name);
				n++;
			}
		}
		printf("== %d functions match \"%s\"\n", n, argv[3]);
		return 0;
	}
	fprintf(stderr, "bad mode\n");
	return 1;
}
