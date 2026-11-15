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

#include "audio.h"
#include "hll.h"
#include "ChrLoader.h"

static int ring_pos = 0;

static void vmSound_Init(possibly_unused void *imainsystem)
{
	audio_init();
}

//void vmSound_Enable(int nEnable);

HLL_QUIET_UNIMPLEMENTED(, void, vmSound, SetType, int type);

static int vmSound_Load(int channel, int num)
{
	return wav_prepare(channel, num);
}

static int vmSound_LoadMemory(int channel, int memory)
{
	struct archive_data *dfile = chr_loader_get_blob_as_archive_data(memory);
	if (!dfile)
		return 0;
	return wav_prepare_from_archive_data(channel, AUDIO_NO_METADATA, dfile);
}

static void vmSound_Unload(int channel, int length)
{
	for (int i = 0; i < length; i++)
		wav_unprepare(channel + i);
}

static int vmSound_Play(int channel, possibly_unused int length)
{
	wav_stop(channel);
	return wav_play(channel);
}

static int vmSound_RingPlay(int channel, int ring)
{
	channel += ring_pos++ % ring;
	wav_stop(channel);
	return wav_play(channel);
}

//int vmSound_Stop(int channel, int nLength);

static int vmSound_IsPlay(int channel, int length)
{
	for (int i = 0; i < length; i++) {
		if (wav_is_playing(channel + i))
			return 1;
	}
	return 0;
}

//void vmSound_Wait(int channel, int nLength);
//int vmSound_Fade(int channel, int nLength, int nTime, int nVolume, int nStopFlag);
//int vmSound_StopFade(int channel, int nLength);
//int vmSound_IsFade(int channel, int nLength);
//int vmSound_ReverseLR(int channel, int nLength);

HLL_LIBRARY(vmSound,
	    HLL_EXPORT(Init, vmSound_Init),
	    HLL_TODO_EXPORT(Enable, vmSound_Enable),
	    HLL_EXPORT(SetType, vmSound_SetType),
	    HLL_EXPORT(Load, vmSound_Load),
	    HLL_EXPORT(LoadMemory, vmSound_LoadMemory),
	    HLL_EXPORT(Unload, vmSound_Unload),
	    HLL_EXPORT(Play, vmSound_Play),
	    HLL_EXPORT(RingPlay, vmSound_RingPlay),
	    HLL_TODO_EXPORT(Stop, vmSound_Stop),
	    HLL_EXPORT(IsPlay, vmSound_IsPlay),
	    HLL_TODO_EXPORT(Wait, vmSound_Wait),
	    HLL_TODO_EXPORT(Fade, vmSound_Fade),
	    HLL_TODO_EXPORT(StopFade, vmSound_StopFade),
	    HLL_TODO_EXPORT(IsFade, vmSound_IsFade),
	    HLL_TODO_EXPORT(ReverseLR, vmSound_ReverseLR)
	    );
