/* 版权所有 (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

#include "input.h"
#include "hll.h"

#define TT3_CURSOR_TRANSPARENT 11

HLL_WARN_UNIMPLEMENTED(1, int, Cursor, Load, int nNum, int nLinkNum);

static void Cursor_Set(int num)
{
	switch (num) {
	case 0:
		mouse_show_cursor(true);
		break;
	case TT3_CURSOR_TRANSPARENT:
		mouse_show_cursor(false);
		break;
	default:
		WARNING("Cursor.Set: unexpected argument %d", num);
		break;
	}
}

HLL_LIBRARY(Cursor,
	    HLL_EXPORT(Load, Cursor_Load),
	    HLL_EXPORT(Set, Cursor_Set)
	    );
