#pragma once

#include "AutomationLua.h"
#include "JA2Types.h"

#include <string>
#include <vector>

struct SOLDIERTYPE;

/** @file
 * Small helpers shared by the automation harnesses that build soldiers out of a
 * Lua spec: the tactical battle harness (`BattleScenario.cc`) and the campaign
 * state harness (`CampaignScenario.cc`). Item names, guns, armour and skill
 * points are handled here so both use a single implementation.
 */
namespace Automation::Scenario
{
	// --- Lua field readers -------------------------------------------------

	/** Read an integer from a Lua value (accepts int, double and long long). */
	bool AsInt(sol::object const& o, int& out);
	int  IntField(sol::table const& spec, char const* key, int fallback);
	bool BoolField(sol::table const& spec, char const* key, bool fallback);
	std::string StrField(sol::table const& spec, char const* key, std::string const& fallback);

	/** "none" | "kevlar" | "spectra", from a bool (true = kevlar) or a string. */
	std::string ArmourLevel(sol::object const& o, std::string const& fallback);

	// --- Items -------------------------------------------------------------

	/** An item's index from its original internal name (weapons.json, armours.json, ...).
	 * Throws std::runtime_error with the name when there is no such item. */
	UINT16 ItemByName(std::string const& name);

	/** The internal name of an item index, or "" when it has none. */
	std::string ItemName(UINT16 item);

	// --- Soldier equipment -------------------------------------------------

	void ClearSlot(SOLDIERTYPE& s, UINT8 slot);
	void PutInSlot(SOLDIERTYPE& s, UINT8 slot, UINT16 item, UINT8 count);

	/** A loaded gun in the hand and spare magazines in the pockets. */
	void GiveGun(SOLDIERTYPE& s, UINT16 gun);

	/** Any item, into whatever pocket fits (a gun already in hand is not replaced). */
	void GiveItem(SOLDIERTYPE& s, UINT16 item, UINT8 count = 1, UINT8 condition = 100);

	/** Body armour: "none", "kevlar" (or anything else) or "spectra". */
	void EquipArmour(SOLDIERTYPE& s, std::string const& level);

	/** Skill points and life from a spec table. `health` sets both life and life max. */
	void ApplyStats(SOLDIERTYPE& s, sol::table const& t);

	// --- The equipment schema ---------------------------------------------

	/** The magazine a fixture asked for, either by a magazine item's own internal
	 * name ("CLIP556_30_AP") or by an ammo type's name ("AMMO_AP"). Named by type it
	 * must be a magazine of that type for @a gun's calibre - the closest one to the
	 * weapon's magazine size, preferring an exact match - never a substitute.
	 * Returns NOTHING when there is nothing by that name, or the weapon has no
	 * magazine of that type. */
	UINT16 MagazineFor(std::string const& name, UINT16 gun);

	/** An ammo type's internal name ("AMMO_AP") from the index a weapon is loaded
	 * with, or "" when the content manager has no such type. */
	std::string AmmoTypeName(UINT8 index);

	/** A whole loadout from a spec table: the weapon's `condition`, the `ammo` in
	 * its magazine well (a magazine name or an ammo type name), typed
	 * `attachments` keyed by slot role ("optic", "muzzle", "underbarrel",
	 * "side_rail", "plate", "nvg"), `lbe` keyed by "vest"/"belt"/"pack" and
	 * `pockets` keyed by "POCK1".."POCK12" (a name or { item = , count = }).
	 * Everything goes through the equipment rules; each refusal is reported in
	 * `problems` instead of being silently ignored. */
	void ApplyEquipment(SOLDIERTYPE& s, sol::table const& spec, std::vector<std::string>& problems);

	/** The loadout a soldier carries, as data: weapon with its condition and the
	 * magazine in it, attachments keyed by slot role, worn LBE keyed by kind,
	 * pocket contents keyed by pocket, and the armour by where it is worn. */
	sol::table LoadoutTable(sol::state_view L, SOLDIERTYPE const& s);
}
