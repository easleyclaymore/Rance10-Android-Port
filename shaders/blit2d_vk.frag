#version 450

/* [D521b] 2D 上屏：直接采样合成结果。 */
layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 o_color;

layout(set = 0, binding = 0) uniform sampler2D tex;

void main()
{
	o_color = texture(tex, v_uv);
}
