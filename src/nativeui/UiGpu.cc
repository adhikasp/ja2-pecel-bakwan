#include "UiGpu.h"

#include "UiCore.h"

#include <algorithm>
#include <cstring>

namespace nui
{
namespace
{
	struct Vertex { float x, y, u, v; uint8_t r, g, b, a; };
	static_assert(sizeof(Vertex) == 20, "the shared quad pipeline vertex layout");

	uint32_t Align4(size_t const v) { return uint32_t((v + 3) & ~size_t(3)); }

	/** Premultiplies straight-alpha RGBA in place (the pipeline blends premultiplied). */
	void Premultiply(unsigned char* const p, size_t const pixels)
	{
		for (size_t i = 0; i < pixels; ++i)
		{
			unsigned const a = p[i * 4 + 3];
			for (int c = 0; c < 3; ++c) p[i * 4 + c] = static_cast<unsigned char>(p[i * 4 + c] * a / 255);
		}
	}
}

SdlGpuRenderInterface::SdlGpuRenderInterface(GpuPipeline const& pipe) : m_pipe(pipe) {}

SdlGpuRenderInterface::~SdlGpuRenderInterface()
{
	for (Geometry* g : m_geometries) delete g;
	if (m_pipe.device)
	{
		for (Texture* t : m_textures)
		{
			if (t->gpu) SDL_ReleaseGPUTexture(m_pipe.device, t->gpu);
			if (t->sampler) SDL_ReleaseGPUSampler(m_pipe.device, t->sampler);
			delete t;
		}
		for (SDL_GPUBuffer* b : { m_vertBuf, m_indexBuf }) if (b) SDL_ReleaseGPUBuffer(m_pipe.device, b);
		if (m_upload) SDL_ReleaseGPUTransferBuffer(m_pipe.device, m_upload);
	}
}

void SdlGpuRenderInterface::Prepare(int const w, int const h)
{
	m_w = w;
	m_h = h;
	m_verts.clear();
	m_indices.clear();
	m_draws.clear();
}

Rml::CompiledGeometryHandle SdlGpuRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i)
{
	auto* g = new Geometry{ v, i };
	m_geometries.push_back(g);
	return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
}

void SdlGpuRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle g)
{
	auto* geo = reinterpret_cast<Geometry*>(g);
	m_geometries.erase(std::remove(m_geometries.begin(), m_geometries.end(), geo), m_geometries.end());
	delete geo;
}

void SdlGpuRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f t, Rml::TextureHandle texture)
{
	Geometry const& g = *reinterpret_cast<Geometry*>(handle);
	Texture* const tex = reinterpret_cast<Texture*>(texture);
	if (tex && !tex->gpu) return;

	Batch draw;
	draw.firstIndex = uint32_t(m_indices.size());
	draw.indexCount = uint32_t(g.indices.size());
	draw.texture = tex;
	draw.scissor = m_scissorOn;
	draw.rect = m_scissorOn ? m_scissor : SDL_Rect{ 0, 0, m_w, m_h };
	if (draw.indexCount == 0) return;

	uint32_t const base = uint32_t(m_verts.size() / sizeof(Vertex));
	for (Rml::Vertex const& v : g.vertices)
	{
		Vertex out;
		out.x = (v.position.x + t.x) / float(m_w) * 2.0f - 1.0f;
		out.y = 1.0f - (v.position.y + t.y) / float(m_h) * 2.0f;
		out.u = v.tex_coord.x;
		out.v = v.tex_coord.y;
		out.r = v.colour.red;
		out.g = v.colour.green;
		out.b = v.colour.blue;
		out.a = v.colour.alpha;
		auto const* bytes = reinterpret_cast<uint8_t const*>(&out);
		m_verts.insert(m_verts.end(), bytes, bytes + sizeof(out));
	}
	for (int const i : g.indices) m_indices.push_back(base + uint32_t(i));
	m_draws.push_back(draw);
}

SdlGpuRenderInterface::Texture* SdlGpuRenderInterface::MakeTexture(unsigned char const* rgba, int const w, int const h,
	bool const repeat, bool const nearest, bool const premultiply)
{
	if (!m_pipe.device || w <= 0 || h <= 0) return nullptr;
	std::vector<unsigned char> pixels(rgba, rgba + size_t(w) * h * 4);
	if (premultiply) Premultiply(pixels.data(), size_t(w) * h);

	SDL_GPUTextureCreateInfo ti{};
	ti.type = SDL_GPU_TEXTURETYPE_2D;
	ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	ti.width = uint32_t(w);
	ti.height = uint32_t(h);
	ti.layer_count_or_depth = 1;
	ti.num_levels = 1;
	ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
	SDL_GPUTexture* const gpu = SDL_CreateGPUTexture(m_pipe.device, &ti);
	if (!gpu) return nullptr;

	SDL_GPUSamplerCreateInfo si{};
	si.min_filter = si.mag_filter = nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
	si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	SDL_GPUSamplerAddressMode const mode = repeat ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	si.address_mode_u = si.address_mode_v = si.address_mode_w = mode;
	si.max_lod = 1000.0f;
	SDL_GPUSampler* const sampler = SDL_CreateGPUSampler(m_pipe.device, &si);
	if (!sampler) { SDL_ReleaseGPUTexture(m_pipe.device, gpu); return nullptr; }

	// Upload on a command buffer of its own: texture creation happens while a document loads or updates, never
	// inside the frame's render pass.
	SDL_GPUTransferBufferCreateInfo tci{};
	tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	tci.size = uint32_t(pixels.size());
	SDL_GPUTransferBuffer* const tb = SDL_CreateGPUTransferBuffer(m_pipe.device, &tci);
	SDL_GPUCommandBuffer* const cmd = tb ? SDL_AcquireGPUCommandBuffer(m_pipe.device) : nullptr;
	if (!tb || !cmd)
	{
		if (tb) SDL_ReleaseGPUTransferBuffer(m_pipe.device, tb);
		SDL_ReleaseGPUSampler(m_pipe.device, sampler);
		SDL_ReleaseGPUTexture(m_pipe.device, gpu);
		return nullptr;
	}
	void* const map = SDL_MapGPUTransferBuffer(m_pipe.device, tb, false);
	if (map) std::memcpy(map, pixels.data(), pixels.size());
	SDL_UnmapGPUTransferBuffer(m_pipe.device, tb);
	SDL_GPUCopyPass* const copy = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTextureTransferInfo const src{ tb, 0, uint32_t(w), uint32_t(h) };
	SDL_GPUTextureRegion const dst{ gpu, 0, 0, 0, 0, 0, uint32_t(w), uint32_t(h), 1 };
	SDL_UploadToGPUTexture(copy, &src, &dst, false);
	SDL_EndGPUCopyPass(copy);
	SDL_SubmitGPUCommandBuffer(cmd);
	SDL_ReleaseGPUTransferBuffer(m_pipe.device, tb);

	auto* tex = new Texture{ gpu, sampler };
	m_textures.push_back(tex);
	return tex;
}

Rml::TextureHandle SdlGpuRenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source)
{
	std::vector<unsigned char> pixels;
	bool repeat = false;
	if (!ResolveTextureSource(source, pixels, dimensions, repeat)) return {};
	return reinterpret_cast<Rml::TextureHandle>(MakeTexture(pixels.data(), dimensions.x, dimensions.y, repeat, false, true));
}

Rml::TextureHandle SdlGpuRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> src, Rml::Vector2i dim)
{
	if (src.empty() || dim.x <= 0 || dim.y <= 0) return {};
	// RmlUi's generated textures are premultiplied already.
	return reinterpret_cast<Rml::TextureHandle>(MakeTexture(src.data(), dim.x, dim.y, false, true, false));
}

void SdlGpuRenderInterface::ReleaseTexture(Rml::TextureHandle handle)
{
	auto* t = reinterpret_cast<Texture*>(handle);
	auto it = std::find(m_textures.begin(), m_textures.end(), t);
	if (it == m_textures.end()) return;
	m_textures.erase(it);
	if (m_pipe.device)
	{
		if (t->gpu) SDL_ReleaseGPUTexture(m_pipe.device, t->gpu);
		if (t->sampler) SDL_ReleaseGPUSampler(m_pipe.device, t->sampler);
	}
	delete t;
}

void SdlGpuRenderInterface::EnableScissorRegion(bool const enable) { m_scissorOn = enable; }

void SdlGpuRenderInterface::SetScissorRegion(Rml::Rectanglei r)
{
	m_scissor.x = std::max(0, r.Left());
	m_scissor.y = std::max(0, r.Top());
	m_scissor.w = std::max(0, std::min(r.Right(), m_w) - m_scissor.x);
	m_scissor.h = std::max(0, std::min(r.Bottom(), m_h) - m_scissor.y);
}

bool SdlGpuRenderInterface::EnsureBuffer(SDL_GPUBuffer*& buf, uint32_t& size, uint32_t const bytes, SDL_GPUBufferUsageFlags const usage)
{
	uint32_t const want = std::max<uint32_t>(Align4(bytes), 16);
	if (buf && size >= want) return true;
	if (buf) SDL_ReleaseGPUBuffer(m_pipe.device, buf);
	SDL_GPUBufferCreateInfo ci{};
	ci.usage = usage;
	ci.size = std::max<uint32_t>(want, size + size / 2);
	buf = SDL_CreateGPUBuffer(m_pipe.device, &ci);
	size = buf ? ci.size : 0;
	return buf != nullptr;
}

bool SdlGpuRenderInterface::Upload(SDL_GPUCommandBuffer* cmd)
{
	if (!cmd || !m_pipe.device || m_draws.empty()) return false;
	size_t const vb = m_verts.size();
	size_t const ib = m_indices.size() * 4;
	if (!EnsureBuffer(m_vertBuf, m_vertBufSize, uint32_t(vb), SDL_GPU_BUFFERUSAGE_VERTEX)) return false;
	if (!EnsureBuffer(m_indexBuf, m_indexBufSize, uint32_t(ib), SDL_GPU_BUFFERUSAGE_INDEX)) return false;

	size_t const total = Align4(vb) + Align4(ib);
	if (!m_upload || m_uploadSize < total)
	{
		if (m_upload) SDL_ReleaseGPUTransferBuffer(m_pipe.device, m_upload);
		SDL_GPUTransferBufferCreateInfo ci{};
		ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		ci.size = uint32_t(std::max<size_t>(total + total / 2, 1u << 16));
		m_upload = SDL_CreateGPUTransferBuffer(m_pipe.device, &ci);
		m_uploadSize = m_upload ? ci.size : 0;
	}
	if (!m_upload) return false;
	uint8_t* const map = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(m_pipe.device, m_upload, true));
	if (!map) return false;
	std::memcpy(map, m_verts.data(), vb);
	std::memcpy(map + Align4(vb), m_indices.data(), ib);
	SDL_UnmapGPUTransferBuffer(m_pipe.device, m_upload);

	SDL_GPUCopyPass* const copy = SDL_BeginGPUCopyPass(cmd);
	{
		SDL_GPUTransferBufferLocation const src{ m_upload, 0 };
		SDL_GPUBufferRegion const dst{ m_vertBuf, 0, uint32_t(vb) };
		SDL_UploadToGPUBuffer(copy, &src, &dst, false);
	}
	{
		SDL_GPUTransferBufferLocation const src{ m_upload, Align4(vb) };
		SDL_GPUBufferRegion const dst{ m_indexBuf, 0, uint32_t(ib) };
		SDL_UploadToGPUBuffer(copy, &src, &dst, false);
	}
	SDL_EndGPUCopyPass(copy);
	return true;
}

void SdlGpuRenderInterface::Draw(SDL_GPURenderPass* pass)
{
	if (!pass || m_draws.empty() || !m_pipe.pipeline) return;
	SDL_BindGPUGraphicsPipeline(pass, m_pipe.pipeline);
	SDL_GPUBufferBinding const vbind{ m_vertBuf, 0 };
	SDL_BindGPUVertexBuffers(pass, 0, &vbind, 1);
	SDL_GPUBufferBinding const ibind{ m_indexBuf, 0 };
	SDL_BindGPUIndexBuffer(pass, &ibind, SDL_GPU_INDEXELEMENTSIZE_32BIT);
	SDL_Rect const full{ 0, 0, m_w, m_h };
	for (Batch const& d : m_draws)
	{
		if (d.indexCount == 0) continue;
		SDL_SetGPUScissor(pass, d.scissor ? &d.rect : &full);
		SDL_GPUTexture* const tex = d.texture ? d.texture->gpu : m_pipe.white;
		SDL_GPUSampler* const sampler = d.texture && d.texture->sampler ? d.texture->sampler : m_pipe.linear;
		SDL_GPUTextureSamplerBinding const tsb{ tex, sampler };
		SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
		SDL_DrawGPUIndexedPrimitives(pass, d.indexCount, 1, d.firstIndex, 0, 0);
	}
}

} // namespace nui
