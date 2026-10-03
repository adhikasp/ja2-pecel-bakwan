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
#include "WorldMan.h"
#include "Strategic.h"
#include "StrategicMap.h"
#include "Weapons.h"
#include "WorldDef.h"

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
	int IntField(sol::table const& spec, char const* const key, int const fallback)
	{
		sol::object const o = spec[key];
		if (o.is<int>())    return o.as<int>();
		if (o.is<double>()) return int(o.as<double>());
		if (o.is<long long>()) return int(o.as<long long>());
		return fallback;
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

	// An item's index from its original internal name (weapons.json, armours.json).
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

	void PutInSlot(SOLDIERTYPE& s, UINT8 const slot, UINT16 const item, UINT8 const count)
	{
		s.inv[slot] = OBJECTTYPE{};
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

	bool const clear    = BoolField(spec, "clear", true);
	bool const start    = BoolField(spec, "start", true);
	bool const armour   = BoolField(spec, "armour", true);
	int const  enemies  = std::max(0, IntField(spec, "enemies", 10));
	int const  distance = std::max(1, IntField(spec, "distance", 6));
	SoldierClass const sc = ParseClass(StrField(spec, "class", "administrator"));
	UINT16 const ourGun   = ItemByName(StrField(spec, "weapon", "MP5K"));
	std::string const enemyWeapon = StrField(spec, "enemy_weapon", "");

	UINT16 const vest     = armour ? ItemByName("KEVLAR_VEST")     : NOTHING;
	UINT16 const helmet   = armour ? ItemByName("KEVLAR_HELMET")   : NOTHING;
	UINT16 const leggings = armour ? ItemByName("KEVLAR_LEGGINGS") : NOTHING;

	if (clear)
	{
		// as ja2.debug("clearenemies"): drop the defenders and their strategic count
		FOR_EACH_IN_TEAM(e, ENEMY_TEAM) TacticalRemoveSoldier(*e);
		EliminateAllEnemies(gWorldSector);
		gTacticalStatus.fEnemyInSector = FALSE;
	}

	// Equip the player's team and find where they stand.
	int anchorSum = 0, anchorN = 0;
	FOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (!s->bInSector || s->bLife <= 0) continue;
		GiveGun(*s, ourGun);
		if (armour)
		{
			PutInSlot(*s, VESTPOS,   vest,     1);
			PutInSlot(*s, HELMETPOS, helmet,   1);
			PutInSlot(*s, LEGPOS,    leggings, 1);
		}
		anchorSum += s->sGridNo;
		++anchorN;
	}
	if (anchorN == 0) throw std::runtime_error("ja2.debug(\"battle\"): no merc is in the sector");
	GridNo const anchor = INT16(anchorSum / anchorN);

	// Spawn the enemies around them.
	UINT16 const enemyGun = enemyWeapon.empty() ? NOTHING : ItemByName(enemyWeapon);
	std::vector<GridNo> const tiles = FreeTilesAround(anchor, distance, distance + 6);
	int spawned = 0;
	for (int i = 0; i < enemies && i < int(tiles.size()); ++i)
	{
		SOLDIERTYPE* const e = TacticalCreateEnemySoldier(sc);
		if (!e) continue;
		e->sSector = gWorldSector;
		e->sInsertionGridNo = tiles[i];
		e->ubStrategicInsertionCode = INSERTION_CODE_GRIDNO;
		if (e->ubInsertionDirection >= 100) e->ubInsertionDirection -= 100;
		e->ubInsertionDirection = GetDirectionToGridNoFromGridNo(tiles[i], anchor);
		AddSoldierToSector(e);
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
