#pragma once
// The game's SDL_GPU presentation (docs/plan/native-modern-game.md, Phase 2 follow-up): one compositor and one
// present path for everything the player sees. The legacy 8-bit frame buffer, the Phase 8 world texture and the
// native UI (src/nativeui/UiGpu.cc, through the same graphics pipeline) are all drawn as triangles of
// VideoGpu::Vertex into one render pass on the swapchain, which SDL_GPU presents directly. There is no
// SDL_Renderer when this is active.
//
// The vertex and fragment shaders are committed in src/sgp/shaders/quad.h (tools/shaders/build.sh); the pipeline
// is built for the swapchain's format and premultiplied-alpha blending.

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace VideoGpu
{
	/** What the native UI's GPU render interface needs to draw its own triangles in the compositor's pass. */
	struct Vertex
	{
		float   x, y;       // NDC: (-1,-1) lower-left .. (1,1) upper-right
		float   u, v;       // texture coordinates
		uint8_t r, g, b, a; // premultiplied
	};
	static_assert(sizeof(Vertex) == 20, "the quad pipeline's vertex layout");

	struct Resources
	{
		SDL_GPUDevice*           device = nullptr;
		SDL_GPUGraphicsPipeline* pipeline = nullptr;
		SDL_GPUSampler*          samplerNearest = nullptr;
		SDL_GPUSampler*          samplerLinear = nullptr;
		SDL_GPUTexture*          white = nullptr; // 1x1 opaque white for untextured geometry
	};

	/** Claims @a window for @a device and builds the pipeline. False leaves the SDL_Renderer path in place. */
	bool Init(SDL_GPUDevice* device, SDL_Window* window);
	void Shutdown();
	bool Active();
	std::string const& Error();
	Resources const& GetResources();

	/** Begins a frame: acquires a command buffer and the swapchain texture. False when the window is not ready
	 * (minimised, too many frames in flight): the frame is skipped. */
	bool BeginFrame();
	SDL_GPUCommandBuffer* Cmd();
	SDL_GPURenderPass* Pass();
	int Width();
	int Height();
	/** Whether the swapchain is a B8G8R8A8 format (the read-back path needs to know). */
	bool SwapchainIsBgra();

	/** Uploads w x h RGBA8 pixels into a texture owned by VideoGpu (created or resized as needed; the handle is
	 * kept in @a slot). Returns null on failure. The return value is valid until the next resize of the slot. */
	SDL_GPUTexture* UploadRgba(SDL_GPUTexture*& slot, int& slotW, int& slotH, void const* pixels, int w, int h);

	/** Copies @a src into a managed texture used only for sampling. Some backends (D3D12) need the explicit
	 * transition a copy performs before a texture another pass wrote as storage can be sampled. Returns the
	 * copy, or @a src when the copy cannot be made (the caller then samples @a src directly). */
	SDL_GPUTexture* CopyToSampled(SDL_GPUTexture* src, int w, int h, SDL_GPUTexture*& slot, int& slotW, int& slotH);

	/* The compositor's layers for this frame: a textured quad in window pixels, UVs normalized. */
	void QuadReset();
	void Quad(SDL_GPUTexture* tex, float dx, float dy, float dw, float dh,
	          float u0, float v0, float u1, float v1,
	          float r, float g, float b, float a, bool nearest);
	/** Uploads the quads recorded by Quad() (their own copy pass, before the render pass). */
	bool QuadUpload();

	/** Opens the render pass (clear to the letterbox colour). @a capture renders it offscreen instead of straight
	 * to the swapchain, so it can be read back; Submit() then still presents it. */
	void BeginPass(float cr, float cg, float cb, bool capture);
	/** Draws the quads recorded by Quad() into the open pass. */
	void QuadDraw();
	void EndPass();

	/** Submits the frame. When it was rendered for capture, waits for the GPU and reads it back. */
	bool Submit();

	/** The captured frame (window pixels, RGB24), or empty when nothing was captured. */
	int CaptureWidth();
	int CaptureHeight();
	std::vector<uint8_t> const& CaptureRgb();
	void ClearCapture();
}
