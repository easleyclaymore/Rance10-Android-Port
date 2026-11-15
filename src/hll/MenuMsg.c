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

#include "hll.h"

static bool enabled = false;
static int state = 0;

HLL_WARN_UNIMPLEMENTED( , void, MenuMsg, Init, void *imainsystem);

static void MenuMsg_SetState(int flag)
{
	state = flag;
}

static void MenuMsg_SetEnable(int enable)
{
	enabled = !!enable;
}

static int MenuMsg_GetState(void)
{
	return state;
}

static int MenuMsg_GetEnable(void)
{
	return enabled;
}

HLL_LIBRARY(MenuMsg,
			HLL_EXPORT(Init, MenuMsg_Init),
			HLL_EXPORT(SetState, MenuMsg_SetState),
			HLL_EXPORT(SetEnable, MenuMsg_SetEnable),
			HLL_EXPORT(GetState, MenuMsg_GetState),
			HLL_EXPORT(GetEnable, MenuMsg_GetEnable));
