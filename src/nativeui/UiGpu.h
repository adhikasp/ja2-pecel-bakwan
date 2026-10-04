#pragma once
// RmlUi through SDL_GPU (docs/plan/native-modern-game.md, Phase 2): the native UI's triangles go through the one
// graphics pipeline the SDL_GPU presentation built (src/sgp/VideoGpu.cc), so the UI is a part of the single
// compositor and present path instead of a second renderer. The host drives a frame with Prepare/Upload/Draw.
//
// This header is toolkit-level: it names SDL handles only, no game code. src/game/NativeUI fills GpuPipeline from
// VideoGpu::GetResources().

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <vector>

namespace nui
{

/** The pipeline, samplers and white texel VideoGpu created for the shared vertex layout (position, uv,
 * premultiplied colour). */
struct GpuPipeline
{
	SDL_GPUDevice*           device = nullptr;
	SDL_GPUGraphicsPipeline* pipeline = nullptr;
	SDL_GPUSampler*          nearest = nullptr;
	SDL_GPUSampler*          linear = nullptr;
	SDL_GPUTexture*          white = nullptr;
};

/** RmlUi's render interface on SDL_GPU. Triangles are recorded on the CPU, uploaded in one copy pass and drawn
 * through the shared pipeline with premultiplied-alpha blending, in window pixels. */
class SdlGpuRenderInterface : public Rml::RenderInterface
{
public:
	explicit SdlGpuRenderInterface(GpuPipeline const& pipe);
	~SdlGpuRenderInterface() override;

	/** Starts recording for a @a w x @a h pixel target (positions are converted to NDC there). */
	void Prepare(int w, int h);
	/** Uploads the recorded triangles on @a cmd (a copy pass of its own). False if there is nothing to draw. */
	bool Upload(SDL_GPUCommandBuffer* cmd);
	/** Draws the recorded triangles into @a pass. */
	void Draw(SDL_GPURenderPass* pass);

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle g) override;
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f t, Rml::TextureHandle texture) override;
	Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> src, Rml::Vector2i dim) override;
	void ReleaseTexture(Rml::TextureHandle t) override;
	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei r) override;

private:
	struct Geometry { Rml::Span<const Rml::Vertex> vertices; Rml::Span<const int> indices; };
	struct Texture { SDL_GPUTexture* gpu = nullptr; SDL_GPUSampler* sampler = nullptr; };
	struct Batch
	{
		uint32_t   firstIndex = 0;
		uint32_t   indexCount = 0;
		Texture*   texture = nullptr;
		bool       scissor = false;
		SDL_Rect   rect{ 0, 0, 0, 0 };
	};

	Texture* MakeTexture(unsigned char const* rgba, int w, int h, bool repeat, bool nearest, bool premultiplied);
	void     Release(Texture*);
	bool     EnsureBuffer(SDL_GPUBuffer*& buf, uint32_t& size, uint32_t bytes, SDL_GPUBufferUsageFlags usage);

	GpuPipeline             m_pipe;
	int                     m_w = 0, m_h = 0;
	std::vector<uint8_t>    m_verts;    // the shared vertex layout: 20 bytes each
	std::vector<uint32_t>   m_indices;
	std::vector<Batch>      m_draws;
	std::vector<Geometry*>  m_geometries;
	std::vector<Texture*>   m_textures;
	SDL_GPUBuffer*          m_vertBuf = nullptr;
	uint32_t                m_vertBufSize = 0;
	SDL_GPUBuffer*          m_indexBuf = nullptr;
	uint32_t                m_indexBufSize = 0;
	SDL_GPUTransferBuffer*  m_upload = nullptr;
	uint32_t                m_uploadSize = 0;
	bool                    m_scissorOn = false;
	SDL_Rect                m_scissor{ 0, 0, 0, 0 };
};

} // namespace nui
