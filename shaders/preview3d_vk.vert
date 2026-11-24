#version 450

/* [D517k] 3D 预览的 Vulkan(SDL_GPU) 顶点着色器。
 * world 做 model、view 传透视矩阵（与 GL 版 parts.v.glsl 同语义）。
 * 面颜色走顶点属性，省掉纹理+采样器一套（预览是纯色面）。 */
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;

layout(location = 0) out vec3 v_color;

layout(set = 0, binding = 0) uniform UBO {
	mat4 world;
	mat4 view;
} ubo;

void main()
{
	gl_Position = ubo.view * ubo.world * vec4(a_pos, 1.0);
	v_color = a_color;
}
