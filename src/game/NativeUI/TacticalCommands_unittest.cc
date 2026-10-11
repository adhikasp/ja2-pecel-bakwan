#include "TacticalCommands.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace TacticalCommands;

namespace
{
	/** A merc standing in the open, our turn, with a rifle. */
	Context Ready()
	{
		Context c;
		c.hasMerc = true;
		c.alive = true;
		c.ourTurn = true;
		c.canBurst = true;
		c.canClimb = true;
		c.otherMercs = true;
		c.otherSquads = true;
		c.slotMask = 0x3;
		return c;
	}
}

TEST(TacticalCommands, theTableCoversEveryId)
{
	ASSERT_EQ(All().size(), static_cast<size_t>(COUNT));
	for (int i = 0; i < COUNT; ++i) EXPECT_EQ(static_cast<int>(All()[i].id), i) << i;
}

TEST(TacticalCommands, noTwoCommandsShareAKeyOrAName)
{
	std::set<std::string> keys, names;
	for (Command const& c : All())
	{
		EXPECT_TRUE(names.insert(c.name).second) << "name " << c.name;
		if (c.key[0] != '\0') { EXPECT_TRUE(keys.insert(c.key).second) << "key " << c.key << " of " << c.name; }
	}
}

TEST(TacticalCommands, everyCommandHasALabelAnIconAndAnAvailability)
{
	for (Command const& c : All())
	{
		EXPECT_NE(std::string(c.label).find("tac.cmd."), std::string::npos) << c.name;
		EXPECT_EQ(std::string(c.label), std::string("tac.cmd.") + c.name);
		EXPECT_NE(c.icon[0], '\0') << c.name;
		EXPECT_NE(c.availability, nullptr) << c.name;
		EXPECT_NE(c.group[0], '\0') << c.name;
	}
}

TEST(TacticalCommands, hudButtonsNameDistinctCommands)
{
	std::set<std::string> huds;
	for (Command const& c : All())
		if (c.hud[0] != '\0') { EXPECT_TRUE(huds.insert(c.hud).second) << c.hud; }
	// the buttons of the bar
	for (char const* name : { "stance.stand", "stance.crouch", "stance.prone", "move.run", "move.stealth", "act.burst", "act.look",
		"act.talk", "move.climb", "level.toggle", "turn.end", "nav.map", "panel.detail", "nav.options", "nav.overhead" })
	{
		Command const* c = Find(name);
		ASSERT_NE(c, nullptr) << name;
		EXPECT_NE(c->hud[0], '\0') << name;
	}
}

TEST(TacticalCommands, findByNameAndByKey)
{
	ASSERT_NE(Find("stance.crouch"), nullptr);
	EXPECT_EQ(Find("stance.crouch")->id, Id::StanceCrouch);
	EXPECT_EQ(Find("no.such"), nullptr);

	EXPECT_EQ(ForKey("c", false, false, false)->id, Id::StanceCrouch);
	EXPECT_EQ(ForKey("space", false, true, false)->id, Id::SquadNext);
	EXPECT_EQ(ForKey("space", false, false, false)->id, Id::MercNext);
	EXPECT_EQ(ForKey("q", true, false, false)->id, Id::SwapHands);
	EXPECT_EQ(ForKey("f3", false, false, false)->id, Id::Merc3);
	EXPECT_EQ(ForKey("0", false, false, false)->id, Id::Squad10);
	EXPECT_EQ(ForKey("c", true, false, false), nullptr);
	EXPECT_EQ(ForKey("x", false, false, false), nullptr);
}

TEST(TacticalCommands, keyNamesAreCanonical)
{
	EXPECT_EQ(KeyName("", U'C', false, false, false), "c");
	EXPECT_EQ(KeyName("space", 0, false, true, false), "shift+space");
	EXPECT_EQ(KeyName("", U'q', true, false, false), "ctrl+q");
	EXPECT_EQ(KeyName("f10", 0, false, false, true), "alt+f10");
	EXPECT_EQ(KeyLabel("shift+space"), "Shift+Space");
	EXPECT_EQ(KeyLabel("c"), "C");
	EXPECT_EQ(KeyLabel("pageup"), "PgUp");
}

TEST(TacticalCommands, nobodyIsSelected)
{
	Context c;
	for (Id id : { Id::StanceStand, Id::StanceCrouch, Id::Run, Id::Burst, Id::Look, Id::Climb })
	{
		Availability const a = Check(c, id);
		EXPECT_FALSE(a.enabled) << static_cast<int>(id);
		EXPECT_EQ(a.why, Why::NoMerc) << static_cast<int>(id);
	}
	// the game flow still works without a merc
	EXPECT_TRUE(Check(c, Id::Map).enabled);
	EXPECT_TRUE(Check(c, Id::Overhead).enabled);
	EXPECT_TRUE(Check(c, Id::Log).enabled);
}

TEST(TacticalCommands, aMercDownCannotAct)
{
	Context c = Ready();
	c.alive = false;
	EXPECT_EQ(Check(c, Id::StanceCrouch).why, Why::Dead);
	EXPECT_EQ(Check(c, Id::Look).why, Why::Dead);
}

TEST(TacticalCommands, theEnemysTurnLocksTheBody)
{
	Context c = Ready();
	c.combat = true;
	c.ourTurn = false;
	EXPECT_EQ(Check(c, Id::StanceCrouch).why, Why::NotYourTurn);
	EXPECT_EQ(Check(c, Id::EndTurn).why, Why::NotYourTurn);
	c.ourTurn = true;
	EXPECT_TRUE(Check(c, Id::StanceCrouch).enabled);
	EXPECT_TRUE(Check(c, Id::EndTurn).enabled);
}

TEST(TacticalCommands, stanceButtonsFollowTheStance)
{
	Context c = Ready();
	c.stance = 0;
	EXPECT_EQ(Check(c, Id::StanceStand).why, Why::Already);
	EXPECT_TRUE(Check(c, Id::StanceCrouch).enabled);
	EXPECT_EQ(Check(c, Id::StanceUp).why, Why::Already);
	c.stance = 2;
	EXPECT_TRUE(Check(c, Id::StanceStand).enabled);
	EXPECT_EQ(Check(c, Id::StanceProne).why, Why::Already);
	EXPECT_EQ(Check(c, Id::StanceDown).why, Why::Already);
	c.stance = 0;
	c.canProne = false;
	EXPECT_EQ(Check(c, Id::StanceProne).why, Why::NoStance);
	c.robot = true;
	EXPECT_EQ(Check(c, Id::StanceProne).why, Why::Robot);
}

TEST(TacticalCommands, anItemInHandBlocksWhatTheLegacyKeysBlocked)
{
	Context c = Ready();
	c.handFull = true;
	for (Id id : { Id::Burst, Id::Look, Id::Stealth, Id::Level, Id::Map, Id::Options, Id::StanceUp, Id::StanceDown })
		EXPECT_EQ(Check(c, id).why, Why::HandFull) << static_cast<int>(id);
	// a stance key never did
	EXPECT_TRUE(Check(c, Id::StanceCrouch).enabled);
}

TEST(TacticalCommands, burstAndClimbNeedTheirCapability)
{
	Context c = Ready();
	c.canBurst = false;
	c.canClimb = false;
	EXPECT_EQ(Check(c, Id::Burst).why, Why::NoBurst);
	EXPECT_EQ(Check(c, Id::Climb).why, Why::NothingToClimb);
	c.canBurst = true;
	c.canClimb = true;
	EXPECT_TRUE(Check(c, Id::Burst).enabled);
	EXPECT_TRUE(Check(c, Id::Climb).enabled);
}

TEST(TacticalCommands, endTurnInRealTimeFollowsThePolicy)
{
	Context c = Ready();
	c.combat = false;
	c.turnBasedAllowed = true;
	EXPECT_TRUE(Check(c, Id::EndTurn).enabled);
	c.turnBasedAllowed = false;
	EXPECT_EQ(Check(c, Id::EndTurn).why, Why::NoTurnBased);
}

TEST(TacticalCommands, mapAndOptionsAreLockedByTheGameFlow)
{
	Context c = Ready();
	c.panelsLocked = true;
	EXPECT_EQ(Check(c, Id::Map).why, Why::Locked);
	EXPECT_EQ(Check(c, Id::Options).why, Why::Locked);
	c = Ready();
	c.conversation = true;
	EXPECT_EQ(Check(c, Id::Map).why, Why::Busy);
	c = Ready();
	c.meanwhile = true;
	EXPECT_EQ(Check(c, Id::Options).why, Why::Busy);
}

TEST(TacticalCommands, detailPanelCommands)
{
	Context c = Ready();
	EXPECT_EQ(Check(c, Id::DetailClose).why, Why::NoDetail);
	EXPECT_EQ(Check(c, Id::KeyRing).why, Why::NoDetail);
	c.detailOpen = true;
	EXPECT_TRUE(Check(c, Id::DetailClose).enabled);
	EXPECT_TRUE(Check(c, Id::KeyRing).enabled);
	c.itemsLocked = true;
	EXPECT_EQ(Check(c, Id::KeyRing).why, Why::OutOfReach);
}

TEST(TacticalCommands, selectingAMercNeedsACard)
{
	Context c = Ready();
	c.slotMask = 0x5; // cards 1 and 3
	EXPECT_TRUE(Check(c, Id::Merc1).enabled);
	EXPECT_EQ(Check(c, Id::Merc2).why, Why::Nobody);
	EXPECT_TRUE(Check(c, Id::Merc3).enabled);
	EXPECT_EQ(Check(c, Id::Merc10).why, Why::Nobody);
	// a squad key is always a request
	EXPECT_TRUE(Check(c, Id::Squad4).enabled);
}

TEST(TacticalCommands, cyclingNeedsSomeoneElse)
{
	Context c = Ready();
	c.otherMercs = false;
	c.otherSquads = false;
	EXPECT_EQ(Check(c, Id::MercNext).why, Why::Nobody);
	EXPECT_EQ(Check(c, Id::SquadNext).why, Why::Nobody);
	c.otherMercs = true;
	EXPECT_TRUE(Check(c, Id::MercNext).enabled);
	c.keyRingOpen = true;
	EXPECT_EQ(Check(c, Id::MercNext).why, Why::Busy);
}

TEST(TacticalCommands, everyWhyHasAName)
{
	for (int w = 1; w <= static_cast<int>(Why::NoDetail); ++w)
		EXPECT_NE(std::string(WhyName(static_cast<Why>(w))), "") << w;
	EXPECT_EQ(std::string(WhyName(Why::None)), "");
}
