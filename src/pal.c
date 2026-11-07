/* Core-side PAL state and injected data directory. No OS headers here. */
#include <stdlib.h>
#include <string.h>

#include "pal.h"
#include "xsystem4.h"

static const Platform *active_platform;

const Platform *pal_get(void)
{
	return active_platform ? active_platform : pal_linux();
}

void pal_set(const Platform *platform)
{
	active_platform = platform;
}

/* Data directory resolution order:
 *   1. explicit injection (xsys4_set_data_dir)
 *   2. XSYS4_DATA_DIR environment variable
 *   3. build-time default injected by meson
 *   4. portable relative fallback
 * The process owns the injected string; the getter never returns NULL.
 */
static char *injected_data_dir;

#ifndef XSYS4_DEFAULT_DATA_DIR
#define XSYS4_DEFAULT_DATA_DIR "share/xsystem4"
#endif

void xsys4_set_data_dir(const char *dir)
{
	free(injected_data_dir);
	injected_data_dir = dir ? strdup(dir) : NULL;
}

const char *xsys4_data_dir(void)
{
	if (injected_data_dir)
		return injected_data_dir;
	const char *env = getenv("XSYS4_DATA_DIR");
	if (env && *env)
		return env;
	return XSYS4_DEFAULT_DATA_DIR;
}
