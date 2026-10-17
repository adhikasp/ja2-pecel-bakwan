#include "KeyBindings.h"

#include "MapCommands.h"
#include "TacticalCommands.h"

#include <SDL3/SDL_keycode.h>

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

using namespace KeyBindings;

namespace
{
/** A screen's worth of actions: two with a default key, one without. */
std::vector<Action> TestActions()
{
	return {
		{ "", "one.a", "g1", "l.one.a", "c", "", false },
		{ "", "one.b", "g1", "l.one.b", "d", "", false },
		{ "", "two.a", "g2", "l.two.a", "", "", false },
	};
}

Registry Make()
{
	Registry r;
	r.AddSource("test", TestActions);
	return r;
}

/** A second screen whose keys are its own: it runs 'd' too, and shares one action with the first. */
std::vector<Action> OtherActions()
{
	return {
		{ "", "other.a", "g3", "l.other.a", "d", "", false },
		{ "", "other.b", "g3", "l.other.b", "e", "", false },
		{ "", "one.a", "g1", "l.one.a", "c", "", false }, // the same action as the first screen's
	};
}

/** Two screens: the first's actions on c/d, the second's on d/e, and one action both run on c. */
Registry MakeTwo()
{
	Registry r;
	r.AddSource("one", TestActions);
	r.AddSource("two", OtherActions);
	return r;
}

/** Gives the process-wide registry its defaults back whatever happens (the tactical key tests must see them). */
struct DefaultsBack
{
	~DefaultsBack() { Bindings().ResetAll(); }
};
}

TEST(KeyBindings, sourcesGiveEveryActionWithItsDefault)
{
	Registry const r = Make();
	ASSERT_EQ(r.Actions().size(), 3u);
	EXPECT_EQ(r.Actions()[0].scope, "test");
	EXPECT_EQ(r.Actions()[0].name, "one.a");
	EXPECT_EQ(r.Actions()[0].group, "g1");
	EXPECT_EQ(r.Actions()[0].label, "l.one.a");
	EXPECT_EQ(r.Actions()[0].def, "c");
	EXPECT_EQ(r.Actions()[0].key, "c");
	EXPECT_FALSE(r.Actions()[0].changed);
	EXPECT_EQ(r.Key("one.a"), "c");
	EXPECT_EQ(r.Key("one.b"), "d");
	EXPECT_EQ(r.Key("two.a"), "");   // no default key
	EXPECT_EQ(r.Key("no.such"), ""); // no such action
	EXPECT_EQ(r.Owner("c")->name, "one.a");
	EXPECT_EQ(r.Owner("d")->name, "one.b");
	EXPECT_EQ(r.Owner(""), nullptr);
	EXPECT_EQ(r.Owner("z"), nullptr);
}

TEST(KeyBindings, bindingAKeyMovesItAndUnbindsTheActionThatHadIt)
{
	Registry r = Make();
	BindOutcome const out = r.Bind("two.a", "c");
	EXPECT_EQ(out.result, BindResult::Ok);
	EXPECT_EQ(out.loser, "one.a"); // it had c and is unbound now
	EXPECT_EQ(r.Key("two.a"), "c");
	EXPECT_EQ(r.Key("one.a"), "");
	EXPECT_TRUE(r.Find("two.a")->changed);
	EXPECT_TRUE(r.Find("one.a")->changed);
	EXPECT_EQ(r.Owner("c")->name, "two.a");
	// the two actions are not both on c
	EXPECT_EQ(r.Owner("c")->name, "two.a");
}

TEST(KeyBindings, bindingAFreeKeyOnlyTouchesTheOneAction)
{
	Registry r = Make();
	BindOutcome const out = r.Bind("two.a", "j");
	EXPECT_EQ(out.result, BindResult::Ok);
	EXPECT_TRUE(out.loser.empty());
	EXPECT_EQ(r.Key("two.a"), "j");
	EXPECT_EQ(r.Key("one.a"), "c");
	EXPECT_EQ(r.Key("one.b"), "d");
}

TEST(KeyBindings, bindingRefusesWhatIsNotAnActionOrAKey)
{
	Registry r = Make();
	EXPECT_EQ(r.Bind("no.such", "j").result, BindResult::UnknownAction);
	EXPECT_EQ(r.Bind("one.a", "a+b").result, BindResult::BadKey);
	EXPECT_EQ(r.Bind("one.a", "C").result, BindResult::BadKey);
	EXPECT_EQ(r.Key("one.a"), "c"); // nothing changed
	EXPECT_EQ(r.Key("one.b"), "d");
}

TEST(KeyBindings, bindingTheKeyItAlreadyRunsChangesNothing)
{
	Registry r = Make();
	EXPECT_EQ(r.Bind("one.a", "c").result, BindResult::Unchanged);
	EXPECT_FALSE(r.Find("one.a")->changed);
	EXPECT_TRUE(r.Overrides().empty());
	// an action with no key, bound to no key: also nothing to do
	EXPECT_EQ(r.Bind("two.a", "").result, BindResult::Unchanged);
}

TEST(KeyBindings, anActionCanBeLeftUnboundAndRebound)
{
	Registry r = Make();
	EXPECT_EQ(r.Bind("one.a", "").result, BindResult::Ok);
	EXPECT_EQ(r.Key("one.a"), "");
	EXPECT_EQ(r.Owner("c"), nullptr);
	EXPECT_EQ(r.Overrides().at("one.a"), "");
	EXPECT_EQ(r.Bind("one.a", "z").result, BindResult::Ok);
	EXPECT_EQ(r.Key("one.a"), "z");
}

TEST(KeyBindings, resetGivesTheDefaultBack)
{
	Registry r = Make();
	r.Bind("one.a", "j");
	r.Bind("one.b", "k");
	EXPECT_EQ(r.Overrides().size(), 2u);
	r.Reset("one.a");
	EXPECT_EQ(r.Key("one.a"), "c");
	EXPECT_FALSE(r.Find("one.a")->changed);
	EXPECT_EQ(r.Key("one.b"), "k");
	EXPECT_EQ(r.Overrides().size(), 1u);
	// the default goes back to the action that wants it, whoever holds it now
	r.Bind("two.a", "c");
	EXPECT_EQ(r.Key("one.a"), "");
	EXPECT_EQ(r.Key("two.a"), "c");
	BindOutcome const out = r.Reset("one.a");
	EXPECT_EQ(r.Key("one.a"), "c");
	EXPECT_EQ(r.Key("two.a"), "");
	EXPECT_EQ(out.loser, "two.a"); // the reset says whose key it took
	// a reset that has to take nothing names nobody
	EXPECT_TRUE(r.Reset("one.b").loser.empty());
	// and an action this registry does not have
	EXPECT_EQ(r.Reset("no.such").result, BindResult::UnknownAction);
}

TEST(KeyBindings, loadNeverLeavesTwoActionsOnAKey)
{
	// an override that collides with another action's default takes it from that action
	Registry r = Make();
	r.Load({ { "two.a", "d" } }); // one.b's default
	EXPECT_EQ(r.Key("two.a"), "d");
	EXPECT_EQ(r.Key("one.b"), "");
	EXPECT_TRUE(r.Find("one.b")->changed);
	// and the saved state says so, so the next save keeps it
	EXPECT_EQ(r.Overrides().at("one.b"), "");
	// two overrides for one key: the table order settles it
	Registry two = Make();
	two.Load({ { "one.a", "z" }, { "one.b", "z" } });
	EXPECT_EQ(two.Key("one.a"), "z");
	EXPECT_EQ(two.Key("one.b"), "");
	// a default that collides with nothing is left alone
	Registry plain = Make();
	plain.Load({ { "two.a", "j" } });
	EXPECT_EQ(plain.Key("one.a"), "c");
	EXPECT_EQ(plain.Key("one.b"), "d");
	EXPECT_EQ(plain.Key("two.a"), "j");
	// whatever the file said, one action per key
	for (Registry* reg : { &r, &two, &plain })
	{
		std::set<std::string> keys;
		for (Action const& a : reg->Actions())
			if (!a.key.empty()) EXPECT_TRUE(keys.insert(a.key).second) << a.key << " on " << a.name;
	}
}

TEST(KeyBindings, resetAllClearsEveryChange)
{
	Registry r = Make();
	r.Bind("one.a", "j");
	r.Bind("one.b", "k");
	r.ResetAll();
	EXPECT_TRUE(r.Overrides().empty());
	for (Action const& a : r.Actions())
	{
		EXPECT_EQ(a.key, a.def);
		EXPECT_FALSE(a.changed);
	}
}

TEST(KeyBindings, overridesAreOnlyWhatDiffersFromTheDefault)
{
	Registry r = Make();
	EXPECT_TRUE(r.Overrides().empty());
	r.Bind("one.a", "j");
	EXPECT_EQ(r.Overrides().size(), 1u);
	EXPECT_EQ(r.Overrides().at("one.a"), "j");
	r.Bind("one.a", "c"); // back to the default: not a change any more
	EXPECT_TRUE(r.Overrides().empty());
	EXPECT_FALSE(r.Find("one.a")->changed);
}

TEST(KeyBindings, loadIgnoresUnknownNamesAndBadKeysButTakesAnUnbind)
{
	Registry r = Make();
	r.Load({ { "one.a", "j" }, { "no.such", "k" }, { "one.b", "a+b" }, { "two.a", "" } });
	EXPECT_EQ(r.Key("one.a"), "j");
	EXPECT_EQ(r.Key("one.b"), "d"); // the bad key was ignored
	EXPECT_EQ(r.Key("two.a"), "");
	EXPECT_FALSE(r.Find("no.such"));     // an unknown action is not invented
	EXPECT_EQ(r.Overrides().size(), 1u); // and is not kept either
	EXPECT_EQ(r.Overrides().at("one.a"), "j");
}

TEST(KeyBindings, overridesRoundTripThroughTheConfigFormat)
{
	Registry const saved = []
	{
		Registry r = Make();
		r.Bind("one.a", "j");
		r.Bind("one.b", "");
		r.Bind("two.a", "shift+space");
		return r;
	}();
	std::string lines;
	for (auto const& [name, key] : saved.Overrides()) lines += name + "=" + key + "\n";

	Registry r = Make();
	r.Load(ParseOverrides(lines));
	EXPECT_EQ(r.Key("one.a"), "j");
	EXPECT_EQ(r.Key("one.b"), "");
	EXPECT_EQ(r.Key("two.a"), "shift+space");
	EXPECT_EQ(r.Overrides(), saved.Overrides());
}

TEST(KeyBindings, noTwoActionsShareAKeyAfterAnyBind)
{
	Registry r = Make();
	for (std::string const key : { "j", "c", "d", "j", "", "c", "space" })
	{
		r.Bind("two.a", key);
		r.Bind("one.a", key);
		r.Bind("one.b", key);
		std::set<std::string> keys;
		for (Action const& a : r.Actions())
		{
			if (a.key.empty()) continue;
			EXPECT_TRUE(keys.insert(a.key).second) << a.key << " on " << a.name;
		}
	}
}

TEST(KeyBindings, keyNamesAreCanonical)
{
	EXPECT_EQ(KeyName("", U'C', false, false, false), "c");
	EXPECT_EQ(KeyName("space", 0, false, true, false), "shift+space");
	EXPECT_EQ(KeyName("", U'q', true, false, false), "ctrl+q");
	EXPECT_EQ(KeyName("f10", 0, false, false, true), "alt+f10");
	EXPECT_EQ(KeyLabel("shift+space"), "Shift+Space");
	EXPECT_EQ(KeyLabel("c"), "C");
	EXPECT_EQ(KeyLabel("pageup"), "PgUp");
	EXPECT_EQ(KeyLabel("pagedown"), "PgDn");
	EXPECT_EQ(KeyLabel("escape"), "Esc");
	EXPECT_EQ(KeyLabel("return"), "Enter");
	EXPECT_EQ(KeyLabel("up"), "Up");
	EXPECT_EQ(KeyLabel(""), "");
}

TEST(KeyBindings, isKeyRejectsWhatIsNotAKey)
{
	for (std::string const key : { "c",           "1",    "0",      "`",     "/",       "-",      "=",
								   "f1",          "f12",  "pageup", "space", "insert",  "escape", "return",
								   "up",          "down", "left",   "right", "shift+c", "ctrl+q", "ctrl+shift+space",
								   "ctrl+alt+f12" })
		EXPECT_TRUE(IsKey(key)) << key;
	for (std::string const key : { "", " ", "C", "+", "a+b", "ctrl+", "shift+", "ctrl+ctrl+q", "ctrl+shift+",
								   "noSuchKey", "42", "ctrl+F1", "\t" })
		EXPECT_FALSE(IsKey(key)) << key;
}

TEST(KeyBindings, keyFromInputIsTheVocabulary)
{
	EXPECT_EQ(KeyFromInput(SDLK_C, false, false, false), "c");
	EXPECT_EQ(KeyFromInput(SDLK_SPACE, false, false, false), "space");
	EXPECT_EQ(KeyFromInput(SDLK_TAB, false, false, false), "tab");
	EXPECT_EQ(KeyFromInput(SDLK_F1, false, false, false), "f1");
	EXPECT_EQ(KeyFromInput(SDLK_PAGEUP, false, false, false), "pageup");
	EXPECT_EQ(KeyFromInput(SDLK_1, false, true, false), "shift+1");
	EXPECT_EQ(KeyFromInput(SDLK_D, true, false, false), "ctrl+d");
	// the keys the map screen's own shortcuts need (issue #48 slice 2)
	EXPECT_EQ(KeyFromInput(SDLK_ESCAPE, false, false, false), "escape");
	EXPECT_EQ(KeyFromInput(SDLK_RETURN, false, false, false), "return");
	EXPECT_EQ(KeyFromInput(SDLK_UP, false, false, false), "up");
	EXPECT_EQ(KeyFromInput(SDLK_DOWN, false, false, false), "down");
	EXPECT_EQ(KeyFromInput(SDLK_LEFT, false, false, false), "left");
	EXPECT_EQ(KeyFromInput(SDLK_RIGHT, false, false, false), "right");
	EXPECT_EQ(KeyFromInput(SDLK_RIGHT, false, true, false), "shift+right");
	// the numpad's + is the '=' key the map's clock runs; its shifted form is nobody's
	EXPECT_EQ(KeyFromInput(SDLK_PLUS, false, false, false), "=");
	EXPECT_EQ(KeyFromInput(SDLK_PLUS, false, true, false), "");
	EXPECT_EQ(KeyFromInput(SDLK_MINUS, false, false, false), "-");
	// a key that cannot be bound
	EXPECT_EQ(KeyFromInput(SDLK_LSHIFT, false, true, false), "");
	EXPECT_EQ(KeyFromInput(SDLK_KP_ENTER, false, false, false), "");
	EXPECT_EQ(KeyFromInput(SDLK_PAUSE, false, false, false), "");
}

TEST(KeyBindings, parseOverridesReadsTheLines)
{
	std::map<std::string, std::string> const o =
		ParseOverrides("stance.crouch=j\nmove.climb=\nsquad.next=shift+space\n");
	ASSERT_EQ(o.size(), 3u);
	EXPECT_EQ(o.at("stance.crouch"), "j");
	EXPECT_EQ(o.at("move.climb"), "");
	EXPECT_EQ(o.at("squad.next"), "shift+space");
	EXPECT_TRUE(ParseOverrides("").empty());
	EXPECT_TRUE(ParseOverrides("=x\nnoequals\n").empty());
}

TEST(KeyBindings, eachScreenKeepsItsOwnKeyOnTheSameLetter)
{
	// the two screens' legacy keys collide ('d' is this one's and that one's), so a key runs one action per screen
	Registry const r = MakeTwo();
	EXPECT_EQ(r.Owner("one", "d")->name, "one.b");
	EXPECT_EQ(r.Owner("two", "d")->name, "other.a");
	EXPECT_EQ(r.Owner("one", "e"), nullptr); // e is the second screen's
	EXPECT_EQ(r.Owner("two", "e")->name, "other.b");
	EXPECT_EQ(r.Owner("d")->name, "one.b"); // any screen: the first in table order
}

TEST(KeyBindings, bindingOneScreensKeyLeavesTheOtherScreensAlone)
{
	Registry r = MakeTwo();
	BindOutcome const out = r.Bind("other.a", "g"); // the second screen's action, on a free key
	EXPECT_EQ(out.result, BindResult::Ok);
	EXPECT_TRUE(out.loser.empty()) << out.loser;
	EXPECT_EQ(r.Key("other.a"), "g");
	EXPECT_EQ(r.Key("one.b"), "d"); // the first screen's is its own
	// and a key the same screen holds is taken from that screen's action
	EXPECT_EQ(r.Bind("other.a", "e").loser, "other.b");
	EXPECT_EQ(r.Key("other.b"), "");
	EXPECT_EQ(r.Key("one.b"), "d");
	EXPECT_EQ(r.Key("one.a"), "c");
}

TEST(KeyBindings, aSharedActionIsOneRowOnBothScreens)
{
	Registry r = MakeTwo();
	// "one.a" is declared by both screens: one action, one key, registered once
	EXPECT_EQ(r.Actions().size(), 5u);
	EXPECT_TRUE(r.Find("one.a")->shared);
	EXPECT_FALSE(r.Find("one.b")->shared);
	EXPECT_EQ(r.Find("one.a")->scope, "one");
	EXPECT_EQ(r.Owner("one", "c")->name, "one.a");
	EXPECT_EQ(r.Owner("two", "c")->name, "one.a");
	// taking the shared action's key on one screen leaves it unbound on both
	EXPECT_EQ(r.Bind("other.a", "c").loser, "one.a");
	EXPECT_EQ(r.Key("one.a"), "");
	EXPECT_EQ(r.Owner("one", "c"), nullptr);
	EXPECT_EQ(r.Owner("two", "c")->name, "other.a"); // the action that took it runs it on its own screen
}

TEST(KeyBindings, loadResolvesConflictsOneScreenAtATime)
{
	Registry r = MakeTwo();
	r.Load({ { "other.a", "c" } }); // the second screen's action wants the shared action's key
	EXPECT_EQ(r.Key("other.a"), "c");
	EXPECT_EQ(r.Key("one.a"), ""); // the shared action lost it, on both screens
	EXPECT_EQ(r.Key("one.b"), "d");
	// an override on one screen's own key leaves the other screen's action alone
	Registry plain = MakeTwo();
	plain.Load({ { "other.b", "d" } }); // d is other.a's on the second screen and one.b's on the first
	EXPECT_EQ(plain.Key("other.b"), "d");
	EXPECT_EQ(plain.Key("other.a"), ""); // its own screen's holder lost it
	EXPECT_EQ(plain.Key("one.b"), "d");  // the first screen's keeps it
}

TEST(KeyBindings, sourceRegistersOneActionForANameTwoScreensDeclare)
{
	Registry const r = MakeTwo();
	std::set<std::string> names;
	for (Action const& a : r.Actions()) EXPECT_TRUE(names.insert(a.name).second) << a.name;
	// and per screen the keys are unique
	for (std::string const screen : { "one", "two" })
	{
		std::set<std::string> keys;
		for (Action const& a : r.Actions())
		{
			if (!a.key.empty() && (a.scope == screen || a.shared))
				EXPECT_TRUE(keys.insert(a.key).second) << a.key << " twice on " << screen << " (" << a.name << ")";
		}
	}
}

TEST(KeyBindings, theProcessRegistryHasTheWholeTacticalTable)
{
	DefaultsBack const guard;
	Registry& r = Bindings();
	for (TacticalCommands::Command const& c : TacticalCommands::All())
	{
		EXPECT_EQ(r.Key(c.name), std::string(c.key)) << c.name;
		EXPECT_EQ(r.Find(c.name)->def, std::string(c.key)) << c.name;
		EXPECT_EQ(r.Find(c.name)->scope, TacticalCommands::SCOPE) << c.name;
	}
	// a rebind reaches the tactical key lookup, and the default key is free again
	r.Bind("stance.crouch", "j");
	EXPECT_EQ(TacticalCommands::ForKey("j", false, false, false)->id, TacticalCommands::Id::StanceCrouch);
	EXPECT_EQ(TacticalCommands::ForKey("c", false, false, false), nullptr);
	r.ResetAll();
	EXPECT_EQ(TacticalCommands::ForKey("c", false, false, false)->id, TacticalCommands::Id::StanceCrouch);
}

TEST(KeyBindings, theProcessRegistryHasTheWholeMapTable)
{
	DefaultsBack const guard;
	Registry& r = Bindings();
	ASSERT_EQ(r.Actions().size(),
			  static_cast<size_t>(TacticalCommands::COUNT) + static_cast<size_t>(MapCommands::COUNT) - 11u);
	for (MapCommands::Command const& c : MapCommands::All())
	{
		Action const* const a = r.Find(c.name);
		ASSERT_TRUE(a) << c.name;
		EXPECT_EQ(a->key, std::string(c.key)) << c.name;
		EXPECT_EQ(a->def, std::string(c.key)) << c.name;
		EXPECT_EQ(a->shared, c.shared) << c.name;
		if (!c.shared)
		{
			EXPECT_EQ(a->scope, MapCommands::SCOPE) << c.name;
		}
	}
	// the two screens' defaults collide on plenty of keys and each keeps its own: space is the map's clock and the
	// tactical HUD's next merc, 'c' the map's contract and the HUD's crouch, 'm' the map's mines and the HUD's map
	EXPECT_EQ(r.Owner(MapCommands::SCOPE, "space")->name, "time.toggle");
	EXPECT_EQ(r.Owner(TacticalCommands::SCOPE, "space")->name, "merc.next");
	EXPECT_EQ(r.Owner(MapCommands::SCOPE, "c")->name, "act.contract");
	EXPECT_EQ(r.Owner(TacticalCommands::SCOPE, "c")->name, "stance.crouch");
	EXPECT_EQ(r.Owner(MapCommands::SCOPE, "m")->name, "view.mines");
	EXPECT_EQ(r.Owner(TacticalCommands::SCOPE, "m")->name, "nav.map");
	// the shared ones run on both: the map's squads and the HUD's are one action, and both open the options screen
	EXPECT_TRUE(r.Find("squad.select.4")->shared);
	EXPECT_EQ(r.Owner(MapCommands::SCOPE, "4")->name, "squad.select.4");
	EXPECT_EQ(r.Owner(TacticalCommands::SCOPE, "4")->name, "squad.select.4");
	EXPECT_TRUE(r.Find("nav.options")->shared);
	EXPECT_EQ(r.Owner(MapCommands::SCOPE, "o")->name, "nav.options");
	EXPECT_EQ(r.Owner(TacticalCommands::SCOPE, "o")->name, "nav.options");
	// and one key never runs two actions on one screen
	for (std::string const& screen : { std::string(TacticalCommands::SCOPE), std::string(MapCommands::SCOPE) })
	{
		std::set<std::string> keys;
		for (Action const& a : r.Actions())
		{
			if (!a.key.empty() && (a.scope == screen || a.shared)) EXPECT_TRUE(keys.insert(a.key).second) << a.key;
		}
	}
	r.Bind("view.mines", "n");
	EXPECT_EQ(r.Key("view.mines"), "n");
	EXPECT_EQ(r.Key("nav.map"), "m"); // the tactical HUD's m is its own screen's: the map's rebind leaves it alone
	r.ResetAll();
}
