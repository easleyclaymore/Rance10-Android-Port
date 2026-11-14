/* sample CALLMETHOD call sites with preceding instructions */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <system4/ain.h>
#include <system4/instructions.h>
static uint16_t r16(const uint8_t*p){return (uint16_t)(p[0]|(p[1]<<8));}
static int32_t r32(const uint8_t*p){return (int32_t)((uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24));}
struct rec{size_t ip;int op;int a0,a1,a2;};
int main(int argc,char**argv){
	int e=0; struct ain*a=ain_open(argv[1],&e); if(!a)return 1;
	initialize_instructions(a->version);
	struct rec win[7]; int n=0; int sites=0;
	for(size_t ip=0;ip+2<=a->code_size;){
		int op=r16(a->code+ip)&~0xC000;
		if(op<0||op>=NR_OPCODES){ip+=2;continue;}
		int w=instruction_width(op); if(w<2)break;
		if(op==CALLMETHOD&&sites<12){
			printf("---- CALLMETHOD at 0x%zx nargs=%d ----\n",ip,r32(a->code+ip+2));
			for(int k=0;k<n;k++){struct rec*r=&win[k];
				printf("   0x%08zx %-30s %d %d %d\n",r->ip,instructions[r->op].name?instructions[r->op].name:"?",r->a0,r->a1,r->a2);}
			sites++;
		}
		struct rec cur={ip,op,0,0,0};
		if(w>=6)cur.a0=r32(a->code+ip+2);
		if(w>=10)cur.a1=r32(a->code+ip+6);
		if(w>=14)cur.a2=r32(a->code+ip+10);
		if(n<7){win[n++]=cur;}else{for(int k=1;k<7;k++)win[k-1]=win[k];win[6]=cur;}
		ip+=w;
	}
	return 0;
}
