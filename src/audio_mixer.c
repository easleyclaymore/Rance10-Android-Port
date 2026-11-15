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

#include <stdatomic.h>
#include <math.h>
#include <assert.h>

#include <sndfile.h>
#include <SDL.h>

#include "system4.h"
#include "system4/archive.h"
#include "system4/utfsjis.h"

#include "audio.h"
#include "mixer.h"
#include "xsystem4.h"

#define clamp(min_value, max_value, value) min(max_value, max(min_value, value))
#define muldiv(x, y, denom) ((int64_t)(x) * (int64_t)(y) / (int64_t)(denom))

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

/*
 * 真正的混音器实现就在这个头文件里。
 * 本文件其余部分实现的是管理混音通道的高层接口
 * （加载音频、启停等）。
 */
#define STS_MIXER_IMPLEMENTATION
#include "sts_mixer.h"

#define CHUNK_SIZE 1024

struct fade {
	atomic_bool fading;
	bool stop;
	uint_least32_t start_pos;
	uint_least32_t frames;
	uint_least32_t elapsed;
	float start_volume;
	float end_volume;
	enum fade_curve curve;
};

struct channel {
	// 压缩包数据
	struct archive_data *dfile;
	int mixer_no;

	// 音频文件数据
	SNDFILE *file;
	SF_INFO info;
	sf_count_t offset;

	// 流数据
	atomic_int voice;
	sts_mixer_stream_t stream;
	float data[CHUNK_SIZE * 2];

	// 主线程只读
	atomic_uint_least32_t frame;

	atomic_uint volume;
	atomic_bool swapped;
	uint_least32_t loop_start;
	uint_least32_t loop_end;
	atomic_uint loop_count;
	struct fade fade;
};

struct mixer {
	sts_mixer_t mixer;
	sts_mixer_stream_t stream;
	int voice;
	atomic_bool muted;
	float data[CHUNK_SIZE * 2];
	char *name;

	struct mixer *parent;
	struct mixer **children;
	int nr_children;
};

static struct mixer *master = NULL;
static struct mixer *mixers = NULL;
static int nr_mixers = 0;

static SDL_AudioDeviceID audio_device = 0;

/*
 * SDL2 音频回调。
 */
static void audio_callback(possibly_unused void *data, Uint8 *stream, int len)
{
	sts_mixer_mix_audio(&master->mixer, stream, len / (sizeof(float) * 2));
	if (master->muted) {
		memset(stream, 0, len);
	}
}

/*
 * 定位到流里的指定位置。
 * 成功返回 true，否则返回 false。
 */
static bool cb_seek(struct channel *ch, uint_least32_t pos)
{
	if (pos > ch->info.frames) {
		pos = ch->info.frames;
	}
	sf_count_t r = sf_seek(ch->file, pos, SEEK_SET);
	if (r < 0) {
		WARNING("sf_seek failed");
		return false;
	}
	ch->frame = r;
	return true;
}

/*
 * 如果该流要循环，就定位到循环起点。
 * 要循环返回 true，要停止返回 false。
 */
static bool cb_loop(struct channel *ch)
{
	if (!cb_seek(ch, ch->loop_start) || ch->loop_count == 1) {
		return false;
	}
	if (ch->loop_count > 1) {
		ch->loop_count--;
	}
	return true;
}

/*
 * 回填流音频数据的回调。
 * 由 sys_mixer_mix_audio 调用。
 */
static int cb_read_frames(struct channel *ch, float *out, sf_count_t frame_count, uint_least32_t *num_read)
{
	*num_read = 0;

	// 处理数据块跨越循环点的情况（无缝衔接）
	// 注意：这里假定循环长度大于块长度
	if (ch->frame + frame_count >= ch->loop_end) {
		// 读到 loop_end 为止
		*num_read = sf_readf_float(ch->file, out, ch->loop_end - ch->frame);
		// 调整后续要用的参数
		ch->frame += *num_read;
		out += *num_read;
		frame_count -= *num_read;
		// 定位到 loop_start
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
	} else if (ch->frame >= ch->loop_end) {
		// 定位到 loop_start
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
	}

	// 读剩下的数据
	sf_count_t n = sf_readf_float(ch->file, out, frame_count);
	*num_read += n;
	ch->frame += n;
	out += *num_read;
	frame_count -= *num_read;

	// XXX：这本来不该需要，但 libsndfile 有时会在文件末尾之前
	//      就停止读取（即 ch->frame + frame_count 小于 ch->info.frames，
	//      而 sf_readf_float 返回的帧数少于请求量）。
	//      不确定这是 xsystem4 还是 libsndfile 的 bug
	if (frame_count > 0) {
		if (!cb_loop(ch))
			return STS_STREAM_COMPLETE;
		*num_read += sf_readf_float(ch->file, out, frame_count);
	}

	return STS_STREAM_CONTINUE;
}

static float fade_curve(enum fade_curve curve, float t)
{
	switch (curve) {
	case FADE_CURVE_LOG_IN: return -logf(1.0f - t) / 7.0f;
	case FADE_CURVE_QUAD_OUT: return 1.0f - (1.0f - t) * (1.0f - t);
	case FADE_CURVE_EXPO_OUT: return (100.0f - powf(100.0f, 1.0f - t)) / 99.0f;
	case FADE_CURVE_SINE_IN: return 1.0f - cosf(t * (float)M_PI / 2.0f);
	case FADE_CURVE_SINE_IN_OUT: return (1.0f - cosf(t * (float)M_PI)) / 2.0f;
	case FADE_CURVE_SQRT_IN: return 1.0f - sqrtf(1.0f - t);
	case FADE_CURVE_LINEAR:
	default: return t;
	}
}

/*
 * 计算某个采样点上淡变的增益。
 */
static float cb_calc_fade(struct fade *fade)
{
	if (fade->elapsed >= fade->frames)
		return fade->stop ? 0.0 : fade->end_volume;

	float progress = (float)fade->elapsed / (float)fade->frames;
	float delta_v = fade->end_volume - fade->start_volume;
	float gain = fade->start_volume + delta_v * fade_curve(fade->curve, progress);
	if (gain < 0.0) return 0.0;
	if (gain > 1.0) return 1.0;
	return gain;
}

static int refill_stream(sts_mixer_sample_t *sample, void *data)
{
	struct channel *ch = data;
	uint_least32_t frames_read;
	memset(ch->data, 0, sizeof(float) * sample->length);

	// 从文件读音频数据
	int r = cb_read_frames(ch, ch->data, CHUNK_SIZE, &frames_read);

	// 单声道转立体声
	if (ch->info.channels == 1) {
		for (int i = CHUNK_SIZE-1; i >= 0; i--) {
			ch->data[i*2+1] = ch->data[i];
			ch->data[i*2] = ch->data[i];
		}
	}

	// 左右声道互换
	else if (ch->swapped) {
		for (int i = 0; i < CHUNK_SIZE; i++) {
			float tmp = ch->data[i*2];
			ch->data[i*2] = ch->data[i*2+1];
			ch->data[i*2+1] = tmp;
		}
	}

	// 设置淡变增益
	if (ch->fade.fading) {
		float gain = cb_calc_fade(&ch->fade);
		mixers[ch->mixer_no].mixer.voices[ch->voice].gain = gain;
		ch->volume = gain * 100.0;

		ch->fade.elapsed += frames_read;
		if (ch->fade.elapsed >= ch->fade.frames) {
			ch->fade.fading = false;
			ch->volume = ch->fade.end_volume * 100.0;
			if (ch->fade.stop) {
				cb_seek(ch, 0);
				r = STS_STREAM_COMPLETE;
			}
		}
	} else {
		float gain = ch->volume / 100.0;
		mixers[ch->mixer_no].mixer.voices[ch->voice].gain = gain;
	}

	if (r == STS_STREAM_COMPLETE) {
		ch->voice = -1;
	}

	return r;
}

static int refill_mixer(sts_mixer_sample_t *sample, void *data)
{
	struct mixer *mixer = data;
	sts_mixer_mix_audio(&mixer->mixer, &mixer->data, CHUNK_SIZE);
	if (mixer->muted) {
		memset(mixer->data, 0, sizeof(float) * sample->length);
	}

	return STS_STREAM_CONTINUE;
}

int channel_play(struct channel *ch)
{
	SDL_LockAudioDevice(audio_device);
	if (ch->voice >= 0) {
		SDL_UnlockAudioDevice(audio_device);
		return 1;
	}
	memset(ch->data, 0, sizeof(ch->data));
	ch->voice = sts_mixer_play_stream(&mixers[ch->mixer_no].mixer, &ch->stream, 1.0f);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_stop(struct channel *ch)
{
	SDL_LockAudioDevice(audio_device);
	if (ch->voice < 0) {
		SDL_UnlockAudioDevice(audio_device);
		return 1;
	}
	cb_seek(ch, 0);
	sts_mixer_stop_voice(&mixers[ch->mixer_no].mixer, ch->voice);
	ch->voice = -1;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_is_playing(struct channel *ch)
{
	return ch->voice >= 0;
}

int channel_set_mixer(struct channel *ch, int mixer_no)
{
	assert(ch->voice < 0);
	if (mixer_no < 0 || mixer_no >= nr_mixers)
		return 0;
	ch->mixer_no = mixer_no;
	return 1;
}

int channel_get_mixer(struct channel *ch)
{
	return ch->mixer_no;
}

int channel_set_volume(struct channel *ch, int volume)
{
	SDL_LockAudioDevice(audio_device);
	ch->volume = clamp(0, 100, volume);
	ch->fade.fading = false;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_set_loop_count(struct channel *ch, int count)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_count = max(0, count);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_get_loop_count(struct channel *ch)
{
	return ch->loop_count;
}

int channel_set_loop_start_pos(struct channel *ch, int pos)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_start = clamp(0, ch->info.frames, pos);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_set_loop_end_pos(struct channel *ch, int pos)
{
	SDL_LockAudioDevice(audio_device);
	ch->loop_end = clamp(0, ch->info.frames, pos);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

static void channel_interrupt_fade(struct channel *ch)
{
	SDL_LockAudioDevice(audio_device);
	bool stop = ch->fade.fading && ch->fade.stop;
	ch->fade.fading = false;
	SDL_UnlockAudioDevice(audio_device);
	if (stop)
		channel_stop(ch);
}

int channel_fade(struct channel *ch, int time, int volume, bool stop, enum fade_curve curve)
{
	channel_interrupt_fade(ch);
	if (!time && stop)
		return channel_stop(ch);

	// XXX：time=0 的淡变用来设音量。这个必须
	//      立即生效，而不是走音频回调 ——
	//      因为此时流不一定已经在播了。
	//      例如下面这个调用序列应当从 0 淡到 33：
	//
	//          SACT2.Music_Fade(ch, 0, 0, 0);
	//          SACT2.Music_Fade(ch, 33, 4000, 0);
	//          SACT2.Music_Play(ch);
	if (!time)
		return channel_set_volume(ch, volume);

	SDL_LockAudioDevice(audio_device);
	ch->fade.fading = true;
	ch->fade.stop = stop;
	ch->fade.start_pos = ch->frame;
	ch->fade.start_volume = (float)ch->volume / 100.0;
	ch->fade.frames = muldiv(time, ch->info.samplerate, 1000);
	ch->fade.elapsed = 0;
	ch->fade.end_volume = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	ch->fade.curve = curve;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_stop_fade(struct channel *ch)
{
	SDL_LockAudioDevice(audio_device);
	// XXX：这里需要把音量设成 end_volume，可能还要停掉
	//      流；但交给回调去做更合适
	ch->fade.elapsed = ch->fade.frames;
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

int channel_is_fading(struct channel *ch)
{
	return ch->fade.fading;
}

int channel_pause(possibly_unused struct channel *ch)
{
	// 注意：《战国兰斯》的 SACT2.dll 里没有实现
	WARNING("channel_pause not implemented");
	return 0;
}

int channel_restart(possibly_unused struct channel *ch)
{
	// 注意：《战国兰斯》的 SACT2.dll 里没有实现
	WARNING("channel_restart not implemented");
	return 0;
}

int channel_is_paused(possibly_unused struct channel *ch)
{
	return 0;
}

int channel_get_pos(struct channel *ch)
{
	return muldiv(ch->frame, 1000, ch->info.samplerate);
}

int channel_get_length(struct channel *ch)
{
	// FIXME：它和 channel_get_time_length 有什么区别？
	return muldiv(ch->info.frames, 1000, ch->info.samplerate);
}

int channel_get_sample_pos(struct channel *ch)
{
	return ch->frame;
}

int channel_get_sample_length(struct channel *ch)
{
	return ch->info.frames;
}

int channel_seek(struct channel *ch, int pos)
{
	// 注意：SACT2.Music_Seek 在《战国兰斯》里好像什么也不做……
	SDL_LockAudioDevice(audio_device);
	int r = cb_seek(ch, muldiv(pos, ch->info.samplerate, 1000));
	SDL_UnlockAudioDevice(audio_device);
	return r;
}

int channel_reverse_LR(struct channel *ch)
{
	ch->swapped = !ch->swapped;
	return 1;
}

int channel_get_volume(struct channel *ch)
{
	return ch->volume;
}

int channel_get_time_length(struct channel *ch)
{
	return muldiv(ch->info.frames, 1000, ch->info.samplerate);
}

int channel_get_data_no(struct channel *ch)
{
	return ch->dfile ? ch->dfile->no : -1;
}

static sf_count_t channel_vio_get_filelen(void *data)
{
	return ((struct channel*)data)->dfile->size;
}

static sf_count_t channel_vio_seek(sf_count_t offset, int whence, void *data)
{
	struct channel *ch = data;
	switch (whence) {
	case SEEK_CUR:
		ch->offset += offset;
		break;
	case SEEK_SET:
		ch->offset = offset;
		break;
	case SEEK_END:
		ch->offset = ch->dfile->size + offset;
		break;
	}
	ch->offset = clamp(0, (sf_count_t)ch->dfile->size, ch->offset);
	return ch->offset;
}

static sf_count_t channel_vio_read(void *ptr, sf_count_t count, void *data)
{
	struct channel *ch = data;
	sf_count_t c = min(count, (sf_count_t)ch->dfile->size - ch->offset);
	memcpy(ptr, ch->dfile->data + ch->offset, c);
	ch->offset += c;
	return c;
}

static sf_count_t channel_vio_write(possibly_unused const void *ptr,
				    possibly_unused sf_count_t count,
				    possibly_unused void *user_data)
{
	ERROR("sndfile vio write not supported");
}

static sf_count_t channel_vio_tell(void *data)
{
	return ((struct channel*)data)->offset;
}

static SF_VIRTUAL_IO channel_vio = {
	.get_filelen = channel_vio_get_filelen,
	.seek = channel_vio_seek,
	.read = channel_vio_read,
	.write = channel_vio_write,
	.tell = channel_vio_tell
};

static bool init_channel(struct channel *ch)
{
	if (sf_error(ch->file) != SF_ERR_NO_ERROR) {
		WARNING("sf_open_virtual failed: %s", sf_strerror(ch->file));
		return false;
	}
	if (ch->info.channels > 2) {
		WARNING("Audio file has more than 2 channels");
		return false;
	}

	// 创建流
	ch->stream.userdata = ch;
	ch->stream.callback = refill_stream;
	ch->stream.sample.frequency = ch->info.samplerate;
	ch->stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
	ch->stream.sample.length = CHUNK_SIZE * 2;
	ch->stream.sample.data = ch->data;
	ch->voice = -1;

	ch->volume = 100;
	ch->loop_start = 0;
	ch->loop_end = ch->info.frames;
	ch->loop_count = 1;
	ch->mixer_no = 0;

	return true;
}

struct channel *channel_open_archive_data(struct archive_data *dfile)
{
	struct channel *ch = xcalloc(1, sizeof(struct channel));
	ch->dfile = dfile;

	// 打开文件
	ch->file = sf_open_virtual(&channel_vio, SFM_READ, &ch->info, ch);

	if (!init_channel(ch)) {
		archive_free_data(dfile);
		if (ch->file)
			sf_close(ch->file);
		free(ch);
		return NULL;
	}
	return ch;
}

struct channel *channel_open_file(const char *path)
{
	struct channel *ch = xcalloc(1, sizeof(struct channel));
#ifdef _WIN32
	wchar_t *wpath = utf8_to_wchar(path);
	ch->file = sf_wchar_open(wpath, SFM_READ, &ch->info);
	free(wpath);
#else
	ch->file = sf_open(path, SFM_READ, &ch->info);
#endif

	if (!init_channel(ch)) {
		if (ch->file)
			sf_close(ch->file);
		free(ch);
		return NULL;
	}
	return ch;
}

void channel_close(struct channel *ch)
{
	channel_stop(ch);
	sf_close(ch->file);
	if (ch->dfile)
		archive_free_data(ch->dfile);
	free(ch);
}

#define SJIS_MASTER "\x83\x7d\x83\x58\x83\x5e\x81\x5b"
#define SJIS_VOICE  "\x89\xb9\x90\xba"

void mixer_init(void)
{
	// 初始化混音器命名
	if (!config.mixer_nr_channels) {
		nr_mixers = 3;
		mixers = xcalloc(nr_mixers, sizeof(struct mixer));
		mixers[0].name = strdup("Music");
		mixers[1].name = strdup("Sound");
		mixers[2].name = strdup("Master");
		master = &mixers[2];
	} else {
		// 注意：老游戏没有显式的 master 通道。
		//       为了保持一致，这种情况我们补一个混音器。
		int need_master = 1;
		if (!strcmp(config.mixer_channels[0], "Master") || !strcmp(config.mixer_channels[0], SJIS_MASTER)) {
			need_master = 0;
		}
		nr_mixers = config.mixer_nr_channels + need_master;
		mixers = xcalloc(nr_mixers, sizeof(struct mixer));
		for (unsigned i = 0; i < config.mixer_nr_channels; i++) {
			mixers[i].name = strdup(config.mixer_channels[i]);
		}
		if (need_master) {
			mixers[nr_mixers-1].name = strdup("Master");
			master = &mixers[nr_mixers-1];
		} else {
			master = &mixers[0];
		}
	}

	// 初始化混音器层级
	// 注意：Master 和 Voice 是特殊节点；列表里跟在它们
	//       后面的通道都成为它们的子节点。这看起来是
	//       System 4 里写死的行为。
	struct mixer *parent = master;
	for (int i = 0; i < nr_mixers; i++) {
		if (&mixers[i] == master)
			continue;
		mixers[i].parent = parent;
		parent->children = xrealloc_array(parent->children, parent->nr_children, parent->nr_children+1, sizeof(struct mixer*));
		parent->children[parent->nr_children++] = &mixers[i];
		if (!strcmp(mixers[i].name, "Voice") || !strcmp(mixers[i].name, SJIS_VOICE)) {
			parent = &mixers[i];
		}
	}

	// 初始化各混音器
	for (int i = 0; i < nr_mixers; i++) {
		sts_mixer_init(&mixers[i].mixer, 44100, STS_MIXER_SAMPLE_FORMAT_FLOAT);
		int volume = i < (int)config.mixer_nr_channels ? config.mixer_volumes[i] : config.default_volume;
		mixers[i].mixer.gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	}

	// 初始化混音器的流
	for (int i = 0; i < nr_mixers; i++) {
		if (&mixers[i] == master)
			continue;
		mixers[i].stream.userdata = &mixers[i];
		mixers[i].stream.callback = refill_mixer;
		mixers[i].stream.sample.frequency = 44100;
		mixers[i].stream.sample.audio_format = STS_MIXER_SAMPLE_FORMAT_FLOAT;
		mixers[i].stream.sample.length = CHUNK_SIZE * 2;
		mixers[i].stream.sample.data = mixers[i].data;
		mixers[i].voice = sts_mixer_play_stream(&mixers[i].parent->mixer, &mixers[i].stream, 1.0f);
	}

	// 初始化 SDL 音频
	SDL_AudioSpec have;
	SDL_AudioSpec want = {
		.format = AUDIO_F32,
		.freq = 44100,
		.channels = 2,
		.samples = CHUNK_SIZE,
		.callback = audio_callback,
	};
	/* XSYS4_NO_AUDIO：无头环境里跳过 SDL/ALSA 设备。 */
	if (!SDL_getenv("XSYS4_NO_AUDIO"))
		audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audio_device > 0)
		SDL_PauseAudioDevice(audio_device, 0);
}

int mixer_get_numof(void)
{
	// 返回 System40.ini 里指定的混音器数量，
	// 即使 xsystem4 后来又加了混音器。
	return config.mixer_nr_channels;
}

const char *mixer_get_name(int n)
{
	if (n < 0 || n >= config.mixer_nr_channels)
		return NULL;
	return mixers[n].name;
}

int mixer_set_name(int n, const char *name)
{
	if (n < 0 || n >= config.mixer_nr_channels)
		return 0;
	free(mixers[n].name);
	mixers[n].name = strdup(name);
	return 1;
}

int mixer_get_volume(int n, int *volume)
{
	if (n < 0 || n >= config.mixer_nr_channels) {
		return 0;
	}
	SDL_LockAudioDevice(audio_device);
	*volume = clamp(0, 100, (int)(mixers[n].mixer.gain * 100));
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}

float mixer_get_effective_volume(int n)
{
	if (n < 0 || n >= nr_mixers)
		return 0.0f;
	SDL_LockAudioDevice(audio_device);
	float volume = 1.0f;
	for (struct mixer *m = &mixers[n]; m; m = m->parent) {
		if (m->muted) {
			volume = 0.0f;
			break;
		}
		volume *= m->mixer.gain;
	}
	SDL_UnlockAudioDevice(audio_device);
	return volume;
}

int mixer_set_volume(int n, int volume)
{
	if (n < 0 || n >= config.mixer_nr_channels)
		return 0;
	SDL_LockAudioDevice(audio_device);
	mixers[n].mixer.gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	SDL_UnlockAudioDevice(audio_device);
	return 1;
}
int mixer_get_mute(int n, int *mute)
{
	if (n < 0 || n >= config.mixer_nr_channels)
		return 0;
	*mute = mixers[n].muted;
	return 1;
}

int mixer_set_mute(int n, int mute)
{
	if (n < 0 || n >= config.mixer_nr_channels)
		return 0;
	mixers[n].muted = !!mute;
	return 1;
}

int mixer_stream_play(sts_mixer_stream_t* stream, int volume)
{
	SDL_LockAudioDevice(audio_device);
	float gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	int voice = sts_mixer_play_stream(&master->mixer, stream, gain);
	SDL_UnlockAudioDevice(audio_device);
	return voice;
}

bool mixer_stream_set_volume(int voice, int volume)
{
	if (voice < 0 || voice >= STS_MIXER_VOICES)
		return false;
	SDL_LockAudioDevice(audio_device);
	master->mixer.voices[voice].gain = clamp(0.0f, 1.0f, (float)volume / 100.0f);
	SDL_UnlockAudioDevice(audio_device);
	return true;
}

void mixer_stream_stop(int voice)
{
	SDL_LockAudioDevice(audio_device);
	sts_mixer_stop_voice(&master->mixer, voice);
	SDL_UnlockAudioDevice(audio_device);
}
