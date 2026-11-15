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

#include "system4/string.h"

#include "hll.h"
#include "input.h"
#include "vm.h"

static void handle_events_throttled(void)
{
	// Big Bang Age uses busy loops to wait input events. To prevent taking 100%
	// CPU time, sleep 10ms every 30 input event checks.
	static int count;
	if (++count == 30) {
		count = 0;
		vm_sleep(10);
	}
	handle_events();
}

HLL_WARN_UNIMPLEMENTED( , void, InputDevice, Init, void *imainsystem);

void InputDevice_ClearKeyDownFlag(void)
{
	key_clear_flag(false);
}

static int InputDevice_IsKeyDown(int key)
{
	handle_events_throttled();
	return key_is_down(key);
}

static int InputDevice_GetCsrPos(int *x, int *y)
{
	handle_events_throttled();
	mouse_get_pos(x, y);
	return mouse_focus && keyboard_focus;
}

static int InputDevice_SetCsrPos(int x, int y)
{
	handle_events_throttled();
	mouse_set_pos(x, y);
	return mouse_focus && keyboard_focus;
}

static void InputDevice_ClearWheelCount(void)
{
	mouse_clear_wheel();
}

static int InputDevice_GetWheelCountForward(void)
{
	int forward, back;
	handle_events_throttled();
	mouse_get_wheel(&forward, &back);
	return forward;
}

static int InputDevice_GetWheelCountBack(void)
{
	int forward, back;
	handle_events_throttled();
	mouse_get_wheel(&forward, &back);
	return back;
}

HLL_LIBRARY(InputDevice,
			HLL_EXPORT(Init, InputDevice_Init),
			HLL_EXPORT(ClearKeyDownFlag, InputDevice_ClearKeyDownFlag),
			HLL_EXPORT(IsKeyDown, InputDevice_IsKeyDown),
			HLL_EXPORT(GetCsrPos, InputDevice_GetCsrPos),
			HLL_EXPORT(SetCsrPos, InputDevice_SetCsrPos),
			HLL_EXPORT(ClearWheelCount, InputDevice_ClearWheelCount),
			HLL_EXPORT(GetWheelCountForward, InputDevice_GetWheelCountForward),
			HLL_EXPORT(GetWheelCountBack, InputDevice_GetWheelCountBack));
