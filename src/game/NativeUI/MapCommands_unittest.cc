#include "MapCommands.h"

#include "KeyBindings.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace MapCommands;

// The map screen's action table (issue #48 slice 2): what it is, not what it does. The keys the map's own keyboard
// holds are its own - the tactical screen has its own for the same letters - so the uniqueness this checks is the one
// the map's dispatch needs: one action per key, on the map.

namespace
{
/** Every action the map screen dispatches, in table order. */
std::vector<Command> const& Dispatched()
{
	return All();
}
}

TEST(MapCommands, everyActionIsNamedGroupedAndLabelled)
{
	ASSERT_EQ(All().size(), static_cast<size_t>(COUNT));
	for (size_t i = 0; i < All().size(); ++i)
	{
		Command const& c = All()[i];
		EXPECT_EQ(static_cast<size_t>(std::to_underlying(c.id)), i) << c.name << " is out of Id order";
		EXPECT_FALSE(std::string(c.name).empty());
		// a shared action keeps the group of the screen that registered it; the map's own are its own groups
		if (!c.shared)
		{
			EXPECT_NE(std::string(c.group).find("map_"), std::string::npos) << c.name << " is not in a map group";
		}
		EXPECT_EQ(std::string(c.label), std::string("map.cmd.") + c.name);
		EXPECT_TRUE(c.key == nullptr || KeyBindings::IsKey(c.key)) << c.name << " has a default that is not a key";
	}
}

TEST(MapCommands, theNamesAreUnique)
{
	std::set<std::string> names;
	for (Command const& c : Dispatched()) EXPECT_TRUE(names.insert(c.name).second) << c.name << " is declared twice";
}

TEST(MapCommands, oneActionPerKeyOnTheMap)
{
	// the map dispatches its own actions and the shared ones (the tactical table's squads and options screen), and a
	// key press reaches exactly one of them
	std::map<std::string, std::string> owner;
	for (Command const& c : Dispatched())
	{
		if (std::string(c.key).empty()) continue;
		auto const [it, fresh] = owner.emplace(c.key, c.name);
		EXPECT_TRUE(fresh) << c.key << " runs both " << it->second << " and " << c.name;
	}
}

TEST(MapCommands, theLegacyKeysAreKept)
{
	// the keys the map screen had, action by action: the ones a player has in his fingers
	auto key = [](char const* name) { return std::string(Find(name)->key); };
	EXPECT_EQ(key("time.toggle"), "space");
	EXPECT_EQ(key("time.more"), "=");
	EXPECT_EQ(key("time.less"), "-");
	EXPECT_EQ(key("msg.up"), "up");
	EXPECT_EQ(key("msg.page_down"), "pagedown");
	EXPECT_EQ(key("msg.stop_speech"), "backspace");
	EXPECT_EQ(key("level.up"), "insert");
	EXPECT_EQ(key("level.down"), "delete");
	EXPECT_EQ(key("view.towns"), "w");
	EXPECT_EQ(key("view.mines"), "m");
	EXPECT_EQ(key("view.teams"), "t");
	EXPECT_EQ(key("view.militia"), "z");
	EXPECT_EQ(key("view.airspace"), "a");
	EXPECT_EQ(key("view.items"), "i");
	EXPECT_EQ(key("team.next"), "right");
	EXPECT_EQ(key("team.prev"), "left");
	EXPECT_EQ(key("panel.merc_inventory"), "return");
	EXPECT_EQ(key("squad.select.1"), "1");
	EXPECT_EQ(key("squad.select.10"), "0");
	EXPECT_EQ(key("squad.select.11"), "shift+1");
	EXPECT_EQ(key("squad.select.20"), "shift+0");
	EXPECT_EQ(key("act.contract"), "c");
	EXPECT_EQ(key("act.help"), "h");
	EXPECT_EQ(key("act.settings"), "v");
	EXPECT_EQ(key("act.assign"), "alt+a");
	EXPECT_EQ(key("act.sector_inventory"), "ctrl+i");
	EXPECT_EQ(key("prebattle.enter_sector"), "e");
	EXPECT_EQ(key("prebattle.retreat"), "r");
	EXPECT_EQ(key("nav.laptop"), "l");
	EXPECT_EQ(key("nav.back"), "escape");
	EXPECT_EQ(key("nav.save"), "ctrl+s");
	EXPECT_EQ(key("nav.load"), "ctrl+l");
	EXPECT_EQ(key("nav.options"), "o");
	EXPECT_EQ(key("team.sort.name"), "f1");
	EXPECT_EQ(key("team.sort.contract"), "f6");
	// what is not a player action is not in the table (and so stays a raw keycase): the cheats, Alt+X, Pause
	for (char const* notAnAction : { "act.cheat.ambush", "act.cheat.teleport", "time.pause", "nav.save_quick" })
		EXPECT_EQ(Find(notAnAction), nullptr) << notAnAction;
}

TEST(MapCommands, theEntryCarriesWhatTheEffectNeeds)
{
	// the squads and the sort methods are the same action with a different n, as their legacy keys were
	for (int i = 0; i < 10; ++i)
	{
		Command const& c = Get(static_cast<Id>(std::to_underlying(Id::Squad1) + i));
		EXPECT_EQ(c.arg, i);
		EXPECT_EQ(Find("squad.select." + std::to_string(i + 1)), &c);
	}
	for (int i = 0; i < 10; ++i)
	{
		Command const& c = Get(static_cast<Id>(std::to_underlying(Id::Squad11) + i));
		EXPECT_EQ(c.arg, 10 + i);
		EXPECT_EQ(Find("squad.select." + std::to_string(i + 11)), &c);
	}
	for (int i = 0; i < 6; ++i)
	{
		Command const& c = Get(static_cast<Id>(std::to_underlying(Id::TeamSortName) + i));
		EXPECT_EQ(c.arg, i);
	}
	// the keys that repeat while held are the ones the legacy KEY_REPEAT block repeated
	for (Command const* c : { &Get(Id::MsgUp), &Get(Id::MsgDown), &Get(Id::MsgPageUp), &Get(Id::MsgPageDown),
							  &Get(Id::TeamNext), &Get(Id::TeamPrev) })
		EXPECT_TRUE(c->repeat) << c->name;
	EXPECT_FALSE(Get(Id::MsgFirst).repeat);
	EXPECT_FALSE(Get(Id::TimeToggle).repeat);
}

TEST(MapCommands, forKeyReadsTheRegistry)
{
	// the process registry's binding, not the table's default: the defaults first
	for (Command const& c : Dispatched())
	{
		if (!std::string(c.key).empty())
		{
			Command const* const found = ForKey(c.key);
			ASSERT_TRUE(found) << c.key;
			EXPECT_EQ(std::string(found->name), std::string(c.name)) << c.key;
		}
	}
	EXPECT_EQ(ForKey("no-such-key"), nullptr);

	KeyBindings::Bindings().Bind("view.mines", "n");
	EXPECT_EQ(std::string(ForKey("n")->name), "view.mines");
	EXPECT_EQ(ForKey("m"), nullptr); // m is free on the map now
	EXPECT_EQ(KeyBindings::Bindings().Key("view.mines"), "n");
	KeyBindings::Bindings().ResetAll();
	EXPECT_EQ(std::string(ForKey("m")->name), "view.mines");
}
