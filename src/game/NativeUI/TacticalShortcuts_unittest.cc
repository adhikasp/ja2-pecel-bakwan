#include "TacticalShortcuts.h"

#include "KeyBindings.h"
#include "MapCommands.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace TacticalShortcuts;

namespace
{
/** Gives the process-wide registry its defaults back whatever happens (the rebind tests must see them). */
struct DefaultsBack
{
	~DefaultsBack() { KeyBindings::Bindings().ResetAll(); }
};
}

// The tactical screen's remaining shortcut table (issue #48 slice 3): what it is, not what it does. Its keys are the
// tactical screen's, together with the command table's (TacticalCommands), so the uniqueness this checks is the one
// the screen's dispatch needs: one action per key, on the tactical screen.

TEST(TacticalShortcuts, everyActionIsNamedGroupedAndLabelled)
{
	ASSERT_EQ(All().size(), static_cast<size_t>(COUNT));
	for (size_t i = 0; i < All().size(); ++i)
	{
		Command const& c = All()[i];
		EXPECT_EQ(static_cast<size_t>(std::to_underlying(c.id)), i) << c.name << " is out of Id order";
		EXPECT_FALSE(std::string(c.name).empty());
		EXPECT_FALSE(std::string(c.group).empty()) << c.name;
		EXPECT_FALSE(std::string(c.icon).empty()) << c.name;
		EXPECT_EQ(std::string(c.label), std::string("tac.cmd.") + c.name);
		EXPECT_TRUE(c.key == nullptr || KeyBindings::IsKey(c.key)) << c.name << " has a default that is not a key";
	}
}

TEST(TacticalShortcuts, theNamesAreUnique)
{
	std::set<std::string> names;
	for (Command const& c : All()) EXPECT_TRUE(names.insert(c.name).second) << c.name << " is declared twice";
}

TEST(TacticalShortcuts, oneActionPerKeyOnTheScreen)
{
	// the screen dispatches its own keys; the command table's are the other half of the same key space, and a key
	// press reaches exactly one action
	DefaultsBack const guard;
	std::set<std::string> keys;
	for (Command const& c : All())
	{
		if (std::string(c.key).empty()) continue;
		EXPECT_TRUE(keys.insert(c.key).second) << c.key << " runs both " << c.key << " actions";
	}
	// and none collides with the HUD's command table (the registry resolves that; this is the table's own promise)
	for (Command const& c : All())
	{
		if (std::string(c.key).empty()) continue;
		EXPECT_EQ(ForKey(c.key), &c) << c.key << " is not " << c.name << " on the screen";
	}
}

TEST(TacticalShortcuts, theLegacyKeysAreKept)
{
	// the keys the tactical screen had, action by action: the ones a player has in his fingers
	auto key = [](char const* name) { return std::string(Find(name)->key); };
	EXPECT_EQ(key("act.reload"), "alt+r");
	EXPECT_EQ(key("file.quick_save"), "alt+s");
	EXPECT_EQ(key("file.quick_load"), "alt+l");
	EXPECT_EQ(key("view.cursor_3d"), "home");
	EXPECT_EQ(key("act.auto_bandage"), "a");
	EXPECT_EQ(key("act.cycle_enemies"), "e");
	EXPECT_EQ(key("act.range"), "f");
	EXPECT_EQ(key("act.cycle_stack"), "n");
	EXPECT_EQ(key("act.action_mode"), "u");
	EXPECT_EQ(key("act.exchange"), "x");
	EXPECT_EQ(key("act.auto_reload"), "ctrl+shift+r");
	EXPECT_EQ(key("act.switch_headgear"), "ctrl+n");
	EXPECT_EQ(key("act.settings"), "v");
	EXPECT_EQ(key("view.merc_lights"), "g");
	EXPECT_EQ(key("view.wireframe"), "w");
	EXPECT_EQ(key("view.zoom_in"), "ctrl+=");
	EXPECT_EQ(key("view.zoom_out"), "ctrl+-");
	EXPECT_EQ(key("view.tracking"), "alt+f");
	EXPECT_EQ(key("move.continue"), "end");
	EXPECT_EQ(key("move.climb_window"), "shift+j");
	EXPECT_EQ(key("move.squad_stealth"), "alt+z");
	EXPECT_EQ(key("nav.help"), "shift+h");
	EXPECT_EQ(key("nav.save"), "ctrl+s");
	EXPECT_EQ(key("nav.load"), "ctrl+l");
	EXPECT_EQ(key("panel.clear_log"), "f12");
	EXPECT_EQ(key("msg.stop_speech"), "backspace");
	EXPECT_EQ(key("audio.music_down"), "alt+-");
	EXPECT_EQ(key("act.cycle_known_enemies"), "return");
	EXPECT_EQ(key("select.all"), "=");
	// what is not a player action is not in the table (and so stays a raw keycase)
	for (char const* notAnAction : { "act.cheat.cliff", "view.merc_lights_debug", "select.cover_size" })
		EXPECT_EQ(Find(notAnAction), nullptr) << notAnAction;
}

TEST(TacticalShortcuts, theSharedNamesAreTheMapsToo)
{
	// the names the map screen declares as well: one action, one key, each screen's own effect (issue #48 slice 3)
	for (char const* name :
		 { "file.quick_save", "file.quick_load", "nav.save", "nav.load", "act.settings", "msg.stop_speech" })
	{
		EXPECT_TRUE(Find(name)->shared) << name;
		ASSERT_NE(MapCommands::Find(name), nullptr) << name;
		EXPECT_EQ(std::string(MapCommands::Find(name)->key), std::string(Find(name)->key)) << name;
	}
	// the ones only the tactical screen runs are not shared
	EXPECT_FALSE(Find("act.auto_bandage")->shared);
	EXPECT_EQ(MapCommands::Find("act.auto_bandage"), nullptr);
}

TEST(TacticalShortcuts, forKeyReadsTheRegistry)
{
	// the process registry's binding, not the table's default: the defaults first
	for (Command const& c : All())
	{
		if (!std::string(c.key).empty())
		{
			Command const* const found = ForKey(c.key);
			ASSERT_TRUE(found) << c.key;
			EXPECT_EQ(std::string(found->name), std::string(c.name)) << c.key;
		}
	}
	EXPECT_EQ(ForKey("no-such-key"), nullptr);
	// a key a command of the HUD owns is not a shortcut: the command table's own lookup answers for it
	EXPECT_EQ(ForKey("c"), nullptr);
	EXPECT_EQ(ForKey("space"), nullptr);

	DefaultsBack const guard;
	KeyBindings::Bindings().Bind("act.auto_bandage", "alt+b");
	EXPECT_EQ(std::string(ForKey("alt+b")->name), "act.auto_bandage");
	EXPECT_EQ(ForKey("a"), nullptr); // a is free on the screen now
	EXPECT_EQ(KeyBindings::Bindings().Key("act.auto_bandage"), "alt+b");
	KeyBindings::Bindings().ResetAll();
	EXPECT_EQ(std::string(ForKey("a")->name), "act.auto_bandage");
}
