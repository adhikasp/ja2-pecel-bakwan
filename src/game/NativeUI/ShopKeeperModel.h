#pragma once
// Pure helpers the native shopkeeper view model uses (docs/ui/shopkeeper.md). No game state, no RmlUi: the
// interesting decisions are testable on their own (ShopKeeperModel_unittest.cc).

#include <string>

namespace NativeUI
{

namespace ShopKeeperModel
{
	/** The condition colour bucket: 0 ok (>=70), 1 warn (>=30), 2 danger (<30). */
	int ConditionBucket(int percent);

	/** The classes of a trade slot: empty, selected/hatched, repaired, and the overlay text. */
	std::string SlotClass(bool active, bool selected, bool repaired, std::string const& overlay);
}

}
