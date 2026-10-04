#pragma once
// The intro/ending cinematic's presentation model (docs/ui/intro.md): which scene plays when in which mode, where
// the presentation hands over to, and when the hint bar is visible. Pure data and decisions — no game state, no
// RmlUi (IntroModel_unittest.cc).

#include "ScreenIDs.h"

#include <cstdint>
#include <string>
#include <vector>

namespace NativeUI
{

namespace IntroModel
{
	enum class Kind { Splash, Beginning, Ending };

	/** One scene of a presentation: a Smacker flic in INTRODIR (the videos are content and stay the legacy
	 * decoder's). The id is what tests and ja2.viewModel("intro") report. */
	struct Scene
	{
		std::string id;
		std::string file;
	};

	/** The scenes of one presentation, in order. The ending picks its variant of the throne speech and of the
	 * helicopter departure from who is still alive — the legacy chain of Intro.cc GetNextIntroVideo(). */
	std::vector<Scene> Chain(Kind kind, bool miguelDead, bool skyriderDead);

	/** The screen the presentation hands over to when the chain is done (or skipped with Esc). */
	ScreenID ExitTarget(Kind kind);

	/** The hint bar is visible while the player is active and for @a idleMs after that (docs/ui/intro.md §6:
	 * fading hints — untouched, the screen is pure picture). */
	bool ChromeVisible(uint32_t nowMs, uint32_t lastInputMs, uint32_t idleMs = 2000);

	/** How long a scene whose video cannot be played stands as a still card (§S6), in milliseconds. */
	constexpr uint32_t CARD_MS = 4000;
}

}
