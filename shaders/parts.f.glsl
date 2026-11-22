/* Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

uniform sampler2D tex;
uniform float blend_rate;
uniform vec2 bot_left;
uniform vec2 top_right;
uniform vec3 add_color;
uniform vec3 multiply_color;
uniform int draw_filter;

uniform int use_clipper;
uniform sampler2D clipper_tex;

in vec2 tex_coord;
in vec2 clip_coord;
out vec4 frag_color;

const int DRAW_FILTER_MULTIPLY = 2;
const int DRAW_FILTER_SCREEN = 3;

bool inside_rect(vec2 p, vec2 bl, vec2 tr) {
	return all(greaterThanEqual(p, bl)) && all(lessThan(p, tr));
}

void main() {
	vec2 size = vec2(textureSize(tex, 0));
	/* [D498] bot_left/top_right 的语义是「纹理内的像素子矩形」——网格
	 * UV 恒为 0..1（共享矩形 VBO，覆盖整张纹理），必须把它重映射进
	 * 窗口才是真正的子区域采样。旧实现只把窗口当 discard 掩码：整张
	 * 纹理仍被压进 quad，窗口外的碎片被丢掉。对整张纹理的普通绘制
	 * （窗口=全纹理）无感；对 TALT 图集子区域绘制（标题按钮 flat：
	 * 环 200x200@(0,0) + 球 154x154@(224,0)）则把 384x224 整图压进
	 * 200x200 的 quad——环被压成 0.52 宽的椭圆、球被挤到 quad 右侧，
	 * 即用户报告的「金环偏移 + 游离暗球」。 */
	vec2 uv0 = bot_left / size;
	vec2 uv1 = top_right / size;
	vec2 uv = mix(uv0, uv1, tex_coord);
	if (!inside_rect(uv, uv0, uv1))
		discard;

	vec4 tex_color = texture(tex, uv);
	vec3 mod_color = (tex_color.rgb + add_color) * multiply_color;
	float alpha = tex_color.a * blend_rate;

	if (use_clipper != 0) {
		if (!inside_rect(clip_coord, vec2(0.0), vec2(1.0)))
			discard;
		alpha *= texture(clipper_tex, clip_coord).a;
	}

	if (draw_filter == DRAW_FILTER_MULTIPLY) {
		// result = mix(dst, mod_color*dst, alpha), used with (GL_DST_COLOR, GL_ZERO)
		frag_color = vec4(mix(vec3(1.0), mod_color, alpha), 1.0);
	} else if (draw_filter == DRAW_FILTER_SCREEN) {
		// result = mod_color*alpha + (1 - mod_color*alpha)*dst,
		// used with (GL_ONE, GL_ONE_MINUS_SRC_COLOR)
		frag_color = vec4(mod_color * alpha, 1.0);
	} else {
		// normal or additive — blend func handles alpha
		frag_color = vec4(mod_color, alpha);
	}
}
