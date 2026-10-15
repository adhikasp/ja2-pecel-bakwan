#pragma once
// The world renderer on SDL_GPU (docs/plan/native-modern-game.md, Phase 8): runs the pipeline of
// WorldPipeline.h in a compute shader (src/sgp/shaders/world_raster.comp) and leaves the picture in an RGBA8
// texture that the presentation draws as the world layer, or reads back as RGB565 (headless, screenshots and the
// equivalence tests).
//
// The shader ships as SPIR-V (Vulkan), DXBC SM 5.1 (D3D12) and MSL (Metal), all made from the one GLSL source by
// tools/shaders/build.sh. Where no device works, Init fails and the game keeps the software world.

#include "WorldPipeline.h"

#include <SDL3/SDL.h>
#include <string>
#include <vector>

namespace WorldGpu {

struct Stats
{
	double buildMs  = 0;   // binning (CPU)
	double uploadMs = 0;   // copying into the transfer buffers and recording the frame (CPU)
	double waitMs   = 0;   // waiting for the GPU (only when read back)
	size_t binItems = 0;
	size_t uploadBytes = 0;
	bool skipped = false; // the frame was the one already in the target: no binning, upload or dispatch
	int dirtyX = 0, dirtyY = 0, dirtyW = 0, dirtyH = 0; // the region that was dispatched (the whole target, whole)
};

/** The target pixels a dispatch covers: [x0, x1) x [y0, y1). x0/y0 are 16-aligned (a dispatch is a grid of
 * 16x16 bins); empty means nothing changed and there is nothing to dispatch. */
struct Region
{
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	bool Empty() const { return x1 <= x0 || y1 <= y0; }
	size_t Pixels() const { return Empty() ? 0 : size_t(x1 - x0) * size_t(y1 - y0); }
};

/** The shader formats there is code for (for SDL_CreateGPUDevice). */
SDL_GPUShaderFormat ShaderFormats();

class Renderer
{
public:
	~Renderer();

	/** On `device` (shared with SDL's GPU renderer) or, when null, on a device of its own without a window. */
	bool Init(SDL_GPUDevice* device);
	void Shutdown();
	bool Ready() const { return m_pipeline != nullptr; }
	SDL_GPUDevice* Device() const { return m_device; }
	std::string const& Error() const { return m_error; }
	std::string DriverName() const;

	/** Rasterizes the frame. With readback the 565 result is kept for Read565 (which then waits for it), and the
	 * whole target is always dispatched (the readback sees every pixel). Without readback only the region the
	 * frame changed since the last one is dispatched - the target already holds the rest; a frame that changed
	 * nothing at all is not dispatched (skipped). */
	bool Render(WorldPipe::Frame const&, WorldPipe::SpritePool&, uint16_t const* shadeTable, bool readback);
	/** The last frame read back (Render with readback), width * height pixels. Waits for the GPU. */
	bool Read565(std::vector<uint16_t>& out);

	SDL_GPUTexture* Texture() const { return m_texture; }
	int Width() const { return m_w; }
	int Height() const { return m_h; }
	Stats const& LastStats() const { return m_stats; }

private:
	struct Buffer { SDL_GPUBuffer* buf = nullptr; uint32_t size = 0; };
	bool Ensure(Buffer&, uint32_t bytes, SDL_GPUBufferUsageFlags usage);
	bool EnsureTarget(int w, int h);
	/** The target pixels the frame changed since the last dispatch, and so the region to dispatch. Empty when
	 * nothing changed at all; the whole target when something outside the instances did (see Render). */
	Region DirtyRegion(WorldPipe::Frame const&, uint32_t poolGeneration, uint16_t const* shadeTable) const;
	void RememberFrame(WorldPipe::Frame const&, uint32_t poolGeneration, uint16_t const* shadeTable);
	SDL_GPUDevice*          m_device = nullptr;
	bool                    m_ownDevice = false;
	SDL_GPUComputePipeline* m_pipeline = nullptr;
	SDL_GPUTexture*         m_texture = nullptr;
	int                     m_w = 0, m_h = 0;
	Buffer m_instances, m_ranges, m_items, m_pixels, m_palettes, m_columns, m_shade, m_lights, m_out;
	SDL_GPUTransferBuffer*  m_upload = nullptr;
	uint32_t                m_uploadSize = 0;
	SDL_GPUTransferBuffer*  m_download = nullptr;
	uint32_t                m_downloadSize = 0;
	SDL_GPUFence*           m_fence = nullptr;
	uint32_t                m_poolGeneration = 0;
	std::vector<uint16_t>   m_shadeCopy;
	WorldPipe::Bins         m_bins;
	Stats                   m_stats;
	std::string             m_error;

	// What the target holds, to recognise a frame that does not need dispatching: the instance list, the depth
	// runs and palettes it indexes, the frame's clear and lighting, and the pool generation behind its sprites.
	struct FrameKey
	{
		bool valid = false;
		int width = 0, height = 0;
		uint32_t clearColor = 0;
		uint16_t clearDepth = 0;
		uint32_t mask = 0;
		uint32_t poolGeneration = 0;
		uint32_t points = 0;
		float ambientR = 0, ambientG = 0, ambientB = 0, sunR = 0, sunG = 0, sunB = 0, falloff = 0;
		std::vector<WorldPipe::Instance> instances;
		std::vector<uint32_t> palettes;
		std::vector<uint16_t> columns;
		std::vector<WorldPipe::PointLight> lights;
		std::vector<uint16_t> shade; // the 65536-entry shade table the ops darken through
	};
	FrameKey m_last;
};

} // namespace WorldGpu
