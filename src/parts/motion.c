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

#include "system4.h"
#include "system4/string.h"

#include "audio.h"
#include "xsystem4.h"
#include "parts_internal.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

static inline float deg2rad(float deg)
{
	return deg * (M_PI / 180.0);
}

static TAILQ_HEAD(sound_motion_list, sound_motion) sound_motion_list =
	TAILQ_HEAD_INITIALIZER(sound_motion_list);

static struct parts_motion_list global_motion_list =
	TAILQ_HEAD_INITIALIZER(global_motion_list);

// BeginMotion 之后为真，直到 motion 结束或调用 EndMotion
static bool is_motion = false;
// motion 暂停期间为真（时间不推进）
static bool is_motion_paused = false;
// 当前 motion 已经过去的时间
static int motion_t = 0;
// 当前 motion 的结束时间
static int motion_end_t = 0;

static struct parts_motion *parts_motion_alloc(enum parts_motion_type type, int begin_time, int end_time)
{
	struct parts_motion *motion = xcalloc(1, sizeof(struct parts_motion));
	motion->type = type;
	motion->begin_time = begin_time;
	motion->end_time = end_time;
	return motion;
}

static void parts_motion_free(struct parts_motion *motion)
{
	free(motion);
}

static void parts_motion_list_clear(struct parts_motion_list *motion_list)
{
	while (!TAILQ_EMPTY(motion_list)) {
		struct parts_motion *motion = TAILQ_FIRST(motion_list);
		TAILQ_REMOVE(motion_list, motion, entry);
		parts_motion_free(motion);
	}
}

static void parts_motion_list_add(struct parts_motion_list *motion_list, struct parts_motion *motion)
{
	struct parts_motion *p;
	TAILQ_FOREACH(p, motion_list, entry) {
		if (p->begin_time > motion->begin_time) {
			TAILQ_INSERT_BEFORE(p, motion, entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(motion_list, motion, entry);
	if (motion->end_time > motion_end_t)
		motion_end_t = motion->end_time;
	// FIXME？如果 is_motion=true 时再加一个 motion 会怎样？
	//        这里是不是该调 parts_update_motion？
}

void parts_clear_motion(struct parts *parts)
{
	parts_motion_list_clear(&parts->motion);
}

static void parts_init_all_motion(void);

void parts_add_motion(struct parts *parts, struct parts_motion *motion)
{
	parts_motion_list_add(&parts->motion, motion);
	/* [D186] 清偿 `[D119-MOTION]`：Rance10 **从不**调用
	 * `PartsEngine.BeginMotion`（实测 0 次），于是 motion_t 永远停在 0，
	 * 所有淡入保持 begin 值——卡片/按钮的 local.alpha 一直是 0，部件全透明
	 * （卡片立绘合成正确却看不见就是这个）。
	 * 首次入队时自动开表。不能走 `PE_BeginMotion()`：它开头
	 * `if (parts_began_click) return;`，而编队界面正处在点击窗口内。
	 * `XSYS4_NO_AUTO_MOTION=1` 回退。 */
	static int auto_motion = -1;
	if (auto_motion < 0) {
		const char *e = getenv("XSYS4_NO_AUTO_MOTION");
		auto_motion = (e && *e == '1') ? 0 : 1;
	}
	if (auto_motion && !is_motion) {
		motion_t = 0;
		is_motion = true;
		is_motion_paused = false;
		parts_init_all_motion();
	}
}

static inline float motion_progress(struct parts_motion *m, int t)
{
	return (float)(t - m->begin_time) / (m->end_time - m->begin_time);
}

static int motion_calculate_i(struct parts_motion *m, int t)
{
	if (t >= m->end_time)
		return m->end.i;

	const int delta = m->end.i - m->begin.i;
	const float progress = motion_progress(m, t);
	return m->begin.i + (delta * progress);
}

static float motion_calculate_f(struct parts_motion *m, int t)
{
	if (t >= m->end_time)
		return m->end.f;

	const float delta = m->end.f - m->begin.f;
	const float progress = motion_progress(m, t);
	return m->begin.f + (delta * progress);
}

static Point motion_calculate_point(struct parts_motion *m, int t)
{
	if (t >= m->end_time)
		return (Point) { m->end.x, m->end.y };

	const int delta_x = m->end.x - m->begin.x;
	const int delta_y = m->end.y - m->begin.y;
	const float progress = motion_progress(m, t);
	return (Point) {
		m->begin.x + (delta_x * progress),
		m->begin.y + (delta_y * progress)
	};
}

static Point motion_vibration_point(struct parts_motion *m, int t)
{
	if (t >= m->end_time)
		return (Point) { m->end.x, m->end.y };

	const float progress = motion_progress(m, t);
	const int range_x = m->begin.x * (1.f - progress);
	const int range_y = m->begin.y * (1.f - progress);
	return (Point) {
		m->end.x + (range_x ? (rand() % range_x - range_x / 2) : 0),
		m->end.y + (range_y ? (rand() % range_y - range_y / 2) : 0),
	};
}

static void parts_update_with_motion(struct parts *parts, struct parts_motion *motion)
{
	switch (motion->type) {
	case PARTS_MOTION_POS:
		parts_set_pos(parts, motion_calculate_point(motion, motion_t));
		break;
	case PARTS_MOTION_ALPHA:
		parts_set_alpha(parts, motion_calculate_i(motion, motion_t));
		break;
	case PARTS_MOTION_CG: {
		int i = motion_calculate_i(motion, motion_t);
		if (i == 0) {
			parts_state_reset(&parts->states[parts->state], PARTS_CG);
		} else {
			parts_cg_set_by_index(parts, parts_get_cg(parts, parts->state), i);
		}
		break;
	}
	case PARTS_MOTION_HGAUGE_RATE:
		parts_hgauge_set_rate(parts, parts_get_hgauge(parts, parts->state),
				motion_calculate_f(motion, motion_t));
		break;
	case PARTS_MOTION_VGAUGE_RATE:
		parts_vgauge_set_rate(parts, parts_get_vgauge(parts, parts->state),
				motion_calculate_f(motion, motion_t));
		break;
	case PARTS_MOTION_NUMERAL_NUMBER:
		parts_numeral_set_number(parts, parts_get_numeral(parts, parts->state),
				motion_calculate_i(motion, motion_t));
		break;
	case PARTS_MOTION_MAG_X:
		parts_set_scale_x(parts, motion_calculate_f(motion, motion_t));
		break;
	case PARTS_MOTION_MAG_Y:
		parts_set_scale_y(parts, motion_calculate_f(motion, motion_t));
		break;
		/*
		  case PARTS_MOTION_ROTATE_X:
		  parts->rotation.x = motion_calculate_f(motion, motion_t);
		  sprite_dirty(&parts->sp);
		  break;
		  case PARTS_MOTION_ROTATE_Y:
		  parts->rotation.y = motion_calculate_f(motion, motion_t);
		  sprite_dirty(&parts->sp);
		  break;
		*/
	case PARTS_MOTION_ROTATE_Z:
		parts_set_rotation_z(parts, motion_calculate_f(motion, motion_t));
		break;
	case PARTS_MOTION_VIBRATION_SIZE:
		parts_set_pos(parts, motion_vibration_point(motion, motion_t));
		break;
	default:
		WARNING("Invalid motion type: %d", motion->type);
	}
}

static void parts_update_global_motion(void)
{
	struct parts_motion *motion;
	TAILQ_FOREACH(motion, &global_motion_list, entry) {
		switch (motion->type) {
		case PARTS_MOTION_VIBRATION_SIZE:
			parts_set_global_pos(motion_vibration_point(motion, motion_t));
			break;
		default:
			WARNING("Invalid global motion type: %d", motion->type);
		}
	}
}

static void parts_update_all_motion(void)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		struct parts_motion *motion;
		TAILQ_FOREACH(motion, &parts->motion, entry) {
			if (motion->begin_time > motion_t)
				break;
			// FIXME？如果一个 motion 在另一个的时间范围内开始并结束呢？
			//        这个实现会取消先前的 motion，停在
			//        第二个 motion 的末状态。
			parts_update_with_motion(parts, motion);
		}
	}
	parts_update_global_motion();

	struct sound_motion *sound;
	TAILQ_FOREACH(sound, &sound_motion_list, entry) {
		if (sound->begin_time > motion_t)
			break;
		if (sound->played)
			continue;
		audio_play_sound(sound->sound_no);
		sound->played = true;
	}
}

/*
 * 注意：如果一次 motion 从 t=100 开始、初始值 v=0，
 *       那这个值就会成为 t=0 时的初值。
 */
static void parts_init_all_motion(void)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		struct parts_motion *motion;
		bool initialized[PARTS_NR_MOTION_TYPES] = {0};
		TAILQ_FOREACH(motion, &parts->motion, entry) {
			if (!initialized[motion->type]) {
				parts_update_with_motion(parts, motion);
				initialized[motion->type] = true;
			}
		}
	}
	parts_update_global_motion();
}

static void parts_fini_all_motion(void)
{
	struct parts *parts;
	PARTS_LIST_FOREACH(parts) {
		parts_clear_motion(parts);
	}

	parts_motion_list_clear(&global_motion_list);

	while (!TAILQ_EMPTY(&sound_motion_list)) {
		struct sound_motion *motion = TAILQ_FIRST(&sound_motion_list);
		TAILQ_REMOVE(&sound_motion_list, motion, entry);
		free(motion);
	}
}

void PE_AddMotionPos(int parts_no, int begin_x, int begin_y, int end_x, int end_y, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_POS, begin_t, end_t);
	motion->begin.x = begin_x;
	motion->begin.y = begin_y;
	motion->end.x = end_x;
	motion->end.y = end_y;
	parts_add_motion(parts, motion);
}

void PE_AddMotionPos_curve(int parts_no, int begin_x, int begin_y, int end_x, int end_y,
		int begin_t, int end_t, struct string *curve_name)
{
	// TODO：用曲线
	PE_AddMotionPos(parts_no, begin_x, begin_y, end_x, end_y, begin_t, end_t);
}

void PE_AddMotionAlpha(int parts_no, int begin_a, int end_a, int begin_t, int end_t)
{
	if (getenv("XSYS4_ALPHA_DBG"))
		NOTICE("[alpha] motion parts=%d %d->%d t=%d..%d",
		       parts_no, begin_a, end_a, begin_t, end_t);
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_ALPHA, begin_t, end_t);
	motion->begin.i = begin_a;
	motion->end.i = end_a;
	parts_add_motion(parts, motion);
}

void PE_AddMotionAlpha_curve(int parts_no, int begin_a, int end_a, int begin_t, int end_t,
		struct string *curve_name)
{
	// TODO：用曲线
	PE_AddMotionAlpha(parts_no, begin_a, end_a, begin_t, end_t);
}

void PE_AddMotionCG_by_index(int parts_no, int begin_cg_no, int nr_cg, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_CG, begin_t, end_t);
	motion->begin.i = begin_cg_no;
	motion->end.i = begin_cg_no + nr_cg - 1;
	parts_add_motion(parts, motion);
}

void PE_AddMotionHGaugeRate(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_HGAUGE_RATE, begin_t, end_t);
	motion->begin.f = begin_numerator / begin_denominator;
	motion->end.f = end_numerator / end_denominator;
	parts_add_motion(parts, motion);
}

void PE_AddMotionHGaugeRate_curve(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t,
			    struct string *curve_name)
{
	// TODO：用曲线
	PE_AddMotionHGaugeRate(parts_no, begin_numerator, begin_denominator,
			end_numerator, end_denominator, begin_t, end_t);
}

void PE_AddMotionVGaugeRate(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_VGAUGE_RATE, begin_t, end_t);
	motion->begin.f = begin_numerator / begin_denominator;
	motion->end.f = end_numerator / end_denominator;
	parts_add_motion(parts, motion);
}

void PE_AddMotionVGaugeRate_curve(int parts_no, float begin_numerator, float begin_denominator,
			    float end_numerator, float end_denominator, int begin_t, int end_t,
			    struct string *curve_name)
{
	// TODO：用曲线
	PE_AddMotionVGaugeRate(parts_no, begin_numerator, begin_denominator,
			end_numerator, end_denominator, begin_t, end_t);
}

void PE_AddMotionNumeralNumber(int parts_no, int begin_n, int end_n, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_NUMERAL_NUMBER, begin_t, end_t);
	motion->begin.i = begin_n;
	motion->end.i = end_n;
	parts_add_motion(parts, motion);
}

void PE_AddMotionNumeralNumber_curve(int parts_no, int begin_n, int end_n, int begin_t,
		int end_t, struct string *curve_name)
{
	// TODO：用曲线
	PE_AddMotionNumeralNumber(parts_no, begin_n, end_n, begin_t, end_t);
}

void PE_AddMotionMagX(int parts_no, float begin, float end, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_MAG_X, begin_t, end_t);
	motion->begin.f = begin;
	motion->end.f = end;
	parts_add_motion(parts, motion);
}

void PE_AddMotionMagX_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name)
{
	// TODO：用曲线
	PE_AddMotionMagX(parts_no, begin, end, begin_t, end_t);
}

void PE_AddMotionMagY(int parts_no, float begin, float end, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_MAG_Y, begin_t, end_t);
	motion->begin.f = begin;
	motion->end.f = end;
	parts_add_motion(parts, motion);
}

void PE_AddMotionMagY_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name)
{
	// TODO：用曲线
	PE_AddMotionMagY(parts_no, begin, end, begin_t, end_t);
}

void PE_AddMotionRotateX(int parts_no, float begin, float end, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_ROTATE_X, begin_t, end_t);
	motion->begin.f = deg2rad(begin);
	motion->end.f = deg2rad(end);
	parts_add_motion(parts, motion);
}

void PE_AddMotionRotateX_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name)
{
	// TODO：用曲线
	PE_AddMotionRotateX(parts_no, begin, end, begin_t, end_t);
}

void PE_AddMotionRotateY(int parts_no, float begin, float end, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_ROTATE_Y, begin_t, end_t);
	motion->begin.f = deg2rad(begin);
	motion->end.f = deg2rad(end);
	parts_add_motion(parts, motion);
}

void PE_AddMotionRotateY_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name)
{
	// TODO：用曲线
	PE_AddMotionRotateY(parts_no, begin, end, begin_t, end_t);
}

void PE_AddMotionRotateZ(int parts_no, float begin, float end, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_ROTATE_Z, begin_t, end_t);
	motion->begin.f = deg2rad(begin);
	motion->end.f = deg2rad(end);
	parts_add_motion(parts, motion);
}

void PE_AddMotionRotateZ_curve(int parts_no, float begin, float end, int begin_t, int end_t,
		struct string *curve_name)
{
	// TODO：用曲线
	PE_AddMotionRotateZ(parts_no, begin, end, begin_t, end_t);
}

void PE_AddMotionVibrationSize(int parts_no, int begin_w, int begin_h, int begin_t, int end_t)
{
	struct parts *parts = parts_get(parts_no);
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_VIBRATION_SIZE, begin_t, end_t);
	motion->begin.x = begin_w;
	motion->begin.y = begin_h;
	motion->end.x = parts->local.pos.x;
	motion->end.y = parts->local.pos.y;
	parts_add_motion(parts, motion);
}

void PE_AddWholeMotionVibrationSize(int begin_w, int begin_h, int begin_t, int end_t)
{
	struct parts_motion *motion = parts_motion_alloc(PARTS_MOTION_VIBRATION_SIZE, begin_t, end_t);
	motion->begin.x = begin_w;
	motion->begin.y = begin_h;
	motion->end.x = 0;
	motion->end.y = 0;
	parts_motion_list_add(&global_motion_list, motion);
}

void PE_AddMotionSound(int sound_no, int begin_t)
{
	struct sound_motion *m = xcalloc(1, sizeof(struct sound_motion));
	m->sound_no = sound_no;
	m->begin_time = begin_t;

	struct sound_motion *p;
	TAILQ_FOREACH(p, &sound_motion_list, entry) {
		if (p->begin_time > m->begin_time) {
			TAILQ_INSERT_BEFORE(p, m, entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(&sound_motion_list, m, entry);
	if (m->begin_time > motion_end_t)
		motion_end_t = m->begin_time;
}

void PE_BeginMotion(void)
{
	if (parts_began_click)
		return;

	// FIXME：启动一次 motion 似乎会清掉非默认状态
	motion_t = 0;
	is_motion = true;
	is_motion_paused = false;
	parts_init_all_motion();
}

void PE_EndMotion(void)
{
	if (parts_began_click)
		return;
	motion_t = 0;
	motion_end_t = 0;
	is_motion = false;
	is_motion_paused = false;
	parts_fini_all_motion();
}

void PE_SetMotionTime(int t)
{
	if (!is_motion)
		return;
	if (motion_t == t)
		return;
	motion_t = t;
	parts_update_all_motion();
	if (t >= motion_end_t)
		PE_EndMotion();
}

void PE_SeekEndMotion(void)
{
	PE_SetMotionTime(motion_end_t - 1);
}

static int msgskip_speedup_rate = 1;

/* AIN 最后一次驱动 motion 时钟的墙上时间。AIN 从没调过
 * UpdateMotionTime 时是 0（兰斯10 就是如此：它的场景靠
 * Parts.Motion 做动画，但场景等待期间字节码里没有任何东西去推它）。 */
static uint32_t last_explicit_motion_tick;

void PE_UpdateMotionTime(int time, bool skip)
{
	last_explicit_motion_tick = SDL_GetTicks();
	if (is_motion_paused)
		return;
	if (skip)
		PE_SetMotionTime(motion_t + time * msgskip_speedup_rate);
	else
		PE_SetMotionTime(motion_t + time);
}

/* 没人推的时候，由这里推进 motion 时钟。
 *
 * 整个 Parts.Motion 系统（淡入、滑动）都靠 motion_t 驱动，
 * 按设计应由 AIN 通过 PartsEngine.UpdateMotionTime() 来推进。
 * 兰斯10 从不调它：200 秒跑测里测到 0 次调用，
 * 于是每个 motion 都停在 t=0，标题菜单按钮的 alpha 永远是 0。
 * VM 帧泵每帧调这里一次；如果 AIN 哪天自己开始推进，
 * 我们就退让，两者绝不会叠加。 */
void PE_AdvanceMotionClock(int passed_time)
{
	if (!is_motion || is_motion_paused || passed_time <= 0)
		return;
	if (last_explicit_motion_tick &&
	    SDL_GetTicks() - last_explicit_motion_tick < 32)
		return;
	PE_SetMotionTime(motion_t + passed_time);
}

void PE_SetSpeedupRateByMessageSkip(int parts_no, int rate)
{
	msgskip_speedup_rate = max(1, rate);
}

int PE_GetSpeedupRateByMessageSkip(int parts_no)
{
	return msgskip_speedup_rate;
}

bool PE_IsMotion(void)
{
	return is_motion;
}

int PE_GetMotionEndTime(void)
{
	return motion_end_t;
}

/* [D389] motion 起点帧。全局 motion 时钟从 0 起，故恒 0。
 * 与 `PE_GetMotionEndTime` 配对给 `PartsEngine.GetMotionBegin/EndFrame`，
 * 它们是 AIN 侧 `CASTask@IsEndTask` 判「到没到结束帧」的唯一入口。 */
int PE_GetMotionBeginFrame(void)
{
	return 0;
}

void PE_PauseMotion(bool pause)
{
	if (parts_began_click)
		return;
	if (!is_motion)
		return;
	if (is_motion_paused == pause)
		return;
	is_motion_paused = pause;

	// 暂停期间，逐帧推进不再更新整屏震动偏移。
	// 把它复位到原点，免得画面
	// 卡在上一次的抖动位置上。
	if (pause)
		parts_set_global_pos((Point){ 0, 0 });
}
