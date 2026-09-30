#pragma once
// RmlUi plumbing of the Phase 0 save/load spike (RmlSpike.cc) and the Phase 1 style demos and gallery. The render
// interface, clock, tokens and fonts were promoted to the native UI runtime (src/nativeui/UiCore.h); the spikes use
// them from there.

#include "UiCore.h"

namespace spike {

using nui::SdlRenderInterface;
using nui::VirtualClock;
using nui::RmlClock;
using nui::LoadFontFileOnce;
using nui::Tokens;
using nui::LoadUiFonts;

/** nui::InitRml plus the spike's embedded Lato faces, once. */
void InitRmlOnce();

} // namespace spike
