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

#include <errno.h>
#include <string.h>
#include <time.h>
#include <SDL.h>

#include "system4/string.h"

#include "hll.h"
#include "vm/page.h"

HLL_QUIET_UNIMPLEMENTED( , void, vmSystem, Init, void *imainsystem);

void vmSystem_CurrentTime(struct page **page)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
		WARNING("clock_gettime failed: %s", strerror(errno));
		return;
	}
	struct tm *tm = localtime(&ts.tv_sec);
	if (!tm) {
		WARNING("localtime failed: %s", strerror(errno));
		return;
	}

	union vm_value *date = (*page)->values;
	date[0].i = tm->tm_year + 1900;
	date[1].i = tm->tm_mon + 1;
	date[2].i = tm->tm_mday;
	date[3].i = tm->tm_hour;
	date[4].i = tm->tm_min;
	date[5].i = tm->tm_sec;
	date[6].i = tm->tm_wday;
}

static void vmSystem_Reset(void)
{
	vm_reset();
}

static void vmSystem_Shutdown(void)
{
	vm_exit(0);
}

static int vmSystem_OpenWeb(struct string *url)
{
	return SDL_OpenURL(url->text) == 0;
}

HLL_WARN_UNIMPLEMENTED(0, int, vmSystem, AddURLMenu, struct string *title, struct string *url);

HLL_LIBRARY(vmSystem,
	    HLL_EXPORT(Init, vmSystem_Init),
	    HLL_EXPORT(CurrentTime, vmSystem_CurrentTime),
	    HLL_EXPORT(Reset, vmSystem_Reset),
	    HLL_EXPORT(Shutdown, vmSystem_Shutdown),
	    HLL_EXPORT(OpenWeb, vmSystem_OpenWeb),
	    HLL_EXPORT(AddURLMenu, vmSystem_AddURLMenu)
	    );
