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

#include "system4.h"
#include <stdlib.h>

#include "asset_manager.h"
#include "audio.h"
#include "input.h"
#include "xsystem4.h"
#include "parts_internal.h"
#include "vm.h"

// BeginClick 与 EndClick 之间为真
bool parts_began_click = false;

// 上次更新时的鼠标位置
static Point parts_prev_pos = {0};
// 上次更新时左键是否已经按下
static bool prev_clicking = false;
// 最近一次（完整）点击到的部件号
static int clicked_parts = 0;
// 本次（部分）点击到的部件号
static int click_down_parts = 0;

/* [D119 probe] CPartsFunctionSet@CallFunctionMouseClick dispatches on the
 * 第三个消息变量（KeyCode）：1 -> this[7]，2 -> this[8]，4 -> this[9]。
 * xsystem4 传的是 VK_LBUTTON（1）；兰斯10 的 AIN 可能期望左键是 4。
 * XSYS4_KEYCODE_PROBE=N 可以覆盖它，做受控 A/B。 */
static int click_keycode(void)
{
	static int k = -1;
	if (k < 0) {
		const char *e = getenv("XSYS4_KEYCODE_PROBE");
		k = e ? atoi(e) : VK_LBUTTON;
	}
	return k;
}

// 拖拽状态
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

/* [D207] 命中矩形 = 局部 hitbox + 直接父 global.pos。
 *
 * 抽成函数是为了让 parts_hittest() 和 [inp] 调试打印**共用同一套计算**：
 * 之前两处各算一遍，日志显示的命中区与实测结果可能不是同一个值，
 * 排查标题菜单时因此误判过一轮。
 *
 * 注意：XSYS4_PARTS_DUMP 打的是装载瞬间的 global.pos（例如 (548,718)），
 * 而输入帧里这个字段已被 parts_update_global_pos() 重算成 (0,40)；
 * 两者不是同一时刻的快照，别拿 dump 的坐标去点（D207 证伪记录见
 * docs/HYPOTHESES.md H018）。 */
static Rectangle parts_hitbox_at(struct parts *parts, int state)
{
	Rectangle hb = parts->states[state].common.hitbox;
	if (parts->parent) {
		hb.x += parts->parent->global.pos.x;
		hb.y += parts->parent->global.pos.y;
	}
	return hb;
}

static bool parts_hittest(struct parts *parts, int state, Point pos)
{
	Rectangle hb = parts_hitbox_at(parts, state);
	return SDL_PointInRect(&pos, &hb);
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
	// 不管当前显示状态是什么，命中盒一律用 DEFAULT 状态。
	/* [D128] 隐藏的图层不参与命中（否则旧场景的按钮还挡在新场景前面）。 */
	bool is_hovered = parts_is_visible(parts)
		&& parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos)
		&& !*hover_consumed;

	bool was_hovered = parts->is_hovered;
	parts->is_hovered = is_hovered;

	// !pass_cursor 的部件会替它下面的部件吃掉光标
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
		// 悬停时 MOUSE_ON 消息每帧都发
		parts_msg_push(parts, PARTS_MSG_MOUSE_ON,
				"iii", cur_pos.x, cur_pos.y, parts->hover_time);
		parts->hover_time += passed_time;

		// 悬停时鼠标一动就发 MOUSE_MOVE 消息
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

	// 按下：第一个有资格的部件捕获这次点击
	if (cur_clicking && !prev_clicking) {
		click_down_parts = parts->no;
		*click_consumed = true;

		drag_parts = parts;
		drag_initial_pos = parts->local.pos;
		drag_start_cursor = cur_pos;

		// 按下状态切换时发 KEY_TRIGGER 消息
		parts_msg_push(parts, PARTS_MSG_KEY_TRIGGER, "i", VK_LBUTTON);
	}

	// 按住期间 KEY_DOWN 消息每帧都发（首帧除外）
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

	/* [D113] GetClickNumber() 报告的是这次点击捕获到的部件，
	 * 与 Parts_SetClickable 无关。clickable 只门控 CLICK/KEY_UP
	 * 消息（按钮回调）。xsystem4 把两者都绑在 clickable 上，
	 * 结果那些所有部件都是 点击許可=0 的场景 —— 例如兰斯10 的
	 * 「点击任意处继续」开始画面 —— 再也离不开了：
	 * parts::detail::WaitForClick 会一直轮询 GetClickNumber() > 0。
	 * 上面只对 click_eligible 的部件设 click_down_parts，
	 * 所以这里仍然遵守 pass_cursor / 从前到后的消费顺序。 */
	if (prev_clicking && !cur_clicking && click_down_parts == parts->no) {
		clicked_parts = parts->no;
		/* [D114] 每一次捕获到的点击都要排 CLICK/KEY_UP 消息，
		 * 不只是可点击的部件。AIN 那边在部件消息管理器上注册了
		 * 「整屏」鼠标委托（AFL_Parts_AddWholeMouseLClickEvent）；
		 * SceneStack@Join 用其中一个去调 SceneStack@Exit，
		 * 而那是 IsFinish 唯一的写入者。
		 * 兰斯10 的开始画面三个部件都是 点击許可=0，
		 * 用 clickable 门控消息就让 IsFinish 永远为 0：
		 * WaitForClick 一直在 GetClickNumber 上退出，
		 * Join 立刻又进等待，Scenes::RunTitle 于是从没真正开始。
		 * clickable 仍控制按钮音效。 */
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

	/* [D113] 输入探针：XSYS4_INPUT_DBG=1 会打印每次更新的
	 * 前若干帧，这样丢掉的点击才能归因到坐标、命中盒、
	 * began_click 还是 clickable 标志。 */
	static int dbg_n;
	int dbg = getenv("XSYS4_INPUT_DBG") &&
		(dbg_n < 40 || dbg_n % 60 == 0 || cur_clicking || prev_clicking || clicked_parts);
	/* [D238] 计数器必须**每帧**递增，不能只在打印时递增。
	 * 原来 `dbg_n++` 写在 if (dbg) 里面：打完前 40 帧后 dbg_n 正好是 40，
	 * 40 % 60 != 0，于是"每 60 帧一条心跳"永远不成立 —— 探针自己静音，
	 * 读日志的人会以为"引擎再没跑过输入"。这与债 [PROBE-GATED] 同型。 */
	dbg_n++;
	if (dbg) {
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
			/* [D207] 与 parts_hittest 共用同一套坐标基准，
			 * 否则调试打印会把命中区显示在一个根本没被使用的位置。 */
			Rectangle hb = parts_hitbox_at(parts, PARTS_STATE_DEFAULT);
			int gx = hb.x, gy = hb.y;
			/* [D189] 只打光标覆盖到的部件：40 条限流会被无关部件吃光，
			 * 而"我点的位置到底有没有可点部件"才是判断按钮点不动的那条。 */
			bool covers = cur_pos.x >= gx && cur_pos.x < gx + hb.w &&
				cur_pos.y >= gy && cur_pos.y < gy + hb.h;
			/* [D207] XSYS4_INPUT_ALL=1：连"没覆盖光标"的部件也打出来。
			 * 只打覆盖部件时，一个几何算错的按钮会完全消失在日志里，
			 * 看起来像"这个部件不存在"——这正是标题菜单排查踩的坑。 */
			bool dump_all = getenv("XSYS4_INPUT_ALL") != NULL;
			if (!covers && !dump_all)
				continue;
			NOTICE("[inp]   parts=%d pos=(%d,%d) hb=(%d,%d,%d,%d) "
			       "global=(%d,%d) parent=%d gz=%d click=%d pass=%d show=%d "
			       "ctrl=%d cover=%d uc=%s",
			       parts->no, parts->local.pos.x, parts->local.pos.y,
			       hb.x, hb.y, hb.w, hb.h, gx, gy,
			       parts->parent ? parts->parent->no : -1,
			       parts->global.z, parts->clickable, parts->pass_cursor,
			       parts->global.show, parts->controller_no,
			       covers ? 1 : 0,
			       parts->uc_object ? parts->uc_object : "-");
		}
	}
	// z 最高的排前面：排序后的列表是从前到后。
	for (int si = 0; si < sorted_nr; si++) {
		parts = sorted[si];
		parts_update_mouse(parts, cur_pos, cur_clicking, passed_time,
				&hover_consumed, &click_consumed);
	}

	/* [D191] 右键。AIN 的 CPartsMessageManager@CallDelegateMouseClick 用第三个
	 * 消息变量（KeyCode）挑 delegate 列表：1→this[7]、2→this[8]、
	 * 4→this[9]。`AFL_Parts_AddWholeMouseRClickEvent` 注册的正是 index 8，
	 * 所以右键**不需要新的消息类型**，只要把 KeyCode 换成 VK_RBUTTON 发一条
	 * whole 消息。之前引擎从不发它：SceneStack@RegisterEvent 注册了右键
	 * lambda，但它永远收不到 → SceneParty@OnRClick → Close(24301) 不跑，
	 * 编队界面「戻る／決定」出不去，只能靠后门调 Close。 */
	if (parts_began_click && mouse_consume_rclick_pulse()) {
		parts_msg_push(NULL, PARTS_MSG_MOUSE_CLICK_AIN,
				"iii", cur_pos.x, cur_pos.y, VK_RBUTTON);
		parts_msg_push(NULL, PARTS_MSG_KEY_UP, "i", VK_RBUTTON);
	}

	// 处理拖拽移动
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

		// 跟踪放置目标
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

	// 处理释放
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
			// TODO：播一下点空的音效
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
