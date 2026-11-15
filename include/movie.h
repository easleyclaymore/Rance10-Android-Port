/* 版权所有 (C) 2023 kichikuou <KichikuouChrome@gmail.com>
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

#ifndef SYSTEM4_MOVIE_H
#define SYSTEM4_MOVIE_H

struct movie_context;
struct sact_sprite;

struct movie_context *movie_load(const char *filename);
void movie_free(struct movie_context *mc);
bool movie_play(struct movie_context *mc);
bool movie_draw(struct movie_context *mc, struct sact_sprite *sprite);
bool movie_is_end(struct movie_context *mc);
int movie_get_position(struct movie_context *mc);
bool movie_set_volume(struct movie_context *mc, int volume /* 0-100 */);

#endif /* SYSTEM4_MOVIE_H */
