#pragma once
// Phase 0 UI toolkit spike, hosted in the game: ja2.debug("uispike_rml") / ja2.debug("uispike_inhouse") opens the
// save/load spike screen (src/spike/) full screen, drawn at the canvas resolution through SDL's software renderer
// into the frame buffer, so ja2ctl screenshots and input work as for any other screen. Esc returns.

#include "ScreenIDs.h"

#include <string>
#include <vector>

struct UiSpikeElement
{
	std::string id;
	float x, y, w, h;
};

struct UiSpikeInfo
{
	std::string toolkit;          // "" when no spike screen is open
	double      lastFrameMs = 0;  // layout + draw + copy into the frame buffer, wall clock
	double      meanFrameMs = 0;
	int         frames = 0;
	int         selected = -1, hovered = -1;
	bool        modal = false;
	std::string status;
	std::vector<UiSpikeElement> elements;
	std::vector<std::string>    problems; // layout audit (gallery): clipped, off screen, overflowing
};

/** kind = "rml" or "inhouse". Throws if the spikes are not built in. */
void        UiSpikeOpen(std::string const& kind);
UiSpikeInfo UiSpikeGetInfo();

ScreenID UiSpikeScreenHandle();
