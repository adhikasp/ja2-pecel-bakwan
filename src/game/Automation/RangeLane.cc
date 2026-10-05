#include "RangeLane.h"
#include "ScenarioItems.h"

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
#include "LOS.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Random.h"
#include "Soldier_Add.h"
#include "Soldier_Control.h"
#include "Soldier_Create.h"
#include "Soldier_Tile.h"
#include "Strategic.h"
#include "StrategicMap.h"
#include "WorldDef.h"
#include "WorldMan.h"
#include "Weapons.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace Automation
{

namespace
{
	Lane g_lane;
	bool g_lastShotOrdered = false;
	std::string g_lastShotRefusal;

	// Why the game would not let the last lane shot happen, in the game's own words.
	char const* ShotRefusalName(ItemHandleResult const result)
	{
		switch (result)
		{
			case ITEM_HANDLE_NOAPS:    return "not enough AP";
			case ITEM_HANDLE_NOAMMO:   return "no ammo";
			case ITEM_HANDLE_BROKEN:   return "the weapon is broken or jammed";
			case ITEM_HANDLE_UNCONSCIOUS: return "the shooter is unconscious";
			case ITEM_HANDLE_REFUSAL:  return "the shooter refused";
			case ITEM_HANDLE_NOROOM:   return "no room to reload";
			case ITEM_HANDLE_CANNOT_GETTO_LOCATION: return "cannot reach the target";
			case ITEM_HANDLE_RELOADING: return "still reloading";
			default:                   return "the game refused the shot";
		}
	}

	// The two actors, re-resolved from their ids every time. A soldier that has left
	// the sector (a target a heavy round killed) must never be dereferenced, so the
	// lane looks it up rather than holding a pointer across shots.
	SOLDIERTYPE* SoldierById(bool const ours, UINT8 const id)
	{
		SOLDIERTYPE* found = nullptr;
		FOR_EACH_IN_TEAM(s, ours ? OUR_TEAM : ENEMY_TEAM)
		{
			if (s->ubID != id || !s->bInSector) continue;
			found = s;
			break;
		}
		return found;
	}

	// The recorder: one entry per trigger pull, filled in when the round arrives.
	// Both events carry the shooter, and a lane has one shooter, so the impact of a
	// shot always belongs to the most recent shot that has not been resolved yet.
	void RecordFired(ShotFired const& fired)
	{
		SOLDIERTYPE* const shooter = SoldierById(true, g_lane.shooterId);
		if (!g_lane.staged || !shooter || fired.shooter != shooter) return;
		LaneShot shot;
		shot.chanceToHit     = int(fired.chanceToHit);
		shot.roll            = int(fired.roll);
		shot.hit             = fired.hit != FALSE;
		shot.noiseVolume     = int(fired.noiseVolume);
		shot.conditionBefore = int(fired.gunConditionBefore);
		shot.conditionAfter  = int(fired.gunConditionAfter);
		g_lane.shots.push_back(shot);

		// Keep the shooter supplied with AP. A merc who runs out mid-lane hands the
		// turn to the enemy, who then shoots back and the measurement is no longer of
		// the lane. Topping the pool back up on every trigger pull keeps the turn on
		// our side for as long as the fixture wants to shoot.
		shooter->bActionPoints = g_lane.apPool;
	}

	void RecordImpact(ShotImpact const& impact)
	{
		SOLDIERTYPE* const shooter = SoldierById(true, g_lane.shooterId);
		if (!g_lane.staged || !shooter || impact.shooter != shooter) return;
		for (auto it = g_lane.shots.rbegin(); it != g_lane.shots.rend(); ++it)
		{
			if (it->impacted) continue;
			it->impacted           = true;
			it->distance           = int(impact.distance);
			it->hitLocation        = int(impact.hitLocation);
			it->impactBeforeArmour = int(impact.impactBeforeArmour);
			it->armourProtection   = int(impact.armourProtection);
			it->damage             = int(impact.impactAfterArmour);
			it->penetrated         = impact.penetrated != FALSE;
			return;
		}
		// A round that arrives with no trigger pull to attach to would be dropped
		// silently, which would quietly under-report the lane's damage. Say so.
		SLOGW("range lane: a round arrived with no shot to attach it to ({} damage)", impact.impactAfterArmour);
	}

	// Listen once; Observable replaces a listener with the same key.
	struct ListenerInstaller
	{
		ListenerInstaller()
		{
			OnShotFired.addListener("automation:range-lane", RecordFired);
			OnShotImpact.addListener("automation:range-lane", RecordImpact);
		}
	};
	ListenerInstaller const g_installListeners;

	// A standable, visible tile exactly @a distance tiles from @a anchor with a clear
	// line to it - the far end of a shooting lane. Ties are broken by grid number so
	// the same sector always gives the same lane.
	GridNo LaneTargetTile(GridNo const anchor, int const distance)
	{
		SOLDIERTYPE dummy{};
		dummy.bLevel = 0;
		dummy.bTeam  = ENEMY_TEAM;
		dummy.sGridNo = anchor;

		GridNo best = NOWHERE;
		for (GridNo const g : GridSquare{anchor, distance})
		{
			if (SpacesAway(anchor, g) != distance) continue;
			if (!GridNoOnVisibleWorldTile(g)) continue;
			if (!NewOKDestination(&dummy, g, TRUE, 0)) continue;
			if (!LocationToLocationLineOfSightTest(anchor, 0, g, 0, 255, TRUE)) continue;
			if (best == NOWHERE || g < best) best = g;
		}
		return best;
	}

	SoldierClass ParseClass(std::string const& name)
	{
		if (name == "administrator" || name == "admin") return SOLDIER_CLASS_ADMINISTRATOR;
		if (name == "army" || name == "troop" || name == "troops") return SOLDIER_CLASS_ARMY;
		if (name == "elite") return SOLDIER_CLASS_ELITE;
		throw std::runtime_error("range lane: unknown target class \"" + name + "\"");
	}

	// Look at a soldier, without running sight or spending anything: a lane is set
	// up, not played.
	void FaceTarget(SOLDIERTYPE& s, SOLDIERTYPE const& target)
	{
		UINT16 const direction = GetDirectionToGridNoFromGridNo(target.sGridNo, s.sGridNo);
		EVENT_SetSoldierDirection(&s, direction);
		EVENT_SetSoldierDesiredDirection(&s, direction);
	}

	// Apply a loadout table to a soldier, turning every refusal into one error that
	// names the soldier and what the rules said.
	void ApplyLoadout(SOLDIERTYPE& s, sol::object const& setup, char const* const what)
	{
		if (!setup.is<sol::table>()) return;
		std::vector<std::string> problems;
		Scenario::ApplyEquipment(s, setup.as<sol::table>(), problems);
		if (problems.empty()) return;
		std::string msg = std::string(what) + " " + s.name.to_std_string() + ": ";
		for (size_t i = 0; i < problems.size(); ++i) msg += (i ? "; " : "") + problems[i];
		throw std::runtime_error(msg);
	}
}

sol::table StageRangeLane(sol::state_view L, sol::table const& spec)
{
	if (guiCurrentScreen != GAME_SCREEN)
		throw std::runtime_error("ja2.debug(\"lane\"): needs the tactical screen");
	if (!gWorldSector.IsValid())
		throw std::runtime_error("ja2.debug(\"lane\"): no sector is loaded");

	// Start from an empty lane before anything else: the recorder must not attribute
	// anything to the lane that was here before, and every value read below is the new
	// lane's rather than the last one's.
	g_lane = Lane{};

	int const distance = std::max(1, Scenario::IntField(spec, "distance", 8));
	sol::object const seedObj = spec["seed"];
	int seed;
	if (Scenario::AsInt(seedObj, seed))
	{
		// A lane that reseeds itself is reproducible on its own: the same spec and
		// seed give the same shots however the sector was reached.
		SetRandomSeed(static_cast<UINT32>(seed));
	}
	else
	{
		InitializeRandom();
	}

	// A lane is its own sector state: drop whatever was standing in it.
	FOR_EACH_IN_TEAM(e, ENEMY_TEAM) TacticalRemoveSoldier(*e);
	EliminateAllEnemies(gWorldSector);
	gTacticalStatus.fEnemyInSector = FALSE;

	// The shooter: our first merc standing in the sector, or the one named.
	sol::object const shooterSpec = spec["shooter"];
	std::string const wantName = Scenario::StrField(
		shooterSpec.is<sol::table>() ? shooterSpec.as<sol::table>() : sol::table(L.create_table()), "name", "");
	SOLDIERTYPE* shooter = nullptr;
	FOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (!s->bInSector || s->bLife <= 0) continue;
		if (wantName.empty() || s->name.to_std_string() == wantName) { shooter = s; break; }
	}
	if (!shooter) throw std::runtime_error("ja2.debug(\"lane\"): no merc to shoot from");

	// The gun in his hand, then his gear. GiveGun first, so `ammo` and `condition`
	// have a weapon to apply to.
	sol::table const shooterTable = shooterSpec.is<sol::table>() ? shooterSpec.as<sol::table>() : sol::table(L.create_table());
	std::string const weaponName = Scenario::StrField(shooterTable, "weapon", "MP5K");
	Scenario::GiveGun(*shooter, Scenario::ItemByName(weaponName));
	Scenario::EquipArmour(*shooter, Scenario::ArmourLevel(shooterTable["armour"], "none"));

	// Put the shooter back on his feet. A matrix runs one lane after another, and a
	// merc who is still bleeding from the last cell is not a controlled shooter.
	sol::object const shooterStats = shooterTable["stats"];
	sol::table stats = shooterStats.is<sol::table>() ? shooterStats.as<sol::table>() : sol::table(L.create_table());
	if (stats["health"] == sol::nil) stats["health"] = 100;
	Scenario::ApplyStats(*shooter, stats);
	shooter->bBleeding      = 0;
	shooter->sDamage        = 0;
	shooter->bActionPoints  = g_lane.apPool;
	shooter->bInitialActionPoints = g_lane.apPool;
	ApplyLoadout(*shooter, shooterSpec, "range lane shooter");

	GridNo const anchor = shooter->sGridNo;
	GridNo const targetGrid = LaneTargetTile(anchor, distance);
	if (targetGrid == NOWHERE)
		throw std::runtime_error("ja2.debug(\"lane\"): no tile " + std::to_string(distance)
			+ " from " + std::to_string(anchor) + " with a clear line of sight");

	// Inside the messy-death range a solid torso hit ends a soldier outright
	// (Weapons.cc: MIN_DAMAGE_FOR_INSTANT_KILL within MAX_DISTANCE_FOR_MESSY_DEATH),
	// and the target is restored between shots rather than during them, so a run of
	// N shots cannot be N measurements that close. Say so instead of failing later
	// with a dead target.
	if (PythSpacesAway(anchor, targetGrid) <= MAX_DISTANCE_FOR_MESSY_DEATH)
		throw std::runtime_error("ja2.debug(\"lane\"): a distance of " + std::to_string(distance)
			+ " is inside the messy-death range (" + std::to_string(MAX_DISTANCE_FOR_MESSY_DEATH)
			+ " tiles), where one solid hit can end a soldier: a lane measures one shot at a"
			  " time, so ask for " + std::to_string(MAX_DISTANCE_FOR_MESSY_DEATH + 1) + " tiles or more");

	// The target: one enemy, exactly `distance` tiles away, in the open.
	sol::object const targetSpec = spec["target"];
	sol::table const targetTable = targetSpec.is<sol::table>() ? targetSpec.as<sol::table>() : sol::table(L.create_table());
	SOLDIERTYPE* const target = TacticalCreateEnemySoldier(
		ParseClass(Scenario::StrField(targetTable, "class", "administrator")));
	if (!target) throw std::runtime_error("ja2.debug(\"lane\"): could not create a target");
	target->sSector = gWorldSector;
	target->sInsertionGridNo = targetGrid;
	target->ubStrategicInsertionCode = INSERTION_CODE_GRIDNO;
	AddSoldierToSector(target);
	EVENT_SetSoldierPosition(target, targetGrid, SSP_NONE);
	target->sFinalDestination = targetGrid;
	FaceTarget(*shooter, *target);

	sol::object const targetArmour = targetTable["armour"];
	Scenario::EquipArmour(*target, Scenario::ArmourLevel(targetArmour, "none"));

	// The target is a training dummy: full health, so a lane measures the shot and
	// not how many rounds it took to drop the man. Its `stats` can still set health,
	// which is how a fixture asks for a tougher dummy.
	sol::object const targetStats = targetTable["stats"];
	sol::table dummyStats = targetStats.is<sol::table>() ? targetStats.as<sol::table>() : sol::table(L.create_table());
	if (dummyStats["health"] == sol::nil) dummyStats["health"] = 100;
	Scenario::ApplyStats(*target, dummyStats);
	ApplyLoadout(*target, targetSpec, "range lane target");

	// The sector's garrison counters have to match the soldiers in it, or the
	// battle-end code sees an inconsistent sector (see StageBattle).
	SECTORINFO& si = SectorInfo[gWorldSector.AsByte()];
	si.ubNumAdmins = si.ubNumTroops = si.ubNumElites = 0;
	FOR_EACH_IN_TEAM(e, ENEMY_TEAM)
	{
		if (!e->bInSector || e->bLife == 0) continue;
		if      (e->ubSoldierClass == SOLDIER_CLASS_ADMINISTRATOR) ++si.ubNumAdmins;
		else if (e->ubSoldierClass == SOLDIER_CLASS_ELITE)         ++si.ubNumElites;
		else                                                       ++si.ubNumTroops;
	}
	si.ubAdminsInBattle = si.ubNumAdmins;
	si.ubTroopsInBattle = si.ubNumTroops;
	si.ubElitesInBattle = si.ubNumElites;

	// Combat first, with our turn, then let everyone look: that settles who sees
	// whom, so the lane does not depend on which side spotted the other.
	if ((gTacticalStatus.uiFlags & INCOMBAT) == 0) EnterCombatMode(OUR_TEAM);
	AllTeamsLookForAll(FALSE);

	g_lane.staged      = true;
	g_lane.shooterId   = shooter->ubID;
	g_lane.targetId    = target->ubID;
	g_lane.shooterName = shooter->name.to_std_string();
	g_lane.targetName  = target->name.to_std_string();
	g_lane.distance    = distance;
	g_lane.actualRange = SpacesAway(anchor, targetGrid);

	sol::table t = L.create_table();
	t["shooter"]     = g_lane.shooterName;
	t["target"]      = g_lane.targetName;
	t["shooterGrid"] = int(anchor);
	t["targetGrid"]  = int(targetGrid);
	t["distance"]    = g_lane.actualRange;
	return t;
}

bool FireLaneShot()
{
	if (!g_lane.staged)
		throw std::runtime_error("ja2.debug(\"laneShot\"): no lane is staged");

	SOLDIERTYPE* const shooter = SoldierById(true, g_lane.shooterId);
	SOLDIERTYPE* const target  = SoldierById(false, g_lane.targetId);
	if (!shooter) throw std::runtime_error("ja2.debug(\"laneShot\"): the lane's shooter is out of the sector");
	if (!target)  throw std::runtime_error("ja2.debug(\"laneShot\"): the lane's target left the sector "
		"(a shot killed it? lower the damage or raise its health)");

	// Every shot is measured against a fresh target: full life, no bleeding and
	// its armour back at full condition, so a run of N shots is N measurements and
	// not a running total. The gun keeps its condition - that is the wear under test.
	target->bLife     = target->bLifeMax;
	target->bBleeding = 0;
	target->sDamage   = 0;
	for (auto const& worn : { UINT8(VESTPOS), UINT8(HELMETPOS), UINT8(LEGPOS) })
	{
		if (target->inv[worn].usItem != NOTHING) target->inv[worn].bStatus[0] = 100;
	}
	if (target->inv[VESTPOS].usItem != NOTHING)
	{
		INT8 const plates = FindPlatesAttachment(&target->inv[VESTPOS]);
		if (plates != NO_SLOT) target->inv[VESTPOS].bAttachStatus[plates] = 100;
	}

	// The AP for the shot: a lane measures the shot, not the turn's bookkeeping.
	shooter->bActionPoints    = g_lane.apPool;
	shooter->bAimTime         = 0;
	shooter->bAimShotLocation = AIM_SHOT_TORSO;
	shooter->target           = target;
	shooter->opponent         = target;
	shooter->bDoBurst         = 0;

	// Face down the lane before ordering. A click on a tile the shooter is not
	// looking at spends its first order turning him (see battle.lua), which a lane
	// has no use for: every order here must be a shot.
	FaceTarget(*shooter, *target);

	if (shooter->inv[HANDPOS].usItem == NOTHING)
		throw std::runtime_error("ja2.debug(\"laneShot\"): the shooter has nothing in his hand");

	// The same entry a click on the tile would take (FireAtGrid): it checks the AP
	// cost and the ammo, then starts the real fire chain. Its verdict is handed back
	// so a fixture can tell "the shot fired" from "the game would not let it".
	ItemHandleResult const result = HandleItem(shooter, target->sGridNo, target->bLevel,
		shooter->inv[HANDPOS].usItem, FALSE);
	g_lastShotOrdered = (result == ITEM_HANDLE_OK);
	g_lastShotRefusal = g_lastShotOrdered ? "" : ShotRefusalName(result);
	return g_lastShotOrdered;
}

sol::table LaneReport(sol::state_view L)
{
	sol::table t = L.create_table();
	if (!g_lane.staged)
		throw std::runtime_error("ja2.lane(): no lane is staged");

	SOLDIERTYPE* const shooter = SoldierById(true, g_lane.shooterId);
	SOLDIERTYPE* const target  = SoldierById(false, g_lane.targetId);

	t["shooter"]     = g_lane.shooterName;
	t["target"]      = g_lane.targetName;
	t["distance"]    = g_lane.actualRange;
	t["targetGrid"]  = target ? int(target->sGridNo) : int(NOWHERE);
	t["targetLife"]  = target ? int(target->bLife) : 0;
	t["condition"]   = shooter ? int(shooter->inv[HANDPOS].bGunStatus) : 0;
	t["rounds"]      = shooter ? int(shooter->inv[HANDPOS].ubGunShotsLeft) : 0;
	t["ammo"]        = shooter ? Scenario::ItemName(shooter->inv[HANDPOS].usGunAmmoItem) : "";
	t["ammoType"]    = shooter ? Scenario::AmmoTypeName(shooter->inv[HANDPOS].ubGunAmmoType) : "";
	t["weapon"]      = shooter ? Scenario::ItemName(shooter->inv[HANDPOS].usItem) : "";
	t["refusal"]     = g_lastShotRefusal;   // why the last order was refused, "" when it was not

	// What the dummy is left wearing. The target is restored before each shot, so this
	// is the wear the last round did to it.
	if (target && target->inv[VESTPOS].usItem != NOTHING)
	{
		sol::table vest = L.create_table();
		vest["item"]      = Scenario::ItemName(target->inv[VESTPOS].usItem);
		vest["condition"] = int(target->inv[VESTPOS].bStatus[0]);
		t["targetArmour"] = vest;
	}

	sol::table shots = L.create_table();
	int hits = 0, impacts = 0, damage = 0, protection = 0, penetrated = 0, noise = 0;
	for (size_t i = 0; i < g_lane.shots.size(); ++i)
	{
		LaneShot const& s = g_lane.shots[i];
		sol::table row = L.create_table();
		row["index"]            = int(i);
		row["chanceToHit"]      = s.chanceToHit;
		row["roll"]             = s.roll;
		row["hit"]              = s.hit;
		row["impacted"]         = s.impacted;
		row["noiseVolume"]      = s.noiseVolume;
		row["conditionBefore"]  = s.conditionBefore;
		row["conditionAfter"]   = s.conditionAfter;
		row["wear"]             = s.conditionBefore - s.conditionAfter;
		row["distance"]         = s.distance;
		row["hitLocation"]      = s.hitLocation;
		row["impactBeforeArmour"] = s.impactBeforeArmour;
		row["armourProtection"] = s.armourProtection;
		row["damage"]           = s.damage;
		row["penetrated"]       = s.penetrated;
		shots[i + 1] = row;
		if (s.hit) ++hits;
		if (s.impacted)
		{
			++impacts;
			damage += s.damage;
			protection += s.armourProtection;
			if (s.penetrated) ++penetrated;
		}
		noise += s.noiseVolume;
	}

	// The totals a fixture asserts on: one row per shot, and the run over them.
	// `hits` counts rolls that connected; `impacts` counts rounds that actually
	// arrived. They differ on purpose - whether the shot was aimed to hit and whether
	// the round reached the body are two separate decisions in the pipeline.
	sol::table summary = L.create_table();
	summary["shots"]      = int(g_lane.shots.size());
	summary["hits"]       = hits;
	summary["impacts"]    = impacts;
	summary["damage"]     = damage;
	summary["protection"] = protection;
	summary["penetrated"] = penetrated;
	summary["noise"]      = noise;
	t["shots"]   = shots;
	t["summary"] = summary;
	return t;
}

} // namespace Automation