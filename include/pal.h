/* 平台抽象层。
 *
 * Core 逻辑绝不能直接 include Linux/POSIX 头文件。平台服务通过
 * Platform vtable 注入：Linux 开发构建用 pal_linux()，
 * Android 构建通过 pal_set() 提供 JNI 实现。
 * 这一层不写死绝对路径，也不假定 uid/root。
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
	/* 把主机名解析成数字地址。成功返回 0。 */
	int (*resolve)(const char *host, char *out, size_t out_size);
} IPlatformNet;

typedef struct IPlatformInfo {
	const char *(*platform_name)(void);
	const char *(*os_description)(void);
	const char *(*data_dir)(void);
} IPlatformInfo;

typedef struct IPlatformMem {
	/* 本进程的常驻内存 / 峰值常驻内存（字节）。
	 * 0 表示「本平台不支持」。仅供诊断。 */
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

/* 注入当前平台实现（启动时调用一次）。传 NULL 则恢复 Linux 默认实现。 */
void pal_set(const Platform *platform);
const Platform *pal_get(void);
const Platform *pal_linux(void);

#endif /* XSYSTEM4_PAL_H */
