#version 450

/* [D521b] 2D 上屏：全屏四边形（GL 合成结果 → swapchain）。
 * 顶点序按两个显式三角（6 顶点）给出；v 翻转处理 GL FBO 的下原点。 */
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;

layout(location = 0) out vec2 v_uv;

void main()
{
	gl_Position = vec4(a_pos, 0.0, 1.0);
	v_uv = vec2(a_uv.x, 1.0 - a_uv.y);
}
