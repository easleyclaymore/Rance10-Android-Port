/* gfx/private.h  SDL/OpenGL 专有的私有数据
 *
 * 版权所有 (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 * 版权所有 (C) 2000- Fumihiko Murata <fmurata@p1.tcnet.ne.jp>
 *
 * 本程序是自由软件：你可以依据自由软件基金会发布的
 * GNU 通用公共许可证（第 2 版，或你选择的任何更高版本）的条款，
 * 重新分发和/或修改本程序。
 * （或你选择的任何更高版本）。
 *
 * 分发本程序是希望它有用，但不作任何担保；
 * 甚至不担保适销性或适用于特定用途。
 * 详见 GNU 通用公共许可证了解更多。
 *
 *
 * 你应当已经随本程序收到一份 GNU 通用公共许可证的副本；
 * 如果没有，见 <http://gnu.org/licenses/>。
 */

#ifndef SYSTEM4_GFX_PRIVATE_H
#define SYSTEM4_GFX_PRIVATE_H

#include <stdbool.h>
#include <SDL3/SDL.h>
#include "gfx/gl.h"

struct sdl_private {
	SDL_Window *window;
	const SDL_PixelFormatDetails *format; /* [D329] SDL3：由 SDL 拥有，不要释放 */
	struct {
		SDL_GLContext context;
		GLuint vao;
		GLuint vbo;
		GLuint quad_vbo;
		GLuint rect_ibo;
		GLuint line_ibo;
	} gl;
	int w, h;
	SDL_Rect viewport;
	bool ms_active; /* mouse is active */
};
extern struct sdl_private sdl;

#endif /* SYSTEM4_GFX_PRIVATE_H */
