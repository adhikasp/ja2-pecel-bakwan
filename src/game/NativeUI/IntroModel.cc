#include "IntroModel.h"

namespace NativeUI
{

namespace IntroModel
{

std::vector<Scene> Chain(Kind const kind, bool const miguelDead, bool const skyriderDead)
{
	switch (kind)
	{
		case Kind::Splash:
			return {
				{ "splashscreen", "splashscreen.smk" },
			};

		case Kind::Beginning:
			return {
				{ "rebel-cr", "rebel_cr.smk" },
				{ "omerta",   "omerta.smk"   },
				{ "prague-cr","prague_cr.smk"},
				{ "prague",   "prague.smk"   },
			};

		case Kind::Ending:
			return {
				{ miguelDead ? "throne-nomig" : "throne-mig", miguelDead ? "throne_nomig.smk" : "throne_mig.smk" },
				{ "heli-flyby", "heli_flyby.smk" },
				{ skyriderDead ? "heli-nosky" : "heli-sky", skyriderDead ? "heli_nosky.smk" : "heli_sky.smk" },
			};
	}
	return {};
}

ScreenID ExitTarget(Kind const kind)
{
	// The ending is the campaign's last page: it hands over to the victory epilogue (docs/ui/epilogue.md), which
	// restarts the game and goes on to the credits. The splash and the new-game intro start the game.
	return kind == Kind::Ending ? EPILOGUE_SCREEN : INIT_SCREEN;
}

bool ChromeVisible(uint32_t const nowMs, uint32_t const lastInputMs, uint32_t const idleMs)
{
	return nowMs - lastInputMs < idleMs;
}

}

}
