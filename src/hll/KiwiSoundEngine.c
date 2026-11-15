/* 版权所有 (C) 2021 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <stdio.h>

#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"

#include "vm.h"
#include "hll.h"
#include "asset_manager.h"
#include "audio.h"
#include "audio_meta.h"

static int kiwi_global_focus = 0;

static void KiwiSoundEngine_SetGlobalFocus(int nNum)
{
	kiwi_global_focus = nNum;
}

//int KiwiSoundEngine_Sound_GetGroupNum(int nCh);
//static bool KiwiSoundEngine_Sound_PrepareFromFile(int ch, struct string *filename);

static int KiwiSoundEngine_GetMasterGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetBGMGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetSEGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetVoiceGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetGimicSEGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_GetBackVoiceGroup(void)
{
	return 0;
}

static int KiwiSoundEngine_MillisecondsToSamples(int ms, int samples_per_sec)
{
	return (int)((int64_t)ms * samples_per_sec / 1000);
}

static int KiwiSoundEngine_GetSoundFileName(int num, struct string **file_name)
{
	struct string *name = audio_meta_get_sound_name_by_number(num);
	if (*file_name)
		free_string(*file_name);
	*file_name = string_ref(name ? name : &EMPTY_STRING);
	return name != NULL;
}

static bool rance9_bgm_exists(int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	return asset_exists_by_name(ASSET_BGM, name, NULL);
}

static int rance9_bgm_prepare(int ch, int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	struct archive_data *dfile = asset_get_by_name(ASSET_BGM, name, NULL);
	if (!dfile)
		return 0;
	return bgm_prepare_from_archive_data(ch, num, dfile);
}

static bool rance9_wav_exists(int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	return asset_exists_by_name(ASSET_SOUND, name, NULL);
}

static int rance9_wav_prepare(int ch, int num)
{
	char name[16];
	snprintf(name, sizeof(name), "%05d", num);
	struct archive_data *dfile = asset_get_by_name(ASSET_SOUND, name, NULL);
	if (!dfile)
		return 0;
	return wav_prepare_from_archive_data(ch, num, dfile);
}

static void KiwiSoundEngine_PreLink(void);

/* KiwiSoundEngine.GetMixerMute(group, ref bool mute)，AIN 库21 的 f[35]。
 * [HACK][D119-MIXER] Minimal implementation: audio is never muted by the
 * 引擎里没有逐混音器的静音状态，所以报「没静音」，并把 false 写进
 * ref 形参。待重构：在 audio.c 里记下每个混音器的静音状态，并补上 setter。
 * 已登记进技术债。 */
static bool KiwiSoundEngine_GetMixerMute(int group, bool *mute)
{
	(void)group;
	if (mute)
		*mute = false;
	return false;
}

HLL_LIBRARY(KiwiSoundEngine,
	    HLL_EXPORT(_PreLink, KiwiSoundEngine_PreLink),
	    HLL_EXPORT(SetGlobalFocus, KiwiSoundEngine_SetGlobalFocus),
	    HLL_EXPORT(Music_IsExist, bgm_exists),
	    HLL_EXPORT(Music_Prepare, bgm_prepare),
	    HLL_EXPORT(Music_Unprepare, bgm_unprepare),
	    HLL_EXPORT(Music_Play, bgm_play),
	    HLL_EXPORT(Music_Stop, bgm_stop),
	    HLL_EXPORT(Music_IsPlay, bgm_is_playing),
	    HLL_EXPORT(Music_SetLoopCount, bgm_set_loop_count),
	    HLL_EXPORT(Music_GetLoopCount, bgm_get_loop_count),
	    HLL_EXPORT(Music_SetLoopStartPos, bgm_set_loop_start_pos),
	    HLL_EXPORT(Music_SetLoopEndPos, bgm_set_loop_end_pos),
	    HLL_EXPORT(Music_Fade, bgm_fade),
	    HLL_EXPORT(Music_StopFade, bgm_stop_fade),
	    HLL_EXPORT(Music_IsFade, bgm_is_fading),
	    HLL_EXPORT(Music_Pause, bgm_pause),
	    HLL_EXPORT(Music_Restart, bgm_restart),
	    HLL_EXPORT(Music_IsPause, bgm_is_paused),
	    HLL_EXPORT(Music_GetPos, bgm_get_pos),
	    HLL_EXPORT(Music_GetLength, bgm_get_length),
	    HLL_EXPORT(Music_GetSamplePos, bgm_get_sample_pos),
	    HLL_EXPORT(Music_GetSampleLength, bgm_get_sample_length),
	    HLL_EXPORT(Music_Seek, bgm_seek),
	    //HLL_EXPORT(Music_MillisecondsToSamples, KiwiSoundEngine_Music_MillisecondsToSamples),
	    //HLL_EXPORT(Music_GetFormat, KiwiSoundEngine_Music_GetFormat),
	    HLL_EXPORT(Sound_IsExist, wav_exists),
	    HLL_EXPORT(Sound_Prepare, wav_prepare),
	    HLL_EXPORT(Sound_Unprepare, wav_unprepare),
	    HLL_EXPORT(Sound_Play, wav_play),
	    HLL_EXPORT(Sound_Stop, wav_stop),
	    HLL_EXPORT(Sound_IsPlay, wav_is_playing),
	    HLL_EXPORT(Sound_Fade, wav_fade),
	    HLL_EXPORT(Sound_StopFade, wav_stop_fade),
	    HLL_EXPORT(Sound_IsFade, wav_is_fading),
	    HLL_EXPORT(Sound_GetTimeLength, wav_get_time_length),
	    HLL_TODO_EXPORT(Sound_GetGroupNum, KiwiSoundEngine_Sound_GetGroupNum),
	    HLL_EXPORT(Sound_GetGroupNumFromDataNum, wav_get_group_num_from_data_num),
	    HLL_TODO_EXPORT(Sound_PrepareFromFile, KiwiSoundEngine_Sound_PrepareFromFile),
	    HLL_EXPORT(Sound_GetDataLength, wav_get_data_length),
	    HLL_EXPORT(GetMasterGroup, KiwiSoundEngine_GetMasterGroup),
	    HLL_EXPORT(GetBGMGroup, KiwiSoundEngine_GetBGMGroup),
	    HLL_EXPORT(GetSEGroup, KiwiSoundEngine_GetSEGroup),
	    HLL_EXPORT(GetVoiceGroup, KiwiSoundEngine_GetVoiceGroup),
	    HLL_EXPORT(GetGimicSEGroup, KiwiSoundEngine_GetGimicSEGroup),
	    HLL_EXPORT(GetBackVoiceGroup, KiwiSoundEngine_GetBackVoiceGroup),
	    HLL_TODO_EXPORT(GetFreeSeID, KiwiSoundEngine_GetFreeSeID),
	    HLL_EXPORT(IsExistSeID, audio_se_exists),
	    HLL_EXPORT(SetSeParam, audio_se_set_params),
	    HLL_EXPORT(PlaySe, audio_se_play),
	    HLL_EXPORT(StopSe, audio_se_stop),
	    HLL_EXPORT(IsPlaySe, audio_se_is_playing),
	    HLL_TODO_EXPORT(GetFreeID, KiwiSoundEngine_GetFreeID),
	    HLL_EXPORT(IsExistID, audio_id_exists),
	    HLL_EXPORT(Prepare, audio_prepare),
	    HLL_EXPORT(Unprepare, audio_unprepare),
	    HLL_EXPORT(Play, audio_play),
	    HLL_EXPORT(Stop, audio_stop),
	    HLL_EXPORT(IsPlay, audio_is_playing),
	    HLL_EXPORT(SetLoopCount, audio_set_loop_count),
	    HLL_EXPORT(GetLoopCount, audio_get_loop_count),
	    HLL_EXPORT(Pause, audio_pause),
	    HLL_EXPORT(Restart, audio_restart),
	    HLL_EXPORT(IsPause, audio_is_paused),
	    HLL_EXPORT(Fade, audio_fade),
	    HLL_EXPORT(StopFade, audio_stop_fade),
	    HLL_EXPORT(IsFade, audio_is_fading),
	    HLL_EXPORT(Seek, audio_seek),
	    HLL_EXPORT(GetPos, audio_get_pos),
	    HLL_EXPORT(GetLength, audio_get_length),
	    HLL_EXPORT(GetGroupNum, audio_get_group),
	    HLL_EXPORT(IsExistFile, audio_exists_by_name),
	    HLL_EXPORT(GetLengthFromFile, audio_get_length_by_name),
	    HLL_EXPORT(GetGroupNumFromFile, audio_get_group_by_name),
	    HLL_EXPORT(GetGroupVolume, audio_get_group_volume),
	    HLL_EXPORT(MillisecondsToSamples, KiwiSoundEngine_MillisecondsToSamples),
	    HLL_EXPORT(GetSoundFileName, KiwiSoundEngine_GetSoundFileName),
	    HLL_EXPORT(GetMixerMute, KiwiSoundEngine_GetMixerMute)
	);

static void KiwiSoundEngine_PreLink(void)
{
	int libno = ain_get_library(ain, "KiwiSoundEngine");
	if (libno < 0)
		return;
	if (ain_get_library_function(ain, libno, "Sound_Seek") >= 0) {
		static_library_replace(&lib_KiwiSoundEngine, "Music_IsExist", rance9_bgm_exists);
		static_library_replace(&lib_KiwiSoundEngine, "Music_Prepare", rance9_bgm_prepare);
		static_library_replace(&lib_KiwiSoundEngine, "Sound_IsExist", rance9_wav_exists);
		static_library_replace(&lib_KiwiSoundEngine, "Sound_Prepare", rance9_wav_prepare);
	}
	if (ain_get_library_function(ain, libno, "Prepare") >= 0)
		audio_meta_load_timemap();
}
