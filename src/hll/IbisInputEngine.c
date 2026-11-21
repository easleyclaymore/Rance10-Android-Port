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

#include <SDL3/SDL.h>

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

/* [D494] 手柄三件套。AIN 签名（dump-hll 类型表）：
 *   int   Joystick_GetNumofDevice()            （args=0 / ret int）
 *   bool  Joystick_IsKeyDown(int dev, int code)（args=2 / ret bool）
 *   float Joystick_GetAxis(int dev, int axis)  （args=2 / ret float）
 *
 * 消费点：IbisInputEngine 是键盘/手柄输入的 AIN 侧门面；主流程每章调
 * IsKeyDown ~2500 次（hllhot 实测）——stub 时代恒 false，等于"没有手柄"，
 * 键鼠玩家无感，插手柄的玩家没反应。
 *
 * 映射判据：没有运行期证据说明 JoystickCode 的编码表（原版 Ibis 在
 * Windows DirectInput 上有自己的常量）。选**直通映射**：code 即 SDL 按钮
 * 下标、axis 即 SDL 轴下标，越界返回 false/0.0f 并在 XSYS4_INPUT_DBG 下
 * 留痕。语义注释里写明这是占位映射，等有手柄实测再对表。 */
static SDL_JoystickID ibis_ids[8];
static SDL_Joystick *ibis_opened[8];
static int ibis_nr_ids = -1;   /* -1 = 还没枚举过 */

static void ibis_refresh(void)
{
	if (ibis_nr_ids >= 0)
		return;
	int n = 0;
	SDL_JoystickID *p = SDL_GetJoysticks(&n);
	ibis_nr_ids = 0;
	if (p) {
		for (int i = 0; i < n && ibis_nr_ids < 8; i++)
			ibis_ids[ibis_nr_ids++] = p[i];
		SDL_free(p);
	}
	/* 设备清单只在首次调用时枚举（不做热插拔重扫；需要时再补）。 */
}

static SDL_Joystick *ibis_joystick(int device_number)
{
	ibis_refresh();
	if (device_number < 0 || device_number >= ibis_nr_ids)
		return NULL;
	if (!ibis_opened[device_number])
		ibis_opened[device_number] = SDL_OpenJoystick(ibis_ids[device_number]);
	return ibis_opened[device_number];
}

static int IbisInputEngine_Joystick_GetNumofDevice(void)
{
	ibis_refresh();
	return ibis_nr_ids;
}

bool IbisInputEngine_Joystick_IsKeyDown(int DeviceNumber, int JoystickCode)
{
	SDL_Joystick *js = ibis_joystick(DeviceNumber);
	if (!js)
		return false;
	int nr = SDL_GetNumJoystickButtons(js);
	if (JoystickCode < 0 || JoystickCode >= nr) {
		if (getenv("XSYS4_INPUT_DBG"))
			NOTICE("[joy] IsKeyDown dev=%d code=%d (nr_buttons=%d) out of range",
			       DeviceNumber, JoystickCode, nr);
		return false;
	}
	return SDL_GetJoystickButton(js, JoystickCode);
}

float IbisInputEngine_Joystick_GetAxis(int DeviceNumber, int AxisType)
{
	SDL_Joystick *js = ibis_joystick(DeviceNumber);
	if (!js)
		return 0.0f;
	int nr = SDL_GetNumJoystickAxes(js);
	if (AxisType < 0 || AxisType >= nr) {
		if (getenv("XSYS4_INPUT_DBG"))
			NOTICE("[joy] GetAxis dev=%d axis=%d (nr_axes=%d) out of range",
			       DeviceNumber, AxisType, nr);
		return 0.0f;
	}
	return SDL_GetJoystickAxis(js, AxisType) / 32767.0f;
}

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
