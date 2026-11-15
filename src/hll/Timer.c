/* 版权所有 (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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

#include <time.h>

#include "hll.h"
#include "vm.h"

HLL_WARN_UNIMPLEMENTED(, void, Timer, Init, void *imainsystem);

static int Timer_Get(void)
{
	// 返回值和 system.GetTime() 一样。
	return vm_time();
}

static void Timer_Wait(int time)
{
	vm_sleep(time);
}

static void Timer_GetDayTime(int *year, int *month, int *day_of_week, int *day,
							 int *hour, int *minute, int *second, int *milliseconds)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm *tm = localtime(&ts.tv_sec);

	*year         = tm->tm_year + 1900;
	*month        = tm->tm_mon + 1;
	*day_of_week  = tm->tm_wday;
	*day          = tm->tm_mday;
	*hour         = tm->tm_hour;
	*minute       = tm->tm_min;
	*second       = tm->tm_sec;
	*milliseconds = ts.tv_nsec / 1000000;
}

HLL_LIBRARY(Timer,
			HLL_EXPORT(Init, Timer_Init),
			HLL_EXPORT(Get, Timer_Get),
			HLL_EXPORT(Wait, Timer_Wait),
			HLL_EXPORT(GetDayTime, Timer_GetDayTime));
