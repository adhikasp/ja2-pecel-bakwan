#include "VideoGpu.h"
#include "Logger.h"
#include "shaders/quad.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace VideoGpu
{
namespace
{
	SDL_GPUDevice*           g_device = nullptr;
	SDL_Window*              g_window = nullptr;
	SDL_GPUGraphicsPipeline* g_pipeline = nullptr;
	SDL_GPUShader*           g_vert = nullptr;
	SDL_GPUShader*           g_frag = nullptr;
	SDL_GPUSampler*          g_nearest = nullptr;
	SDL_GPUSampler*          g_linear = nullptr;
	SDL_GPUTexture*          g_white = nullptr;
	SDL_GPUTextureFormat     g_format = SDL_GPU_TEXTUREFORMAT_INVALID;
	Resources                g_res;
	std::string              g_error;

	// The frame in progress.
	SDL_GPUCommandBuffer*    g_cmd = nullptr;
	SDL_GPUTexture*          g_swapchain = nullptr;
	SDL_GPURenderPass*       g_pass = nullptr;
	int                      g_w = 0, g_h = 0;

	// The compositor's triangles.
	std::vector<Vertex>      g_verts;
	std::vector<uint32_t>    g_indices;
	struct Layer { SDL_GPUTexture* tex; SDL_GPUSampler* sampler; uint32_t firstIndex; uint32_t indexCount; };
	std::vector<Layer>       g_layers;
	SDL_GPUBuffer*           g_vertBuf = nullptr;
	uint32_t                 g_vertBufSize = 0;
	SDL_GPUBuffer*           g_indexBuf = nullptr;
	uint32_t                 g_indexBufSize = 0;

	// Reusable staging.
	SDL_GPUTransferBuffer*   g_upload = nullptr;
	uint32_t                 g_uploadSize = 0;

	// The capture path.
	SDL_GPUTexture*          g_captureTex = nullptr;
	int                      g_captureW = 0, g_captureH = 0;
	bool                     g_wantCapture = false;
	SDL_GPUTransferBuffer*   g_download = nullptr;
	uint32_t                 g_downloadSize = 0;
	std::vector<uint8_t>     g_captureRgb;
	int                      g_captureOutW = 0, g_captureOutH = 0;

	uint32_t Align4(size_t v) { return uint32_t((v + 3) & ~size_t(3)); }

	bool EnsureBuffer(SDL_GPUBuffer*& buf, uint32_t& size, uint32_t bytes, SDL_GPUBufferUsageFlags usage)
	{
		bytes = std::max<uint32_t>(Align4(bytes), 16);
		if (buf && size >= bytes) return true;
		if (buf) SDL_ReleaseGPUBuffer(g_device, buf);
		SDL_GPUBufferCreateInfo ci{};
		ci.usage = usage;
		ci.size = std::max<uint32_t>(bytes, size + size / 2);
		buf = SDL_CreateGPUBuffer(g_device, &ci);
		size = buf ? ci.size : 0;
		if (!buf) g_error = std::string("creating a GPU buffer failed: ") + SDL_GetError();
		return buf != nullptr;
	}

	bool EnsureUpload(uint32_t bytes)
	{
		if (g_upload && g_uploadSize >= bytes) return true;
		if (g_upload) SDL_ReleaseGPUTransferBuffer(g_device, g_upload);
		SDL_GPUTransferBufferCreateInfo ci{};
		ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		ci.size = std::max<uint32_t>(bytes + bytes / 2, 1u << 16);
		g_upload = SDL_CreateGPUTransferBuffer(g_device, &ci);
		g_uploadSize = g_upload ? ci.size : 0;
		if (!g_upload) g_error = std::string("creating the upload buffer failed: ") + SDL_GetError();
		return g_upload != nullptr;
	}

	bool ShaderCode(SDL_GPUShaderFormat const formats, bool const vertex,
	                const uint8_t*& code, size_t& size, const char*& entry, SDL_GPUShaderFormat& format)
	{
		if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
		{
			code = vertex ? quad_vert_spirv : quad_frag_spirv;
			size = vertex ? quad_vert_spirv_size : quad_frag_spirv_size;
			entry = "main";
			format = SDL_GPU_SHADERFORMAT_SPIRV;
			return true;
		}
		if (formats & SDL_GPU_SHADERFORMAT_DXBC)
		{
			code = vertex ? quad_vert_dxbc : quad_frag_dxbc;
			size = vertex ? quad_vert_dxbc_size : quad_frag_dxbc_size;
			entry = "main";
			format = SDL_GPU_SHADERFORMAT_DXBC;
			return true;
		}
		if (formats & SDL_GPU_SHADERFORMAT_MSL)
		{
			code = vertex ? quad_vert_msl : quad_frag_msl;
			size = vertex ? quad_vert_msl_size : quad_frag_msl_size;
			entry = "main0";
			format = SDL_GPU_SHADERFORMAT_MSL;
			return true;
		}
		return false;
	}

	SDL_GPUTexture* CreateTexture(int const w, int const h, SDL_GPUTextureFormat const format,
	                              SDL_GPUTextureUsageFlags const usage)
	{
		SDL_GPUTextureCreateInfo ci{};
		ci.type = SDL_GPU_TEXTURETYPE_2D;
		ci.format = format;
		ci.usage = usage;
		ci.width = uint32_t(w);
		ci.height = uint32_t(h);
		ci.layer_count_or_depth = 1;
		ci.num_levels = 1;
		ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
		return SDL_CreateGPUTexture(g_device, &ci);
	}

	void NdcX(float const px, float& out) { out = px / float(g_w) * 2.0f - 1.0f; }
	void NdcY(float const py, float& out) { out = 1.0f - py / float(g_h) * 2.0f; }
}

bool Init(SDL_GPUDevice* const device, SDL_Window* const window)
{
	Shutdown();
	if (!device || !window) { g_error = "no device or window"; return false; }
	g_device = device;
	g_window = window;

	if (!SDL_ClaimWindowForGPUDevice(device, window))
	{
		g_error = std::string("claiming the window for SDL_GPU failed: ") + SDL_GetError();
		g_device = nullptr;
		return false;
	}
	g_format = SDL_GetGPUSwapchainTextureFormat(device, window);

	SDL_GPUShaderFormat const formats = SDL_GetGPUShaderFormats(device);
	const uint8_t* code = nullptr;
	size_t size = 0;
	const char* entry = nullptr;
	SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;

	SDL_GPUShaderCreateInfo si{};
	if (!ShaderCode(formats, true, code, size, entry, format)) { g_error = "no vertex shader format"; Shutdown(); return false; }
	si.code_size = size; si.code = code; si.entrypoint = entry; si.format = format; si.stage = SDL_GPU_SHADERSTAGE_VERTEX;
	g_vert = SDL_CreateGPUShader(device, &si);
	if (!ShaderCode(formats, false, code, size, entry, format)) { g_error = "no fragment shader format"; Shutdown(); return false; }
	si.code_size = size; si.code = code; si.entrypoint = entry; si.format = format; si.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	si.num_samplers = 1;
	g_frag = SDL_CreateGPUShader(device, &si);
	if (!g_vert || !g_frag) { g_error = std::string("creating the quad shaders failed: ") + SDL_GetError(); Shutdown(); return false; }

	SDL_GPUVertexBufferDescription const vb{ 0, Uint32(sizeof(Vertex)), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 };
	SDL_GPUVertexAttribute const attrs[3] = {
		{ 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,     offsetof(Vertex, x) },
		{ 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,     offsetof(Vertex, u) },
		{ 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(Vertex, r) },
	};

	SDL_GPUColorTargetBlendState blend{};
	blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
	blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
	blend.enable_blend = true;
	SDL_GPUColorTargetDescription const target{ g_format, blend };

	SDL_GPUGraphicsPipelineCreateInfo ci{};
	ci.vertex_shader = g_vert;
	ci.fragment_shader = g_frag;
	ci.vertex_input_state.vertex_buffer_descriptions = &vb;
	ci.vertex_input_state.num_vertex_buffers = 1;
	ci.vertex_input_state.vertex_attributes = attrs;
	ci.vertex_input_state.num_vertex_attributes = 3;
	ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	ci.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
	ci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
	for (SDL_GPUStencilOpState* s : { &ci.depth_stencil_state.back_stencil_state, &ci.depth_stencil_state.front_stencil_state })
	{
		s->fail_op = SDL_GPU_STENCILOP_KEEP;
		s->pass_op = SDL_GPU_STENCILOP_KEEP;
		s->depth_fail_op = SDL_GPU_STENCILOP_KEEP;
		s->compare_op = SDL_GPU_COMPAREOP_ALWAYS;
	}
	ci.target_info.color_target_descriptions = &target;
	ci.target_info.num_color_targets = 1;
	ci.target_info.has_depth_stencil_target = false;
	g_pipeline = SDL_CreateGPUGraphicsPipeline(device, &ci);
	if (!g_pipeline) { g_error = std::string("creating the quad pipeline failed: ") + SDL_GetError(); Shutdown(); return false; }

	auto const makeSampler = [&](SDL_GPUFilter const filter) {
		SDL_GPUSamplerCreateInfo sci{};
		sci.min_filter = filter;
		sci.mag_filter = filter;
		sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sci.max_lod = 1000.0f;
		return SDL_CreateGPUSampler(device, &sci);
	};
	g_nearest = makeSampler(SDL_GPU_FILTER_NEAREST);
	g_linear = makeSampler(SDL_GPU_FILTER_LINEAR);
	if (!g_nearest || !g_linear) { g_error = std::string("creating the quad samplers failed: ") + SDL_GetError(); Shutdown(); return false; }

	// A 1x1 white texel, so untextured geometry can use the same pipeline.
	g_white = CreateTexture(1, 1, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER);
	if (!g_white || !EnsureUpload(4)) { g_error = std::string("creating the white texture failed: ") + SDL_GetError(); Shutdown(); return false; }
	{
		uint8_t* const map = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device, g_upload, false));
		if (!map) { g_error = "mapping the upload buffer failed"; Shutdown(); return false; }
		map[0] = map[1] = map[2] = map[3] = 255;
		SDL_UnmapGPUTransferBuffer(device, g_upload);
		SDL_GPUCommandBuffer* const cmd = SDL_AcquireGPUCommandBuffer(device);
		if (!cmd) { g_error = std::string("no command buffer: ") + SDL_GetError(); Shutdown(); return false; }
		SDL_GPUCopyPass* const copy = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTextureTransferInfo const src{ g_upload, 0, 1, 1 };
		SDL_GPUTextureRegion const dst{ g_white, 0, 0, 0, 0, 0, 1, 1, 1 };
		SDL_UploadToGPUTexture(copy, &src, &dst, false);
		SDL_EndGPUCopyPass(copy);
		SDL_SubmitGPUCommandBuffer(cmd);
	}

	g_res = Resources{ device, g_pipeline, g_nearest, g_linear, g_white };
	SLOGI("Presentation: SDL_GPU on {}", SDL_GetGPUDeviceDriver(device));
	return true;
}

void Shutdown()
{
	if (g_device)
	{
		SDL_WaitForGPUIdle(g_device);
		if (g_vertBuf) SDL_ReleaseGPUBuffer(g_device, g_vertBuf);
		if (g_indexBuf) SDL_ReleaseGPUBuffer(g_device, g_indexBuf);
		if (g_upload) SDL_ReleaseGPUTransferBuffer(g_device, g_upload);
		if (g_download) SDL_ReleaseGPUTransferBuffer(g_device, g_download);
		if (g_captureTex) SDL_ReleaseGPUTexture(g_device, g_captureTex);
		if (g_white) SDL_ReleaseGPUTexture(g_device, g_white);
		if (g_pipeline) SDL_ReleaseGPUGraphicsPipeline(g_device, g_pipeline);
		if (g_vert) SDL_ReleaseGPUShader(g_device, g_vert);
		if (g_frag) SDL_ReleaseGPUShader(g_device, g_frag);
		if (g_nearest) SDL_ReleaseGPUSampler(g_device, g_nearest);
		if (g_linear) SDL_ReleaseGPUSampler(g_device, g_linear);
		if (g_window) SDL_ReleaseWindowFromGPUDevice(g_device, g_window);
	}
	g_vertBuf = g_indexBuf = nullptr;
	g_upload = g_download = nullptr;
	g_captureTex = g_white = nullptr;
	g_pipeline = nullptr;
	g_vert = g_frag = nullptr;
	g_nearest = g_linear = nullptr;
	g_vertBufSize = g_indexBufSize = g_uploadSize = g_downloadSize = 0;
	g_verts.clear();
	g_indices.clear();
	g_layers.clear();
	g_captureRgb.clear();
	g_captureOutW = g_captureOutH = 0;
	g_device = nullptr;
	g_window = nullptr;
}

bool Active() { return g_pipeline != nullptr; }
std::string const& Error() { return g_error; }
Resources const& GetResources() { return g_res; }
SDL_GPUCommandBuffer* Cmd() { return g_cmd; }
SDL_GPURenderPass* Pass() { return g_pass; }
int Width() { return g_w; }
int Height() { return g_h; }
bool SwapchainIsBgra() { return g_format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM; }
int CaptureWidth() { return g_captureOutW; }
int CaptureHeight() { return g_captureOutH; }
std::vector<uint8_t> const& CaptureRgb() { return g_captureRgb; }
void ClearCapture() { g_captureRgb.clear(); g_captureOutW = g_captureOutH = 0; }

bool BeginFrame()
{
	if (!Active()) return false;
	g_cmd = SDL_AcquireGPUCommandBuffer(g_device);
	if (!g_cmd) { g_error = std::string("no command buffer: ") + SDL_GetError(); return false; }
	uint32_t w = 0, h = 0;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(g_cmd, g_window, &g_swapchain, &w, &h) || !g_swapchain)
	{
		// Not an error: the window is minimised or the GPU is behind. Skip the frame.
		g_swapchain = nullptr;
		SDL_SubmitGPUCommandBuffer(g_cmd);
		g_cmd = nullptr;
		return false;
	}
	g_w = int(w);
	g_h = int(h);
	QuadReset();
	return true;
}

SDL_GPUTexture* UploadRgba(SDL_GPUTexture*& slot, int& slotW, int& slotH, void const* const pixels, int const w, int const h)
{
	if (!Active() || !g_cmd || w <= 0 || h <= 0) return nullptr;
	if (!slot || slotW != w || slotH != h)
	{
		if (slot) SDL_ReleaseGPUTexture(g_device, slot);
		slot = CreateTexture(w, h, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER);
		slotW = slot ? w : 0;
		slotH = slot ? h : 0;
		if (!slot) { g_error = std::string("creating a layer texture failed: ") + SDL_GetError(); return nullptr; }
	}
	size_t const bytes = size_t(w) * h * 4;
	if (!EnsureUpload(uint32_t(bytes))) return nullptr;
	void* const map = SDL_MapGPUTransferBuffer(g_device, g_upload, true);
	if (!map) { g_error = "mapping the upload buffer failed"; return nullptr; }
	std::memcpy(map, pixels, bytes);
	SDL_UnmapGPUTransferBuffer(g_device, g_upload);
	SDL_GPUCopyPass* const copy = SDL_BeginGPUCopyPass(g_cmd);
	SDL_GPUTextureTransferInfo const src{ g_upload, 0, uint32_t(w), uint32_t(h) };
	SDL_GPUTextureRegion const dst{ slot, 0, 0, 0, 0, 0, uint32_t(w), uint32_t(h), 1 };
	SDL_UploadToGPUTexture(copy, &src, &dst, false);
	SDL_EndGPUCopyPass(copy);
	return slot;
}

SDL_GPUTexture* CopyToSampled(SDL_GPUTexture* const src, int const w, int const h, SDL_GPUTexture*& slot, int& slotW, int& slotH)
{
	if (!Active() || !g_cmd || !src || w <= 0 || h <= 0) return src;
	if (!slot || slotW != w || slotH != h)
	{
		if (slot) SDL_ReleaseGPUTexture(g_device, slot);
		slot = CreateTexture(w, h, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER);
		slotW = slot ? w : 0;
		slotH = slot ? h : 0;
		if (!slot) return src;
	}
	SDL_GPUCopyPass* const copy = SDL_BeginGPUCopyPass(g_cmd);
	SDL_GPUTextureLocation const from{ src, 0, 0, 0, 0, 0 };
	SDL_GPUTextureLocation const to{ slot, 0, 0, 0, 0, 0 };
	SDL_CopyGPUTextureToTexture(copy, &from, &to, uint32_t(w), uint32_t(h), 1, false);
	SDL_EndGPUCopyPass(copy);
	return slot;
}

void QuadReset()
{
	g_verts.clear();
	g_indices.clear();
	g_layers.clear();
}

void Quad(SDL_GPUTexture* const tex, float const dx, float const dy, float const dw, float const dh,
          float const u0, float const v0, float const u1, float const v1,
          float const r, float const g, float const b, float const a, bool const nearest)
{
	if (!tex || dw <= 0 || dh <= 0) return;
	uint32_t const base = uint32_t(g_verts.size());
	float x0, y0, x1, y1;
	NdcX(dx, x0); NdcY(dy, y0);
	NdcX(dx + dw, x1); NdcY(dy + dh, y1);
	uint8_t const cr = uint8_t(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
	uint8_t const cg = uint8_t(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
	uint8_t const cb = uint8_t(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
	uint8_t const ca = uint8_t(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
	g_verts.push_back({ x0, y0, u0, v0, cr, cg, cb, ca });
	g_verts.push_back({ x1, y0, u1, v0, cr, cg, cb, ca });
	g_verts.push_back({ x1, y1, u1, v1, cr, cg, cb, ca });
	g_verts.push_back({ x0, y1, u0, v1, cr, cg, cb, ca });
	g_indices.push_back(base + 0); g_indices.push_back(base + 1); g_indices.push_back(base + 2);
	g_indices.push_back(base + 0); g_indices.push_back(base + 2); g_indices.push_back(base + 3);
	g_layers.push_back({ tex, nearest ? g_nearest : g_linear, uint32_t(g_indices.size()) - 6, 6 });
}

bool QuadUpload()
{
	if (g_indices.empty() || !g_cmd) return false;
	if (!EnsureBuffer(g_vertBuf, g_vertBufSize, uint32_t(g_verts.size() * sizeof(Vertex)), SDL_GPU_BUFFERUSAGE_VERTEX)) return false;
	if (!EnsureBuffer(g_indexBuf, g_indexBufSize, uint32_t(g_indices.size() * 4), SDL_GPU_BUFFERUSAGE_INDEX)) return false;
	size_t const vb = g_verts.size() * sizeof(Vertex);
	size_t const ib = g_indices.size() * 4;
	if (!EnsureUpload(uint32_t(vb + ib))) return false;
	uint8_t* const map = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(g_device, g_upload, true));
	if (!map) { g_error = "mapping the upload buffer failed"; return false; }
	std::memcpy(map, g_verts.data(), vb);
	std::memcpy(map + Align4(vb), g_indices.data(), ib);
	SDL_UnmapGPUTransferBuffer(g_device, g_upload);
	SDL_GPUCopyPass* const copy = SDL_BeginGPUCopyPass(g_cmd);
	{
		SDL_GPUTransferBufferLocation const src{ g_upload, 0 };
		SDL_GPUBufferRegion const dst{ g_vertBuf, 0, uint32_t(vb) };
		SDL_UploadToGPUBuffer(copy, &src, &dst, false);
	}
	{
		SDL_GPUTransferBufferLocation const src{ g_upload, Align4(vb) };
		SDL_GPUBufferRegion const dst{ g_indexBuf, 0, uint32_t(ib) };
		SDL_UploadToGPUBuffer(copy, &src, &dst, false);
	}
	SDL_EndGPUCopyPass(copy);
	return true;
}

void BeginPass(float const cr, float const cg, float const cb, bool const capture)
{
	if (!g_cmd) return;
	g_wantCapture = capture;
	SDL_GPUTexture* target = g_swapchain;
	if (capture)
	{
		if (!g_captureTex || g_captureW != g_w || g_captureH != g_h)
		{
			if (g_captureTex) SDL_ReleaseGPUTexture(g_device, g_captureTex);
			g_captureTex = CreateTexture(g_w, g_h, g_format, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
			g_captureW = g_captureTex ? g_w : 0;
			g_captureH = g_captureTex ? g_h : 0;
		}
		if (g_captureTex) target = g_captureTex;
		else { g_wantCapture = false; target = g_swapchain; }
	}
	SDL_GPUColorTargetInfo color{};
	color.texture = target;
	color.clear_color = SDL_FColor{ cr, cg, cb, 1.0f };
	color.load_op = SDL_GPU_LOADOP_CLEAR;
	color.store_op = SDL_GPU_STOREOP_STORE;
	g_pass = SDL_BeginGPURenderPass(g_cmd, &color, 1, nullptr);
}

void QuadDraw()
{
	if (!g_pass || g_indices.empty()) return;
	SDL_BindGPUGraphicsPipeline(g_pass, g_pipeline);
	SDL_GPUBufferBinding const vbind{ g_vertBuf, 0 };
	SDL_BindGPUVertexBuffers(g_pass, 0, &vbind, 1);
	SDL_GPUBufferBinding const ibind{ g_indexBuf, 0 };
	SDL_BindGPUIndexBuffer(g_pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
	for (Layer const& l : g_layers)
	{
		SDL_GPUTextureSamplerBinding const tsb{ l.tex, l.sampler };
		SDL_BindGPUFragmentSamplers(g_pass, 0, &tsb, 1);
		SDL_DrawGPUIndexedPrimitives(g_pass, l.indexCount, 1, l.firstIndex, 0, 0);
	}
}

void EndPass()
{
	if (g_pass) SDL_EndGPURenderPass(g_pass);
	g_pass = nullptr;
}

bool Submit()
{
	if (!g_cmd) return false;
	g_pass = nullptr;

	if (g_wantCapture && g_captureTex)
	{
		size_t const bytes = size_t(g_w) * g_h * 4;
		if (!g_download || g_downloadSize < bytes)
		{
			if (g_download) SDL_ReleaseGPUTransferBuffer(g_device, g_download);
			SDL_GPUTransferBufferCreateInfo ci{};
			ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
			ci.size = uint32_t(bytes);
			g_download = SDL_CreateGPUTransferBuffer(g_device, &ci);
			g_downloadSize = g_download ? ci.size : 0;
		}
		SDL_GPUCopyPass* const copy = SDL_BeginGPUCopyPass(g_cmd);
		if (g_download)
		{
			SDL_GPUTextureRegion const src{ g_captureTex, 0, 0, 0, 0, 0, uint32_t(g_w), uint32_t(g_h), 1 };
			SDL_GPUTextureTransferInfo const dst{ g_download, 0, uint32_t(g_w), uint32_t(g_h) };
			SDL_DownloadFromGPUTexture(copy, &src, &dst);
		}
		SDL_EndGPUCopyPass(copy);
		// Still present the frame: a straight copy into the swapchain texture (blits are command-buffer commands,
		// not copy-pass commands).
		SDL_GPUBlitInfo blit{};
		blit.source.texture = g_captureTex;
		blit.source.w = uint32_t(g_w); blit.source.h = uint32_t(g_h);
		blit.destination.texture = g_swapchain;
		blit.destination.w = uint32_t(g_w); blit.destination.h = uint32_t(g_h);
		blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
		blit.flip_mode = SDL_FLIP_NONE;
		blit.filter = SDL_GPU_FILTER_NEAREST;
		SDL_BlitGPUTexture(g_cmd, &blit);

		SDL_GPUFence* const fence = SDL_SubmitGPUCommandBufferAndAcquireFence(g_cmd);
		g_cmd = nullptr;
		if (fence)
		{
			SDL_WaitForGPUFences(g_device, true, &fence, 1);
			SDL_ReleaseGPUFence(g_device, fence);
			if (g_download)
			{
				uint8_t const* const p = static_cast<uint8_t const*>(SDL_MapGPUTransferBuffer(g_device, g_download, false));
				if (p)
				{
					g_captureRgb.resize(size_t(g_w) * g_h * 3);
					bool const bgra = SwapchainIsBgra();
					for (int y = 0; y < g_h; ++y)
						for (int x = 0; x < g_w; ++x)
						{
							uint8_t const* const s = p + (size_t(y) * g_w + x) * 4;
							uint8_t* const d = &g_captureRgb[(size_t(y) * g_w + x) * 3];
							if (bgra) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; }
							else       { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
						}
					g_captureOutW = g_w;
					g_captureOutH = g_h;
					SDL_UnmapGPUTransferBuffer(g_device, g_download);
				}
			}
		}
		g_wantCapture = false;
		g_swapchain = nullptr;
		return true;
	}

	g_wantCapture = false;
	g_swapchain = nullptr;
	bool const ok = SDL_SubmitGPUCommandBuffer(g_cmd);
	g_cmd = nullptr;
	return ok;
}

} // namespace VideoGpu
