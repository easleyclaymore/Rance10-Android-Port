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

#ifndef XSYSTEM4_H
#define XSYSTEM4_H

#include <stddef.h>
#include <stdbool.h>

enum resume_save_format {
	SAVE_FORMAT_JSON,
	SAVE_FORMAT_RSM,
};

struct config {
	char *game_name;
	char *boot_name;
	char *ain_filename;
	char *vm_name;
	char *game_dir;
	char *save_dir;
	char *home_dir;
	int view_width;
	int view_height;
	size_t mixer_nr_channels;
	char **mixer_channels;
	int *mixer_volumes;
	int default_volume;

	char *bgi_path;
	char *wai_path;
	char *ex_path;
	char *fnl_path;
	char *font_paths[2];

	bool joypad;
	bool echo;
	float text_x_scale;
	bool manual_text_x_scale;
	enum resume_save_format save_format;
	int msgskip_delay;
};

extern struct config config;

// 便利函数：给 printf 这类变参函数传文本时，
// 省掉繁琐的内存管理
const char *display_sjis0(const char *sjis);
const char *display_sjis1(const char *sjis);
const char *display_sjis2(const char *sjis);
const char *display_utf0(const char *utf);
const char *display_utf1(const char *utf);
const char *display_utf2(const char *utf);

void indent_message(int indent, const char *fmt, ...);

void log_message(const char *log, const char *fmt, ...);

#define UNIMPLEMENTED(fmt, ...) \
	sys_warning("unimplemented: %s" fmt "\n", __func__, ##__VA_ARGS__)

bool is_absolute_path(const char *path);
char *unix_path(const char *path);
char *gamedir_path(const char *path);
char *gamedir_path_icase(const char *path);
char *savedir_path(const char *path);

void get_date(int *year, int *month, int *mday, int *wday);
void get_time(int *hour, int *min, int *sec, int *ms);

void screenshot_save(void);

/* 数据目录在启动时注入：优先 XSYS4_DATA_DIR，否则用 meson 给的构建期默认值。
 * 源码里绝不写死路径。 */
const char *xsys4_data_dir(void);
void xsys4_set_data_dir(const char *dir);

#define MMAP_IF_64BIT (sizeof(void*) >= 8 ? ARCHIVE_MMAP : 0)

extern bool game_daibanchou_en;
extern bool game_rance02_mg;
extern bool game_rance6_mg;
extern bool game_rance7_mg;
extern bool game_rance8;
extern bool game_rance8_mg;
extern bool game_dungeons_and_dolls;

#endif /* XSYSTEM4_H */
