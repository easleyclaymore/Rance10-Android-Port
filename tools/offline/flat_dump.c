#include <stdio.h>
#include <stdlib.h>
#include "system4/flat.h"
#include "system4/file.h"

int main(int argc, char **argv)
{
	size_t size;
	uint8_t *data = file_read(argv[1], &size);
	if (!data) { printf("read fail\n"); return 1; }
	int error = 0;
	struct flat *fl = flat_open(data, size, &error);
	if (!fl) { printf("parse fail err=%d\n", error); return 1; }
	printf("hdr %dx%d fps=%d nr_timelines=%zu nr_libraries=%zu\n",
	       fl->hdr.width, fl->hdr.height, fl->hdr.fps,
	       fl->nr_timelines, fl->nr_libraries);
	for (size_t t = 0; t < fl->nr_timelines; t++) {
		struct flat_timeline *tl = &fl->timelines[t];
		printf("timeline %zu: type=%d nr_frames=%u\n", t, tl->type, tl->frame_count);
		if (tl->type == 3 && tl->graphic.keys) {
			for (uint32_t k = 0; k < tl->graphic.count && k < 8; k++) {
				struct flat_key_data_graphic *key = &tl->graphic.keys[k];
				printf("  key[%u] pos=(%.1f,%.1f) scale=(%.3f,%.3f) area=(%d,%d %dx%d) origin=(%d,%d) align=%d alpha=%d\n",
				       k, key->pos_x, key->pos_y, key->scale_x, key->scale_y,
				       key->area_x, key->area_y, key->area_width, key->area_height,
				       key->origin_x, key->origin_y, key->align, key->alpha);
			}
		}
	}
	for (size_t l = 0; l < fl->nr_libraries; l++) {
		struct flat_library *lib = &fl->libraries[l];
		printf("library %zu: type=%d\n", l, lib->type);
	}
	return 0;
}
