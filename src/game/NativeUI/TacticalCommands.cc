#include "TacticalCommands.h"

#include <algorithm>
#include <cctype>

// The table of tactical commands and their availability (see TacticalCommands.h). The rules are the ones the legacy
// single-merc panel enabled and disabled its buttons by (UpdateSMPanel) and the key handlers checked before they ran.

namespace TacticalCommands
{

namespace
{
	Availability On() { return {}; }
	Availability Off(Why const w) { return { false, w }; }

	/** A command of the selected soldier: somebody, on his feet, and the enemy not moving. */
	Availability Soldier(Context const& c)
	{
		if (!c.hasMerc)                 return Off(Why::NoMerc);
		if (!c.alive)                   return Off(Why::Dead);
		if (c.combat && !c.ourTurn)     return Off(Why::NotYourTurn);
		return On();
	}

	Availability AlwaysOn(Context const&, Id) { return On(); }

	Availability Stand(Context const& c, Id)
	{
		Availability a = Soldier(c);
		if (a.enabled && c.stance == 0) return Off(Why::Already);
		return a;
	}

	Availability Crouch(Context const& c, Id)
	{
		Availability a = Soldier(c);
		if (!a.enabled) return a;
		if (c.stance == 1) return Off(Why::Already);
		if (!c.canCrouch)  return Off(c.vehicle ? Why::Vehicle : c.robot ? Why::Robot : Why::NoStance);
		return a;
	}

	Availability Prone(Context const& c, Id)
	{
		Availability a = Soldier(c);
		if (!a.enabled) return a;
		if (c.stance == 2) return Off(Why::Already);
		if (!c.canProne)   return Off(c.vehicle ? Why::Vehicle : c.robot ? Why::Robot : Why::NoStance);
		return a;
	}

	Availability Higher(Context const& c, Id)
	{
		Availability a = Soldier(c);
		if (!a.enabled) return a;
		if (c.handFull)   return Off(Why::HandFull);
		if (c.stance == 0) return Off(Why::Already);
		return a;
	}

	Availability Lower(Context const& c, Id)
	{
		Availability a = Soldier(c);
		if (!a.enabled) return a;
		if (c.handFull)   return Off(Why::HandFull);
		if (c.stance == 2) return Off(Why::Already);
		return a;
	}

	Availability RunC(Context const& c, Id)
	{
		Availability a = Soldier(c);
		if (!a.enabled) return a;
		if (c.robot)   return Off(Why::Robot);
		if (c.inWater) return Off(Why::Water);
		return a;
	}

	Availability StealthC(Context const& c, Id)
	{
		Availability a = Soldier(c);
		if (!a.enabled) return a;
		if (c.handFull) return Off(Why::HandFull);
		if (c.robot)    return Off(Why::Robot);
		return a;
	}

	Availability ClimbC(Context const& c, Id)
	{
		Availability a = Soldier(c);
		if (!a.enabled) return a;
		if (c.itemsLocked) return Off(Why::OutOfReach);
		if (!c.canClimb)   return Off(Why::NothingToClimb);
		return a;
	}

	Availability LevelC(Context const& c, Id)
	{
		if (c.handFull) return Off(Why::HandFull);
		if (c.hasMerc && c.alive && c.combat && !c.ourTurn) return Off(Why::NotYourTurn);
		return On();
	}

	Availability BurstC(Context const& c, Id)
	{
		Availability a = Soldier(c);
		if (!a.enabled) return a;
		if (c.handFull)    return Off(Why::HandFull);
		if (c.itemsLocked) return Off(Why::OutOfReach);
		if (!c.canBurst)   return Off(Why::NoBurst);
		return a;
	}

	Availability PointerMode(Context const& c, Id)
	{
		Availability a = Soldier(c);
		if (!a.enabled) return a;
		if (c.handFull)    return Off(Why::HandFull);
		if (c.itemsLocked) return Off(Why::OutOfReach);
		return a;
	}

	Availability Swap(Context const& c, Id)
	{
		if (!c.hasMerc) return Off(Why::NoMerc);
		if (c.robot)    return Off(Why::Robot);
		if (!c.canSwapHands) return Off(Why::EmptyHands);
		return On();
	}

	Availability EndTurnC(Context const& c, Id)
	{
		if (!c.combat) return c.turnBasedAllowed ? On() : Off(Why::NoTurnBased);
		if (!c.ourTurn)       return Off(Why::NotYourTurn);
		if (c.handFull)       return Off(Why::HandFull);
		if (c.panelsLocked)   return Off(Why::Locked);
		return On();
	}

	Availability MapC(Context const& c, Id)
	{
		if (c.handFull)      return Off(Why::HandFull);
		if (c.panelsLocked)  return Off(Why::Locked);
		if (c.conversation)  return Off(Why::Busy);
		if (c.pickupOpen)    return Off(Why::OutOfReach);
		return On();
	}

	Availability OptionsC(Context const& c, Id)
	{
		if (c.handFull)      return Off(Why::HandFull);
		if (c.panelsLocked)  return Off(Why::Locked);
		if (c.mapDisabled)   return Off(Why::Locked);
		if (c.meanwhile)     return Off(Why::Busy);
		return On();
	}

	Availability OverheadC(Context const& c, Id)
	{
		if (c.overhead) return Off(Why::Already);
		return On();
	}

	Availability PauseC(Context const& c, Id)
	{
		if (c.conversation) return Off(Why::Busy);
		return On();
	}

	Availability DetailC(Context const& c, Id)
	{
		if (!c.hasMerc && !c.detailOpen) return Off(Why::NoMerc);
		return On();
	}

	Availability DetailCloseC(Context const& c, Id)
	{
		return c.detailOpen ? On() : Off(Why::NoDetail);
	}

	Availability KeyRingC(Context const& c, Id)
	{
		if (!c.detailOpen) return Off(Why::NoDetail);
		if (c.itemsLocked) return Off(Why::OutOfReach);
		return On();
	}

	Availability MuteC(Context const& c, Id)
	{
		return c.hasMerc ? On() : Off(Why::NoMerc);
	}

	Availability MercNextC(Context const& c, Id)
	{
		if (c.conversation) return Off(Why::Busy);
		if (c.keyRingOpen)  return Off(Why::Busy);
		if (!c.hasMerc)     return Off(Why::NoMerc);
		if (!c.otherMercs)  return Off(Why::Nobody);
		return On();
	}

	Availability SquadNextC(Context const& c, Id)
	{
		if (c.conversation) return Off(Why::Busy);
		if (c.keyRingOpen)  return Off(Why::Busy);
		if (!c.hasMerc)     return Off(Why::NoMerc);
		if (!c.otherSquads) return Off(Why::Nobody);
		return On();
	}

	Availability LocateC(Context const& c, Id)
	{
		return c.hasMerc ? On() : Off(Why::NoMerc);
	}

	Availability MercN(Context const& c, Id const id)
	{
		int const n = static_cast<int>(id) - static_cast<int>(Id::Merc1);
		if (n < 0 || n > 9 || !(c.slotMask & (1u << n))) return Off(Why::Nobody);
		return On();
	}

	std::vector<Command> BuildTable()
	{
		std::vector<Command> t;
		auto add = [&](Id id, char const* name, char const* group, char const* icon, char const* key, char const* hud, AvailabilityFn fn, int arg = 0)
		{
			t.push_back({ id, name, group, "", icon, key, hud, arg, fn });
		};
		add(Id::StanceStand,  "stance.stand",  "stance", "stance-stand",  "s",  "tac.stance.stand",  Stand);
		add(Id::StanceCrouch, "stance.crouch", "stance", "stance-crouch", "c",  "tac.stance.crouch", Crouch);
		add(Id::StanceProne,  "stance.prone",  "stance", "stance-prone",  "p",  "tac.stance.prone",  Prone);
		add(Id::StanceUp,     "stance.up",     "stance", "chevron-up",    "pageup",   "", Higher);
		add(Id::StanceDown,   "stance.down",   "stance", "chevron-down",  "pagedown", "", Lower);
		add(Id::Run,          "move.run",      "move",   "run",           "r",  "tac.move.run",      RunC);
		add(Id::Stealth,      "move.stealth",  "move",   "sneak",         "z",  "tac.stealth",       StealthC);
		add(Id::Climb,        "move.climb",    "move",   "climb",         "j",  "tac.climb",         ClimbC);
		add(Id::Level,        "level.toggle",  "move",   "chevron-up",    "tab", "tac.level",        LevelC);
		add(Id::Burst,        "act.burst",     "act",    "burst",         "b",  "tac.burst",         BurstC);
		add(Id::Look,        "act.look",       "act",    "look",          "l",  "tac.look",          PointerMode);
		add(Id::Talk,         "act.talk",      "act",    "talk",          "",   "tac.talk",          PointerMode);
		add(Id::SwapHands,    "act.swap_hands", "act",   "swap-hands",    "ctrl+q", "tac.detail.swap", Swap);
		add(Id::EndTurn,      "turn.end",      "turn",   "end-turn",      "d",  "tac.endturn",       EndTurnC);
		add(Id::Map,          "nav.map",       "nav",    "map",           "m",  "tac.map",           MapC);
		add(Id::Options,      "nav.options",   "nav",    "options",       "o",  "tac.options",       OptionsC);
		add(Id::Overhead,     "nav.overhead",  "nav",    "map",           "insert", "tac.overhead",  OverheadC);
		add(Id::Pause,        "time.pause",    "turn",   "pause",         "",   "tac.clock",      PauseC); // the Pause key is handled on its release (Turn_Based_Input.cc)
		add(Id::Detail,       "panel.detail",  "panel",  "inventory",     "`",  "tac.inventory",     DetailC);
		add(Id::DetailClose,  "panel.close",   "panel",  "close",         "",   "tac.detail.close",  DetailCloseC);
		add(Id::KeyRing,      "panel.keyring", "panel",  "key",           "k",  "tac.inv.keys",      KeyRingC);
		add(Id::Log,          "panel.log",     "panel",  "menu",          "h",  "tac.log",           AlwaysOn);
		add(Id::Mute,         "merc.mute",     "panel",  "mute",          "",   "tac.detail.mute",   MuteC);
		add(Id::Readout,      "panel.readout", "panel",  "chart",         "",   "tac.detail.readout", AlwaysOn);
		add(Id::Loadout,      "panel.loadout", "panel",  "inventory",     "",   "tac.detail.loadout", AlwaysOn);
		add(Id::MercNext,     "merc.next",     "select", "chevron-right", "space", "",              MercNextC);
		add(Id::SquadNext,    "squad.next",    "select", "chevron-down",  "shift+space", "tac.squadtab.next", SquadNextC);
		add(Id::Locate,       "merc.locate",   "select", "pointer",       "/",  "",                  LocateC);
		add(Id::Roofs,        "view.roofs",    "view",   "unexplored",    "t",  "tac.roofs",         AlwaysOn);
		add(Id::Glow,         "view.glow",     "view",   "sector-inventory", "i", "tac.glow",        AlwaysOn);
		static char const* const mercName[10] = { "merc.select.1", "merc.select.2", "merc.select.3", "merc.select.4", "merc.select.5",
			"merc.select.6", "merc.select.7", "merc.select.8", "merc.select.9", "merc.select.10" };
		static char const* const squadName[10] = { "squad.select.1", "squad.select.2", "squad.select.3", "squad.select.4", "squad.select.5",
			"squad.select.6", "squad.select.7", "squad.select.8", "squad.select.9", "squad.select.10" };
		static char const* const fKey[10] = { "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10" };
		static char const* const nKey[10] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };
		for (int i = 0; i < 10; ++i)
			add(static_cast<Id>(static_cast<int>(Id::Merc1) + i), mercName[i], "select", "face-gear", fKey[i], "", MercN, i);
		for (int i = 0; i < 10; ++i)
			add(static_cast<Id>(static_cast<int>(Id::Squad1) + i), squadName[i], "select", "squad", nKey[i], "", AlwaysOn, i);

		// the label of a command is its name under tac.cmd. in the string table
		static std::vector<std::string> labels;
		labels.clear();
		labels.reserve(t.size());
		for (Command const& c : t) labels.push_back(std::string("tac.cmd.") + c.name);
		for (size_t i = 0; i < t.size(); ++i) t[i].label = labels[i].c_str();
		return t;
	}
}

char const* WhyName(Why const w)
{
	switch (w)
	{
		case Why::None:           return "";
		case Why::NoMerc:         return "no_merc";
		case Why::Dead:           return "dead";
		case Why::NotYourTurn:    return "not_your_turn";
		case Why::HandFull:       return "hand_full";
		case Why::OutOfReach:     return "out_of_reach";
		case Why::Busy:           return "busy";
		case Why::Locked:         return "locked";
		case Why::Robot:          return "robot";
		case Why::Vehicle:        return "vehicle";
		case Why::Water:          return "water";
		case Why::NoBurst:        return "no_burst";
		case Why::NothingToClimb: return "nothing_to_climb";
		case Why::NoRoof:         return "no_roof";
		case Why::NoTurnBased:    return "no_turn_based";
		case Why::Already:        return "already";
		case Why::NoStance:       return "no_stance";
		case Why::Nobody:         return "nobody";
		case Why::NoDetail:       return "no_detail";
		case Why::EmptyHands:     return "empty_hands";
	}
	return "";
}

std::vector<Command> const& All()
{
	static std::vector<Command> const table = BuildTable();
	return table;
}

Command const& Get(Id const id)
{
	return All()[static_cast<size_t>(id)];
}

Command const* Find(std::string_view const name)
{
	for (Command const& c : All())
		if (name == c.name) return &c;
	return nullptr;
}

Command const* ForKey(std::string_view const key, bool const ctrl, bool const shift, bool const alt)
{
	std::string want;
	if (ctrl)  want += "ctrl+";
	if (shift) want += "shift+";
	if (alt)   want += "alt+";
	want += key;
	for (Command const& c : All())
		if (c.key[0] != '\0' && want == c.key) return &c;
	return nullptr;
}

std::string KeyName(std::string_view const named, char32_t const ch, bool const ctrl, bool const shift, bool const alt)
{
	std::string s;
	if (ctrl)  s += "ctrl+";
	if (shift) s += "shift+";
	if (alt)   s += "alt+";
	if (!named.empty()) s += named;
	else if (ch > 0 && ch < 128) s += static_cast<char>(std::tolower(static_cast<int>(ch)));
	return s;
}

std::string KeyLabel(std::string_view const key)
{
	std::string out;
	size_t i = 0;
	while (i <= key.size())
	{
		size_t const j = key.find('+', i);
		std::string_view part = key.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i);
		if (!part.empty())
		{
			std::string p(part);
			if (p == "pageup") p = "PgUp";
			else if (p == "pagedown") p = "PgDn";
			else if (p == "`") p = "`";
			else
			{
				p[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(p[0])));
			}
			if (!out.empty()) out += "+";
			out += p;
		}
		if (j == std::string_view::npos) break;
		i = j + 1;
	}
	return out;
}

Availability Check(Context const& c, Id const id)
{
	Command const& cmd = Get(id);
	return cmd.availability ? cmd.availability(c, id) : Availability{};
}

}
