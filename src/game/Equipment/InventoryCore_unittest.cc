#include "gtest/gtest.h"

#include "InventoryCore.h"

using namespace Equipment;

namespace
{
	constexpr uint16_t VEST_ITEM = 7, JACKET = 8, SCOPE = 20, ARMOUR_PLATE = 21, RIFLE = 30, CANTEEN = 40;
	constexpr int VEST_SLOT = 3, HAND_SLOT = 5, POCKET = 12;
	constexpr int NAILS = 99;

	InvRelations Rules()
	{
		InvRelations r;
		r.canAttach = [](uint16_t held, uint16_t host) { return held == SCOPE && host == RIFLE; };
		r.canMerge  = [](uint16_t held, uint16_t target) { return held == ARMOUR_PLATE && target == ARMOUR_PLATE; };
		// Nails keeps his vest: only his leather jacket goes into that slot
		r.fetish    = [](InvParty const& m, int slot, uint16_t item) {
			return m.merc == NAILS && slot == VEST_SLOT && item != JACKET;
		};
		return r;
	}

	InvParty Merc(int id, bool conscious = true, bool canPay = true)
	{
		InvParty p;
		p.merc      = id;
		p.conscious = conscious;
		p.canPay    = canPay;
		return p;
	}

	HeldStack Hold(uint16_t item, InvParty from, int count = 1)
	{
		HeldStack h;
		h.item     = item;
		h.count    = count;
		h.from     = from;
		h.fromSlot = POCKET;
		return h;
	}
}

TEST(InventoryCore, HandIsEmptyUntilSet)
{
	InventoryCore c(Rules());
	EXPECT_FALSE(c.Holding());
	c.SetHand(Hold(RIFLE, Merc(1)));
	EXPECT_TRUE(c.Holding());
	EXPECT_EQ(c.Hand().item, RIFLE);
	c.ClearHand();
	EXPECT_FALSE(c.Holding());
}

TEST(InventoryCore, TakeFromEmptyPocketIsRefused)
{
	InventoryCore c(Rules());
	TakeVerdict const v = c.PlanTake(Merc(1), POCKET, 0, false);
	EXPECT_EQ(v.kind, TakeKind::Refused);
	EXPECT_EQ(v.why, InvWhy::NothingHere);
}

TEST(InventoryCore, TakePicksUpAndCtrlCleansUp)
{
	InventoryCore c(Rules());
	EXPECT_EQ(c.PlanTake(Merc(1), POCKET, RIFLE, false).kind, TakeKind::Take);
	EXPECT_EQ(c.PlanTake(Merc(1), POCKET, RIFLE, true).kind, TakeKind::CleanUp);
}

TEST(InventoryCore, NailsKeepsHisVest)
{
	InventoryCore c(Rules());
	TakeVerdict const v = c.PlanTake(Merc(NAILS), VEST_SLOT, VEST_ITEM, false);
	EXPECT_EQ(v.kind, TakeKind::Refused);
	EXPECT_EQ(v.why, InvWhy::NailsVest);
	// the same pocket on anybody else is fine
	EXPECT_EQ(c.PlanTake(Merc(1), VEST_SLOT, VEST_ITEM, false).kind, TakeKind::Take);
}

TEST(InventoryCore, NailsTakesOnlyHisJacketIntoTheVestSlot)
{
	InventoryCore c(Rules());
	c.SetHand(Hold(VEST_ITEM, Merc(NAILS)));
	PlaceVerdict const v = c.PlanPlace(Merc(NAILS), VEST_SLOT, 0, true, false);
	EXPECT_EQ(v.kind, PlaceKind::Refused);
	EXPECT_TRUE(v.fetish);
	c.SetHand(Hold(JACKET, Merc(NAILS)));
	EXPECT_EQ(c.PlanPlace(Merc(NAILS), VEST_SLOT, 0, true, false).kind, PlaceKind::Put);
}

TEST(InventoryCore, PlacingOnYourselfCostsNothing)
{
	InventoryCore c(Rules());
	c.SetHand(Hold(RIFLE, Merc(1)));
	PlaceVerdict const v = c.PlanPlace(Merc(1), POCKET + 1, 0, false, false);
	EXPECT_EQ(v.kind, PlaceKind::Put);
	EXPECT_EQ(v.apFrom, 0);
	EXPECT_EQ(v.apTo, 0);
	EXPECT_FALSE(v.passed);
}

TEST(InventoryCore, PassingToAnotherMercCostsTwoEach)
{
	InventoryCore c(Rules());
	c.SetHand(Hold(RIFLE, Merc(1)));
	PlaceVerdict const v = c.PlanPlace(Merc(2), POCKET, 0, false, false);
	EXPECT_EQ(v.kind, PlaceKind::Put);
	EXPECT_EQ(v.apFrom, PASS_COST_AP);
	EXPECT_EQ(v.apTo, PASS_COST_AP);
	EXPECT_TRUE(v.passed);
}

TEST(InventoryCore, PassingNeedsThreeApOnBothSides)
{
	InventoryCore c(Rules());
	c.SetHand(Hold(RIFLE, Merc(1, true, false)));
	EXPECT_EQ(c.PlanPlace(Merc(2), POCKET, 0, false, false).why, InvWhy::NoAP);

	c.SetHand(Hold(RIFLE, Merc(1)));
	EXPECT_EQ(c.PlanPlace(Merc(2, true, false), POCKET, 0, false, false).why, InvWhy::NoAPTarget);
}

TEST(InventoryCore, UnconsciousMercsNeitherPayNorCheck)
{
	InventoryCore c(Rules());
	// the giver is down: no check, no charge on his side
	c.SetHand(Hold(RIFLE, Merc(1, false, false)));
	PlaceVerdict v = c.PlanPlace(Merc(2), POCKET, 0, false, false);
	EXPECT_EQ(v.kind, PlaceKind::Put);
	EXPECT_EQ(v.apFrom, 0);
	EXPECT_EQ(v.apTo, PASS_COST_AP);
	// the receiver is down: the giver pays, the receiver does not
	c.SetHand(Hold(RIFLE, Merc(1)));
	v = c.PlanPlace(Merc(2, false, false), POCKET, 0, false, false);
	EXPECT_EQ(v.kind, PlaceKind::Put);
	EXPECT_EQ(v.apFrom, PASS_COST_AP);
	EXPECT_EQ(v.apTo, 0);
}

TEST(InventoryCore, ItemFromTheGroundCostsNothing)
{
	InventoryCore c(Rules());
	c.SetHand(Hold(RIFLE, Merc(-1)));
	PlaceVerdict const v = c.PlanPlace(Merc(2), POCKET, 0, false, false);
	EXPECT_EQ(v.kind, PlaceKind::Put);
	EXPECT_EQ(v.apFrom + v.apTo, 0);
	EXPECT_FALSE(v.passed);
}

TEST(InventoryCore, DroppingOnAHeldGunAttaches)
{
	InventoryCore c(Rules());
	c.SetHand(Hold(SCOPE, Merc(1)));
	EXPECT_EQ(c.PlanPlace(Merc(1), HAND_SLOT, RIFLE, true, false).kind, PlaceKind::Attach);
}

TEST(InventoryCore, AttachOnlyOnHostPockets)
{
	InventoryCore c(Rules());
	c.SetHand(Hold(SCOPE, Merc(1)));
	// a plain pocket swaps instead
	EXPECT_EQ(c.PlanPlace(Merc(1), POCKET, RIFLE, false, false).kind, PlaceKind::Put);
}

TEST(InventoryCore, MergeAsksAQuestionFirst)
{
	InventoryCore c(Rules());
	c.SetHand(Hold(ARMOUR_PLATE, Merc(1)));
	PlaceVerdict const v = c.PlanPlace(Merc(1), HAND_SLOT, ARMOUR_PLATE, true, false);
	EXPECT_EQ(v.kind, PlaceKind::AskMerge);
	EXPECT_FALSE(c.Asking());
	Question q;
	q.kind = QuestionKind::Merge;
	q.merc = 1;
	q.slot = HAND_SLOT;
	c.Ask(q);
	EXPECT_TRUE(c.Asking());
	Question const a = c.Answer();
	EXPECT_EQ(a.kind, QuestionKind::Merge);
	EXPECT_EQ(a.slot, HAND_SLOT);
	EXPECT_FALSE(c.Asking());
}

TEST(InventoryCore, AttachBeatsMergeWhenBothApply)
{
	InvRelations r = Rules();
	r.canAttach = [](uint16_t, uint16_t) { return true; };
	r.canMerge  = [](uint16_t, uint16_t) { return true; };
	InventoryCore c(r);
	c.SetHand(Hold(ARMOUR_PLATE, Merc(1)));
	EXPECT_EQ(c.PlanPlace(Merc(1), HAND_SLOT, ARMOUR_PLATE, true, false).kind, PlaceKind::Attach);
}

TEST(InventoryCore, CtrlClickGathersTheStack)
{
	InventoryCore c(Rules());
	c.SetHand(Hold(CANTEEN, Merc(1), 2));
	EXPECT_EQ(c.PlanPlace(Merc(1), POCKET, CANTEEN, false, true).kind, PlaceKind::CleanUp);
}

TEST(InventoryCore, PlacingWithAnEmptyHandIsRefused)
{
	InventoryCore c(Rules());
	EXPECT_EQ(c.PlanPlace(Merc(1), POCKET, 0, false, false).why, InvWhy::HandEmpty);
}

TEST(InventoryCore, ARefusedFitSaysSo)
{
	InvRelations r = Rules();
	r.fits = [](InvParty const&, int slot, uint16_t item) { return !(slot == POCKET && item == RIFLE); };
	InventoryCore c(r);
	c.SetHand(Hold(RIFLE, Merc(1)));
	PlaceVerdict const v = c.PlanPlace(Merc(1), POCKET, 0, false, false);
	EXPECT_EQ(v.kind, PlaceKind::Refused);
	EXPECT_EQ(v.why, InvWhy::DoesNotFit);
	// the same item is fine in the hand slot, and Ctrl never needs it to fit
	EXPECT_EQ(c.PlanPlace(Merc(1), HAND_SLOT, 0, true, false).kind, PlaceKind::Put);
	EXPECT_EQ(c.PlanPlace(Merc(1), POCKET, RIFLE, false, true).kind, PlaceKind::CleanUp);
}

TEST(InventoryCore, ARemoteMercCannotBeReached)
{
	InventoryCore c(Rules());
	InvParty far = Merc(2);
	far.inReach = false;
	EXPECT_EQ(c.PlanTake(far, POCKET, RIFLE, false).why, InvWhy::OutOfReach);
	c.SetHand(Hold(RIFLE, Merc(1)));
	EXPECT_EQ(c.PlanPlace(far, POCKET, 0, false, false).why, InvWhy::OutOfReach);
}

TEST(InventoryCore, ActivateOpensStackOrDescription)
{
	InventoryCore c(Rules());
	EXPECT_EQ(c.PlanActivate(0, 0, 0, true), ActivateKind::Nothing);
	EXPECT_EQ(c.PlanActivate(CANTEEN, 3, 4, true), ActivateKind::OpenStack);
	EXPECT_EQ(c.PlanActivate(CANTEEN, 1, 4, true), ActivateKind::Describe);
	EXPECT_EQ(c.PlanActivate(CANTEEN, 3, 0, true), ActivateKind::Describe);
	// the map screen has no stack popup
	EXPECT_EQ(c.PlanActivate(CANTEEN, 3, 4, false), ActivateKind::Describe);
}

TEST(InventoryCore, ApplyNeedsAHandAndAConsciousMerc)
{
	InventoryCore c(Rules());
	EXPECT_EQ(c.PlanApply(Merc(1)), InvWhy::HandEmpty);
	c.SetHand(Hold(CANTEEN, Merc(1)));
	EXPECT_EQ(c.PlanApply(Merc(1, false)), InvWhy::Unconscious);
	EXPECT_EQ(c.PlanApply(Merc(1)), InvWhy::None);
}

TEST(InventoryCore, GiveFollowsThePassingRules)
{
	InventoryCore c(Rules());
	c.SetHand(Hold(RIFLE, Merc(1)));
	PlaceVerdict const v = c.PlanGive(Merc(2));
	EXPECT_EQ(v.kind, PlaceKind::Put);
	EXPECT_EQ(v.apFrom, PASS_COST_AP);
	EXPECT_EQ(v.apTo, PASS_COST_AP);
	c.SetHand(Hold(RIFLE, Merc(1, true, false)));
	EXPECT_EQ(c.PlanGive(Merc(2)).why, InvWhy::NoAP);
}

TEST(InventoryCore, SheetAttachRules)
{
	InventoryCore c(Rules());
	SheetAttachRequest r;
	r.sheetIsAttachment = true;
	EXPECT_EQ(c.PlanSheetAttach(r).kind, SheetAttachKind::Ignored);

	r = SheetAttachRequest{};
	r.shopOwned = true;
	EXPECT_EQ(c.PlanSheetAttach(r).kind, SheetAttachKind::Ignored);

	// attach what is in the hand: costs the reload AP
	r = SheetAttachRequest{};
	r.handFull = true;
	EXPECT_EQ(c.PlanSheetAttach(r).kind, SheetAttachKind::Attach);
	r.payerCanPay = false;
	EXPECT_EQ(c.PlanSheetAttach(r).why, InvWhy::NoAP);

	// an inseparable attachment asks first
	r = SheetAttachRequest{};
	r.handFull = true;
	r.handInseparable = true;
	r.inseparableValid = true;
	EXPECT_EQ(c.PlanSheetAttach(r).kind, SheetAttachKind::AskPermanent);

	// an empty hand takes the attachment out
	r = SheetAttachRequest{};
	r.slotOccupied = true;
	EXPECT_EQ(c.PlanSheetAttach(r).kind, SheetAttachKind::Detach);
	r.slotOccupied = false;
	EXPECT_EQ(c.PlanSheetAttach(r).why, InvWhy::NothingToTake);
	r.slotOccupied = true;
	r.payerCanPay = false;
	EXPECT_EQ(c.PlanSheetAttach(r).why, InvWhy::NoAP);
}

TEST(InventoryCore, UnloadRules)
{
	InventoryCore c(Rules());
	EXPECT_TRUE(c.PlanUnload(true, true, false).ok);
	EXPECT_EQ(c.PlanUnload(true, true, true).why, InvWhy::HandFull);
	EXPECT_EQ(c.PlanUnload(true, false, false).why, InvWhy::NothingToUnload);
	EXPECT_EQ(c.PlanUnload(false, true, false).why, InvWhy::NothingToUnload);
}

TEST(MoneySplit, StepsMoveMoneyToTheHand)
{
	MoneySplit m(2500);
	EXPECT_EQ(m.Add(1000), InvWhy::None);
	EXPECT_EQ(m.Add(100), InvWhy::None);
	EXPECT_EQ(m.Removing(), 1100u);
	EXPECT_EQ(m.Remaining(), 1400u);
	EXPECT_EQ(m.Remove(100), InvWhy::None);
	EXPECT_EQ(m.Removing(), 1000u);
	EXPECT_EQ(m.Remaining(), 1500u);
}

TEST(MoneySplit, CannotTakeMoreThanThePile)
{
	MoneySplit m(50);
	EXPECT_FALSE(m.CanAdd(100));
	EXPECT_EQ(m.Add(100), InvWhy::MoneyShort);
	EXPECT_EQ(m.Add(10), InvWhy::None);
	EXPECT_EQ(m.Remove(100), InvWhy::MoneyShort);
	EXPECT_EQ(m.Removing(), 10u);
}

TEST(MoneySplit, AWithdrawalIsCappedAtOnePocket)
{
	MoneySplit m(100000, 20000);
	for (int i = 0; i < 20; ++i) EXPECT_EQ(m.Add(1000), InvWhy::None);
	EXPECT_EQ(m.Add(1000), InvWhy::MoneyCap);
	EXPECT_EQ(m.Removing(), 20000u);
}

TEST(InventoryCore, EveryRefusalHasWords)
{
	for (int i = 1; i <= static_cast<int>(InvWhy::DoesNotFit); ++i)
	{
		EXPECT_STRNE(Describe(static_cast<InvWhy>(i)), "") << i;
	}
}
