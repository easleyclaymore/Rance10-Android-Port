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

#include <cglm/cglm.h>

#include "gfx/gfx.h"
#include "hll.h"
#include "sact.h"

struct fill_angele_shader {
	struct shader s;
	GLint center;
	GLint start_angle;
	GLint angle;
	GLint color;
};
static struct fill_angele_shader shader;

static void load_shader(void)
{
	gfx_load_shader(&shader.s, "shaders/render.v.glsl", "shaders/fill_angle.f.glsl");
	shader.center = glGetUniformLocation(shader.s.program, "center");
	shader.start_angle = glGetUniformLocation(shader.s.program, "start_angle");
	shader.angle = glGetUniformLocation(shader.s.program, "angle");
	shader.color = glGetUniformLocation(shader.s.program, "color");
}

static bool FillAngle_FillAngleAMAP(int sprite, int center_x, int center_y, int start_angle, int angle, int alpha)
{
	struct sact_sprite *sp = sact_try_get_sprite(sprite);
	if (!sp)
		return false;
	struct texture *dst = sprite_get_texture(sp);
	float w = dst->w;
	float h = dst->h;

	if (!shader.s.program)
		load_shader();
	glUseProgram(shader.s.program);
	glUniform2f(shader.center, center_x / w, center_y / h);
	glUniform1f(shader.start_angle, glm_rad(start_angle));
	glUniform1f(shader.angle, glm_rad(angle));
	glUniform4f(shader.color, 0, 0, 0, alpha / 255.0);
	glUseProgram(0);

	mat4 mw_transform = MAT4(
	     w, 0, 0, 0,
	     0, h, 0, 0,
	     0, 0, 1, 0,
	     0, 0, 0, 1);
	mat4 wv_transform = WV_TRANSFORM(dst->w, dst->h);
	GLuint fbo = gfx_set_framebuffer(GL_DRAW_FRAMEBUFFER, dst, 0, 0, dst->w, dst->h);

	struct gfx_render_job job = {
		.shader = &shader.s,
		.shape = GFX_RECTANGLE,
		.texture = 0,
		.world_transform = mw_transform[0],
		.view_transform = wv_transform[0],
		.data = NULL
	};
	glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
	gfx_render(&job);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);

	gfx_reset_framebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	return true;
}

HLL_LIBRARY(FillAngle,
	    HLL_EXPORT(FillAngleAMAP, FillAngle_FillAngleAMAP)
	    );
