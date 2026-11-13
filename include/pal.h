/* Platform Abstraction Layer.
 *
 * Core logic must never include Linux/POSIX headers directly. Platform
 * services are injected through a Platform vtable: Linux development builds
 * use pal_linux(), Android builds provide a JNI-backed implementation via
 * pal_set(). No absolute paths or uid/root assumptions live in this layer.
 */
#ifndef XSYSTEM4_PAL_H
#define XSYSTEM4_PAL_H

#include <stddef.h>
#include <stdint.h>

struct pal_file;
struct pal_dir;

typedef struct IPlatformFile {
	struct pal_file *(*open)(const char *path, const char *mode);
	void (*close)(struct pal_file *f);
	size_t (*read)(struct pal_file *f, void *buf, size_t n);
	size_t (*write)(struct pal_file *f, const void *buf, size_t n);
	long (*seek)(struct pal_file *f, long off, int whence);
	long (*tell)(struct pal_file *f);
	int (*stat)(const char *path, int64_t *size, int *is_dir);
	int (*mkdir)(const char *path);
	struct pal_dir *(*opendir)(const char *path);
	const char *(*readdir)(struct pal_dir *d);
	void (*closedir)(struct pal_dir *d);
} IPlatformFile;

typedef struct IPlatformClock {
	uint64_t (*monotonic_ms)(void);
	uint64_t (*wall_ms)(void);
} IPlatformClock;

typedef struct IPlatformThread {
	void (*sleep_ms)(uint32_t ms);
	uint64_t (*thread_id)(void);
} IPlatformThread;

typedef struct IPlatformNet {
	/* Resolve a host name to a numeric address. Returns 0 on success. */
	int (*resolve)(const char *host, char *out, size_t out_size);
} IPlatformNet;

typedef struct IPlatformInfo {
	const char *(*platform_name)(void);
	const char *(*os_description)(void);
	const char *(*data_dir)(void);
} IPlatformInfo;

typedef struct IPlatformMem {
	/* Resident / peak resident size of this process, in bytes.
	 * 0 means "not available on this platform". Diagnostic only. */
	void (*usage)(uint64_t *rss_bytes, uint64_t *peak_bytes);
} IPlatformMem;

typedef struct Platform {
	const IPlatformFile *file;
	const IPlatformClock *clock;
	const IPlatformThread *thread;
	const IPlatformNet *net;
	const IPlatformInfo *info;
	const IPlatformMem *mem;
} Platform;

/* Inject the active platform (called once at startup). NULL restores Linux. */
void pal_set(const Platform *platform);
const Platform *pal_get(void);
const Platform *pal_linux(void);

#endif /* XSYSTEM4_PAL_H */
