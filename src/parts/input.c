/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include "system4.h"
#include <stdlib.h>

#include "asset_manager.h"
#include "audio.h"
#include "input.h"
#include "xsystem4.h"
#include "parts_internal.h"
#include "vm.h"

// true between calls to BeginClick and EndClick
bool parts_began_click = false;

// the mouse position at last update
static Point parts_prev_pos = {0};
// true when the left mouse button was down last update
static bool prev_clicking = false;
// the last (fully) clicked parts number
static int clicked_parts = 0;
// the current (partially) clicked parts number
static int click_down_parts = 0;

/* [D119 probe] CPartsFunctionSet@CallFunctionMouseClick dispatches on the
 * third message variable (KeyCode): 1 -> this[7], 2 -> this[8], 4 -> this[9].
 * xsystem4 passes VK_LBUTTON (1); Rance10's AIN may expect 4 for left click.
 * XSYS4_KEYCODE_PROBE=N overrides it for a controlled A/B test. */
static int click_keycode(void)
{
	static int k = -1;
	if (k < 0) {
		const char *e = getenv("XSYS4_KEYCODE_PROBE");
		k = e ? atoi(e) : VK_LBUTTON;
	}
	return k;
}

// drag state
static struct parts *drag_parts = NULL;
static bool is_dragging = false;
static Point drag_initial_pos;
static Point drag_start_cursor;
static struct parts *drop_target = NULL;

/* [D123] 命中测试必须按"当前" z 从高到低遍历，不能直接走
 * PARTS_LIST_FOREACH_REVERSE。
 *
 * parts_list 的顺序在部件插入时就定下来了，那时子活动的父链还没挂，
 * global.z 全是 0/1；之后 PE_RefreshGlobalTree() 重算了 global.z
 * （用户组件容器 gz=13、其子活动按钮 gz=16），却没有重排链表——
 * 实测逐个 resort 也会留下乱序，全量 remove/insert 又会触发精灵重复
 * 释放导致核心转储。最安全的做法是在输入阶段现算 z、现排序，不动
 * 全局链表与精灵注册。
 *
 * 不这么做的话，父容器会抢在本该盖在它上面的子按钮之前消费
 * hover/click，点击落到没注册委托的容器 part，DG_CALL 打空炮。 */
static int parts_input_z(struct parts *p)
{
	return parts_multi_controller ? p->controller_no : p->global.z;
}

static int parts_input_z2(struct parts *p)
{
	return parts_multi_controller ? p->global.z : 0;
}

static int parts_input_z_cmp(const void *a, const void *b)
{
	struct parts *pa = *(struct parts *const *)a;
	struct parts *pb = *(struct parts *const *)b;
	int za = parts_input_z(pa), zb = parts_input_z(pb);
	if (za != zb)
		return za > zb ? -1 : 1;   /* 高 z 优先 */
	int z2a = parts_input_z2(pa), z2b = parts_input_z2(pb);
	if (z2a != z2b)
		return z2a > z2b ? -1 : 1;
	/* 同层保持稳定：编号大的（后创建的）在前 */
	return pa->no > pb->no ? -1 : (pa->no < pb->no ? 1 : 0);
}

static struct parts **input_z_sorted;
static int input_z_cap;

static struct parts **parts_input_collect(int *out_nr)
{
	int nr = 0;
	struct parts *p;
	PARTS_LIST_FOREACH(p) {
		if (nr == input_z_cap) {
			input_z_cap = input_z_cap ? input_z_cap * 2 : 64;
			input_z_sorted = xrealloc_array(input_z_sorted, nr,
					input_z_cap, sizeof(*input_z_sorted));
		}
		input_z_sorted[nr++] = p;
	}
	if (nr > 1)
		qsort(input_z_sorted, nr, sizeof(*input_z_sorted), parts_input_z_cmp);
	*out_nr = nr;
	return input_z_sorted;
}

static bool parts_hittest(struct parts *parts, int state, Point pos)
{
	Rectangle hitbox = parts->states[state].common.hitbox;
	if (parts->parent) {
		hitbox.x += parts->parent->global.pos.x;
		hitbox.y += parts->parent->global.pos.y;
	}
	return SDL_PointInRect(&pos, &hitbox);
}

static void drag_state_reset(void)
{
	drag_parts = NULL;
	is_dragging = false;
	drop_target = NULL;
}

void parts_input_reset_drag(struct parts *parts)
{
	if (parts == drag_parts)
		drag_state_reset();
	else if (parts == drop_target)
		drop_target = NULL;
}

static void parts_update_mouse(struct parts *parts, Point cur_pos, bool cur_clicking,
		int passed_time, bool *hover_consumed, bool *click_consumed)
{
	// Always use DEFAULT state hitbox regardless of the current display state.
	/* [D128] 隐藏的图层不参与命中（否则旧场景的按钮还挡在新场景前面）。 */
	bool is_hovered = parts_is_visible(parts)
		&& parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos)
		&& !*hover_consumed;

	bool was_hovered = parts->is_hovered;
	parts->is_hovered = is_hovered;

	// !pass_cursor parts consume the cursor for parts behind them
	if (is_hovered && !parts->pass_cursor)
		*hover_consumed = true;

	if (parts->linked_from >= 0 && is_hovered != was_hovered) {
		parts_dirty(parts_get(parts->linked_from));
	}

	if (!parts_began_click)
		return;

	if (is_hovered && !was_hovered) {
		parts_msg_push(parts, PARTS_MSG_MOUSE_ENTER, "ii", cur_pos.x, cur_pos.y);
		parts->hover_time = 0;
	}
	if (!is_hovered && was_hovered) {
		parts_msg_push(parts, PARTS_MSG_MOUSE_LEAVE, "ii", cur_pos.x, cur_pos.y);
	}

	if (is_hovered) {
		// MOUSE_ON message fires every frame while hovering
		parts_msg_push(parts, PARTS_MSG_MOUSE_ON,
				"iii", cur_pos.x, cur_pos.y, parts->hover_time);
		parts->hover_time += passed_time;

		// MOUSE_MOVE message fires when cursor moves while hovering
		if (cur_pos.x != parts_prev_pos.x || cur_pos.y != parts_prev_pos.y) {
			parts_msg_push(parts, PARTS_MSG_MOUSE_MOVE, "ii", cur_pos.x, cur_pos.y);
		}
	}

	if (!is_hovered) {
		parts_set_state(parts, PARTS_STATE_DEFAULT);
		return;
	}

	bool click_eligible = parts->clickable || !parts->pass_cursor;
	if (!click_eligible || *click_consumed) {
		if (!was_hovered)
			audio_play_sound(parts->on_cursor_sound);
		parts_set_state(parts, PARTS_STATE_HOVERED);
		return;
	}

	// click down: first eligible part captures the click
	if (cur_clicking && !prev_clicking) {
		click_down_parts = parts->no;
		*click_consumed = true;

		drag_parts = parts;
		drag_initial_pos = parts->local.pos;
		drag_start_cursor = cur_pos;

		// KEY_TRIGGER message fires on press transition
		parts_msg_push(parts, PARTS_MSG_KEY_TRIGGER, "i", VK_LBUTTON);
	}

	// KEY_DOWN message fires every frame while held (not first frame)
	if (prev_clicking && cur_clicking && click_down_parts == parts->no) {
		parts_msg_push(parts, PARTS_MSG_KEY_DOWN, "i", VK_LBUTTON);
	}

	if (cur_clicking && click_down_parts == parts->no) {
		parts_set_state(parts, PARTS_STATE_CLICKED);
	} else {
		if (!was_hovered) {
			audio_play_sound(parts->on_cursor_sound);
		}
		parts_set_state(parts, PARTS_STATE_HOVERED);
	}

	/* [D113] GetClickNumber() reports the parts that captured this click,
	 * independent of Parts_SetClickable. clickable only gates the CLICK/KEY_UP
	 * messages (button callbacks). xsystem4 tied both to clickable, which
	 * makes scenes whose parts are all 点击許可=0 -- e.g. Rance10's
	 * "click anywhere to continue" start screen -- impossible to leave:
	 * parts::detail::WaitForClick polls GetClickNumber() > 0 forever.
	 * click_down_parts is only set for click_eligible parts above, so this
	 * still respects pass_cursor / front-to-back consumption. */
	if (prev_clicking && !cur_clicking && click_down_parts == parts->no) {
		clicked_parts = parts->no;
		/* [D114] Queue the CLICK/KEY_UP messages for every captured click,
		 * not just for clickable parts.  The AIN side registers
		 * "whole screen" mouse delegates (AFL_Parts_AddWholeMouseLClickEvent)
		 * on the parts message manager; SceneStack@Join uses one of them to
		 * call SceneStack@Exit, which is the only writer of IsFinish.
		 * Rance10's start screen has 点击許可=0 on all three parts ("click
		 * anywhere to continue"), so gating the message on clickable left
		 * IsFinish=0 forever: WaitForClick kept exiting on GetClickNumber,
		 * Join immediately re-entered 効果設定待ち, and Scenes::RunTitle
		 * never started.  clickable still controls the button sound. */
		if (parts->clickable)
			audio_play_sound(parts->on_click_sound);
		/* [D115] 用 PARTS_MSG_MOUSE_CLICK（=5）发出去，AIN 的
		 * CPartsMessageManager@CallDelegate 会落到 CallDelegateMouseDoubleClick
		 * ——AIN 侧的 switch 编号与 xsystem4 枚举错位，左键点击是 4。
		 * 实测 4 → CallDelegateMouseClick → SceneStack@RegisterEvent 的 lambda，
		 * 这是 start screen 唯一能收到点击的入口。 */
		parts_msg_push(parts, PARTS_MSG_MOUSE_CLICK_AIN,
				"iii", cur_pos.x, cur_pos.y, click_keycode());
		parts_msg_push(parts, PARTS_MSG_KEY_UP, "i", VK_LBUTTON);
		/* [D115] 同一此点击还要以“全屏”身份入队一份：AIN 的
		 * AFL_Parts_AddWholeMouseLClickEvent 系列把委托注册在 parts_no=0 的
		 * 函数集上，只有 parts_no==0 的消息才会派发到它们。
		 * SceneStack@RegisterEvent 注册左键 lambda → SceneStack@Exit →
		 * IsFinish，是 start screen 推进到标题场景的另一条入口。 */
		parts_msg_push(NULL, PARTS_MSG_MOUSE_CLICK_AIN,
				"iii", cur_pos.x, cur_pos.y, click_keycode());
	}
}

void PE_UpdateInputState(int passed_time);

/* [D120] 帧心跳的输入阶段。
 *
 * sact_Update() 与 vm_frame_pump() 都必须经过这里。此前只有 sact_Update 调
 * PE_UpdateInputState，而 AIN 在 parts::detail::WaitForClick 这类纯 AIN 等待
 * 循环里自旋时永远回不到 sact_Update：SDL 事件被 handle_events() 泵进来，却没
 * 有人跑命中测试，clicked_parts 恒为 0，于是"点一下没反应"随机发生——
 * 实际取决于那一刻 AIN 恰好停在哪个循环里。 */
void PE_PumpInput(int passed_time)
{
	handle_events();
	PE_UpdateInputState(passed_time);
}

void PE_UpdateInputState(int passed_time)
{
	Point cur_pos;
	/* [D120] 输入窗口没开（AIN 尚未 PartsEngine.BeginInput）时不要把按键
	 * 脉冲消费掉。parts_update_mouse() 在 !parts_began_click 时直接 return，
	 * 这一轮拿到的脉冲不会被任何人用掉，却被永久清除——于是"BeginInput
	 * 之前 0.2 秒到达的点击"必然丢失，表现为随机点不动。
	 * 留着脉冲，等输入窗口开启后的第一轮再消费。 */
	bool pulse = parts_began_click && mouse_consume_press_pulse();
	bool cur_clicking = key_is_down(VK_LBUTTON) || pulse;
	mouse_get_pos(&cur_pos.x, &cur_pos.y);

	/* [D113] input probe: XSYS4_INPUT_DBG=1 prints the first frames of every
	 * update so a lost click can be attributed to coordinates, hitbox,
	 * began_click, or the clickable flag. */
	static int dbg_n;
	int dbg = getenv("XSYS4_INPUT_DBG") &&
		(dbg_n < 40 || dbg_n % 60 == 0 || cur_clicking || prev_clicking || clicked_parts);
	if (dbg) {
		dbg_n++;
		NOTICE("[inp] pos=(%d,%d) clicking=%d pulse=%d began=%d prev=%d down=%d",
		       cur_pos.x, cur_pos.y, cur_clicking, pulse, parts_began_click,
		       prev_clicking, click_down_parts);
	}

	bool hover_consumed = false;
	bool click_consumed = false;
	/* [D123] 排序后的候选表，命中与调试打印共用同一顺序。 */
	int sorted_nr = 0;
	struct parts **sorted = parts_input_collect(&sorted_nr);
	struct parts *parts;
	if (dbg) {
		for (int si = 0; si < sorted_nr; si++) {
			parts = sorted[si];
			Rectangle hb = parts->states[PARTS_STATE_DEFAULT].common.hitbox;
			/* [D123] 命中判定实际用的是"局部 hitbox + 父 global.pos"，
			 * 只打局部值会误判成"子活动部件都在 0,0 命中不到"。 */
			int gx = hb.x, gy = hb.y;
			if (parts->parent) {
				gx += parts->parent->global.pos.x;
				gy += parts->parent->global.pos.y;
			}
			NOTICE("[inp]   parts=%d pos=(%d,%d) hb=(%d,%d,%d,%d) "
			       "global=(%d,%d) parent=%d gz=%d click=%d pass=%d show=%d "
			       "ctrl=%d uc=%s",
			       parts->no, parts->local.pos.x, parts->local.pos.y,
			       hb.x, hb.y, hb.w, hb.h, gx, gy,
			       parts->parent ? parts->parent->no : -1,
			       parts->global.z, parts->clickable, parts->pass_cursor,
			       parts->global.show, parts->controller_no,
			       parts->uc_object ? parts->uc_object : "-");
		}
	}
	// Highest z first: the sorted list is front-to-back.
	for (int si = 0; si < sorted_nr; si++) {
		parts = sorted[si];
		parts_update_mouse(parts, cur_pos, cur_clicking, passed_time,
				&hover_consumed, &click_consumed);
	}

	// Drag movement processing
	if (drag_parts && cur_clicking) {
		bool cursor_moved = (cur_pos.x != parts_prev_pos.x ||
				cur_pos.y != parts_prev_pos.y);
		if (cursor_moved && drag_parts->draggable) {
			Point new_pos = {
				drag_initial_pos.x + (cur_pos.x - drag_start_cursor.x),
				drag_initial_pos.y + (cur_pos.y - drag_start_cursor.y)
			};
			parts_set_pos(drag_parts, new_pos);
			if (!is_dragging) {
				parts_msg_push(drag_parts, PARTS_MSG_DRAG_BEGIN, "");
			}
			parts_msg_push(drag_parts, PARTS_MSG_DRAGGING, "iiii",
					drag_start_cursor.x, drag_start_cursor.y, cur_pos.x, cur_pos.y);
			is_dragging = true;
		}

		// Drop target tracking
		if (cursor_moved && is_dragging) {
			struct parts *new_drop = NULL;
			PARTS_LIST_FOREACH_REVERSE(parts) {
				if (parts == drag_parts)
					continue;
				if (parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos)) {
					new_drop = parts;
					if (!parts->pass_cursor)
						break;
				}
			}
			if (new_drop != drop_target) {
				if (drop_target) {
					parts_msg_push(drop_target, PARTS_MSG_DROP_LEAVE,
							"i", drag_parts->no);
				}
				if (new_drop) {
					parts_msg_push(new_drop, PARTS_MSG_DROP_ENTER,
							"i", drag_parts->no);
				}
				drop_target = new_drop;
			} else if (drop_target) {
				parts_msg_push(drop_target, PARTS_MSG_DROP_ON,
						"iii", drag_parts->no, cur_pos.x, cur_pos.y);
			}
		}
	}

	// Release handling
	if (prev_clicking && !cur_clicking) {
		if (is_dragging && drag_parts && drag_parts->draggable) {
			parts_msg_push(drag_parts, PARTS_MSG_DRAG_END, "");
		}
		if (drop_target && drag_parts) {
			parts_msg_push(drop_target, PARTS_MSG_DROPPED,
					"iii", drag_parts->no, cur_pos.x, cur_pos.y);
			parts_msg_push(drop_target, PARTS_MSG_DROP_LEAVE, "i", drag_parts->no);
		}
		drag_state_reset();

		if (!click_down_parts) {
			// TODO: play misclick sound
		}
		click_down_parts = 0;
	}

	if (dbg && clicked_parts)
		NOTICE("[inp]   CLICKED parts=%d", clicked_parts);
	/* [D120] 输入窗口关闭时不要推进"上一次按下状态"。一次点击的按下与抬起
	 * 可能跨在两个窗口里：按下发生在 A 窗口，抬起发生在 B 窗口。若在窗口
	 * 之外照样把 prev_clicking 更新成"未按下"，抬起沿就被吃掉了，
	 * clicked_parts 永远不置位 —— 表现正是"点了没反应，但重试几次偶尔能中"。
	 * 冻结在窗口内的最后状态，边缘检测只在窗口开的时候前进。 */
	if (parts_began_click)
		prev_clicking = cur_clicking;
	parts_prev_pos = cur_pos;
}

void PE_SetPassCursor(int parts_no, bool pass)
{
	parts_get(parts_no)->pass_cursor = !!pass;
}

bool PE_GetPartsPassCursor(int parts_no)
{
	return parts_get(parts_no)->pass_cursor;
}

void PE_SetClickable(int parts_no, bool clickable)
{
	parts_get(parts_no)->clickable = !!clickable;
}

bool PE_GetPartsClickable(int parts_no)
{
	return parts_get(parts_no)->clickable;
}

void PE_SetPartsGroupDecideOnCursor(possibly_unused int group_no, possibly_unused bool decide_on_cursor)
{
	UNIMPLEMENTED("(%d, %s)", group_no, decide_on_cursor ? "true" : "false");
}

void PE_SetPartsGroupDecideClick(possibly_unused int group_no, possibly_unused bool decide_click)
{
	UNIMPLEMENTED("(%d, %s)", group_no, decide_click ? "true" : "false");
}

void PE_SetOnCursorShowLinkPartsNumber(int parts_no, int link_parts_no)
{
	struct parts *parts = parts_get(parts_no);
	struct parts *link_parts = parts_get(link_parts_no);
	parts->linked_to = link_parts_no;
	link_parts->linked_from = parts_no;
}

int PE_GetOnCursorShowLinkPartsNumber(int parts_no)
{
	return parts_get(parts_no)->linked_to;
}

bool PE_SetPartsOnCursorSoundNumber(int parts_no, int sound_no)
{
	if (!asset_exists(ASSET_SOUND, sound_no)) {
		WARNING("Invalid sound number: %d", sound_no);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	parts->on_cursor_sound = sound_no;
	return true;
}

bool PE_SetPartsClickSoundNumber(int parts_no, int sound_no)
{
	if (!asset_exists(ASSET_SOUND, sound_no)) {
		WARNING("Invalid sound number: %d", sound_no);
		return false;
	}

	struct parts *parts = parts_get(parts_no);
	parts->on_click_sound = sound_no;
	return true;
}

bool PE_SetClickMissSoundNumber(possibly_unused int sound_no)
{
	UNIMPLEMENTED("(%d)", sound_no);
	return true;
}

bool PE_IsInputBegun(void)
{
	return parts_began_click;
}

void PE_BeginInput(void)
{
	parts_began_click = true;
}

void PE_EndInput(void)
{
	/* [D158] 同样不设配额：只有"这一帧真的捕获到点击"才打。
	 * 这行和 GetClickNumber 的非零行成对出现才对；只有 EndInput 有、
	 * GetClickNumber 没有 ⇒ AIN 在 DoInput 窗口里没轮询点击号。 */
	if (getenv("XSYS4_INPUT_DBG") && clicked_parts)
		NOTICE("[inp] EndInput (clicked=%d)", clicked_parts);
	parts_began_click = false;
	clicked_parts = 0;
	drag_state_reset();
}

void PE_SetDrag(int parts_no, bool enable)
{
	parts_get(parts_no)->draggable = !!enable;
}

int PE_GetClickPartsNumber(void)
{
	/* [D158] 只按内容过滤，**不设总量配额**。
	 *
	 * 旧写法是 `n < 200 && (n < 30 || clicked_parts)`：启动阶段 30 帧就把
	 * "必打"名额烧光，之后只在 clicked_parts 非零时才打。上一轮据此得出
	 * "GetClickNumber 恒为 0、AIN 没轮询点击"的结论 —— 其实是探针自己
	 * 哑了（同一个坑第四次）。现在：非零必打，另加每 300 次一条心跳，
	 * 用来确认 AIN 到底还在不在轮询。 */
	if (getenv("XSYS4_INPUT_DBG")) {
		static int n = 0;
		if (clicked_parts)
			NOTICE("[inp] GetClickNumber #%d -> %d", n, clicked_parts);
		else if (n % 300 == 0)
			NOTICE("[inp] GetClickNumber #%d -> 0 (poll alive)", n);
		n++;
	}
	return clicked_parts;
}

bool PE_IsCursorIn(int parts_no, int mouse_x, int mouse_y, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;

	Point mouse_pos = { mouse_x, mouse_y };
	return parts_hittest(parts, state, mouse_pos);
}
