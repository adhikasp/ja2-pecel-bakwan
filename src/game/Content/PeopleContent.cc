// The native people & quest registry (issue #156), seeded once by
// tools/generate_people_content.py from the four legacy layers:
//   mercs-profile-info.json, script-records-NPCs.json (+ the binary .npc records),
//   Quests.h, QuestText.cc and strategic-map-npc-placements.json.
// This file is now the source of truth; edit it by hand. Re-running the generator
// re-seeds from the legacy layers and discards those edits (its --check mode guards it).

#include "PeopleContent.h"
#include "Soldier_Profile.h"

namespace People
{

std::vector<NpcDef> const& NpcDefs()
{
	static std::vector<NpcDef> const defs = {
		{BARRY, NpcKind::Aim, "BARRY", "", false, 0, 0, {}},
		{BLOOD, NpcKind::Aim, "BLOOD", "", false, 0, 0, {}},
		{LYNX, NpcKind::Aim, "LYNX", "", false, 0, 0, {}},
		{GRIZZLY, NpcKind::Aim, "GRIZZLY", "", false, 0, 0, {}},
		{VICKY, NpcKind::Aim, "VICKY", "", false, 0, 0, {}},
		{TREVOR, NpcKind::Aim, "TREVOR", "", false, 0, 0, {}},
		{GRUNTY, NpcKind::Aim, "GRUNTY", "", false, 0, 0, {}},
		{IVAN, NpcKind::Aim, "IVAN", "", false, 0, 0, {}},
		{STEROID, NpcKind::Aim, "STEROID", "", false, 0, 0, {}},
		{IGOR, NpcKind::Aim, "IGOR", "", false, 0, 0, {}},
		{SHADOW, NpcKind::Aim, "SHADOW", "", false, 0, 0, {}},
		{RED, NpcKind::Aim, "RED", "", false, 0, 0, {}},
		{REAPER, NpcKind::Aim, "REAPER", "", false, 0, 0, {}},
		{FIDEL, NpcKind::Aim, "FIDEL", "", false, 0, 0, {}},
		{FOX, NpcKind::Aim, "FOX", "", false, 0, 0, {}},
		{SIDNEY, NpcKind::Aim, "SIDNEY", "", false, 0, 0, {}},
		{GUS, NpcKind::Aim, "GUS", "", false, 0, 0, {}},
		{BUNS, NpcKind::Aim, "BUNS", "", false, 0, 0, {}},
		{ICE, NpcKind::Aim, "ICE", "", false, 0, 0, {}},
		{SPIDER, NpcKind::Aim, "SPIDER", "", false, 0, 0, {}},
		{CLIFF, NpcKind::Aim, "CLIFF", "", false, 0, 0, {}},
		{21, NpcKind::Aim, "BULL", "", false, 0, 0, {}},
		{22, NpcKind::Aim, "HITMAN", "", false, 0, 0, {}},
		{23, NpcKind::Aim, "BUZZ", "", false, 0, 0, {}},
		{24, NpcKind::Aim, "RAIDER", "", false, 0, 0, {}},
		{RAVEN, NpcKind::Aim, "RAVEN", "", false, 0, 0, {}},
		{26, NpcKind::Aim, "STATIC", "", false, 0, 0, {}},
		{27, NpcKind::Aim, "LEN", "", false, 0, 0, {}},
		{28, NpcKind::Aim, "DANNY", "", false, 0, 0, {}},
		{MAGIC, NpcKind::Aim, "MAGIC", "", false, 0, 0, {}},
		{30, NpcKind::Aim, "STEPHEN", "", false, 0, 0, {}},
		{31, NpcKind::Aim, "SCULLY", "", false, 0, 0, {}},
		{32, NpcKind::Aim, "MALICE", "", false, 0, 0, {}},
		{DR_Q, NpcKind::Aim, "DR_Q", "", false, 0, 0, {}},
		{NAILS, NpcKind::Aim, "NAILS", "", false, 0, 0, {}},
		{35, NpcKind::Aim, "THOR", "", false, 0, 0, {}},
		{SCOPE, NpcKind::Aim, "SCOPE", "", false, 0, 0, {}},
		{37, NpcKind::Aim, "WOLF", "", false, 0, 0, {}},
		{38, NpcKind::Aim, "MD", "", false, 0, 0, {}},
		{39, NpcKind::Aim, "MELTDOWN", "", false, 0, 0, {}},
		{BIFF, NpcKind::Merc, "BIFF", "", false, 0, 0, {}},
		{HAYWIRE, NpcKind::Merc, "HAYWIRE", "", false, 0, 0, {}},
		{GASKET, NpcKind::Merc, "GASKET", "", false, 0, 0, {}},
		{RAZOR, NpcKind::Merc, "RAZOR", "", false, 0, 0, {}},
		{FLO, NpcKind::Merc, "FLO", "", false, 0, 0, {}},
		{GUMPY, NpcKind::Merc, "GUMPY", "", false, 0, 0, {}},
		{LARRY_NORMAL, NpcKind::Merc, "LARRY_NORMAL", "", false, 0, 0, {}},
		{LARRY_DRUNK, NpcKind::Merc, "LARRY_DRUNK", "", false, 0, 0, {}},
		{COUGAR, NpcKind::Merc, "COUGAR", "", false, 0, 0, {}},
		{NUMB, NpcKind::Merc, "NUMB", "", false, 0, 0, {}},
		{BUBBA, NpcKind::Merc, "BUBBA", "", false, 0, 0, {}},
		{MIGUEL, NpcKind::Rpc, "MIGUEL", "", false, 0, 0, {{QUEST_FOOD_ROUTE, QuestRole::Giver}, {QUEST_DELIVER_LETTER, QuestRole::Resolver}, {QUEST_DELIVER_LETTER, QuestRole::Dialogue}, {QUEST_FOOD_ROUTE, QuestRole::Dialogue}}},
		{CARLOS, NpcKind::Rpc, "CARLOS", "", false, 0, 0, {{QUEST_DELIVER_LETTER, QuestRole::Dialogue}, {QUEST_FOOD_ROUTE, QuestRole::Dialogue}}},
		{IRA, NpcKind::Rpc, "IRA", "", false, 0, 0, {}},
		{DIMITRI, NpcKind::Rpc, "DIMITRI", "", false, 0, 0, {{QUEST_DELIVER_LETTER, QuestRole::Dialogue}}},
		{DEVIN, NpcKind::Rpc, "DEVIN", "G9,D13,C5,H2,C6", false, 0, 0, {}},
		{ROBOT, NpcKind::Rpc, "ROBOT", "", false, 0, 0, {}},
		{HAMOUS, NpcKind::Rpc, "HAMOUS", "G6,F12,D7,D3,D9", false, 0, 0, {}},
		{SLAY, NpcKind::Rpc, "SLAY", "", false, 0, 0, {{QUEST_KILL_TERRORISTS, QuestRole::Dialogue}}},
		{RPC65, NpcKind::Rpc, "RPC65", "", false, 0, 0, {}},
		{DYNAMO, NpcKind::Rpc, "DYNAMO", "", false, 0, 0, {{QUEST_DELIVER_LETTER, QuestRole::Dialogue}}},
		{SHANK, NpcKind::Rpc, "SHANK", "", false, 0, 0, {{QUEST_FREE_SHANK, QuestRole::Giver}}},
		{IGGY, NpcKind::Rpc, "IGGY", "", false, 0, 0, {}},
		{VINCE, NpcKind::Rpc, "VINCE", "", false, 0, 0, {}},
		{CONRAD, NpcKind::Rpc, "CONRAD", "", false, 0, 0, {}},
		{RPC71, NpcKind::Rpc, "RPC71", "", false, 0, 0, {}},
		{MADDOG, NpcKind::Rpc, "MADDOG", "", false, 0, 0, {}},
		{DARREL, NpcKind::Rpc, "DARREL", "", false, 0, 0, {}},
		{PERKO, NpcKind::Rpc, "PERKO", "", false, 0, 0, {}},
		{QUEEN, NpcKind::Npc, "QUEEN", "", false, 0, 0, {}},
		{AUNTIE, NpcKind::Npc, "AUNTIE", "", false, 0, 0, {{QUEST_BLOODCATS, QuestRole::Giver}, {QUEST_BLOODCATS, QuestRole::Resolver}, {QUEST_BLOODCATS, QuestRole::Dialogue}}},
		{ENRICO, NpcKind::Npc, "ENRICO", "", false, 0, 0, {}},
		{CARMEN, NpcKind::Npc, "CARMEN", "C5,C13,G9", false, 0, 0, {{QUEST_KILL_TERRORISTS, QuestRole::Giver}, {QUEST_KILL_TERRORISTS, QuestRole::Dialogue}}},
		{JOE, NpcKind::Npc, "JOE", "", false, 0, 0, {}},
		{STEVE, NpcKind::Npc, "STEVE", "", false, 0, 0, {}},
		{RAT, NpcKind::Npc, "RAT", "", false, 0, 0, {{QUEST_FIND_HERMIT, QuestRole::Giver}}},
		{ANNIE, NpcKind::Npc, "ANNIE", "", false, 0, 0, {}},
		{CHRIS, NpcKind::Npc, "CHRIS", "", false, 0, 0, {}},
		{BOB, NpcKind::Npc, "BOB", "F8", true, 0, 0, {}},
		{BRENDA, NpcKind::Npc, "BRENDA", "", false, 0, 0, {}},
		{KINGPIN, NpcKind::Npc, "KINGPIN", "", false, 0, 0, {{QUEST_KINGPIN_IDOL, QuestRole::Giver}, {QUEST_KINGPIN_MONEY, QuestRole::Giver}, {QUEST_KINGPIN_IDOL, QuestRole::Resolver}, {QUEST_KINGPIN_MONEY, QuestRole::Resolver}, {QUEST_KINGPIN_IDOL, QuestRole::Dialogue}, {QUEST_KINGPIN_MONEY, QuestRole::Dialogue}}},
		{DARREN, NpcKind::Npc, "DARREN", "", false, 0, 0, {{QUEST_KINGPIN_MONEY, QuestRole::Giver}, {QUEST_KINGPIN_MONEY, QuestRole::Dialogue}}},
		{MARIA, NpcKind::Npc, "MARIA", "", false, 0, 0, {{QUEST_DELIVER_LETTER, QuestRole::Dialogue}, {QUEST_RESCUE_MARIA, QuestRole::Dialogue}}},
		{ANGEL, NpcKind::Npc, "ANGEL", "", false, 0, 0, {{QUEST_RESCUE_MARIA, QuestRole::Giver}, {QUEST_RESCUE_MARIA, QuestRole::Resolver}, {QUEST_RESCUE_MARIA, QuestRole::Dialogue}}},
		{JOEY, NpcKind::Npc, "JOEY", "", false, 0, 0, {{QUEST_RUNAWAY_JOEY, QuestRole::Resolver}, {QUEST_RUNAWAY_JOEY, QuestRole::Dialogue}}},
		{TONY, NpcKind::Npc, "TONY", "", false, 0, 0, {}},
		{FRANK, NpcKind::Npc, "FRANK", "", false, 0, 0, {{QUEST_KINGPIN_MONEY, QuestRole::Giver}, {QUEST_KINGPIN_MONEY, QuestRole::Dialogue}, {QUEST_RESCUE_MARIA, QuestRole::Dialogue}}},
		{SPIKE, NpcKind::Npc, "SPIKE", "", false, 0, 0, {{QUEST_KINGPIN_MONEY, QuestRole::Dialogue}}},
		{DAMON, NpcKind::Npc, "DAMON", "", false, 0, 0, {{QUEST_KINGPIN_MONEY, QuestRole::Dialogue}}},
		{KYLE, NpcKind::Npc, "KYLE", "", false, 0, 0, {{QUEST_LEATHER_SHOP_DREAM, QuestRole::Giver}, {QUEST_LEATHER_SHOP_DREAM, QuestRole::Resolver}, {QUEST_LEATHER_SHOP_DREAM, QuestRole::Dialogue}}},
		{MICKY, NpcKind::Npc, "MICKY", "G9,D13,C5,H2,C6", true, 0, 0, {}},
		{SKYRIDER, NpcKind::Npc, "SKYRIDER", "B15,E14,D12,C16", true, 0, 0, {{QUEST_CHOPPER_PILOT, QuestRole::Resolver}, {QUEST_ESCORT_SKYRIDER, QuestRole::Resolver}, {QUEST_ESCORT_SKYRIDER, QuestRole::Dialogue}}},
		{PABLO, NpcKind::Npc, "PABLO", "", false, 0, 0, {}},
		{SAL, NpcKind::Npc, "SAL", "", false, 0, 0, {}},
		{FATHER, NpcKind::Npc, "FATHER", "", false, 0, 0, {{QUEST_FOOD_ROUTE, QuestRole::Dialogue}}},
		{FATIMA, NpcKind::Npc, "FATIMA", "", false, 0, 0, {{QUEST_DELIVER_LETTER, QuestRole::Dialogue}}},
		{WARDEN, NpcKind::Npc, "WARDEN", "", false, 0, 0, {}},
		{GORDON, NpcKind::Npc, "GORDON", "", false, 0, 0, {}},
		{GABBY, NpcKind::Npc, "GABBY", "H11,I4", true, 0, 0, {{QUEST_FIND_HERMIT, QuestRole::Resolver}}},
		{ERNEST, NpcKind::Npc, "ERNEST", "", false, 0, 0, {}},
		{FRED, NpcKind::Npc, "FRED", "", false, 0, 0, {{QUEST_CREATURES, QuestRole::Giver}}},
		{MADAME, NpcKind::Npc, "MADAME", "", false, 0, 0, {{QUEST_RESCUE_MARIA, QuestRole::Dialogue}}},
		{YANNI, NpcKind::Npc, "YANNI", "", false, 0, 0, {{QUEST_CHITZENA_IDOL, QuestRole::Giver}, {QUEST_CHITZENA_IDOL, QuestRole::Resolver}, {QUEST_CHITZENA_IDOL, QuestRole::Dialogue}}},
		{MARTHA, NpcKind::Npc, "MARTHA", "", false, 0, 0, {{QUEST_RUNAWAY_JOEY, QuestRole::Giver}, {QUEST_RUNAWAY_JOEY, QuestRole::Dialogue}}},
		{TIFFANY, NpcKind::Npc, "TIFFANY", "", false, 0, 0, {}},
		{T_REX, NpcKind::Npc, "T_REX", "", false, 0, 0, {}},
		{DRUGGIST, NpcKind::Npc, "DRUGGIST", "", false, 0, 0, {}},
		{JAKE, NpcKind::Npc, "JAKE", "", false, 0, 0, {{QUEST_FREE_SHANK, QuestRole::Resolver}}},
		{PACOS, NpcKind::Npc, "PACOS", "", false, 0, 0, {{QUEST_DELIVER_LETTER, QuestRole::Dialogue}}},
		{GERARD, NpcKind::Npc, "GERARD", "", false, 0, 0, {}},
		{SKIPPER, NpcKind::Npc, "SKIPPER", "", false, 0, 0, {}},
		{HANS, NpcKind::Npc, "HANS", "", false, 0, 0, {{QUEST_RUNAWAY_JOEY, QuestRole::Dialogue}}},
		{JOHN, NpcKind::Npc, "JOHN", "", false, 0, 0, {{QUEST_ESCORT_TOURISTS, QuestRole::Resolver}, {QUEST_ESCORT_TOURISTS, QuestRole::Dialogue}}},
		{MARY, NpcKind::Npc, "MARY", "", false, 0, 0, {{QUEST_ESCORT_TOURISTS, QuestRole::Resolver}, {QUEST_ESCORT_TOURISTS, QuestRole::Dialogue}}},
		{GENERAL, NpcKind::Npc, "GENERAL", "", false, 0, 0, {}},
		{SERGEANT, NpcKind::Npc, "SERGEANT", "", false, 0, 0, {}},
		{ARMAND, NpcKind::Npc, "ARMAND", "", false, 0, 0, {}},
		{LORA, NpcKind::Npc, "LORA", "", false, 0, 0, {}},
		{FRANZ, NpcKind::Npc, "FRANZ", "", false, 0, 0, {}},
		{HOWARD, NpcKind::Npc, "HOWARD", "", false, 0, 0, {}},
		{SAM, NpcKind::Npc, "SAM", "", false, 0, 0, {}},
		{ELDIN, NpcKind::Npc, "ELDIN", "", false, 0, 0, {}},
		{ARNIE, NpcKind::Npc, "ARNIE", "", false, 0, 0, {}},
		{TINA, NpcKind::Npc, "TINA", "", false, 0, 0, {}},
		{FREDO, NpcKind::Npc, "FREDO", "", false, 0, 0, {}},
		{WALTER, NpcKind::Npc, "WALTER", "", false, 0, 0, {}},
		{JENNY, NpcKind::Npc, "JENNY", "", false, 0, 0, {}},
		{BILLY, NpcKind::Npc, "BILLY", "", false, 0, 0, {}},
		{BREWSTER, NpcKind::Npc, "BREWSTER", "", false, 0, 0, {}},
		{ELLIOT, NpcKind::Npc, "ELLIOT", "", false, 0, 0, {}},
		{DEREK, NpcKind::Npc, "DEREK", "", false, 0, 0, {{QUEST_CREATURES, QuestRole::Dialogue}}},
		{OLIVER, NpcKind::Npc, "OLIVER", "", false, 0, 0, {{QUEST_CREATURES, QuestRole::Dialogue}}},
		{WALDO, NpcKind::Npc, "WALDO", "", false, 0, 0, {{QUEST_CHOPPER_PILOT, QuestRole::Giver}}},
		{DOREEN, NpcKind::Npc, "DOREEN", "", false, 0, 0, {{QUEST_FREE_CHILDREN, QuestRole::Giver}, {QUEST_FREE_CHILDREN, QuestRole::Resolver}}},
		{JIM, NpcKind::Npc, "JIM", "", false, 0, 0, {}},
		{JACK, NpcKind::Npc, "JACK", "", false, 0, 0, {}},
		{OLAF, NpcKind::Npc, "OLAF", "", false, 0, 0, {}},
		{RAY, NpcKind::Npc, "RAY", "", false, 0, 0, {}},
		{OLGA, NpcKind::Npc, "OLGA", "", false, 0, 0, {}},
		{TYRONE, NpcKind::Npc, "TYRONE", "", false, 0, 0, {}},
		{MADLAB, NpcKind::Npc, "MADLAB", "H7,H16,I11,E4", false, 0, 0, {{QUEST_DELIVER_VIDEO_CAMERA, QuestRole::Giver}, {QUEST_FIND_SCIENTIST, QuestRole::Resolver}, {QUEST_DELIVER_VIDEO_CAMERA, QuestRole::Resolver}}},
		{KEITH, NpcKind::Npc, "KEITH", "", false, 0, 0, {{QUEST_ARMY_FARM, QuestRole::Giver}, {QUEST_ARMY_FARM, QuestRole::Resolver}, {QUEST_ARMY_FARM, QuestRole::Dialogue}}},
		{MATT, NpcKind::Npc, "MATT", "", false, 0, 0, {{QUEST_CREATURES, QuestRole::Giver}, {QUEST_FREE_DYNAMO, QuestRole::Giver}, {QUEST_FREE_DYNAMO, QuestRole::Resolver}, {QUEST_FREE_DYNAMO, QuestRole::Dialogue}}},
		{MIKE, NpcKind::Npc, "MIKE", "", false, 0, 0, {}},
		{DARYL, NpcKind::Npc, "DARYL", "", false, 0, 0, {}},
		{HERVE, NpcKind::Npc, "HERVE", "", false, 0, 0, {}},
		{PETER, NpcKind::Npc, "PETER", "", false, 0, 0, {}},
		{ALBERTO, NpcKind::Npc, "ALBERTO", "", false, 0, 0, {}},
		{CARLO, NpcKind::Npc, "CARLO", "", false, 0, 0, {}},
		{MANNY, NpcKind::Npc, "MANNY", "", false, 0, 0, {}},
		{OSWALD, NpcKind::Npc, "OSWALD", "", false, 0, 0, {{QUEST_CREATURES, QuestRole::Giver}}},
		{CALVIN, NpcKind::Npc, "CALVIN", "", false, 0, 0, {{QUEST_CREATURES, QuestRole::Giver}}},
		{CARL, NpcKind::Npc, "CARL", "", false, 0, 0, {{QUEST_CREATURES, QuestRole::Giver}}},
		{SPECK, NpcKind::Npc, "SPECK", "", false, 0, 0, {}},
		{PROF_HUMMER, NpcKind::Vehicle, "PROF_HUMMER", "", false, 0, 0, {}},
		{PROF_ELDERODO, NpcKind::Vehicle, "PROF_ELDERODO", "", false, 0, 0, {}},
		{PROF_ICECREAM, NpcKind::Vehicle, "PROF_ICECREAM", "", false, 0, 0, {}},
		{PROF_HELICOPTER, NpcKind::Vehicle, "PROF_HELICOPTER", "", false, 0, 0, {}},
		{NPC164, NpcKind::Reserved, "NPC164", "", false, 0, 0, {}},
	};
	return defs;
}

std::vector<QuestDef> const& QuestDefs()
{
	static std::vector<QuestDef> const defs = {
		{QUEST_DELIVER_LETTER, "DELIVER_LETTER", "Deliver Letter", {}, {MIGUEL}, true, {MIGUEL, CARLOS, DIMITRI, DYNAMO, MARIA, FATIMA, PACOS}, {}, "the Omerta rebels as allies", "", "HISTORY_ACCEPTED_ASSIGNMENT_FROM_ENRICO", "started at game init by CheckForQuests; handed in to Miguel via NPC_ACTION_FATIMA_GIVE_LETTER"},
		{QUEST_FOOD_ROUTE, "FOOD_ROUTE", "Food Route", {MIGUEL}, {FATHER}, false, {MIGUEL, CARLOS, FATHER}, {QUEST_DELIVER_LETTER}, "Miguel's rebels can be hired", "", "FACT_FOOD_QUEST_OVER", "ended by Father's action 112 through a next-day event"},
		{QUEST_KILL_TERRORISTS, "KILL_TERRORISTS", "Terrorists", {CARMEN}, {CARMEN}, false, {SLAY, CARMEN}, {}, "cash for each confirmed kill", "", "FACT_ALL_TERRORISTS_KILLED", "ended when Carmen receives the last head"},
		{QUEST_KINGPIN_IDOL, "KINGPIN_IDOL", "Kingpin Chalice", {KINGPIN}, {KINGPIN}, false, {KINGPIN}, {}, "Kingpin's goodwill", "San Mona", "", ""},
		{QUEST_KINGPIN_MONEY, "KINGPIN_MONEY", "Kingpin Money", {KINGPIN, DARREN, FRANK}, {KINGPIN}, false, {KINGPIN, DARREN, FRANK, SPIKE, DAMON}, {}, "Kingpin's goodwill", "San Mona", "FACT_KINGPIN_KNOWS_MONEY_GONE", "also started by the 'Kingpin knows the money is gone' event, ended when Kingpin dies"},
		{QUEST_RUNAWAY_JOEY, "RUNAWAY_JOEY", "Runaway Joey", {MARTHA}, {JOEY}, false, {JOEY, MARTHA, HANS}, {}, "Martha's gratitude", "", "", ""},
		{QUEST_RESCUE_MARIA, "RESCUE_MARIA", "Rescue Maria", {ANGEL}, {ANGEL}, false, {MARIA, ANGEL, FRANK, MADAME}, {}, "Angel's leather shop", "San Mona", "FACT_MARIA_QUEST_OVER", "EndQuest also resets Madame's brothel state"},
		{QUEST_CHITZENA_IDOL, "CHITZENA_IDOL", "Chitzena Chalice", {YANNI}, {YANNI}, false, {YANNI}, {}, "loyalty in Chitzena", "Chitzena", "", ""},
		{QUEST_HELD_IN_ALMA, "HELD_IN_ALMA", "Held in Alma", {}, {}, true, {}, {}, "freedom", "", "", "started by a Queen meanwhile, ended by the Alma prison event"},
		{QUEST_INTERROGATION, "INTERROGATION", "Interogation", {}, {}, true, {}, {}, "escape", "", "", "started by a Queen meanwhile"},
		{QUEST_ARMY_FARM, "ARMY_FARM", "Hillbilly Problem", {KEITH}, {KEITH}, false, {KEITH}, {}, "Keith's farm", "", "", ""},
		{QUEST_FIND_SCIENTIST, "FIND_SCIENTIST", "Find Scientist", {}, {MADLAB}, true, {}, {}, "Madlab and the robot", "", "", "started by the AWOL_SCIENTIST meanwhile"},
		{QUEST_DELIVER_VIDEO_CAMERA, "DELIVER_VIDEO_CAMERA", "Deliver Video Camera", {MADLAB}, {MADLAB}, false, {}, {}, "the robot", "", "", ""},
		{QUEST_BLOODCATS, "BLOODCATS", "Blood Cats", {AUNTIE}, {AUNTIE}, false, {AUNTIE}, {}, "Auntie's loyalty", "", "FACT_BLOODCAT_QUEST_STARTED_TWO_DAYS_AGO", "Auntie's record carries action 171 (START_BLOODCAT_QUEST); a 2-day timer fact"},
		{QUEST_FIND_HERMIT, "FIND_HERMIT", "Find Hermit", {RAT}, {GABBY}, false, {}, {}, "the hermit's creature-blood vial", "", "", ""},
		{QUEST_CREATURES, "CREATURES", "Creatures", {FRED, MATT, OSWALD, CALVIN, CARL}, {}, true, {DEREK, OLIVER}, {}, "the mines stay productive", "", "", "started by the mine event, ended by the CREATURES meanwhile"},
		{QUEST_CHOPPER_PILOT, "CHOPPER_PILOT", "Find Chopper Pilot", {WALDO}, {SKYRIDER}, false, {}, {}, "Skyrider's helicopter", "", "", ""},
		{QUEST_ESCORT_SKYRIDER, "ESCORT_SKYRIDER", "Escort SkyRider", {}, {SKYRIDER}, true, {SKYRIDER}, {}, "the helicopter service", "", "", "started when Skyrider is escorted"},
		{QUEST_FREE_DYNAMO, "FREE_DYNAMO", "Free Dynamo", {MATT}, {MATT}, false, {MATT}, {}, "Dynamo can be recruited", "", "", ""},
		{QUEST_ESCORT_TOURISTS, "ESCORT_TOURISTS", "Escort Tourists", {}, {JOHN, MARY}, true, {JOHN, MARY}, {}, "tourist gratitude", "", "", "started when John/Mary are escorted"},
		{QUEST_FREE_CHILDREN, "FREE_CHILDREN", "Doreen", {DOREEN}, {DOREEN}, false, {}, {}, "Doreen's change of heart", "", "FACT_DOREEN_HAD_CHANGE_OF_HEART", "also ended when the children are freed"},
		{QUEST_LEATHER_SHOP_DREAM, "LEATHER_SHOP_DREAM", "Leather Shop Dream", {KYLE}, {KYLE}, false, {KYLE}, {}, "Kyle's leather shop", "", "", ""},
		{QUEST_FREE_SHANK, "FREE_SHANK", "Escort Shank", {SHANK}, {JAKE}, false, {}, {}, "Shank can be recruited", "", "", ""},
		{QUEST_KILL_DEIDRANNA, "KILL_DEIDRANNA", "Kill Deidranna", {}, {}, true, {}, {}, "Arulco is free", "", "FACT_QUEEN_DEAD", "endgame only"},
	};
	return defs;
}

NpcDef const* FindNpc(ProfileID id)
{
	for (NpcDef const& def : NpcDefs()) if (def.id == id) return &def;
	return nullptr;
}

QuestDef const* FindQuest(Quests id)
{
	for (QuestDef const& def : QuestDefs()) if (def.id == id) return &def;
	return nullptr;
}

const char* NpcName(ProfileID id)
{
	NpcDef const* const def = FindNpc(id);
	return def ? def->name : "";
}

const char* QuestTitle(Quests id)
{
	QuestDef const* const def = FindQuest(id);
	return def ? def->title : "";
}

const char* NpcKindName(NpcKind kind)
{
	switch (kind)
	{
		case NpcKind::Aim:      return "AIM";
		case NpcKind::Merc:     return "MERC";
		case NpcKind::Imp:      return "IMP";
		case NpcKind::Rpc:      return "RPC";
		case NpcKind::Npc:      return "NPC";
		case NpcKind::Vehicle:  return "VEHICLE";
		case NpcKind::Reserved: return "RESERVED";
	}
	return "";
}

const char* QuestRoleName(QuestRole role)
{
	switch (role)
	{
		case QuestRole::Giver:    return "giver";
		case QuestRole::Resolver: return "resolver";
		case QuestRole::Dialogue: return "dialogue";
	}
	return "";
}

const char* QuestStageName(QuestStage stage)
{
	switch (stage)
	{
		case QuestStage::NotStarted: return "NOT_STARTED";
		case QuestStage::InProgress: return "IN_PROGRESS";
		case QuestStage::Done:       return "DONE";
	}
	return "";
}

void ApplyQuestTransition(QuestTransition const& transition, const SGPSector& sector)
{
	if (transition.change == QuestChange::Start)
	{
		StartQuest(static_cast<UINT8>(transition.quest), sector);
	}
	else
	{
		EndQuest(static_cast<UINT8>(transition.quest), sector);
	}
}

void ApplyQuestTransition(uint8_t rawQuest, QuestChange change, const SGPSector& sector)
{
	if (rawQuest == NO_QUEST) return;
	ApplyQuestTransition({ static_cast<Quests>(rawQuest), change }, sector);
}

bool IsReservedProfile(ProfileID id)
{
	NpcDef const* const def = FindNpc(id);
	return def != nullptr && def->kind == NpcKind::Reserved;
}

} // namespace People
