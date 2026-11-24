#version 450

/* [D517k] 3D 预览的 Vulkan(SDL_GPU) 片元着色器：直接输出顶点色。 */
layout(location = 0) in vec3 v_color;

layout(location = 0) out vec4 o_color;

void main()
{
	o_color = vec4(v_color, 1.0);
}
