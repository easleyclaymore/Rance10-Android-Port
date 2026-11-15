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

#ifndef SYSTEM4_DUNGEON_SKYBOX_H
#define SYSTEM4_DUNGEON_SKYBOX_H

#include <cglm/types.h>

enum draw_dungeon_version;
struct dtx;
struct skybox;

struct skybox *skybox_create(enum draw_dungeon_version version, struct dtx *dtx);
void skybox_free(struct skybox *s);
void skybox_render(struct skybox *s, const mat4 view_transform, const mat4 proj_transform);

#endif /* SYSTEM4_DUNGEON_SKYBOX_H */
