/* Address/struct/opcode level AIN inspector. Diagnostic tool, links libsys4.a only.
 *
 * Modes:
 *   at:<hex>       disasm N instructions from a raw code offset (may be misaligned)
 *   atf:<hex>      find the enclosing AIN function, print its header/locals,
 *                  then disasm from the FUNCTION START up to past <hex> (aligned)
 *   struct:<name>  structure / interface / vtable layout
 *   func:<name>    function header/locals + disasm from its start
 *   callhll        CALLHLL tag histogram
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <system4/ain.h>
#include <system4/string.h>
#include <system4/instructions.h>
static uint16_t r16(const uint8_t*p){return (uint16_t)(p[0]|(p[1]<<8));}
static int32_t r32(const uint8_t*p){return (int32_t)((uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24));}
static void disasm_at(struct ain*a,size_t ip,int n){
	for(int i=0;i<n && ip+2<=a->code_size;i++){
		int raw=r16(a->code+ip), op=raw&~0xC000;
		if(op<0||op>=NR_OPCODES){printf("  BAD 0x%04x\n",raw);break;}
		int na=instructions[op].nr_args;
		printf("  0x%08zx: 0x%03x %-34s",ip,op,instructions[op].name?instructions[op].name:"?");
		for(int k=0;k<na&&k<4;k++) printf(" %d",r32(a->code+ip+2+k*4));
		printf("\n"); ip+=instruction_width(op);
	}
}
/* Aligned disassembly: walk from a known function start, mark the target ip. */
static void disasm_range(struct ain*a,size_t from,size_t to,size_t mark){
	size_t ip=from;
	for(int i=0;i<2000 && ip<to && ip+2<=a->code_size;i++){
		int raw=r16(a->code+ip), op=raw&~0xC000;
		printf("  %s 0x%08zx: 0x%03x %-32s", ip==mark?">>":"  ",ip,op,
			op>=0&&op<NR_OPCODES&&instructions[op].name?instructions[op].name:"?");
		if(op<0||op>=NR_OPCODES){printf(" BAD 0x%04x\n",raw);break;}
		int na=instructions[op].nr_args;
		for(int k=0;k<na&&k<4;k++) printf(" %d",r32(a->code+ip+2+k*4));
		printf("\n");
		int w=instruction_width(op); if(w<2)break;
		ip+=(size_t)w;
	}
}
static void print_function(struct ain*a,int fno){
	struct ain_function*f=&a->functions[fno];
	printf("FUNC %s fno=%d addr=0x%x nr_args=%d nr_vars=%d struct_type=%d ret=t%d/s%d\n",
		f->name?f->name:"?",fno,f->address,f->nr_args,f->nr_vars,f->struct_type,
		f->return_type.data,f->return_type.struc);
	for(int v=0;v<f->nr_vars;v++)
		printf("  var[%d] type=t%d struc=%d rank=%d %s\n",v,
			f->vars[v].type.data,f->vars[v].type.struc,f->vars[v].type.rank,
			f->vars[v].name?f->vars[v].name:"");
}
int main(int argc,char**argv){
	if(argc<3){fprintf(stderr,
		"usage: %s <ain> <mode> [count]\n"
		"  at:<hex>            从任意偏移反汇编 N 条（可能未对齐）\n"
		"  atf:<hex>           定位包含该地址的函数，从头对齐反汇编\n"
		"  func:<name>         按函数全名打印头/局部变量并反汇编\n"
		"  findsym:<substr>    按名字子串列出函数（fno/地址/参数/返回）\n"
		"  lib:<n>[:<substr>]  打印库 n 的函数表与签名\n"
		"  struct:<name>       结构体/接口布局\n"
		"  memb:<n>            结构体 n 的成员（带类型文本）\n"
		"  fno:<n>             按函数号打印\n"
		"  str:<n> / dno:<n>   字符串 / 委托\n"
		"  callhll             CALLHLL 的 tag 直方图\n",argv[0]);return 1;}
	int err=0; struct ain*a=ain_open(argv[1],&err);
	if(!a){fprintf(stderr,"ain_open failed %d\n",err);return 1;}
	initialize_instructions(a->version);
	const char*spec=argv[2]; int count=argc>3?atoi(argv[3]):40;
	if(!strncmp(spec,"callhll",7)){
		long long hist[512]; memset(hist,0,sizeof hist);
		long long total=0,neg=0,oor=0; int shown=0;
		for(size_t ip=0;ip+2<=a->code_size;){
			int op=r16(a->code+ip)&~0xC000;
			if(op<0||op>=NR_OPCODES){ip+=2;continue;}
			int w=instruction_width(op); if(w<2)break;
			if(op==CALLHLL&&w>=14){
				int lib=r32(a->code+ip+2),fn=r32(a->code+ip+6),tag=r32(a->code+ip+10);
				total++; if(tag<0)neg++;
				if(tag>=0&&tag<512)hist[tag]++;else oor++;
				if(shown<20&&lib==5){printf("  CALLHLL 0x%zx lib=%d fn=%d tag=%d\n",ip,lib,fn,tag);shown++;}
			}
			ip+=w;
		}
		printf("CALLHLL total=%lld neg=%lld out_of_range=%lld\n",total,neg,oor);
		for(int i=0;i<512;i++)if(hist[i])printf("  tag %3d : %lld\n",i,hist[i]);
		return 0;
	}
	if(!strncmp(spec,"at:",3)){size_t ip=strtoul(spec+3,NULL,16);
		printf("DISASM at 0x%zx count=%d\n",ip,count); disasm_at(a,ip,count); return 0;}
	if(!strncmp(spec,"atf:",4)){
		size_t ip=strtoul(spec+4,NULL,16);
		int best=-1; size_t bestaddr=0;
		for(int i=0;i<a->nr_functions;i++){
			size_t addr=(size_t)a->functions[i].address;
			if(addr<=ip&&addr>=bestaddr){best=i;bestaddr=addr;}
		}
		if(best<0){fprintf(stderr,"no function starts before 0x%zx\n",ip);return 1;}
		print_function(a,best);
		printf("--- aligned disasm from function start, target 0x%zx ---\n",ip);
		disasm_range(a,bestaddr,ip+0x40,ip);
		return 0;}
	if(!strncmp(spec,"sno:",4)){
		int n=atoi(spec+4);
		if(n<0||n>=a->nr_structures){fprintf(stderr,"sno out of range\n");return 1;}
		struct ain_struct*st=&a->structures[n];
		printf("STRUCT [%d] %s nr_members=%d nr_interfaces=%d is_interface=%d ctor=%d\n",
			n,st->name?st->name:"?",st->nr_members,st->nr_interfaces,st->is_interface,st->constructor);
		for(int j=0;j<st->nr_interfaces;j++)printf("  iface[%d] struct_type=%d vtable_offset=%d\n",j,
			st->interfaces[j].struct_type,st->interfaces[j].vtable_offset);
		return 0;}
	if(!strncmp(spec,"struct:",7)){
		const char*want=spec+7;
		for(int s=0;s<a->nr_structures;s++){struct ain_struct*st=&a->structures[s];
			if(!st->name||strcmp(st->name,want))continue;
			printf("STRUCT %s no=%d nr_members=%d nr_interfaces=%d nr_vmethods=%d ctor=%d dtor=%d is_interface=%d nr_iface_methods=%d\n",
				st->name,s,st->nr_members,st->nr_interfaces,st->nr_vmethods,st->constructor,st->destructor,st->is_interface,st->nr_iface_methods);
			for(int m=0;m<st->nr_members;m++)printf("  memb[%d] type=%d struc=%d rank=%d %s\n",m,st->members[m].type.data,st->members[m].type.struc,st->members[m].type.rank,st->members[m].name);
			for(int j=0;j<st->nr_interfaces;j++)printf("  iface[%d] struct_type=%d vtable_offset=%d (%s)\n",j,st->interfaces[j].struct_type,st->interfaces[j].vtable_offset,
				st->interfaces[j].struct_type>=0&&st->interfaces[j].struct_type<a->nr_structures?a->structures[st->interfaces[j].struct_type].name:"?");
			for(int j=0;j<st->nr_vmethods;j++)printf("  vmethod[%d] fno=%d %s\n",j,st->vmethods[j],
				st->vmethods[j]>=0&&st->vmethods[j]<a->nr_functions?a->functions[st->vmethods[j]].name:"?");
			return 0;}
		printf("struct not found: %s\n",want);return 1;}
	if(!strncmp(spec,"fno:",4)){
		int n=atoi(spec+4);
		if(n<0||n>=a->nr_functions){fprintf(stderr,"fno out of range\n");return 1;}
		print_function(a,n);
		disasm_at(a,a->functions[n].address,count);
		return 0;}
	if(!strncmp(spec,"func:",5)){
		const char*want=spec+5;
		for(int i=0;i<a->nr_functions;i++){struct ain_function*f=&a->functions[i];
			if(!f->name||strcmp(f->name,want))continue;
			print_function(a,i);
			disasm_at(a,f->address,count);return 0;}
		printf("func not found: %s\n",want);return 1;}
	if(!strncmp(spec,"str:",4)){
		int n=atoi(spec+4);
		if(n<0||n>=(int)a->nr_strings){fprintf(stderr,"str index out of range\n");return 1;}
		printf("STR[%d] = [%s]\n",n,(a->strings[n]&&a->strings[n]->text)?a->strings[n]->text:"(null)");
		return 0;}
	if(!strncmp(spec,"dno:",4)){
		int n=atoi(spec+4);
		if(n<0||n>=(int)a->nr_delegates){fprintf(stderr,"dno out of range\n");return 1;}
		printf("DELEGATE [%d] nr_args=%d ret=%d/%d\n",n,a->delegates[n].nr_arguments,
			a->delegates[n].return_type.data,a->delegates[n].return_type.struc);
		for(int i=0;i<a->delegates[n].nr_arguments;i++)
			printf("  arg[%d] type=%d struc=%d rank=%d\n",i,
				a->delegates[n].variables[i].type.data,
				a->delegates[n].variables[i].type.struc,
				a->delegates[n].variables[i].type.rank);
		return 0;}
	if(!strncmp(spec,"findsym:",8)){
		const char*want=spec+8; int shown=0;
		for(int i=0;i<a->nr_functions;i++){
			struct ain_function*f=&a->functions[i];
			if(!f->name||!strstr(f->name,want))continue;
			char*rt=ain_strtype_d(a,&f->return_type);
			printf("fno=%-7d addr=0x%-7x args=%d vars=%d struc=%d ret=%-18s %s\n",
				i,f->address,f->nr_args,f->nr_vars,f->struct_type,
				rt?rt:"?",f->name);
			free(rt);
			shown++;
		}
		printf("-- %d function(s) matching \"%s\"\n",shown,want);
		return shown?0:1;}

	if(!strncmp(spec,"lib:",4)){
		const char*arg=spec+4;
		char buf[256]; snprintf(buf,sizeof buf,"%s",arg);
		char*sub=strchr(buf,':');
		if(sub){*sub=0;sub++;}
		int l=atoi(buf);
		if(l<0||l>=a->nr_libraries){fprintf(stderr,"lib out of range\n");return 1;}
		struct ain_library*lib=&a->libraries[l];
		printf("lib[%d] %s nr_functions=%d\n",l,lib->name?lib->name:"?",lib->nr_functions);
		int shown=0;
		for(int i=0;i<lib->nr_functions;i++){
			struct ain_hll_function*fn=&lib->functions[i];
			if(sub&&*sub&&(!fn->name||!strstr(fn->name,sub)))continue;
			char*rt=ain_strtype_d(a,&fn->return_type);
			printf("  [%d] %-32s args=%d ret=%-16s",i,
				fn->name?fn->name:"?",fn->nr_arguments,rt?rt:"?");
			for(int k=0;k<fn->nr_arguments;k++){
				char*at=ain_strtype_d(a,&fn->arguments[k].type);
				printf(" %s:%s",fn->arguments[k].name?fn->arguments[k].name:"?",
					at?at:"?");
				free(at);
			}
			printf("\n");
			free(rt);
			shown++;
		}
		return shown?0:1;}

	if(!strncmp(spec,"memb:",5)){
		int n=atoi(spec+5);
		if(n<0||n>=a->nr_structures){fprintf(stderr,"struct no out of range\n");return 1;}
		struct ain_struct*st=&a->structures[n];
		printf("STRUCT [%d] %s members=%d iface=%d nr_interfaces=%d vmethods=%d\n",
			n,st->name?st->name:"?",st->nr_members,st->is_interface,
			st->nr_interfaces,st->nr_vmethods);
		for(int m=0;m<st->nr_members;m++){
			char*t=ain_strtype_d(a,&st->members[m].type);
			printf("  [%d] %-28s %s\n",m,
				st->members[m].name?st->members[m].name:"?",t?t:"?");
			free(t);
		}
		for(int j=0;j<st->nr_interfaces;j++)
			printf("  iface[%d] struct=%d %s vtable_offset=%d\n",j,
				st->interfaces[j].struct_type,
				(st->interfaces[j].struct_type>=0&&
				 st->interfaces[j].struct_type<a->nr_structures)
				 ?a->structures[st->interfaces[j].struct_type].name:"?",
				st->interfaces[j].vtable_offset);
		return 0;}

	fprintf(stderr,"bad spec %s\n",spec);return 1;
}
