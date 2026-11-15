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
#include "input.h"

HLL_QUIET_UNIMPLEMENTED( , void, vmCursor, Init, void *imainsystem);

static void vmCursor_SetPos(int x, int y)
{
	mouse_set_pos(x, y);
}

static int vmCursor_GetPos(int *x, int *y)
{
	handle_events();
	mouse_get_pos(x, y);
	return mouse_focus && keyboard_focus;
}

//void vmCursor_SetShow(int nShow);
//int vmCursor_GetShow(void);

HLL_LIBRARY(vmCursor,
	    HLL_EXPORT(Init, vmCursor_Init),
	    HLL_EXPORT(SetPos, vmCursor_SetPos),
	    HLL_EXPORT(GetPos, vmCursor_GetPos),
	    HLL_TODO_EXPORT(SetShow, vmCursor_SetShow),
	    HLL_TODO_EXPORT(GetShow, vmCursor_GetShow)
	    );
