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

#include "xsystem4.h"
#include "input.h"
#include "hll.h"

static int ADVSYS_Init(void)
{
	return 1;
}

static int ADVSYS_KEY_IsDown(int key)
{
	return key_is_down(key);
}

static int ADVSYS_SYSTEM_IsResetOnce(void)
{
	// TODO
	return 0;
}

HLL_LIBRARY(ADVSYS,
	    HLL_EXPORT(Init, ADVSYS_Init),
	    HLL_EXPORT(KEY_IsDown, ADVSYS_KEY_IsDown),
	    HLL_EXPORT(SYSTEM_IsResetOnce, ADVSYS_SYSTEM_IsResetOnce));
