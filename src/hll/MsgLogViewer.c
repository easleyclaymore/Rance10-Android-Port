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

#include <stdlib.h>
#include "vm.h"
#include "vm/page.h"
#include "system4/string.h"
#include "hll.h"
#include "MsgLogManager.h"

struct log_data {
	struct string *text;
	int voice;
	int separator;
};

static size_t log_viewer_size = 0;
static size_t log_viewer_i = 0;
static struct log_data *log_viewer = NULL;

static int MsgLogViewer_Numof(void)
{
	return log_viewer_i;
}

static void MsgLogViewer_Clear(void)
{
	for (size_t i = 0; i < log_viewer_i; i++) {
		free_string(log_viewer[i].text);
	}
	free(log_viewer);
	log_viewer = NULL;
	log_viewer_size = 0;
	log_viewer_i = 0;
}

HLL_WARN_UNIMPLEMENTED( , void, MsgLogViewer, Add, struct page *log);

static void MsgLogViewer_Get(int index, struct page **log)
{
	// ???
	if ((size_t)index >= log_viewer_i) {
		(*log)->values[0].i = vm_string_ref(&EMPTY_STRING);
		(*log)->values[1].i = 0;
		(*log)->values[2].i = 0;
		return;
	}

	(*log)->values[0].i = vm_string_ref(log_viewer[index].text);
	(*log)->values[1].i = 0;
	(*log)->values[2].i = 0;
}

static void log_viewer_alloc(void)
{
	if (log_viewer_i < log_viewer_size)
		return;
	if (!log_viewer_size)
		log_viewer_size = 128;
	log_viewer = xrealloc(log_viewer, sizeof(struct log_data) * log_viewer_size * 2);
	log_viewer_size *= 2;
}

static void log_viewer_push(struct string *s, int voice, int separator)
{
	log_viewer_alloc();
	log_viewer[log_viewer_i++] = (struct log_data) {
		.text = string_ref(s),
		.voice = voice,
		.separator = separator
	};
}

int MsgLogManager_Numof(void);

static void MsgLogViewer_ConvertFromMsgLogManager(possibly_unused int msg_log_manager)
{
	int log_size = MsgLogManager_Numof();
	MsgLogViewer_Clear();

	for (int i = 0; i < log_size; i++) {
		if (msg_log[i].data_type == MSG_LOG_INT) {
			if (msg_log[i].type == 1102) {
				log_viewer_push(&EMPTY_STRING, 0, 0);
			}
		}
		if (msg_log[i].data_type == MSG_LOG_STRING) {
			log_viewer_push(msg_log[i].s, 0, 0);
		}
	}
}

HLL_LIBRARY(MsgLogViewer,
	    HLL_EXPORT(Numof, MsgLogViewer_Numof),
	    HLL_EXPORT(Clear, MsgLogViewer_Clear),
	    HLL_EXPORT(Add, MsgLogViewer_Add),
	    HLL_EXPORT(Get, MsgLogViewer_Get),
	    HLL_EXPORT(ConvertFromMsgLogManager, MsgLogViewer_ConvertFromMsgLogManager));
