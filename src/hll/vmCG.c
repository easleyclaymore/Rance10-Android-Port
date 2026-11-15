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

#include "system4.h"
#include "system4/cg.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "hll.h"
#include "vmSurface.h"

HLL_QUIET_UNIMPLEMENTED( , void, vmCG, New, void);
//int vmCG_Release(void);
HLL_WARN_UNIMPLEMENTED( , void, vmCG, SetType, int type);
//int vmCG_EnumHandle(void);

static int vmCG_Exist(int num)
{
	return asset_exists(ASSET_CG, num);
}

static int vmCG_Load(int num)
{
	struct cg *cg = asset_cg_load(num);
	if (!cg)
		return 0;
	struct vm_surface *sf = vm_surface_create(cg->metrics.w, cg->metrics.h);
	gfx_init_texture_with_cg(&sf->texture, cg);
	sf->has_pixel = cg->metrics.has_pixel;
	sf->has_alpha = cg->metrics.has_alpha;
	sf->cg_no = num;
	cg_free(cg);
	return sf->no;
}

static void vmCG_Unload(int surface)
{
	vm_surface_delete(surface);
}

static int vmCG_Trans(int surface, int x, int y, int num)
{
	struct vm_surface *sf = vm_surface_get(surface);
	if (!sf)
		return 0;

	struct cg *cg = asset_cg_load(num);
	if (!cg)
		return 0;

	Texture src;
	gfx_init_texture_with_cg(&src, cg);
	cg_free(cg);

	if (sf->has_alpha)
		gfx_copy_with_alpha_map(&sf->texture, x, y, &src, 0, 0, src.w, src.h);
	else
		gfx_blend_amap(&sf->texture, x, y, &src, 0, 0, src.w, src.h);
	gfx_delete_texture(&src);
	return 1;
}

HLL_LIBRARY(vmCG,
	    HLL_EXPORT(New, vmCG_New),
	    HLL_TODO_EXPORT(Release, vmCG_Release),
	    HLL_EXPORT(SetType, vmCG_SetType),
	    HLL_TODO_EXPORT(EnumHandle, vmCG_EnumHandle),
	    HLL_EXPORT(Exist, vmCG_Exist),
	    HLL_EXPORT(Load, vmCG_Load),
	    HLL_EXPORT(Unload, vmCG_Unload),
	    HLL_EXPORT(Trans, vmCG_Trans)
	    );
