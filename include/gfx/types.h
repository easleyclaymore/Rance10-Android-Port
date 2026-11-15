/* 版权所有 (C) 1997-1998 Masaki Chikama (Wren) <chikama@kasumi.ipl.mech.nagoya-u.ac.jp>
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

#ifndef SYSTEM4_GRAPHICS_H
#define SYSTEM4_GRAPHICS_H

#include <stdint.h>
#include <SDL.h>
#include <cglm/types.h>

typedef SDL_Color Color;

#define COLOR(_r, _g, _b, _a) ((Color){.r=_r, .g=_g, .b=_b, .a=_a})

typedef SDL_Point Point;

#define POINT(_x, _y) ((Point){.x=_x, .y=_y})

typedef struct {
        int w;
        int h;
} Dimension;

#define DIM(_w, _h) ((Dimension){.w=_w, .h=_h})

typedef SDL_Rect Rectangle;

#define RECT(_x, _y, _w, _h) ((Rectangle){.x=_x, .y=_y, .w=_w, .h=_h})

#define MAT4(r0c0, r0c1, r0c2, r0c3, r1c0, r1c1, r1c2, r1c3, r2c0, r2c1, r2c2, r2c3, r3c0, r3c1, r3c2, r3c3) \
	{								\
		{ r0c0, r1c0, r2c0, r3c0 },				\
		{ r0c1, r1c1, r2c1, r3c1 },				\
		{ r0c2, r1c2, r2c2, r3c2 },				\
		{ r0c3, r1c3, r2c3, r3c3 }				\
	}

#define WV_TRANSFORM(w, h)			\
	MAT4(2.0 / w, 0,       0, -1,		\
	     0,       2.0 / h, 0, -1,		\
	     0,       0,       1, -1,		\
	     0,       0,       0,  1)

#define WORLD_TRANSFORM(w, h, x, y)	\
	MAT4(w, 0, 0, x,		\
	     0, h, 0, y,		\
	     0, 0, 1, 0,		\
	     0, 0, 0, 1)

#endif /* SYSTEM4_GRAPHICS_H */
