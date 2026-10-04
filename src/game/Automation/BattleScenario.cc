#include "BattleScenario.h"

#include "Auto_Resolve.h"
#include "Campaign_Types.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "GridSquare.h"
#include "Handle_Items.h"
#include "Isometric_Utils.h"
#include "Item_Types.h"
#include "ItemModel.h"
#include "Items.h"
#include "JAScreens.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Add.h"
#include "Soldier_Control.h"
#include "Soldier_Create.h"
#include "Soldier_Tile.h"
#include "Strategic.h"
#include "StrategicMap.h"
#include "WorldDef.h"
#include "WorldMan.h"

#include <string_theory/string>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace Automation
{

namespace
{
	bool AsInt(sol::object const& o, int& out)
	{
		if (o.is<int>())        { out = o.as<int>();        return true; }
		if (o.is<double>())     { out = int(o.as<double>()); return true; }
		if (o.is<long long>())  { out = int(o.as<long long>()); return true; }
		return false;
	}

	int IntField(sol::table const& spec, char const* const key, int const fallback)
	{
		sol::object const o = spec[key];
		int v;
		return AsInt(o, v) ? v : fallback;
	}

	bool BoolField(sol::table const& spec, char const* const key, bool const fallback)
	{
		sol::object const o = spec[key];
		return o.is<bool>() ? o.as<bool>() : fallback;
	}

	std::string StrField(sol::table const& spec, char const* const key, std::string const& fallback)
	{
		sol::object const o = spec[key];
		return o.is<std::string>() ? o.as<std::string>() : fallback;
	}

	/** "none" | "kevlar" | "spectra", from a bool (true = kevlar) or a string. */
	std::string ArmourLevel(sol::object const& o, std::string const& fallback)
	{
		if (o.is<bool>())        return o.as<bool>() ? "kevlar" : "none";
		if (o.is<std::string>()) return o.as<std::string>();
		return fallback;
	}

	// An item's index from its original internal name (weapons.json, armours.json, ...).
	UINT16 ItemByName(std::string const& name)
	{
		ItemModel const* const item = GCM->getItemByName(ST::string(name));
		if (!item) throw std::runtime_error("unknown item \"" + name + "\"");
		return item->getItemIndex();
	}

	SoldierClass ParseClass(std::string const& name)
	{
		if (name == "administrator" || name == "admin") return SOLDIER_CLASS_ADMINISTRATOR;
		if (name == "army" || name == "troop" || name == "troops") return SOLDIER_CLASS_ARMY;
		if (name == "elite") return SOLDIER_CLASS_ELITE;
		throw std::runtime_error("unknown enemy class \"" + name + "\"");
	}

	void ClearSlot(SOLDIERTYPE& s, UINT8 const slot) { s.inv[slot] = OBJECTTYPE{}; }

	void PutInSlot(SOLDIERTYPE& s, UINT8 const slot, UINT16 const item, UINT8 const count)
	{
		ClearSlot(s, slot);
		OBJECTTYPE obj;
		CreateItems(item, 100, count, &obj);
		if (!PlaceObject(&s, slot, &obj))
		{
			// some kits refuse a slot; let the normal rules find room
			AutoPlaceObject(&s, &obj, TRUE);
		}
	}

	// A loaded gun in the hand and spare magazines in the pockets.
	void GiveGun(SOLDIERTYPE& s, UINT16 const gun)
	{
		PutInSlot(s, HANDPOS, gun, 1);
		OBJECTTYPE mag;
		CreateItems(DefaultMagazine(gun), 100, 2, &mag);
		AutoPlaceObject(&s, &mag, TRUE);
	}

	// Any item, into whatever pocket fits (a gun already in hand is not replaced).
	void GiveItem(SOLDIERTYPE& s, UINT16 const item)
	{
		OBJECTTYPE obj;
		CreateItems(item, 100, 1, &obj);
		AutoPlaceObject(&s, &obj, TRUE);
	}

	void EquipArmour(SOLDIERTYPE& s, std::string const& level)
	{
		UINT16 vest = NOTHING, helmet = NOTHING, legs = NOTHING;
		if (level == "spectra")
		{
			vest = ItemByName("SPECTRA_VEST"); helmet = ItemByName("SPECTRA_HELMET"); legs = ItemByName("SPECTRA_LEGGINGS");
		}
		else if (level != "none" && !level.empty())
		{
			vest = ItemByName("KEVLAR_VEST"); helmet = ItemByName("KEVLAR_HELMET"); legs = ItemByName("KEVLAR_LEGGINGS");
		}
		ClearSlot(s, VESTPOS); ClearSlot(s, HELMETPOS); ClearSlot(s, LEGPOS);
		if (vest   != NOTHING) PutInSlot(s, VESTPOS,   vest,   1);
		if (helmet != NOTHING) PutInSlot(s, HELMETPOS, helmet, 1);
		if (legs   != NOTHING) PutInSlot(s, LEGPOS,    legs,   1);
	}

	// Skill points and life. `health` sets both life and life max (full health).
	void ApplyStats(SOLDIERTYPE& s, sol::table const& t)
	{
		auto seti = [&](char const* const key, INT8& field, int const lo, int const hi) {
			sol::object const o = t[key];
			int v;
			if (AsInt(o, v)) field = INT8(std::clamp(v, lo, hi));
		};
		seti("marksmanship", s.bMarksmanship, 1, 100);
		seti("agility",      s.bAgility,      1, 100);
		seti("dexterity",    s.bDexterity,    1, 100);
		seti("strength",     s.bStrength,     1, 100);
		seti("leadership",   s.bLeadership,   1, 100);
		seti("wisdom",       s.bWisdom,       1, 100);
		seti("medical",      s.bMedical,      0, 100);
		seti("mechanical",   s.bMechanical,   0, 100);
		seti("explosive",    s.bExplosive,    0, 100);
		seti("morale",       s.bMorale,       0, 100);
		seti("level",        s.bExpLevel,     1, 10);
		sol::object const healthObj = t["health"];
		int health;
		if (AsInt(healthObj, health))
		{
			health = std::clamp(health, 1, 100);
			s.bLifeMax = INT8(health);
			s.bLife    = INT8(health);
			s.bBleeding = 0;
		}
		s.bBreathMax = 100;
		s.bBreath    = 100;
	}

	// Move an actor onto @a grid without running sight (placement must not start combat
	// before StageBattle chooses who goes first).
	void PlaceActor(SOLDIERTYPE& s, GridNo const grid)
	{
		if (grid == NOWHERE) return;
		EVENT_SetSoldierPosition(&s, grid, SSP_NONE);
		EVENT_SetSoldierDirection(&s, s.bDirection);
		EVENT_SetSoldierDesiredDirection(&s, s.bDirection);
		s.sFinalDestination = grid;
	}

	// Find the per-merc setup entry, by name if the entries are named, else by position.
	sol::object MercSetup(sol::object const& our, int const index, std::string const& name)
	{
		if (!our.is<sol::table>()) return sol::nil;
		sol::table const table = our.as<sol::table>();
		int const n = int(table.size());
		auto entryName = [&](sol::object const& e) -> std::string {
			if (!e.is<sol::table>()) return std::string();
			sol::object const nm = e.as<sol::table>()["name"];
			return nm.is<std::string>() ? nm.as<std::string>() : std::string();
		};
		bool named = false;
		for (int i = 1; i <= n && !named; ++i) named = !entryName(table[i]).empty();
		if (named)
		{
			for (int i = 1; i <= n; ++i)
			{
				sol::object const e = table[i];
				if (entryName(e) == name) return e;
			}
			return sol::nil;
		}
		return index <= n ? sol::object(table[index]) : sol::object(sol::nil);
	}

	// Free, standable tiles a given distance from @a anchor, nearest to @a ideal first.
	std::vector<GridNo> FreeTilesAround(GridNo const anchor, int const ideal, int const radius)
	{
		SOLDIERTYPE dummy{};
		dummy.bLevel = 0;
		dummy.bTeam  = ENEMY_TEAM;
		dummy.sGridNo = anchor;

		std::vector<GridNo> tiles;
		for (GridNo const g : GridSquare{anchor, radius})
		{
			if (!GridNoOnVisibleWorldTile(g)) continue;
			if (!NewOKDestination(&dummy, g, TRUE, 0)) continue;
			tiles.push_back(g);
		}
		std::sort(tiles.begin(), tiles.end(), [&](GridNo const a, GridNo const b) {
			return std::abs(SpacesAway(anchor, a) - ideal) < std::abs(SpacesAway(anchor, b) - ideal);
		});
		return tiles;
	}
}

void FireAtGrid(SOLDIERTYPE* const soldier, INT16 const targetGridNo)
{
	if (!soldier) throw std::runtime_error("ja2.debug(\"fire\"): no selected merc");
	if (targetGridNo == NOWHERE) throw std::runtime_error("ja2.debug(\"fire\"): bad target tile");
	if (soldier->inv[HANDPOS].usItem == NOTHING)
		throw std::runtime_error("ja2.debug(\"fire\"): " + soldier->name.to_std_string() + " has nothing in his hand");
	// The same entry the AI fires through (AIMain.cc: AI_ACTION_FIRE_GUN): it sets the
	// attacking hand and weapon, checks the AP cost and ammo, and starts the real fire chain.
	SOLDIERTYPE const* const target = WhoIsThere2(targetGridNo, 0);
	INT8 const level = target ? target->bLevel : 0;
	HandleItem(soldier, targetGridNo, level, soldier->inv[HANDPOS].usItem, FALSE);
}

void StageBattle(sol::table const& spec)
{
	if (guiCurrentScreen != GAME_SCREEN)
		throw std::runtime_error("ja2.debug(\"battle\"): needs the tactical screen");
	if (!gWorldSector.IsValid())
		throw std::runtime_error("ja2.debug(\"battle\"): no sector is loaded");

	bool const clear  = BoolField(spec, "clear", true);
	bool const start  = BoolField(spec, "start", true);
	int  distance = std::max(1, IntField(spec, "distance", 6));
	int  const defaultEnemies = std::max(0, IntField(spec, "enemies", 10));
	std::string const defaultWeapon = StrField(spec, "weapon", "MP5K");
	sol::object const armourObj = spec["armour"];
	std::string const defaultArmour = ArmourLevel(armourObj, "kevlar");
	std::string const defaultClass  = StrField(spec, "class", "administrator");
	std::string const defaultEnemyWeapon = StrField(spec, "enemy_weapon", "");

	// `enemies` may be a count or a table with its own setup.
	sol::object const enemiesObj = spec["enemies"];
	sol::table enemySpec;
	int enemies = defaultEnemies;
	std::string enemyClass = defaultClass;
	std::string enemyWeapon = defaultEnemyWeapon;
	std::vector<GridNo> enemyGrids;
	bool haveEnemyGrids = false;
	if (enemiesObj.is<sol::table>())
	{
		enemySpec = enemiesObj.as<sol::table>();
		enemies = std::max(0, IntField(enemySpec, "count", 10));
		enemyClass = StrField(enemySpec, "class", defaultClass);
		enemyWeapon = StrField(enemySpec, "weapon", defaultEnemyWeapon);
		distance = std::max(1, IntField(enemySpec, "distance", distance));
		sol::object const gridsObj = enemySpec["grids"];
		if (gridsObj.is<sol::table>())
		{
			sol::table const grids = gridsObj.as<sol::table>();
			for (int i = 1; i <= int(grids.size()); ++i)
			{
				sol::object const o = grids[i];
				int g;
				if (AsInt(o, g)) enemyGrids.push_back(GridNo(g));
			}
			haveEnemyGrids = !enemyGrids.empty();
		}
	}
	SoldierClass const enemySoldierClass = ParseClass(enemyClass);

	if (clear)
	{
		// as ja2.debug("clearenemies"): drop the defenders and their strategic count
		FOR_EACH_IN_TEAM(e, ENEMY_TEAM) TacticalRemoveSoldier(*e);
		EliminateAllEnemies(gWorldSector);
		gTacticalStatus.fEnemyInSector = FALSE;
	}

	// Equip and place the player's team.
	sol::object const our = spec["our"];
	int anchorSum = 0, anchorN = 0, mercIndex = 0;
	FOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (!s->bInSector || s->bLife <= 0) continue;
		++mercIndex;
		sol::object const setup = MercSetup(our, mercIndex, s->name.to_std_string());

		std::string weapon = defaultWeapon;
		std::string armour = defaultArmour;
		GridNo grid = NOWHERE;
		sol::object items = sol::nil, stats = sol::nil;
		if (setup.is<sol::table>())
		{
			sol::table const e = setup.as<sol::table>();
			weapon = StrField(e, "weapon", weapon);
			sol::object const setupArmour = e["armour"];
			armour = ArmourLevel(setupArmour, armour);
			grid   = GridNo(IntField(e, "grid", grid));
			items  = e["items"];
			stats  = e["stats"];
		}

		GiveGun(*s, ItemByName(weapon));
		EquipArmour(*s, armour);
		if (stats.is<sol::table>()) ApplyStats(*s, stats.as<sol::table>());
		if (items.is<sol::table>())
		{
			sol::table const list = items.as<sol::table>();
			for (int i = 1; i <= int(list.size()); ++i)
			{
				sol::object const o = list[i];
				if (o.is<std::string>()) GiveItem(*s, ItemByName(o.as<std::string>()));
			}
		}
		if (grid != NOWHERE) TeleportSoldier(*s, grid, true);

		anchorSum += s->sGridNo;
		++anchorN;
	}
	if (anchorN == 0) throw std::runtime_error("ja2.debug(\"battle\"): no merc is in the sector");
	GridNo const anchor = INT16(anchorSum / anchorN);

	// Spawn and place the enemies.
	UINT16 const enemyGun = enemyWeapon.empty() ? NOTHING : ItemByName(enemyWeapon);
	std::vector<GridNo> const tiles = haveEnemyGrids ? std::vector<GridNo>{} : FreeTilesAround(anchor, distance, distance + 6);
	int spawned = 0;
	for (int i = 0; i < enemies; ++i)
	{
		GridNo grid = NOWHERE;
		if (haveEnemyGrids)
		{
			if (i >= int(enemyGrids.size())) break;
			grid = enemyGrids[i];
		}
		else
		{
			if (i >= int(tiles.size())) break;
			grid = tiles[i];
		}
		SOLDIERTYPE* const e = TacticalCreateEnemySoldier(enemySoldierClass);
		if (!e) continue;
		e->sSector = gWorldSector;
		e->sInsertionGridNo = grid;
		e->ubStrategicInsertionCode = INSERTION_CODE_GRIDNO;
		AddSoldierToSector(e);
		PlaceActor(*e, grid);
		if (enemyGun != NOTHING) GiveGun(*e, enemyGun);
		++spawned;
	}
	if (spawned == 0) throw std::runtime_error("ja2.debug(\"battle\"): could not place any enemy");

	// The strategic enemy counters must match the soldiers now in the sector, or the battle
	// end/strategic code thinks the sector's garrison is inconsistent (Queen_Command warns
	// "Sector admin counters are bad" and the turn handling gets confused). Same counting as
	// ja2.debug("clearenemies")/FakeEncounter().
	SECTORINFO& si = SectorInfo[gWorldSector.AsByte()];
	si.ubNumAdmins = si.ubNumTroops = si.ubNumElites = 0;
	FOR_EACH_IN_TEAM(e, ENEMY_TEAM)
	{
		if (!e->bInSector || e->bLife == 0) continue;
		if      (e->ubSoldierClass == SOLDIER_CLASS_ADMINISTRATOR) ++si.ubNumAdmins;
		else if (e->ubSoldierClass == SOLDIER_CLASS_ELITE)         ++si.ubNumElites;
		else                                                       ++si.ubNumTroops;
	}
	// the "in battle" counts the death handling decrements alongside them
	si.ubAdminsInBattle = si.ubNumAdmins;
	si.ubTroopsInBattle = si.ubNumTroops;
	si.ubElitesInBattle = si.ubNumElites;

	// Enter combat first, with the player's turn, so the scenario is deterministic (a sighting
	// pass started in real time can hand the first turn to whichever side spots the other).
	// Then let everyone look, which settles who sees whom (and fills in the sightings and
	// interrupts) while combat is already running.
	if (start && (gTacticalStatus.uiFlags & INCOMBAT) == 0) EnterCombatMode(OUR_TEAM);
	AllTeamsLookForAll(FALSE);
}

}
