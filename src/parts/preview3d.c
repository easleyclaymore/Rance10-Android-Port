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
#include "preview3d_vk.h"

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
	int glow;
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

void preview3d_set_param(int render_mode, int fog_mode, int edge, int dof,
			 int glow)
{
	pq.render_mode = render_mode;
	pq.fog_mode = fog_mode;
	pq.edge = edge;
	pq.dof = dof;
	pq.glow = glow;
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

/* 相机 + 旋转（两条渲染路径共用）。 */
static void pq_camera(int w, int h, mat4 proj, mat4 rot)
{
	float fov = 45.0f * (float)(M_PI / 180.0);
	glm_perspective(fov, (float)w / (float)h, 0.1f, 100.0f, proj);
	glm_translate(proj, (vec3){ 0.0f, 0.0f, -4.6f });

	glm_mat4_identity(rot);
	glm_rotate_y(rot, pq.angle, rot);
	glm_rotate_x(rot, 0.42f, rot);
}

/* 推进旋转（用墙钟，不依赖 VM 帧泵）。 */
static void pq_tick(void)
{
	Uint64 now = SDL_GetTicks();
	if (pq.last_ms)
		preview3d_tick((int)(now - pq.last_ms));
	pq.last_ms = now;
}

/* 面排序 + 每面的 world 矩阵/颜色（两条渲染路径共用）。
 *
 * 输出最多 PQ_MAX_FACES 个面：`edge=1` 时每个可见面先压一个略大的深色
 * 轮廓面（painter 排序下轮廓只在本体边缘露出 3%，看起来就是描边），
 * 再压本体面。返回实际面数。 */
#define PQ_MAX_FACES 12
static int pq_build_faces(const mat4 rot, mat4 world[PQ_MAX_FACES],
			  float colors[PQ_MAX_FACES][3])
{
	static const float base[3] = { 176.0f, 184.0f, 198.0f };
	static const float light[3] = { 0.35f, 0.62f, 0.70f };
	static const float bgf[3] = { 20.0f, 22.0f, 28.0f };
	/* 轮廓色：比背景略亮一点的黑边，避免与背景糊在一起。 */
	static const float edge_rgb[3] = { 46.0f, 50.0f, 58.0f };

	float depth[6];
	for (int i = 0; i < 6; i++)
		depth[i] = face_depth(rot, face_idx[i]);
	int order[6];
	for (int i = 0; i < 6; i++)
		order[i] = i;
	for (int i = 0; i < 6; i++) {
		for (int j = i + 1; j < 6; j++) {
			if (depth[order[j]] < depth[order[i]]) {
				int t = order[i];
				order[i] = order[j];
				order[j] = t;
			}
		}
	}

	int n = 0;
	for (int k = 0; k < 6; k++) {
		const int fi = order[k];
		const int *f = face_idx[fi];
		const float *nrm = face_n[fi];
		const float *v0 = cube_v[f[0]];
		const float *v1 = cube_v[f[1]];
		const float *v3 = cube_v[f[3]];
		mat4 m = GLM_MAT4_IDENTITY_INIT;
		m[0][0] = (v1[0] - v0[0]) * PQ_CUBE;
		m[0][1] = (v1[1] - v0[1]) * PQ_CUBE;
		m[0][2] = (v1[2] - v0[2]) * PQ_CUBE;
		m[1][0] = (v3[0] - v0[0]) * PQ_CUBE;
		m[1][1] = (v3[1] - v0[1]) * PQ_CUBE;
		m[1][2] = (v3[2] - v0[2]) * PQ_CUBE;
		m[2][0] = nrm[0];
		m[2][1] = nrm[1];
		m[2][2] = nrm[2];
		m[3][0] = v0[0] * PQ_CUBE;
		m[3][1] = v0[1] * PQ_CUBE;
		m[3][2] = v0[2] * PQ_CUBE;
		m[3][3] = 1.0f;
		mat4 w;
		glm_mat4_mul(rot, m, w);

		/* [边缘] 先压轮廓：两条切向基向量放大 6%，平移沿它们各退 3%，
		 * 使放大围绕面中心而不是围绕角点。 */
		if (pq.edge) {
			mat4 e = GLM_MAT4_IDENTITY_INIT;
			glm_mat4_copy(w, e);
			for (int r = 0; r < 3; r++) {
				e[0][r] *= 1.06f;
				e[1][r] *= 1.06f;
			}
			for (int r = 0; r < 3; r++)
				e[3][r] = w[3][r] - 0.03f * (w[0][r] + w[1][r]);
			glm_mat4_copy(e, world[n]);
			colors[n][0] = edge_rgb[0] / 255.0f;
			colors[n][1] = edge_rgb[1] / 255.0f;
			colors[n][2] = edge_rgb[2] / 255.0f;
			n++;
		}

		vec4 rn;
		glm_mat4_mulv(rot, (vec4){ nrm[0], nrm[1], nrm[2], 0.0f }, rn);
		float ndl = rn[0] * light[0] + rn[1] * light[1] + rn[2] * light[2];
		float shade = 1.0f;
		if (pq.render_mode != 0) {
			if (ndl < 0.0f)
				ndl = 0.0f;
			shade = 0.42f + 0.58f * ndl;
			/* [光晕] 存在时高光更强（原版是 bloom 后处理，
			 * 这里用高光增益近似）。 */
			if (pq.render_mode >= 2) {
				float hi = 0.22f * (ndl * ndl * ndl * ndl);
				if (pq.glow)
					hi *= 1.9f;
				shade += hi;
			}
		}
		float cr = base[0] * shade, cg = base[1] * shade, cb = base[2] * shade;
		float d = depth[fi];
		/* [雾化] 越远越往背景色混。
		 * **方向不能反**：face_depth 的实测范围是 [4.24, 4.96]
		 * （相机 4.6 ± 立方体半对角线 0.54），d 越大 = 越远。
		 * D517j 初版写成 `(5.9 - d) * 0.42`，把**近面**混成背景、
		 * 远面保留 —— 一开雾化就只剩一个大方块（d712 实证）。
		 * 这里改成随 d 递增，并把系数放大到能看出层次。 */
		if (pq.fog_mode) {
			float t = (d - 4.20f) * 0.85f;
			if (t > 0.0f) {
				if (t > 0.70f)
					t = 0.70f;
				cr = cr * (1.0f - t) + bgf[0] * t;
				cg = cg * (1.0f - t) + bgf[1] * t;
				cb = cb * (1.0f - t) + bgf[2] * t;
			}
		}
		/* [景深] 远景额外降对比（向背景收）。焦平面按相机距离 4.6
		 * 取，只有明显更远的面才虚化，避免与雾化完全重叠。 */
		if (pq.dof && d > 4.60f) {
			float t = (d - 4.60f) * 0.95f;
			if (t > 0.45f)
				t = 0.45f;
			cr = cr * (1.0f - t) + bgf[0] * t;
			cg = cg * (1.0f - t) + bgf[1] * t;
			cb = cb * (1.0f - t) + bgf[2] * t;
		}
		glm_mat4_copy(w, world[n]);
		colors[n][0] = (cr > 255.0f ? 255.0f : cr) / 255.0f;
		colors[n][1] = (cg > 255.0f ? 255.0f : cg) / 255.0f;
		colors[n][2] = (cb > 255.0f ? 255.0f : cb) / 255.0f;
		n++;
		if (n >= PQ_MAX_FACES)
			break;
	}
	return n;
}

/* Vulkan(SDL_GPU) 路径：整帧由 gfx_vk 负责（含清屏），无 GL 调用。 */
bool preview3d_render_vk(int w, int h)
{
	if (!pq.active || w <= 0 || h <= 0)
		return false;
	pq_tick();
	mat4 proj, rot;
	pq_camera(w, h, proj, rot);
	mat4 world[PQ_MAX_FACES];
	float colors[PQ_MAX_FACES][3];
	int n = pq_build_faces(rot, world, colors);
	return gfx_vk_draw_faces((const float (*)[16])world, colors, n,
				 (const float *)proj);
}

void preview3d_render(int w, int h)
{
	static const SDL_Color bg = { 20, 22, 28, 255 };

	if (!pq.active || w <= 0 || h <= 0)
		return;
	pq_tick();

	/* 背景：把标题画面盖住（原版这里画的是实时 3D 场景）。 */
	parts_draw_screen_rect(0, 0, w, h, bg, 1.0f);

	mat4 proj, rot;
	pq_camera(w, h, proj, rot);

	mat4 world[PQ_MAX_FACES];
	float colors[PQ_MAX_FACES][3];
	int n = pq_build_faces(rot, world, colors);

	for (int k = 0; k < n; k++) {
		SDL_Color c = {
			(Uint8)(colors[k][0] * 255.0f + 0.5f),
			(Uint8)(colors[k][1] * 255.0f + 0.5f),
			(Uint8)(colors[k][2] * 255.0f + 0.5f),
			255,
		};
		parts_draw_solid_quad(world[k], proj, c, 1.0f);
	}
}
