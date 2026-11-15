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
#include "input.h"
#include "sact.h"

bool IbisInputEngine_Mouse_MovePosImmediately(int x, int y)
{
	mouse_set_pos(x, y);
	return true;
}

bool IbisInputEngine_Mouse_HideCursorByGame(bool hide)
{
	return mouse_show_cursor(!hide);
}

//bool Mouse_IsHideCursorByGame(void);
//void Mouse_HideByStepMessage(void);
HLL_QUIET_UNIMPLEMENTED(, void, IbisInputEngine, Mouse_HideByStepMessage);

static void IbisInputEngine_Joystick_ClearCaptureFlag(void)
{
	// TODO
}

//int IbisInputEngine_Joystick_GetNumofDevice(void);
HLL_WARN_UNIMPLEMENTED(0, int,  IbisInputEngine, Joystick_GetNumofDevice);

bool IbisInputEngine_Joystick_IsKeyDown(int DeviceNumber, int JoystickCode)
{
	// TODO
	return false;
}

//float IbisInputEngine_Joystick_GetAxis(int DeviceNumber, int AxisType);
HLL_WARN_UNIMPLEMENTED(0.0, float, IbisInputEngine, Joystick_GetAxis, int dev, int axis);

HLL_LIBRARY(IbisInputEngine,
	    HLL_EXPORT(Mouse_GetPos, sact_Mouse_GetPos),
	    HLL_EXPORT(Mouse_MovePosImmediately, IbisInputEngine_Mouse_MovePosImmediately),
	    HLL_EXPORT(Mouse_HideCursorByGame, IbisInputEngine_Mouse_HideCursorByGame),
	    HLL_TODO_EXPORT(Mouse_IsHideCursorByGame, IbisInputEngine_Mouse_IsHideCursorByGame),
	    HLL_EXPORT(Mouse_HideByStepMessage, IbisInputEngine_Mouse_HideByStepMessage),
	    HLL_EXPORT(MouseWheel_ClearCount, mouse_clear_wheel),
	    HLL_EXPORT(MouseWheel_GetCount, mouse_get_wheel),
	    HLL_EXPORT(Key_IsDown, sact_Key_IsDown),
	    HLL_EXPORT(Joystick_ClearCaptureFlag, IbisInputEngine_Joystick_ClearCaptureFlag),
	    HLL_EXPORT(Joystick_GetNumofDevice, IbisInputEngine_Joystick_GetNumofDevice),
	    HLL_EXPORT(Joystick_IsKeyDown, IbisInputEngine_Joystick_IsKeyDown),
	    HLL_EXPORT(Joystick_GetAxis, IbisInputEngine_Joystick_GetAxis));
