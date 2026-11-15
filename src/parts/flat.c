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

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <cglm/cglm.h>

#include "system4/mt19937int.h"

#include "system4.h"
#include "system4/archive.h"
#include "system4/cg.h"
#include "system4/flat.h"
#include "system4/string.h"

#include "asset_manager.h"
#include "gfx/gfx.h"
#include "parts.h"
#include "parts_internal.h"
#include "xsystem4.h"

enum emitter_direction_type {
	EMITTER_DIRECTION_RANDOM         = 0,
	EMITTER_DIRECTION_FIXED          = 1,
	EMITTER_DIRECTION_PARENT         = 2,
	EMITTER_DIRECTION_PARENT_REVERSE = 3,
	EMITTER_DIRECTION_DISK           = 4,
};

enum emitter_create_pos_type {
	EMITTER_CREATE_POS_NONE   = 0,
	EMITTER_CREATE_POS_SPHERE = 1,
	EMITTER_CREATE_POS_CIRCLE = 2,
	EMITTER_CREATE_POS_RECT   = 3,
};

enum emitter_parent_key_mode {
	EMITTER_PARENT_KEY_SELECTIVE         = 0,
	EMITTER_PARENT_KEY_ANCESTOR_CURRENT  = 1,
	EMITTER_PARENT_KEY_ANCESTOR_AT_BIRTH = 2,
};

static inline float mt_next(struct mt19937 *mt)
{
	return mt19937_genrand(mt) / 4294967296.0f;
}

static inline float mt_next_signed(struct mt19937 *mt)
{
	return mt_next(mt) - 0.5f;
}

// 近似均匀随机的三维单位向量。
static void random_unit_vec3(struct mt19937 *mt, vec3 v)
{
	v[0] = mt_next_signed(mt);
	v[1] = mt_next_signed(mt);
	v[2] = mt_next_signed(mt);
	glm_vec3_normalize(v);
}

static struct flat_layer_state *flat_layer_state_new(size_t nr_timelines)
{
	struct flat_layer_state *s = xcalloc(1, sizeof(struct flat_layer_state));
	s->suppress_advance = true;
	s->jump_target = -1;
	s->nr_timelines = nr_timelines;
	s->last_script_keys = xmalloc(nr_timelines * sizeof(int));
	for (size_t i = 0; i < nr_timelines; i++)
		s->last_script_keys[i] = -2;  // uninitialized
	s->children = xcalloc(nr_timelines, sizeof(struct flat_layer_state *));
	return s;
}

static void flat_layer_state_free(struct flat_layer_state *s)
{
	if (!s)
		return;
	for (size_t i = 0; i < s->nr_timelines; i++)
		flat_layer_state_free(s->children[i]);
	free(s->children);
	free(s->last_script_keys);
	free(s);
}

void parts_flat_free(struct parts_flat *f)
{
	if (f->flat)
		flat_free(f->flat);
	if (f->name)
		free_string(f->name);
	flat_layer_state_free(f->root_state);
	if (f->textures) {
		for (size_t i = 0; i < f->nr_libraries; i++)
			gfx_delete_texture(&f->textures[i]);
		free(f->textures);
	}
	if (f->stop_motion_frames) {
		for (size_t i = 0; i < f->nr_libraries; i++)
			free(f->stop_motion_frames[i].lib_indices);
		free(f->stop_motion_frames);
	}
}

int parts_flat_find_library(struct flat *fl, const char *name)
{
	for (size_t i = 0; i < fl->nr_libraries; i++) {
		if (strcmp(fl->libraries[i].name->text, name) == 0)
			return (int)i;
	}
	return -1;
}

static int flat_compute_max_frame(struct flat_timeline *timelines, size_t nr_timelines)
{
	int max_frame = 0;
	for (size_t i = 0; i < nr_timelines; i++) {
		int end = timelines[i].begin_frame + timelines[i].frame_count;
		if (end > max_frame)
			max_frame = end;
	}
	return max_frame;
}

// 处理当前帧的脚本 timeline。
static void flat_process_layer_scripts(struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines)
{
	if (state->stopped)
		return;
	for (size_t i = 0; i < nr_timelines; i++) {
		if (timelines[i].type != FLAT_TIMELINE_SCRIPT)
			continue;

		// 找最后一个满足 frame_index <= current_frame 的关键帧
		int matched = -1;
		for (size_t j = 0; j < timelines[i].script.count; j++) {
			if ((int)timelines[i].script.keys[j].frame_index <= state->current_frame)
				matched = (int)j;
			else
				break;
		}

		// 只有匹配到的关键帧变了才触发
		if (state->last_script_keys[i] == matched)
			continue;
		state->last_script_keys[i] = matched;
		if (matched < 0)
			continue;

		struct flat_script_key *key = &timelines[i].script.keys[matched];
		if (key->has_jump) {
			state->jump_target = key->jump_frame;
		} else if (key->is_stop) {
			state->stopped = true;
		}
	}
}

// 不管本层是否停止，都要推进子层。
static bool flat_advance_children(struct flat *fl,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines);

// 把一层推进一个 tick。有任何帧变化就返回 true。
static bool flat_advance_layer(struct flat *fl,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines)
{
	int max_end_frame = flat_compute_max_frame(timelines, nr_timelines) - 1;
	int prev_frame = state->current_frame;

	do {
		int next_frame;
		if (state->jump_target != -1) {
			next_frame = state->jump_target;
			state->jump_target = -1;
		} else {
			int increment = (state->stopped || state->suppress_advance) ? 0 : 1;
			next_frame = state->current_frame + increment;
		}

		state->suppress_advance = false;
		state->current_frame = min(max(0, next_frame), max_end_frame);

		flat_process_layer_scripts(state, timelines, nr_timelines);
	} while (state->jump_target != -1);

	bool child_changed = flat_advance_children(fl, state, timelines, nr_timelines);
	return state->current_frame != prev_frame || child_changed;
}

static bool flat_advance_children(struct flat *fl,
		struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines)
{
	bool changed = false;
	for (size_t i = 0; i < nr_timelines; i++) {
		if (timelines[i].type != FLAT_TIMELINE_GRAPHIC)
			continue;

		// 父帧离开活动范围时，销毁子状态。
		int local = state->current_frame - timelines[i].begin_frame;
		if (local < 0 || local >= timelines[i].frame_count) {
			if (state->children[i]) {
				flat_layer_state_free(state->children[i]);
				state->children[i] = NULL;
			}
			continue;
		}

		int lib_idx = parts_flat_find_library(fl, timelines[i].library_name->text);
		if (lib_idx < 0)
			continue;
		struct flat_library *lib = &fl->libraries[lib_idx];
		if (lib->type != FLAT_LIB_TIMELINE)
			continue;

		if (!state->children[i])
			state->children[i] = flat_layer_state_new(lib->timeline.nr_timelines);
		if (flat_advance_layer(fl, state->children[i],
				lib->timeline.timelines, lib->timeline.nr_timelines)) {
			changed = true;
		}
	}
	return changed;
}

// 为 STOP_MOTION 库构造 CG 列表：第 0 帧是
// `sm->library_name` 指定的基础库；后续帧是编号变体
// `${stem}_NN.${ext}`，遇到第一个缺失项就停。
static void build_stop_motion_frames(struct parts_flat *f, int sm_lib_idx)
{
	struct flat_stop_motion *sm = &f->flat->libraries[sm_lib_idx].stop_motion;
	const char *base = sm->library_name->text;

	int base_idx = parts_flat_find_library(f->flat, base);
	if (base_idx < 0)
		return;

	const char *dot = strrchr(base, '.');
	if (!dot) {
		WARNING("flat: invalid STOP_MOTION library name '%s'", base);
		return;
	}

	int *indices = xmalloc(sizeof(int));
	indices[0] = base_idx;
	int count = 1;

	char name_buf[512];
	for (int i = 1; ; i++) {
		snprintf(name_buf, sizeof(name_buf), "%.*s_%02d.%s",
				(int)(dot - base), base, i, dot + 1);
		int idx = parts_flat_find_library(f->flat, name_buf);
		if (idx < 0)
			break;
		indices = xrealloc(indices, (count + 1) * sizeof(int));
		indices[count++] = idx;
	}

	f->stop_motion_frames[sm_lib_idx].lib_indices = indices;
	f->stop_motion_frames[sm_lib_idx].count = count;
}

static int stop_motion_frame_index(int loop_type, int span, int local, int total)
{
	if (total <= 1)
		return 0;
	int idx = (span > 0) ? local / span : 0;
	switch (loop_type) {
	case 0:  // stop at last frame
		return min(idx, total - 1);
	case 1:  // loop
		return idx % total;
	case 2: {  // ping-pong
		int period = total * 2 - 2;
		idx = idx % period;
		return idx < total ? idx : period - idx;
	}
	default:
		return 0;
	}
}

int parts_flat_stop_motion_get_cg_lib(struct parts_flat *f, int sm_lib_idx, int local)
{
	if (sm_lib_idx < 0 || (size_t)sm_lib_idx >= f->nr_libraries)
		return -1;
	struct flat_stop_motion_frames *frames = &f->stop_motion_frames[sm_lib_idx];
	if (frames->count <= 0)
		return -1;
	struct flat_stop_motion *sm = &f->flat->libraries[sm_lib_idx].stop_motion;
	int frame = stop_motion_frame_index(sm->loop_type, sm->span, local, frames->count);
	return frames->lib_indices[frame];
}

bool parts_flat_load(struct parts *parts, struct parts_flat *f, struct string *filename)
{
	if (f->flat) {
		parts_flat_free(f);
		memset(f, 0, sizeof(struct parts_flat));
	}

	if (!filename)
		return false;

	struct archive_data *dfile = asset_get_by_name(ASSET_FLAT, filename->text, NULL);
	if (!dfile)
		return false;

	int error;
	f->flat = flat_open(dfile->data, dfile->size, &error);
	if (!f->flat) {
		archive_free_data(dfile);
		return false;
	}

	f->name = string_dup(filename);
	f->stopped = false;
	f->elapsed = 0;
	f->end_frame = flat_compute_max_frame(
			f->flat->timelines, f->flat->nr_timelines) - 1;
	f->root_state = flat_layer_state_new(f->flat->nr_timelines);

	// 装载各 CG 库的纹理。
	/* [D200] 有些 flat（任务画面／選択肢／红按钮）把图形放在 TALT 段的
	 * QNT 图集里，LIBL 段声明 0 个库。timeline 的 library_name 找不到库，
	 * render_flat_item 会直接 return —— 按钮一个像素都画不出来。
	 * 追加一张 TALT 图集纹理，渲染侧按索引回退；render_flat_cg 已经会用
	 * key->area_x/y/w/h 从图集裁剪。
	 * 默认开启：这是 FLAT/TALT 格式支持，不是实验开关。 */
	bool talt_extra = false;
	if (f->flat->nr_libraries == 0 &&
	    f->flat->nr_talt_entries > 0) {
		struct talt_entry *e = &f->flat->talt_entries[0];
		if (e->size > 4 && e->off + e->size <= dfile->size &&
		    dfile->data[e->off] == 'Q' && dfile->data[e->off+1] == 'N' &&
		    dfile->data[e->off+2] == 'T')
			talt_extra = true;
	}
	f->nr_libraries = f->flat->nr_libraries + (talt_extra ? 1 : 0);
	f->textures = xcalloc(f->nr_libraries, sizeof(Texture));
	f->stop_motion_frames = xcalloc(f->nr_libraries,
			sizeof(struct flat_stop_motion_frames));
	for (size_t i = 0; i < f->flat->nr_libraries; i++) {
		struct flat_library *lib = &f->flat->libraries[i];
		if (lib->type != FLAT_LIB_CG)
			continue;
		struct cg *cg = cg_load_buffer((uint8_t *)lib->cg.data, lib->cg.size);
		if (!cg) {
			WARNING("flat: failed to load CG for library '%s'", lib->name->text);
			continue;
		}
		gfx_init_texture_with_cg(&f->textures[i], cg);
		cg_free(cg);
	}
	if (talt_extra) {
		struct talt_entry *e = &f->flat->talt_entries[0];
		struct cg *cg = cg_load_buffer(dfile->data + e->off, e->size);
		if (cg) {
			gfx_init_texture_with_cg(&f->textures[f->flat->nr_libraries], cg);
			cg_free(cg);
		}
	}
	for (size_t i = 0; i < f->flat->nr_libraries; i++) {
		if (f->flat->libraries[i].type == FLAT_LIB_STOP_MOTION)
			build_stop_motion_frames(f, (int)i);
	}
	for (size_t i = 0; i < f->flat->nr_libraries; i++) {
		if (f->flat->libraries[i].type != FLAT_LIB_EMITTER)
			continue;
		struct flat_emitter *em = &f->flat->libraries[i].emitter;
		if (em->end_pos_type != 0)
			WARNING("flat: emitter '%s': unsupported end_pos_type %d",
					display_sjis0(em->library_name->text), em->end_pos_type);
		if (em->pos_track_mode == 0)
			WARNING("flat: emitter '%s': unsupported pos_track_mode %d",
					display_sjis0(em->library_name->text), em->pos_track_mode);
	}

	// TODO：原版引擎是对 flat 的可见精灵做逐帧命中测试
	// （带完整变换链），而不是
	// 用静态命中盒。目前先按 CG 纹理估算尺寸。
	int w = f->flat->hdr.width;
	int h = f->flat->hdr.height;
	if (w <= 0 || h <= 0) {
		w = 0;
		h = 0;
		for (size_t i = 0; i < f->nr_libraries; i++) {
			if (f->textures[i].w > w)
				w = f->textures[i].w;
			if (f->textures[i].h > h)
				h = f->textures[i].h;
		}
	}
	if (w > 0 && h > 0)
		parts_set_dims(parts, &f->common, w, h);

	// f->flat 里有指针指向 dfile->data，但现在释放应该是安全的，
	// 因为我们需要的 CG 数据都已经读完了。
	archive_free_data(dfile);
	return true;
}

/* [D226] 这个 flat 在当前帧**最多能画出多大 alpha**（0..255）。
 *
 * 只算真会把像素落到屏幕上的层：CG 库要有纹理、STOP_MOTION 要能映射到
 * 有效 CG、TALT 图集要真的加载了；TIMELINE 库递归下去。
 *
 * 遮挡推断（`parts_update_cover_layer`）用它而不是 `global.alpha`：整屏
 * flat 即使 nominal alpha=255，只要内部 key 已经淡到 0 或纹理没装载，就
 * 画不出不透明的一层，不该当"视觉地板"把下面全滤掉（H038/H039）。
 *
 * 为什么不靠渲染侧反馈：场景稳定后精灵不再变脏，`scene_render` 不会重画，
 * 反馈值就永远停在"没画过"，与旧口径等价（实测 paint=-1 一直不变）。 */
static int flat_layer_paint_alpha(struct parts_flat *f, struct flat_layer_state *state,
		struct flat_timeline *timelines, size_t nr_timelines,
		int parent_alpha, int depth)
{
	if (!state || depth > 8)
		return 0;
	int best = 0;
	for (size_t i = 0; i < nr_timelines; i++) {
		struct flat_timeline *tl = &timelines[i];
		if (tl->type != FLAT_TIMELINE_GRAPHIC)
			continue;
		int local = state->current_frame - tl->begin_frame;
		if (local < 0 || local >= tl->frame_count)
			continue;

		int lib_idx = parts_flat_find_library(f->flat, tl->library_name->text);
		struct flat_library *lib = (lib_idx >= 0 && (size_t)lib_idx < f->flat->nr_libraries)
				? &f->flat->libraries[lib_idx] : NULL;
		bool drawable = false;
		if (lib) {
			switch (lib->type) {
			case FLAT_LIB_CG:
				drawable = (size_t)lib_idx < f->nr_libraries &&
					   f->textures[lib_idx].handle != 0;
				break;
			case FLAT_LIB_STOP_MOTION: {
				int cg = parts_flat_stop_motion_get_cg_lib(f, lib_idx, local);
				drawable = cg >= 0 && (size_t)cg < f->nr_libraries &&
					   f->textures[cg].handle != 0;
				break;
			}
			case FLAT_LIB_TIMELINE:
				drawable = true;  /* 像素在子 timeline 里，下面递归 */
				break;
			default:
				break;
			}
		} else if (f->nr_libraries > f->flat->nr_libraries &&
			   f->flat->nr_talt_entries > 0) {
			/* LIBL 为空、图形在 TALT 图集里（D200）：渲染侧按图集裁剪。 */
			drawable = f->textures[f->flat->nr_libraries].handle != 0;
		}
		if (!drawable)
			continue;

		struct flat_key_data_graphic *keys;
		uint32_t nr_keys;
		if (tl->graphic.frames) {
			keys = tl->graphic.frames[local].keys;
			nr_keys = tl->graphic.frames[local].count;
		} else {
			if (local >= (int)tl->graphic.count)
				continue;
			keys = &tl->graphic.keys[local];
			nr_keys = 1;
		}
		for (uint32_t k = 0; k < nr_keys; k++) {
			int a = keys[k].alpha * parent_alpha / 255;
			if (keys[k].alpha > 255)
				a = parent_alpha;
			if (a > best)
				best = a;
			if (lib && lib->type == FLAT_LIB_TIMELINE) {
				struct flat_layer_state *child = state->children
						? state->children[i] : NULL;
				if (child) {
					int sub = flat_layer_paint_alpha(f, child,
							lib->timeline.timelines,
							lib->timeline.nr_timelines,
							a, depth + 1);
					if (sub > best)
						best = sub;
				}
			}
		}
	}
	return best;
}

int parts_flat_max_paint_alpha(struct parts_flat *f)
{
	if (!f->flat || !f->root_state)
		return 0;
	return flat_layer_paint_alpha(f, f->root_state,
			f->flat->timelines, f->flat->nr_timelines, 255, 0);
}

bool parts_flat_update(struct parts_flat *f, int passed_time)
{
	/* [D194] `fps <= 0` 只表示「这张 flat 不按时间推进」，**不能**把
	 * `needs_advance` 的首次初始化一起挡掉。原先这一句在函数最前面，
	 * 静态 flat（`hdr.fps == 0`）永远轮不到 `flat_advance_layer()`，
	 * `root_state` 停在未初始化状态 —— `parts_render_flat` 的准入通过了、
	 * 部件几何也对，但 `render_flat_layer` 一个像素都画不出来。
	 * 任务地图的選択肢按钮（`任务画面/シス／任务／选择肢／红按钮`）正是
	 * 这种静态 flat：能点、能推进流程，画面上却完全看不见。 */
	if (!f->flat)
		return false;

	// 消费挂起的前向定位请求（来自 GoFramePartsFlat）。
	if (f->pending_seek_delta > 0) {
		for (int i = 0; i < f->pending_seek_delta; i++) {
			flat_advance_layer(f->flat, f->root_state,
					f->flat->timelines, f->flat->nr_timelines);
		}
		f->pending_seek_delta = 0;
		f->elapsed = 0;
		return true;
	}

	// 装载 / 定位之后推进一次。
	if (f->needs_advance) {
		f->needs_advance = false;
		bool changed = flat_advance_layer(f->flat, f->root_state,
				f->flat->timelines, f->flat->nr_timelines);
		f->elapsed = 0;
		return changed;
	}

	if (f->stopped || f->flat->hdr.fps <= 0)
		return false;

	f->elapsed += passed_time;
	int delta = (int)((int64_t)f->elapsed * f->flat->hdr.fps / 1000);
	if (!delta)
		return false;
	f->elapsed -= (unsigned)((int64_t)delta * 1000 / f->flat->hdr.fps);

	bool any_changed = false;
	for (int i = 0; i < delta; i++) {
		if (flat_advance_layer(f->flat, f->root_state,
				f->flat->timelines, f->flat->nr_timelines))
			any_changed = true;
	}
	return any_changed;
}

// 用单个关键帧的属性构造图层矩阵。
// 顺序是：T(位置) * Rz * Rx * Ry * S(缩放) * T(-原点) * Reverse。
// 各分量可以单独启用；位置由调用方解析好。
void parts_flat_build_layer_matrix(const struct flat_key_data_graphic *key,
		vec2 pos,
		bool use_rotation, bool use_scale, bool use_origin,
		bool reverse_lr, bool reverse_tb,
		mat4 out)
{
	glm_mat4_identity(out);
	glm_translate(out, (vec3){ pos[0], pos[1], 0 });
	// 原版引擎会把 3D 旋转过的精灵做真正的透视投影，
	// 这里为了简单用正交近似。
	if (use_rotation) {
		if (key->angle_z != 0) glm_rotate_z(out, glm_rad(key->angle_z), out);
		if (key->angle_x != 0) glm_rotate_x(out, glm_rad(-key->angle_x), out);
		if (key->angle_y != 0) glm_rotate_y(out, glm_rad(key->angle_y), out);
	}
	if (use_scale)
		glm_scale(out, (vec3){ key->scale_x, key->scale_y, 1.0f });
	if (use_origin)
		glm_translate(out, (vec3){ -(float)key->origin_x, -(float)key->origin_y, 0 });
	if (reverse_lr || reverse_tb) {
		glm_scale(out, (vec3){
				reverse_lr ? -1.0f : 1.0f,
				reverse_tb ? -1.0f : 1.0f,
				1.0f });
	}
}

// 用记录下来的祖先关键帧构造发射器的粒子基础矩阵。
// chain.
//
// SELECTIVE 模式：每个祖先贡献一个新的默认关键帧
// （scale=1、rotation=0、origin=0、reverse=false），只覆盖
// 发射器 inherit_* 标志里列出的属性。
//
// ANCESTOR_CURRENT / ANCESTOR_AT_BIRTH 模式：整条
// 父视图链上的关键帧原样使用，inherit_* 标志被忽略；
// 所有祖先属性都影响粒子的基础矩阵。
void parts_flat_build_emitter_base_matrix(const struct flat_emitter *em,
		const struct flat_key_stack *stack, mat4 out)
{
	bool connected = em->parent_key_mode != EMITTER_PARENT_KEY_SELECTIVE;
	glm_mat4_identity(out);
	for (int i = 0; i < stack->count; i++) {
		const struct flat_key_data_graphic *key = stack->keys[i];
		vec2 pos = { key->pos_x, key->pos_y };

		mat4 layer_m;
		parts_flat_build_layer_matrix(key, pos,
				connected || em->inherit_rotation,
				connected || em->inherit_scale,
				connected,
				(connected || em->inherit_reverse_lr) && key->reverse_lr,
				(connected || em->inherit_reverse_tb) && key->reverse_tb,
				layer_m);

		glm_mat4_mul(out, layer_m, out);
	}
}

// 返回 birth_frame 这一帧该出生多少粒子，
// 把 create_count 个粒子均匀摊到 active_frames 上。
static int emitter_get_birth_count(int birth_frame, int create_count,
		int frame_count, int particle_lifetime)
{
	int active_frames = frame_count - particle_lifetime + 1;
	if (active_frames <= 1)
		return create_count;
	if (create_count == 1)
		return birth_frame == 0 ? 1 : 0;

	if (create_count >= active_frames) {
		// 稠密情况：均匀分配，每帧取平均速率的
		// floor 或 ceil。
		float rate = (float)create_count / active_frames;
		return (int)((birth_frame + 1) * rate) - (int)(birth_frame * rate);
	}

	// 稀疏情况：把一个粒子钉在第 0 帧，其余
	// create_count-1 个均匀铺在帧 1 到 active_frames-1 之间
	// （这样帧 active_frames-1 也总能分到一个）。
	if (birth_frame == 0)
		return 1;
	float rate = (float)(create_count - 1) / (active_frames - 1);
	return (int)(birth_frame * rate) - (int)((birth_frame - 1) * rate);
}

static float emitter_fade_alpha(int age, struct flat_emitter *em)
{
	if (em->fade_in_frame > 0 && age < em->fade_in_frame)
		return (float)age / em->fade_in_frame;
	if (em->fade_out_frame > 0 && age > em->particle_lifetime - em->fade_out_frame)
		return (float)(em->particle_lifetime - age) / em->fade_out_frame;
	return 1.0f;
}

static float emitter_gravity_displacement(int age, struct flat_emitter *em, int fps)
{
	if (!em->is_fall)
		return 0;
	const float G = 9.8f;
	float t = (float)age / fps;
	if (em->width != 0 && em->air_resistance != 0) {
		float k = em->air_resistance / em->width;
		float exp_term = 1 - expf(-k * t);
		return (t - exp_term / k) * em->width * G / em->air_resistance;
	}
	return 0.5f * G * t * t;
}

// 在 `t` 处从 `begin` 插值到 `end`，两端各有按
// begin_rand / end_rand 缩放的随机抖动。`sync` 为真时两端共用
// 同一次抖动抽样（但第二次抽样仍然消耗，以保持 RNG
// 序列在 sync 开关前后一致）。
static float lerp_with_jitter(struct mt19937 *mt,
		float begin, float begin_rand, float end, float end_rand,
		bool sync, float t)
{
	float r1 = mt_next_signed(mt) * begin_rand;
	float r2 = mt_next_signed(mt) * end_rand;
	float b = begin + r1;
	float e = end + (sync ? r1 : r2);
	return b + (e - b) * t;
}

static void emitter_interpolate_scale(float t, struct flat_emitter *em,
		struct mt19937 *mt, vec2 out)
{
	float overall = lerp_with_jitter(mt, em->begin_scale, em->begin_scale_rand,
			em->end_scale, em->end_scale_rand, em->sync_scale_rand, t);
	float x = lerp_with_jitter(mt, em->begin_x_scale, em->begin_x_scale_rand,
			em->end_x_scale, em->end_x_scale_rand, em->sync_scale_rand, t);
	float y = lerp_with_jitter(mt, em->begin_y_scale, em->begin_y_scale_rand,
			em->end_y_scale, em->end_y_scale_rand, em->sync_scale_rand, t);
	out[0] = x * overall;
	out[1] = y * overall;
}

static void emitter_interpolate_rotation(float t, struct flat_emitter *em,
		struct mt19937 *mt, vec3 out)
{
	out[0] = lerp_with_jitter(mt, em->begin_x_angle, em->begin_x_angle_rand,
			em->end_x_angle, em->end_x_angle_rand, em->sync_rotation_rand, t);
	out[1] = lerp_with_jitter(mt, em->begin_y_angle, em->begin_y_angle_rand,
			em->end_y_angle, em->end_y_angle_rand, em->sync_rotation_rand, t);
	out[2] = lerp_with_jitter(mt, em->begin_z_angle, em->begin_z_angle_rand,
			em->end_z_angle, em->end_z_angle_rand, em->sync_rotation_rand, t);
}

static void emitter_calc_create_position(struct flat_emitter *em,
		struct mt19937 *mt, vec2 out)
{
	switch (em->create_pos_type) {
	case EMITTER_CREATE_POS_RECT:
		out[0] = mt_next_signed(mt) * em->create_pos_length;
		out[1] = mt_next_signed(mt) * em->create_pos_length2;
		return;
	case EMITTER_CREATE_POS_SPHERE: {
		vec3 v;
		random_unit_vec3(mt, v);
		out[0] = v[0];
		out[1] = v[1];
		break;
	}
	case EMITTER_CREATE_POS_CIRCLE:
		out[0] = mt_next_signed(mt);
		out[1] = mt_next_signed(mt);
		glm_vec2_normalize(out);
		break;
	default:
		glm_vec2_zero(out);
		break;
	}
	float dist = mt_next(mt) * (em->create_pos_length - em->create_pos_length2)
			+ em->create_pos_length2;
	glm_vec2_scale(out, dist, out);
}

// 3D 拒绝采样：
// 随机取一个单位向量 c，使 dot(v, c) > cos(rand * angle/2)，
// 也就是把方向限制在输入方向 `rand * angle/2` 的锥内。
static void randomize_direction_within_cone(vec3 v, float angle_deg, struct mt19937 *mt)
{
	glm_vec3_normalize(v);
	float theta = mt_next(mt) * glm_rad(angle_deg) * 0.5f;
	if (!(theta > 0))
		return;
	float cos_t = cosf(theta);
	for (int i = 0; i < 1000; i++) {
		vec3 c;
		random_unit_vec3(mt, c);
		if (glm_vec3_dot(v, c) > cos_t) {
			glm_vec3_copy(c, v);
			return;
		}
	}
}

static void emitter_calc_direction(struct flat_emitter *em,
		struct mt19937 *mt, vec2 parent_vel, vec2 out)
{
	vec3 dir;
	switch (em->direction_type) {
	case EMITTER_DIRECTION_RANDOM:
		random_unit_vec3(mt, dir);
		break;
	case EMITTER_DIRECTION_PARENT:
		dir[0] = parent_vel[0];
		dir[1] = parent_vel[1];
		dir[2] = 0;
		break;
	case EMITTER_DIRECTION_PARENT_REVERSE:
		dir[0] = -parent_vel[0];
		dir[1] = -parent_vel[1];
		dir[2] = 0;
		break;
	case EMITTER_DIRECTION_DISK: {
		// direction_x/y/z 是发射圆盘的法线。在圆盘上取一个
		// 随机单位向量；下面的 randomize_direction_within_cone
		// 再把它加厚成半宽 direction_angle/2 的一条带。
		vec3 normal = { em->direction_x, em->direction_y, em->direction_z };
		if (glm_vec3_norm2(normal) < 1e-12f)
			glm_vec3_copy((vec3){ 0, 0, 1 }, normal);
		glm_vec3_ortho(normal, dir);
		float phi = mt_next(mt) * 2 * GLM_PIf;
		glm_vec3_rotate(dir, phi, normal);
		break;
	}
	case EMITTER_DIRECTION_FIXED:
	default:
		glm_vec3_copy((vec3){ em->direction_x, em->direction_y, em->direction_z }, dir);
		break;
	}
	randomize_direction_within_cone(dir, em->direction_angle, mt);
	// 投影到 2D 时直接丢掉 Z、不在 2D 里重新归一，
	// 这样斜出 XY 平面的方向
	// 在屏幕上就显得慢一些。
	glm_vec3_normalize(dir);
	out[0] = dir[0];
	out[1] = dir[1];
}

static void emitter_calc_trajectory(int age, struct flat_emitter *em,
		vec2 dir, int fps, float move_rand_factor, vec2 out)
{
	float t = (float)age / fps;
	float accel = em->acceleration * move_rand_factor;
	float speed = em->speed * move_rand_factor;
	float move_length = em->move_length * move_rand_factor;
	float curve = em->move_curve * move_rand_factor;

	if (accel < 0 && speed != 0) {
		float t_max = fabsf(speed / accel);
		if (t > t_max)
			t = t_max;
	}

	float displacement = 0.5f * accel * t * t + speed * t;

	if (move_length != 0) {
		float norm_t = em->particle_lifetime > 0 ? (float)age / em->particle_lifetime : 0;
		if (curve > 1.0f)
			displacement += powf(norm_t, curve) * move_length;
		else if (curve < -1.0f)
			displacement += (1 - powf(1 - norm_t, -curve)) * move_length;
		else
			displacement += move_length * norm_t;
	}
	glm_vec2_scale(dir, displacement, out);
}

// 把每帧的发射器图层属性（位置、alpha、颜色、reverse 标志、
// draw_filter）解析进 `out`，并应用发射器的 inherit_* 开关。
void parts_flat_emitter_resolve_layer(
		const struct flat_emitter *em,
		const struct flat_key_data_graphic *key,
		float parts_alpha, float layer_alpha,
		struct flat_emitter_layer_effective *out)
{
	bool connected = em->parent_key_mode != EMITTER_PARENT_KEY_SELECTIVE;
	out->use_origin = connected;
	out->use_scale = connected || em->inherit_scale;
	out->use_rotation = connected || em->inherit_rotation;
	out->pos[0] = key->pos_x;
	out->pos[1] = key->pos_y;
	out->reverse_lr = (connected || em->inherit_reverse_lr) && key->reverse_lr;
	out->reverse_tb = (connected || em->inherit_reverse_tb) && key->reverse_tb;
	out->alpha = (connected || em->inherit_alpha) ? layer_alpha : parts_alpha;
	if (connected || em->inherit_add_color) {
		out->add_color[0] = key->add_r / 255.0f;
		out->add_color[1] = key->add_g / 255.0f;
		out->add_color[2] = key->add_b / 255.0f;
	} else {
		glm_vec3_zero(out->add_color);
	}
	if (connected || em->inherit_mul_color) {
		out->mul_color[0] = key->mul_r / 255.0f;
		out->mul_color[1] = key->mul_g / 255.0f;
		out->mul_color[2] = key->mul_b / 255.0f;
	} else {
		glm_vec3_one(out->mul_color);
	}
	int draw_filter = (connected || em->inherit_draw_filter) ? key->draw_filter
			: PARTS_DRAW_FILTER_NORMAL;
	// 发射器自己的 draw_filter 覆盖继承来的那个。
	out->draw_filter = em->draw_filter != PARTS_DRAW_FILTER_NORMAL
			? em->draw_filter : draw_filter;
}

// 计算对齐原点偏移（1-9 手机键盘布局）。
void parts_flat_align_offset(int align, int w, int h, vec2 out)
{
	if (align < 1 || align > 9) {
		glm_vec2_zero(out);
		return;
	}
	int col = (align - 1) % 3;
	int row = (align - 1) / 3;
	out[0] = -(float)w * col / 2.0f;
	out[1] = -(float)h * row / 2.0f;
}

// 计算发射器 particle_align 对应的对齐原点偏移。
bool parts_flat_emitter_get_align_offset(struct parts_flat *f, int emitter_lib_idx, vec2 out)
{
	struct flat_emitter *em = &f->flat->libraries[emitter_lib_idx].emitter;
	if (em->particle_lifetime <= 0 || em->create_count <= 0)
		return false;
	if (f->flat->hdr.fps <= 0)
		return false;

	int cg_lib_idx = parts_flat_find_library(f->flat, em->library_name->text);
	if (cg_lib_idx < 0 || (size_t)cg_lib_idx >= f->flat->nr_libraries)
		return false;
	if (f->flat->libraries[cg_lib_idx].type == FLAT_LIB_STOP_MOTION) {
		cg_lib_idx = parts_flat_stop_motion_get_cg_lib(f, cg_lib_idx, 0);
		if (cg_lib_idx < 0)
			return false;
	}

	Texture *tex = &f->textures[cg_lib_idx];
	if (!tex->handle)
		return false;

	int align = em->particle_align;
	if (align < 1 || align > 9) align = 5;
	parts_flat_align_offset(align, tex->w, tex->h, out);
	return true;
}

// 枚举这个发射器在 `birth_frame` 出生的粒子，算出每个粒子
// 在给定 `age`（出生后经过的帧数）时的姿态，并把结果交给 `fn`。
// RNG 用发射器的 rand_seed 和 birth_frame 播种，
// 所以同一个 birth_frame 无论 `age` 多少
// 都产出同一批粒子。
void parts_flat_foreach_emitter_particle(struct parts_flat *f, int emitter_lib_idx,
		const struct flat_key_data_graphic *keys,
		int birth_frame, int age, int frame_count,
		flat_emitter_particle_fn fn, void *ud)
{
	struct flat *fl = f->flat;
	struct flat_emitter *em = &fl->libraries[emitter_lib_idx].emitter;

	int count = emitter_get_birth_count(birth_frame, em->create_count,
			frame_count, em->particle_lifetime);
	if (count == 0)
		return;

	float fade_alpha = emitter_fade_alpha(age, em);
	if (fade_alpha <= 0.f)
		return;

	// 解析粒子的 CG。
	int lib_idx = parts_flat_find_library(fl, em->library_name->text);
	if (lib_idx < 0)
		return;
	if (fl->libraries[lib_idx].type == FLAT_LIB_STOP_MOTION) {
		lib_idx = parts_flat_stop_motion_get_cg_lib(f, lib_idx, age);
		if (lib_idx < 0)
			return;
	}

	// birth_frame 时父部件的速度（只在 direction_type 是
	// PARENT 或 PARENT_REVERSE 时使用）。帧 > 0 用后向差分，
	// 帧 0 用前向差分，其他情况为 0。
	vec2 parent_vel = { 0, 0 };
	if (em->direction_type == EMITTER_DIRECTION_PARENT
			|| em->direction_type == EMITTER_DIRECTION_PARENT_REVERSE) {
		const struct flat_key_data_graphic *cur = &keys[birth_frame];
		if (birth_frame > 0) {
			const struct flat_key_data_graphic *prev = &keys[birth_frame - 1];
			parent_vel[0] = cur->pos_x - prev->pos_x;
			parent_vel[1] = cur->pos_y - prev->pos_y;
		} else if (birth_frame + 1 < frame_count) {
			const struct flat_key_data_graphic *next = &keys[birth_frame + 1];
			parent_vel[0] = next->pos_x - cur->pos_x;
			parent_vel[1] = next->pos_y - cur->pos_y;
		}
	}

	struct mt19937 mt;
	mt19937_init(&mt, em->rand_seed * (birth_frame + 1));

	int fps = fl->hdr.fps;
	float pixels_per_meter = (float)fl->hdr.game_view_width / fl->hdr.meter;
	float t = em->particle_lifetime > 0 ? (float)age / em->particle_lifetime : 0;
	float gravity_y = emitter_gravity_displacement(age, em, fps);

	for (int i = 0; i < count; i++) {
		struct flat_emitter_particle p;
		emitter_interpolate_scale(t, em, &mt, p.scale);
		vec2 create_pos;
		emitter_calc_create_position(em, &mt, create_pos);
		emitter_interpolate_rotation(t, em, &mt, p.rot);
		vec2 dir;
		emitter_calc_direction(em, &mt, parent_vel, dir);
		float move_rand_factor = 1.0f - (mt_next(&mt) - 0.5f) * em->move_rand * 0.01f;
		vec2 traj;
		emitter_calc_trajectory(age, em, dir, fps, move_rand_factor, traj);

		if (em->align_to_direction && (dir[0] != 0 || dir[1] != 0))
			p.rot[2] += glm_deg(atan2f(dir[1], dir[0])) + 90;

		p.pos[0] = (create_pos[0] + traj[0]) * pixels_per_meter;
		p.pos[1] = (create_pos[1] + traj[1] + gravity_y) * pixels_per_meter;
		p.fade_alpha = fade_alpha;
		p.cg_lib_idx = lib_idx;

		fn(&p, ud);
	}
}

bool PE_ExistsFlatFile(struct string *filename)
{
	if (!filename)
		return false;
	return asset_exists_by_name(ASSET_FLAT, filename->text, NULL);
}

bool PE_SetPartsFlat(int parts_no, struct string *filename, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (parts_flat_load(parts, f, filename)) {
		f->needs_advance = true;
		parts_dirty(parts);
		/* [D192] 空 flat（hdr=0x0 / nr_libraries=0）会退化成 0x0 部件，
		 * 画不出也点不到。成功路径也要能看到名字才查得下去。 */
		if (getenv("XSYS4_FLAT_DBG"))
			NOTICE("[flat] SetPartsFlat ok no=%d state=%d name='%s' hdr=%dx%d libs=%zu",
			       parts_no, state,
			       filename ? display_sjis0(filename->text) : "(null)",
			       f->flat->hdr.width, f->flat->hdr.height, f->nr_libraries);
		return true;
	}
	/* [D192] flat 加载失败时部件退化成 0x0 / tex=0，既不画也点不到。
	 * 任务地图点节点后该出来的「確認／放棄」按钮正是这种形态。 */
	if (getenv("XSYS4_FLAT_DBG"))
		NOTICE("[flat] SetPartsFlat FAILED no=%d state=%d name='%s'", parts_no,
		       state, filename ? display_sjis0(filename->text) : "(null)");
	return false;
}

/* AIN PartsEngine：bool SetPartsFlatThread(int Number, string FileName, int State)。
 *
 * 原版是"后台线程去读 flat，调用方轮询 IsPartsFlatEnd"的异步入口。我们
 * 的 flat 加载本来就是一次读完的同步实现，所以直接复用同步路径并回报
 * 成功：调用方紧接着用 IsPartsFlatEnd 轮询，而同步实现此时帧已经就位，
 * 天然返回"已结束"。
 *
 * [D164] 这个导出原来**完全没注册**（连 HLL_TODO_EXPORT 都没有）。
 * 谓词版 Array.Find 修好后，QuestMapBoxView@InitFlatAsync 走到这条路径，
 * 一进来就是 Unimplemented HLL ERROR，把启动画面卡在 WaitForClick。 */
bool PE_SetPartsFlatThread(int parts_no, struct string *filename, int state)
{
	return PE_SetPartsFlat(parts_no, filename, state);
}

/* AIN PartsEngine f[733]：string GetPartsFlatName(int parts_no, int state)。
 *
 * 上一次给该状态的 PE_SetPartsFlat() 传的名字。兰斯10 会读回它，
 * 在替换之前判断这个状态是不是已经带了美术资源
 * （场景切换会对每个状态调 SetFlat 并比较结果），
 * 所以不存在的状态必须回空串，而不是报错。 */
struct string *PE_GetPartsFlatName(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return string_ref(&EMPTY_STRING);
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return string_ref(&EMPTY_STRING);
	struct parts_flat *f = parts_get_flat(parts, state);
	return string_ref(f->name ? f->name : &EMPTY_STRING);
}

bool PE_IsPartsFlatEnd(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat || !f->root_state)
		return true;
	return f->root_state->current_frame == f->end_frame;
}

int PE_GetPartsFlatCurrentFrameNumber(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat || !f->root_state)
		return 0;
	return f->root_state->current_frame + f->pending_seek_delta;
}

bool PE_BackPartsFlatBeginFrame(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat)
		return false;
	f->pending_seek_delta = 0;
	flat_layer_state_free(f->root_state);
	f->root_state = flat_layer_state_new(f->flat->nr_timelines);
	f->needs_advance = true;
	f->elapsed = 0;
	parts_dirty(parts);
	return true;
}

bool PE_StepPartsFlatFinalFrame(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat || !f->root_state)
		return false;

	// 直接跳到结束帧。
	f->root_state->current_frame = f->end_frame;
	f->root_state->stopped = false;
	f->root_state->suppress_advance = true;
	f->pending_seek_delta = 0;
	f->needs_advance = true;
	f->elapsed = 0;
	parts_dirty(parts);
	return true;
}

bool PE_SetPartsFlatSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	parts_set_surface_area(parts, &f->common, x, y, w, h);
	return true;
}

/* [D192] 任意部件类型的「表示領域」。
 *
 * AIN 的 `Parts_SetPartsRectangleDetectionSurfaceArea(Number, X, Y, W, H, State)`
 * 原本只有 HLL_TODO_EXPORT 桩：调用被丢弃 → `common.surface_area` 恒为空 →
 * hitbox 退化成 `{0,0,common.w,common.h}`。任务地图点完对象后弹出的
 * 「確認／放棄」選択肢按钮，其 flat 是空 flat（`hdr=0x0`、`nr_libraries=0`），
 * `common.w/h` 也是 0，于是 hitbox 全零：按钮建得出来、日志一切正常，
 * 但既画不出也点不到，回合永远推不动。
 *
 * 判据：`XSYS4_PARTS_DUMP=1` 下 `QuestMapSelectionButton` 的部件不再是 0x0。 */
bool PE_SetPartsRectangleDetectionSurfaceArea(int parts_no, int x, int y, int w, int h, int state)
{
	/* [D193] 这个入口是 D192 从 TODO 桩补出来的，先证明 AIN 到底调不调。 */
	if (getenv("XSYS4_FLAT_DBG"))
		NOTICE("[rect] SetRectangleDetectionSurfaceArea no=%d (%d,%d) %dx%d state=%d",
		       parts_no, x, y, w, h, state);
	if (!parts_state_valid(--state))
		return false;
	struct parts *parts = parts_try_get(parts_no);
	if (!parts)
		return false;
	parts_set_surface_area(parts, &parts->states[state].common, x, y, w, h);
	return true;
}

bool PE_SetPartsFlatAndStop(int parts_no, struct string *filename, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	bool result = parts_flat_load(parts, f, filename);
	f->stopped = true;
	return result;
}

bool PE_StopPartsFlat(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat)
		return false;
	f->stopped = true;
	return true;
}

bool PE_StartPartsFlat(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat)
		return false;
	f->stopped = false;
	return true;
}

bool PE_GoFramePartsFlat(int parts_no, int frame_no, int state)
{
	if (!parts_state_valid(--state))
		return false;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	if (!f->flat || !f->root_state)
		return false;

	int current = f->root_state->current_frame;
	if (frame_no < current) {
		// 向后定位：直接设置 current_frame。
		f->root_state->current_frame = min(max(0, frame_no), f->end_frame);
		f->root_state->stopped = false;
		f->root_state->suppress_advance = true;
		f->pending_seek_delta = 0;
		f->needs_advance = true;
	} else {
		// 向前定位：留到下一次更新再做。
		f->pending_seek_delta = frame_no - current;
	}
	f->elapsed = 0;
	parts_dirty(parts);
	return true;
}

int PE_GetPartsFlatEndFrame(int parts_no, int state)
{
	if (!parts_state_valid(--state))
		return 0;

	struct parts *parts = parts_get(parts_no);
	struct parts_flat *f = parts_get_flat(parts, state);
	return f->flat ? f->end_frame : -1;
}
