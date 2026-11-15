/* 版权所有 (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#ifndef SYSTEM4_SCENE_H
#define SYSTEM4_SCENE_H

#include <stdbool.h>
#include "queue.h"

typedef struct cJSON cJSON;
struct texture;

struct sprite {
	TAILQ_ENTRY(sprite) entry;
	// 精灵在场景里的 Z 层
	int z;
	// 次级 Z 层（给 GoatGUIEngine 用）
	int z2;
	// 没有像素数据的精灵不会进场景。
	bool has_pixel;
	bool has_alpha;
	// 精灵被隐藏时会从场景里移除。
	bool hidden;
	// 这个标志表示精灵是否被收入当前场景：
	// 有像素或文本数据、且没被隐藏的精灵才应该在场景里。
	// （隐藏时会从场景里摘掉。）
	bool in_scene;
	// 唯一 ID
	int id;
	// 渲染函数。
	void (*render)(struct sprite*);
	// 调试打印函数
	cJSON *(*to_json)(struct sprite*, bool);
};

extern bool scene_is_dirty;

void scene_register_sprite(struct sprite *sp);
void scene_unregister_sprite(struct sprite *sp);
void scene_render(void);
int scene_set_wp(int cg_no);
int scene_set_wp_color(int r, int g, int b);
int scene_set_wp_texture(struct texture *tex);
void scene_set_sprite_z(struct sprite *sp, int z);
void scene_set_sprite_z2(struct sprite *sp, int z, int z2);

void scene_print(void);
cJSON *scene_to_json(bool verbose);
struct sprite *scene_get(int id);

static inline void scene_sprite_dirty(struct sprite *sp)
{
	if (!sp)
		return;
	scene_is_dirty = true;
	if (sp->hidden) {
		scene_unregister_sprite(sp);
	} else if (sp->has_pixel) {
		scene_register_sprite(sp);
	}
}

static inline void scene_dirty(void)
{
	scene_is_dirty = true;
}

static inline void scene_set_sprite_show(struct sprite *sp, bool show)
{
	sp->hidden = !show;
	scene_sprite_dirty(sp);
}

static inline bool scene_get_sprite_show(struct sprite *sp)
{
	return !sp->hidden;
}

#endif /* SYSTEM4_SCENE_H */
