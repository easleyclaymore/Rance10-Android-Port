/* 版权所有 (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#ifndef SYSTEM4_POLYOBJ_H
#define SYSTEM4_POLYOBJ_H

#include <stddef.h>
#include <stdint.h>
#include <cglm/types.h>

struct polyobj_location {
	uint32_t offset;
	uint32_t length;
};

struct polyobj {
	uint8_t *data;
	size_t size;
	int nr_textures;
	struct polyobj_location *textures;
	int nr_objects;
	struct polyobj_location *objects;
};

struct polyobj_triangle {
	int material;
	int index[3];
	vec2 uv[3];
};

struct polyobj_part {
	int nr_vertices;
	vec3 *vertices;
	int nr_triangles;
	struct polyobj_triangle *triangles;
};

struct polyobj_object {
	char *name;
	int nr_materials;
	int *materials;
	int nr_parts;
	struct polyobj_part *parts;
};

struct polyobj *polyobj_load(const char *path);
void polyobj_free(struct polyobj *po);
struct cg *polyobj_get_texture(struct polyobj *po, int index);
struct polyobj_object *polyobj_get_object(struct polyobj *po, int index);
void polyobj_object_free(struct polyobj_object *obj);

#endif /* SYSTEM4_POLYOBJ_H */
