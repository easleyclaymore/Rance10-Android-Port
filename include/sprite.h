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

#ifndef SYSTEM4_SPRITE_H
#define SYSTEM4_SPRITE_H

#include <stdbool.h>
#include <SDL.h>
#include "queue.h"
#include "gfx/gfx.h"
#include "scene.h"

struct string;
struct page;
union vm_value;
struct text_style;

struct sact_sprite {
	struct sprite sp;
	LIST_ENTRY(sact_sprite) entry;
	// 精灵的纹理（CG 或纯色）。延迟初始化。
	struct texture texture;
	// 没挂 CG 时，用它填充纯色。
	SDL_Color color;
	// 着色器参数
	int blend_rate;
	SDL_Color multiply_color;
	SDL_Color add_color;
	// 绘制方式（混合模式）
	enum draw_method draw_method;
	// 精灵的位置与尺寸。
	Rectangle rect;
	// 精灵的可见区域
	Rectangle surface_area;
	// 可以挂任意文本：它画在另一张纹理上，
	// 再叠加到精灵上。
	struct {
		struct string *str;
		struct texture texture;
		Point home;
		Point pos;
		int char_space;
		int line_space;
		int current_line_height;
	} text;
	// 精灵句柄
	int no;
	// 挂在精灵上的 CG 号。
	int cg_no;
	// 精灵被挂起。确切含义不明。
	bool suspended;
	// （可选）绑定到该精灵的绘制插件。
	struct draw_plugin *plugin;
};

/*
 * XXX：这里有两套精灵渲染器：
 *        * SACT2 —— 混合率 + 亮度（亮度塞在 multiply_color 字段里）
 *        * ChipmunkSpriteEngine —— 混合率、add color、multiply color、surface area
 *          （走部件系统的着色器）
 *
 *      用哪一套由初始化函数决定。
 */
void sprite_init_sact(void);
void sprite_init_chipmunk(void);

static inline void sprite_dirty(struct sact_sprite *sp)
{
	scene_sprite_dirty(&sp->sp);
}

void sprite_free(struct sact_sprite *sp);
struct texture *sprite_get_texture(struct sact_sprite *sp);
void sprite_set_cg(struct sact_sprite *sp, struct cg *cg);
void sprite_set_cg_2x(struct sact_sprite *sp, struct cg *cg);
int sprite_set_cg_from_asset(struct sact_sprite *sp, int cg_no);
int sprite_set_cg_2x_from_asset(struct sact_sprite *sp, int cg_no);
int sprite_set_cg_by_name(struct sact_sprite *sp, const char *name);
int sprite_set_cg_from_file(struct sact_sprite *sp, const char *path);
int sprite_save_cg(struct sact_sprite *sp, const char *path);
void sprite_init(struct sact_sprite *sp);
void sprite_init_color(struct sact_sprite *sp, int w, int h, int r, int g, int b, int a);
void sprite_init_custom(struct sact_sprite *sp);
void sprite_set_pos(struct sact_sprite *sp, int x, int y);
void sprite_set_x(struct sact_sprite *sp, int x);
void sprite_set_y(struct sact_sprite *sp, int y);
static inline void sprite_set_z(struct sact_sprite *sp, int z) { scene_set_sprite_z(&sp->sp, z); }
static inline void sprite_set_z2(struct sact_sprite *sp, int z, int z2) { scene_set_sprite_z2(&sp->sp, z, z2); }
static inline int sprite_get_blend_rate(struct sact_sprite *sp) { return sp->blend_rate; }
void sprite_set_blend_rate(struct sact_sprite *sp, int rate);
void sprite_set_multiply_color(struct sact_sprite *sp, int r, int g, int b);
void sprite_set_add_color(struct sact_sprite *sp, int r, int g, int b);
void sprite_set_surface_area(struct sact_sprite *sp, int x, int y, int w, int h);
static inline void sprite_set_show(struct sact_sprite *sp, bool show) { scene_set_sprite_show(&sp->sp, show); }
int sprite_set_draw_method(struct sact_sprite *sp, enum draw_method method);
enum draw_method sprite_get_draw_method(struct sact_sprite *sp);
static inline int sprite_exists_alpha(struct sact_sprite *sp) { return sp->sp.has_alpha; }
static inline int sprite_get_pos_x(struct sact_sprite *sp) { return sp->rect.x; }
static inline int sprite_get_pos_y(struct sact_sprite *sp) { return sp->rect.y; }
static inline int sprite_get_width(struct sact_sprite *sp) { return sp->rect.w; }
static inline int sprite_get_height(struct sact_sprite *sp) { return sp->rect.h; }
static inline int sprite_get_z(struct sact_sprite *sp) { return sp->sp.z; }
static inline int sprite_get_z2(struct sact_sprite *sp) { return sp->sp.z2; }
static inline int sprite_get_show(struct sact_sprite *sp) { return !sp->sp.hidden; }
void sprite_set_text_home(struct sact_sprite *sp, int x, int y);
void sprite_set_text_line_space(struct sact_sprite *sp, int px);
void sprite_set_text_char_space(struct sact_sprite *sp, int px);
void sprite_set_text_pos(struct sact_sprite *sp, int x, int y);
void sprite_text_draw(struct sact_sprite *sp, struct string *text, struct text_style *ts);
void sprite_text_clear(struct sact_sprite *sp);
void sprite_text_home(struct sact_sprite *sp, int size);
void sprite_text_new_line(struct sact_sprite *sp, int size);
void sprite_text_copy(struct sact_sprite *dsp, struct sact_sprite *ssp);
static inline int sprite_get_text_home_x(struct sact_sprite *sp) { return sp->text.home.x; }
static inline int sprite_get_text_home_y(struct sact_sprite *sp) { return sp->text.home.y; }
static inline int sprite_get_text_char_space(struct sact_sprite *sp) { return sp->text.char_space; }
static inline int sprite_get_text_pos_x(struct sact_sprite *sp) { return sp->text.pos.x; }
static inline int sprite_get_text_pos_y(struct sact_sprite *sp) { return sp->text.pos.y; }
static inline int sprite_get_text_line_space(struct sact_sprite *sp) { return sp->text.line_space; }
bool sprite_is_point_in(struct sact_sprite *sp, int x, int y);
bool sprite_is_point_in_rect(struct sact_sprite *sp, int x, int y);
int sprite_get_amap_value(struct sact_sprite *sp, int x, int y);
void sprite_get_pixel_value(struct sact_sprite *sp, int x, int y, int *r, int *g, int *b);
void sprite_bind_plugin(struct sact_sprite *sp, struct draw_plugin *plugin);
void sprite_call_plugins(void);

#endif /* SYSTEM4_SPRITE_H */
