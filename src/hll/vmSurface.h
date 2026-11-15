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

#ifndef SYSTEM4_HLL_VMSURFACE_H
#define SYSTEM4_HLL_VMSURFACE_H

#include "gfx/gfx.h"

struct vm_surface {
	struct texture texture;
	int no;
	int w;
	int h;
	bool has_pixel;
	bool has_alpha;
	int cg_no;
};

struct vm_surface *vm_surface_create(int width, int height);
struct vm_surface *vm_surface_get(int no);
void vm_surface_delete(int no);
int vm_surface_get_main_surface(void);

#endif /* SYSTEM4_HLL_VMSURFACE_H */
