/* 版权所有 (C) 2024 kichikuou <KichikuouChrome@gmail.com>
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

#include "system4.h"

#include "hll.h"
#include "audio.h"

static int max_channel;
static int *current;

static void vmMusic_Init(int max_channel_, possibly_unused void *imainsystem)
{
	audio_init();
	max_channel = max_channel_;
	current = xcalloc(max_channel, sizeof(int));
}

static int vmMusic_Prepare(int channel, int track)
{
	if (channel < 0 || channel >= max_channel)
		return 0;
	if (current[channel] == track)
		return 1;  // Keep playing current music.
	int r = bgm_prepare(channel, track);
	if (r)
		current[channel] = track;
	return r;
}

static int vmMusic_Play(int channel)
{
	return bgm_play(channel);
}

static int vmMusic_Stop(int channel)
{
	return bgm_stop(channel);
}

static int vmMusic_IsPlay(int channel)
{
	return bgm_is_playing(channel);
}

//int vmMusic_SetLoop(int channel, int nLoop);
//int vmMusic_GetLoop(int channel);

static int vmMusic_Fade(int channel, int time, int volume, int stop)
{
	return bgm_fade(channel, time, volume, stop);
}

//int vmMusic_StopFade(int channel);
//int vmMusic_IsFade(int channel);
//int vmMusic_GetPos(int channel);
//int vmMusic_GetLength(int channel);

HLL_LIBRARY(vmMusic,
	    HLL_EXPORT(Init, vmMusic_Init),
	    HLL_EXPORT(Prepare, vmMusic_Prepare),
	    HLL_EXPORT(Play, vmMusic_Play),
	    HLL_EXPORT(Stop, vmMusic_Stop),
	    HLL_EXPORT(IsPlay, vmMusic_IsPlay),
	    HLL_TODO_EXPORT(SetLoop, vmMusic_SetLoop),
	    HLL_TODO_EXPORT(GetLoop, vmMusic_GetLoop),
	    HLL_EXPORT(Fade, vmMusic_Fade),
	    HLL_TODO_EXPORT(StopFade, vmMusic_StopFade),
	    HLL_TODO_EXPORT(IsFade, vmMusic_IsFade),
	    HLL_TODO_EXPORT(GetPos, vmMusic_GetPos),
	    HLL_TODO_EXPORT(GetLength, vmMusic_GetLength)
	    );
