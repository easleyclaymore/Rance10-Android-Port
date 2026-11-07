/* PCInfo - minimal implementation for Rance X bring-up
 * GetOSInfo(ref string)
 */
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

#include "hll.h"
#include "vm.h"
#include "system4/string.h"

static void PCInfo_GetOSInfo(struct string **info)
{
	struct utsname u;
	char buf[512];

	if (uname(&u) != 0) {
		*info = string_dup(&EMPTY_STRING);
		return;
	}
	snprintf(buf, sizeof(buf), "%s %s %s", u.sysname, u.release, u.machine);
	*info = make_string(buf, strlen(buf));
}

HLL_LIBRARY(PCInfo,
	HLL_EXPORT(GetOSInfo, PCInfo_GetOSInfo));
