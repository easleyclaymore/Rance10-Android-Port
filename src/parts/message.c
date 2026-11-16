/* 版权所有 (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"

#include "vm.h"
#include "parts.h"
#include "parts_internal.h"

#define PARTS_MSG_MAX_VARS 4

struct parts_message {
	STAILQ_ENTRY(parts_message) entry;
	int parts_no;
	int delegate_index;
	int type;
	int variables[PARTS_MSG_MAX_VARS];
	int nr_variables;
};

static STAILQ_HEAD(, parts_message) msg_queue =
		STAILQ_HEAD_INITIALIZER(msg_queue);

void parts_msg_push(struct parts* parts, int type, const char *fmt, ...)
{
	/* [D114] XSYS4_MSG_DBG=1：确认多控制器消息
	 * 系统已启用，并确认点击消息确实进了队列。 */
	static int msg_dbg_n;
	/* 只打非悬停消息，否则前 60 条全被 MOUSE_ON 占满，看不到点击。 */
	if (getenv("XSYS4_MSG_DBG") && type != PARTS_MSG_MOUSE_ON && msg_dbg_n < 120) {
		msg_dbg_n++;
		NOTICE("[msg] push type=%d parts=%d multi=%d di=%d", type,
		       parts ? parts->no : 0, parts_multi_controller,
		       parts ? parts->delegate_index : 0);
	}

	// 消息系统是兰斯9 引入的
	if (!parts_multi_controller)
		return;

	struct parts_message *msg = xmalloc(sizeof(*msg));
	/* [D115] parts == NULL 表示“全屏事件”：parts_no 固定 0。
	 * AIN 侧 CPartsMessageManager@CallDelegate 只有看到 parts_no == 0 才去取
	 * GetFunctionSet(0) 上注册的 AddWholeMouseLClickEvent 系列委托；
	 * 之前 C 侧只推 per-parts 消息，全屏委托永远收不到点击，
	 * SceneStack@Join 的 SceneStack@Exit 也就永远不被调用。 */
	msg->parts_no = parts ? parts->no : 0;
	msg->delegate_index = parts ? parts->delegate_index : 0;
	/* [D123 实验开关] 把标题段的点击消息改投到指定 part（用它的 parts_no
	 * 和 delegate_index），用来验证"委托注册在子活动按钮 part 上"。
	 * 只对 >= 1000001040 的标题段生效，避免把网络确认框的 OK 也改投，
	 * 否则根本进不到标题。默认关闭。 */
	if (parts && type == PARTS_MSG_MOUSE_CLICK_AIN &&
	    msg->parts_no >= 1000001040 && getenv("XSYS4_CLICK_RETARGET")) {
		int target = atoi(getenv("XSYS4_CLICK_RETARGET"));
		if (target > 0) {
			int di = PE_GetDelegateIndex(target);
			NOTICE("[retarget] click %d(di=%d) -> %d(di=%d)",
			       msg->parts_no, msg->delegate_index, target, di);
			msg->parts_no = target;
			msg->delegate_index = di;
		}
	}
	/* [D119 探针] AIN 的 CPartsMessageManager@CallDelegate 会丢掉所有
	 * delegate_index < 0 的消息（立刻返回 0）。activity.c 创建的
	 * 活动部件保持默认的 -1，所以逐部件的点击永远派发不出去。
	 * 受控实验：显式打开时回退到下标 0。 */
	/* [D260] 转正（原 D119 实验开关）。
	 *
	 * AIN 的 `CPartsMessageManager@CallDelegate` 对 `delegate_index < 0` 的消息
	 * 直接返回 0，而 `activity.c` 创建的部件 `delegate_index` 默认就是 -1
	 * （AIN 从不调 `Parts_SetDelegateIndex`）—— 于是战斗里每一次点击都打空炮：
	 * `s77`/`s78` 实测点技能按钮只得到 `clicked=900000705`（背景 flat），
	 * 战斗永远停在 `Round 1/10`。
	 *
	 * **2026-11-16 复核：本改动基本是 no-op，`s80` 的「45→73」是跑测波动。**
	 * `s87` 全量候选表 91447 条里只有 **21 条 `di<0`**（0.02%）—— 说明 AIN
	 * 确实调了 `Parts_SetDelegateIndex`（战斗部件实测 109=75、118=84、
	 * 705=689、716=687），委托一直是注册好的。当时把活动数 45→73 当成修复
	 * 效果，是又一次「拿波动的计数当判据」。代码保留（无害），但不要再据此
	 * 认为「点击打空炮」已解决 —— 点击链路的真问题在捕获判据与图层顺序。
	 *
	 * 保留 `XSYS4_NO_DI_FALLBACK=1` 作为回退闸，供其它场景做 A/B。 */
	if (msg->delegate_index < 0 && !getenv("XSYS4_NO_DI_FALLBACK"))
		msg->delegate_index = 0;
	msg->type = type;
	msg->nr_variables = 0;

	va_list ap;
	va_start(ap, fmt);
	for (const char *p = fmt; *p; p++) {
		if (msg->nr_variables >= PARTS_MSG_MAX_VARS) {
			VM_ERROR("parts_msg_push: too many variables");
		}
		switch (*p) {
		case 'i':
			msg->variables[msg->nr_variables++] = va_arg(ap, int);
			break;
		default:
			VM_ERROR("parts_msg_push: unsupported format '%c'", *p);
		}
	}
	va_end(ap);

	STAILQ_INSERT_TAIL(&msg_queue, msg, entry);
}

void PE_ReleaseMessage(void)
{
	while (!STAILQ_EMPTY(&msg_queue)) {
		struct parts_message *msg = STAILQ_FIRST(&msg_queue);
		STAILQ_REMOVE_HEAD(&msg_queue, entry);
		free(msg);
	}
}

void PE_PopMessage(void)
{
	if (STAILQ_EMPTY(&msg_queue))
		return;
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	/* [D115] XSYS4_MSG_POPDBG=1：谁把消息弹掉了。点击消息若被悬停处理吃掉，
	 * 这里能看到 type=5 被 pop 而没有对应 dispatch。 */
	if (getenv("XSYS4_MSG_POPDBG") && msg->type != PARTS_MSG_MOUSE_ON)
		NOTICE("[mpop] type=%d parts=%d", msg->type, msg->parts_no);
	STAILQ_REMOVE_HEAD(&msg_queue, entry);
	free(msg);
}

/* [D158] XSYS4_MSG_TRACE=<type>：当**队头**消息的类型等于该值时，
 * 把消息访问器的调用序列打出来。用来回答"点击消息为什么被 PopMessage
 * 掉而不是派发" —— 只看得到结果（[mpop]），看不到 AIN 在中间问了什么、
 * 问到哪一步就放弃了。 */
static int msg_trace_type = -2;

static void msg_trace(const char *what, int a, int b)
{
	if (msg_trace_type == -2) {
		const char *e = getenv("XSYS4_MSG_TRACE");
		msg_trace_type = (e && *e) ? atoi(e) : -1;
	}
	if (msg_trace_type < 0)
		return;
	struct parts_message *head = STAILQ_FIRST(&msg_queue);
	if (!head || head->type != msg_trace_type)
		return;
	NOTICE("[msgt] %s=%d,%d head{type=%d parts=%d di=%d nvar=%d}", what, a, b,
	       head->type, head->parts_no, head->delegate_index, head->nr_variables);
}

int PE_GetMessageType(void)
{
	/* [D112] 「队列空」的哨兵是 -1，不是 0：
	 *   CPartsMessageManager@Update: MessageType = GetMessageType();
	 *   while (MessageType != -1) { if (!CallDelegate(t)) PopMessage(); ... }
	 * 空队列时返回 0，会让循环把「没有消息」永远当成
	 * 消息类型 0（PopMessage 在空队列上是空操作），
	 * 这就是那个把 SceneStack@WaitFrame 卡死的忙循环。 */
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	if (!msg && getenv("XSYS4_MSG_NO_SENTINEL"))
		return 0;   /* demo knob: reproduce the pre-D112 busy-wait (frozen start screen) */
	/* [D130] 队头消息身份：CallDelegate 对 parts_no==0 走全屏 FunctionSet，
	 * 对其它值走 member1[delegate_index]。分派打空炮时必须先知道走的哪条。 */
	if (msg && getenv("XSYS4_MSGQ_DBG")) {
		static int n;
		if (n++ < 4000)
			NOTICE("[msgq] type=%d parts=%d di=%d", msg->type,
			       msg->parts_no, msg->delegate_index);
	}
	return msg ? msg->type : -1;
}

int PE_GetMessagePartsNumber(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	msg_trace("parts", msg ? msg->parts_no : 0, 0);
	return msg ? msg->parts_no : 0;
}

int PE_GetMessageDelegateIndex(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	msg_trace("di", msg ? msg->delegate_index : -1, 0);
	return msg ? msg->delegate_index : -1;
}

int PE_GetMessageVariableCount(void)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	msg_trace("nvar", msg ? msg->nr_variables : 0, 0);
	return msg ? msg->nr_variables : 0;
}

int PE_GetMessageVariableType(possibly_unused int index)
{
	// 当前实现里所有变量都是整数。
	msg_trace("vtype", index, 1);
	return 1;
}

int PE_GetMessageVariableInt(int index)
{
	struct parts_message *msg = STAILQ_FIRST(&msg_queue);
	msg_trace("vint", index, msg && index >= 0 && index < msg->nr_variables
	          ? msg->variables[index] : 0);
	if (!msg)
		return 0;
	if (index < 0 || index >= msg->nr_variables)
		return 0;
	return msg->variables[index];
}

float PE_GetMessageVariableFloat(possibly_unused int index)
{
	return 0.0f;
}

bool PE_GetMessageVariableBool(possibly_unused int index)
{
	return false;
}

void PE_GetMessageVariableString(possibly_unused int index, struct string **out)
{
	if (*out)
		free_string(*out);
	*out = string_ref(&EMPTY_STRING);
}
