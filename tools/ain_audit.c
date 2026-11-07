/* Rance10 AIN static audit: IFACE types, interface tables, opcode census, disasm.
 * Links against libsys4.a only. Diagnostic tool, not part of the runtime. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <system4/ain.h>
#include <system4/instructions.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static int32_t  rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }
static const char *tn(int t)
{
	static char b[4][32]; static int k;
	char *s = b[(k++) & 3];
	if (t == 89) return "IFACE";
	if (t == 100) return "IFACE_WRAP";
	if (t == 95) return "DELEGATE";
	snprintf(s, 32, "t%d", t); return s;
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s <file.ain> audit|opcodes|disasm <func>\n", argv[0]); return 1; }
	int err = 0;
	struct ain *ain = ain_open(argv[1], &err);
	if (!ain) { fprintf(stderr, "ain_open failed: %d\n", err); return 1; }
	initialize_instructions(ain->version);
	printf("AIN version=%d code_size=%zu functions=%d structures=%d globals=%d\n",
	       ain->version, ain->code_size, ain->nr_functions, ain->nr_structures, ain->nr_globals);
	const char *mode = argv[2];

	if (!strcmp(mode, "audit")) {
		int ng=0, nf=0, nm=0, nfr=0, nfa=0;
		for (int i=0;i<ain->nr_globals;i++) if (ain->globals[i].type.data==89||ain->globals[i].type.data==100) {
			if (ng<12) printf("GLOBAL[%d] %s %s\n", i, tn(ain->globals[i].type.data), ain->globals[i].name);
			ng++;
		}
		for (int f=0;f<ain->nr_functions;f++) {
			struct ain_function *fn=&ain->functions[f];
			if (fn->return_type.data==89||fn->return_type.data==100) { if(nfr<8) printf("FUNC-RET %s -> %s\n",fn->name,tn(fn->return_type.data)); nfr++; }
			for (int v=0;v<fn->nr_vars;v++) if (fn->vars[v].type.data==89||fn->vars[v].type.data==100) {
				if (nf<24) printf("LOCAL  %-52s var[%d/%d] %-12s struc=%d %s\n", fn->name, v, fn->nr_vars,
					tn(fn->vars[v].type.data), fn->vars[v].type.struc, fn->vars[v].name);
				nf++;
			}
			for (int a=0;a<fn->nr_args;a++) if (fn->vars[a].type.data==89||fn->vars[a].type.data==100) nfa++;
		}
		for (int s=0;s<ain->nr_structures;s++) {
			struct ain_struct *st=&ain->structures[s];
			for (int m=0;m<st->nr_members;m++) if (st->members[m].type.data==89||st->members[m].type.data==100) {
				if (nm<24) printf("MEMBER %-40s [%d/%d] %-12s struc=%d %s\n", st->name, m, st->nr_members,
					tn(st->members[m].type.data), st->members[m].type.struc, st->members[m].name);
				nm++;
			}
		}
		printf("TOTAL IFACE: globals=%d locals=%d members=%d func_ret=%d func_args=%d\n",ng,nf,nm,nfr,nfa);
		int nsi=0, nvm=0;
		for (int s=0;s<ain->nr_structures;s++) {
			struct ain_struct *st=&ain->structures[s];
			if (st->nr_interfaces) {
				if (nsi<20) { printf("STRUCT-IFACE %-40s nr_if=%d vmethods=%d\n",st->name,st->nr_interfaces,st->nr_vmethods);
					for(int j=0;j<st->nr_interfaces && j<6;j++) printf("    if[%d] struct_type=%d vtable_offset=%d (%s)\n", j,
						st->interfaces[j].struct_type, st->interfaces[j].vtable_offset,
						st->interfaces[j].struct_type>=0 && st->interfaces[j].struct_type<ain->nr_structures ? ain->structures[st->interfaces[j].struct_type].name : "?"); }
				nsi++;
			}
			nvm+=st->nr_vmethods;
		}
		printf("TOTAL structs_with_interfaces=%d vmethod_entries=%d\n",nsi,nvm);
		return 0;
	}

	if (!strcmp(mode, "opcodes")) {
		long long cnt[NR_OPCODES]; memset(cnt,0,sizeof cnt);
		long long bad=0, total=0;
		size_t ip=0;
		while (ip+2 <= ain->code_size) {
			int op = rd16(ain->code+ip) & ~0xC000;
			if (op<0 || op>=NR_OPCODES || instructions[op].nr_args<0) { bad++; ip+=2; continue; }
			int w=instruction_width(op);
			if (w<2 || ip+(size_t)w>ain->code_size) { bad++; break; }
			cnt[op]++; total++; ip+=w;
		}
		printf("decoded=%lld bad=%lld\n",total,bad);
		for (int op=0;op<NR_OPCODES;op++) if (cnt[op]) {
			if (op>=0x100 || op==CALLMETHOD || op==NEW || op==REF || op==ASSIGN || op==REFREF || op==CALLFUNC2 || op==X_ICAST)
				printf("op 0x%03x %-34s %lld\n", op, instructions[op].name?instructions[op].name:"?", cnt[op]);
		}
		return 0;
	}

	if (!strcmp(mode, "disasm")) {
		const char *want = argc>3 ? argv[3] : "CASTimerImp@Reset";
		int fi=-1;
		for (int i=0;i<ain->nr_functions;i++) if (ain->functions[i].name && !strcmp(ain->functions[i].name,want)) { fi=i; break; }
		if (fi<0) { printf("function not found: %s\n",want); return 1; }
		struct ain_function *f=&ain->functions[fi];
		printf("FUNC %s fno=%d addr=0x%x nr_args=%d nr_vars=%d struct_type=%d\n",f->name,fi,f->address,f->nr_args,f->nr_vars,f->struct_type);
		for(int v=0;v<f->nr_vars;v++) printf("  var[%d] %-14s struc=%d %s\n",v,tn(f->vars[v].type.data),f->vars[v].type.struc,f->vars[v].name);
		size_t ip=f->address; int n=argc>4?atoi(argv[4]):60;
		for(int i=0;i<n && ip+2<=ain->code_size;i++) {
			int raw=rd16(ain->code+ip); int op=raw & ~0xC000;
			if(op<0||op>=NR_OPCODES){printf("  BAD 0x%04x\n",raw);break;}
			int na=instructions[op].nr_args;
			printf("  0x%08zx: 0x%03x %-32s",ip,op,instructions[op].name?instructions[op].name:"?");
			for(int a=0;a<na && a<4;a++) printf(" %d",rd32(ain->code+ip+2+a*4));
			printf("\n"); ip+=instruction_width(op);
		}
		return 0;
	}
	fprintf(stderr,"unknown mode %s\n",mode); return 1;
}
