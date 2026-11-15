/* 版权所有 (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#ifndef XSYSTEM4_JSON_H
#define XSYSTEM4_JSON_H

#include <stdbool.h>
#include "gfx/types.h"

typedef struct cJSON cJSON;
struct texture;
struct text_style;
struct sact_sprite;
struct sprite;

cJSON *num2_to_json(double a, double b);
cJSON *num2_to_json_object(const char *a_name, double a, const char *b_name, double b);
cJSON *num3_to_json(double a, double b, double c);
cJSON *num3_to_json_object(const char *a_name, double a, const char *b_name, double b,
		const char *c_name, double c);
cJSON *num4_to_json(double a, double b, double c, double d);
cJSON *num4_to_json_object(const char *a_name, double a, const char *b_name, double b,
		const char *c_name, double c, const char *d_name, double d);
cJSON *texture_to_json(struct texture *t, bool verbose);
cJSON *texture_to_json_with_pixels(struct texture *t);
cJSON *text_style_to_json(struct text_style *ts, bool verbose);
cJSON *scene_sprite_to_json(struct sprite *sp, bool verbose);
cJSON *sprite_to_json(struct sact_sprite *sp, bool verbose);

static inline cJSON *xy_to_json(double x, double y, bool verbose)
{
	if (!verbose)
		return num2_to_json(x, y);
	return num2_to_json_object("x", x, "y", y);
}

static inline cJSON *wh_to_json(double w, double h, bool verbose)
{
	if (!verbose)
		return num2_to_json(w, h);
	return num2_to_json_object("w", w, "h", h);
}

static inline cJSON *point_to_json(Point *p, bool verbose)
{
	return xy_to_json(p->x, p->y, verbose);
}

static inline cJSON *rectangle_to_json(Rectangle *r, bool verbose)
{
	if (!verbose)
		return num4_to_json(r->x, r->y, r->w, r->h);
	return num4_to_json_object("x", r->x, "y", r->y, "w", r->w, "h", r->h);
}

static inline cJSON *color_to_json(Color *c, bool verbose)
{
	if (!verbose)
		return num4_to_json(c->r, c->g, c->b, c->a);
	return num4_to_json_object("r", c->r, "g", c->g, "b", c->b, "a", c->a);
}

static inline cJSON *vec3_point_to_json(vec3 v, bool verbose)
{
	if (!verbose)
		return num3_to_json(v[0], v[1], v[2]);
	return num3_to_json_object("x", v[0], "y", v[1], "z", v[2]);
}

static inline cJSON *vec3_color_to_json(vec3 v, bool verbose)
{
	if (!verbose)
		return num3_to_json(v[0], v[1], v[2]);
	return num3_to_json_object("r", v[0], "g", v[1], "b", v[2]);
}

#endif
