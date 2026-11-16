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

#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <SDL.h>
#include "system4.h"
#include "queue.h"
#include "gfx/gfx.h"
#include "gfx/private.h"
#include "input.h"
#include "scene.h"
#include "vm.h"
#include "xsystem4.h"
#include "debugger.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

bool key_state[VK_NR_KEYCODES];

/* [D113] SDL 把鼠标按下/抬起报成状态变化。一次短点击可能
 * 让两个事件落在同一次事件泵里，于是按帧轮询 key_state 会看到
 * 「从没按下过」。把这次按下记成一个一次性脉冲，
 * 部件输入层就仍然能看到一次「按下 -> 抬起」的边沿。 */
static bool mouse_press_pulse = false;

bool mouse_consume_press_pulse(void)
{
	bool p = mouse_press_pulse;
	mouse_press_pulse = false;
	return p;
}

/* [D233] 每个键的一次性「按下脉冲」。
 *
 * xdotool 的 click / rclick / key 都在**同一帧**里完成 press+release：
 * 事件泵把两个事件一起吃进来，`key_state[code]` 立刻回到 false，而 AIN
 * 只有在自己的指令流里轮询 `Key_IsDown()` 才能看到按键 —— 于是永远读到 0。
 * 兰斯10 的回合切换用 `CASClick`（右键）/ `SpaceKeyClick`（空格）轮询，
 * 结果就是画面停在 Round 演出上、怎么点都不走。
 *
 * 这里给每个键留一个一次性脉冲，只由 AIN 侧的 `sact_Key_IsDown()` 消费：
 * 读到一次 true 之后清掉，等价于「这次按下至少被观察到一帧」。 */
static bool key_press_pulse[VK_NR_KEYCODES];

/* [D233] 合成点击「先被看见、再抬起」。
 *
 * xdotool 的 `click`/`rclick`/`key` 把 press 与 release 塞进**同一次事件泵**，
 * 于是 `key_state` 在同一帧里 true→false，AIN 下次轮询时已经是抬起状态。
 * 兰斯10 的 `CASClick` 是边沿检测（按下记一帧，**抬起那一帧**才返回 true），
 * 一次都看不见就永远等不到 —— 回合切换就卡在 Round 演出上。
 *
 * 光延长按住时间不够：AIN 的等待循环每轮要做一次 view 更新，实测轮询率
 * 只有 4 次/秒，60ms 的窗口大概率被跳过。所以改成状态机：
 *   按下 → 保持 down，直到某一处 `key_is_down()` 真的读到 true；
 *   抬起 → 若还没被读到过就挂起，等读到之后再抬。
 * 这样每个合成点击保证被观测到「一次按下 + 一次抬起」，与轮询快慢无关。
 * 兜底 300ms：若这段时间根本没人读键盘（过场 CG 之类），强制抬起，
 * 不让键状态卡住。
 */
#define KEY_RELEASE_FALLBACK_MS 300
static bool key_seen_down[VK_NR_KEYCODES];
static bool key_release_pending[VK_NR_KEYCODES];
static uint32_t key_press_tick[VK_NR_KEYCODES];

static void key_note_press(int code)
{
	if (code < 0 || code >= VK_NR_KEYCODES)
		return;
	key_state[code] = true;
	key_seen_down[code] = false;
	key_release_pending[code] = false;
	key_press_tick[code] = SDL_GetTicks();
}

static void key_note_release(int code)
{
	if (code < 0 || code >= VK_NR_KEYCODES)
		return;
	if (key_seen_down[code])
		key_state[code] = false;
	else
		key_release_pending[code] = true;
}

static void key_flush_pending(void)
{
	uint32_t now = SDL_GetTicks();
	for (int i = 0; i < VK_NR_KEYCODES; i++) {
		if (!key_release_pending[i])
			continue;
		if (key_seen_down[i] ||
		    now - key_press_tick[i] >= KEY_RELEASE_FALLBACK_MS) {
			key_state[i] = false;
			key_release_pending[i] = false;
		}
	}
}

bool key_consume_press_pulse(int code)
{
	if (code < 0 || code >= VK_NR_KEYCODES || !key_press_pulse[code])
		return false;
	key_press_pulse[code] = false;
	return true;
}

/* [D191] 右键释放脉冲，语义同上面的左键脉冲。 */
static bool mouse_rclick_pulse = false;

bool mouse_consume_rclick_pulse(void)
{
	bool p = mouse_rclick_pulse;
	mouse_rclick_pulse = false;
	return p;
}
static enum sact_keycode sdl_keytable[] = {
	[SDL_SCANCODE_0] = VK_0,
	[SDL_SCANCODE_1] = VK_1,
	[SDL_SCANCODE_2] = VK_2,
	[SDL_SCANCODE_3] = VK_3,
	[SDL_SCANCODE_4] = VK_4,
	[SDL_SCANCODE_5] = VK_5,
	[SDL_SCANCODE_6] = VK_6,
	[SDL_SCANCODE_7] = VK_7,
	[SDL_SCANCODE_8] = VK_8,
	[SDL_SCANCODE_9] = VK_9,
	[SDL_SCANCODE_A] = VK_A,
	[SDL_SCANCODE_B] = VK_B,
	[SDL_SCANCODE_C] = VK_C,
	[SDL_SCANCODE_D] = VK_D,
	[SDL_SCANCODE_E] = VK_E,
	[SDL_SCANCODE_F] = VK_F,
	[SDL_SCANCODE_G] = VK_G,
	[SDL_SCANCODE_H] = VK_H,
	[SDL_SCANCODE_I] = VK_I,
	[SDL_SCANCODE_J] = VK_J,
	[SDL_SCANCODE_K] = VK_K,
	[SDL_SCANCODE_L] = VK_L,
	[SDL_SCANCODE_M] = VK_M,
	[SDL_SCANCODE_N] = VK_N,
	[SDL_SCANCODE_O] = VK_O,
	[SDL_SCANCODE_P] = VK_P,
	[SDL_SCANCODE_Q] = VK_Q,
	[SDL_SCANCODE_R] = VK_R,
	[SDL_SCANCODE_S] = VK_S,
	[SDL_SCANCODE_T] = VK_T,
	[SDL_SCANCODE_U] = VK_U,
	[SDL_SCANCODE_V] = VK_V,
	[SDL_SCANCODE_W] = VK_W,
	[SDL_SCANCODE_X] = VK_X,
	[SDL_SCANCODE_Y] = VK_Y,
	[SDL_SCANCODE_Z] = VK_Z,
	[SDL_SCANCODE_BACKSPACE] = VK_BACK,
	[SDL_SCANCODE_TAB] = VK_TAB,
	[SDL_SCANCODE_CLEAR] = VK_CLEAR,
	[SDL_SCANCODE_RETURN] = VK_RETURN,
	[SDL_SCANCODE_LSHIFT] = VK_SHIFT,
	[SDL_SCANCODE_RSHIFT] = VK_SHIFT,
	[SDL_SCANCODE_LCTRL] = VK_CONTROL,
	[SDL_SCANCODE_RCTRL] = VK_CONTROL,
	[SDL_SCANCODE_LALT] = VK_MENU,
	[SDL_SCANCODE_RALT] = VK_MENU,
	[SDL_SCANCODE_PAUSE] = VK_PAUSE,
	[SDL_SCANCODE_CAPSLOCK] = VK_CAPITAL,
	[SDL_SCANCODE_ESCAPE] = VK_ESCAPE,
	[SDL_SCANCODE_SPACE] = VK_SPACE,
	[SDL_SCANCODE_PAGEUP] = VK_PRIOR,
	[SDL_SCANCODE_PAGEDOWN] = VK_NEXT,
	[SDL_SCANCODE_END] = VK_END,
	[SDL_SCANCODE_HOME] = VK_HOME,
	[SDL_SCANCODE_LEFT] = VK_LEFT,
	[SDL_SCANCODE_UP] = VK_UP,
	[SDL_SCANCODE_RIGHT] = VK_RIGHT,
	[SDL_SCANCODE_DOWN] = VK_DOWN,
	[SDL_SCANCODE_SELECT] = VK_SELECT,
	[SDL_SCANCODE_EXECUTE] = VK_EXECUTE,
	[SDL_SCANCODE_PRINTSCREEN] = VK_SNAPSHOT,
	[SDL_SCANCODE_INSERT] = VK_INSERT,
	[SDL_SCANCODE_DELETE] = VK_DELETE,
	[SDL_SCANCODE_HELP] = VK_HELP,
	[SDL_SCANCODE_KP_0] = VK_NUMPAD0,
	[SDL_SCANCODE_KP_1] = VK_NUMPAD1,
	[SDL_SCANCODE_KP_2] = VK_NUMPAD2,
	[SDL_SCANCODE_KP_3] = VK_NUMPAD3,
	[SDL_SCANCODE_KP_4] = VK_NUMPAD4,
	[SDL_SCANCODE_KP_5] = VK_NUMPAD5,
	[SDL_SCANCODE_KP_6] = VK_NUMPAD6,
	[SDL_SCANCODE_KP_7] = VK_NUMPAD7,
	[SDL_SCANCODE_KP_8] = VK_NUMPAD8,
	[SDL_SCANCODE_KP_9] = VK_NUMPAD9,
	[SDL_SCANCODE_KP_MULTIPLY] = VK_MULTIPLY,
	[SDL_SCANCODE_KP_PLUS] = VK_ADD,
	[SDL_SCANCODE_KP_VERTICALBAR] = VK_SEPARATOR, // ???
	[SDL_SCANCODE_KP_MINUS] = VK_SUBTRACT,
	[SDL_SCANCODE_KP_PERIOD] = VK_DECIMAL,
	[SDL_SCANCODE_KP_DIVIDE] = VK_DIVIDE,
	[SDL_SCANCODE_KP_ENTER] = VK_RETURN,
	[SDL_SCANCODE_F1] = VK_F1,
	[SDL_SCANCODE_F2] = VK_F2,
	[SDL_SCANCODE_F3] = VK_F3,
	[SDL_SCANCODE_F4] = VK_F4,
	[SDL_SCANCODE_F5] = VK_F5,
	[SDL_SCANCODE_F6] = VK_F6,
	[SDL_SCANCODE_F7] = VK_F7,
	[SDL_SCANCODE_F8] = VK_F8,
	[SDL_SCANCODE_F9] = VK_F9,
	[SDL_SCANCODE_F10] = VK_F10,
	[SDL_SCANCODE_F11] = VK_F11,
	[SDL_SCANCODE_F12] = VK_F12,
	[SDL_SCANCODE_F13] = VK_F13,
	[SDL_SCANCODE_F14] = VK_F14,
	[SDL_SCANCODE_F15] = VK_F15,
	[SDL_SCANCODE_F16] = VK_F16,
	[SDL_SCANCODE_F17] = VK_F17,
	[SDL_SCANCODE_F18] = VK_F18,
	[SDL_SCANCODE_F19] = VK_F19,
	[SDL_SCANCODE_F20] = VK_F20,
	[SDL_SCANCODE_F21] = VK_F21,
	[SDL_SCANCODE_F22] = VK_F22,
	[SDL_SCANCODE_F23] = VK_F23,
	[SDL_SCANCODE_F24] = VK_F24,
	[SDL_SCANCODE_NUMLOCKCLEAR] = VK_NUMLOCK,
	[SDL_SCANCODE_SCROLLLOCK] = VK_SCROLL,
};

static enum sact_keycode sdl_to_sact_key(SDL_Scancode scancode, SDL_Keymod modifiers)
{
	if (!(modifiers & KMOD_NUM)) {
		switch (scancode) {
		case SDL_SCANCODE_KP_0:      return VK_INSERT;
		case SDL_SCANCODE_KP_1:      return VK_END;
		case SDL_SCANCODE_KP_2:      return VK_DOWN;
		case SDL_SCANCODE_KP_3:      return VK_NEXT;
		case SDL_SCANCODE_KP_4:      return VK_LEFT;
		case SDL_SCANCODE_KP_5:      return VK_CLEAR;
		case SDL_SCANCODE_KP_6:      return VK_RIGHT;
		case SDL_SCANCODE_KP_7:      return VK_HOME;
		case SDL_SCANCODE_KP_8:      return VK_UP;
		case SDL_SCANCODE_KP_9:      return VK_PRIOR;
		case SDL_SCANCODE_KP_PERIOD: return VK_DELETE;
		default: break;
		}
	}
	if (scancode >= sizeof(sdl_keytable) / sizeof(*sdl_keytable))
		return 0;
	return sdl_keytable[scancode];
}

bool mouse_focus = true;
bool keyboard_focus = true;

#define MAX_CONTROLLERS 4
static SDL_GameController *controllers[MAX_CONTROLLERS];

#ifdef __ANDROID__
struct deferred_keyevent {
	STAILQ_ENTRY(deferred_keyevent) entry;
	SDL_Event e;
};
static STAILQ_HEAD(deferred_keyevent_queue, deferred_keyevent) deferred_keyevent_queue =
	STAILQ_HEAD_INITIALIZER(deferred_keyevent_queue);
static uint32_t last_keyevent_timestamp;
#define DEFERRED_KEY_DELAY 10
#endif

// 存一个由触摸事件合成的鼠标按键事件（.timestamp != 0 时才有效）。
// 这类事件我们延后处理，免得游戏在读到指针位置之前
// 就先把按键按下事件处理掉了。
static SDL_MouseButtonEvent deferred_synthetic_mouse_event;
#define SYNTHETIC_MOUSE_EVENT_DELAY 50

static uint32_t long_touch_start_timestamp;
static SDL_FRect long_touch_finger_rect;
#define LONG_TOUCH_DURATION 1000

static float scroll_gesture_y;
#define SCROLL_GESTURE_SENSITIVITY 0.02f  // 2% of screen height

static enum sact_keycode sdl_to_sact_button(int button)
{
	switch (button) {
	case SDL_BUTTON_LEFT:   return VK_LBUTTON;
	case SDL_BUTTON_RIGHT:  return VK_RBUTTON;
	case SDL_BUTTON_MIDDLE: return VK_MBUTTON;
	default:                return 0;
	}
}

bool key_is_down(enum sact_keycode code)
{
	if (code < 0 || code >= VK_NR_KEYCODES) {
		return false;
	}
	if (key_state[code])
		key_seen_down[code] = true;   // [D233] 记下「这次按下被读到了」
	return key_state[code];
}

void key_clear_flag(bool no_ctrl)
{
	/* 清键状态时把未消费的按下脉冲一起丢掉：否则上一轮遗留的脉冲
	 * 会在之后的某次轮询里冒出一个幽灵按键（D233）。 */
	memset(key_press_pulse, 0, sizeof(key_press_pulse));
	memset(key_release_pending, 0, sizeof(key_release_pending));
	for (int i = 0; i < VK_NR_KEYCODES; i++) {
		if (no_ctrl && i == VK_CONTROL)
			continue;
		key_state[i] = false;
	}
}

void mouse_get_pos(int *x, int *y)
{
	int wx, wy;
	SDL_PumpEvents();
	SDL_GetMouseState(&wx, &wy);
	*x = (wx - sdl.viewport.x) * sdl.w / sdl.viewport.w;
	*y = (wy - sdl.viewport.y) * sdl.h / sdl.viewport.h;
}

void mouse_set_pos(int x, int y)
{
	int wx = x * sdl.viewport.w / sdl.w + sdl.viewport.x;
	int wy = y * sdl.viewport.h / sdl.h + sdl.viewport.y;
	SDL_WarpMouseInWindow(sdl.window, wx, wy);
}

static int wheel_dir = 0;

void mouse_get_wheel(int *forward, int *back)
{
	*forward = wheel_dir > 0;
	*back = wheel_dir < 0;
}

void mouse_clear_wheel(void)
{
	wheel_dir = 0;
}

bool mouse_show_cursor(bool show)
{
	return SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE) >= 0;
}

static void(*input_handler)(const char*);
static void(*editing_handler)(const char*, int, int);
static void(*key_handler)(int);

static void key_event(SDL_KeyboardEvent *e, bool pressed)
{
	enum sact_keycode code = sdl_to_sact_key(e->keysym.scancode, e->keysym.mod);
	if (code) {
		if (pressed) {
			key_note_press(code);
			key_press_pulse[code] = true;
			if (key_handler)
				key_handler(code);
		} else {
			key_note_release(code);
		}
	}
}

static void mouse_event(SDL_MouseButtonEvent *e)
{
	/* [D242] 只打按钮事件（mousemove 不走这里，量很小）。
	 * 目的：二分「X 层没把 ButtonPress 交给 SDL」和「SDL 收到了但引擎
	 * 边沿丢了」——s24/s26 的 `[inp]` 显示 began=1、候选表命中 716、
	 * 却始终没有 EndInput；而同一批跑测里 `key:Return` 能产生
	 * `[key] IsDown(1)=1`，说明 XTEST 与焦点是通的。 */
	if (getenv("XSYS4_MOUSE_DBG"))
		NOTICE("[mouse] btn=%d state=%d which=%d clicks=%d pos=(%d,%d) win=%u",
		       e->button, e->state, (int)e->which, e->clicks,
		       e->x, e->y, (unsigned)e->windowID);
	enum sact_keycode code = sdl_to_sact_button(e->button);
	if (code == VK_LBUTTON && e->state == SDL_PRESSED)
		mouse_press_pulse = true;
	/* [D191] 右键释放脉冲。xdotool 的 rclick 在同一帧内完成 press+release，
	 * 只读 key_state 会漏掉，必须留一个脉冲给帧泵消费。 */
	if (code == VK_RBUTTON && e->state == SDL_RELEASED)
		mouse_rclick_pulse = true;
	if (code) {
		if (e->state == SDL_PRESSED) {
			key_note_press(code);
			key_press_pulse[code] = true;
		} else {
			key_note_release(code);
		}
	}
#ifdef DEBUGGER_ENABLED
	if (e->button == SDL_BUTTON_MIDDLE && e->state == SDL_PRESSED
			&& (dbg_start_in_debugger || dbg_dap))
		dbg_repl(DBG_STOP_PAUSE, NULL);
#endif
}

static void synthetic_mouse_event(SDL_MouseButtonEvent *e)
{
	if (e->state == SDL_PRESSED) {
		mouse_press_pulse = true;
		// 视口之外的触摸当作右键。
		SDL_Point p = { .x = e->x, .y = e->y };
		if (SDL_PointInRect(&p, &sdl.viewport)) {
			key_note_press(VK_LBUTTON);
			key_press_pulse[VK_LBUTTON] = true;
		} else {
			key_note_press(VK_RBUTTON);
			key_press_pulse[VK_RBUTTON] = true;
		}
	} else {
		key_state[VK_LBUTTON] = false;
		key_state[VK_RBUTTON] = false;
	}
}

#define JOYAXIS_DEADZONE 13500

enum joyaxis_axis {
	JOYAXIS_X,
	JOYAXIS_Y
};

enum joyaxis_direction {
	JOYAXIS_DIR_LEFT_UP    = -1,
	JOYAXIS_DIR_NONE       =  0,
	JOYAXIS_DIR_RIGHT_DOWN =  1
};

enum joyaxis_state {
	// A：初始状态
	// - joy_clear_flag 被调用后进入长冷却，
	//   然后进入 JOYAXIS_STATE_B
	JOYAXIS_STATE_A,
	// B：按住状态
	// - joy_clear_flag 被调用后进入短冷却，
	//   方向不变就一直停在 JOYAXIS_STATE_B
	JOYAXIS_STATE_B,
};

struct joyaxis {
	enum joyaxis_state state;
	unsigned long cooldown_expire_time;
	enum joyaxis_direction real_direction;
	enum joyaxis_direction logical_direction;
};

struct joyaxis joyaxis_state[2] = { 0 };

bool joybutton_state[SDL_CONTROLLER_BUTTON_MAX];

static unsigned long joy_time(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((unsigned long)ts.tv_sec * 1000) + ((unsigned long)ts.tv_nsec / 1000000);
}

static bool joy_check_axis(struct joyaxis *axis, enum joyaxis_direction direction)
{
	if (axis->logical_direction == direction)
		return true;
	if (!axis->cooldown_expire_time)
		return false;
	// 冷却中：检查冷却是否到期
	if (joy_time() >= axis->cooldown_expire_time) {
		axis->state = JOYAXIS_STATE_B;
		axis->cooldown_expire_time = 0;
		axis->logical_direction = axis->real_direction;
	}
	return axis->logical_direction == direction;
}

bool joy_key_is_down(uint8_t code)
{
	// 下面是 SACT 默认的按键映射
	switch (code) {
	case 1: // Left Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_X], JOYAXIS_DIR_LEFT_UP);
	case 2: // Right Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_X], JOYAXIS_DIR_RIGHT_DOWN);
	case 3: // Up Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_Y], JOYAXIS_DIR_LEFT_UP);
	case 4: // Down Arrow
		return joy_check_axis(&joyaxis_state[JOYAXIS_Y], JOYAXIS_DIR_RIGHT_DOWN);
	case 5: // Esc
		return joybutton_state[SDL_CONTROLLER_BUTTON_A];
	case 6: // Enter
		return joybutton_state[SDL_CONTROLLER_BUTTON_B];
	case 7: // Space
		return joybutton_state[SDL_CONTROLLER_BUTTON_X];
	case 8: // Ctrl
		return joybutton_state[SDL_CONTROLLER_BUTTON_Y];
	case 9: // a
		return joybutton_state[SDL_CONTROLLER_BUTTON_LEFTSHOULDER];
	case 10: // z
		return joybutton_state[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER];
	case 11: // Page Up
		return joybutton_state[SDL_CONTROLLER_BUTTON_BACK];
	case 12: // Page Down
		return joybutton_state[SDL_CONTROLLER_BUTTON_START];
	case 13:
		return joybutton_state[SDL_CONTROLLER_BUTTON_GUIDE];
	case 14:
		return joybutton_state[SDL_CONTROLLER_BUTTON_LEFTSTICK];
	case 15:
		return joybutton_state[SDL_CONTROLLER_BUTTON_RIGHTSTICK];
	case 16:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_LEFT];
	case 17:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_RIGHT];
	case 18:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_UP];
	case 19:
		return joybutton_state[SDL_CONTROLLER_BUTTON_DPAD_DOWN];
	default:
		return false;
	}
}

static float joy_calc_deg(int x, int y)
{
	// 算出角度（度）
	float a = atan2f(y, x) * (180.0f/M_PI);
	if (a < 0)
		a += 360.0f;
	// 旋转，让「上」对应 0/360
	a += 90.0f;
	if (a > 360.0f)
		a -= 360.0f;
	return a;
}

static float joy_calc_pow(int x, int y)
{
	// XXX：理论上这里应该按角度缩放，让
	//      (32767,32767) 正好返回 1，但 alicesoft 自己
	//      似乎就是像下面这样直接夹取勾股值
	return min(1.0f, sqrtf(x*x + y*y) / 32767.0f);
}

static float joy_calc_trigger(int left, int right)
{
	// for whatever reason alicesoft treats triggers as an analog stick
	// （至少在手柄上是这样）
	float v = 315.0f;
	v += ((float)left/32767.0f) * 90.0f;
	v -= ((float)right/32767.0f) * 90.0f;
	if (v < 0)
		v += 360.0f;
	if (v > 360.0f)
		v -= 360.0f;
	return v;
}

bool joy_get_stick_status(int controller, int type, float *deg, float *pow)
{
	SDL_GameController *ctrl = controllers[controller];
	if (!ctrl)
		goto invalid;
	if (type == 0) {
		int x = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTX);
		int y = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTY);
		if (abs(x) < JOYAXIS_DEADZONE && abs(y) < JOYAXIS_DEADZONE) {
			*pow = 0.0f;
			*deg = 0.0f;
		} else {
			*pow = joy_calc_pow(x, y);
			*deg = joy_calc_deg(x, y);
		}
		return true;
	}
	if (type == 1) {
		int left = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
		int right = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
		*pow = 1.0f;
		*deg = joy_calc_trigger(left, right);
		return true;
	}
invalid:
	*deg = 0.0f;
	*pow = 0.0f;
	return false;
}

static void joyaxis_clear_flag(struct joyaxis *joy)
{
	// 让该轴进入冷却
	if (joy->logical_direction) {
		joy->logical_direction = 0;
		if (joy->state == JOYAXIS_STATE_A) {
			joy->cooldown_expire_time = joy_time() + 300;
		} else {
			joy->cooldown_expire_time = joy_time() + 50;
		}
	}
}

void joy_clear_flag(void)
{
	for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
		joybutton_state[i] = false;
	}
	joyaxis_clear_flag(&joyaxis_state[JOYAXIS_X]);
	joyaxis_clear_flag(&joyaxis_state[JOYAXIS_Y]);
}

static void joyaxis_update(enum joyaxis_axis axis, int value)
{
	// 确定方向
	int direction = 0;
	if (value < -JOYAXIS_DEADZONE)
		direction = -1;
	else if (value > JOYAXIS_DEADZONE)
		direction = 1;

	// 方向变了就重置轴状态
	if (direction != joyaxis_state[axis].real_direction) {
		joyaxis_state[axis].state = JOYAXIS_STATE_A;
		joyaxis_state[axis].cooldown_expire_time = 0;
		joyaxis_state[axis].real_direction = direction;
		joyaxis_state[axis].logical_direction = direction;
	}
}

static void controller_axis_event(SDL_ControllerAxisEvent *e)
{
	switch (e->axis) {
	case SDL_CONTROLLER_AXIS_LEFTX:
		joyaxis_update(JOYAXIS_X, e->value);
		break;
	case SDL_CONTROLLER_AXIS_LEFTY:
		joyaxis_update(JOYAXIS_Y, e->value);
		break;
	default:
		break;
	}
}

static void controller_button_event(SDL_ControllerButtonEvent *e)
{
	joybutton_state[e->button] = e->state == SDL_PRESSED;
}

void register_input_handler(void(*handler)(const char*))
{
	input_handler = handler;
}

void clear_input_handler(void)
{
	input_handler = NULL;
}

void register_editing_handler(void(*handler)(const char*, int, int))
{
	editing_handler = handler;
}

void clear_editing_handler(void)
{
	editing_handler = NULL;
}

void register_key_handler(void(*handler)(int))
{
	key_handler = handler;
}

void clear_key_handler(void)
{
	key_handler = NULL;
}

static void fire_deferred_events(void)
{
	uint32_t now = SDL_GetTicks();

	// 延后的鼠标按键事件超过 50ms 就强制发出。
	if (deferred_synthetic_mouse_event.timestamp &&
		deferred_synthetic_mouse_event.timestamp + SYNTHETIC_MOUSE_EVENT_DELAY < now) {
		synthetic_mouse_event(&deferred_synthetic_mouse_event);
		deferred_synthetic_mouse_event.timestamp = 0;
	}
	// 长按模拟按下 Ctrl 键。
	if (long_touch_start_timestamp && long_touch_start_timestamp + LONG_TOUCH_DURATION < now) {
		key_state[VK_LBUTTON] = false;
		key_state[VK_CONTROL] = true;
	}

#ifdef __ANDROID__
	// 发出延后的键盘与文本输入事件。
	while (!STAILQ_EMPTY(&deferred_keyevent_queue) && now >= last_keyevent_timestamp + DEFERRED_KEY_DELAY) {
		struct deferred_keyevent *ev = STAILQ_FIRST(&deferred_keyevent_queue);
		switch (ev->e.type) {
		case SDL_TEXTINPUT:
			if (input_handler)
				input_handler(ev->e.text.text);
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			last_keyevent_timestamp = now;
			key_event(&ev->e.key, ev->e.type == SDL_KEYDOWN);
			break;
		}
		STAILQ_REMOVE_HEAD(&deferred_keyevent_queue, entry);
		free(ev);
	}
#endif
}

static void handle_window_event(SDL_Event *e)
{
	switch (e->window.event) {
	case SDL_WINDOWEVENT_EXPOSED:
		gfx_swap();
		break;
	case SDL_WINDOWEVENT_ENTER:
		mouse_focus = true;
		break;
	case SDL_WINDOWEVENT_LEAVE:
		mouse_focus = false;
		break;
	case SDL_WINDOWEVENT_FOCUS_GAINED:
		keyboard_focus = true;
		break;
	case SDL_WINDOWEVENT_FOCUS_LOST:
		keyboard_focus = false;
		break;
	case SDL_WINDOWEVENT_SIZE_CHANGED:
		gfx_update_screen_scale();
		gfx_swap();
		break;
	}
}

void handle_window_events(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			vm_exit(0);
			break;
		case SDL_WINDOWEVENT:
			handle_window_event(&e);
			break;
		}
	}
}

void handle_events(void)
{
	key_flush_pending();
	fire_deferred_events();

	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			vm_exit(0);
			break;
		case SDL_WINDOWEVENT:
			handle_window_event(&e);
			break;
		case SDL_APP_DIDENTERFOREGROUND:
			gfx_swap();
			break;
		case SDL_KEYDOWN:
			if (e.key.keysym.scancode == SDL_SCANCODE_F9) {
				vm_stack_trace();
			} else if (e.key.keysym.scancode == SDL_SCANCODE_F11) {
				gfx_toggle_fullscreen();
			} else if (e.key.keysym.scancode == SDL_SCANCODE_S) {
				if (e.key.keysym.mod & (KMOD_LALT | KMOD_RALT)) {
					screenshot_save();
				}
			}
			// fallthrough
		case SDL_KEYUP:
#ifdef __ANDROID__
			if (input_handler && e.key.timestamp < last_keyevent_timestamp + DEFERRED_KEY_DELAY) {
				// 虚拟键盘的输入是一串连续的
				// SDL_KEYDOWN / SDL_KEYUP 事件。为了让游戏有机会
				// 看到上一个按键事件，这里把事件延后。
				struct deferred_keyevent *ev = xmalloc(sizeof(struct deferred_keyevent));
				ev->e = e;
				STAILQ_INSERT_TAIL(&deferred_keyevent_queue, ev, entry);
			} else {
				last_keyevent_timestamp = e.key.timestamp;
				key_event(&e.key, e.type == SDL_KEYDOWN);
			}
#else
			key_event(&e.key, e.type == SDL_KEYDOWN);
#endif
			break;
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEBUTTONDOWN:
			if (e.button.which == SDL_TOUCH_MOUSEID) {
				if (deferred_synthetic_mouse_event.timestamp)
					synthetic_mouse_event(&deferred_synthetic_mouse_event);
				deferred_synthetic_mouse_event = e.button;
			} else {
				mouse_event(&e.button);
			}
			break;
		case SDL_MOUSEWHEEL:
			wheel_dir = e.wheel.y;
			break;
		case SDL_FINGERDOWN:
			if (SDL_GetNumTouchFingers(e.tfinger.touchId) >= 2) {
				// 用户即将开始一个多点触控手势。
				key_state[VK_LBUTTON] = false;
				key_state[VK_RBUTTON] = false;
				deferred_synthetic_mouse_event.timestamp = 0;
				long_touch_start_timestamp = 0;
				scroll_gesture_y = -1.0f;
				break;
			}
			long_touch_start_timestamp = e.tfinger.timestamp;
			// 在这个矩形内的移动（距触摸起点 1% 屏幕尺寸）
			// 会被忽略。
			long_touch_finger_rect = (SDL_FRect) {
				.x = e.tfinger.x - 0.01,
				.y = e.tfinger.y - 0.01,
				.w = 0.02,
				.h = 0.02
			};
			break;
		case SDL_FINGERMOTION:
			if (SDL_PointInFRect(&(SDL_FPoint){ e.tfinger.x, e.tfinger.y }, &long_touch_finger_rect))
				break;
			// 只有在 Ctrl 模拟还没开始时才取消定时器。
			if (!key_state[VK_CONTROL])
				long_touch_start_timestamp = 0;
			break;
		case SDL_FINGERUP:
			long_touch_start_timestamp = 0;
			key_state[VK_CONTROL] = false;
			break;
		case SDL_MULTIGESTURE:
			if (e.mgesture.numFingers == 2) {
				if (scroll_gesture_y < 0.0f)
					scroll_gesture_y = e.mgesture.y;
				float dy = scroll_gesture_y - e.mgesture.y;
				if (dy * dy > SCROLL_GESTURE_SENSITIVITY * SCROLL_GESTURE_SENSITIVITY) {
					wheel_dir = dy < 0 ? 1 : -1;  // Swipe up to scroll down.
					scroll_gesture_y = e.mgesture.y;
				}
			}
		case SDL_CONTROLLERDEVICEADDED:
			if (e.cdevice.which < MAX_CONTROLLERS)
				controllers[e.cdevice.which] = SDL_GameControllerOpen(e.cdevice.which);
			break;
		case SDL_CONTROLLERAXISMOTION:
			controller_axis_event(&e.caxis);
			break;
		case SDL_CONTROLLERBUTTONUP:
		case SDL_CONTROLLERBUTTONDOWN:
			controller_button_event(&e.cbutton);
			break;
		case SDL_TEXTINPUT:
			if (!input_handler) {
				break;
#ifdef __ANDROID__
			} else if (!STAILQ_EMPTY(&deferred_keyevent_queue)) {
				// 按键事件与文本事件的顺序必须保持。
				struct deferred_keyevent *ev = xmalloc(sizeof(struct deferred_keyevent));
				ev->e = e;
				STAILQ_INSERT_TAIL(&deferred_keyevent_queue, ev, entry);
#endif
			} else {
				input_handler(e.text.text);
			}
			break;
		case SDL_TEXTEDITING:
			if (editing_handler)
				editing_handler(e.edit.text, e.edit.start, e.edit.length);
			break;
		default:
			break;
		}
	}
	if (dbg_dap)
		dbg_dap_handle_messages();
}
