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

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "system4.h"
#include "system4/cg.h"

#include "asset_manager.h"
#include "cJSON.h"
#include "gfx/gfx.h"
#include "json.h"
#include "queue.h"
#include "scene.h"
#include "xsystem4.h"

static int id_counter = 0;

bool scene_is_dirty = true;

static TAILQ_HEAD(listhead, sprite) sprite_list = TAILQ_HEAD_INITIALIZER(sprite_list);

static Texture wp = {0};

void scene_register_sprite(struct sprite *sp)
{
	if (sp->in_scene)
		return;

	if (!sp->id)
		sp->id = ++id_counter;

	struct sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (p->z == sp->z) {
			if (p->z2 > sp->z2) {
				TAILQ_INSERT_BEFORE(p, sp, entry);
				sp->in_scene = true;
				return;
			}
		} else if (p->z > sp->z) {
			TAILQ_INSERT_BEFORE(p, sp, entry);
			sp->in_scene = true;
			return;
		}
	}
	TAILQ_INSERT_TAIL(&sprite_list, sp, entry);
	sp->in_scene = true;
	scene_dirty();
}

void scene_unregister_sprite(struct sprite *sp)
{
	if (!sp->in_scene)
		return;
	TAILQ_REMOVE(&sprite_list, sp, entry);
	sp->in_scene = false;
	scene_dirty();
}

void scene_render(void)
{
	gfx_clear();
	if (wp.handle) {
		Rectangle r = RECT(0, 0, wp.w, wp.h);
		gfx_render_texture(&wp, &r);
	}

	struct sprite *sp;
	TAILQ_FOREACH(sp, &sprite_list, entry) {
		if (sp->render)
			sp->render(sp);
		else
			WARNING("sprite in scene without render function");
	}
}

int scene_set_wp(int cg_no) {
	if (!cg_no) {
		gfx_delete_texture(&wp);
		scene_dirty();
		return 1;
	}

	struct cg *cg = asset_cg_load(cg_no);
	if (!cg)
		return 0;

	gfx_delete_texture(&wp);
	gfx_init_texture_with_cg(&wp, cg);
	cg_free(cg);
	scene_dirty();
	return 1;
}

int scene_set_wp_color(int r, int g, int b)
{
	gfx_set_clear_color(r, g, b, 255);
	scene_dirty();
	return 1;
}

int scene_set_wp_texture(Texture *tex)
{
	gfx_delete_texture(&wp);
	gfx_init_texture_rgb(&wp, tex->w, tex->h, (SDL_Color){ 0, 0, 0, 255 });
	gfx_copy(&wp, 0, 0, tex, 0, 0, tex->w, tex->h);

	scene_dirty();
	return 1;
}

void scene_set_sprite_z(struct sprite *sp, int z)
{
	sp->z = z;
	if (sp->in_scene) {
		scene_unregister_sprite(sp);
		scene_register_sprite(sp);
	}
}

void scene_set_sprite_z2(struct sprite *sp, int z, int z2)
{
	sp->z = z;
	sp->z2 = z2;
	if (sp->in_scene) {
		scene_unregister_sprite(sp);
		scene_register_sprite(sp);
	}
}

void scene_print(void)
{
	struct sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		cJSON *json = p->to_json ? p->to_json(p, false) : scene_sprite_to_json(p, false);
		char *text = cJSON_Print(json);
		sys_message("%s\n", text);
		free(text);
		cJSON_Delete(json);
	}
}

cJSON *scene_to_json(bool verbose)
{
	cJSON *ar = cJSON_CreateArray();
	struct sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		cJSON *sprite = p->to_json ? p->to_json(p, verbose) : scene_sprite_to_json(p, verbose);
		cJSON_AddItemToArray(ar, sprite);
	}
	return ar;
}

struct sprite *scene_get(int id)
{
	struct sprite *p;
	TAILQ_FOREACH(p, &sprite_list, entry) {
		if (p->id == id)
			return p;
	}
	return NULL;
}
