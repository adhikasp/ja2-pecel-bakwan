#pragma once
// Phase 0 world renderer spike (docs/plan/native-modern-game-decisions.md): the current sector's static tiles,
// rendered the GPU way (instances with a sprite, a palette/shade LUT, a depth and a depth-test op, drawn in
// submission order against a depth buffer, see src/spike/WorldSpikeRaster.h), compared pixel by pixel with the
// legacy software renderer (RenderStaticWorld) at 1x, for the tactical viewport as it is on screen.

#include <string>

struct WorldSpikeResult
{
	int         width = 0, height = 0;     // compared area (the world viewport)
	long long   pixels = 0, different = 0;
	double      percent = 0;
	int         instances = 0;             // sprites drawn by the spike
	int         skipped = 0;               // nodes the spike does not handle (items, corpses, absolute positions)
	int         sprites = 0;               // distinct decoded frames ("atlas" entries)
	double      legacyMs = 0, buildMs = 0, rasterMs = 0;
	std::string legacyPng, spikePng, diffPng;
};

/** Renders the static world both ways and compares. PNGs are written to outDir when it is not empty.
 * Leaves a full redraw pending. Throws if there is no world loaded or the spikes are not built in. */
WorldSpikeResult RunWorldSpike(std::string const& outDir);
