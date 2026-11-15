/* 版权所有 (C) 2022 kichikuou <KichikuouChrome@gmail.com>
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

#include <stdlib.h>

#include "system4/string.h"

#include "audio.h"
#include "hll.h"
#include "xsystem4.h"

static int channel = -1;

static bool SoundFilePlayer_Load(struct string *file_name)
{
	if (channel >= 0)
		bgm_unprepare(channel);

	channel = bgm_get_unused_channel();
	char *path = unix_path(file_name->text);
	if (!bgm_prepare_from_file(channel, path)) {
		WARNING("Cannot load %s", display_utf0(path));
		channel = -1;
	}
	free(path);
	return channel >= 0;
}

static void SoundFilePlayer_Release(void)
{
	if (channel >= 0) {
		bgm_unprepare(channel);
		channel = -1;
	}
}

static bool SoundFilePlayer_Play(int play_count)
{
	if (channel < 0)
		return false;
	bgm_set_loop_count(channel, play_count);
	bgm_play(channel);
	return true;
}

static bool SoundFilePlayer_Stop(void)
{
	if (channel < 0)
		return false;
	bgm_stop(channel);
	return true;
}

static bool SoundFilePlayer_IsPlay(void)
{
	return channel >= 0 && bgm_is_playing(channel);
}

static int SoundFilePlayer_GetDuration(void)
{
	return channel >= 0 ? bgm_get_length(channel) : 0;
}

static int SoundFilePlayer_GetPosition(void)
{
	return channel >= 0 ? bgm_get_pos(channel) : 0;
}

static bool SoundFilePlayer_BeginFadeVolume(int volume, int time)
{
	if (channel < 0)
		return false;
	bgm_fade(channel, time, volume, false);
	return true;
}

static bool SoundFilePlayer_StopFadeVolume(void)
{
	if (channel < 0)
		return false;
	bgm_stop_fade(channel);
	return true;
}

static bool SoundFilePlayer_IsFadeVolume(void)
{
	return channel >= 0 && bgm_is_fading(channel);
}

HLL_QUIET_UNIMPLEMENTED(true, bool, SoundFilePlayer, Update, void);

HLL_LIBRARY(SoundFilePlayer,
	    HLL_EXPORT(Load, SoundFilePlayer_Load),
	    HLL_EXPORT(Release, SoundFilePlayer_Release),
	    HLL_EXPORT(Play, SoundFilePlayer_Play),
	    HLL_EXPORT(Stop, SoundFilePlayer_Stop),
	    HLL_EXPORT(IsPlay, SoundFilePlayer_IsPlay),
	    HLL_EXPORT(GetDuration, SoundFilePlayer_GetDuration),
	    HLL_EXPORT(GetPosition, SoundFilePlayer_GetPosition),
	    HLL_EXPORT(BeginFadeVolume, SoundFilePlayer_BeginFadeVolume),
	    HLL_EXPORT(StopFadeVolume, SoundFilePlayer_StopFadeVolume),
	    HLL_EXPORT(IsFadeVolume, SoundFilePlayer_IsFadeVolume),
	    HLL_EXPORT(Update, SoundFilePlayer_Update)
	    );
