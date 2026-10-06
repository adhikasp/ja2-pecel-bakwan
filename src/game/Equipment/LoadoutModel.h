#pragma once
// The plain rules behind the native loadout screen (issue #262, docs/plan/equipment-revamp.md):
// the weight band the load readout shows, the text of a weight, the ammunition order the
// swap-ammo quick action walks, and the keys drag and drop uses. No game globals here - the
// screen (NativeUI/LoadoutScreen.cc) fills the inputs from the live soldier - so
// LoadoutModel_unittest.cc can assert them against hand-made values.

#include <string_theory/string>

#include <cstdint>
#include <string>
#include <vector>

namespace Equipment
{

// ---- the load readout (the encumbrance number of #140, shown live) -------------------------------------------
enum class WeightBand : uint8_t
{
	Light,
	Loaded,
	Heavy,
	Overloaded,
};

/** Light below 50 %, Loaded 50 to 89 %, Heavy 90 to 100 %, Overloaded above 100 % of the carry
 *  capacity - the same figure the legacy breath adjustment reads as "overloaded". */
WeightBand WeightBandOf(int percent);
/** "light" | "loaded" | "heavy" | "overloaded": the "lo.band.*" string key. */
char const* WeightBandKey(WeightBand band);

/** The carry capacity the legacy percentage divides against: 500 g per effective strength point,
 *  doubled above 80 (Items.cc CalculateCarriedWeight). */
int CarryCapacityGrams(int effectiveStrength);

/** A weight to one decimal with its unit: metric is "12.4 kg", otherwise "27.3 lb". */
ST::string WeightText(int grams, bool metric);

// ---- the swap-ammo quick action ------------------------------------------------------------------------------
/** The canonical ammunition order it walks: ball, AP, super AP, hollow point (the game's AMMO_*
 *  indexes; the readout's order). */
int const* AmmoTypeOrder(int& count);
/** The next carried ammunition type after @a current in the canonical order, or -1 when @a carried
 *  holds no type other than @a current. */
int NextAmmoType(int current, std::vector<int> const& carried);
/** "ball" | "ap" | "hp" | "sub" for the readout's "ro.ammo.*" strings, from a game AMMO_* index. */
char const* AmmoTypeKey(int gameAmmoIndex);

// ---- drag keys -----------------------------------------------------------------------------------------------
enum class DragKind : uint8_t
{
	Slot,
	Attachment,
	None,
};

struct DragKey
{
	DragKind kind  = DragKind::None;
	int      index = -1;
};

/** An inventory slot ("i5") or a weapon attachment position ("a2"). */
std::string SlotKey(int pos);
std::string AttachmentKey(int index);
/** The key back, or { None, -1 } when it names no slot or attachment. */
DragKey ParseDragKey(std::string const& key);

}
