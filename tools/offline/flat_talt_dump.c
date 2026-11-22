/* flat_talt_dump.c — 离线分析 flat 的 TALT 图集：
 * 1) 打印 timeline 关键帧/库名(hex)/TALT meta(含名字 hex)
 * 2) cg_load_buffer 解码 TALT 图集，输出 PPM(PRGB) + PGM(alpha)
 * 3) 按 meta 区域统计 alpha 包围盒（判断子图在区域内是否居中/偏移）
 *
 * 用法: flat_talt_dump <file.flat> [ppm_out_prefix]
 * 编译: gcc -O1 -I subprojects/libsys4/include -o /tmp/ftdump
 *       tools/offline/flat_talt_dump.c build-sdl3/subprojects/libsys4/libsys4.a
 *       -ldeflate -lz -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/flat.h"
#include "system4/string.h"
#include "system4/file.h"
#include "system4/cg.h"

static void hex_print(const char *s, size_t n)
{
	for (size_t i = 0; i < n && s[i]; i++)
		printf("%02x", (unsigned char)s[i]);
}

int main(int argc, char **argv)
{
	if (argc < 2) { printf("usage: %s <file.flat> [ppm_prefix]\n", argv[0]); return 1; }
	size_t size;
	uint8_t *data = file_read(argv[1], &size);
	if (!data) { printf("read fail\n"); return 1; }
	int error = 0;
	struct flat *fl = flat_open(data, size, &error);
	if (!fl) { printf("parse fail err=%d\n", error); return 1; }
	printf("hdr %dx%d fps=%d nr_timelines=%zu nr_libraries=%zu nr_talt=%u\n",
	       fl->hdr.width, fl->hdr.height, fl->hdr.fps,
	       fl->nr_timelines, fl->nr_libraries, fl->nr_talt_entries);

	int nr_graphic = 0;
	for (size_t t = 0; t < fl->nr_timelines; t++) {
		struct flat_timeline *tl = &fl->timelines[t];
		if (tl->type != FLAT_TIMELINE_GRAPHIC)
			continue;
		printf("timeline %zu (graphic#%d) name_hex=", t, nr_graphic++);
		hex_print(tl->library_name->text, tl->library_name->size);
		printf("\n");
		for (uint32_t k = 0; k < tl->graphic.count && k < 8; k++) {
			struct flat_key_data_graphic *key = &tl->graphic.keys[k];
			printf("  key[%u] pos=(%.1f,%.1f) area=(%d,%d %dx%d) align=%d alpha=%d\n",
			       k, key->pos_x, key->pos_y,
			       key->area_x, key->area_y, key->area_width, key->area_height,
			       key->align, key->alpha);
		}
	}

	if (!fl->nr_talt_entries) {
		printf("no talt\n");
		return 0;
	}
	struct talt_entry *e = &fl->talt_entries[0];
	printf("talt entry0: size=%u off=%u nr_meta=%u\n", e->size, e->off, e->nr_meta);
	for (unsigned j = 0; j < e->nr_meta; j++) {
		struct talt_metadata *m = &e->metadata[j];
		const char *nm = (const char *)(fl->data + m->unknown1_off);
		printf("  meta[%u] name_hex=", j);
		hex_print(nm, m->unknown1_size > 64 ? 64 : m->unknown1_size);
		printf(" area=(%u,%u %ux%u)\n", m->unknown2, m->unknown3, m->unknown4, m->unknown5);
	}

	struct cg *cg = cg_load_buffer(fl->data + e->off, e->size);
	if (!cg) { printf("cg decode fail\n"); return 1; }
	int W = cg->metrics.w, H = cg->metrics.h;
	printf("atlas decoded %dx%d bpp=%d has_alpha=%d pitch=%d\n",
	       W, H, cg->metrics.bpp, cg->metrics.has_alpha, cg->metrics.pixel_pitch);
	uint8_t *px = cg->pixels;

	/* 每 meta 区域统计 alpha>0 的包围盒 */
	for (unsigned j = 0; j < e->nr_meta; j++) {
		struct talt_metadata *m = &e->metadata[j];
		int x0 = m->unknown2, y0 = m->unknown3, w = m->unknown4, h = m->unknown5;
		int minx = w, miny = h, maxx = -1, maxy = -1;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				int xi = x0 + x, yi = y0 + y;
				if (xi < 0 || yi < 0 || xi >= W || yi >= H)
					continue;
				const uint8_t *p = px + (yi * W + xi) * 4;
				if (p[3] > 8) {
					if (x < minx) minx = x;
					if (y < miny) miny = y;
					if (x > maxx) maxx = x;
					if (y > maxy) maxy = y;
				}
			}
		}
		if (maxx < 0) {
			printf("  meta[%u] region EMPTY\n", j);
			continue;
		}
		printf("  meta[%u] region %dx%d content bbox=(%d,%d)-(%d,%d) size=%dx%d center=(%.1f,%.1f) region_center=(%.1f,%.1f)\n",
		       j, w, h, minx, miny, maxx, maxy,
		       maxx - minx + 1, maxy - miny + 1,
		       (minx + maxx) / 2.0f, (miny + maxy) / 2.0f,
		       w / 2.0f, h / 2.0f);
	}

	if (argc >= 3) {
		FILE *fp = fopen(argv[2], "wb");
		if (fp) {
			fprintf(fp, "P6\n%d %d\n255\n", W, H);
			for (int i = 0; i < W * H; i++)
				fwrite(px + i * 4, 1, 3, fp);
			fclose(fp);
			char buf[512];
			snprintf(buf, sizeof(buf), "%s.a.pgm", argv[2]);
			FILE *fa = fopen(buf, "wb");
			if (fa) {
				fprintf(fa, "P5\n%d %d\n255\n", W, H);
				for (int i = 0; i < W * H; i++)
					fputc(px[i * 4 + 3], fa);
				fclose(fa);
			}
			printf("wrote %s and %s.a.pgm\n", argv[2], argv[2]);
		}
	}
	cg_free(cg);
	return 0;
}
