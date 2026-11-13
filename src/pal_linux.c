/* Linux/POSIX implementation of the PAL. This is the ONLY place where
 * Linux-specific headers are allowed for platform services. */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "pal.h"
#include "xsystem4.h"

struct pal_file { FILE *fp; };
struct pal_dir  { DIR *dp; struct dirent *ent; };

static struct pal_file *lf_open(const char *path, const char *mode)
{
	FILE *fp = fopen(path, mode);
	if (!fp) return NULL;
	struct pal_file *f = calloc(1, sizeof(*f));
	if (!f) { fclose(fp); return NULL; }
	f->fp = fp;
	return f;
}
static void lf_close(struct pal_file *f) { if (f) { if (f->fp) fclose(f->fp); free(f); } }
static size_t lf_read(struct pal_file *f, void *buf, size_t n) { return f ? fread(buf, 1, n, f->fp) : 0; }
static size_t lf_write(struct pal_file *f, const void *buf, size_t n) { return f ? fwrite(buf, 1, n, f->fp) : 0; }
static long lf_seek(struct pal_file *f, long off, int whence) { return f ? (fseek(f->fp, off, whence) ? -1 : 0) : -1; }
static long lf_tell(struct pal_file *f) { return f ? ftell(f->fp) : -1; }
static int lf_stat(const char *path, int64_t *size, int *is_dir)
{
	struct stat st;
	if (stat(path, &st)) return -1;
	if (size) *size = (int64_t)st.st_size;
	if (is_dir) *is_dir = S_ISDIR(st.st_mode);
	return 0;
}
static int lf_mkdir(const char *path) { return mkdir(path, 0777); }
static struct pal_dir *lf_opendir(const char *path)
{
	DIR *dp = opendir(path);
	if (!dp) return NULL;
	struct pal_dir *d = calloc(1, sizeof(*d));
	if (!d) { closedir(dp); return NULL; }
	d->dp = dp;
	return d;
}
static const char *lf_readdir(struct pal_dir *d)
{
	if (!d) return NULL;
	d->ent = readdir(d->dp);
	return d->ent ? d->ent->d_name : NULL;
}
static void lf_closedir(struct pal_dir *d) { if (d) { closedir(d->dp); free(d); } }

static uint64_t now_ms(clockid_t id)
{
	struct timespec ts;
	if (clock_gettime(id, &ts)) return 0;
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}
static uint64_t lc_monotonic_ms(void) { return now_ms(CLOCK_MONOTONIC); }
static uint64_t lc_wall_ms(void) { return now_ms(CLOCK_REALTIME); }
static void lt_sleep_ms(uint32_t ms)
{
	struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}
static uint64_t lt_thread_id(void) { return (uint64_t)(uintptr_t)pthread_self(); }
static int ln_resolve(const char *host, char *out, size_t out_size)
{
	/* Minimal placeholder: Android/JNI implementation will provide the
	 * real resolver. Never fabricate an address. */
	(void)host; (void)out; (void)out_size;
	return -1;
}
static const char *li_platform_name(void) { return "linux"; }
static const char *li_os_description(void)
{
	static char buf[512];
	static int ready = 0;
	if (!ready) {
		struct utsname u;
		if (uname(&u) == 0)
			snprintf(buf, sizeof(buf), "%s %s %s", u.sysname, u.release, u.machine);
		else
			snprintf(buf, sizeof(buf), "unknown");
		ready = 1;
	}
	return buf;
}
static const char *li_data_dir(void) { return xsys4_data_dir(); }

static const IPlatformFile linux_file = {
	lf_open, lf_close, lf_read, lf_write, lf_seek, lf_tell,
	lf_stat, lf_mkdir, lf_opendir, lf_readdir, lf_closedir
};
static const IPlatformClock linux_clock = { lc_monotonic_ms, lc_wall_ms };
static const IPlatformThread linux_thread = { lt_sleep_ms, lt_thread_id };
static const IPlatformNet linux_net = { ln_resolve };
static const IPlatformInfo linux_info = { li_platform_name, li_os_description, li_data_dir };
/* RSS/peak for diagnostics ([mem]). Lives in the adapter because it reads a
 * Linux-specific file; Core asks through the vtable instead. */
static void linux_mem_usage(uint64_t *rss_bytes, uint64_t *peak_bytes)
{
	*rss_bytes = 0;
	*peak_bytes = 0;
	FILE *f = fopen("/proc/self/status", "r");
	if (!f)
		return;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		long kb = 0;
		if (sscanf(line, "VmRSS: %ld kB", &kb) == 1)
			*rss_bytes = (uint64_t)kb * 1024;
		else if (sscanf(line, "VmHWM: %ld kB", &kb) == 1)
			*peak_bytes = (uint64_t)kb * 1024;
	}
	fclose(f);
}

static const IPlatformMem linux_mem = { linux_mem_usage };

static const Platform linux_platform = {
	&linux_file, &linux_clock, &linux_thread, &linux_net, &linux_info, &linux_mem
};

const Platform *pal_linux(void) { return &linux_platform; }
