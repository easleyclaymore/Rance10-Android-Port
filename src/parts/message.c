/* Copyright (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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
	/* [D114] XSYS4_MSG_DBG=1: confirm that the multi-controller message
	 * system is enabled and that click messages actually reach the queue. */
	static int msg_dbg_n;
	/* 只打非悬停消息，否则前 60 条全被 MOUSE_ON 占满，看不到点击。 */
	if (getenv("XSYS4_MSG_DBG") && type != PARTS_MSG_MOUSE_ON && msg_dbg_n < 120) {
		msg_dbg_n++;
		NOTICE("[msg] push type=%d parts=%d multi=%d di=%d", type,
		       parts ? parts->no : 0, parts_multi_controller,
		       parts ? parts->delegate_index : 0);
	}

	// Message system is introduced in Rance 9
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
	/* [D119 probe] AIN CPartsMessageManager@CallDelegate drops any message
	 * whose delegate_index < 0 (return 0 immediately). Activity parts created
	 * by activity.c keep the default -1, so per-parts clicks never dispatch.
	 * Controlled experiment: fall back to index 0 when explicitly enabled. */
	if (msg->delegate_index < 0 && getenv("XSYS4_DI_FALLBACK"))
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
	/* [D112] -1, not 0, is the "queue empty" sentinel:
	 *   CPartsMessageManager@Update: MessageType = GetMessageType();
	 *   while (MessageType != -1) { if (!CallDelegate(t)) PopMessage(); ... }
	 * Returning 0 on an empty queue made the loop treat "no message" as
	 * message type 0 forever (PopMessage is a no-op when empty), which is the
	 * busy loop that froze SceneStack@WaitFrame. */
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
	// In the current implementation, all variables are integers.
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
