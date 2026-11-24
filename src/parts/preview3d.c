/* [D517j] 3D 设置页的预览场景。
 *
 * 背景（为什么是"重新实现"而不是"接原版"）：见 docs/3D-SEMANTICS.md。
 * 3D 预览在原版由 PartsEngine 内部渲染路径完成 —— 不经任何 HLL、也不是
 * `PARTS_3DLAYER` 部件（3D 页的 EX 里所有 ＣＧ名 都是空串，一个模型/场景
 * 引用都没有）。所以这里用现有 GLES3 后端（`parts_shader` + 单位四边形）
 * 画一个能反映设置项的测试场景。
 *
 * 接入点：
 *   - `activity.c` 装载/释放 `Asra\设置／０８／３Ｄ页面` 时 set_active
 *   - `scene.c` 的 `scene_render()` 在 ctrl>=2 的精灵之前调用 render
 * 这样预览位于 ctrl=1（标题画面）之上、ctrl=2（配置页控件）之下，
 * 既盖住标题、又不挡选项 —— 与原版的层序一致。
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cglm/cglm.h>
#include <SDL3/SDL.h>

#include "xsystem4.h"
#include "preview3d.h"

#define PQ_CUBE 0.62f

static const float cube_v[8][3] = {
	{ -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
	{ -1, -1,  1 }, { 1, -1,  1 }, { 1, 1,  1 }, { -1, 1,  1 },
};

/* 每个面 4 个顶点索引，顺序与单位方形的 (0,0)(1,0)(1,1)(0,1) 对应。 */
static const int face_idx[6][4] = {
	{ 4, 5, 6, 7 }, /* +z 前 */
	{ 1, 0, 3, 2 }, /* -z 后 */
	{ 5, 1, 2, 6 }, /* +x 右 */
	{ 0, 4, 7, 3 }, /* -x 左 */
	{ 3, 7, 6, 2 }, /* +y 上 */
	{ 0, 1, 5, 4 }, /* -y 下 */
};

static const float face_n[6][3] = {
	{ 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
};

static struct {
	bool active;
	float angle;      /* 绕 Y 轴（弧度） */
	Uint64 last_ms;
	int render_mode;  /* 0=软件 1=着色無 2=着色存在 */
	int fog_mode;
	int edge;
	int dof;
} pq;

void preview3d_set_active(bool on)
{
	pq.active = on;
	if (on)
		pq.angle = 0.6f;
}

bool preview3d_active(void)
{
	return pq.active;
}

void preview3d_tick(int ms)
{
	if (!pq.active || ms <= 0)
		return;
	pq.angle += (float)ms * 0.0005f;
	if (pq.angle > 6.2831853f)
		pq.angle -= 6.2831853f;
}

void preview3d_set_param(int render_mode, int fog_mode, int edge, int dof)
{
	pq.render_mode = render_mode;
	pq.fog_mode = fog_mode;
	pq.edge = edge;
	pq.dof = dof;
}

/* 面中心的视图空间 z（越大越近）。 */
static float face_depth(const mat4 rot, const int f[4])
{
	vec3 c = { 0, 0, 0 };
	for (int i = 0; i < 4; i++) {
		c[0] += cube_v[f[i]][0] * PQ_CUBE * 0.5f;
		c[1] += cube_v[f[i]][1] * PQ_CUBE * 0.5f;
		c[2] += cube_v[f[i]][2] * PQ_CUBE * 0.5f;
	}
	vec4 rc;
	glm_mat4_mulv(rot, (vec4){ c[0], c[1], c[2], 1.0f }, rc);
	return rc[2] + 4.6f; /* 相机距离，用于雾化归一化 */
}

void preview3d_render(int w, int h)
{
	static const SDL_Color bg = { 20, 22, 28, 255 };
	static const SDL_Color base = { 176, 184, 198, 255 };
	static const float light[3] = { 0.35f, 0.62f, 0.70f };

	if (!pq.active || w <= 0 || h <= 0)
		return;

	/* 自驱动旋转：用墙钟算 delta（不依赖 VM 帧泵，标题静置时也在转）。 */
	Uint64 now = SDL_GetTicks();
	if (pq.last_ms)
		preview3d_tick((int)(now - pq.last_ms));
	pq.last_ms = now;

	/* 背景：把标题画面盖住（原版这里画的是实时 3D 场景）。 */
	parts_draw_screen_rect(0, 0, w, h, bg, 1.0f);

	/* 相机：45° 视场、后退 4.6（与 draw.c 的 copy_rotate_y 同一套）。 */
	mat4 proj;
	float fov = 45.0f * (float)(M_PI / 180.0);
	glm_perspective(fov, (float)w / (float)h, 0.1f, 100.0f, proj);
	glm_translate(proj, (vec3){ 0.0f, 0.0f, -4.6f });

	mat4 rot = GLM_MAT4_IDENTITY_INIT;
	glm_rotate_y(rot, pq.angle, rot);
	glm_rotate_x(rot, 0.42f, rot);

	/* painter：按面深度从远到近。 */
	int order[6] = { 0, 1, 2, 3, 4, 5 };
	float depth[6];
	for (int i = 0; i < 6; i++)
		depth[i] = face_depth(rot, face_idx[i]);
	for (int i = 0; i < 6; i++) {
		for (int j = i + 1; j < 6; j++) {
			if (depth[order[j]] < depth[order[i]]) {
				int t = order[i];
				order[i] = order[j];
				order[j] = t;
			}
		}
	}

	for (int k = 0; k < 6; k++) {
		const int fi = order[k];
		const int *f = face_idx[fi];
		const float *n = face_n[fi];

		/* 把单位方形映射到该面：col0=v1-v0, col1=v3-v0, col2=法线, col3=v0 */
		const float *v0 = cube_v[f[0]];
		const float *v1 = cube_v[f[1]];
		const float *v3 = cube_v[f[3]];
		mat4 m = GLM_MAT4_IDENTITY_INIT;
		m[0][0] = (v1[0] - v0[0]) * PQ_CUBE;
		m[0][1] = (v1[1] - v0[1]) * PQ_CUBE;
		m[0][2] = (v1[2] - v0[2]) * PQ_CUBE;
		m[0][3] = 0.0f;
		m[1][0] = (v3[0] - v0[0]) * PQ_CUBE;
		m[1][1] = (v3[1] - v0[1]) * PQ_CUBE;
		m[1][2] = (v3[2] - v0[2]) * PQ_CUBE;
		m[1][3] = 0.0f;
		m[2][0] = n[0];
		m[2][1] = n[1];
		m[2][2] = n[2];
		m[2][3] = 0.0f;
		m[3][0] = v0[0] * PQ_CUBE;
		m[3][1] = v0[1] * PQ_CUBE;
		m[3][2] = v0[2] * PQ_CUBE;
		m[3][3] = 1.0f;

		mat4 world;
		glm_mat4_mul(rot, m, world);

		/* 光照：法线随模型旋转，与固定方向光点积。
		 * render_mode 0（软件）不光照；1（着色無）平坦；2（着色存在）加高光。 */
		vec4 rn;
		glm_mat4_mulv(rot, (vec4){ n[0], n[1], n[2], 0.0f }, rn);
		float ndl = rn[0] * light[0] + rn[1] * light[1] + rn[2] * light[2];
		float shade = 1.0f;
		if (pq.render_mode != 0) {
			if (ndl < 0.0f)
				ndl = 0.0f;
			shade = 0.42f + 0.58f * ndl;
			if (pq.render_mode >= 2) {
				/* 简化高光：正对光源的面再亮一档 */
				float spec = ndl * ndl * ndl * ndl;
				shade += 0.22f * spec;
			}
		}

		SDL_Color c = {
			(Uint8)(base.r * shade > 255 ? 255 : base.r * shade),
			(Uint8)(base.g * shade > 255 ? 255 : base.g * shade),
			(Uint8)(base.b * shade > 255 ? 255 : base.b * shade),
			255,
		};

		/* 雾化：远处面往背景色混。depth 4.6 约等于原点，5.6 为最远。 */
		if (pq.fog_mode) {
			float d = face_depth(rot, f);
			float t = (5.9f - d) * 0.42f; /* 越远越大 */
			if (t > 0.0f) {
				if (t > 0.85f)
					t = 0.85f;
				c.r = (Uint8)(c.r * (1.0f - t) + bg.r * t);
				c.g = (Uint8)(c.g * (1.0f - t) + bg.g * t);
				c.b = (Uint8)(c.b * (1.0f - t) + bg.b * t);
			}
		}

		parts_draw_solid_quad(world, proj, c, 1.0f);
	}

	/* 抗锯齿/边缘在此版本不画描边（edge 已在 EX 里为 0 或 1，留作后续）。 */
}
