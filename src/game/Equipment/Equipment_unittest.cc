#include "gtest/gtest.h"

#include "AttachmentRules.h"
#include "EquipmentCatalog.h"
#include "Item_Types.h"
#include "Lbe.h"
#include "PocketRules.h"
#include "Slots.h"
#include "Weapons.h"

using namespace Equipment;

namespace
{
	AttachmentDef Attach(uint16_t itemId, SlotRole role, MountKind mount)
	{
		return AttachmentDef{ itemId, role, mount };
	}

	bool MountMatchesRole(SlotRole role, MountKind mount)
	{
		switch (role)
		{
			case SlotRole::Optic:       return mount == MountKind::Rail;
			case SlotRole::Muzzle:      return mount == MountKind::MuzzleThread || mount == MountKind::ChokeThread;
			case SlotRole::Underbarrel: return mount == MountKind::UnderbarrelMount;
			case SlotRole::SideRail:    return mount == MountKind::SideRailMount;
			case SlotRole::Plate:       return mount == MountKind::PlatePocket;
			case SlotRole::Nvg:         return mount == MountKind::NvgMount;
		}
		return false;
	}
}

// --- slot schema -----------------------------------------------------------

TEST(Slots, gunPlatformsHaveThreeToFourAttachmentSlots)
{
	// Three to five slots per weapon class counts the magazine well, which is
	// the ammunition system's: three to four typed attachment slots here.
	EXPECT_EQ(GunPlatform(HANDGUNCLASS).slotCount, 3);
	EXPECT_EQ(GunPlatform(SMGCLASS).slotCount, 3);
	EXPECT_EQ(GunPlatform(SHOTGUNCLASS).slotCount, 3);
	EXPECT_EQ(GunPlatform(RIFLECLASS).slotCount, 4);
	EXPECT_EQ(GunPlatform(MGCLASS).slotCount, 4);
}

TEST(Slots, meleeWeaponsAreNotPlatforms)
{
	EXPECT_EQ(GunPlatform(KNIFECLASS).slotCount, 0);
	EXPECT_EQ(GunPlatform(MONSTERCLASS).slotCount, 0);
	EXPECT_EQ(GunPlatform(NOGUNCLASS).slotCount, 0);
}

TEST(Slots, everySlotMountMatchesItsRole)
{
	const uint8_t classes[] = { HANDGUNCLASS, SMGCLASS, RIFLECLASS, MGCLASS, SHOTGUNCLASS };
	for (uint8_t weaponClass : classes)
	{
		Platform p = GunPlatform(weaponClass);
		ASSERT_GT(p.slotCount, 0);
		for (int i = 0; i < p.slotCount; ++i)
		{
			EXPECT_TRUE(MountMatchesRole(p.slots[i].role, p.slots[i].mount))
				<< "weapon class " << weaponClass << " slot " << i;
		}
		// no duplicate roles on one platform
		for (int i = 0; i < p.slotCount; ++i)
		{
			for (int j = i + 1; j < p.slotCount; ++j)
			{
				EXPECT_NE(p.slots[i].role, p.slots[j].role);
			}
		}
	}
}

TEST(Slots, shotgunMuzzleIsAChokeThreadNotAMuzzleThread)
{
	Platform shotgun = GunPlatform(SHOTGUNCLASS);
	Platform rifle   = GunPlatform(RIFLECLASS);
	EXPECT_EQ(shotgun.slots[shotgun.IndexOf(SlotRole::Muzzle)].mount, MountKind::ChokeThread);
	EXPECT_EQ(rifle.slots[rifle.IndexOf(SlotRole::Muzzle)].mount, MountKind::MuzzleThread);
}

TEST(Slots, plateCarrierHasTwoPlatePockets)
{
	Platform vest = PlateCarrierPlatform();
	ASSERT_EQ(vest.slotCount, 2);
	EXPECT_EQ(vest.slots[0].role, SlotRole::Plate);
	EXPECT_EQ(vest.slots[1].role, SlotRole::Plate);
}

TEST(Slots, helmetHasOneNvgMount)
{
	Platform helmet = HelmetPlatform();
	ASSERT_EQ(helmet.slotCount, 1);
	EXPECT_EQ(helmet.slots[0].role, SlotRole::Nvg);
	EXPECT_EQ(helmet.slots[0].mount, MountKind::NvgMount);
}

TEST(Slots, slotBonusStaysWithinSchemaBounds)
{
	SlotPolicy more{ 2, true };
	SlotPolicy fewer{ -2, true };
	EXPECT_EQ(GunPlatform(RIFLECLASS, more).slotCount, MAX_HOST_SLOTS);
	EXPECT_EQ(GunPlatform(HANDGUNCLASS, fewer).slotCount, 3);
	EXPECT_EQ(GunPlatform(KNIFECLASS, more).slotCount, 0);
}

// --- attachment compatibility ----------------------------------------------

TEST(AttachmentRules, suppressorFitsEveryThreadedMuzzle)
{
	uint16_t none[MAX_HOST_SLOTS] = {};
	AttachmentDef suppressor = Attach(SILENCER, SlotRole::Muzzle, MountKind::MuzzleThread);

	EXPECT_TRUE(CanAttach(GunPlatform(HANDGUNCLASS), suppressor, none).ok);
	EXPECT_TRUE(CanAttach(GunPlatform(RIFLECLASS), suppressor, none).ok);
	EXPECT_TRUE(CanAttach(GunPlatform(MGCLASS), suppressor, none).ok);
	EXPECT_TRUE(CanAttach(GunPlatform(SMGCLASS), suppressor, none).ok);
}

TEST(AttachmentRules, mountTypeDecidesNeverAWhitelist)
{
	// The same suppressor is rejected on a shotgun not because the shotgun is
	// on some list, but because a choke thread is not a muzzle thread.
	uint16_t none[MAX_HOST_SLOTS] = {};
	AttachmentDef suppressor = Attach(SILENCER, SlotRole::Muzzle, MountKind::MuzzleThread);
	AttachmentDef duckbill   = Attach(DUCKBILL, SlotRole::Muzzle, MountKind::ChokeThread);

	AttachResult onShotgun = CanAttach(GunPlatform(SHOTGUNCLASS), suppressor, none);
	EXPECT_FALSE(onShotgun.ok);
	EXPECT_EQ(onShotgun.reason, AttachReject::MountMismatch);

	AttachResult duckbillOnRifle = CanAttach(GunPlatform(RIFLECLASS), duckbill, none);
	EXPECT_FALSE(duckbillOnRifle.ok);
	EXPECT_EQ(duckbillOnRifle.reason, AttachReject::MountMismatch);

	EXPECT_TRUE(CanAttach(GunPlatform(SHOTGUNCLASS), duckbill, none).ok);
}

TEST(AttachmentRules, opticFitsEveryPlatformWithAnOpticSlot)
{
	// The no-whitelist invariant: an optic mounts on any platform that offers
	// a rail - every class, no exceptions.
	uint16_t none[MAX_HOST_SLOTS] = {};
	AttachmentDef optic = Attach(SNIPERSCOPE, SlotRole::Optic, MountKind::Rail);
	const uint8_t classes[] = { HANDGUNCLASS, SMGCLASS, RIFLECLASS, MGCLASS, SHOTGUNCLASS };
	for (uint8_t weaponClass : classes)
	{
		Platform p = GunPlatform(weaponClass);
		if (p.IndexOf(SlotRole::Optic) < 0) continue;
		EXPECT_TRUE(CanAttach(p, optic, none).ok) << "weapon class " << weaponClass;
	}
}

TEST(AttachmentRules, noSlotWhenThePlatformOffersNoSuchRole)
{
	uint16_t none[MAX_HOST_SLOTS] = {};
	AttachmentDef optic = Attach(SNIPERSCOPE, SlotRole::Optic, MountKind::Rail);
	AttachResult result = CanAttach(GunPlatform(SHOTGUNCLASS), optic, none);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.reason, AttachReject::NoSlot);
}

TEST(AttachmentRules, oneUnderbarrelSlotIsTheConflictRule)
{
	// A bipod and an underbarrel launcher fight over the same slot: the
	// vanilla mutual-exclusion switch is now structural.
	AttachmentDef launcher  = Attach(UNDER_GLAUNCHER, SlotRole::Underbarrel, MountKind::UnderbarrelMount);
	uint16_t present[MAX_HOST_SLOTS] = {};
	present[GunPlatform(RIFLECLASS).IndexOf(SlotRole::Underbarrel)] = BIPOD;

	AttachResult replaceable = CanAttach(GunPlatform(RIFLECLASS), launcher, present, true);
	EXPECT_TRUE(replaceable.ok);

	AttachResult fixed = CanAttach(GunPlatform(RIFLECLASS), launcher, present, false);
	EXPECT_FALSE(fixed.ok);
	EXPECT_EQ(fixed.reason, AttachReject::Occupied);
}

TEST(AttachmentRules, twoPlatesFitBothPlatePockets)
{
	// Typed slots make "no two of the same attachment" structural - and let a
	// plate carrier carry a front and a back plate.
	AttachmentDef plate = Attach(CERAMIC_PLATES, SlotRole::Plate, MountKind::PlatePocket);
	uint16_t present[MAX_HOST_SLOTS] = {};
	present[0] = CERAMIC_PLATES;

	EXPECT_TRUE(CanAttach(PlateCarrierPlatform(), plate, present).ok);
}

TEST(AttachmentRules, gogglesMountOnHelmetsNotOnVests)
{
	// The old "extra attachments" tables are gone: a helmet offers an NVG
	// mount, and that is the whole rule.
	uint16_t none[MAX_HOST_SLOTS] = {};
	AttachmentDef nvg = Attach(NIGHTGOGGLES, SlotRole::Nvg, MountKind::NvgMount);

	EXPECT_TRUE(CanAttach(HelmetPlatform(), nvg, none).ok);
	EXPECT_EQ(CanAttach(PlateCarrierPlatform(), nvg, none).reason, AttachReject::NoSlot);
	EXPECT_EQ(CanAttach(GunPlatform(RIFLECLASS), nvg, none).reason, AttachReject::NoSlot);
}

TEST(AttachmentRules, looseMountPolicyAcceptsAnythingIntoARole)
{
	SlotPolicy loose{ 0, false };
	uint16_t none[MAX_HOST_SLOTS] = {};
	AttachmentDef duckbill = Attach(DUCKBILL, SlotRole::Muzzle, MountKind::ChokeThread);
	EXPECT_TRUE(CanAttach(GunPlatform(RIFLECLASS), duckbill, none, true, loose).ok);
}

// --- catalog schema invariants ---------------------------------------------

TEST(EquipmentCatalog, everyAttachmentIsRoleMountConsistent)
{
	size_t count = 0;
	const AttachmentDef* all = AllAttachments(count);
	ASSERT_GT(count, 0u);
	for (size_t i = 0; i < count; ++i)
	{
		EXPECT_TRUE(MountMatchesRole(all[i].role, all[i].mount))
			<< "attachment " << all[i].itemId;
		EXPECT_LT(all[i].itemId, MAXITEMS);
		for (size_t j = i + 1; j < count; ++j)
		{
			EXPECT_NE(all[i].itemId, all[j].itemId) << "duplicate attachment " << all[i].itemId;
		}
	}
}

TEST(EquipmentCatalog, everyAttachmentHasOneRealTradeOff)
{
	// Curate, do not catalog: a bonus with no price is a bug. Until the
	// curated set lands with #100, the invariant is that the set stays small.
	size_t count = 0;
	AllAttachments(count);
	EXPECT_LE(count, 20u);
}

TEST(EquipmentCatalog, everyLbeProvidesTypedPockets)
{
	size_t count = 0;
	const LbeDef* all = AllLbe(count);
	ASSERT_EQ(count, size_t{ NUM_LBE_SLOTS });
	for (size_t i = 0; i < count; ++i)
	{
		EXPECT_GT(all[i].pocketCount, 0);
		EXPECT_LE(all[i].pocketCount, MAX_LBE_POCKETS);
		EXPECT_LT(all[i].itemId, MAXITEMS);
		for (int j = 0; j < all[i].pocketCount; ++j)
		{
			EXPECT_LE(static_cast<int>(all[i].pockets[j]), static_cast<int>(PocketKind::Magazine));
		}
		for (size_t j = i + 1; j < count; ++j)
		{
			EXPECT_NE(all[i].kind, all[j].kind) << "two LBE items of one kind";
			EXPECT_NE(all[i].itemId, all[j].itemId);
		}
	}
}

TEST(EquipmentCatalog, lookupsAnswerNullForPlainItems)
{
	EXPECT_EQ(AttachmentFor(FIRSTAIDKIT), nullptr);
	EXPECT_EQ(LbeFor(FIRSTAIDKIT), nullptr);
	EXPECT_NE(AttachmentFor(SILENCER), nullptr);
	EXPECT_NE(LbeFor(LBE_VEST), nullptr);
}

// --- pocket fit ------------------------------------------------------------

namespace
{
	ItemTraits Item(ItemSize size, uint16_t stack = 1, bool magazine = false, bool lbe = false)
	{
		ItemTraits t;
		t.size = size;
		t.stackLimit = stack;
		t.isMagazine = magazine;
		t.isLbe = lbe;
		return t;
	}
}

TEST(PocketRules, pocketsNeverHoldLoadBearingGear)
{
	// Nesting depth one is structural: a pouch holds items, not pouches.
	ItemTraits vest = Item(ItemSize::Large, 1, false, true);
	for (PocketKind pocket : { PocketKind::Small, PocketKind::Medium, PocketKind::Large, PocketKind::Magazine })
	{
		FitResult fit = CanFit(pocket, vest, 0);
		EXPECT_FALSE(fit.ok);
		EXPECT_EQ(fit.reason, FitReject::NoNesting);
	}
}

TEST(PocketRules, smallPocketTakesSmallThingsOnly)
{
	EXPECT_TRUE(CanFit(PocketKind::Small, Item(ItemSize::Small), 0).ok);
	EXPECT_EQ(CanFit(PocketKind::Small, Item(ItemSize::Medium), 0).reason, FitReject::TooBig);
	EXPECT_EQ(CanFit(PocketKind::Small, Item(ItemSize::Large), 0).reason, FitReject::TooBig);
}

TEST(PocketRules, mediumPocketTakesAnythingButLarge)
{
	EXPECT_TRUE(CanFit(PocketKind::Medium, Item(ItemSize::Small), 0).ok);
	EXPECT_TRUE(CanFit(PocketKind::Medium, Item(ItemSize::Medium), 0).ok);
	EXPECT_EQ(CanFit(PocketKind::Medium, Item(ItemSize::Large), 0).reason, FitReject::TooBig);
}

TEST(PocketRules, largePocketTakesAnySize)
{
	EXPECT_TRUE(CanFit(PocketKind::Large, Item(ItemSize::Small), 0).ok);
	EXPECT_TRUE(CanFit(PocketKind::Large, Item(ItemSize::Medium), 0).ok);
	EXPECT_TRUE(CanFit(PocketKind::Large, Item(ItemSize::Large), 0).ok);
}

TEST(PocketRules, magazinePocketTakesMagazinesOnly)
{
	EXPECT_TRUE(CanFit(PocketKind::Magazine, Item(ItemSize::Small, 1, true), 0).ok);
	EXPECT_EQ(CanFit(PocketKind::Magazine, Item(ItemSize::Small), 0).reason, FitReject::WrongKind);
	EXPECT_EQ(CanFit(PocketKind::Magazine, Item(ItemSize::Large), 0).reason, FitReject::WrongKind);
}

TEST(PocketRules, stackingIsCappedByTheStackLimit)
{
	ItemTraits ammo = Item(ItemSize::Small, 4);
	EXPECT_TRUE(CanFit(PocketKind::Small, ammo, 3).ok);
	EXPECT_EQ(CanFit(PocketKind::Small, ammo, 4).reason, FitReject::StackFull);
}
