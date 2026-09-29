// Render path spike: SDL_Renderer (software / each GPU driver, incl. SDL's "gpu" driver built on SDL_GPU) vs
// raw SDL_GPU, for the UI + world compositor, measured offscreen (render targets + readback, which is also how
// headless screenshots would work on the GPU).
#include "RenderPathSpike.h"
#include "UiSpike.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace spike {
namespace {

using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

/** A 1024x1024 atlas of 32x32 cells with an opaque diamond each ("tiles"), alpha outside. */
SDL_Surface* MakeAtlas()
{
	SDL_Surface* s = SDL_CreateSurface(1024, 1024, SDL_PIXELFORMAT_ARGB8888);
	for (int y = 0; y < 1024; ++y)
	{
		auto* row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(s->pixels) + y * s->pitch);
		for (int x = 0; x < 1024; ++x)
		{
			int const cx = x % 32 - 16, cy = y % 32 - 16, cell = (x / 32) + (y / 32) * 32;
			bool const in = std::abs(cx) + 2 * std::abs(cy) < 16;
			Uint32 const rgb = (Uint32(40 + cell * 7 % 200) << 16) | (Uint32(60 + cell * 13 % 180) << 8) | Uint32(50 + cell * 3 % 150);
			row[x] = in ? (0xFF000000u | rgb) : 0;
		}
	}
	return s;
}

/** World-like load: n sprites from the atlas, one textured quad each, all in one SDL_RenderGeometry batch
 * (what a tile renderer would do), plus the same as n separate SDL_RenderTexture calls. */
struct SpriteLoad
{
	std::vector<SDL_Vertex> verts;
	std::vector<int>        idx;
	std::vector<std::pair<SDL_FRect, SDL_FRect>> quads;
	SpriteLoad(int n, int w, int h)
	{
		Uint32 seed = 12345;
		auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
		for (int i = 0; i < n; ++i)
		{
			float const x = float(rnd() % (w - 32)), y = float(rnd() % (h - 32));
			int const cell = int(rnd() % 1024);
			float const u = (cell % 32) / 32.f, v = (cell / 32) / 32.f, d = 1 / 32.f;
			SDL_FColor const c{ 1, 1, 1, 1 };
			int const base = int(verts.size());
			verts.push_back({ { x, y }, c, { u, v } });
			verts.push_back({ { x + 32, y }, c, { u + d, v } });
			verts.push_back({ { x + 32, y + 32 }, c, { u + d, v + d } });
			verts.push_back({ { x, y + 32 }, c, { u, v + d } });
			idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
			quads.push_back({ { (cell % 32) * 32.f, (cell / 32) * 32.f, 32, 32 }, { x, y, 32, 32 } });
		}
	}
};

void Sync(SDL_Renderer* r)
{
	// Reading back one pixel waits for the GPU to finish everything queued before it.
	SDL_Rect const px{ 0, 0, 1, 1 };
	if (SDL_Surface* s = SDL_RenderReadPixels(r, &px)) SDL_DestroySurface(s);
}

RenderPathRow MeasureRenderer(char const* driver, int w, int h, int frames)
{
	RenderPathRow row;
	row.backend = std::string("SDL_Renderer/") + driver;
	row.width = w;
	row.height = h;
	SDL_Window*   win = nullptr;
	SDL_Surface*  surf = nullptr;
	SDL_Renderer* r = nullptr;
	if (std::string(driver) == "software")
	{
		surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
		r = SDL_CreateSoftwareRenderer(surf);
	}
	else
	{
		win = SDL_CreateWindow("render path spike", 64, 64, SDL_WINDOW_HIDDEN);
		if (win) r = SDL_CreateRenderer(win, driver);
	}
	if (!r)
	{
		row.error = SDL_GetError();
		if (win) SDL_DestroyWindow(win);
		if (surf) SDL_DestroySurface(surf);
		return row;
	}
	SDL_Texture* target = nullptr;
	if (win)
	{
		target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
		if (!target) { row.error = SDL_GetError(); SDL_DestroyRenderer(r); SDL_DestroyWindow(win); return row; }
		SDL_SetRenderTarget(r, target);
	}

	SDL_Surface* atlasS = MakeAtlas();
	SDL_Texture* atlas = SDL_CreateTextureFromSurface(r, atlasS);
	SDL_DestroySurface(atlasS);
	SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
	SpriteLoad const load(20000, w, h);

	auto time = [&](auto&& draw) {
		draw();
		Sync(r); // warm up
		auto const t0 = Clock::now();
		for (int i = 0; i < frames; ++i) { draw(); Sync(r); }
		return ms(t0, Clock::now()) / frames;
	};
	auto clear = [&] { SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderClear(r); };

	row.spritesBatchedMs = time([&] {
		clear();
		SDL_RenderGeometry(r, atlas, load.verts.data(), int(load.verts.size()), load.idx.data(), int(load.idx.size()));
	});
	row.spritesCallsMs = time([&] {
		clear();
		for (auto const& [src, dst] : load.quads) SDL_RenderTexture(r, atlas, &src, &dst);
	});
	for (char const* kind : { "rml", "inhouse" })
	{
		try
		{
			auto screen = CreateScreen(kind, r, SaveListModel::Fake());
			screen->setSize(w, h);
			ApplyState(*screen, "hover");
			double const v = time([&] { screen->update(1.0 / 60); screen->render(); });
			(std::string(kind) == "rml" ? row.uiRmlMs : row.uiInhouseMs) = v;
		}
		catch (std::exception const& e)
		{
			row.error += std::string(kind) + ": " + e.what() + "; ";
		}
	}
	// Full-frame readback: the cost of a headless screenshot on this path.
	{
		auto const t0 = Clock::now();
		for (int i = 0; i < 3; ++i) if (SDL_Surface* s = SDL_RenderReadPixels(r, nullptr)) SDL_DestroySurface(s);
		row.readbackMs = ms(t0, Clock::now()) / 3;
	}
	SDL_DestroyTexture(atlas);
	if (target) SDL_DestroyTexture(target);
	SDL_DestroyRenderer(r);
	if (win) SDL_DestroyWindow(win);
	if (surf) SDL_DestroySurface(surf);
	return row;
}

/** Raw SDL_GPU with no window at all: a claimed-by-nobody device, an offscreen colour target, sprites as
 * SDL_BlitGPUTexture (no shaders needed for the spike) and a download through a transfer buffer. */
RenderPathRow MeasureSdlGpu(int w, int h, int sprites, int frames)
{
	RenderPathRow row;
	row.backend = "SDL_GPU (raw, windowless)";
	row.width = w;
	row.height = h;
	SDL_GPUDevice* dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
		SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
	if (!dev) { row.error = SDL_GetError(); return row; }
	row.backend += std::string(" ") + SDL_GetGPUDeviceDriver(dev);

	SDL_GPUTextureCreateInfo ti{};
	ti.type = SDL_GPU_TEXTURETYPE_2D;
	ti.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
	ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	ti.width = Uint32(w);
	ti.height = Uint32(h);
	ti.layer_count_or_depth = 1;
	ti.num_levels = 1;
	SDL_GPUTexture* target = SDL_CreateGPUTexture(dev, &ti);
	ti.width = ti.height = 1024;
	ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	SDL_GPUTexture* atlas = SDL_CreateGPUTexture(dev, &ti);
	if (!target || !atlas) { row.error = SDL_GetError(); SDL_DestroyGPUDevice(dev); return row; }

	// Upload the atlas
	{
		SDL_Surface* a = MakeAtlas();
		SDL_GPUTransferBufferCreateInfo tb{ SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, Uint32(1024 * 1024 * 4), 0 };
		SDL_GPUTransferBuffer* up = SDL_CreateGPUTransferBuffer(dev, &tb);
		void* p = SDL_MapGPUTransferBuffer(dev, up, false);
		SDL_memcpy(p, a->pixels, 1024 * 1024 * 4);
		SDL_UnmapGPUTransferBuffer(dev, up);
		SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(dev);
		SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
		SDL_GPUTextureTransferInfo src{ up, 0, 1024, 1024 };
		SDL_GPUTextureRegion dst{};
		dst.texture = atlas; dst.w = 1024; dst.h = 1024; dst.d = 1;
		SDL_UploadToGPUTexture(cp, &src, &dst, false);
		SDL_EndGPUCopyPass(cp);
		SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
		SDL_WaitForGPUFences(dev, true, &f, 1);
		SDL_ReleaseGPUFence(dev, f);
		SDL_ReleaseGPUTransferBuffer(dev, up);
		SDL_DestroySurface(a);
	}
	SpriteLoad const load(sprites, w, h);
	SDL_GPUTransferBufferCreateInfo db{ SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, Uint32(w * h * 4), 0 };
	SDL_GPUTransferBuffer* down = SDL_CreateGPUTransferBuffer(dev, &db);

	auto frame = [&](bool download) {
		SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(dev);
		SDL_GPUColorTargetInfo ct{};
		ct.texture = target;
		ct.load_op = SDL_GPU_LOADOP_CLEAR;
		ct.store_op = SDL_GPU_STOREOP_STORE;
		ct.clear_color = { 0, 0, 0, 1 };
		SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cb, &ct, 1, nullptr));
		for (auto const& [s, d] : load.quads)
		{
			SDL_GPUBlitInfo bi{};
			bi.source = { atlas, 0, 0, Uint32(s.x), Uint32(s.y), 32, 32 };
			bi.destination = { target, 0, 0, Uint32(d.x), Uint32(d.y), 32, 32 };
			bi.load_op = SDL_GPU_LOADOP_LOAD;
			bi.filter = SDL_GPU_FILTER_NEAREST;
			SDL_BlitGPUTexture(cb, &bi);
		}
		if (download)
		{
			SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
			SDL_GPUTextureRegion src{};
			src.texture = target; src.w = Uint32(w); src.h = Uint32(h); src.d = 1;
			SDL_GPUTextureTransferInfo dst{ down, 0, Uint32(w), Uint32(h) };
			SDL_DownloadFromGPUTexture(cp, &src, &dst);
			SDL_EndGPUCopyPass(cp);
		}
		SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
		SDL_WaitForGPUFences(dev, true, &f, 1);
		SDL_ReleaseGPUFence(dev, f);
	};
	frame(false);
	auto t0 = Clock::now();
	for (int i = 0; i < frames; ++i) frame(false);
	row.spritesCallsMs = ms(t0, Clock::now()) / frames; // one blit per sprite: no batching without shaders
	t0 = Clock::now();
	for (int i = 0; i < 3; ++i) frame(true);
	row.readbackMs = ms(t0, Clock::now()) / 3 - row.spritesCallsMs;
	row.note = "sprites as SDL_BlitGPUTexture (no pipeline/shaders); " + std::to_string(sprites) + " sprites";

	SDL_ReleaseGPUTransferBuffer(dev, down);
	SDL_ReleaseGPUTexture(dev, atlas);
	SDL_ReleaseGPUTexture(dev, target);
	SDL_DestroyGPUDevice(dev);
	return row;
}

} // namespace

std::vector<RenderPathRow> RunRenderPathSpike(bool quick, bool gpu)
{
	std::vector<RenderPathRow> rows;
	std::vector<std::pair<int, int>> sizes{ { 1920, 1080 }, { 3840, 2160 } };
	int const frames = quick ? 2 : 10;
	std::vector<std::string> drivers{ "software" };
	if (gpu)
	{
		for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i)
		{
			std::string const d = SDL_GetRenderDriver(i);
			if (d != "software") drivers.push_back(d);
		}
	}
	for (auto const& [w, h] : sizes)
	{
		for (auto const& d : drivers) rows.push_back(MeasureRenderer(d.c_str(), w, h, frames));
		if (gpu) rows.push_back(MeasureSdlGpu(w, h, 20000, frames));
	}
	return rows;
}

std::string RenderPathTable(std::vector<RenderPathRow> const& rows)
{
	std::ostringstream o;
	char buf[512];
	o << "| Backend | Size | 20k sprites, 1 batch (ms) | 20k sprites, 1 call each (ms) | UI RmlUi (ms) | UI in-house (ms) | Full readback (ms) | Note |\n";
	o << "|---|---|---|---|---|---|---|---|\n";
	auto f = [](double v) { if (v < 0) return std::string("-"); char b[32]; snprintf(b, sizeof b, "%.2f", v); return std::string(b); };
	for (auto const& r : rows)
	{
		snprintf(buf, sizeof buf, "| %s | %dx%d | %s | %s | %s | %s | %s | %s |\n", r.backend.c_str(), r.width, r.height,
			f(r.spritesBatchedMs).c_str(), f(r.spritesCallsMs).c_str(), f(r.uiRmlMs).c_str(), f(r.uiInhouseMs).c_str(),
			f(r.readbackMs).c_str(), (r.error.empty() ? r.note : "error: " + r.error).c_str());
		o << buf;
	}
	return o.str();
}

} // namespace spike
