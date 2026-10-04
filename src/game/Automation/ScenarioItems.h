#pragma once

#include "AutomationLua.h"
#include "JA2Types.h"

#include <string>

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
	void GiveItem(SOLDIERTYPE& s, UINT16 item);

	/** Body armour: "none", "kevlar" (or anything else) or "spectra". */
	void EquipArmour(SOLDIERTYPE& s, std::string const& level);

	/** Skill points and life from a spec table. `health` sets both life and life max. */
	void ApplyStats(SOLDIERTYPE& s, sol::table const& t);
}
