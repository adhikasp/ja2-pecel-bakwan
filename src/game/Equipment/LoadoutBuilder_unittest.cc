#include "gtest/gtest.h"

#include "AttachmentRules.h"
#include "EquipmentCatalog.h"
#include "Item_Types.h"
#include "LoadoutBuilder.h"
#include "Weapons.h"

using namespace Equipment;

namespace
{
	// A tiny fake world: item 1 is a rifle, 2 an SMG, 3 a shotgun, 4 a combat
	// knife; 10-19 small items, 20-29 magazines, 30-39 large items. Load-
	// bearing gear is what the catalog says it is.
	ItemTraits FakeTraits(uint16_t id)
	{
		ItemTraits t;
		if (LbeFor(id) != nullptr)
		{
			t.size = ItemSize::Large;
			t.isLbe = true;
			return t;
		}
		switch (id)
		{
			case 1: t.host = GunPlatform(RIFLECLASS);    t.size = ItemSize::Large;  return t;
			case 2: t.host = GunPlatform(SMGCLASS);      t.size = ItemSize::Large;  return t;
			case 3: t.host = GunPlatform(SHOTGUNCLASS);  t.size = ItemSize::Large;  return t;
			case 4: t.host = GunPlatform(KNIFECLASS);    t.size = ItemSize::Medium; return t;
			default: break;
		}
		if (id >= 10 && id < 20) { t.size = ItemSize::Small; t.stackLimit = 4; return t; }
		if (id >= 20 && id < 30) { t.size = ItemSize::Small; t.stackLimit = 2; t.isMagazine = true; return t; }
		if (id >= 30 && id < 40) { t.size = ItemSize::Large; return t; }
		t.known = false;
		return t;
	}

	LoadoutSpec RifleSquadKit()
	{
		LoadoutSpec spec;
		spec.weapon = 1;
		spec.weaponAttachments[GunPlatform(RIFLECLASS).IndexOf(SlotRole::Optic)]  = SNIPERSCOPE;
		spec.weaponAttachments[GunPlatform(RIFLECLASS).IndexOf(SlotRole::Muzzle)] = SILENCER;
		spec.lbe[0] = LBE_VEST;
		spec.lbe[1] = LBE_BELT;
		spec.lbe[2] = LBE_PACK;
		spec.pockets[0][0] = { 10, 2 }; // vest medium pocket: two small items
		spec.pockets[1][0] = { 20, 1 }; // belt magazine pocket: one magazine
		spec.pockets[2][0] = { 30, 1 }; // pack large pocket: one large item
		return spec;
	}
}

TEST(LoadoutBuilder, aFullLoadoutValidates)
{
	LoadoutReport report = ValidateLoadout(RifleSquadKit(), FakeTraits);
	EXPECT_TRUE(report.ok);
	EXPECT_TRUE(report.problems.empty());
}

TEST(LoadoutBuilder, attachmentGoesOnTheSlotOfItsRole)
{
	LoadoutSpec spec = RifleSquadKit();
	// The suppressor moved from the muzzle slot to the optic slot.
	spec.weaponAttachments[GunPlatform(RIFLECLASS).IndexOf(SlotRole::Optic)]  = SILENCER;
	spec.weaponAttachments[GunPlatform(RIFLECLASS).IndexOf(SlotRole::Muzzle)] = 0;

	LoadoutReport report = ValidateLoadout(spec, FakeTraits);
	ASSERT_FALSE(report.ok);
	EXPECT_EQ(report.problems[0].kind, LoadoutReject::BadAttachment);
}

TEST(LoadoutBuilder, mountMismatchIsReported)
{
	LoadoutSpec spec = RifleSquadKit();
	// A duckbill choke on a rifle: right role, wrong interface.
	spec.weaponAttachments[GunPlatform(RIFLECLASS).IndexOf(SlotRole::Muzzle)] = DUCKBILL;

	LoadoutReport report = ValidateLoadout(spec, FakeTraits);
	ASSERT_FALSE(report.ok);
	EXPECT_EQ(report.problems[0].kind, LoadoutReject::BadAttachment);
	EXPECT_STREQ(report.problems[0].text, Describe(AttachReject::MountMismatch));
}

TEST(LoadoutBuilder, anAttachmentWithoutAWeaponIsRejected)
{
	LoadoutSpec spec = RifleSquadKit();
	spec.weapon = 0;

	LoadoutReport report = ValidateLoadout(spec, FakeTraits);
	ASSERT_FALSE(report.ok);
	EXPECT_EQ(report.problems[0].kind, LoadoutReject::NotAWeapon);
}

TEST(LoadoutBuilder, meleeTakesNoAttachments)
{
	LoadoutSpec spec = RifleSquadKit();
	spec.weapon = 4; // a knife is no platform

	LoadoutReport report = ValidateLoadout(spec, FakeTraits);
	ASSERT_FALSE(report.ok);
	EXPECT_EQ(report.problems[0].kind, LoadoutReject::NotAWeapon);
}

TEST(LoadoutBuilder, lbeGoesInItsOwnSlot)
{
	LoadoutSpec spec = RifleSquadKit();
	spec.lbe[0] = LBE_PACK; // a pack worn as a vest

	LoadoutReport report = ValidateLoadout(spec, FakeTraits);
	ASSERT_FALSE(report.ok);
	EXPECT_EQ(report.problems[0].kind, LoadoutReject::BadLbe);
}

TEST(LoadoutBuilder, pocketItemsMustFitThePocket)
{
	LoadoutSpec spec = RifleSquadKit();
	// A large item into the vest's medium pocket.
	spec.pockets[0][0] = { 30, 1 };

	LoadoutReport report = ValidateLoadout(spec, FakeTraits);
	ASSERT_FALSE(report.ok);
	EXPECT_EQ(report.problems[0].kind, LoadoutReject::BadPocketItem);
	EXPECT_STREQ(report.problems[0].text, Describe(FitReject::TooBig));
}

TEST(LoadoutBuilder, lbeItemsNeverGoInPockets)
{
	LoadoutSpec spec = RifleSquadKit();
	// A spare pack into the pack's large pocket: nesting depth one.
	spec.pockets[2][0] = { LBE_BELT, 1 };

	LoadoutReport report = ValidateLoadout(spec, FakeTraits);
	ASSERT_FALSE(report.ok);
	EXPECT_EQ(report.problems[0].kind, LoadoutReject::BadPocketItem);
	EXPECT_STREQ(report.problems[0].text, Describe(FitReject::NoNesting));
}

TEST(LoadoutBuilder, stackingBeyondTheLimitIsRejected)
{
	LoadoutSpec spec = RifleSquadKit();
	spec.pockets[1][0] = { 20, 3 }; // three magazines in one pocket, limit 2

	LoadoutReport report = ValidateLoadout(spec, FakeTraits);
	ASSERT_FALSE(report.ok);
	EXPECT_EQ(report.problems[0].kind, LoadoutReject::BadPocketItem);
	EXPECT_STREQ(report.problems[0].text, Describe(FitReject::StackFull));
}

TEST(LoadoutBuilder, unknownItemsAreReported)
{
	LoadoutSpec spec = RifleSquadKit();
	spec.pockets[0][0] = { 999, 1 };

	LoadoutReport report = ValidateLoadout(spec, FakeTraits);
	ASSERT_FALSE(report.ok);
	EXPECT_EQ(report.problems[0].kind, LoadoutReject::UnknownItem);
}
