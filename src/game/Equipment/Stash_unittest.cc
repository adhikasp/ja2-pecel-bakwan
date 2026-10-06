#include "gtest/gtest.h"

#include "Item_Types.h"
#include "Stash.h"

using namespace Equipment;

namespace
{
	// A tiny fake world for the stash rules. 1 is a rifle that takes a 5.56x45 magazine of 30,
	// 2 an SMG that takes a 9mm of 20, 3 a shotgun (shotgun shells, 8), 10-19 small stackable
	// items (4 to a pile), 20-29 magazines, 30-39 large single items, 40 money, 41 a cheap
	// repairable item (repair ease -1) and 42 an easy one (+1).
	constexpr uint16_t RIFLE = 1, SMG = 2, SHOTGUN = 3;
	constexpr uint16_t FIRST_STACKABLE = 10, LAST_STACKABLE = 19;
	constexpr uint16_t FIRST_MAGAZINE = 20, LAST_MAGAZINE = 29;
	constexpr uint16_t FIRST_BIG = 30, LAST_BIG = 39;
	constexpr uint16_t CASH = 40, AWKWARD_ITEM = 41, EASY_ITEM = 42;

	StashTraits FakeTraits(uint16_t const id)
	{
		StashTraits t;
		switch (id)
		{
			case RIFLE:
				t.itemClass = IC_GUN; t.name = "Rifle"; t.gun = true;
				t.calibre = 1; t.magSize = 30; t.stackLimit = 0; t.weight = 3500;
				t.repairable = true; t.repairEase = 0;
				return t;
			case SMG:
				t.itemClass = IC_GUN; t.name = "SMG"; t.gun = true;
				t.calibre = 2; t.magSize = 20; t.stackLimit = 1; t.weight = 2500;
				t.repairable = true;
				return t;
			case SHOTGUN:
				t.itemClass = IC_GUN; t.name = "Shotgun"; t.gun = true;
				t.calibre = 3; t.magSize = 8; t.stackLimit = 1; t.weight = 3000;
				return t;
			case CASH:
				t.itemClass = IC_MONEY; t.name = "Money"; t.money = true;
				t.stackLimit = 0; t.weight = 0;
				return t;
			case AWKWARD_ITEM:
				t.itemClass = IC_MISC; t.name = "Zowie Field Radio"; t.stackLimit = 1;
				t.repairable = true; t.repairEase = -1; t.weight = 200;
				return t;
			case EASY_ITEM:
				t.itemClass = IC_MISC; t.name = "Cleaning Kit"; t.stackLimit = 2;
				t.repairable = true; t.repairEase = 1; t.weight = 100;
				return t;
			default: break;
		}
		if (id >= FIRST_STACKABLE && id <= LAST_STACKABLE)
		{
			t.itemClass = IC_MISC; t.stackLimit = 4; t.weight = 50;
			t.name = "Small " + std::to_string(id);
			t.repairable = true;
			return t;
		}
		if (id >= FIRST_MAGAZINE && id <= LAST_MAGAZINE)
		{
			t.itemClass = IC_AMMO; t.magazine = true;
			t.stackLimit = 4; t.weight = 20;
			t.calibre   = uint16_t(id - FIRST_MAGAZINE + 1);
			t.capacity  = (t.calibre == 1) ? 30 : (t.calibre == 2) ? 20 : 8;
			t.ammoType  = t.calibre;
			t.name      = "Magazine " + std::to_string(t.calibre);
			return t;
		}
		if (id >= FIRST_BIG && id <= LAST_BIG)
		{
			t.itemClass = IC_ARMOUR; t.stackLimit = 1; t.weight = 2000;
			t.name      = "Big " + std::to_string(id);
			t.repairable = true;
			return t;
		}
		t.name = "Unknown " + std::to_string(id);
		return t;
	}

	constexpr uint16_t MAG_556 = FIRST_MAGAZINE + 0;
	constexpr uint16_t MAG_9MM = FIRST_MAGAZINE + 1;
	constexpr uint16_t MAG_SHELLS = FIRST_MAGAZINE + 2;

	StashPile Pile(uint16_t const item, uint8_t const count, int8_t const condition = 100)
	{
		StashPile p;
		p.itemId = item;
		p.count  = count;
		for (uint8_t k = 0; k < count; ++k) p.status[k] = condition;
		return p;
	}

	StashPile Magazine(uint16_t const item, uint8_t const count, int const roundsEach)
	{
		StashPile p = Pile(item, count);
		for (uint8_t k = 0; k < count; ++k) p.rounds[k] = uint8_t(roundsEach);
		return p;
	}

	int TotalItems(Stash const& s)
	{
		int n = 0;
		for (StashPile const& p : s) n += p.count;
		return n;
	}
}

// --- categories ---------------------------------------------------------------

TEST(StashCategory, everyItemClassLandsInOneCategory)
{
	EXPECT_EQ(CategoryOf(IC_GUN), StashCategory::Guns);
	EXPECT_EQ(CategoryOf(IC_LAUNCHER), StashCategory::Guns);
	EXPECT_EQ(CategoryOf(IC_AMMO), StashCategory::Ammo);
	EXPECT_EQ(CategoryOf(IC_ARMOUR), StashCategory::Armour);
	EXPECT_EQ(IC_ARMOUR, CategoryMask(StashCategory::Armour));
	EXPECT_EQ(CategoryOf(IC_GRENADE), StashCategory::Explosives);
	EXPECT_EQ(CategoryOf(IC_BOMB), StashCategory::Explosives);
	EXPECT_EQ(CategoryOf(IC_MEDKIT), StashCategory::Medical);
	EXPECT_EQ(CategoryOf(IC_MISC), StashCategory::Other);
	EXPECT_EQ(CategoryOf(IC_MONEY), StashCategory::Other);
	EXPECT_EQ(CategoryMask(StashCategory::All), uint32_t(IC_ALL));
}

TEST(StashCategory, otherIsEverythingTheOtherCategoriesDoNotClaim)
{
	// the filter and the type sort read the same table, so "Other" must be the
	// exact complement of the five named categories
	uint32_t named = CategoryMask(StashCategory::Guns) | CategoryMask(StashCategory::Ammo)
		| CategoryMask(StashCategory::Armour) | CategoryMask(StashCategory::Explosives)
		| CategoryMask(StashCategory::Medical);
	EXPECT_EQ(named & CategoryMask(StashCategory::Other), 0u);
	EXPECT_EQ(named | CategoryMask(StashCategory::Other), uint32_t(IC_ALL));
	EXPECT_STREQ(CategoryKey(StashCategory::Guns), "guns");
}

// --- selection ----------------------------------------------------------------

TEST(StashSelection, aMarkIsThePlayersOwnAndNotTheStashs)
{
	Stash s{ Pile(10, 2), Pile(11, 1) };
	ToggleMark(s, 1);
	EXPECT_EQ(MarkedCount(s), 1);
	EXPECT_FALSE(s[0].marked);
	EXPECT_TRUE(s[1].marked);
}

TEST(StashSelection, anEmptySlotIsNeverMarked)
{
	Stash s{ StashPile{}, Pile(10, 1) };
	ToggleMark(s, 0);
	EXPECT_EQ(MarkedCount(s), 0);
	ToggleMark(s, 9); // out of range
	EXPECT_EQ(MarkedCount(s), 0);
}

TEST(StashSelection, aMarkSurvivesSorting)
{
	Stash s{ Pile(11, 1), Pile(10, 1), Pile(12, 1) };
	ToggleMark(s, 2); // item 12, which sorts last by name
	SortStash(s, StashSort::Name, FakeTraits);
	ASSERT_EQ(s.size(), 3u);
	EXPECT_EQ(s[2].itemId, 12);
	EXPECT_TRUE(s[2].marked);
	EXPECT_EQ(MarkedCount(s), 1);
}

TEST(StashSelection, markAllAndInvertSkipWhatTheSquadCannotReach)
{
	Stash s{ Pile(10, 1), Pile(11, 1), Pile(12, 1) };
	s[2].reachable = false;
	EXPECT_EQ(MarkReachable(s, true), 2);
	EXPECT_EQ(MarkedCount(s), 2);
	EXPECT_FALSE(s[2].marked);
	EXPECT_EQ(InvertMarks(s), 2);
	EXPECT_EQ(MarkedCount(s), 0);
	EXPECT_FALSE(s[2].marked); // an unreachable pile is not selectable, so it is not inverted either
}

// --- stack & merge ------------------------------------------------------------

TEST(StashMerge, likePilesGoTogetherAsFarAsAPileTakes)
{
	Stash s{ Pile(10, 4), Pile(10, 3), Pile(10, 1) }; // four to a pile
	StashReport r = MergeStash(s, FakeTraits);
	// the first pile is already full, so the other two find a keeper: 3 + 1 = 4, one pile emptied
	EXPECT_EQ(r.items, 1);
	EXPECT_EQ(r.piles, 1);
	EXPECT_EQ(TotalItems(s), 8);
	EXPECT_EQ(s[0].count, 4);
	EXPECT_EQ(s[1].count, 4);
	EXPECT_EQ(StashPileCount(s), 2u);
	EXPECT_STREQ(r.note, "stash.note.merged");
}

TEST(StashMerge, moneyIsAlwaysOnePile)
{
	Stash s{ Pile(CASH, 1), Pile(CASH, 1), Pile(CASH, 1) };
	s[0].money = 100; s[1].money = 250; s[2].money = 50;
	MergeStash(s, FakeTraits);
	ASSERT_EQ(StashPileCount(s), 1u);
	EXPECT_EQ(s[0].money, 400u);
}

TEST(StashMerge, anUnreachablePileNeverMergesWithAReachableOne)
{
	Stash s{ Pile(10, 4), Pile(10, 4) };
	s[1].reachable = false;
	MergeStash(s, FakeTraits);
	// nothing merged, and both survive: a pile the squad cannot reach stays where it lies
	EXPECT_EQ(StashPileCount(s), 2u);
	EXPECT_EQ(s[0].count, 4);
	EXPECT_EQ(s[1].count, 4);
}

TEST(StashMerge, aRifleWithASilencerIsNotABareRifle)
{
	Stash s{ Pile(RIFLE, 1), Pile(RIFLE, 1) };
	s[1].attach[0] = SILENCER;
	MergeStash(s, FakeTraits);
	EXPECT_EQ(StashPileCount(s), 2u);
}

TEST(StashMerge, mergingKeepsEachItemsOwnCondition)
{
	Stash s{ Pile(10, 2, 100), Pile(10, 2, 40) };
	MergeStash(s, FakeTraits);
	ASSERT_EQ(StashPileCount(s), 1u);
	EXPECT_EQ(s[0].status[0], 100);
	EXPECT_EQ(s[0].status[1], 100);
	EXPECT_EQ(s[0].status[2], 40);
	EXPECT_EQ(s[0].status[3], 40);
}

TEST(StashMerge, anItemThatDoesNotStackIsLeftAlone)
{
	// getPerPocket 1: like items still sit in their own piles
	Stash s{ Pile(RIFLE, 1), Pile(RIFLE, 1) };
	MergeStash(s, FakeTraits);
	EXPECT_EQ(StashPileCount(s), 2u);
}

TEST(StashMerge, getPerPocketZeroAlsoMeansOneToAPile)
{
	// A gun's definition says 0, which ItemSlotLimit reads as one per slot: two rifles never stack,
	// however many of them the sector holds.
	Stash s{ Pile(RIFLE, 1), Pile(RIFLE, 1), Pile(RIFLE, 1) };
	StashReport r = MergeStash(s, FakeTraits);
	EXPECT_EQ(r.items, 0);
	EXPECT_EQ(StashPileCount(s), 3u);
}

// --- sorting ------------------------------------------------------------------

TEST(StashSort, byTypeGroupsTheCategoriesThenNamesWithin)
{
	Stash s{ Pile(FIRST_BIG, 1), Pile(RIFLE, 1), Pile(MAG_556, 1), Pile(FIRST_STACKABLE, 1) };
	SortStash(s, StashSort::Type, FakeTraits);
	// IC_GUN (2) < IC_AMMO (0x400) < IC_ARMOUR (0x800) < IC_MISC (0x10000000)
	ASSERT_EQ(s.size(), 4u);
	EXPECT_EQ(s[0].itemId, RIFLE);
	EXPECT_EQ(s[1].itemId, MAG_556);
	EXPECT_EQ(s[2].itemId, FIRST_BIG);
	EXPECT_EQ(s[3].itemId, FIRST_STACKABLE);
	EXPECT_STREQ(Describe(StashSort::Type), "type");
}

TEST(StashSort, byNameIsAlphabetical)
{
	Stash s{ Pile(FIRST_STACKABLE, 1), Pile(RIFLE, 1), Pile(AWKWARD_ITEM, 1) };
	SortStash(s, StashSort::Name, FakeTraits);
	ASSERT_EQ(s.size(), 3u);
	EXPECT_STREQ(FakeTraits(s[0].itemId).name.c_str(), "Rifle");
	EXPECT_STREQ(FakeTraits(s[1].itemId).name.c_str(), "Small 10");
	EXPECT_STREQ(FakeTraits(s[2].itemId).name.c_str(), "Zowie Field Radio");
}

TEST(StashSort, byConditionPutsTheWorstFirst)
{
	Stash s{ Pile(10, 1, 90), Pile(11, 1, 20), Pile(12, 1, 55) };
	SortStash(s, StashSort::Condition, FakeTraits);
	ASSERT_EQ(s.size(), 3u);
	EXPECT_EQ(s[0].status[0], 20);
	EXPECT_EQ(s[1].status[0], 55);
	EXPECT_EQ(s[2].status[0], 90);
}

TEST(StashSort, byCountPutsTheBiggestPileFirst)
{
	Stash s{ Pile(10, 1), Pile(11, 4), Pile(12, 2) };
	SortStash(s, StashSort::Count, FakeTraits);
	ASSERT_EQ(s.size(), 3u);
	EXPECT_EQ(s[0].count, 4);
	EXPECT_EQ(s[1].count, 2);
	EXPECT_EQ(s[2].count, 1);
	EXPECT_STREQ(Describe(StashSort::Count), "count");
}

TEST(StashSort, anEmptySlotGoesToTheEndAndNothingIsLost)
{
	Stash s;
	s.push_back(StashPile{});
	s.push_back(Pile(11, 1));
	s.push_back(StashPile{});
	s.push_back(Pile(10, 1));
	SortStash(s, StashSort::Name, FakeTraits);
	EXPECT_EQ(s.size(), 4u);
	EXPECT_EQ(s[0].itemId, 10);
	EXPECT_EQ(s[1].itemId, 11);
	EXPECT_EQ(s[2].itemId, 0);
	EXPECT_EQ(s[3].itemId, 0);
}

TEST(StashSort, theSameOrderComesOutEveryTime)
{
	Stash a{ Pile(12, 1), Pile(10, 1), Pile(11, 1) };
	Stash b{ Pile(11, 1), Pile(12, 1), Pile(10, 1) };
	SortStash(a, StashSort::Type, FakeTraits);
	SortStash(b, StashSort::Type, FakeTraits);
	for (size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a[i].itemId, b[i].itemId);
}

// --- reloading ---------------------------------------------------------------

TEST(StashReload, roundsTopUpAMagazineThatIsNotFull)
{
	// "loose ammo fills magazines in the sector inventory, not in the middle of a fight"
	Stash s{ Magazine(MAG_556, 1, 10), Magazine(MAG_556, 1, 25) };
	StashReport r = FillMagazines(s, FakeTraits);
	// 35 rounds fill one 30-round magazine and leave 5 in the next; nothing is invented or lost
	ASSERT_EQ(StashPileCount(s), 2u);
	EXPECT_EQ(s[0].rounds[0], 30);
	EXPECT_EQ(s[1].rounds[0], 5);
	EXPECT_EQ(r.rounds, 20);
	EXPECT_STREQ(r.note, "stash.note.loaded");
}

TEST(StashReload, aMagazineTakesRoundsOnlyFromItsOwnKind)
{
	// a 9mm magazine does not feed a 5.56 one, however many 9mm lie around
	Stash s{ Magazine(MAG_556, 1, 5), Magazine(MAG_9MM, 4, 20) };
	FillMagazines(s, FakeTraits);
	EXPECT_EQ(s[0].rounds[0], 5);
	EXPECT_EQ(s[0].count, 1);
	EXPECT_EQ(TotalItems(s), 5);
}

TEST(StashReload, aFullMagazineIsLeftAlone)
{
	Stash s{ Magazine(MAG_556, 1, 30) };
	StashReport r = FillMagazines(s, FakeTraits);
	EXPECT_EQ(s[0].rounds[0], 30);
	EXPECT_EQ(r.rounds, 0);
	EXPECT_EQ(r.guns, 0);
}

TEST(StashReload, aGunTakesTheFullestMagazineThatFitsIt)
{
	Stash s;
	StashPile gun = Pile(RIFLE, 1, 80);
	gun.rounds[0] = 4;   // nearly empty
	gun.ammoItem  = MAG_556;
	s.push_back(gun);
	s.push_back(Magazine(MAG_556, 1, 12));
	s.push_back(Magazine(MAG_556, 1, 30));

	StashReport r = FillMagazines(s, FakeTraits);
	// the gun is loaded and its own spent magazine is back in the stash
	ASSERT_EQ(r.guns, 1);
	EXPECT_EQ(s[0].rounds[0], 30);
	EXPECT_EQ(TotalItems(s), 3); // the gun, the 12-round spare and the gun's own spent 4-round one
	int spare = 0, spent = 0;
	for (StashPile const& p : s)
	{
		if (p.itemId != MAG_556) continue;
		if (p.rounds[0] == 12) ++spare;
		if (p.rounds[0] == 4)  ++spent;
	}
	EXPECT_EQ(spare, 1);
	EXPECT_EQ(spent, 1);
}

TEST(StashReload, aGunWithNoMatchingMagazineKeepsWhatItHas)
{
	Stash s;
	StashPile gun = Pile(SMG, 1);
	gun.rounds[0] = 6;
	gun.ammoItem  = MAG_9MM;
	s.push_back(gun);
	s.push_back(Magazine(MAG_556, 2, 30)); // wrong calibre

	StashReport r = FillMagazines(s, FakeTraits);
	EXPECT_EQ(r.guns, 0);
	EXPECT_EQ(s[0].rounds[0], 6);
	EXPECT_EQ(TotalItems(s), 3);
}

TEST(StashReload, aFullGunIsNotReloadedAndItsMagazineIsNotDuplicated)
{
	Stash s;
	StashPile gun = Pile(RIFLE, 1);
	gun.rounds[0] = 30;
	gun.ammoItem  = MAG_556;
	s.push_back(gun);
	s.push_back(Magazine(MAG_556, 1, 30));

	StashReport r = FillMagazines(s, FakeTraits);
	EXPECT_EQ(r.guns, 0);
	EXPECT_EQ(TotalItems(s), 2); // no spare magazine conjured up
}

TEST(StashReload, reloadingNothingIsNotAnError)
{
	Stash s{ Pile(10, 2), Pile(FIRST_BIG, 1) };
	StashReport r = FillMagazines(s, FakeTraits);
	EXPECT_EQ(r.rounds, 0);
	EXPECT_EQ(r.guns, 0);
	EXPECT_EQ(TotalItems(s), 3);
}

// --- repair ------------------------------------------------------------------

TEST(StashRepair, theWorstItemIsFixedFirst)
{
	Stash s{ Pile(10, 1, 90), Pile(11, 1, 20), Pile(12, 1, 55) };
	StashReport r = RepairStash(s, FakeTraits, 1000);
	EXPECT_EQ(r.repaired, 3);
	EXPECT_EQ(s[0].status[0], 100);
	EXPECT_EQ(s[1].status[0], 100);
	EXPECT_EQ(s[2].status[0], 100);
	// 10 + 80 + 45 damage at ease 0 costs exactly that many points
	EXPECT_EQ(r.pointsSpent, 135);
	EXPECT_EQ(r.pointsLeft, 865);
	EXPECT_STREQ(r.note, "stash.note.repaired");
}

TEST(StashRepair, thePointsRunOutAndTheRestKeepsItsDamage)
{
	// 80 damage at ease 0 costs 80 points
	Stash s{ Pile(10, 1, 20) };
	StashReport r = RepairStash(s, FakeTraits, 30);
	EXPECT_EQ(r.repaired, 0);
	EXPECT_EQ(r.pointsLeft, 30);
	EXPECT_EQ(s[0].status[0], 20);
	EXPECT_STREQ(r.note, "stash.note.nothing_to_repair");
}

TEST(StashRepair, theRepairEaseDecidesHowFarThePointsGo)
{
	Stash s{ Pile(AWKWARD_ITEM, 1, 50), Pile(EASY_ITEM, 1, 50) };
	// 50 damage: awkward (ease -1) costs 55, easy (ease +1) costs 45. The awkward one is tried
	// first and does not fit in 50 points, so the easy one behind it is repaired instead.
	StashReport r = RepairStash(s, FakeTraits, 50);
	EXPECT_EQ(r.repaired, 1);
	EXPECT_EQ(r.pointsSpent, 45);
	EXPECT_EQ(r.pointsLeft, 5);
	EXPECT_EQ(s[0].status[0], 50); // the awkward one did not fit in the budget
	EXPECT_EQ(s[1].status[0], 100);
}

TEST(StashRepair, anUnreachableItemIsNotRepaired)
{
	Stash s{ Pile(10, 1, 10) };
	s[0].reachable = false;
	StashReport r = RepairStash(s, FakeTraits, 1000);
	EXPECT_EQ(r.repaired, 0);
	EXPECT_EQ(s[0].status[0], 10);
}

TEST(StashRepair, anItemThatCannotBeRepairedIsLeftAlone)
{
	Stash s{ Pile(MAG_556, 2) }; // magazines are not repairable in the fake world
	StashReport r = RepairStash(s, FakeTraits, 1000);
	EXPECT_EQ(r.repaired, 0);
}

TEST(StashRepair, everyItemInAPileIsConsidered)
{
	Stash s{ Pile(10, 3, 40) };
	StashReport r = RepairStash(s, FakeTraits, 300);
	EXPECT_EQ(r.repaired, 3);
	for (uint8_t k = 0; k < 3; ++k) EXPECT_EQ(s[0].status[k], 100);
}

// --- mass move ---------------------------------------------------------------

TEST(StashMove, onlyMarkedPilesMove)
{
	Stash from{ Pile(10, 2), Pile(11, 2), Pile(12, 1) };
	Stash to;
	from[1].marked = true;
	StashReport r = MoveMarked(from, to, FakeTraits);
	EXPECT_EQ(r.items, 2);
	EXPECT_EQ(TotalItems(from), 3);
	EXPECT_EQ(TotalItems(to), 2);
	ASSERT_EQ(StashPileCount(to), 1u);
	EXPECT_EQ(to[0].itemId, 11);
	EXPECT_FALSE(to[0].marked); // a thing that arrives is no longer marked
	EXPECT_STREQ(r.note, "stash.note.moved");
}

TEST(StashMove, movedPilesStackOntoWhatIsAlreadyThere)
{
	Stash from{ Pile(10, 4), Pile(10, 2) };
	Stash to{ Pile(10, 3) };
	from[0].marked = from[1].marked = true;
	MoveMarked(from, to, FakeTraits);
	EXPECT_EQ(TotalItems(to), 9);
	ASSERT_EQ(StashPileCount(to), 3u); // 4 + 4 + 1, four to a pile
	EXPECT_EQ(StashPileCount(from), 0u);
}

TEST(StashMove, movingNothingChangesNothing)
{
	Stash from{ Pile(10, 2) };
	Stash to{ Pile(11, 1) };
	StashReport r = MoveMarked(from, to, FakeTraits);
	EXPECT_EQ(r.items, 0);
	EXPECT_EQ(TotalItems(from), 2);
	EXPECT_EQ(TotalItems(to), 1);
}

TEST(StashMove, movingMoneyPoolsItIntoTheOnePileThere)
{
	Stash from{ Pile(CASH, 1), Pile(CASH, 1) };
	from[0].money = 100; from[0].marked = true;
	from[1].money = 250; from[1].marked = true;
	Stash to{ Pile(CASH, 1) };
	to[0].money = 50;

	StashReport r = MoveMarked(from, to, FakeTraits);
	EXPECT_EQ(r.money, 350u);
	ASSERT_EQ(StashPileCount(to), 1u);
	EXPECT_EQ(to[0].money, 400u);
	EXPECT_EQ(StashPileCount(from), 0u);
}

TEST(StashMove, aRifleWithASilencerNeverStacksOntoABareRifle)
{
	Stash from{ Pile(RIFLE, 1) };
	from[0].attach[0] = SILENCER;
	from[0].marked = true;
	Stash to{ Pile(RIFLE, 1) };
	MoveMarked(from, to, FakeTraits);
	EXPECT_EQ(StashPileCount(to), 2u);
}

TEST(StashMove, aDropAllIsTheSameRuleWithEverythingMarked)
{
	Stash from{ Pile(10, 2), Pile(FIRST_BIG, 1), Magazine(MAG_556, 2, 30) };
	MarkReachable(from, true);
	Stash to{ Pile(10, 1) };
	StashReport r = MoveMarked(from, to, FakeTraits);
	EXPECT_EQ(r.items, 5);
	EXPECT_EQ(StashPileCount(from), 0u);
	EXPECT_EQ(TotalItems(to), 6);
	EXPECT_EQ(StashPileCount(to), 3u);
}

TEST(StashMove, movingBetweenTwoSectorsKeepsEveryItem)
{
	Stash a{ Pile(10, 3), Pile(RIFLE, 1), Magazine(MAG_556, 2, 30) };
	Stash b{ Pile(10, 4) };
	MarkReachable(a, true);
	StashReport r = MoveMarked(a, b, FakeTraits);
	EXPECT_EQ(r.items, 6);
	EXPECT_EQ(TotalItems(a), 0);
	// the pile already in b is full at 4, so the 3 arriving small items overflow beside it
	EXPECT_EQ(TotalItems(b), 10);
	EXPECT_EQ(StashPileCount(b), 4u); // 4 small, 3 small, the rifle, the magazines
}

// --- readings ----------------------------------------------------------------

TEST(StashReadings, weightAddsUpOverThePiles)
{
	Stash s{ Pile(FIRST_BIG, 2), Pile(10, 4) }; // 2x2000g + 4x50g
	EXPECT_EQ(StashWeight(s, FakeTraits), 4200u);
}

TEST(StashReadings, anEmptyStashHasNoPilesAndNoWeight)
{
	Stash s;
	EXPECT_EQ(StashPileCount(s), 0u);
	EXPECT_EQ(StashWeight(s, FakeTraits), 0u);
	EXPECT_EQ(MarkedCount(s), 0);
	EXPECT_EQ(InvertMarks(s), 0);
}