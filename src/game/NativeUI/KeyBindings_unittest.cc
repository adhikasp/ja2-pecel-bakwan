#include "KeyBindings.h"

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
	r.Reset("one.a");
	EXPECT_EQ(r.Key("one.a"), "c");
	EXPECT_EQ(r.Key("two.a"), "");
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
	EXPECT_EQ(KeyLabel(""), "");
}

TEST(KeyBindings, isKeyRejectsWhatIsNotAKey)
{
	for (std::string const key : { "c", "1", "0", "`", "/", "-", "f1", "f12", "pageup", "space", "insert", "shift+c",
								   "ctrl+q", "ctrl+shift+space", "ctrl+alt+f12" })
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
	// a key that cannot be bound
	EXPECT_EQ(KeyFromInput(SDLK_RETURN, false, false, false), "");
	EXPECT_EQ(KeyFromInput(SDLK_ESCAPE, false, false, false), "");
	EXPECT_EQ(KeyFromInput(SDLK_UP, false, false, false), "");
	EXPECT_EQ(KeyFromInput(SDLK_LSHIFT, false, true, false), "");
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

TEST(KeyBindings, theProcessRegistryHasTheWholeTacticalTable)
{
	DefaultsBack const guard;
	Registry& r = Bindings();
	ASSERT_EQ(r.Actions().size(), static_cast<size_t>(TacticalCommands::COUNT));
	for (TacticalCommands::Command const& c : TacticalCommands::All())
	{
		EXPECT_EQ(r.Key(c.name), std::string(c.key)) << c.name;
		EXPECT_EQ(r.Find(c.name)->def, std::string(c.key)) << c.name;
		EXPECT_EQ(r.Find(c.name)->scope, "tactical") << c.name;
	}
	// a rebind reaches the tactical key lookup, and the default key is free again
	r.Bind("stance.crouch", "j");
	EXPECT_EQ(TacticalCommands::ForKey("j", false, false, false)->id, TacticalCommands::Id::StanceCrouch);
	EXPECT_EQ(TacticalCommands::ForKey("c", false, false, false), nullptr);
	r.ResetAll();
	EXPECT_EQ(TacticalCommands::ForKey("c", false, false, false)->id, TacticalCommands::Id::StanceCrouch);
}
