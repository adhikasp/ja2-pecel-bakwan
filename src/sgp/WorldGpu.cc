#include "WorldGpu.h"
#include "shaders/world_raster.comp.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace WorldGpu {

namespace {
using clock_type = std::chrono::steady_clock;
double Ms(clock_type::time_point a, clock_type::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

struct Params
{
	uint32_t width, height, binCols, clearColor, clearDepth, mask, pointCount, pad0;
	float ambientR, ambientG, ambientB, sunR, sunG, sunB, falloff, pad1;
};
static_assert(sizeof(Params) == 64);

uint32_t Align4(size_t v) { return uint32_t((v + 3) & ~size_t(3)); }
}

SDL_GPUShaderFormat ShaderFormats()
{
	return SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_MSL;
}

Renderer::~Renderer()
{
	Shutdown();
}

std::string Renderer::DriverName() const
{
	char const* n = m_device ? SDL_GetGPUDeviceDriver(m_device) : nullptr;
	return n ? n : "";
}

bool Renderer::Init(SDL_GPUDevice* device)
{
	Shutdown();
	m_error.clear();
	if (!device)
	{
		device = SDL_CreateGPUDevice(ShaderFormats(), false, std::getenv("JA2_GPU_DRIVER"));
		if (!device)
		{
			m_error = std::string("no GPU device: ") + SDL_GetError();
			return false;
		}
		m_ownDevice = true;
	}
	m_device = device;
	SDL_GPUShaderFormat const formats = SDL_GetGPUShaderFormats(device);
	if (!SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTURETYPE_2D,
		SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE))
	{
		m_error = "RGBA8 storage textures are not supported";
		Shutdown();
		return false;
	}

	SDL_GPUComputePipelineCreateInfo ci{};
	// The shader in the device's language (tools/shaders/build.sh makes all three from the GLSL source)
	if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
	{
		ci.code = world_raster_spirv; ci.code_size = world_raster_spirv_size; ci.entrypoint = "main"; ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
	}
	else if (formats & SDL_GPU_SHADERFORMAT_DXBC)
	{
		ci.code = world_raster_dxbc; ci.code_size = world_raster_dxbc_size; ci.entrypoint = "main"; ci.format = SDL_GPU_SHADERFORMAT_DXBC;
	}
	else if (formats & SDL_GPU_SHADERFORMAT_MSL)
	{
		ci.code = world_raster_msl; ci.code_size = world_raster_msl_size; ci.entrypoint = "main0"; ci.format = SDL_GPU_SHADERFORMAT_MSL;
	}
	else
	{
		m_error = std::string("the GPU device (") + DriverName() + ") takes none of SPIR-V, DXBC, MSL";
		Shutdown();
		return false;
	}
	ci.num_readonly_storage_buffers = 8;
	ci.num_readwrite_storage_textures = 1;
	ci.num_readwrite_storage_buffers = 1;
	ci.num_uniform_buffers = 1;
	ci.threadcount_x = 16;
	ci.threadcount_y = 16;
	ci.threadcount_z = 1;
	m_pipeline = SDL_CreateGPUComputePipeline(device, &ci);
	if (!m_pipeline)
	{
		m_error = std::string("creating the compute pipeline failed: ") + SDL_GetError();
		Shutdown();
		return false;
	}
	return true;
}

void Renderer::Shutdown()
{
	if (!m_device) return;
	SDL_WaitForGPUIdle(m_device);
	if (m_fence) SDL_ReleaseGPUFence(m_device, m_fence);
	m_fence = nullptr;
	for (Buffer* b : { &m_instances, &m_ranges, &m_items, &m_pixels, &m_palettes, &m_columns, &m_shade, &m_lights, &m_out })
	{
		if (b->buf) SDL_ReleaseGPUBuffer(m_device, b->buf);
		*b = {};
	}
	if (m_upload) SDL_ReleaseGPUTransferBuffer(m_device, m_upload);
	if (m_download) SDL_ReleaseGPUTransferBuffer(m_device, m_download);
	m_upload = m_download = nullptr;
	m_uploadSize = m_downloadSize = 0;
	if (m_texture) SDL_ReleaseGPUTexture(m_device, m_texture);
	m_texture = nullptr;
	m_w = m_h = 0;
	if (m_pipeline) SDL_ReleaseGPUComputePipeline(m_device, m_pipeline);
	m_pipeline = nullptr;
	if (m_ownDevice) SDL_DestroyGPUDevice(m_device);
	m_device = nullptr;
	m_ownDevice = false;
	m_poolGeneration = 0;
	m_shadeCopy.clear();
}

bool Renderer::Ensure(Buffer& b, uint32_t bytes, SDL_GPUBufferUsageFlags usage)
{
	bytes = std::max<uint32_t>(Align4(bytes), 16);
	if (b.buf && b.size >= bytes) return true;
	if (b.buf) SDL_ReleaseGPUBuffer(m_device, b.buf);
	uint32_t const size = std::max<uint32_t>(bytes, b.size + b.size / 2);
	SDL_GPUBufferCreateInfo ci{};
	ci.usage = usage;
	ci.size = size;
	b.buf = SDL_CreateGPUBuffer(m_device, &ci);
	b.size = b.buf ? size : 0;
	if (!b.buf) m_error = std::string("creating a GPU buffer failed: ") + SDL_GetError();
	return b.buf != nullptr;
}

bool Renderer::EnsureTarget(int w, int h)
{
	if (m_texture && m_w == w && m_h == h) return true;
	if (m_texture) SDL_ReleaseGPUTexture(m_device, m_texture);
	SDL_GPUTextureCreateInfo ti{};
	ti.type = SDL_GPU_TEXTURETYPE_2D;
	ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
	ti.width = uint32_t(w);
	ti.height = uint32_t(h);
	ti.layer_count_or_depth = 1;
	ti.num_levels = 1;
	m_texture = SDL_CreateGPUTexture(m_device, &ti);
	m_w = m_texture ? w : 0;
	m_h = m_texture ? h : 0;
	if (!m_texture) m_error = std::string("creating the world texture failed: ") + SDL_GetError();
	return m_texture != nullptr;
}

bool Renderer::Render(WorldPipe::Frame const& f, WorldPipe::SpritePool& pool, uint16_t const* shadeTable, bool readback)
{
	if (!m_pipeline || f.width <= 0 || f.height <= 0) return false;
	auto const t0 = clock_type::now();
	m_bins.Build(f);
	auto const t1 = clock_type::now();
	m_stats = {};
	m_stats.buildMs = Ms(t0, t1);
	m_stats.binItems = m_bins.items.size();

	if (!EnsureTarget(f.width, f.height)) return false;

	SDL_GPUBufferUsageFlags const ro = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
	// The sprite pool: appended to, or all again when it was reset or outgrew the buffer
	size_t const poolBytes = pool.pixels.size() * 2;
	bool fullPool = m_poolGeneration != pool.generation || !m_pixels.buf || m_pixels.size < poolBytes;
	if (fullPool)
	{
		if (m_pixels.buf && m_pixels.size < poolBytes)
		{
			SDL_ReleaseGPUBuffer(m_device, m_pixels.buf);
			m_pixels.buf = nullptr;
			uint32_t const want = uint32_t(std::max<size_t>(poolBytes * 3 / 2, 16u << 20));
			m_pixels.size = 0;
			if (!Ensure(m_pixels, want, ro)) return false;
		}
		else if (!Ensure(m_pixels, uint32_t(std::max<size_t>(poolBytes * 3 / 2, 16u << 20)), ro))
		{
			return false;
		}
		pool.uploaded = 0;
		m_poolGeneration = pool.generation;
	}
	size_t const poolFrom = (pool.uploaded & ~size_t(1)) * 2; // byte offset, 4-aligned
	size_t const poolTo   = Align4(poolBytes);
	bool const shadeChanged = m_shadeCopy.empty() || std::memcmp(m_shadeCopy.data(), shadeTable, 65536 * 2) != 0;

	struct Part { Buffer* dst; void const* src; size_t bytes; size_t dstOffset; };
	std::vector<float> lightData;
	lightData.reserve(f.lighting.points.size() * 8);
	for (WorldPipe::PointLight const& p : f.lighting.points)
		lightData.insert(lightData.end(), { p.x, p.y, p.radius, p.intensity, p.r, p.g, p.b, 0.0f });
	std::vector<Part> parts = {
		{ &m_instances, f.instances.data(), f.instances.size() * sizeof(WorldPipe::Instance), 0 },
		{ &m_ranges,    m_bins.ranges.data(), m_bins.ranges.size() * 4, 0 },
		{ &m_items,     m_bins.items.data(),  m_bins.items.size() * 4, 0 },
		{ &m_palettes,  f.palettes.data(),    f.palettes.size() * 4, 0 },
		{ &m_columns,   f.columns.data(),     f.columns.size() * 2, 0 },
	};
	for (Part const& p : parts)
	{
		if (!Ensure(*p.dst, uint32_t(p.bytes), ro)) return false;
	}
	if (poolTo > poolFrom) parts.push_back({ &m_pixels, reinterpret_cast<uint8_t const*>(pool.pixels.data()) + poolFrom, poolTo - poolFrom, poolFrom });
	if (!Ensure(m_shade, 65536 * 2, ro)) return false;
	if (shadeChanged) parts.push_back({ &m_shade, shadeTable, 65536 * 2, 0 });
	if (!Ensure(m_lights, uint32_t(lightData.size() * 4), ro)) return false;
	if (!lightData.empty()) parts.push_back({ &m_lights, lightData.data(), lightData.size() * 4, 0 });
	if (!Ensure(m_out, uint32_t(size_t(f.width) * f.height * 4), SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE)) return false;

	size_t total = 0;
	for (Part const& p : parts) total += Align4(p.bytes);
	if (total > m_uploadSize)
	{
		if (m_upload) SDL_ReleaseGPUTransferBuffer(m_device, m_upload);
		SDL_GPUTransferBufferCreateInfo ti{};
		ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		ti.size = uint32_t(std::max<size_t>(total + total / 2, 1u << 20));
		m_upload = SDL_CreateGPUTransferBuffer(m_device, &ti);
		m_uploadSize = m_upload ? ti.size : 0;
		if (!m_upload)
		{
			m_error = std::string("creating the upload buffer failed: ") + SDL_GetError();
			return false;
		}
	}
	auto* map = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(m_device, m_upload, true));
	if (!map) return false;
	size_t off = 0;
	std::vector<size_t> offsets;
	for (Part const& p : parts)
	{
		offsets.push_back(off);
		if (p.bytes)
		{
			size_t const avail = p.dst == &m_pixels ? std::min(p.bytes, poolBytes - poolFrom) : p.bytes;
			std::memcpy(map + off, p.src, avail);
			if (avail < Align4(p.bytes)) std::memset(map + off + avail, 0, Align4(p.bytes) - avail);
		}
		off += Align4(p.bytes);
	}
	SDL_UnmapGPUTransferBuffer(m_device, m_upload);
	pool.uploaded = pool.pixels.size();
	if (shadeChanged) m_shadeCopy.assign(shadeTable, shadeTable + 65536);
	m_stats.uploadBytes = total;

	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
	if (!cmd)
	{
		m_error = std::string("no command buffer: ") + SDL_GetError();
		return false;
	}
	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
	for (size_t i = 0; i < parts.size(); ++i)
	{
		if (!parts[i].bytes) continue;
		SDL_GPUTransferBufferLocation src{ m_upload, uint32_t(offsets[i]) };
		SDL_GPUBufferRegion dst{ parts[i].dst->buf, uint32_t(parts[i].dstOffset), Align4(parts[i].bytes) };
		SDL_UploadToGPUBuffer(copy, &src, &dst, false);
	}
	SDL_EndGPUCopyPass(copy);

	SDL_GPUStorageTextureReadWriteBinding tex{};
	tex.texture = m_texture;
	SDL_GPUStorageBufferReadWriteBinding out{};
	out.buffer = m_out.buf;
	SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, &tex, 1, &out, 1);
	SDL_BindGPUComputePipeline(pass, m_pipeline);
	SDL_GPUBuffer* ros[8] = { m_instances.buf, m_ranges.buf, m_items.buf, m_pixels.buf, m_palettes.buf, m_columns.buf, m_shade.buf, m_lights.buf };
	SDL_BindGPUComputeStorageBuffers(pass, 0, ros, 8);
	Params const params{ uint32_t(f.width), uint32_t(f.height), uint32_t(m_bins.cols), f.clearColor, f.clearDepth, f.translucentMask,
		uint32_t(f.lighting.points.size()), 0,
		f.lighting.ambientR, f.lighting.ambientG, f.lighting.ambientB,
		f.lighting.sunR, f.lighting.sunG, f.lighting.sunB, f.lighting.falloff, 0.0f };
	SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));
	SDL_DispatchGPUCompute(pass, uint32_t((f.width + 15) / 16), uint32_t((f.height + 15) / 16), 1);
	SDL_EndGPUComputePass(pass);

	if (m_fence)
	{
		SDL_ReleaseGPUFence(m_device, m_fence);
		m_fence = nullptr;
	}
	if (readback)
	{
		uint32_t const bytes = uint32_t(size_t(f.width) * f.height * 4);
		if (bytes > m_downloadSize)
		{
			if (m_download) SDL_ReleaseGPUTransferBuffer(m_device, m_download);
			SDL_GPUTransferBufferCreateInfo ti{};
			ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
			ti.size = bytes;
			m_download = SDL_CreateGPUTransferBuffer(m_device, &ti);
			m_downloadSize = m_download ? bytes : 0;
		}
		if (m_download)
		{
			SDL_GPUCopyPass* dl = SDL_BeginGPUCopyPass(cmd);
			SDL_GPUBufferRegion src{ m_out.buf, 0, bytes };
			SDL_GPUTransferBufferLocation dst{ m_download, 0 };
			SDL_DownloadFromGPUBuffer(dl, &src, &dst);
			SDL_EndGPUCopyPass(dl);
		}
		m_fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
	}
	else
	{
		SDL_SubmitGPUCommandBuffer(cmd);
	}
	m_stats.uploadMs = Ms(t1, clock_type::now());
	return true;
}

bool Renderer::Read565(std::vector<uint16_t>& out)
{
	if (!m_fence || !m_download) return false;
	auto const t0 = clock_type::now();
	SDL_WaitForGPUFences(m_device, true, &m_fence, 1);
	m_stats.waitMs = Ms(t0, clock_type::now());
	SDL_ReleaseGPUFence(m_device, m_fence);
	m_fence = nullptr;
	auto const* p = static_cast<uint32_t const*>(SDL_MapGPUTransferBuffer(m_device, m_download, false));
	if (!p) return false;
	size_t const n = size_t(m_w) * m_h;
	out.resize(n);
	for (size_t i = 0; i < n; ++i) out[i] = uint16_t(p[i]);
	SDL_UnmapGPUTransferBuffer(m_device, m_download);
	return true;
}

} // namespace WorldGpu
