#include "CampaignScenario.h"
#include "BattleScenario.h"
#include "ScenarioItems.h"

#include "Assignments.h"
#include "Animation_Data.h"
#include "Auto_Resolve.h"
#include "Campaign_Types.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "GameSettings.h"
#include "Game_Clock.h"
#include "Game_Events.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "LaptopSave.h"
#include "MapScreen.h"
#include "Map_Screen_Interface.h"
#include "Merc_Hiring.h"
#include "MercProfile.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "PeopleContent.h"
#include "Quests.h"
#include "Soldier_Add.h"
#include "Soldier_Control.h"
#include "Soldier_Create.h"
#include "Soldier_Profile.h"
#include "Soldier_Profile_Type.h"
#include "Soldier_Tile.h"
#include "Squads.h"
#include "Strategic.h"
#include "StrategicMap.h"
#include "Strategic_Status.h"
#include "Strategic_Town_Loyalty.h"
#include "Tactical_Placement_GUI.h"
#include "Text.h"
#include "TownModel.h"

#include <magic_enum/magic_enum.hpp>

// magic_enum's default range is -128..127; the campaign enums are larger. These
// specialisations match EnumCodeGen.cc, so the ranges are identical everywhere.
namespace magic_enum::customize
{
	template<> struct enum_range<Fact>   { static constexpr int min = -1; static constexpr int max = 400; };
	template<> struct enum_range<Quests> { static constexpr int min = 0;  static constexpr int max = 500; };
}

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace Automation::Scenario
{

void SetSectorGarrison(const SGPSector& sector, int const admins, int const troops, int const elites)
{
	SECTORINFO& si = SectorInfo[sector.AsByte()];
	si.ubNumAdmins = UINT8(std::clamp(admins, 0, 255));
	si.ubNumTroops = UINT8(std::clamp(troops, 0, 255));
	si.ubNumElites = UINT8(std::clamp(elites, 0, 255));
	si.ubAdminsInBattle = si.ubNumAdmins;
	si.ubTroopsInBattle = si.ubNumTroops;
	si.ubElitesInBattle = si.ubNumElites;
}

int ResolveQuest(std::string const& name)
{
	std::string n = name;
	if (n.rfind("QUEST_", 0) != 0) n = "QUEST_" + n;
	auto const q = magic_enum::enum_cast<Quests>(n);
	if (!q.has_value()) throw std::runtime_error("unknown quest \"" + name + "\"");
	return int(*q);
}

int ResolveFact(std::string const& name)
{
	std::string n = name;
	if (n.rfind("FACT_", 0) != 0) n = "FACT_" + n;
	auto const f = magic_enum::enum_cast<Fact>(n);
	if (!f.has_value()) throw std::runtime_error("unknown fact \"" + name + "\"");
	return int(*f);
}

}

namespace Automation
{

namespace
{
	std::string Lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		return s;
	}

	bool IEquals(std::string const& a, std::string const& b) { return Lower(a) == Lower(b); }

	// --- resolving names to ids -------------------------------------------

	int TownByName(std::string const& name)
	{
		for (auto const& [id, town] : GCM->getTowns())
		{
			if (IEquals(town->internalName.to_std_string(), name) ||
			    IEquals(town->name.to_std_string(), name) ||
			    IEquals(town->nameLocative.to_std_string(), name))
			{
				return id;
			}
		}
		throw std::runtime_error("unknown town \"" + name + "\"");
	}

	int TownId(sol::object const& key)
	{
		if (key.is<int>())
		{
			int const id = key.as<int>();
			if (id > BLANK_SECTOR && id < NUM_TOWNS) return id;
			throw std::runtime_error("unknown town id " + std::to_string(id));
		}
		if (key.is<std::string>()) return TownByName(key.as<std::string>());
		throw std::runtime_error("ja2.debug(\"campaign\"): town keys must be a name or an id");
	}

	ProfileID ProfileByName(std::string const& name)
	{
		for (MercProfile const* const p : GCM->listMercProfiles())
		{
			MERCPROFILESTRUCT const& m = p->getStruct();
			if (IEquals(m.zName.to_std_string(), name) || IEquals(m.zNickname.to_std_string(), name))
			{
				return p->getID();
			}
		}
		// Not a hireable merc: any named profile will do — an NPC like Miguel or Skyrider, whose fate the ending
		// cinematic's chain asks about (docs/ui/intro.md §S3).
		for (int p = 0; p < NUM_PROFILES; ++p)
		{
			MERCPROFILESTRUCT const& m = gMercProfiles[p];
			if (IEquals(m.zName.to_std_string(), name) || IEquals(m.zNickname.to_std_string(), name)) return ProfileID(p);
		}
		// The compiled people registry knows the named NPCs by their internal name ("MIGUEL", "SKYRIDER") even
		// before a campaign has loaded the profiles.
		for (People::NpcDef const& npc : People::NpcDefs())
		{
			if (IEquals(npc.name, name)) return npc.id;
		}
		throw std::runtime_error("unknown merc \"" + name + "\"");
	}

	SGPSector SectorFromString(std::string const& name)
	{
		SGPSector const sector = SGPSector::FromShortString(name);
		if (!sector.IsValid()) throw std::runtime_error("unknown sector \"" + name + "\"");
		return sector;
	}

	int QuestStatus(sol::object const& o)
	{
		if (o.is<int>()) return std::clamp(o.as<int>(), 0, 2);
		if (o.is<std::string>())
		{
			std::string const s = Lower(o.as<std::string>());
			if (s == "done" || s == "complete" || s == "completed") return QUESTDONE;
			if (s == "in_progress" || s == "inprogress" || s == "started" || s == "progress") return QUESTINPROGRESS;
			if (s == "not_started" || s == "notstarted" || s == "none") return QUESTNOTSTARTED;
		}
		throw std::runtime_error("ja2.debug(\"campaign\"): quest status must be not_started, in_progress or done");
	}

	int DifficultyFrom(sol::object const& o)
	{
		if (o.is<int>()) return std::clamp(o.as<int>(), 1, 3);
		if (o.is<std::string>())
		{
			std::string const s = Lower(o.as<std::string>());
			if (s == "easy") return DIF_LEVEL_EASY;
			if (s == "medium" || s == "normal") return DIF_LEVEL_MEDIUM;
			if (s == "hard") return DIF_LEVEL_HARD;
			auto const d = magic_enum::enum_cast<DifficultyLevel>("DIF_LEVEL_" + std::string(o.as<std::string>()));
			if (d.has_value()) return int(*d);
		}
		throw std::runtime_error("ja2.debug(\"campaign\"): difficulty must be 1..3 or easy/medium/hard");
	}

	Assignments AssignmentFrom(std::string const& raw)
	{
		std::string const n = Lower(raw);
		if (n == "squad" || n.rfind("squad", 0) == 0)
		{
			int idx = 1;
			auto const pos = n.find('_');
			if (pos != std::string::npos) idx = std::atoi(n.c_str() + pos + 1);
			idx = std::clamp(idx, 1, 20);
			return Assignments(SQUAD_1 + idx - 1);
		}
		if (n == "on_duty" || n == "onduty") return ON_DUTY;
		if (n == "doctor") return DOCTOR;
		if (n == "patient") return PATIENT;
		if (n == "repair") return REPAIR;
		if (n == "train_self") return TRAIN_SELF;
		if (n == "train_town") return TRAIN_TOWN;
		if (n == "train_teammate") return TRAIN_TEAMMATE;
		if (n == "in_transit") return IN_TRANSIT;
		if (n == "vehicle") return VEHICLE;
		if (n == "hospital") return ASSIGNMENT_HOSPITAL;
		if (n == "pow") return ASSIGNMENT_POW;
		throw std::runtime_error("unknown assignment \"" + raw + "\"");
	}

	void ApplyAssignment(SOLDIERTYPE& s, Assignments const a)
	{
		if (a >= SQUAD_1 && a <= SQUAD_20)
		{
			if (!AddCharacterToSquad(&s, INT8(a - SQUAD_1))) ChangeSoldiersAssignment(&s, a);
			return;
		}
		ChangeSoldiersAssignment(&s, a);
	}

	// --- town / sector helpers --------------------------------------------

	std::vector<SGPSector> TownSectors(int const town)
	{
		std::vector<SGPSector> out;
		for (TownSectorInfo const& ts : g_town_sectors)
		{
			if (ts.town == town) out.push_back(SGPSector::FromSectorID(ts.sector, 0));
		}
		return out;
	}

	void StageTown(sol::object const& key, sol::table const& spec)
	{
		int const town = TownId(key);
		std::vector<SGPSector> const sectors = TownSectors(town);
		if (sectors.empty()) throw std::runtime_error("town " + std::to_string(town) + " has no sectors");

		sol::object const ownedObj = spec["owned"];
		if (ownedObj.is<bool>())
		{
			bool const owned = ownedObj.as<bool>();
			for (SGPSector const& sec : sectors)
			{
				StrategicMap[sec.AsStrategicIndex()].fEnemyControlled = owned ? FALSE : TRUE;
				if (owned)
				{
					SectorInfo[sec.AsByte()].fSurfaceWasEverPlayerControlled = TRUE;
					Scenario::SetSectorGarrison(sec, 0, 0, 0);
				}
			}
		}

		sol::object const loyaltyObj = spec["loyalty"];
		if (loyaltyObj.is<int>())
		{
			gTownLoyalty[town].ubRating = UINT8(std::clamp(loyaltyObj.as<int>(), 0, 100));
			gTownLoyalty[town].fStarted = TRUE;
		}

		sol::object const militiaObj = spec["militia"];
		if (militiaObj.is<sol::table>())
		{
			sol::table const m = militiaObj.as<sol::table>();
			int const green   = Scenario::IntField(m, "green", -1);
			int const regular = Scenario::IntField(m, "regular", -1);
			int const elite   = Scenario::IntField(m, "elite", -1);
			// The spec gives the town's total; spread it over the town's sectors.
			int const n = int(sectors.size());
			auto share = [&](int total, int i) { return total / n + (i < total % n ? 1 : 0); };
			for (int i = 0; i < n; ++i)
			{
				SECTORINFO& si = SectorInfo[sectors[i].AsByte()];
				if (green   >= 0) si.ubNumberOfCivsAtLevel[GREEN_MILITIA]   = UINT8(std::clamp(share(green, i), 0, 255));
				if (regular >= 0) si.ubNumberOfCivsAtLevel[REGULAR_MILITIA] = UINT8(std::clamp(share(regular, i), 0, 255));
				if (elite   >= 0) si.ubNumberOfCivsAtLevel[ELITE_MILITIA]   = UINT8(std::clamp(share(elite, i), 0, 255));
			}
		}
	}

	void StageSector(sol::object const& key, sol::table const& spec)
	{
		if (!key.is<std::string>()) throw std::runtime_error("ja2.debug(\"campaign\"): sector keys are \"A9\" strings");
		SGPSector const sector = SectorFromString(key.as<std::string>());

		sol::object const enemyObj = spec["enemy"];
		bool const enemyGiven = enemyObj.is<bool>();
		bool const enemy = enemyGiven && enemyObj.as<bool>();
		StrategicMapElement& sm = StrategicMap[sector.AsStrategicIndex()];
		if (enemyGiven) sm.fEnemyControlled = enemy ? TRUE : FALSE;

		sol::object const adminsObj = spec["admins"];
		sol::object const troopsObj = spec["troops"];
		sol::object const elitesObj = spec["elites"];
		bool const countsGiven = adminsObj.is<int>() || troopsObj.is<int>() || elitesObj.is<int>();
		int admins = Scenario::IntField(spec, "admins", 0);
		int troops = Scenario::IntField(spec, "troops", 0);
		int elites = Scenario::IntField(spec, "elites", 0);

		// A player-controlled sector keeps no enemy garrison: zero it unless the spec
		// says otherwise, so the counters agree with the control flag.
		if (enemyGiven && !enemy && !countsGiven) { admins = troops = elites = 0; }
		if (enemyGiven || countsGiven) Scenario::SetSectorGarrison(sector, admins, troops, elites);
	}

	void StageMerc(sol::table const& e, SGPSector const& defaultSector)
	{
		std::string const name = Scenario::StrField(e, "name", "");
		if (name.empty()) throw std::runtime_error("ja2.debug(\"campaign\"): a merc needs a name");
		ProfileID const pid = ProfileByName(name);

		// A merc who did not come home (the victory epilogue's "the fallen", docs/ui/epilogue.md): what that page
		// reads is the profile, so mark him without putting a soldier on the team.
		if (Scenario::BoolField(e, "dead", false))
		{
			gMercProfiles[pid].ubMiscFlags |= PROFILE_MISC_FLAG_RECRUITED;
			gMercProfiles[pid].bMercStatus = MERC_IS_DEAD;
			return;
		}

		SGPSector sector = defaultSector;
		sol::object const sectorObj = e["sector"];
		if (sectorObj.is<std::string>()) sector = SectorFromString(sectorObj.as<std::string>());

		SOLDIERTYPE* s = FindSoldierByProfileIDOnPlayerTeam(pid);
		if (!s)
		{
			SOLDIERCREATE_STRUCT cs{};
			cs.ubProfile           = pid;
			cs.bTeam               = OUR_TEAM;
			cs.sSector             = sector;
			cs.fCopyProfileItemsOver = TRUE;
			cs.bDirection          = NORTH;
			s = TacticalCreateSoldier(cs);
			if (!s) throw std::runtime_error("ja2.debug(\"campaign\"): could not create " + name);
			s->ubWhatKindOfMercAmI = MERC_TYPE__AIM_MERC;
		}

		s->sSector = sector;
		s->fBetweenSectors = FALSE;
		ApplyAssignment(*s, AssignmentFrom(Scenario::StrField(e, "assignment", "squad")));
		// on your team: the flag the game sets when a merc is recruited (ChangeSoldierTeam, RecruitRPC)
		gMercProfiles[pid].ubMiscFlags |= PROFILE_MISC_FLAG_RECRUITED;

		int const days = Scenario::IntField(e, "contract_days_left", 7);
		s->iTotalContractLength = days;
		s->uiTimeOfLastContractUpdate = GetWorldTotalMin();
		gMercProfiles[pid].bMercStatus = INT8(std::clamp(days, 1, 127));
		gMercProfiles[pid].sSector = sector;

		std::string const weapon = Scenario::StrField(e, "weapon", "");
		if (!weapon.empty()) Scenario::GiveGun(*s, Scenario::ItemByName(weapon));
		sol::object const armourObj = e["armour"];
		if (armourObj.is<bool>() || armourObj.is<std::string>())
			Scenario::EquipArmour(*s, Scenario::ArmourLevel(armourObj, "none"));
		int const health = Scenario::IntField(e, "health", -1);
		if (health >= 0)
		{
			s->bLifeMax = INT8(std::clamp(health, 1, 100));
			s->bLife    = s->bLifeMax;
		}
		// a wound, for the doctor/patient step: life below the max without lowering it
		int const life = Scenario::IntField(e, "life", -1);
		if (life >= 0) s->bLife = INT8(std::clamp(life, 1, int(s->bLifeMax)));
		// rest: low breath for the sleep step. Both the current breath and the max (the fatigue a
		// good night's sleep restores) are set; a merc is only allowed to sleep below 95 max breath.
		int const energy = Scenario::IntField(e, "energy", -1);
		if (energy >= 0)
		{
			s->bBreathMax = INT8(std::clamp(energy, BREATHMAX_ABSOLUTE_MINIMUM, 100));
			s->bBreath    = s->bBreathMax;
		}
		// hold = "TOOLKIT": into his hand, whatever the pockets take - a repair or doctor step needs the
		// kit in a known slot, and AutoPlaceObject can fail on a soldier carrying a full kit already
		std::string const hold = Scenario::StrField(e, "hold", "");
		if (!hold.empty()) Scenario::PutInSlot(*s, HANDPOS, Scenario::ItemByName(hold), 1);
		sol::object const items = e["items"];
		if (items.is<sol::table>())
		{
			sol::table const list = items.as<sol::table>();
			for (int i = 1; i <= int(list.size()); ++i)
			{
				sol::object const o = list[i];
				if (o.is<std::string>())
				{
					Scenario::GiveItem(*s, Scenario::ItemByName(o.as<std::string>()));
				}
				else if (o.is<sol::table>())
				{
					// { item = "TOOLKIT", count = 1, condition = 60 }: a stack or a neglected item,
					// so a repair/condition test can stage what it needs
					sol::table const it = o.as<sol::table>();
					std::string const name = Scenario::StrField(it, "item", Scenario::StrField(it, "name", ""));
					if (name.empty()) throw std::runtime_error("ja2.debug(\"campaign\"): an item table needs item = \"...\"");
					int const count = std::clamp(Scenario::IntField(it, "count", 1), 1, 255);
					int const condition = std::clamp(Scenario::IntField(it, "condition", 100), 1, 100);
					Scenario::GiveItem(*s, Scenario::ItemByName(name), UINT8(count), UINT8(condition));
				}
			}
		}
	}
}

void StageCampaign(sol::table const& spec)
{
	// 1. world clock (drive guiGameClock, not the derived globals)
	{
		int const day    = Scenario::IntField(spec, "day", -1);
		int const hour   = Scenario::IntField(spec, "hour", -1);
		int const minute = Scenario::IntField(spec, "minute", -1);
		if (day >= 0 || hour >= 0 || minute >= 0)
		{
			UINT32 const d = day    >= 0 ? UINT32(day)    : GetWorldDay();
			UINT32 const h = hour   >= 0 ? UINT32(hour)   : GetWorldHour();
			UINT32 const m = minute >= 0 ? UINT32(minute) : GetWorldMinutesInDay() % 60;
			guiGameClock = d * NUM_SEC_IN_DAY + h * NUM_SEC_IN_HOUR + m * NUM_SEC_IN_MIN;
			UpdateGameClockGlobals(ST::string("Day"));
		}
	}

	// 2. money and difficulty
	sol::object const moneyObj = spec["money"];
	if (moneyObj.is<int>()) LaptopSaveInfo.iCurrentBalance = moneyObj.as<int>();
	sol::object const difficultyObj = spec["difficulty"];
	if (difficultyObj.valid() && difficultyObj != sol::lua_nil)
		gGameOptions.ubDifficultyLevel = UINT8(DifficultyFrom(difficultyObj));

	// 3. towns
	sol::object const townsObj = spec["towns"];
	if (townsObj.is<sol::table>())
	{
		for (auto const& [key, value] : townsObj.as<sol::table>())
		{
			if (value.is<sol::table>()) StageTown(key, value.as<sol::table>());
		}
	}

	// 4. sectors
	sol::object const sectorsObj = spec["sectors"];
	if (sectorsObj.is<sol::table>())
	{
		for (auto const& [key, value] : sectorsObj.as<sol::table>())
		{
			if (value.is<sol::table>()) StageSector(key, value.as<sol::table>());
		}
	}

	// 5. the merc roster, in the sector the first merc names (default A9)
	sol::object const mercsObj = spec["mercs"];
	if (mercsObj.is<sol::table>())
	{
		sol::table const mercs = mercsObj.as<sol::table>();
		SGPSector defaultSector(9, 1);
		sol::object const firstSector = mercs[1];
		if (firstSector.is<sol::table>())
		{
			sol::object const sec = firstSector.as<sol::table>()["sector"];
			if (sec.is<std::string>()) defaultSector = SectorFromString(sec.as<std::string>());
		}
		for (int i = 1; i <= int(mercs.size()); ++i)
		{
			sol::object const e = mercs[i];
			if (e.is<sol::table>()) StageMerc(e.as<sol::table>(), defaultSector);
		}
	}

	// 6. quest and fact progress
	sol::object const progressObj = spec["progress"];
	if (progressObj.is<sol::table>())
	{
		sol::table const progress = progressObj.as<sol::table>();
		sol::object const questsObj = progress["quests"];
		if (questsObj.is<sol::table>())
		{
			for (auto const& [key, value] : questsObj.as<sol::table>())
			{
				if (!key.is<std::string>()) throw std::runtime_error("ja2.debug(\"campaign\"): quest keys are names");
				gubQuest[Scenario::ResolveQuest(key.as<std::string>())] = UINT8(QuestStatus(value));
			}
		}
		sol::object const factsObj = progress["facts"];
		if (factsObj.is<sol::table>())
		{
			for (auto const& [key, value] : factsObj.as<sol::table>())
			{
				if (!key.is<std::string>()) throw std::runtime_error("ja2.debug(\"campaign\"): fact keys are names");
				gubFact[Scenario::ResolveFact(key.as<std::string>())] = value.as<bool>() ? TRUE : FALSE;
			}
		}
	}

	// 6b. kills: the enemy dead by rank (the victory epilogue reads them, docs/ui/epilogue.md)
	sol::object const killsObj = spec["kills"];
	if (killsObj.is<sol::table>())
	{
		sol::table const k = killsObj.as<sol::table>();
		struct { char const* key; int rank; } const ranks[] = {
			{ "admins", ENEMY_RANK_ADMIN }, { "troops", ENEMY_RANK_TROOP }, { "elites", ENEMY_RANK_ELITE },
		};
		for (auto const& r : ranks)
		{
			int const n = Scenario::IntField(k, r.key, -1);
			if (n >= 0) gStrategicStatus.usEnemiesKilled[ENEMY_KILLED_TOTAL][r.rank] = UINT16(std::clamp(n, 0, 65535));
		}
		int const player = Scenario::IntField(k, "player", -1);
		if (player >= 0) gStrategicStatus.usPlayerKills = UINT16(std::clamp(player, 0, 65535));
	}

	// 7. rebase the strategic event queue on the staged clock. Staging jumps the clock (a new game
	// starts on day 1), and every event scheduled before the staged "now" would otherwise fire at
	// once when the clock next runs - replaying days of missed hourly updates (each of which
	// fatigues every merc, so a staged squad starts exhausted). Periodic and daily events keep
	// their cadence from the staged now; one-shot events that were missed are dropped: the spec
	// states where the campaign is, it does not replay how it got there.
	{
		UINT32 const now = GetWorldTotalSeconds();
		for (STRATEGICEVENT* e = gpEventList; e; e = e->next)
		{
			if (e->uiTimeStamp >= now) continue;
			if (e->ubEventType == PERIODIC_EVENT || e->ubEventType == EVERYDAY_EVENT || e->ubEventType == RANGED_EVENT)
				e->uiTimeStamp = now + e->uiTimeOffset;
			else
				e->ubFlags |= SEF_DELETION_PENDING;
		}
		DeletePendingStrategicEvents();
	}

	// 8. reconcile the roster with the profiles, then rebuild the map-screen list
	FOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (!s->bActive || s->ubProfile >= NUM_PROFILES) continue;
		gMercProfiles[s->ubProfile].sSector = s->sSector;
	}
	// A staged campaign is not a game that just started: in normal play the landing flow clears this flag,
	// and until it is cleared the map screen ignores clicks (MapScreen.cc checks DidGameJustStart()).
	gTacticalStatus.fDidGameJustStart = FALSE;
	// Time compression is gated on having hired a merc ever (TellPlayerWhyHeCantCompressTime): a staged
	// roster counts, or no world-map step that runs the clock would work.
	gfAtLeastOneMercWasHired = FALSE;
	FOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (s->bActive && s->bLife > 0) { gfAtLeastOneMercWasHired = TRUE; break; }
	}
	ReBuildCharactersList();
}

sol::table CampaignState(sol::state& L)
{
	sol::table s = L.create_table();
	s["day"]        = GetWorldDay();
	s["hour"]       = GetWorldHour();
	s["minute"]     = GetWorldMinutesInDay() % 60;
	s["money"]      = LaptopSaveInfo.iCurrentBalance;
	s["difficulty"] = int(gGameOptions.ubDifficultyLevel);

	sol::table towns = L.create_table();
	for (auto const& [id, town] : GCM->getTowns())
	{
		std::vector<SGPSector> const sectors = TownSectors(id);
		bool owned = !sectors.empty();
		int green = 0, regular = 0, elite = 0;
		sol::table list = L.create_table();
		int n = 1;
		for (SGPSector const& sec : sectors)
		{
			SECTORINFO const& si = SectorInfo[sec.AsByte()];
			if (StrategicMap[sec.AsStrategicIndex()].fEnemyControlled) owned = false;
			green   += si.ubNumberOfCivsAtLevel[GREEN_MILITIA];
			regular += si.ubNumberOfCivsAtLevel[REGULAR_MILITIA];
			elite   += si.ubNumberOfCivsAtLevel[ELITE_MILITIA];
			list[n++] = sec.AsShortString().to_std_string();
		}
		sol::table t = L.create_table();
		t["id"]       = id;
		t["name"]     = town->internalName.to_std_string();
		t["owned"]    = owned;
		t["loyalty"]  = int(gTownLoyalty[id].ubRating);
		t["started"]  = bool(gTownLoyalty[id].fStarted);
		t["sectors"]  = list;
		t["militia"]  = L.create_table_with("green", green, "regular", regular, "elite", elite);
		towns[town->internalName.to_std_string()] = t;
	}
	s["towns"] = towns;

	sol::table sectors = L.create_table();
	for (int y = 1; y <= 16; ++y)
	{
		for (int x = 1; x <= 16; ++x)
		{
			SGPSector const sec(x, y);
			SECTORINFO const& si = SectorInfo[sec.AsByte()];
			sol::table t = L.create_table();
			t["enemy"]  = bool(StrategicMap[sec.AsStrategicIndex()].fEnemyControlled);
			t["admins"] = int(si.ubNumAdmins);
			t["troops"] = int(si.ubNumTroops);
			t["elites"] = int(si.ubNumElites);
			sectors[sec.AsShortString().to_std_string()] = t;
		}
	}
	s["sectors"] = sectors;

	sol::table mercs = L.create_table();
	int mi = 1;
	CFOR_EACH_IN_TEAM(m, OUR_TEAM)
	{
		sol::table t = L.create_table();
		t["name"]           = m->name.to_std_string();
		t["profile"]        = int(m->ubProfile);
		t["sector"]         = m->sSector.AsShortString().to_std_string();
		t["betweenSectors"] = m->fBetweenSectors != 0;
		t["assignment"]     = int(m->bAssignment);
		if (m->bAssignment >= 0 && m->bAssignment <= ASSIGNMENT_EMPTY)
			t["assignmentName"] = pAssignmentStrings[m->bAssignment].to_std_string();
		t["life"]           = int(m->bLife);
		t["lifeMax"]        = int(m->bLifeMax);
		t["energy"]         = int(m->bBreath);
		t["contractDaysLeft"] = int(m->iTotalContractLength);
		sol::table items = L.create_table();
		int ii = 1;
		for (UINT8 slot = 0; slot < NUM_INV_SLOTS; ++slot)
		{
			if (m->inv[slot].usItem == NOTHING) continue;
			sol::table it = L.create_table();
			it["slot"]  = int(slot);
			it["item"]  = Scenario::ItemName(m->inv[slot].usItem);
			it["count"] = int(m->inv[slot].ubNumberOfObjects);
			it["condition"] = int(m->inv[slot].bStatus[0]);
			items[ii++] = it;
		}
		t["items"] = items;
		mercs[mi++] = t;
	}
	s["mercs"] = mercs;

	sol::table quests = L.create_table();
	for (int i = 0; i < MAX_QUESTS; ++i)
	{
		std::string_view const n = magic_enum::enum_name(Quests(i));
		if (n.empty()) continue;
		// Key by the name the spec uses (HELD_IN_ALMA), without the QUEST_ prefix.
		std::string key(n);
		if (key.rfind("QUEST_", 0) == 0) key = key.substr(6);
		quests[key] = int(gubQuest[i]);
	}
	s["quests"] = quests;

	sol::table facts = L.create_table();
	for (int i = 0; i < NUM_FACTS; ++i)
	{
		if (!gubFact[i]) continue;
		std::string_view const n = magic_enum::enum_name(Fact(i));
		facts[n.empty() ? ("FACT_" + std::to_string(i)) : std::string(n)] = true;
	}
	s["facts"] = facts;

	return s;
}

void EnterSector(sol::table const& spec)
{
	sol::object const sectorObj = spec["sector"];
	if (!sectorObj.is<std::string>()) throw std::runtime_error("ja2.debug(\"entersector\", spec): needs sector = \"A9\"");
	SGPSector const sector = SectorFromString(sectorObj.as<std::string>());

	sol::object const battleObj = spec["battle"];
	bool clearEnemies = Scenario::BoolField(spec, "clear_enemies", battleObj.is<sol::table>());
	if (clearEnemies)
	{
		Scenario::SetSectorGarrison(sector, 0, 0, 0);
		StrategicMap[sector.AsStrategicIndex()].fEnemyControlled = FALSE;
	}

	// Move the whole team to the sector so the map/strategic state agrees.
	FOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (!s->bActive || s->bLife <= 0) continue;
		s->sSector = sector;
		s->fBetweenSectors = FALSE;
		if (s->bAssignment == IN_TRANSIT) ChangeSoldiersAssignment(s, ON_DUTY);
	}

	// Never land in the placement GUI: this is a scripted entry.
	gfEnterTacticalPlacementGUI = FALSE;
	SetCurrentWorldSector(sector);
}

void SpawnNpcs(sol::object const& spec)
{
	if (gWorldSector.IsValid() == false) throw std::runtime_error("ja2.debug(\"npcs\"): no sector is loaded");

	int count = 3;
	std::vector<ProfileID> profiles;
	if (spec.is<int>()) count = spec.as<int>();
	else if (spec.is<sol::table>())
	{
		sol::table const t = spec.as<sol::table>();
		count = Scenario::IntField(t, "count", 3);
		sol::object const list = t["profiles"];
		if (list.is<sol::table>())
		{
			sol::table const l = list.as<sol::table>();
			for (int i = 1; i <= int(l.size()); ++i)
			{
				sol::object const o = l[i];
				if (o.is<std::string>()) profiles.push_back(ProfileByName(o.as<std::string>()));
			}
		}
	}

	SOLDIERTYPE* anchor = GetSelectedMan();
	if (!anchor || !anchor->bInSector)
	{
		anchor = nullptr;
		FOR_EACH_IN_TEAM(s, OUR_TEAM) { if (s->bInSector && s->bLife > 0) { anchor = s; break; } }
	}
	if (!anchor) throw std::runtime_error("ja2.debug(\"npcs\"): no merc is in the sector");

	// Spawn to the south and sides first: a sector entry can clamp the camera to the
	// map edge, so townsfolk to the north may stand off screen.
	static const WorldDirections dirs[8] = { SOUTH, EAST, WEST, SOUTHEAST, SOUTHWEST, NORTHEAST, NORTH, NORTHWEST };
	int spawned = 0;
	for (int i = 0; i < count || i < int(profiles.size()); ++i)
	{
		GridNo grid = NewGridNo(anchor->sGridNo, DirectionInc(dirs[i % 8]));
		if (grid == NOWHERE) grid = anchor->sGridNo;

		SOLDIERCREATE_STRUCT cs{};
		cs.bTeam            = CIV_TEAM;
		cs.sSector          = gWorldSector;
		cs.sInsertionGridNo = grid;
		cs.bDirection       = SOUTH;
		cs.bOrders          = STATIONARY;
		cs.bAttitude        = DEFENSIVE;
		cs.ubCivilianGroup  = NON_CIV_GROUP;
		if (i < int(profiles.size()))
		{
			cs.ubProfile = profiles[i];
		}
		else
		{
			cs.ubProfile = NO_PROFILE;
			cs.bBodyType = (i % 2) ? REGFEMALE : REGMALE;
			RandomizeNewSoldierStats(&cs);
		}
		SOLDIERTYPE* const npc = TacticalCreateSoldier(cs);
		if (!npc) continue;
		AddSoldierToSector(npc);
		++spawned;
	}
	if (spawned == 0) throw std::runtime_error("ja2.debug(\"npcs\"): could not spawn anyone");
}

}
