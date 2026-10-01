/* ---- Phase 8 world renderer (WorldRender.h) --------------------------------------------------------------------
 * Included at the end of RenderWorld.cc: it needs RenderTiles and that file's render parameters. The recorded
 * renderers run the same passes a full redraw of the software renderer runs (RenderStaticWorld, then the drawing
 * passes of RenderDynamicWorld), every frame, with RenderTiles recording instead of blitting. */
#include "WorldRender.h"
#include "WorldGpu.h"
#include "Automation.h"
#include "Headless.h"
#include "Interface_Control.h"
#include "stb_image_write.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {

WorldRendererKind   gWorldRequested = WorldRendererKind::Software;
WorldRendererKind   gWorldActive    = WorldRendererKind::Software;
bool                gWorldStarted   = false;  // the requested renderer was tried
std::string         gWorldError;
WorldPipe::SpritePool gWorldPool;
WorldPipe::Frame    gWorldFrame;
WorldPipe::Target   gWorldTarget;
WorldRenderStats    gWorldStats;
WorldGpu::Renderer* gWorldGpu = nullptr;

using wr_clock = std::chrono::steady_clock;
double WrMs(wr_clock::time_point a, wr_clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

/** Byte i of a ZStripInfo's change list as the legacy strip blitters read it: past the 16 changes they run on
 * into the next fields of the struct. */
int StripChange(ZStripInfo const& zi, int i)
{
	if (i < int(std::size(zi.pbZChange))) return zi.pbZChange[i];
	switch (i - int(std::size(zi.pbZChange)))
	{
		case 0:  return zi.bInitialZChange;
		case 1:  return INT8(zi.ubFirstZStripWidth);
		case 2:  return INT8(zi.ubNumberOfZChanges);
		default: return 0;
	}
}

void RecordBlit(WorldPipe::Op const op, ClipInfo const& ci, UINT16 const* palette, UINT16 const z, UINT16 const outline,
	Strips const strips, INT16 const zIndex)
{
	WorldRecorder& r = *gWorldRecorder;
	if (ci.status == ClipInfo::Status::Completely_Clipped || ci.blitLength <= 0 || ci.blitHeight <= 0) return;

	WorldPipe::Instance in{};
	in.x0 = UINT16(ci.iTempX + ci.leftSkip);
	in.y0 = UINT16(ci.iTempY + ci.topSkip);
	in.x1 = UINT16(in.x0 + ci.blitLength);
	in.y1 = UINT16(in.y0 + ci.blitHeight);
	in.ox = INT16(ci.iTempX);
	in.oy = INT16(ci.iTempY);
	in.op = op;
	in.z = z;
	in.outline = outline;
	in.columns = WorldPipe::NO_COLUMNS;

	ETRLEObject const& e = ci.vobject->SubregionProperties(ci.vobjectIndex);
	WorldPipe::SpritePool::Entry const& sp = r.pool->Get(ci.vobject->PixData(e), e.uiDataLength, e.usWidth, e.usHeight);
	in.sprite = sp.offset;
	in.spriteW = sp.w;

	if (strips != Strips::None)
	{
		// The depths exactly as the strip blitters step through them, including their quirks
		auto const& all = ci.vobject->ppZStripInfo;
		UINT16 const which = strips == Strips::MultiZ ? ci.vobjectIndex : UINT16(zIndex);
		if (!all || !all[which]) return; // the legacy blitters draw nothing either
		ZStripInfo const& zi = *all[which];
		int const first = zi.ubFirstZStripWidth;
		int const ls = ci.leftSkip;
		std::vector<UINT16>& out = r.frame->columns;
		in.columns = UINT32(out.size());
		if (strips == Strips::MultiZ)
		{
			// MultiZBlitter: one depth per column
			int level = INT16(z) + zi.bInitialZChange * Z_STRIP_DELTA_Y;
			int cols = ls > first ? HALF_TILE - (ls - first) % HALF_TILE : ls < first ? first - ls : HALF_TILE;
			int index = 0;
			if (ls >= first)
			{
				index = 1 + (ls - first) / HALF_TILE;
				int sum = 0;
				for (int i = 0; i < index; ++i) sum += StripChange(zi, i);
				level += Z_STRIP_DELTA_Y * sum;
			}
			for (int c = 0; c < ci.blitLength; ++c)
			{
				out.push_back(UINT16(level));
				if (--cols == 0)
				{
					cols = HALF_TILE;
					level += StripChange(zi, index++) * Z_STRIP_DELTA_Y;
				}
			}
		}
		else
		{
			// BltTransZTransShadowInc(Obscure): its own start (left-clip steps of Z_SUBLAYERS)...
			UINT16 start = UINT16(INT16(z) + zi.bInitialZChange * Z_SUBLAYERS * 10);
			int const startCols = ls > first ? 20 - (ls - first) % 20 : ls < first ? first - ls : 20;
			int startIndex = 0;
			if (ls >= startCols)
			{
				startIndex = UINT16(1 + (ls - first) / 20);
				for (int i = 0; i < startIndex; ++i)
				{
					int const d = StripChange(zi, i);
					if (d == -1) start -= Z_SUBLAYERS;
					else if (d == 1) start += Z_SUBLAYERS;
				}
			}
			// ...and the non-obscured one steps by Z_SUBLAYERS over opaque pixels but Z_STRIP_DELTA_Y over transparent
			// runs, so its depth depends on the row: one depth per pixel then.
			bool const perPixel = op == WorldPipe::Op::TransShadowZInc;
			int const rows = perPixel ? ci.blitHeight : 1;
			for (int row = 0; row < rows; ++row)
			{
				UINT16 level = start;
				int cols = startCols, index = startIndex;
				UINT16 const* const src = &r.pool->pixels[sp.offset + size_t(ci.topSkip + row) * sp.w];
				for (int c = 0; c < ci.blitLength; ++c)
				{
					bool const opaque = src[ls + c] & 0x100;
					out.push_back(level);
					if (--cols == 0)
					{
						cols = 20;
						int const d = StripChange(zi, index++);
						int const step = perPixel && opaque ? Z_SUBLAYERS : Z_STRIP_DELTA_Y;
						if (d < 0) level -= step;
						else if (d > 0) level += step;
					}
				}
			}
			// The obscured one starts its checkerboard from the unclipped top row
			in.parity = UINT8((ci.topSkip & 1) | (perPixel ? WorldPipe::PER_PIXEL_DEPTH : 0));
		}
		static bool const check = std::getenv("JA2_WORLD_STRIP_CHECK") != nullptr;
		if (check)
		{
			// Debugging aid: the legacy strip blitter and the pipeline's rule for this instance on the same random
			// colour and depth buffers; they must leave the same pixels
			int const W = WORLD_SCREEN_WIDTH, H = WORLD_SCREEN_HEIGHT;
			static std::vector<UINT16> c, zb, c2, zb2;
			static UINT32 seed = 1;
			c.resize(size_t(W) * H);
			zb.resize(size_t(W) * H);
			for (int y = in.y0; y < in.y1 && y < H; ++y)
				for (int x = in.x0; x < in.x1 && x < W; ++x)
				{
					size_t const i = size_t(y) * W + x;
					seed = seed * 1103515245u + 12345u;
					c[i] = UINT16(seed >> 8);
					zb[i] = UINT16(int(z) - 200 + int((seed >> 20) % 400));
				}
			c2 = c;
			zb2 = zb;
			switch (op)
			{
				case WorldPipe::Op::TransZInc:              BltTransZInc(ci, c.data(), W * 2, zb.data(), z); break;
				case WorldPipe::Op::TransZIncBurns:         BltTransZIncSameZBurnsThrough(ci, c.data(), W * 2, zb.data(), z); break;
				case WorldPipe::Op::TransZIncObscure:       BltTransZIncObscure(ci, c.data(), W * 2, zb.data(), z); break;
				case WorldPipe::Op::TransShadowZInc:        BltTransZTransShadowInc(ci, c.data(), W * 2, zb.data(), z, zIndex, palette); break;
				case WorldPipe::Op::TransShadowZIncObscure: BltTransZTransShadowIncObscure(ci, c.data(), W * 2, zb.data(), z, zIndex, palette); break;
				default: break;
			}
			UINT16 const* const pal = palette ? palette : ci.vobject->CurrentShade();
			int bad = 0;
			for (int y = in.y0; y < in.y1 && y < H; ++y)
				for (int x = in.x0; x < in.x1 && x < W; ++x)
				{
					size_t const i = size_t(y) * W + x;
					UINT16 const s = r.pool->pixels[in.sprite + size_t(y - in.oy) * in.spriteW + (x - in.ox)];
					if (s & 0x100) WorldPipe::ApplyPixel(in, s, x, y, WorldPipe::DepthAt(in, *r.frame, x, y), pal, ShadeTable, 0x7BEF, c2[i], zb2[i]);
					if (c[i] != c2[i] || zb[i] != zb2[i]) ++bad;
				}
			if (bad) SLOGW("strip check: {} px differ, op {}, z {} leftSkip {} topSkip {} first {}", bad, WorldPipe::OpName(op), z, ls, ci.topSkip, first);
		}
	}

	static UINT16 const noPalette[256] = {};
	in.palette = r.frame->Palette(palette ? palette : noPalette);
	r.frame->instances.push_back(in);
	++r.blits;
}

/** The passes of a full redraw: RenderStaticWorld, then what RenderDynamicWorld draws (not its dirty-rectangle pass).
 * CalcRenderParameters must have been called. */
void RenderWorldScene(bool const checkInteractive)
{
	{
		RenderTiles const S(gsLStartPointX_M, gsLStartPointY_M, gsLStartPointX_S, gsLStartPointY_S, gsLEndXS, gsLEndYS);
		S(TILES_NONE, RENDER_STATIC_LAND);
		S(TILES_NONE, RENDER_STATIC_OBJECTS);
		if (gRenderFlags & RENDER_FLAG_SHADOWS) S(TILES_NONE, RENDER_STATIC_SHADOWS);
		S(TILES_NONE, RENDER_STATIC_STRUCTS, RENDER_STATIC_ROOF, RENDER_STATIC_ONROOF, RENDER_STATIC_TOPMOST);
		S(TILES_OBSCURED, RENDER_STATIC_STRUCTS, RENDER_STATIC_ONROOF);
	}
	RenderTiles const D(gsStartPointX_M, gsStartPointY_M, gsStartPointX_S, gsStartPointY_S, gsEndXS, gsEndYS);
	D(TILES_OBSCURED, RENDER_STATIC_STRUCTS);
	D(TILES_NONE, RENDER_DYNAMIC_OBJECTS, RENDER_DYNAMIC_SHADOWS, RENDER_DYNAMIC_STRUCT_MERCS, RENDER_DYNAMIC_MERCS, RENDER_DYNAMIC_STRUCTS);
	D(TILES_NONE, RENDER_DYNAMIC_ROOF, RENDER_DYNAMIC_HIGHMERCS, RENDER_DYNAMIC_ONROOF);
	D(checkInteractive ? TILES_DYNAMIC_CHECKFOR_INT_TILE : TILES_NONE, RENDER_DYNAMIC_TOPMOST);
}

/** Records the current view into gWorldFrame. */
void RecordWorldFrame(bool const mutate, bool const checkInteractive)
{
	gWorldFrame.Clear(WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT);
	gWorldFrame.clearColor = 0;
	gWorldFrame.clearDepth = LAND_Z_LEVEL;
	gWorldFrame.translucentMask = UINT16(guiTranslucentMask);
	if (gWorldPool.pixels.size() > (size_t(96) << 20)) gWorldPool.Reset(); // 192 MB: start over
	WorldRecorder rec{ &gWorldFrame, &gWorldPool, mutate };
	ResetLayerOptimizing();
	gWorldRecorder = &rec;
	try
	{
		RenderWorldScene(checkInteractive);
	}
	catch (...)
	{
		gWorldRecorder = nullptr;
		throw;
	}
	gWorldRecorder = nullptr;
	for (LEVELNODE* n : rec.cleared) n->uiFlags |= LEVELNODE_LASTDYNAMIC;
	ResetLayerOptimizing();
}

void CopyToWorldBuffer(UINT16 const* px, int w, int h)
{
	SGPVSurface::Lock l(WORLD_BUFFER);
	UINT16* const dst = l.Buffer<UINT16>();
	int const pitch = int(l.Pitch() / 2);
	int const rows = std::min<int>(h, WORLD_BUFFER->Height());
	int const cols = std::min<int>(w, WORLD_BUFFER->Width());
	for (int y = 0; y < rows; ++y) std::memcpy(dst + size_t(y) * pitch, px + size_t(y) * w, size_t(cols) * 2);
}

void ReadWorldBuffer(std::vector<UINT16>& out, int w, int h)
{
	SGPVSurface::Lock l(WORLD_BUFFER);
	UINT16 const* const src = l.Buffer<UINT16>();
	int const pitch = int(l.Pitch() / 2);
	out.assign(size_t(w) * h, 0);
	for (int y = 0; y < h; ++y) std::memcpy(&out[size_t(y) * w], src + size_t(y) * pitch, size_t(w) * 2);
}

WorldGpu::Renderer* GpuRenderer(std::string& error)
{
	if (gWorldGpu && gWorldGpu->Ready()) return gWorldGpu;
	if (!gWorldGpu) gWorldGpu = new WorldGpu::Renderer;
	// Headless sessions only start SDL's events; a GPU device without a window still needs the video subsystem
	if (!VideoGpuDevice() && !SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO))
	{
		error = std::string("no video subsystem: ") + SDL_GetError();
		return nullptr;
	}
	// Shares the presentation's device when it has one, else a device of its own (headless)
	if (!gWorldGpu->Init(VideoGpuDevice()))
	{
		error = gWorldGpu->Error();
		return nullptr;
	}
	return gWorldGpu;
}

bool WorldRecordingPossible()
{
	return VideoIsLayered() && !GameMode::getInstance()->isEditorMode();
}

void StartRequested()
{
	gWorldStarted = true;
	gWorldActive = WorldRendererKind::Software;
	gWorldError.clear();
	if (gWorldRequested == WorldRendererKind::Software) return;
	if (!WorldRecordingPossible())
	{
		gWorldError = "the world is not a layer of its own";
		return;
	}
	if (gWorldRequested == WorldRendererKind::Gpu && !GpuRenderer(gWorldError))
	{
		SLOGW("World renderer: gpu is not available ({}), using software", gWorldError);
		return;
	}
	gWorldActive = gWorldRequested;
	SLOGI("World renderer: {}{}", WorldRendererName(gWorldActive),
		gWorldActive == WorldRendererKind::Gpu ? " (" + gWorldGpu->DriverName() + ")" : std::string());
}

} // namespace


WorldRendererKind ParseWorldRenderer(std::string const& s)
{
	if (s == "gpu") return WorldRendererKind::Gpu;
	if (s == "pipeline") return WorldRendererKind::Pipeline;
	if (!s.empty() && s != "software") SLOGW("world_renderer: unknown value \"{}\", using software", s);
	return WorldRendererKind::Software;
}

char const* WorldRendererName(WorldRendererKind const k)
{
	switch (k)
	{
		case WorldRendererKind::Gpu:      return "gpu";
		case WorldRendererKind::Pipeline: return "pipeline";
		default:                          return "software";
	}
}

WorldRendererKind WorldRendererDefault(bool const driven)
{
	// Software everywhere for now: the gpu renderer is pixel-equivalent, but at 4K it has not yet been shown to hold
	// 60 fps end to end in a real window (docs/plan/native-modern-game-decisions.md, Phase 8). Driven sessions
	// keep software in any case: their screenshots are the reference images.
	(void)driven;
	return WorldRendererKind::Software;
}

void WorldRendererConfigure(WorldRendererKind const k)
{
	gWorldRequested = k;
	gWorldStarted = false;
}

WorldRendererKind WorldRendererRequested() { return gWorldRequested; }

WorldRendererKind WorldRendererActive()
{
	if (!gWorldStarted) StartRequested();
	if (gWorldActive != WorldRendererKind::Software && !WorldRecordingPossible()) return WorldRendererKind::Software;
	return gWorldActive;
}

WorldRendererKind WorldRendererSwitch(WorldRendererKind const k)
{
	WorldRendererConfigure(k);
	WorldRendererKind const now = WorldRendererActive();
	VideoSetWorldRecorded(now != WorldRendererKind::Software);
	if (now != WorldRendererKind::Gpu) VideoSetWorldGpuTexture(nullptr, 0, 0);
	SetRenderFlags(RENDER_FLAG_FULL);
	return now;
}

std::string WorldRendererError() { return gWorldError; }

void WorldRenderFrameTick()
{
	static wr_clock::time_point last{};
	auto const now = wr_clock::now();
	if (last != wr_clock::time_point{}) gWorldStats.frameMs = WrMs(last, now);
	last = now;
}

WorldRenderStats const& WorldRenderLastStats() { return gWorldStats; }

bool RenderWorldRecorded()
{
	WorldRendererKind const kind = WorldRendererActive();
	VideoSetWorldRecorded(kind != WorldRendererKind::Software);
	if (kind == WorldRendererKind::Software)
	{
		VideoSetWorldGpuTexture(nullptr, 0, 0);
		return false;
	}

	if (gRenderFlags & RENDER_FLAG_FULL)
	{
		gfRenderFullThisFrame = TRUE;
		gfTopMessageDirty     = TRUE;
		fInterfacePanelDirty  = DIRTYLEVEL2;
		ApplyScrolling(gsRenderCenterX, gsRenderCenterY, TRUE, FALSE);
		ClearUiViewport();
	}

	CalcRenderParameters(gsWORLD_VIEWPORT_START_X, gsWORLD_VIEWPORT_START_Y, gsWORLD_VIEWPORT_END_X, gsWORLD_VIEWPORT_END_Y);
	RestoreBackgroundRects();
	if (!GameMode::getInstance()->isEditorMode() || !gfEditMode) RenderTacticalInterface();
	SaveBackgroundRects();

	auto const t0 = wr_clock::now();
	RecordWorldFrame(true, true);
	auto const t1 = wr_clock::now();
	ResetRenderParameters();

	double const frameMs = gWorldStats.frameMs;
	gWorldStats = {};
	gWorldStats.frameMs = frameMs;
	gWorldStats.instances = int(gWorldFrame.instances.size());
	gWorldStats.palettes = int(gWorldFrame.palettes.size() / 256);
	gWorldStats.spritePoolPixels = gWorldPool.pixels.size();
	gWorldStats.recordMs = WrMs(t0, t1);

	// Whoever reads the world buffer (headless composition, screenshots of driven sessions) gets a copy
	bool const cpuCopy = sgp::IsHeadless() || Automation::GetOptions().Active() || !VideoGpuDevice();
	if (kind == WorldRendererKind::Gpu)
	{
		std::string error;
		WorldGpu::Renderer* const gpu = GpuRenderer(error);
		if (!gpu || !gpu->Render(gWorldFrame, gWorldPool, ShadeTable, cpuCopy))
		{
			SLOGE("World renderer: the GPU frame failed ({}), back to software", gpu ? gpu->Error() : error);
			gWorldActive = WorldRendererKind::Software;
			gWorldError = gpu ? gpu->Error() : error;
			VideoSetWorldRecorded(false);
			VideoSetWorldGpuTexture(nullptr, 0, 0);
			SetRenderFlags(RENDER_FLAG_FULL);
			return false;
		}
		gWorldStats.gpuBinMs = gpu->LastStats().buildMs;
		gWorldStats.gpuSubmitMs = gpu->LastStats().uploadMs;
		gWorldStats.uploadBytes = gpu->LastStats().uploadBytes;
		if (cpuCopy)
		{
			std::vector<UINT16> px;
			if (gpu->Read565(px)) CopyToWorldBuffer(px.data(), gpu->Width(), gpu->Height());
			gWorldStats.gpuWaitMs = gpu->LastStats().waitMs;
		}
		VideoSetWorldGpuTexture(VideoGpuDevice() ? gpu->Texture() : nullptr, gpu->Width(), gpu->Height());
	}
	else
	{
		auto const r0 = wr_clock::now();
		gWorldTarget.Clear(gWorldFrame);
		WorldPipe::Rasterize(gWorldFrame, gWorldPool, ShadeTable, gWorldTarget);
		CopyToWorldBuffer(gWorldTarget.color.data(), gWorldTarget.w, gWorldTarget.h);
		gWorldStats.rasterMs = WrMs(r0, wr_clock::now());
		VideoSetWorldGpuTexture(nullptr, 0, 0);
	}
	InvalidateWorldRegion(0, 0, WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT);
	if (gRenderFlags & RENDER_FLAG_FULL && !(gRenderFlags & RENDER_FLAG_SAVEOFF)) UpdateSaveBuffer();
	if (gRenderFlags & RENDER_FLAG_MARKED) ClearMarkedTiles();
	gRenderFlags &= ~(RENDER_FLAG_FULL | RENDER_FLAG_MARKED | RENDER_FLAG_ROOMIDS | RENDER_FLAG_CHECKZ);
	return true;
}


WorldEquivalenceResult RunWorldEquivalence(std::string const& outDir, bool const gpu)
{
	if (!gfWorldLoaded) throw std::runtime_error("world equivalence: no sector loaded");
	WorldEquivalenceResult res;
	CalcRenderParameters(gsWORLD_VIEWPORT_START_X, gsWORLD_VIEWPORT_START_Y, gsWORLD_VIEWPORT_END_X, gsWORLD_VIEWPORT_END_Y);
	SGPRect const clip = gClippingRect;
	int const W = WORLD_SCREEN_WIDTH, H = WORLD_SCREEN_HEIGHT;

	// 1. Record (no side effects on the nodes, so the software renderer below sees the same world)
	auto t0 = wr_clock::now();
	RecordWorldFrame(false, false);
	res.recordMs = WrMs(t0, wr_clock::now());
	res.instances = int(gWorldFrame.instances.size());
	res.palettes = int(gWorldFrame.palettes.size() / 256);
	res.sprites = int(gWorldPool.byData.size());
	for (WorldPipe::Instance const& in : gWorldFrame.instances) ++res.ops[WorldPipe::OpName(in.op)];

	// 2. The software renderer, as a full redraw onto black
	ResetLayerOptimizing();
	{
		std::vector<UINT16> const black(size_t(W) * H, 0);
		CopyToWorldBuffer(black.data(), W, H); // not ColorFill: it is clipped to the clipping rectangle
	}
	std::fill_n(gpZBuffer, size_t(gZBufferPitch / sizeof(*gpZBuffer)) * H, LAND_Z_LEVEL);
	t0 = wr_clock::now();
	RenderWorldScene(false);
	res.legacyMs = WrMs(t0, wr_clock::now());
	std::vector<UINT16> legacy;
	ReadWorldBuffer(legacy, W, H);
	ResetRenderParameters();

	// 3. The CPU pipeline
	t0 = wr_clock::now();
	gWorldTarget.Clear(gWorldFrame);
	WorldPipe::Rasterize(gWorldFrame, gWorldPool, ShadeTable, gWorldTarget);
	res.pipelineMs = WrMs(t0, wr_clock::now());

	std::vector<uint8_t> diff;
	bool const png = !outDir.empty();
	WorldPipe::DiffStats const dp = WorldPipe::Compare(legacy.data(), gWorldTarget.color.data(), W, H,
		clip.iLeft, clip.iTop, clip.iRight, clip.iBottom, png ? &diff : nullptr);
	if (dp.different && std::getenv("JA2_WORLD_STRIP_CHECK"))
	{
		for (int i = 0; i < W * H; ++i)
		{
			if (legacy[i] == gWorldTarget.color[i]) continue;
			int const x = i % W, y = i / W;
			if (x < clip.iLeft || x >= clip.iRight || y < clip.iTop || y >= clip.iBottom) continue;
			SLOGW("diff at {},{}: software {} pipeline {}", x, y, legacy[i], gWorldTarget.color[i]);
			for (WorldPipe::Instance const& in : gWorldFrame.instances)
			{
				if (x < in.x0 || x >= in.x1 || y < in.y0 || y >= in.y1) continue;
				UINT16 const s = gWorldPool.pixels[in.sprite + size_t(y - in.oy) * in.spriteW + (x - in.ox)];
				if (s & 0x100) SLOGW("  {} idx {} z {}", WorldPipe::OpName(in.op), s & 0xFF, WorldPipe::DepthAt(in, gWorldFrame, x, y));
			}
			break;
		}
	}
	res.width = clip.iRight - clip.iLeft;
	res.height = clip.iBottom - clip.iTop;
	res.pixels = dp.pixels;
	res.pipelineDifferent = dp.different;
	res.pipelinePercent = dp.percent();
	auto write = [&](std::string const& name, std::vector<uint8_t> const& rgb) {
		std::string const path = outDir + "/" + name;
		stbi_write_png(path.c_str(), W, H, 3, rgb.data(), W * 3);
		return path;
	};
	if (png)
	{
		res.legacyPng = write("world_software.png", WorldPipe::ToRgb(legacy.data(), W, H));
		res.pipelinePng = write("world_pipeline.png", WorldPipe::ToRgb(gWorldTarget.color.data(), W, H));
		res.pipelineDiffPng = write("world_pipeline_diff.png", diff);
	}

	// 4. The GPU
	if (gpu)
	{
		WorldGpu::Renderer* const r = GpuRenderer(res.gpuError);
		if (r)
		{
			res.gpuDriver = r->DriverName();
			std::vector<UINT16> out;
			t0 = wr_clock::now();
			if (r->Render(gWorldFrame, gWorldPool, ShadeTable, true) && r->Read565(out))
			{
				res.gpuMs = WrMs(t0, wr_clock::now());
				res.gpuRan = true;
				WorldPipe::DiffStats const dg = WorldPipe::Compare(legacy.data(), out.data(), W, H,
					clip.iLeft, clip.iTop, clip.iRight, clip.iBottom, png ? &diff : nullptr);
				res.gpuDifferent = dg.different;
				res.gpuPercent = dg.percent();
				res.gpuVsPipelineDifferent = WorldPipe::Compare(gWorldTarget.color.data(), out.data(), W, H,
					clip.iLeft, clip.iTop, clip.iRight, clip.iBottom).different;
				if (png)
				{
					res.gpuPng = write("world_gpu.png", WorldPipe::ToRgb(out.data(), W, H));
					res.gpuDiffPng = write("world_gpu_diff.png", diff);
				}
			}
			else
			{
				res.gpuError = r->Error();
			}
		}
	}
	SetRenderFlags(RENDER_FLAG_FULL);
	return res;
}
