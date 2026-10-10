#include "PopupModels.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace PopupModels;

namespace
{
	Row const& RowOf(Menu const& m, int cmd)
	{
		Row const* const r = m.Find(cmd);
		EXPECT_NE(r, nullptr) << "no row " << cmd;
		static Row none;
		return r ? *r : none;
	}
	Row const& A(Menu const& m, ActionCmd c) { return RowOf(m, int(c)); }
	Row const& D(Menu const& m, DoorCmd c) { return RowOf(m, int(c)); }
}

TEST(PopupModels, everyWhyHasAKeyOfItsOwn)
{
	std::set<std::string> keys;
	for (int i = int(Why::None) + 1; i < int(Why::Count); ++i)
	{
		std::string const k = WhyKey(Why(i));
		EXPECT_NE(k, "") << "reason " << i;
		EXPECT_TRUE(keys.insert(k).second) << "two reasons share the key " << k;
	}
	EXPECT_STREQ(WhyKey(Why::None), "");
}

// ---- the action menu ------------------------------------------------------------------------------------

TEST(PopupModels, actionMenuGroupsMoveActAndCancel)
{
	Menu const m = BuildActionMenu({});
	ASSERT_EQ(m.rows.size(), 9u);
	for (ActionCmd c : { ActionCmd::Walk, ActionCmd::Run, ActionCmd::Sneak, ActionCmd::Crawl }) EXPECT_EQ(A(m, c).group, 0);
	for (ActionCmd c : { ActionCmd::Act, ActionCmd::Look, ActionCmd::Talk, ActionCmd::Hand }) EXPECT_EQ(A(m, c).group, 1);
	EXPECT_EQ(A(m, ActionCmd::Cancel).group, 2);
	// every command has a name
	std::set<std::string> names;
	for (int i = 0; i < int(ActionCmd::Count); ++i) EXPECT_TRUE(names.insert(ActionCmdName(i)).second);
}

TEST(PopupModels, actionMenuWithNothingInHandCannotAct)
{
	Menu const m = BuildActionMenu({});
	EXPECT_FALSE(A(m, ActionCmd::Act).enabled);
	EXPECT_EQ(A(m, ActionCmd::Act).why, Why::NoUse);
	EXPECT_TRUE(A(m, ActionCmd::Walk).enabled);
	EXPECT_TRUE(A(m, ActionCmd::Cancel).enabled);
}

TEST(PopupModels, actionMenuActionCostsWhatShootingCosts)
{
	ActionInput in;
	in.hand = HandItem::Gun;
	in.actAp = 8;
	Menu m = BuildActionMenu(in);
	EXPECT_TRUE(A(m, ActionCmd::Act).enabled);
	EXPECT_EQ(A(m, ActionCmd::Act).ap, 8);
	in.hand = HandItem::Blade;
	in.actAp = 6;
	EXPECT_EQ(A(BuildActionMenu(in), ActionCmd::Act).ap, 6);
	// a grenade or a medkit has no fixed cost in the menu
	in.hand = HandItem::Explosive;
	m = BuildActionMenu(in);
	EXPECT_TRUE(A(m, ActionCmd::Act).enabled);
	EXPECT_EQ(A(m, ActionCmd::Act).ap, -1);
}

TEST(PopupModels, toolkitAndWirecuttersAreActions)
{
	ActionInput in;
	in.hand = HandItem::Toolkit;
	EXPECT_TRUE(A(BuildActionMenu(in), ActionCmd::Act).enabled);
	in.hand = HandItem::Wirecutters;
	EXPECT_TRUE(A(BuildActionMenu(in), ActionCmd::Act).enabled);
}

TEST(PopupModels, vehicleCannotRunLookTalkOrHandAndSaysWhy)
{
	ActionInput in;
	in.vehicle = true;
	Menu const m = BuildActionMenu(in);
	for (ActionCmd c : { ActionCmd::Run, ActionCmd::Look, ActionCmd::Talk, ActionCmd::Hand, ActionCmd::Act })
	{
		EXPECT_FALSE(A(m, c).enabled) << ActionCmdName(int(c));
		EXPECT_EQ(A(m, c).why, Why::Vehicle) << ActionCmdName(int(c));
	}
	EXPECT_TRUE(A(m, ActionCmd::Walk).enabled); // drive
}

TEST(PopupModels, escortCannotTalkHandOrAct)
{
	ActionInput in;
	in.escort = true;
	in.hand = HandItem::Gun;
	Menu const m = BuildActionMenu(in);
	for (ActionCmd c : { ActionCmd::Act, ActionCmd::Talk, ActionCmd::Hand }) EXPECT_EQ(A(m, c).why, Why::Escort);
	EXPECT_TRUE(A(m, ActionCmd::Look).enabled);
}

TEST(PopupModels, robotAndWaterAndStances)
{
	ActionInput in;
	in.robot = true;
	EXPECT_EQ(A(BuildActionMenu(in), ActionCmd::Run).why, Why::Robot);
	in = {};
	in.inWater = true;
	EXPECT_EQ(A(BuildActionMenu(in), ActionCmd::Run).why, Why::Water);
	in = {};
	in.canCrouch = false;
	in.canProne = false;
	Menu const m = BuildActionMenu(in);
	EXPECT_EQ(A(m, ActionCmd::Sneak).why, Why::Stance);
	EXPECT_EQ(A(m, ActionCmd::Crawl).why, Why::Stance);
	in = {};
	in.robotUncontrolled = true;
	Menu const r = BuildActionMenu(in);
	EXPECT_EQ(A(r, ActionCmd::Walk).why, Why::ControlRobot);
	EXPECT_EQ(A(r, ActionCmd::Look).why, Why::ControlRobot);
}

TEST(PopupModels, aDisabledRowCannotBeChosen)
{
	ActionInput in;
	in.vehicle = true;
	Menu const m = BuildActionMenu(in);
	EXPECT_FALSE(m.CanChoose(int(ActionCmd::Run)));
	EXPECT_TRUE(m.CanChoose(int(ActionCmd::Cancel)));
	EXPECT_FALSE(m.CanChoose(99));
}

// ---- the door menu --------------------------------------------------------------------------------------

namespace
{
	DoorInput FullKit()
	{
		DoorInput d;
		d.hasKey = d.hasCrowbar = d.hasLockpick = d.hasCharge = true;
		return d;
	}
}

TEST(PopupModels, doorMenuGroupsTheDoorTheToolsAndCancel)
{
	Menu const m = BuildDoorMenu(FullKit());
	ASSERT_EQ(m.rows.size(), 9u);
	for (DoorCmd c : { DoorCmd::Open, DoorCmd::Examine, DoorCmd::Untrap }) EXPECT_EQ(D(m, c).group, 0);
	for (DoorCmd c : { DoorCmd::Keyring, DoorCmd::Lockpick, DoorCmd::Crowbar, DoorCmd::Boot, DoorCmd::Explosive }) EXPECT_EQ(D(m, c).group, 1);
	EXPECT_EQ(D(m, DoorCmd::Cancel).group, 2);
	for (Row const& r : m.rows) EXPECT_TRUE(r.enabled) << DoorCmdName(r.cmd);
}

TEST(PopupModels, doorToolsNeedTheirItemAndSayWhat)
{
	DoorInput d;
	Menu const m = BuildDoorMenu(d);
	EXPECT_EQ(D(m, DoorCmd::Keyring).why, Why::NoKey);
	EXPECT_EQ(D(m, DoorCmd::Crowbar).why, Why::NoCrowbar);
	EXPECT_EQ(D(m, DoorCmd::Lockpick).why, Why::NoLockpick);
	EXPECT_EQ(D(m, DoorCmd::Explosive).why, Why::NoCharge);
	EXPECT_TRUE(D(m, DoorCmd::Boot).enabled);
	EXPECT_TRUE(D(m, DoorCmd::Open).enabled);
}

TEST(PopupModels, closingADoorOffersOnlyTheClose)
{
	DoorInput d = FullKit();
	d.closing = true;
	Menu const m = BuildDoorMenu(d);
	EXPECT_TRUE(D(m, DoorCmd::Open).enabled);
	EXPECT_TRUE(D(m, DoorCmd::Cancel).enabled);
	for (DoorCmd c : { DoorCmd::Examine, DoorCmd::Untrap, DoorCmd::Keyring, DoorCmd::Lockpick, DoorCmd::Crowbar, DoorCmd::Boot, DoorCmd::Explosive })
	{
		EXPECT_FALSE(D(m, c).enabled) << DoorCmdName(int(c));
		EXPECT_EQ(D(m, c).why, Why::NotHere) << DoorCmdName(int(c));
	}
}

TEST(PopupModels, anExaminedDoorIsNotExaminedAgain)
{
	DoorInput d = FullKit();
	d.trap = Trap::ProvedTrapped;
	EXPECT_EQ(D(BuildDoorMenu(d), DoorCmd::Examine).why, Why::Examined);
	EXPECT_TRUE(D(BuildDoorMenu(d), DoorCmd::Untrap).enabled);
	d.trap = Trap::ProvedUntrapped;
	Menu const m = BuildDoorMenu(d);
	EXPECT_EQ(D(m, DoorCmd::Examine).why, Why::Examined);
	EXPECT_EQ(D(m, DoorCmd::Untrap).why, Why::NoTrap);
}

TEST(PopupModels, anEscortCannotUseTheToolsEvenOnAProvedTrap)
{
	DoorInput d = FullKit();
	d.escort = true;
	d.trap = Trap::ProvedTrapped;
	Menu const m = BuildDoorMenu(d);
	EXPECT_FALSE(D(m, DoorCmd::Untrap).enabled); // the legacy menu enabled this one by mistake
	EXPECT_FALSE(D(m, DoorCmd::Boot).enabled);
	EXPECT_TRUE(D(m, DoorCmd::Open).enabled);
}

TEST(PopupModels, diagonalLeavesOnlyOpenAndCancel)
{
	DoorInput d = FullKit();
	d.diagonal = true;
	Menu const m = BuildDoorMenu(d);
	for (Row const& r : m.rows)
	{
		bool const free = r.cmd == int(DoorCmd::Open) || r.cmd == int(DoorCmd::Cancel);
		EXPECT_EQ(r.enabled, free) << DoorCmdName(r.cmd);
		if (!free) EXPECT_EQ(r.why, Why::Diagonal);
	}
}

TEST(PopupModels, theCostIsAReasonOnlyWhenNothingElseIs)
{
	DoorInput d = FullKit();
	d.ap[int(DoorCmd::Boot)] = 8;
	d.affordable[int(DoorCmd::Boot)] = false;
	d.ap[int(DoorCmd::Lockpick)] = 10;
	d.affordable[int(DoorCmd::Lockpick)] = false;
	d.hasLockpick = false;
	Menu const m = BuildDoorMenu(d);
	EXPECT_EQ(D(m, DoorCmd::Boot).why, Why::NoAp);
	EXPECT_EQ(D(m, DoorCmd::Boot).ap, 8);
	EXPECT_EQ(D(m, DoorCmd::Lockpick).why, Why::NoLockpick); // no kit comes first
	EXPECT_EQ(D(m, DoorCmd::Cancel).ap, -1);
}

// ---- the pick-up list -----------------------------------------------------------------------------------

TEST(PopupModels, pickupPagesBySix)
{
	PickupList p(14);
	EXPECT_EQ(p.Pages(), 3);
	EXPECT_EQ(p.Rows(), 6);
	EXPECT_FALSE(p.CanUp());
	EXPECT_TRUE(p.CanDown());
	p.Scroll(1);
	p.Scroll(1);
	EXPECT_EQ(p.Page(), 2);
	EXPECT_EQ(p.Rows(), 2);
	EXPECT_FALSE(p.CanDown());
	p.Scroll(1); // past the end: nothing
	EXPECT_EQ(p.Page(), 2);
	p.Scroll(-1);
	EXPECT_EQ(p.Page(), 1);
}

TEST(PopupModels, pickupRowsToggleOnTheirPageAndCountAcrossPages)
{
	PickupList p(8);
	EXPECT_FALSE(p.Any());
	EXPECT_TRUE(p.Toggle(0));
	p.Scroll(1);
	EXPECT_EQ(p.Rows(), 2);
	EXPECT_TRUE(p.Toggle(1));
	EXPECT_FALSE(p.Toggle(2)); // there are only two rows here
	EXPECT_EQ(p.Count(), 2);
	EXPECT_TRUE(p.IsSelected(0));
	EXPECT_TRUE(p.IsSelected(7));
	EXPECT_FALSE(p.IsSelected(1));
	EXPECT_TRUE(p.Toggle(1)); // and back
	EXPECT_EQ(p.Count(), 1);
}

TEST(PopupModels, pickupAllIsEverythingThenNothing)
{
	PickupList p(9);
	p.Toggle(2);
	p.ToggleAll();
	EXPECT_TRUE(p.AllSelected());
	EXPECT_EQ(p.Count(), 9);
	p.ToggleAll();
	EXPECT_FALSE(p.Any());
	// ticking the last unticked row makes "all" read as on
	p.ToggleAll();
	p.Toggle(0);
	EXPECT_FALSE(p.AllSelected());
	p.Toggle(0);
	EXPECT_TRUE(p.AllSelected());
}

TEST(PopupModels, anEmptyPickupListIsHarmless)
{
	PickupList p(0);
	EXPECT_EQ(p.Pages(), 0);
	EXPECT_EQ(p.Rows(), 0);
	EXPECT_FALSE(p.Toggle(0));
	EXPECT_FALSE(p.AllSelected());
	p.ToggleAll();
	EXPECT_FALSE(p.Any());
}

// ---- the stack popup ------------------------------------------------------------------------------------

TEST(PopupModels, stackClickTakesPutsOrDoesNothing)
{
	EXPECT_EQ(PlanStackClick(1, 4, false), StackClick::Take);
	EXPECT_EQ(PlanStackClick(4, 4, false), StackClick::Nothing); // an empty box
	EXPECT_EQ(PlanStackClick(4, 4, true), StackClick::Put);
	EXPECT_EQ(PlanStackClick(0, 4, true), StackClick::Put);
	EXPECT_EQ(PlanStackClick(-1, 4, false), StackClick::Nothing);
}

TEST(PopupModels, stackSplitStaysInsideTheStack)
{
	StackSplit s(4);
	EXPECT_EQ(s.Take(), 1);
	EXPECT_FALSE(s.CanLess());
	s.More(); s.More(); s.More(); s.More();
	EXPECT_EQ(s.Take(), 4);
	EXPECT_FALSE(s.CanMore());
	s.SetCount(2); // two were taken
	EXPECT_EQ(s.Take(), 2);
	s.SetCount(0);
	EXPECT_EQ(s.Take(), 0);
	EXPECT_FALSE(s.CanMore());
	StackSplit none(0);
	EXPECT_EQ(none.Take(), 0);
}

// ---- the key ring ---------------------------------------------------------------------------------------

TEST(PopupModels, aKeyOpensTheDoorInFrontOnlyWhenItFits)
{
	KeyRingInput ring;
	ring.keys = { { 0, 1, 7 }, { 1, 2, 9 }, { 2, 0, 3 } };
	// no door: Use is off for every key and says so
	auto rows = BuildKeyRing(ring);
	ASSERT_EQ(rows.size(), 2u); // the empty slot is not a key
	for (KeyRowOut const& r : rows) { EXPECT_FALSE(r.canUse); EXPECT_EQ(r.why, Why::NoDoor); EXPECT_FALSE(r.fits); }

	ring.doorInFront = true;
	ring.doorLocked = true;
	ring.doorLockId = 9;
	rows = BuildKeyRing(ring);
	EXPECT_FALSE(rows[0].canUse);
	EXPECT_EQ(rows[0].why, Why::WrongKey);
	EXPECT_TRUE(rows[1].canUse);
	EXPECT_TRUE(rows[1].fits);
	EXPECT_EQ(rows[1].count, 2);
}

TEST(PopupModels, anUnlockedDoorAndAMercWithoutApAreReasons)
{
	KeyRingInput ring;
	ring.keys = { { 0, 1, 5 } };
	ring.doorInFront = true;
	ring.doorLockId = 5;
	ring.doorLocked = false;
	EXPECT_EQ(BuildKeyRing(ring)[0].why, Why::Unlocked);
	ring.doorLocked = true;
	ring.canPay = false;
	EXPECT_EQ(BuildKeyRing(ring)[0].why, Why::NoAp);
	ring.canPay = true;
	EXPECT_TRUE(BuildKeyRing(ring)[0].canUse);
}

TEST(PopupModels, droppingAKeyOnADoorIsTheSameVerdictAsUse)
{
	KeyRingInput ring;
	ring.doorInFront = true;
	ring.doorLocked = true;
	ring.doorLockId = 12;
	KeyRowOut const ok = PlanKeyOnDoor({ 3, 1, 12 }, ring);
	EXPECT_TRUE(ok.canUse);
	EXPECT_EQ(ok.slot, 3);
	KeyRowOut const bad = PlanKeyOnDoor({ 4, 1, 11 }, ring);
	EXPECT_FALSE(bad.canUse);
	EXPECT_EQ(bad.why, Why::WrongKey);
}

// ---- the talk panel -------------------------------------------------------------------------------------

TEST(PopupModels, talkApproachesWaitWhileSomeoneSpeaks)
{
	auto rows = BuildTalkMenu({});
	ASSERT_EQ(rows.size(), size_t(Approach::Count));
	for (TalkRow const& r : rows) EXPECT_TRUE(r.enabled);
	TalkInput in;
	in.speaking = true;
	rows = BuildTalkMenu(in);
	for (TalkRow const& r : rows) { EXPECT_FALSE(r.enabled); EXPECT_EQ(r.why, Why::Speaking); }
	std::set<std::string> names;
	for (int i = 0; i < int(Approach::Count); ++i) EXPECT_TRUE(names.insert(ApproachName(i)).second);
}

// ---- the sector exit menu -------------------------------------------------------------------------------

namespace
{
	ExitInput Squad()
	{
		ExitInput in;
		in.okAll = true;
		in.shortTrip = true;
		in.squadSize = 4;
		in.otherMercInSquad = true;
		in.controllableMercs = 4;
		return in;
	}
}

TEST(PopupModels, exitWholeSquadLoadsTheNextSectorWhenTheTripIsShort)
{
	ExitInput const in = Squad();
	ExitState const s = OpenExit(in);
	EXPECT_TRUE(s.all);
	EXPECT_FALSE(s.single);
	EXPECT_TRUE(s.load);
	EXPECT_TRUE(s.loadOff); // nobody else is here: the load is not a choice
	EXPECT_EQ(s.loadWhy, Why::MustLoad);
	EXPECT_EQ(Confirm(s), ExitJump::AllLoad);
}

TEST(PopupModels, exitLongTripGoesToTheMap)
{
	ExitInput in = Squad();
	in.shortTrip = false;
	ExitState const s = OpenExit(in);
	EXPECT_EQ(s.loadWhy, Why::MustTravel);
	EXPECT_EQ(Confirm(s), ExitJump::AllNoLoad);
}

TEST(PopupModels, exitSingleMercCanChooseToLoadOrWalk)
{
	ExitInput in;
	in.okSingle = true;
	in.shortTrip = true;
	in.squadSize = 4;
	in.otherMercInSquad = true;
	in.multipleSquads = true;
	ExitState s = OpenExit(in);
	EXPECT_TRUE(s.single);
	EXPECT_TRUE(s.allOff);
	EXPECT_EQ(s.allWhy, Why::NeedsTogether);
	EXPECT_FALSE(s.loadOff);
	EXPECT_FALSE(s.load);
	ToggleLoad(s);
	EXPECT_TRUE(s.load);
	EXPECT_EQ(Confirm(s), ExitJump::SingleLoad);
	ToggleLoad(s);
	EXPECT_EQ(Confirm(s), ExitJump::SingleNoLoad);
}

TEST(PopupModels, exitCannotLoadWhenSeveralSquadsAreInAHostileSector)
{
	ExitInput in;
	in.okAll = true;
	in.shortTrip = true;
	in.squadSize = 3;
	in.otherMercInSquad = true;
	in.multipleSquads = true;
	in.enemyInSector = true;
	ExitState s = OpenExit(in);
	EXPECT_TRUE(s.loadOff);
	EXPECT_FALSE(s.load);
	EXPECT_EQ(s.loadWhy, Why::Hostile);
	ToggleLoad(s); // off: ignored
	EXPECT_FALSE(s.load);
	EXPECT_EQ(Confirm(s), ExitJump::AllNoLoad);
}

TEST(PopupModels, exitMilitiaWouldHaveToFightOnSoTheNextSectorStaysUnloaded)
{
	ExitInput in = Squad();
	in.enemyInSector = true;
	in.militiaInSector = 5;
	in.multipleSquads = true;
	ExitState const s = OpenExit(in);
	EXPECT_TRUE(s.loadOff);
	EXPECT_FALSE(s.load);
}

TEST(PopupModels, exitInCombatOnlyTheLastActingMercCanLoad)
{
	ExitInput in;
	in.okSingle = true;
	in.shortTrip = true;
	in.combat = true;
	in.controllableMercs = 3;
	in.squadSize = 3;
	in.otherMercInSquad = true;
	in.multipleSquads = true;
	EXPECT_TRUE(OpenExit(in).loadOff);
	in.controllableMercs = 1;
	EXPECT_FALSE(OpenExit(in).loadOff);
}

TEST(PopupModels, anEscortMustTravelWithTheSquad)
{
	ExitInput in;
	in.okSingle = true;
	in.shortTrip = true;
	in.selectedIsEscort = true;
	in.squadSize = 3;
	ExitState const s = OpenExit(in);
	EXPECT_TRUE(s.singleOff);
	EXPECT_EQ(s.singleWhy, Why::MustBeEscorted);
	EXPECT_TRUE(s.allOn);
	// the choice stays where it was: it cannot be changed to single
	ExitState t = s;
	ExitInput const keep = in;
	ChooseSingle(t, keep);
	EXPECT_EQ(t.single, s.single);
}

TEST(PopupModels, aMercCannotLeaveAnEscortAlone)
{
	ExitInput in;
	in.okAll = true;
	in.shortTrip = true;
	in.squadSize = 2;
	in.otherMercInSquad = false;
	in.escortsInSquad = 1;
	ExitState s = OpenExit(in);
	EXPECT_TRUE(s.singleOff);
	EXPECT_EQ(s.singleWhy, Why::WouldIsolate);
	in.escortsInSquad = 2;
	EXPECT_EQ(OpenExit(in).singleWhy, Why::WouldIsolateMany);
	in.otherMercInSquad = true; // somebody else is there to go with him
	EXPECT_FALSE(OpenExit(in).singleOff);
}

TEST(PopupModels, choosingBetweenTheRadiosMovesTheLoadRules)
{
	ExitInput in;
	in.okAll = true;
	in.shortTrip = true;
	in.squadSize = 4;
	in.otherMercInSquad = true;
	in.multipleSquads = true;   // another squad is here, so each choice is real
	ExitState s = OpenExit(in);
	EXPECT_TRUE(s.all);
	ChooseSingle(s, in);
	EXPECT_TRUE(s.single);
	EXPECT_FALSE(s.all);
	EXPECT_FALSE(s.load);       // leaving part of the force: the load is off by default
	EXPECT_FALSE(s.loadOff);
	ToggleLoad(s);
	EXPECT_TRUE(s.load);
	ChooseAll(s, in);
	EXPECT_TRUE(s.all);
	EXPECT_FALSE(s.single);
}

TEST(PopupModels, theUncontrolledRobotSaysWhyEveryoneCannotGo)
{
	ExitInput in;
	in.okSingle = true;
	in.robotUncontrolled = true;
	in.shortTrip = true;
	in.otherMercInSquad = true;
	ExitState const s = OpenExit(in);
	EXPECT_TRUE(s.allOff);
	EXPECT_EQ(s.allWhy, Why::ControlRobot);
}

TEST(PopupModels, nothingSelectedConfirmsNothing)
{
	ExitState s;
	EXPECT_EQ(Confirm(s), ExitJump::None);
	EXPECT_STREQ(ExitJumpName(ExitJump::SingleLoad), "single_load");
}
