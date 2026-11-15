/* 版权所有 (C) 2024 kichikuou <KichikuouChrome@gmail.com>
 *
 * 本程序是自由软件：你可以依据自由软件基金会发布的 GNU 通用公共许可证
 * （第 2 版，或你选择的任何更高版本）的条款，重新分发和/或修改本程序。
 *
 * 分发本程序是希望它有用，但不作任何担保，甚至不担保适销性或
 * 适用于特定用途。详见 GNU 通用公共许可证。
 *
 * 你应当已经随本程序收到一份 GNU 通用公共许可证的副本；
 * 如果没有，见 <http://gnu.org/licenses/>。
 */

#include <stdlib.h>

#include "cJSON.h"
#include "hll.h"
#include "plugin.h"
#include "sact.h"
#include "sprite.h"
#include "xsystem4.h"

static const char plugin_name[] = "DrawRipple";

struct ripple {
	float x;
	float y;
	uint32_t start_time;
};

struct draw_ripple_plugin {
	struct draw_plugin p;
	float width;
	float height;
	int time;
	int clip_surface;
	int nr_ripples;
	struct ripple *ripples;
	uint32_t timestamp;
};

static struct draw_ripple_plugin *get_draw_ripple_plugin(int surface)
{
	struct sact_sprite *sp = sact_try_get_sprite(surface);
	if (!sp)
		return NULL;
	if (!sp->plugin || sp->plugin->name != plugin_name)
		return NULL;
	return (struct draw_ripple_plugin *)sp->plugin;
}

static void DrawRipple_free(struct draw_plugin *_plugin)
{
	struct draw_ripple_plugin *plugin = (struct draw_ripple_plugin *)_plugin;
	free(plugin->ripples);
	free(plugin);
}

static void draw_point(uint32_t *pixels, int w, int h, int x, int y)
{
	if (x < 0 || x >= w || y < 0 || y >= h)
		return;
	pixels[y * w + x] = 0xffffffff;
}

static void draw_ellipse(uint32_t *pixels, int dst_w, int dst_h, int cx, int cy, int rx, int ry)
{
	// Midpoint Ellipse Algorithm.
	if (rx == 0 || ry == 0)
		return;
	const int rx2 = rx * rx;
	const int ry2 = ry * ry;
	int x = 0;
	int y = ry;
	int px = 0;
	int py = 2 * rx2 * y;

	// Region 1
	int p = ry2 - (rx2 * ry) + (0.25 * rx2);
	while (px < py) {
		draw_point(pixels, dst_w, dst_h, cx + x, cy + y);
		draw_point(pixels, dst_w, dst_h, cx - x, cy + y);
		draw_point(pixels, dst_w, dst_h, cx + x, cy - y);
		draw_point(pixels, dst_w, dst_h, cx - x, cy - y);
		x++;
		px += 2 * ry2;
		if (p < 0) {
			p += ry2 + px;
		} else {
			y--;
			py -= 2 * rx2;
			p += ry2 + px - py;
		}
	}

	// Region 2
	p = ry2 * (x + 0.5) * (x + 0.5) + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
	while (y > 0) {
		draw_point(pixels, dst_w, dst_h, cx + x, cy + y);
		draw_point(pixels, dst_w, dst_h, cx - x, cy + y);
		draw_point(pixels, dst_w, dst_h, cx + x, cy - y);
		draw_point(pixels, dst_w, dst_h, cx - x, cy - y);
		y--;
		py -= 2 * rx2;
		if (p > 0) {
			p += rx2 - py;
		} else {
			x++;
			px += 2 * ry2;
			p += rx2 - py + px;
		}
	}
	draw_point(pixels, dst_w, dst_h, cx + rx, cy);
	draw_point(pixels, dst_w, dst_h, cx - rx, cy);
}

static void DrawRipple_update(struct sact_sprite *sp)
{
	struct draw_ripple_plugin *plugin = (struct draw_ripple_plugin *)sp->plugin;
	if (!plugin->ripples)
		return;
	uint32_t timestamp = SDL_GetTicks();
	if (timestamp - plugin->timestamp < 16)
		return;
	plugin->timestamp = timestamp;

	struct texture *dst = sprite_get_texture(sp);
	uint32_t *pixels = xcalloc(dst->w * dst->h, 4);
	for (int i = 0; i < plugin->nr_ripples; i++) {
		struct ripple *r = &plugin->ripples[i];
		float rate = (float)(timestamp - r->start_time) / plugin->time;
		if (rate >= 1.f) {
			r->x = rand() % dst->w;
			r->y = rand() % dst->h;
			r->start_time += plugin->time * floorf(rate);
			continue;
		}
		draw_ellipse(pixels, dst->w, dst->h, r->x, r->y, plugin->width * rate, plugin->height * rate);
	}
	gfx_update_texture_with_pixels(dst, pixels);
	free(pixels);
	struct texture *clip = sprite_get_texture(sact_get_sprite(plugin->clip_surface));
	gfx_blend_da_daxsa(dst, 0, 0, clip, 0, 0, clip->w, clip->h);
	sprite_dirty(sp);
}

static cJSON *DrawRipple_to_json(struct sact_sprite *sp, bool verbose)
{
	struct draw_ripple_plugin *plugin = (struct draw_ripple_plugin *)sp->plugin;
	cJSON *obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "name", plugin->p.name);
	cJSON_AddNumberToObject(obj, "width", plugin->width);
	cJSON_AddNumberToObject(obj, "height", plugin->height);
	cJSON_AddNumberToObject(obj, "time", plugin->time);
	cJSON_AddNumberToObject(obj, "clip_surface", plugin->clip_surface);
	cJSON_AddNumberToObject(obj, "nr_ripples", plugin->nr_ripples);
	cJSON_AddBoolToObject(obj, "started", !!plugin->ripples);
	return obj;
}

static int DrawRipple_Init(int surface)
{
	struct sact_sprite *sp = sact_get_sprite(surface);
	if (!sp) {
		WARNING("DrawRipple.Init: invalid surface %d", surface);
		return 0;
	}
	struct draw_ripple_plugin *plugin = xcalloc(1, sizeof(struct draw_ripple_plugin));
	plugin->p.name = plugin_name;
	plugin->p.free = DrawRipple_free;
	plugin->p.update = DrawRipple_update;
	plugin->p.to_json = DrawRipple_to_json;
	plugin->width = 6;
	plugin->height = 3;
	plugin->time = 100;
	plugin->nr_ripples = 384;
	sprite_bind_plugin(sp, &plugin->p);
	return 1;
}

static void DrawRipple_Release(int surface)
{
	if (!get_draw_ripple_plugin(surface))
		return;
	sprite_bind_plugin(sact_get_sprite(surface), NULL);
}

static void DrawRipple_SetNumof(int surface, int numof)
{
	struct draw_ripple_plugin *plugin = get_draw_ripple_plugin(surface);
	if (!plugin || plugin->ripples)
		return;
	plugin->nr_ripples = numof;
}

static void DrawRipple_SetSize(int surface, float width, float height)
{
	struct draw_ripple_plugin *plugin = get_draw_ripple_plugin(surface);
	if (!plugin)
		return;
	plugin->width = width;
	plugin->height = height;
}

static void DrawRipple_SetTime(int surface, int time)
{
	struct draw_ripple_plugin *plugin = get_draw_ripple_plugin(surface);
	if (!plugin)
		return;
	plugin->time = time;
}

//void DrawRipple_SetSpeed(int nSurface, float fSpeed);

static void DrawRipple_SetClipAMap(int surface, int clip_surface)
{
	struct draw_ripple_plugin *plugin = get_draw_ripple_plugin(surface);
	if (!plugin)
		return;
	plugin->clip_surface = clip_surface;
}

static void DrawRipple_Start(int surface)
{
	struct draw_ripple_plugin *plugin = get_draw_ripple_plugin(surface);
	if (!plugin)
		return;
	if (plugin->ripples)
		return;  // already started
	plugin->ripples = xmalloc(plugin->nr_ripples * sizeof(struct ripple));
	struct sact_sprite *sp = sact_get_sprite(surface);
	uint32_t now = SDL_GetTicks();
	for (int i = 0; i < plugin->nr_ripples; i++) {
		plugin->ripples[i].x = rand() % sp->rect.w;
		plugin->ripples[i].y = rand() % sp->rect.h;
		plugin->ripples[i].start_time = now - rand() % plugin->time;
	}
}

HLL_LIBRARY(DrawRipple,
	    HLL_EXPORT(Init, DrawRipple_Init),
	    HLL_EXPORT(Release, DrawRipple_Release),
	    HLL_EXPORT(SetNumof, DrawRipple_SetNumof),
	    HLL_EXPORT(SetSize, DrawRipple_SetSize),
	    HLL_EXPORT(SetTime, DrawRipple_SetTime),
	    HLL_TODO_EXPORT(SetSpeed, DrawRipple_SetSpeed),
	    HLL_EXPORT(SetClipAMap, DrawRipple_SetClipAMap),
	    HLL_EXPORT(Start, DrawRipple_Start)
	    );
