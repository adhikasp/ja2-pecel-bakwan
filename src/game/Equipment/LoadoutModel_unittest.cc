#include "gtest/gtest.h"

#include "InventorySlots.h"
#include "LoadoutModel.h"
#include "Weapons.h"

#include <string>
#include <vector>

using namespace Equipment;

// --- the load readout ------------------------------------------------------

TEST(LoadoutModel, theWeightBandReadsTheCapacityPercentage)
{
	EXPECT_EQ(WeightBand::Light,      WeightBandOf(0));
	EXPECT_EQ(WeightBand::Light,      WeightBandOf(49));
	EXPECT_EQ(WeightBand::Loaded,     WeightBandOf(50));
	EXPECT_EQ(WeightBand::Loaded,     WeightBandOf(89));
	EXPECT_EQ(WeightBand::Heavy,      WeightBandOf(90));
	EXPECT_EQ(WeightBand::Heavy,      WeightBandOf(100));
	EXPECT_EQ(WeightBand::Overloaded, WeightBandOf(101));
	EXPECT_EQ(WeightBand::Overloaded, WeightBandOf(250));
	EXPECT_STREQ("heavy", WeightBandKey(WeightBand::Heavy));
	EXPECT_STREQ("overloaded", WeightBandKey(WeightBand::Overloaded));
}

TEST(LoadoutModel, theCapacityMatchesTheLegacyDivision)
{
	// 500 g per point, doubled above 80 - the divisor CalculateCarriedWeight uses
	EXPECT_EQ(0, CarryCapacityGrams(0));
	EXPECT_EQ(20000, CarryCapacityGrams(40));
	EXPECT_EQ(40000, CarryCapacityGrams(80));
	EXPECT_EQ(50000, CarryCapacityGrams(90));
}

TEST(LoadoutModel, aWeightReadsInMetricOrPounds)
{
	EXPECT_EQ("12.4 kg", WeightText(12400, true).to_std_string());
	EXPECT_EQ("0.0 kg", WeightText(0, true).to_std_string());
	EXPECT_EQ("27.3 lb", WeightText(12400, false).to_std_string());
	// one decimal, rounded, never a bare integer
	EXPECT_EQ("0.4 kg", WeightText(449, true).to_std_string());
	EXPECT_EQ("1.0 lb", WeightText(453, false).to_std_string());
}

// --- the swap-ammo order ---------------------------------------------------

TEST(LoadoutModel, theAmmoOrderIsBallApSuperApHollowPoint)
{
	int count = 0;
	int const* const order = AmmoTypeOrder(count);
	ASSERT_EQ(4, count);
	EXPECT_EQ(AMMO_REGULAR, order[0]);
	EXPECT_EQ(AMMO_AP, order[1]);
	EXPECT_EQ(AMMO_SUPER_AP, order[2]);
	EXPECT_EQ(AMMO_HP, order[3]);
}

TEST(LoadoutModel, theNextAmmoTypeSkipsTypesThatAreNotCarried)
{
	// loaded with ball, carrying regular + hollow point + AP: the next after ball is AP
	EXPECT_EQ(AMMO_AP, NextAmmoType(AMMO_REGULAR, { AMMO_REGULAR, AMMO_HP, AMMO_AP }));
	// AP is carried too; after AP comes super AP when it is carried, else it wraps to hollow point
	EXPECT_EQ(AMMO_SUPER_AP, NextAmmoType(AMMO_AP, { AMMO_REGULAR, AMMO_AP, AMMO_SUPER_AP, AMMO_HP }));
	EXPECT_EQ(AMMO_HP,      NextAmmoType(AMMO_AP, { AMMO_REGULAR, AMMO_AP, AMMO_HP }));
	// after hollow point it wraps back to ball
	EXPECT_EQ(AMMO_REGULAR, NextAmmoType(AMMO_HP, { AMMO_REGULAR, AMMO_HP }));
}

TEST(LoadoutModel, thereIsNoNextAmmoTypeWithoutAnotherCarried)
{
	EXPECT_EQ(-1, NextAmmoType(AMMO_REGULAR, {}));
	EXPECT_EQ(-1, NextAmmoType(AMMO_REGULAR, { AMMO_REGULAR }));
	EXPECT_EQ(-1, NextAmmoType(AMMO_HP, { AMMO_HP }));
}

TEST(LoadoutModel, theAmmoKeyNamesTheReadoutString)
{
	EXPECT_STREQ("ball", AmmoTypeKey(AMMO_REGULAR));
	EXPECT_STREQ("ap", AmmoTypeKey(AMMO_AP));
	EXPECT_STREQ("ap", AmmoTypeKey(AMMO_SUPER_AP));
	EXPECT_STREQ("hp", AmmoTypeKey(AMMO_HP));
}

// --- drag keys -------------------------------------------------------------

TEST(LoadoutModel, aDragKeyNamesItsSlotOrAttachment)
{
	EXPECT_EQ(DragKind::Slot, ParseDragKey(SlotKey(POCK1POS)).kind);
	EXPECT_EQ(POCK1POS, ParseDragKey(SlotKey(POCK1POS)).index);
	EXPECT_EQ(DragKind::Attachment, ParseDragKey(AttachmentKey(3)).kind);
	EXPECT_EQ(3, ParseDragKey(AttachmentKey(3)).index);

	EXPECT_EQ(DragKind::None, ParseDragKey("").kind);
	EXPECT_EQ(DragKind::None, ParseDragKey("x1").kind);
	EXPECT_EQ(DragKind::None, ParseDragKey("i").kind);
	EXPECT_EQ(DragKind::None, ParseDragKey("i-1").kind);
	EXPECT_EQ(DragKind::None, ParseDragKey("i999").kind);          // past the last inventory slot
	EXPECT_EQ(DragKind::None, ParseDragKey("a4").kind);            // past the last attachment
	EXPECT_EQ(DragKind::None, ParseDragKey("i12x").kind);
}
