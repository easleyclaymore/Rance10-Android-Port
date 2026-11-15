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

#include <stdio.h>

#include "system4.h"
#include "system4/string.h"
#include "system4/utfsjis.h"

#include "hll.h"
#include "xsystem4.h"

static struct string **logs = NULL;
static int nr_logs = 0;

int OutputLog_Create(struct string *name)
{
	logs = xrealloc_array(logs, nr_logs, nr_logs + 1, sizeof(struct string*));
	logs[nr_logs++] = string_dup(name);
	return nr_logs - 1;
}

static void OutputLog_Output(int handle, struct string *s)
{
	if (handle < 0 || handle >= nr_logs) {
		log_message("OutputLog", "%s", display_sjis0(s->text));
	} else {
		log_message(display_sjis0(logs[handle]->text), "%s", display_sjis1(s->text));
	}
}

HLL_WARN_UNIMPLEMENTED( , void, OutputLog, Clear, int handle);
HLL_WARN_UNIMPLEMENTED(0, int,  OutputLog, Save, int handle, struct string *filename);
HLL_WARN_UNIMPLEMENTED(0, bool, OutputLog, EnableAutoSave, int handle, struct string *filename);
HLL_WARN_UNIMPLEMENTED(0, bool, OutputLog, DisableAutoSave, int handle);

HLL_LIBRARY(OutputLog,
	    HLL_EXPORT(Create, OutputLog_Create),
	    HLL_EXPORT(Output, OutputLog_Output),
	    HLL_EXPORT(Clear, OutputLog_Clear),
	    HLL_EXPORT(Save, OutputLog_Save),
	    HLL_EXPORT(EnableAutoSave, OutputLog_EnableAutoSave),
	    HLL_EXPORT(DisableAutoSave, OutputLog_DisableAutoSave));
