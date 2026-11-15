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

#include "hll.h"

HLL_WARN_UNIMPLEMENTED(, void, vmGraphQuake, SetMode, int mode);
//void vmGraphQuake_SetAmp(int nAmp);
HLL_WARN_UNIMPLEMENTED(, void, vmGraphQuake, SetAmpX, int amp_x);
HLL_WARN_UNIMPLEMENTED(, void, vmGraphQuake, SetAmpY, int amp_y);
//void vmGraphQuake_SetCycle(int nCycle);
//int vmGraphQuake_GetMode(void);
//int vmGraphQuake_GetAmp(void);
//int vmGraphQuake_GetAmpX(void);
//int vmGraphQuake_GetAmpY(void);
//int vmGraphQuake_GetCycle(void);

HLL_LIBRARY(vmGraphQuake,
	    HLL_EXPORT(SetMode, vmGraphQuake_SetMode),
	    HLL_TODO_EXPORT(SetAmp, vmGraphQuake_SetAmp),
	    HLL_EXPORT(SetAmpX, vmGraphQuake_SetAmpX),
	    HLL_EXPORT(SetAmpY, vmGraphQuake_SetAmpY),
	    HLL_TODO_EXPORT(SetCycle, vmGraphQuake_SetCycle),
	    HLL_TODO_EXPORT(GetMode, vmGraphQuake_GetMode),
	    HLL_TODO_EXPORT(GetAmp, vmGraphQuake_GetAmp),
	    HLL_TODO_EXPORT(GetAmpX, vmGraphQuake_GetAmpX),
	    HLL_TODO_EXPORT(GetAmpY, vmGraphQuake_GetAmpY),
	    HLL_TODO_EXPORT(GetCycle, vmGraphQuake_GetCycle)
	    );
