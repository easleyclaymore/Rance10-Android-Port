/* Locate every S_PUSH <n> in the AIN code section and name its function. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <system4/ain.h>

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s <file.ain> <str-index>\n", argv[0]); return 1; }
	int err = 0;
	struct ain *a = ain_open(argv[1], &err);
	if (!a) { fprintf(stderr, "ain_open failed: %d\n", err); return 1; }
	uint32_t target = (uint32_t)atoi(argv[2]);
	int hits = 0;
	for (size_t ip = 0; ip + 6 <= a->code_size; ip++) {
		if ((unsigned char)a->code[ip] != 0x41 || (unsigned char)a->code[ip+1] != 0x00)
			continue;
		uint32_t v; memcpy(&v, a->code + ip + 2, 4);
		if (v != target) continue;
		int best = -1; size_t bestaddr = 0;
		for (int i = 0; i < a->nr_functions; i++) {
			size_t ad = (size_t)a->functions[i].address;
			if (ad <= ip && ad >= bestaddr) { best = i; bestaddr = ad; }
		}
		printf("S_PUSH %u @ 0x%08zx  fno=%d %s\n", target, ip, best,
		       best >= 0 && a->functions[best].name ? a->functions[best].name : "?");
		hits++;
	}
	printf("hits=%d\n", hits);
	return 0;
}
