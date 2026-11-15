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

#include "hll.h"
#include "gfx/gfx.h"

static int SystemServiceEx_GetOverlayPresent(void)
{
	return 0;
}

//int SystemServiceEx_GetUpdateResult(void);
HLL_WARN_UNIMPLEMENTED(0, int, SystemServiceEx, GetOverlayActive, void);
//void SystemServiceEx_UpdateMainSurface(int x, int y, int w, int h);
//int SystemServiceEx_GetHasPresented(void);
//void SystemServiceEx_ClearHasPresented(void);

HLL_LIBRARY(SystemServiceEx,
	    //HLL_EXPORT(GetUpdateResult, SystemServiceEx_GetUpdateResult),
	    HLL_EXPORT(GetOverlayPresent, SystemServiceEx_GetOverlayPresent),
	    HLL_EXPORT(GetOverlayActive, SystemServiceEx_GetOverlayActive),
	    //HLL_EXPORT(UpdateMainSurface, SystemServiceEx_UpdateMainSurface),
	    HLL_EXPORT(SetWaitVSync, gfx_set_wait_vsync)
	    //HLL_EXPORT(GetHasPresented, SystemServiceEx_GetHasPresented),
	    //HLL_EXPORT(ClearHasPresented, SystemServiceEx_ClearHasPresented)
	);
