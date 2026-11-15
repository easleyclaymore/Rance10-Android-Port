/* 版权所有 (C) 2026 kichikuou <KichikuouChrome@gmail.com>
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

#ifndef SYSTEM4_AUDIO_META_H
#define SYSTEM4_AUDIO_META_H

enum asset_type;
struct string;

enum audio_meta_field {
	AUDIO_META_GROUP = 1 << 0,
	AUDIO_META_LOOP_COUNT = 1 << 1,
	AUDIO_META_LOOP_RANGE = 1 << 2,
	AUDIO_META_VOLUME = 1 << 3,
};

struct audio_meta {
	unsigned fields;
	int group;
	int loop_count;
	int loop_start;
	int loop_end;
	int volume;
};

void audio_meta_load(void);
void audio_meta_load_timemap(void);
const struct audio_meta *audio_meta_get_by_number(enum asset_type type, int no);
const struct audio_meta *audio_meta_get_by_name(const char *name);
struct string *audio_meta_get_sound_name_by_number(int number);

#endif
