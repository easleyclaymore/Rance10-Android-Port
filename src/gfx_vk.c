/* [D517k] 3D 预览的 Vulkan 渲染路径（经 SDL_GPU，底层驱动 = Vulkan）。
 *
 * 为什么不直接写原生 Vulkan：本仓库的 2D 渲染是**裸 GL**（111 个 gfx API、
 * 932 行 video.c、离屏 framebuffer、87 处混合模式设置），全量迁移是
 * 3000+ 行新代码。先用 SDL_GPU 把「3D 预览」这条路径打通，验证 Vulkan
 * 能起、能画、能截图；2D 的迁移作为后续分期（见 docs/RENDER-BACKEND.md）。
 *
 * 与 GL 路径的关系：**互斥**。SDL 窗口只能是 OPENGL 或 VULKAN 之一，
 * 所以 `XSYS4_RENDER_BACKEND=vulkan` 时整帧由本文件负责，GL 的 gfx
 * 绘制全部跳过（画面只有预览 + 清屏色）。默认仍是 GL。
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cglm/cglm.h>

#include "system4.h"
#include "xsystem4.h"
#include "preview3d_vk.h"
#include "preview3d_vk_spv.h"
#include "blit2d_vk_spv.h"

static struct {
	bool active;
	SDL_GPUDevice *dev;
	SDL_Window *win;
	SDL_GPUGraphicsPipeline *pipe;
	SDL_GPUBuffer *vbo;
	SDL_GPUTransferBuffer *xfer;
	SDL_GPUTextureFormat fmt;
	int vbo_cap;      /* 字节容量 */
	/* [D521b] 2D 上屏（GL 合成结果 → swapchain） */
	SDL_GPUGraphicsPipeline *blit_pipe;
	SDL_GPUShader *blit_vs, *blit_fs;
	SDL_GPUBuffer *blit_vbo;
	SDL_GPUTexture *frame_tex;
	SDL_GPUTransferBuffer *frame_xfer;
	SDL_GPUSampler *sampler;
	int frame_w, frame_h;
} vk;

/* 每面一次 push uniform：world + view */
struct vk_ubo {
	mat4 world;
	mat4 view;
};

bool gfx_vk_active(void)
{
	return vk.active;
}

/* [D521] 无窗口纯设备探测：CreateGPUDevice 不需要窗口（PrepareDriver
 * 只检查 video 驱动的钩子）。此前用 probe 窗口探测——claim 后立即销毁
 * probe 窗口，销毁事件触发 VULKAN_INTERNAL_OnWindowResize 解引用已失效
 * 的 swapchain 状态，核心转储（d810）。 */
bool gfx_vk_probe(void)
{
	if (!SDL_IsMainThread())
		return false;
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_PropertiesID vprops = SDL_CreateProperties();
	SDL_SetBooleanProperty(vprops, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
	SDL_SetBooleanProperty(vprops, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);
	/* [D521f] VERBOSE 属性 SDL 3.4.0 才有；CI 钉 3.2.x LTS（workflow 注释：
	 * 发行版打包线），条件编译降级。 */
#ifdef SDL_PROP_GPU_DEVICE_CREATE_VERBOSE_BOOLEAN
	SDL_SetBooleanProperty(vprops, SDL_PROP_GPU_DEVICE_CREATE_VERBOSE_BOOLEAN, true);
#endif
	SDL_GPUDevice *dev = SDL_CreateGPUDeviceWithProperties(vprops);
	SDL_DestroyProperties(vprops);
	if (!dev) {
		NOTICE("[vk] probe 失败：%s", SDL_GetError());
		return false;
	}
	const char *drv = SDL_GetGPUDeviceDriver(dev);
	bool ok = drv && SDL_strcasecmp(drv, "vulkan") == 0;
	NOTICE("[vk] probe: driver='%s' -> %s", drv ? drv : "?",
	       ok ? "OK" : "非 vulkan");
	SDL_DestroyGPUDevice(dev);
	return ok;
}

bool gfx_vk_init(SDL_Window *win)
{
	if (vk.active)
		return true;
	if (!win)
		return false;
	if (!SDL_IsMainThread())
		return false;

	NOTICE("[vk] current video driver = '%s'",
	       SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(null)");

	/* 只要 SPIR-V（Vulkan 用）：probe 已验证驱动可用，这里直接建。 */
	SDL_PropertiesID vprops = SDL_CreateProperties();
	SDL_SetBooleanProperty(vprops, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
	SDL_SetBooleanProperty(vprops, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);
#ifdef SDL_PROP_GPU_DEVICE_CREATE_VERBOSE_BOOLEAN
	SDL_SetBooleanProperty(vprops, SDL_PROP_GPU_DEVICE_CREATE_VERBOSE_BOOLEAN, true);
#endif
	SDL_GPUDevice *dev = SDL_CreateGPUDeviceWithProperties(vprops);
	SDL_DestroyProperties(vprops);
	if (!dev) {
		NOTICE("[vk] SDL_CreateGPUDeviceWithProperties 失败：%s",
		       SDL_GetError());
		return false;
	}
	const char *drv = SDL_GetGPUDeviceDriver(dev);
	if (!drv || SDL_strcasecmp(drv, "vulkan") != 0) {
		NOTICE("[vk] 拿到的驱动是 '%s' 而不是 vulkan，放弃（回退 GL）",
		       drv ? drv : "?");
		SDL_DestroyGPUDevice(dev);
		return false;
	}
	if (!SDL_ClaimWindowForGPUDevice(dev, win)) {
		NOTICE("[vk] ClaimWindow 失败：%s", SDL_GetError());
		SDL_DestroyGPUDevice(dev);
		return false;
	}

	vk.dev = dev;
	vk.win = win;
	vk.fmt = SDL_GetGPUSwapchainTextureFormat(dev, win);

	/* ---- 着色器 ---- */
	SDL_GPUShader *vs = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
		.code = preview3d_vk_vert_spv,
		.code_size = preview3d_vk_vert_spv_size,
		.entrypoint = "main",
		.format = SDL_GPU_SHADERFORMAT_SPIRV,
		.stage = SDL_GPU_SHADERSTAGE_VERTEX,
		.num_uniform_buffers = 1,
	});
	SDL_GPUShader *fs = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
		.code = preview3d_vk_frag_spv,
		.code_size = preview3d_vk_frag_spv_size,
		.entrypoint = "main",
		.format = SDL_GPU_SHADERFORMAT_SPIRV,
		.stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
		.num_uniform_buffers = 0,
	});
	if (!vs || !fs) {
		NOTICE("[vk] 着色器创建失败：%s", SDL_GetError());
		goto fail;
	}

	/* ---- 管线 ---- */
	SDL_GPUVertexBufferDescription vbd = {
		.slot = 0,
		.pitch = sizeof(float) * 6, /* pos(3) + color(3) */
		.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
	};
	SDL_GPUVertexAttribute attrs[2] = {
		{ .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
		  .offset = 0 },
		{ .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
		  .offset = sizeof(float) * 3 },
	};
	SDL_GPUColorTargetDescription ct = {
		.format = vk.fmt,
		.blend_state = {
			.enable_blend = true,
			.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
			.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.color_blend_op = SDL_GPU_BLENDOP_ADD,
			.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
			.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
			.alpha_blend_op = SDL_GPU_BLENDOP_ADD,
			.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
					    SDL_GPU_COLORCOMPONENT_G |
					    SDL_GPU_COLORCOMPONENT_B |
					    SDL_GPU_COLORCOMPONENT_A,
		},
	};
	SDL_GPUGraphicsPipelineCreateInfo pci = {
		.vertex_shader = vs,
		.fragment_shader = fs,
		.vertex_input_state = {
			.vertex_buffer_descriptions = &vbd,
			.num_vertex_buffers = 1,
			.vertex_attributes = attrs,
			.num_vertex_attributes = 2,
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP,
		.rasterizer_state = {
			.fill_mode = SDL_GPU_FILLMODE_FILL,
			.cull_mode = SDL_GPU_CULLMODE_NONE,
			.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
		},
		.target_info = {
			.color_target_descriptions = &ct,
			.num_color_targets = 1,
		},
	};
	vk.pipe = SDL_CreateGPUGraphicsPipeline(dev, &pci);
	SDL_ReleaseGPUShader(dev, vs);
	SDL_ReleaseGPUShader(dev, fs);
	if (!vk.pipe) {
		NOTICE("[vk] 管线创建失败：%s", SDL_GetError());
		goto fail;
	}

	/* 顶点缓冲：6 面 × 4 顶点 × 6 float，一次建够。 */
	vk.vbo_cap = 6 * 4 * 6 * (int)sizeof(float);
	vk.vbo = SDL_CreateGPUBuffer(dev, &(SDL_GPUBufferCreateInfo){
		.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
		.size = (Uint32)vk.vbo_cap,
	});
	vk.xfer = SDL_CreateGPUTransferBuffer(dev, &(SDL_GPUTransferBufferCreateInfo){
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = (Uint32)vk.vbo_cap,
	});
	if (!vk.vbo || !vk.xfer) {
		NOTICE("[vk] 顶点缓冲创建失败：%s", SDL_GetError());
		goto fail;
	}

	/* [D521b] 2D 上屏资源：blit 管线 + 静态全屏四边形 + 采样器。 */
	vk.blit_vs = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
		.code = blit2d_vk_vert_spv,
		.code_size = blit2d_vk_vert_spv_size,
		.entrypoint = "main",
		.format = SDL_GPU_SHADERFORMAT_SPIRV,
		.stage = SDL_GPU_SHADERSTAGE_VERTEX,
	});
	vk.blit_fs = SDL_CreateGPUShader(dev, &(SDL_GPUShaderCreateInfo){
		.code = blit2d_vk_frag_spv,
		.code_size = blit2d_vk_frag_spv_size,
		.entrypoint = "main",
		.format = SDL_GPU_SHADERFORMAT_SPIRV,
		.stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
		.num_samplers = 1,
	});
	vk.blit_vbo = SDL_CreateGPUBuffer(dev, &(SDL_GPUBufferCreateInfo){
		.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
		.size = sizeof(float) * 4 * 6,
	});
	vk.sampler = SDL_CreateGPUSampler(dev, &(SDL_GPUSamplerCreateInfo){
		.min_filter = SDL_GPU_FILTER_LINEAR,
		.mag_filter = SDL_GPU_FILTER_LINEAR,
		.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
		.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
		.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
		.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	});
	if (!vk.blit_vs || !vk.blit_fs || !vk.blit_vbo || !vk.sampler) {
		NOTICE("[vk] blit 资源创建失败：%s", SDL_GetError());
		goto fail;
	}
	/* 全屏四边形，两个显式三角：clip space + uv。行序在 shader 里翻。 */
	{
		float quad[6][4] = {
			{ -1, -1, 0, 0 }, { 1, -1, 1, 0 }, { 1, 1, 1, 1 },
			{ -1, -1, 0, 0 }, { 1, 1, 1, 1 }, { -1, 1, 0, 1 },
		};
		SDL_GPUTransferBuffer *q = SDL_CreateGPUTransferBuffer(dev,
			&(SDL_GPUTransferBufferCreateInfo){
				.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
				.size = sizeof(quad) });
		if (!q)
			goto fail;
		memcpy(SDL_MapGPUTransferBuffer(dev, q, false), quad, sizeof(quad));
		SDL_GPUCommandBuffer *cq = SDL_AcquireGPUCommandBuffer(dev);
		SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cq);
		SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){
				.transfer_buffer = q, .offset = 0 },
			&(SDL_GPUBufferRegion){ .buffer = vk.blit_vbo, .offset = 0,
				.size = sizeof(quad) }, false);
		SDL_EndGPUCopyPass(cp);
		SDL_SubmitGPUCommandBuffer(cq);
		SDL_ReleaseGPUTransferBuffer(dev, q);
	}

	SDL_GPUVertexBufferDescription bds = {
		.slot = 0,
		.pitch = sizeof(float) * 4,
		.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
	};
	SDL_GPUVertexAttribute battrs[2] = {
		{ .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
		  .offset = 0 },
		{ .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
		  .offset = sizeof(float) * 2 },
	};
	SDL_GPUGraphicsPipelineCreateInfo bpci = {
		.vertex_shader = vk.blit_vs,
		.fragment_shader = vk.blit_fs,
		.vertex_input_state = {
			.vertex_buffer_descriptions = &bds,
			.num_vertex_buffers = 1,
			.vertex_attributes = battrs,
			.num_vertex_attributes = 2,
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.target_info = {
			.num_color_targets = 1,
			.color_target_descriptions = &(SDL_GPUColorTargetDescription){
				.format = vk.fmt,
				.blend_state = { .enable_blend = false },
			},
		},
	};
	vk.blit_pipe = SDL_CreateGPUGraphicsPipeline(dev, &bpci);
	if (!vk.blit_pipe) {
		NOTICE("[vk] blit 管线创建失败：%s", SDL_GetError());
		goto fail;
	}

	vk.active = true;
	NOTICE("[vk] Vulkan 路径就绪：driver=%s swapchain_fmt=%d", drv, (int)vk.fmt);
	return true;

fail:
	if (vk.xfer) { SDL_ReleaseGPUTransferBuffer(dev, vk.xfer); vk.xfer = NULL; }
	if (vk.vbo) { SDL_ReleaseGPUBuffer(dev, vk.vbo); vk.vbo = NULL; }
	if (vk.pipe) { SDL_ReleaseGPUGraphicsPipeline(dev, vk.pipe); vk.pipe = NULL; }
	SDL_ReleaseWindowFromGPUDevice(dev, win);
	SDL_DestroyGPUDevice(dev);
	vk.dev = NULL;
	return false;
}

/* [D521b] 把 GL 合成结果（RGBA，行序自 GL FBO 读出、已含下原点）上传
 * 并作为全屏四边形上屏。w/h 变化时自动重建目标纹理。 */
bool gfx_vk_blit_frame(const unsigned char *rgba, int w, int h)
{
	if (!vk.active || !rgba || w <= 0 || h <= 0) {
		/* [D521d] 一次性失败日志：上屏链路断在哪一环 */
		static int nr_fail0;
		if (nr_fail0++ < 3)
			NOTICE("[vk] blit 前置检查失败：active=%d rgba=%p %dx%d",
			       (int)vk.active, (const void *)rgba, w, h);
		return false;
	}

	/* 尺寸变化（或首次）时重建纹理与 transfer buffer。 */
	if (vk.frame_w != w || vk.frame_h != h) {
		if (vk.frame_tex) {
			SDL_ReleaseGPUTexture(vk.dev, vk.frame_tex);
			vk.frame_tex = NULL;
		}
		if (vk.frame_xfer) {
			SDL_ReleaseGPUTransferBuffer(vk.dev, vk.frame_xfer);
			vk.frame_xfer = NULL;
		}
		vk.frame_tex = SDL_CreateGPUTexture(vk.dev, &(SDL_GPUTextureCreateInfo){
			.type = SDL_GPU_TEXTURETYPE_2D,
			.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
			.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER |
				 SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
			.width = (Uint32)w,
			.height = (Uint32)h,
			.layer_count_or_depth = 1,
			.num_levels = 1,
		});
		vk.frame_xfer = SDL_CreateGPUTransferBuffer(vk.dev,
			&(SDL_GPUTransferBufferCreateInfo){
				.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
				.size = (Uint32)w * h * 4 });
		if (!vk.frame_tex || !vk.frame_xfer) {
			NOTICE("[vk] frame 纹理创建失败：%s", SDL_GetError());
			return false;
		}
		vk.frame_w = w;
		vk.frame_h = h;
	}

	/* 拷贝到位 */
	void *map = SDL_MapGPUTransferBuffer(vk.dev, vk.frame_xfer, false);
	if (!map)
		return false;
	memcpy(map, rgba, (size_t)w * h * 4);
	SDL_UnmapGPUTransferBuffer(vk.dev, vk.frame_xfer);

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(vk.dev);
	if (!cmd)
		return false;
	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	SDL_UploadToGPUTexture(cp, &(SDL_GPUTextureTransferInfo){
			.transfer_buffer = vk.frame_xfer, .offset = 0 },
		&(SDL_GPUTextureRegion){
			.texture = vk.frame_tex, .w = (Uint32)w, .h = (Uint32)h,
			.d = 1 }, false);
	SDL_EndGPUCopyPass(cp);

	SDL_GPUTexture *swap = NULL;
	Uint32 sw = 0, sh = 0;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, vk.win, &swap, &sw, &sh) || !swap) {
		/* [D521d] 一次性失败日志：swapchain 拿不到就要说话，
		 * 否则「图没上屏」在日志里完全不可见 */
		static int nr_failsw;
		if (nr_failsw++ < 3)
			NOTICE("[vk] WaitAndAcquireGPUSwapchainTexture 失败：%s",
			       SDL_GetError());
		SDL_SubmitGPUCommandBuffer(cmd);
		return false;
	}

	SDL_GPUColorTargetInfo ct = {
		.texture = swap,
		.load_op = SDL_GPU_LOADOP_DONT_CARE,
		.store_op = SDL_GPU_STOREOP_STORE,
	};
	SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
	SDL_BindGPUGraphicsPipeline(rp, vk.blit_pipe);
	SDL_BindGPUFragmentSamplers(rp, 0,
		&(SDL_GPUTextureSamplerBinding){
			.texture = vk.frame_tex, .sampler = vk.sampler }, 1);
	SDL_BindGPUVertexBuffers(rp, 0, &(SDL_GPUBufferBinding){
		.buffer = vk.blit_vbo, .offset = 0 }, 1);
	SDL_DrawGPUPrimitives(rp, 6, 1, 0, 0);
	SDL_EndGPURenderPass(rp);
	SDL_SubmitGPUCommandBuffer(cmd);
	/* [D521d] 一次性上屏判据：首帧 present 成功只打一行，
	 * 跑测日志可证「图真的上了屏」（此前 blit 失败全静默，
	 * 只有纹理创建失败才有日志）。XSYS4_VK_BLIT_LOG=1 追加
	 * 每千帧一次心跳。 */
	{
		static bool first_logged = false;
		static int ok_count = 0;
		if (!first_logged) {
			NOTICE("[vk] first present %dx%d ok", w, h);
			first_logged = true;
		}
		ok_count++;
		if (getenv("XSYS4_VK_BLIT_LOG") && ok_count % 1000 == 0)
			NOTICE("[vk] blit 心跳：%d 帧", ok_count);
	}
	return true;
}

void gfx_vk_fini(void)
{
	if (!vk.active)
		return;
	if (vk.frame_tex) { SDL_ReleaseGPUTexture(vk.dev, vk.frame_tex); vk.frame_tex = NULL; }
	if (vk.frame_xfer) { SDL_ReleaseGPUTransferBuffer(vk.dev, vk.frame_xfer); vk.frame_xfer = NULL; }
	if (vk.sampler) { SDL_ReleaseGPUSampler(vk.dev, vk.sampler); vk.sampler = NULL; }
	if (vk.blit_vbo) { SDL_ReleaseGPUBuffer(vk.dev, vk.blit_vbo); vk.blit_vbo = NULL; }
	if (vk.blit_pipe) { SDL_ReleaseGPUGraphicsPipeline(vk.dev, vk.blit_pipe); vk.blit_pipe = NULL; }
	if (vk.blit_vs) { SDL_ReleaseGPUShader(vk.dev, vk.blit_vs); vk.blit_vs = NULL; }
	if (vk.blit_fs) { SDL_ReleaseGPUShader(vk.dev, vk.blit_fs); vk.blit_fs = NULL; }
	if (vk.xfer)
		SDL_ReleaseGPUTransferBuffer(vk.dev, vk.xfer);
	if (vk.vbo)
		SDL_ReleaseGPUBuffer(vk.dev, vk.vbo);
	if (vk.pipe)
		SDL_ReleaseGPUGraphicsPipeline(vk.dev, vk.pipe);
	SDL_ReleaseWindowFromGPUDevice(vk.dev, vk.win);
	SDL_DestroyGPUDevice(vk.dev);
	memset(&vk, 0, sizeof(vk));
}

/* 只清屏（3D 页未激活时的 Vulkan 帧，避免黑屏无反馈）。 */
void gfx_vk_clear_screen(void)
{
	if (!vk.active)
		return;
	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(vk.dev);
	if (!cmd)
		return;
	SDL_GPUTexture *swap = NULL;
	Uint32 sw = 0, sh = 0;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, vk.win, &swap, &sw, &sh) || !swap) {
		SDL_SubmitGPUCommandBuffer(cmd);
		return;
	}
	SDL_GPUColorTargetInfo ct = {
		.texture = swap,
		.clear_color = { 0.06f, 0.06f, 0.08f, 1.0f },
		.load_op = SDL_GPU_LOADOP_CLEAR,
		.store_op = SDL_GPU_STOREOP_STORE,
	};
	SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
	SDL_EndGPURenderPass(rp);
	SDL_SubmitGPUCommandBuffer(cmd);
}

/* 画 N 个面：每面 4 顶点（单位方形）+ 自己的 world 矩阵 + 顶点色。
 * 坐标约定与 GL 路径一致：world 把 (0,0)-(1,1) 映射到该面。 */
bool gfx_vk_draw_faces(const float (*world)[16], const float (*colors)[3],
		       int n, const float view[16])
{
	if (!vk.active || n <= 0 || n > 6)
		return false;

	/* 上传顶点：每面 4 个单位方形顶点 + 该面颜色。 */
	float *map = SDL_MapGPUTransferBuffer(vk.dev, vk.xfer, true);
	if (!map)
		return false;
	const float quad[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
	for (int f = 0; f < n; f++) {
		for (int v = 0; v < 4; v++) {
			float *d = map + (f * 4 + v) * 6;
			d[0] = quad[v][0];
			d[1] = quad[v][1];
			d[2] = 0.0f;
			d[3] = colors[f][0];
			d[4] = colors[f][1];
			d[5] = colors[f][2];
		}
	}
	SDL_UnmapGPUTransferBuffer(vk.dev, vk.xfer);

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(vk.dev);
	if (!cmd)
		return false;

	SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
	SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){
			.transfer_buffer = vk.xfer, .offset = 0 },
		&(SDL_GPUBufferRegion){ .buffer = vk.vbo, .offset = 0,
			.size = (Uint32)(n * 4 * 6 * sizeof(float)) }, true);
	SDL_EndGPUCopyPass(cp);

	SDL_GPUTexture *swap = NULL;
	Uint32 sw = 0, sh = 0;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, vk.win, &swap, &sw, &sh) || !swap) {
		SDL_SubmitGPUCommandBuffer(cmd);
		return false;
	}

	/* 清屏色 = 预览背景（与 GL 路径的 bg 一致）。 */
	SDL_GPUColorTargetInfo ct = {
		.texture = swap,
		.clear_color = { 20 / 255.0f, 22 / 255.0f, 28 / 255.0f, 1.0f },
		.load_op = SDL_GPU_LOADOP_CLEAR,
		.store_op = SDL_GPU_STOREOP_STORE,
	};
	SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
	SDL_BindGPUGraphicsPipeline(rp, vk.pipe);
	SDL_BindGPUVertexBuffers(rp, 0, &(SDL_GPUBufferBinding){
		.buffer = vk.vbo, .offset = 0 }, 1);

	for (int f = 0; f < n; f++) {
		struct vk_ubo ubo;
		memcpy(ubo.world, world[f], sizeof(ubo.world));
		memcpy(ubo.view, view, sizeof(ubo.view));
		SDL_PushGPUVertexUniformData(cmd, 0, &ubo, sizeof(ubo));
		SDL_DrawGPUPrimitives(rp, 4, 1, (Uint32)(f * 4), 0);
	}

	SDL_EndGPURenderPass(rp);
	SDL_SubmitGPUCommandBuffer(cmd);
	return true;
}
