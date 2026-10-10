#pragma once
// Phase 8 world renderer (docs/plan/native-modern-game.md): which renderer draws the tactical world, and the
// equivalence check against the software renderer.
//
//  software  the legacy blitters into the WORLD_BUFFER, incrementally (dirty rectangles, scrolling by copy)
//  gpu       every frame, RenderTiles records the whole scene as instances (src/sgp/WorldPipeline.h) and a compute
//            shader on SDL_GPU rasterizes them (src/sgp/WorldGpu.h); the result is the world layer
//  pipeline  the same recording, rasterized by the CPU implementation of that pipeline: what CI can run without a
//            GPU to check the recording, draw order and shading logic of the gpu path
//
// ja2.json "world_renderer", or JA2_WORLD_RENDERER in the environment (wins). The gpu and pipeline renderers need
// the world to be a layer of its own; they make it one (at the UI scale when no world zoom is set). They are not
// used in the map editor. When the GPU renderer cannot start (no Vulkan device), the game falls back to software.

#include "WorldPipeline.h"

#include <map>
#include <string>

enum class WorldRendererKind { Software, Gpu, Pipeline };

/** Parses "software" / "gpu" / "pipeline" (anything else: software, with a warning). */
WorldRendererKind ParseWorldRenderer(std::string const&);
char const* WorldRendererName(WorldRendererKind);

/** The renderer used when ja2.json does not say: `driven` = headless or an automation session. */
WorldRendererKind WorldRendererDefault(bool driven);

/** What was asked for (ja2.json / environment); set before the video manager starts. */
void              WorldRendererConfigure(WorldRendererKind);
WorldRendererKind WorldRendererRequested();
/** What draws the world now (software when the requested renderer is not available). */
WorldRendererKind WorldRendererActive();
/** Switches at run time (automation, tests). Returns what is active afterwards. */
WorldRendererKind WorldRendererSwitch(WorldRendererKind);
/** Driven sessions read GPU frames back into the WORLD_BUFFER (screenshots); off for frame-rate measurements. */
void              WorldRendererSetReadback(bool);
/** The static layers are recorded once and reused while the view does not change; off = re-record every frame. */
void              WorldRendererSetStaticCache(bool);
/** Extra point lights on the world frame, on top of the game's own (muzzle flashes, explosions, lamps). A
 * test/driving hook: ja2.setWorldLights. Cleared with an empty list. */
void              WorldRendererSetExtraLights(std::vector<WorldPipe::PointLight>);
/** Why the gpu renderer is not active, if it was asked for. */
std::string       WorldRendererError();

/** RenderWorld() for the gpu and pipeline renderers. Returns false when the software renderer draws the frame. */
bool RenderWorldRecorded();

struct WorldRenderStats
{
	int    instances = 0;
	int    palettes = 0;
	size_t spritePoolPixels = 0;
	double recordMs = 0, rasterMs = 0, gpuSubmitMs = 0, gpuBinMs = 0, gpuWaitMs = 0;
	size_t uploadBytes = 0;
	double frameMs = 0;
	int    staticHits = 0, staticMisses = 0; // frames that reused / re-recorded the static layers       // wall-clock time since the previous RenderWorld (any renderer)
};
/** Called whenever a frame is presented: measures the frame interval. */
void WorldRenderFrameTick();
struct WorldFrameTiming { uint64_t frames = 0; int samples = 0; double meanMs = 0, p50Ms = 0, p95Ms = 0, maxMs = 0; };
/** Wall-clock frame intervals (between RenderWorld calls) since the last reset. */
WorldFrameTiming WorldRenderTiming(bool reset);
WorldRenderStats const& WorldRenderLastStats();

struct WorldEquivalenceResult
{
	int         width = 0, height = 0;          // the compared area: the world viewport
	int         instances = 0, sprites = 0, palettes = 0;
	std::map<std::string, int> ops;            // instances per op
	long long   pixels = 0;
	long long   pipelineDifferent = 0;          // CPU pipeline vs software renderer
	double      pipelinePercent = 0;
	bool        gpuRan = false;
	std::string gpuDriver, gpuError;
	long long   gpuDifferent = 0;               // GPU vs software renderer
	double      gpuPercent = 0;
	long long   gpuVsPipelineDifferent = 0;
	long long   gpuVsPipelineQuantizedDifferent = 0; // the pipeline rounded to 565 vs the GPU's 565 readback
	double      legacyMs = 0, recordMs = 0, pipelineMs = 0, gpuMs = 0;
	std::string legacyPng, pipelinePng, gpuPng, pipelineDiffPng, gpuDiffPng;
};

/** Renders the current view three ways: the software renderer as a full redraw (static and dynamic layers:
 * land, objects, shadows, structures, mercs, items, corpses, roofs, on-roof, topmost), the recorded instances on
 * the CPU pipeline and, when `gpu` and a device is available, on the GPU; and compares them pixel by pixel.
 * PNGs go to outDir when it is not empty. Leaves a full redraw pending. Throws if there is no world. */
WorldEquivalenceResult RunWorldEquivalence(std::string const& outDir, bool gpu);
