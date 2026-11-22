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
/* [D336] 本帧刚捕获到一次点击（`EndInput` 会把 clicked_parts 清零，
 * 所以驱动 TextButton 必须紧跟捕获，不能等到帧末）。 */
static bool g_just_clicked;

/* [D498] 「装饰 CG 让位」开关（默认开）。
 *
 * 判据与用途见 click_eligible 处的注释：按钮的高亮/底板 CG 压在按钮
 * 宿主上时，让点击穿过它落到宿主。`XSYS4_DECOR_PASS=0` 回退旧行为。 */
static bool parts_decor_pass_enabled(void)
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("XSYS4_DECOR_PASS");
		on = !(e && !strcmp(e, "0"));
	}
	return on;
}
// 最近一次（完整）点击到的部件号
static int clicked_parts = 0;
// 本次（部分）点击到的部件号
static int click_down_parts = 0;

/* [D309] 光标当前**指向**的部件号（最上层、可见、命中）。
 *
 * 原版引擎有「active parts」概念：`PartsEngine.GetActiveParts` 返回它会话里
 * 被指着的部件，AIN 的 `parts::detail::GetActivePartsType`(8308) 再拿它去
 * `GetComponentType(parts, 1)` 求组件类型，返回 -1 表示「没有」。
 * `parts::detail::WaitForClick`(8309) / `parts::IsInputPartsActive`(7243)
 * 就是靠这一对判断「这次点击算不算数」。
 *
 * D202 把它实现成恒返回 0 的桩，注释说「点击推进仍由 GetClickNumber 负责」——
 * 但那正是开场回想对话「過後查看」点不动的原因：类型恒为 -1，
 * WaitForClick 永远认为没有可点部件，于是 `TextButton@Motion` 一次都不跑。
 * 这里把它补成真实语义：本帧悬停消费顺序里第一个命中的部件。 */
static int active_parts_no = 0;

int parts_input_active_parts(void)
{
	static int off = -1;
	if (off < 0) {
		const char *e = getenv("XSYS4_ACTIVE_PARTS");
		off = (e && !strcmp(e, "0"));
	}
	return off ? 0 : active_parts_no;
}

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

/* [D496] 输入处理参与开关（AIN `SetEnableInputProcess`）的门控判定。
 *
 * 契约（离线 xref，见 parts_internal.h 字段注释）：AIN 侧只写不读，
 * 消费方是原版引擎 C++ 输入处理器。AIN 自己的输入组件过滤
 * `IsInputPartsActive(7243)` 按**类型 {4,7,11}** 判定——那就是"输入
 * 组件族"（文本框/组合框/微调框一族），本门控用同一类型集：
 *   - 只看 `component_type >= 0`（显式设过）的部件，反推值不算；
 *   - flag=0 时该部件不拦光标、不捕获点击（`is_hovered` 不动——
 *     D490 的教训：接收事件与捕获点击是两个语义）；
 *   - clickable 语义原样保留（D165 教训：映射成 clickable 全局生效
 *     会把活动文件点击許可打开的按钮全关掉，整局卡死在任务地图）。
 * `XSYS4_EIP_GATE=0` 回退（转正闸，判据：D165 场景回归——活动装载
 * 107 个、任务地图可进战斗）。 */
static bool eip_gate(void)
{
	static int v = -1;
	if (v < 0) {
		const char *e = getenv("XSYS4_EIP_GATE");
		v = !(e && !strcmp(e, "0"));
	}
	return v;
}

static bool parts_input_disabled(struct parts *parts)
{
	if (!eip_gate() || parts->enable_input_process)
		return false;
	int ct = parts->states[PARTS_STATE_DEFAULT].component_type;
	return ct == 4 || ct == 7 || ct == 11;
}

/* 输入是否只发给「活动层」（默认开，`XSYS4_INPUT_ACTIVE_LAYER=0` 回退）。
 *
 * 语义依据：`PartsEngine.Update` 是按**活动控制器**处理的 —— 只有活动层的
 * 部件会被跑命中测试与消息派发。我们的移植一直没做这一层过滤，只用
 * `clickable`/`pass_cursor` 这类逐件启发式兜，结果是**下层场景的按钮能
 * 吃掉上层场景的点击**：D498 实测打开设置菜单后，点 (1500,828) 抓到的是
 * 标题画面的 `900000048`（TextButton，坐标根本不在它的框里）—— 因为它属于
 * 还在栈里的标题层，而菜单层才是 active。
 *
 * 注意：`PARTS_CONTROLLER_SYSTEM_OVERLAY` 是引擎自己的覆盖层，永远参与。 */
static bool input_active_layer_only(void)
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("XSYS4_INPUT_ACTIVE_LAYER");
		on = !(e && !strcmp(e, "0"));
	}
	return on;
}

static struct parts **parts_input_collect(int *out_nr)
{
	int nr = 0;
	struct parts *p;
	int active_idx = -1;
	if (input_active_layer_only())
		active_idx = PE_get_active_controller_index();
	PARTS_LIST_FOREACH(p) {
		if (active_idx >= 0 && p->controller_no != active_idx &&
		    p->controller_no != PARTS_CONTROLLER_SYSTEM_OVERLAY)
			continue;
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

/* [D397] 对外暴露命中盒：排查「按钮点不中」的第一步是知道按钮到底在哪。
 * 以前只能从 `[pev]` 的 `hb=` 反推，而那行要求**先点中**才打 ——
 * 点不中时恰好什么都看不到。`XSYS4_DUMP_BTN` 用它列出场景全部 TextButton。 */
bool PE_PartsHitbox(int parts_no, int *x, int *y, int *w, int *h)
{
	if (parts_no <= 0)
		return false;
	struct parts *p = parts_get(parts_no);
	if (!p)
		return false;
	Rectangle r = parts_hitbox_at(p, PARTS_STATE_DEFAULT);
	if (x) *x = r.x;
	if (y) *y = r.y;
	if (w) *w = r.w;
	if (h) *h = r.h;
	return true;
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
	/* [D331] 点击捕获归因探针（`XSYS4_CAP_DBG=1`）。
	 *
	 * 用途单一：回答「按下那一帧，光标下的部件各自是什么状态、为什么没人
	 * 拿到 click_down_parts」。之前只能靠 `[inp] down=0` 知道"没人捕获"，
	 * 但捕获链有四级前提（可见 / 命中 / hover 没被吃掉 / 资格判据），
	 * 没有逐级快照就只能猜。这里把四项一次性打出来，并且**只在按下那帧、
	 * 且命中盒真的覆盖光标**时打，量很小。 */
	if (getenv("XSYS4_CAP_DBG") && cur_clicking && !prev_clicking) {
		Rectangle dbghb = parts_hitbox_at(parts, PARTS_STATE_DEFAULT);
		if (cur_pos.x >= dbghb.x && cur_pos.x < dbghb.x + dbghb.w &&
		    cur_pos.y >= dbghb.y && cur_pos.y < dbghb.y + dbghb.h)
			/* [D472] 加 `di=`（delegate_index）与 `hblock=`（本件是否
			 * 会拦光标）。判据「自己没有任何点击委托的全屏件不该吃光标」
			 * 需要这个数：`delegate_index < 0` 的件，AIN 的
			 * CPartsMessageManager@CallDelegate 本来就会把它的消息丢掉
			 * （message.c 有记录），所以它拦光标是纯粹的阻塞。 */
			NOTICE("[cap] no=%d z=%d vis=%d show=%d ck=%d pc=%d "
			       "hcons=%d began=%d di=%d hblock=%d hb=(%d,%d,%d,%d)",
			       parts->no, parts->global.z,
			       parts_is_visible(parts) ? 1 : 0, parts->global.show,
			       parts->clickable, parts->pass_cursor,
			       *hover_consumed ? 1 : 0, parts_began_click ? 1 : 0,
			       parts->delegate_index, parts->pass_cursor ? 0 : 1,
			       dbghb.x, dbghb.y, dbghb.w, dbghb.h);
	}

	// 不管当前显示状态是什么，命中盒一律用 DEFAULT 状态。
	/* [D128] 隐藏的图层不参与命中（否则旧场景的按钮还挡在新场景前面）。 */
	/* [D489] **曾尝试给 hover 加 `global.alpha > 0` 判据（D488），已回退**。
	 *
	 * 动机：战况窗口淡出后图形消失但按钮仍吃光标。实测（d489）结果是
	 * **副作用**：第一击落到 `900000387`（候选 0，空炮），
	 * `25048 Hide` 反而不再触发 —— 说明 `OnClickStartEvent` 的接收件
	 * 恰恰是那个低 alpha 的件，加判据把触发路径也掐断了。
	 * 且 TurnEnd 依旧点不到（换成了 228 在挡）。
	 * **结论：窗口淡出后按钮仍可点，不是 alpha 判据缺失，**
	 * 而是要先弄清 Hide 的 117 行到底对部件做了什么
	 * （IsEnable=false 只是禁用按钮；图形的消失要么靠动画、
	 * 要么靠场景退出时的 parts 释放）。在弄清之前不动这条判据。 */
	bool is_hovered = parts_is_visible(parts)
		&& parts_hittest(parts, PARTS_STATE_DEFAULT, cur_pos)
		&& !*hover_consumed;

	bool was_hovered = parts->is_hovered;
	parts->is_hovered = is_hovered;

	// !pass_cursor 的部件会替它下面的部件吃掉光标
	/* [D261 受控实验 C-2] hover 消费必须与 click_eligible 用**同一判据**。
	 * 只改 `click_eligible` 而不同时改这里，等于白改：`s85` 的
	 * `XSYS4_CLICK_ONLY_CLICKABLE` 仍然 `EndInput=0`，因为背景 705(pc=0)
	 * 在 z 最高处先把 `hover_consumed` 置真，后面 700/701 连
	 * `is_hovered` 都拿不到，直接 `return` 掉 —— 根本走不到捕获那段。 */
	/* [D490] 拆开两个语义（D488/D489 的教训）：
	 *   - `is_hovered`（能不能**收到**事件）——不看 alpha，
	 *     否则 `BattleInfoView` 的 OnClickStartEvent 接收件在淡出时
	 *     收不到点击，`Hide` 反而触发不了（d489 实测）；
	 *   - `hover_blocking` / `click_eligible`（要不要**挡住**别的件、
	 *     要不要**捕获**点击）——淡出到 alpha=0 的件不该再挡：
	 *     渲染端一个像素都不画，命中却还在，就是
	 *     「窗口看不见了、`ButtonTurnEnd` 仍点不到」的根因。 */
	bool hover_blocking = !parts->pass_cursor && parts->global.alpha > 0;
	/* [D338] **没有任何图形尺寸的部件不拦光标、不参与捕获。**
	 *
	 * 战斗里点 (960,886) 命中的是 `900000568` —— 那是**回想对话框的全屏根**
	 * （`st=0`、`w/h=0`、`a=0`、`hb=(0,0,1920,1080)`）：活动结束后它没被释放，
	 * 一个像素都不画，却把战斗画面整片盖住（`bt19`）。同一族还有
	 * `900001014`（`st=9` flat、`hb=1920x1080`、`a=255` 但隐藏它画面无变化）。
	 * 判据只用一条渲染事实：`w<=0 || h<=0` 的部件画不出东西，没理由拦光标。
	 * `XSYS4_CLICK_NEEDS_SHAPE=0` 关闭。 */
	/* **默认生效，`=0` 关闭**（2026-11-19 转正）。
	 *
	 * `bt29`/`bt30` 的 A/B：开着它 + 坐标兜底，第一章照常到战斗
	 * （`Game/Battle` 装载 32 次），而点角色卡能落到真正的按钮件上，
	 * AIN 随即打开技能选择列表（`bt30`/`bt31` 截图里的「滑落英 / 突击雷击」卡）。
	 * 关掉时点击永远停在 `900001014` 这类全屏件上，什么都不发生。 */
	static int pe_needs_shape = -1;
	if (pe_needs_shape < 0) {
		const char *e = getenv("XSYS4_CLICK_NEEDS_SHAPE");
		pe_needs_shape = !(e && !strcmp(e, "0"));
	}
	/* `PARTS_UNINITIALIZED`（组件类型从未设过）也一定画不出东西：
	 * `bt19` 里点 (960,886) 命中的 `900000568` 就是回想对话框的根，
	 * `st=0`、`a=0`、`hb` 全屏 —— 活动结束后它没被释放，把战斗整片盖住。 */
	/* `bt22` 看清了 `900000568` 的真身：`st=12(=PARTS_LAYOUT_BOX)`、
	 * `w/h=1920x1080`、`a=255`、`ck=0 pc=0`、全屏 hb —— 它是回想对话框
	 * 活动根的**布局盒**，自身不绘制，但尺寸/alpha 都是满的。
	 * **试过按 `PARTS_LAYOUT_BOX` 一刀切排除，`bt23` 立刻回归**：
	 * 选项 `QuestMapSelectionButton` 也靠布局盒接收点击，排除后进不了战斗
	 * （`BattleInfoView` 0 次）。所以这里只保留三条与绘制直接相关的判据，
	 * 布局盒的问题要回到"活动释放"那一层解决（部件本该被释放）。 */
	const struct parts_state *pe_st0 = &parts->states[PARTS_STATE_DEFAULT];
	bool pe_no_shape = pe_st0->type == PARTS_UNINITIALIZED ||
			   pe_st0->common.w <= 0 || pe_st0->common.h <= 0;
	/* **整屏布局盒不拦光标**：`PARTS_LAYOUT_BOX` 是纯排版容器（自身从不绘制），
	 * 而当它的命中盒铺满整个视图时，它就是"整屏容器"，接不住任何具体控件 ——
	 * `bt22` 里点战斗角色卡命中的正是这样一个 `st=12`、`1920x1080`、
	 * `a=255`、`ck=0 pc=0` 的全屏布局盒。
	 * 注意**不能把所有布局盒都排除**（`bt23` 试过：选项按钮的容器也是布局盒，
	 * 一刀切会让 `QuestMapSelectionButton` 收不到点击，进不了战斗）；
	 * 局部布局盒仍照常参与。`XSYS4_CLICK_NEEDS_SHAPE=1` 是总闸。 */
	if (pe_st0->type == PARTS_LAYOUT_BOX) {
		Rectangle lb = pe_st0->common.hitbox;
		if (parts->parent) {
			lb.x += parts->parent->global.pos.x;
			lb.y += parts->parent->global.pos.y;
		}
		if (lb.w >= gfx_view_width() && lb.h >= gfx_view_height())
			pe_no_shape = true;
	}
	if (pe_needs_shape && pe_no_shape)
		hover_blocking = false;
	if (getenv("XSYS4_CLICK_ONLY_CLICKABLE"))
		hover_blocking = parts->clickable;
	/* `s83` 的 CLICK_PASS 之所以 EndInput=0，也是同一处没跟上：
	 * 判据成对改才有意义。 */
	if (getenv("XSYS4_CLICK_PASS"))
		hover_blocking = parts->pass_cursor;
	/* [D262] `XSYS4_CLICK_SKIP_FLAT` 也必须同步到这里。
	 * `s89` 开了它却仍然 `EndInput=0`：因为 705 的 `pc=0` 照样先把
	 * `hover_consumed` 置真，技能面板 340 连 `is_hovered` 都拿不到，
	 * 直接 return —— 和 `s85` 完全同型的坑，第三次踩。 */
	if (getenv("XSYS4_CLICK_SKIP_FLAT") &&
	    parts->states[parts->state].type == PARTS_FLAT)
		hover_blocking = false;
	/* 同上：SKIP_FULLSCREEN 必须同步到 hover，否则 705/701/700 的 pc=0
	 * 照样先吃掉光标，按钮 397 连 is_hovered 都拿不到（第四次同型坑）。 */
	if (getenv("XSYS4_CLICK_SKIP_FULLSCREEN")) {
		Rectangle fb = parts_hitbox_at(parts, PARTS_STATE_DEFAULT);
		if (fb.w >= gfx_view_width() && fb.h >= gfx_view_height())
			hover_blocking = false;
	}
	/* [D285 受控实验] 完全透明的部件不参与 hover 消费与点击捕获。
	 *
	 * 依据：`parts_is_visible()` **不检查 alpha**（只看 `global.show` /
	 * `ctrl_stack.show[c]` / `c >= parts_cover_layer`）—— 这对渲染是有意的
	 * （alpha 由绘制时的混合处理），但拿来当输入准入就错了。
	 *
	 * `d301` 实测：`Game/Battle/EnemyAction/EnemyActionIconBase:3` 的全屏
	 * flat `900000705` 是 `a=0`（**完全透明、一个像素不画**）、
	 * `z=1073742949`（比接收器 700/701 的 `1073741824` **还高**），
	 * 于是它总在 z 序最前面把点击吞掉——三次点击全部
	 * `[inp] EndInput (clicked=900000705)`，而真正的整屏接收器 700/701
	 * （`ck=1`）一次都没拿到。
	 *
	 * 这不是「猜一个更好的判据」，而是把一个已知为真的渲染事实（alpha=0
	 * 等于不可见）补进输入层。开关隔离，默认沿用旧行为。 */
	if (getenv("XSYS4_CLICK_SKIP_INVISIBLE") && parts->global.alpha <= 0) {
		hover_blocking = false;
	}
	/* [D496] 输入组件被 AIN 关掉输入处理时不拦光标（与 click_eligible
	 * 的门控成对——D261 的教训：判据必须成对改）。 */
	if (parts_input_disabled(parts))
		hover_blocking = false;
	/* [D498] 装饰 CG 让位（与下面 click_eligible 的同款判据成对）。 */
	if (parts_decor_pass_enabled() &&
	    parts->states[parts->state].type == PARTS_CG &&
	    !parts->clickable && !parts->pass_cursor) {
		struct parts *pp = parts->parent;
		for (int d = 0; d < 4 && pp; d++, pp = pp->parent) {
			if (pp->uc_object) {
				hover_blocking = false;
				break;
			}
		}
	}
	if (is_hovered && hover_blocking)
		*hover_consumed = true;
	/* [D309] 第一个（z 最高、可见、命中）的部件即「光标指向的部件」，
	 * 与 hover 消费顺序一致；不要求真的按下。 */
	if (is_hovered && !active_parts_no)
		active_parts_no = parts->no;

	if (parts->linked_from >= 0 && is_hovered != was_hovered) {
		parts_dirty(parts_get(parts->linked_from));
	}

	/* [D332] **释放沿即使输入窗口已关也必须处理**。
	 *
	 * 实测（`t6`，标题菜单「从头开始」按钮 `900000063`）：按下那一帧
	 * `[cap] ... began=1`（AIN 的 `BeginInput` 窗口开着），抬起那一帧
	 * `[cap] ... began=0`（AIN 已经 `EndInput`）—— 于是这里 `return` 掉，
	 * 下面那行 `prev_clicking && !cur_clicking` 的收尾永远跑不到，
	 * `click_down_parts` 变不成 `clicked_parts`。
	 *
	 * 表现正是「按下有反应（hover/pressed 状态切换了）、松手没有结果」，
	 * 也是 `[TEXTBUTTON-CLICK]` 里「点得中但没反应」的一半。
	 *
	 * 只放行**释放沿**：按下沿仍然要求窗口开着（否则会在 AIN 没准备接收时
	 * 抢走点击）。D120 处理的是相反方向（按下在窗口外），这里是抬起在窗口外。 */
	if (!parts_began_click && !(prev_clicking && !cur_clicking))
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

	/* [D333] 释放收尾**提前**到这里执行。
	 *
	 * 原因：按下被捕获后，抬起那一帧部件可能已经不可见（切场景 / 按钮动画 /
	 * 活动卸载），而下面 `if (!is_hovered) return;` 与 `!click_eligible return;`
	 * 两处都会把释放沿整个跳过，`click_down_parts` 于是永远变不成
	 * `clicked_parts` —— 表现是「按下有反馈、松手没结果」。
	 *
	 * 实测（`t9` 的 `[cap]`）：第一次按下帧 `no=900000063 vis=1 hcons=0`
	 * 完全可捕获，抬起帧同部件已 `vis=0`，点击就此丢失；这也是标题按钮
	 * 「单次命中率 ~30%」的主因。
	 *
	 * 只对「自己就是被按下的那个部件」生效（`click_down_parts == parts->no`），
	 * 其余部件调用它一律直接返回，所以放在最前面是安全的。 */
	if (prev_clicking && !cur_clicking && click_down_parts == parts->no) {
		clicked_parts = parts->no;
		g_just_clicked = true;
		/* [D498 点击流探针] 捕获时刻。与 POLL/CLR 配对，用来判断
		 * 「点击号被读到之前是不是已经被 EndInput 清掉了」。 */
		if (getenv("XSYS4_CLICK_FLOW"))
			NOTICE("[flow] CAP parts=%d began=%d", clicked_parts, parts_began_click);
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
		/* [D266 受控实验] 子部件被点时向父链冒泡一份 CLICK。
		 *
		 * 起因：`s97` 点战况面板的 `900000819 'ButtonClose'`（有委托 di=1860）
		 * 时 `EndInput` 只记到 1 次、面板没关。System4 里按钮常由一个图标子部件
		 * 作图、逻辑挂在**父按钮**上，若原版有向上冒泡而引擎没有，
		 * 就会出现「点中了、消息发了、什么都没发生」。
		 * 本开关把同一个 CLICK 再发给直接父部件，验证这条链路。 */
		if (getenv("XSYS4_CLICK_BUBBLE") && parts->parent) {
			parts_msg_push(parts->parent, PARTS_MSG_MOUSE_CLICK_AIN,
					"iii", cur_pos.x, cur_pos.y, click_keycode());
		}
	}

	if (!is_hovered) {
		parts_set_state(parts, PARTS_STATE_DEFAULT);
		return;
	}

	bool click_eligible = (parts->clickable || !parts->pass_cursor)
		&& parts->global.alpha > 0;   /* [D490] 淡出件不捕获 */
	/* [D496] 输入组件被 AIN 关掉输入处理时不捕获点击（与上面的
	 * hover_blocking 门控成对）。 */
	if (parts_input_disabled(parts))
		click_eligible = false;
	/* [D498] **装饰 CG 让位**：设置菜单实测，点「设置」被
	 * `900000242`（st=1 CG，512x91 高亮底板，z=113）抢走 —— 它比真正的
	 * 按钮宿主 `900000149`（st=12 LayoutBox，uc=MenuContext，z=111）高
	 * 一层，身上没有任何 AIN 对象，于是点击被静默吞掉（`[pev] 候选 0`）。
	 *
	 * 判据：`PARTS_CG` + `ck=0` + `pc=0`，且**父链上有带 uc 的宿主**——
	 * 这正是「按钮的装饰底板压在按钮上」的形态。让位后点击落到 z 次高的
	 * 宿主上（遍历本来就是 z 降序）。全屏背景 CG 没有 uc 祖先，不受影响；
	 * 「点击任意处继续」用的是 flat/LayoutBox，同样不受影响。 */
	if (parts_decor_pass_enabled() &&
	    parts->states[parts->state].type == PARTS_CG &&
	    !parts->clickable && !parts->pass_cursor) {
		struct parts *pp = parts->parent;
		for (int d = 0; d < 4 && pp; d++, pp = pp->parent) {
			if (pp->uc_object) {
				click_eligible = false;
				if (getenv("XSYS4_INPUT_DBG") && d < 2)
					NOTICE("[inp]   装饰CG让位 no=%d -> uc 宿主 %d",
					       parts->no, pp->no);
				break;
			}
		}
	}
	/* [D338] 与 hover 拦截同一判据（见上）。只放 hover 不改这里，
	 * 仍会被它抢走 `click_down_parts`。 */
	if (pe_needs_shape && pe_no_shape)
		click_eligible = false;
	/* [D247] 实验开关：`clickable=0` 的部件不参与点击捕获。
	 *
	 * 现状是 D113 放宽成 `clickable || !pass_cursor` —— 起因是 Rance10 的
	 * 「点击任意处继续」画面三个部件全是 `点击許可=0`，不放宽 `WaitForClick`
	 * 永远出不来。代价是**全屏背景 flat 也会捕获点击**：战斗里
	 * `BattleInfoView.Flat`(900000705) `pass=0`、hb 覆盖整屏、`gz=1<<30+1125`
	 * 排第一，于是点技能按钮 (1845,818) 命中的是 705（`s47` 实测），
	 * 而 705 自己没有委托 → CLICK 消息打空炮 → 技能发不出去。
	 * 本开关只在跑测里开，用来验证「705 截胡」这一条；默认关闭不动既有路径。 */
	if (getenv("XSYS4_CLICK_SKIP_NONCLICKABLE") && !parts->clickable)
		click_eligible = false;
	/* [D251] flat 不参与点击捕获。
	 *
	 * 战斗里 `BattleInfoView.Flat`(900000705) 是 `フラット名 = 战斗开始/…/背景`
	 * 的全屏演出/背景图：ctrl=2、hb 覆盖整屏、`gz=1<<30+1125`、`pass=0`，
	 * 于是点 (1845,818)（「防御 1Ap」按钮）命中的永远是它（`s47` 候选表），
	 * 而它自己没有委托 → CLICK 消息打空炮 → 战斗停在第一回合。
	 *
	 * 不能按 `clickable` 过滤（`s48` 实测：Rance10 所有可点部件 clickable
	 * 都是 0，过滤后 `EndInput` 直接变 0），也不该按 `pass_cursor` 反转
	 * （按钮是 1、背景是 0，反了会把按钮也滤掉）。**按组件类型**最干净：
	 * flat 在原版里是"平面图形/演出层"，真正的按钮是 `按钮`(0) 或 CG。
	 * 开关隔离，默认沿用旧行为。 */
	if (getenv("XSYS4_CLICK_SKIP_FLAT") &&
	    parts->states[parts->state].type == PARTS_FLAT)
		click_eligible = false;
	/* [D261 受控实验] `click_eligible` 只看 `!pass_cursor` 对兰斯10 是反的：
	 * 战斗技能按钮的 `光标选中透過` 恰好都是 1、背景 flat 是 0，于是
	 * 所有按钮都被排除在点击捕获之外（`s77`/`s78`：点技能区永远只得到
	 * `clicked=900000705` 背景）。本开关按**组件类型**重定：flat 不参与，
	 * 其余一律参与。只用于验证，确认后要换成有依据的判据。 */
	if (getenv("XSYS4_CLICK_ANY")) {
		click_eligible =
			parts->states[parts->state].type != PARTS_FLAT;
	}
	/* [D261 受控实验 B] 把 `光标选中透過` 当成**正向**判据。
	 * D113 注释写「按钮是 1、背景是 0，反了会把按钮也滤掉」，但这句自相矛盾：
	 * `!pass_cursor` 恰好把按钮(1)全滤掉、只留背景(0)，与它的结论相反。
	 * 兰斯10 里所有可见按钮 `透過=1`、全屏背景 `透過=0`，若 1 真是"穿透"，
	 * 游戏根本没法玩 —— 所以更可能 1 = "光标可选中"。
	 * 若本开关下 `EndInput` 有值且点到 900000109，方向即成立。 */
	if (getenv("XSYS4_CLICK_PASS"))
		click_eligible = parts->pass_cursor;
	/* [D261 受控实验 C] 只认 `点击許可`（ck=1）。
	 * `s84` 的全部候选表给出关键事实：战斗 UI 里 **`900000700`/`900000701`**
	 * 是 `hb=1920x1080`、`clickable=1`、`gz=1<<30` 的整屏点击层 ——
	 * 它们就是 AIN 用来接「整屏/任意处点击」的接收器。
	 * 而默认的 `clickable || !pass_cursor` 让背景 flat `900000705`(pc=0)
	 * 因为 z 略高而抢在前面，链路就断在"背景吃掉点击、它又没有委托"。
	 * 本开关把捕获条件收敛成 `clickable` 一条。 */
	if (getenv("XSYS4_CLICK_ONLY_CLICKABLE"))
		click_eligible = parts->clickable;
	/* [D264 受控实验] 跳过「铺满整个视图」的部件。
	 *
	 * 依据：`s91` 的 `[actbuild]` 把真按钮的身份挖出来了 ——
	 * `900000397 'ButtonDeffence'` `hb=(1749,751,158x211)`、
	 * `900000395 'ButtonTurnEnd'`、`900000399 'ButtonParty'` …
	 * 它们 `ck=0 pc=0`（默认判据本来就 eligible），却总被
	 * `705`(flat) / `701` / `700`（三者 hb 都是 1920x1080、gz≥1<<30）截胡。
	 * **真正的按钮不会铺满整屏**，而这三个是演出/背景/整屏点击层。
	 * 本开关把「hb 覆盖整个视图」的部件排除出捕获，验证能否落到 397。 */
	if (getenv("XSYS4_CLICK_SKIP_FULLSCREEN")) {
		Rectangle fb = parts_hitbox_at(parts, PARTS_STATE_DEFAULT);
		if (fb.w >= gfx_view_width() && fb.h >= gfx_view_height())
			click_eligible = false;
	}
	/* [D285 同上] hover 与 click 必须用同一判据（D261/D262 同型坑，已踩四次）。 */
	if (getenv("XSYS4_CLICK_SKIP_INVISIBLE") && parts->global.alpha <= 0)
		click_eligible = false;
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
	/* [D309] 重算本帧「光标指向的部件」。 */
	active_parts_no = 0;
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
			/* [D276] 候选表必须带 `a=`（global alpha）。
			 * 没有这一列时，一个 `a=0` 却仍在吃点击的全屏部件在日志里
			 * 和一个正常可见的部件长得一模一样 —— 这次就是靠 dumps 里
			 * 另一处才发现 705 是全透明的，绕了一大圈。 */
			NOTICE("[inp]   parts=%d pos=(%d,%d) hb=(%d,%d,%d,%d) "
			       "global=(%d,%d) parent=%d gz=%d click=%d pass=%d show=%d "
			       "a=%d ctrl=%d cover=%d di=%d tex=%d isvis=%d uc=%s",
			       parts->no, parts->local.pos.x, parts->local.pos.y,
			       hb.x, hb.y, hb.w, hb.h, gx, gy,
			       parts->parent ? parts->parent->no : -1,
			       parts->global.z, parts->clickable, parts->pass_cursor,
			       parts->global.show, (int)parts->global.alpha,
			       parts->controller_no,
			       covers ? 1 : 0, parts->delegate_index,
			       /* [D295] tex / isvis 决定「数据全对却画不出来」的归属：
			        * tex=0 → 纹理没建（装载问题）；
			        * isvis=0 → `show && ctrl_stack.show[c] && c>=cover_layer`
			        * 中某一项挂了（图层/遮挡问题）。 */
			       (parts->states[parts->state].common.w > 0 ||
				parts->states[parts->state].common.h > 0) ? 1 : 0,
			       parts_is_visible(parts) ? 1 : 0,
			       parts->uc_object ? parts->uc_object : "-");
		}
	}
	if (getenv("XSYS4_CAP_DBG") && cur_clicking && !prev_clicking)
		NOTICE("[cap] 按下帧 sorted_nr=%d began=%d pos=(%d,%d)",
		       sorted_nr, parts_began_click, cur_pos.x, cur_pos.y);
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
	/* [D332] 释放沿那一刻窗口可能已经关了（见上）。若只在窗口开着时推进
	 * `prev_clicking`，它会永远冻结在"按下"，之后每一次物理按下都不再是
	 * 上升沿 —— 点击从此全部静默。所以释放沿也推进一次。 */
	/* [D336] 补上 AIN 侧缺失的按钮轮询：只在「本帧刚捕获到点击」时驱动，
	 * 且只驱动部件号匹配的那个 TextButton（见 vm_tick_text_buttons）。 */
	if (g_just_clicked) {
		g_just_clicked = false;
		/* [D337] 把这次点击派发给 AIN 侧的按钮对象（帧泵里消费）。 */
		vm_request_parts_click(clicked_parts, cur_pos.x, cur_pos.y);
	}

	if (parts_began_click || (prev_clicking && !cur_clicking))
		prev_clicking = cur_clicking;
	parts_prev_pos = cur_pos;
}

void PE_SetPassCursor(int parts_no, bool pass)
{
	struct parts *p = parts_get(parts_no);
	if (!p)
		return;
	p->pass_cursor = !!pass;
	/* [D243] 诊断：全屏 flat 播完后到底是「AIN 把它隐藏」还是「AIN 把它设成
	 * 光标透过」。两者都做不到时点击就会被它整层吞掉。限流只影响打印，
	 * 不限流时每次装载几十条会把战斗阶段冲掉。 */
	if (getenv("XSYS4_CTRL_DBG")) {
		static int n;
		if (n++ < 600)
			NOTICE("[ctrl] pass parts=%d v=%d", parts_no, !!pass);
	}
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

/* [D396] 音效编号 getter。-1 表示没设过（`parts_init` 的初值）。 */
int PE_GetPartsOnCursorSoundNumber(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->on_cursor_sound : -1;
}

int PE_GetPartsClickSoundNumber(int parts_no)
{
	struct parts *parts = parts_try_get(parts_no);
	return parts ? parts->on_click_sound : -1;
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
	if (getenv("XSYS4_CLICK_FLOW") && clicked_parts)
		NOTICE("[flow] CLR was=%d", clicked_parts);
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
	if (getenv("XSYS4_CLICK_FLOW") && clicked_parts)
		NOTICE("[flow] POLL -> %d began=%d", clicked_parts, parts_began_click);
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
