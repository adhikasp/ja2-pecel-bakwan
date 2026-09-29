#pragma once

#include <string>
#include <vector>

namespace spike {

struct RenderPathRow
{
	std::string backend;
	int         width = 0, height = 0;
	double      spritesBatchedMs = -1; // 20000 32x32 sprites in one SDL_RenderGeometry batch
	double      spritesCallsMs   = -1; // the same, one draw call per sprite
	double      uiRmlMs          = -1; // the RmlUi spike screen, one frame
	double      uiInhouseMs      = -1; // the in-house spike screen, one frame
	double      readbackMs       = -1; // full-frame readback (headless screenshot)
	std::string note;
	std::string error;
};

/** gpu = false: only the software renderer (what CI can always run). SDL's video subsystem must be up for gpu. */
std::vector<RenderPathRow> RunRenderPathSpike(bool quick, bool gpu);
std::string RenderPathTable(std::vector<RenderPathRow> const&);

} // namespace spike
