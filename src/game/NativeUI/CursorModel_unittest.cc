#include "CursorModel.h"

#include "Interface_Cursors.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace CursorModel;

TEST(CursorModel, everyUICursorIdIsMapped)
{
	ASSERT_EQ(SpecCount(), int(NUM_UI_CURSORS));
	for (int id = 0; id < NUM_UI_CURSORS; ++id)
	{
		Spec const& s = SpecFor(id);
		EXPECT_EQ(s.id, id) << "the table is out of the enum's order at " << id;
		EXPECT_LT(int(s.shape), int(Shape::Count));
	}
}

TEST(CursorModel, everyShapeBeyondNoneHasAnIconAndAName)
{
	std::set<std::string> names;
	for (int i = 0; i < int(Shape::Count); ++i)
	{
		Shape const s = Shape(i);
		EXPECT_TRUE(names.insert(ShapeName(s)).second) << "two shapes share the name " << ShapeName(s);
		if (s != Shape::None) { EXPECT_NE(std::string(ShapeIcon(s)), "") << ShapeName(s); }
	}
}

TEST(CursorModel, aToneThatIsNotOkSaysWhy)
{
	for (int id = 0; id < NUM_UI_CURSORS; ++id)
	{
		Spec const& s = SpecFor(id);
		if (s.tone == Tone::No || s.tone == Tone::Warn) { EXPECT_NE(std::string(s.why), "") << "id " << id; }
		if (s.tone == Tone::Ok || s.tone == Tone::Foe) { EXPECT_EQ(std::string(s.why), "") << "id " << id; }
	}
}

TEST(CursorModel, moveCursorsKeepTheirShapeAndGetAMarker)
{
	EXPECT_EQ(SpecFor(MOVE_RUN_UICURSOR).shape, Shape::Run);
	EXPECT_EQ(SpecFor(MOVE_RUN_UICURSOR).marker, Marker::Tile);
	EXPECT_EQ(SpecFor(CONFIRM_MOVE_SWAT_UICURSOR).shape, Shape::Sneak);
	EXPECT_EQ(SpecFor(CONFIRM_MOVE_SWAT_UICURSOR).marker, Marker::Confirm);
	EXPECT_EQ(SpecFor(ALL_MOVE_PRONE_UICURSOR).mode, Mode::MoveAll);
	EXPECT_EQ(SpecFor(CANNOT_MOVE_UICURSOR).marker, Marker::Bad);
	EXPECT_EQ(SpecFor(CANNOT_MOVE_UICURSOR).tone, Tone::No);
	EXPECT_EQ(std::string(SpecFor(CANNOT_MOVE_UICURSOR).why), "no_path");
}

TEST(CursorModel, parseChance)
{
	EXPECT_EQ(ParseChance("64%"), 64);
	EXPECT_EQ(ParseChance(" 7 %"), 7);
	EXPECT_EQ(ParseChance("100%"), 100);
	EXPECT_EQ(ParseChance(""), -1);
	EXPECT_EQ(ParseChance("64"), -1);
	EXPECT_EQ(ParseChance("abc%"), -1);
}

TEST(CursorModel, noCursorShowsNothing)
{
	Input in;
	in.id = NO_UICURSOR;
	State const s = Evaluate(in);
	EXPECT_FALSE(s.shown);
	EXPECT_EQ(s.mode, Mode::None);
	EXPECT_FALSE(s.chip);
}

TEST(CursorModel, aShotCarriesHitApAndAim)
{
	Input in;
	in.id = ACTION_TARGETAIM3_UICURSOR;
	in.combat = true;
	in.showAp = true;
	in.ap = 8;
	in.apLeft = 12;
	in.location = "Torso";
	in.chance = "64%";
	State const s = Evaluate(in);
	EXPECT_TRUE(s.shown);
	EXPECT_EQ(s.mode, Mode::Target);
	EXPECT_EQ(s.shape, Shape::Fire);
	EXPECT_EQ(s.tone, Tone::Foe);
	EXPECT_EQ(s.hit, 64);
	EXPECT_EQ(s.ap, 8);
	EXPECT_EQ(s.apLeft, 12);
	EXPECT_EQ(s.aim, 3);
	EXPECT_TRUE(s.why.empty());
	ASSERT_EQ(s.lines.size(), 4u);
	EXPECT_EQ(s.lines[0].kind, "where");
	EXPECT_EQ(s.lines[1].kind, "hit");
	EXPECT_EQ(s.lines[2].kind, "aim");
	EXPECT_EQ(s.lines[2].a, 2); // aim step 3 is the second of five clicks
	EXPECT_EQ(s.lines[3].kind, "ap");
	EXPECT_EQ(s.lines[3].b, 4); // 12 left, 8 spent: what remains
	EXPECT_TRUE(s.chip);
}

TEST(CursorModel, notEnoughActionPointsIsANoWithItsReason)
{
	Input in;
	in.id = ACTION_TARGETAIM1_UICURSOR;
	in.combat = true;
	in.showAp = true;
	in.apInvalid = true;
	in.ap = 9;
	in.apLeft = 4;
	State const s = Evaluate(in);
	EXPECT_EQ(s.tone, Tone::No);
	EXPECT_EQ(s.why, "no_ap");
	EXPECT_TRUE(s.chip);
}

TEST(CursorModel, actionPointsAreOnlyForCombat)
{
	Input in;
	in.id = MOVE_WALK_UICURSOR;
	in.showAp = true;
	in.ap = 7;
	State const s = Evaluate(in);
	EXPECT_EQ(s.ap, -1);
	EXPECT_FALSE(s.chip);
}

TEST(CursorModel, aHeldItemMakesTheWorldADropTarget)
{
	Input in;
	in.id = NORMAL_FREEUICURSOR;
	in.held = 1;
	in.tile = "Drop";
	State const s = Evaluate(in);
	EXPECT_EQ(s.mode, Mode::Item);
	EXPECT_EQ(s.shape, Shape::Drop);
	EXPECT_EQ(s.marker, Marker::Tile);
	ASSERT_EQ(s.lines.size(), 1u);
	EXPECT_EQ(s.lines[0].text, "Drop");
}

TEST(CursorModel, aBlockedThrowOfAHeldItemSaysWhy)
{
	Input in;
	in.id = NORMAL_FREEUICURSOR;
	in.held = 2;
	State const s = Evaluate(in);
	EXPECT_EQ(s.tone, Tone::No);
	EXPECT_EQ(s.why, "out_of_reach");
}

TEST(CursorModel, givingNeedsNoMarker)
{
	Input in;
	in.id = NORMAL_FREEUICURSOR;
	in.held = 3;
	State const s = Evaluate(in);
	EXPECT_EQ(s.shape, Shape::Give);
	EXPECT_EQ(s.marker, Marker::None);
}

TEST(CursorModel, aLockedUiIsBusy)
{
	Input in;
	in.id = NORMAL_FREEUICURSOR;
	in.busy = true;
	State const s = Evaluate(in);
	EXPECT_EQ(s.mode, Mode::Busy);
	EXPECT_EQ(s.shape, Shape::Busy);
	EXPECT_EQ(s.why, "busy");
}

TEST(CursorModel, pathIsSolidWhileThisTurnPaysForIt)
{
	PathPlan const p = PlanPath({ 2, 4, 6, 8, 10, 12 }, 12, 7, true);
	EXPECT_EQ(p.steps, 6);
	EXPECT_EQ(p.solid, 3);
	EXPECT_EQ(p.total, 12);
	EXPECT_EQ(p.now, 7);
	EXPECT_EQ(p.next, 5);
	EXPECT_TRUE(p.beyond);
}

TEST(CursorModel, pathWithinTheBudgetHasNoSpill)
{
	PathPlan const p = PlanPath({ 2, 4, 6 }, 6, 25, true);
	EXPECT_EQ(p.solid, 3);
	EXPECT_EQ(p.now, 6);
	EXPECT_EQ(p.next, 0);
	EXPECT_FALSE(p.beyond);
}

TEST(CursorModel, realTimePathIsAllSolid)
{
	PathPlan const p = PlanPath({ 10, 20, 30, 40 }, 40, 0, false);
	EXPECT_EQ(p.solid, 4);
	EXPECT_FALSE(p.beyond);
}

TEST(CursorModel, extraCostOfTheActionCountsAgainstTheBudget)
{
	// the path itself is affordable (6 of 8) but the door costs 4 more
	PathPlan const p = PlanPath({ 2, 4, 6 }, 10, 8, true);
	EXPECT_EQ(p.solid, 3);
	EXPECT_EQ(p.now, 8);
	EXPECT_EQ(p.next, 2);
	EXPECT_TRUE(p.beyond);
}
