/* Address/struct/opcode level AIN inspector. Diagnostic tool, links libsys4.a only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <system4/ain.h>
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
int main(int argc,char**argv){
	if(argc<3){fprintf(stderr,"usage: %s <ain> at:<hex>|struct:<name>|func:<name>|callhll [count]\n",argv[0]);return 1;}
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
	if(!strncmp(spec,"func:",5)){
		const char*want=spec+5;
		for(int i=0;i<a->nr_functions;i++){struct ain_function*f=&a->functions[i];
			if(!f->name||strcmp(f->name,want))continue;
			printf("FUNC %s fno=%d addr=0x%x nr_args=%d nr_vars=%d struct_type=%d ret=%d\n",f->name,i,f->address,f->nr_args,f->nr_vars,f->struct_type,f->return_type.data);
			for(int v=0;v<f->nr_vars;v++)printf("  var[%d] type=%d struc=%d %s\n",v,f->vars[v].type.data,f->vars[v].type.struc,f->vars[v].name);
			disasm_at(a,f->address,count);return 0;}
		printf("func not found: %s\n",want);return 1;}
	fprintf(stderr,"bad spec %s\n",spec);return 1;
}
