#pragma once
// Pure helpers the native auto-resolve view model uses (docs/ui/autoresolve.md). No game state, no RmlUi: the
// interesting decisions are testable on their own (AutoResolveModel_unittest.cc).

#include <string>

namespace NativeUI
{

namespace AutoResolveModel
{
	/** A percentage in 0..100, safe for a zero maximum. */
	int Percent(int value, int max);

	/** The remaining-forces colour, by the legacy screen's thresholds: bad (red), even (yellow), good (green). */
	char const* ForcesClass(int good, int bad);

	/** The classes of a participant card: liveness, status, flags and its interaction state. */
	std::string CellClass(bool dead, bool unconscious, bool bleeding, bool hit, bool leader,
		bool robot, bool epc, bool retreating, bool retreated, bool clickable);
}

}
