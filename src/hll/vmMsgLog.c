/* 版权所有 (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

#include "system4/ain.h"
#include "system4/string.h"

#include "hll.h"
#include "id_pool.h"
#include "vm.h"
#include "vm/page.h"

struct vm_msglog {
	struct page *array;  // backing store
	int ptr;
};

static struct id_pool pool;

static void vmMsgLog_Init(possibly_unused void *imainsystem)
{
	if (pool.slots)
		return;  // already initialized
	id_pool_init(&pool, ID_POOL_HANDLE_BASE);
}

static int vmMsgLog_EnumHandle(void)
{
	return id_pool_count(&pool);
}

static int vmMsgLog_Open(struct page **array)
{
	int no = id_pool_get_unused(&pool);
	struct vm_msglog *log = xcalloc(1, sizeof(struct vm_msglog));
	id_pool_set(&pool, no, log);
	log->array = *array;
	log->ptr = 0;
	return no;
}

static void vmMsgLog_Close(int handle)
{
	struct vm_msglog *log = id_pool_release(&pool, handle);
	if (log)
		free(log);
}

static void vmMsgLog_ModuleFini(void)
{
	for (int i = id_pool_get_first(&pool); i >= 0; i = id_pool_get_next(&pool, i))
		vmMsgLog_Close(i);
	id_pool_delete(&pool);
}

static int vmMsgLog_Set(int handle, int msg_num)
{
	struct vm_msglog *log = id_pool_get(&pool, handle);
	if (!log)
		return 0;

	if (--log->ptr < 0)
		log->ptr = log->array->nr_vars - 1;
	log->array->values[log->ptr].i = msg_num;
	return 1;
}

static int vmMsgLog_Get(int handle, int index)
{
	struct vm_msglog *log = id_pool_get(&pool, handle);
	if (!log)
		return -1;

	int i = (index + log->ptr) % log->array->nr_vars;
	return log->array->values[i].i;
}

static struct string *vmMsgLog_MsgNumToString(int msg_num)
{
	if (msg_num < 0 || msg_num >= ain->nr_messages)
		return string_ref(&EMPTY_STRING);
	return string_ref(ain->messages[msg_num]);
}

HLL_LIBRARY(vmMsgLog,
	    HLL_EXPORT(_ModuleFini, vmMsgLog_ModuleFini),
	    HLL_EXPORT(Init, vmMsgLog_Init),
	    HLL_EXPORT(EnumHandle, vmMsgLog_EnumHandle),
	    HLL_EXPORT(Open, vmMsgLog_Open),
	    HLL_EXPORT(Close, vmMsgLog_Close),
	    HLL_EXPORT(Set, vmMsgLog_Set),
	    HLL_EXPORT(Get, vmMsgLog_Get),
	    HLL_EXPORT(MsgNumToString, vmMsgLog_MsgNumToString)
	    );
