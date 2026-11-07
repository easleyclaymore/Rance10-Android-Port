/* PCInfo - minimal implementation for Rance X bring-up
 * GetOSInfo(ref string)
 */
#include <stdio.h>
#include <string.h>
#include "pal.h"

#include "hll.h"
#include "vm.h"
#include "system4/string.h"

static void PCInfo_GetOSInfo(struct string **info)
{
	const char *desc = pal_get()->info->os_description();
	*info = make_string(desc, strlen(desc));
}

HLL_LIBRARY(PCInfo,
	HLL_EXPORT(GetOSInfo, PCInfo_GetOSInfo));
