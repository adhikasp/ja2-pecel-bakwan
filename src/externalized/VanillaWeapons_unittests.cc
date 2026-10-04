#include "Exceptions.h"
#ifdef WITH_UNITTESTS
#include "gtest/gtest.h"


#include "AmmoTypeModel.h"
#include "CalibreModel.h"
#include "DefaultContentManager.h"
#include "DefaultContentManagerUT.h"
#include "GameInstance.h"
#include "GamePolicy.h"
#include "Items.h"
#include "MagazineModel.h"
#include "Weapons.h"
#include "WeaponModels.h"
#include <utility>

TEST(Items, weaponsLoading)
{
	DefaultContentManager * cm = DefaultContentManagerUT::createDefaultCMForTesting();
	ASSERT_TRUE(cm != NULL);
	ASSERT_TRUE(cm->loadGameData());
	EXPECT_TRUE(cm->getWeaponByName("MP5K") != NULL);
	EXPECT_TRUE(cm->getWeapon(9 /* MP5K */) != NULL);
	EXPECT_EQ(cm->getWeaponByName("MP5K"), cm->getWeapon(9 /* MP5K */));
	delete cm;
}

TEST(Items, bug120_cawsAmmo)
{
	DefaultContentManager * cm = DefaultContentManagerUT::createDefaultCMForTesting();
	ASSERT_TRUE(cm->loadGameData());

	// test SAP clip parameters
	const MagazineModel* sapClip = cm->getMagazineByName("CLIPCAWS_10_SAP");
	EXPECT_EQ(sapClip->calibre->internalName, ST::string("AMMOCAWS"));
	EXPECT_EQ(sapClip->ammoType->getInternalName(), ST::string("AMMO_SUPER_AP"));

	// test FLECH clip parameters
	const MagazineModel* flechClip = cm->getMagazineByName("CLIPCAWS_10_FLECH");
	EXPECT_EQ(flechClip->calibre->internalName, ST::string("AMMOCAWS"));
	EXPECT_EQ(flechClip->ammoType->getInternalName(), ST::string("AMMO_BUCKSHOT"));

	delete cm;
}

TEST(Items, bug120_12gAmmo)
{
	DefaultContentManager * cm = DefaultContentManagerUT::createDefaultCMForTesting();
	ASSERT_TRUE(cm->loadGameData());

	// test SAP clip parameters
	const MagazineModel* clip = cm->getMagazineByName("CLIP12G_7");
	EXPECT_EQ(clip->calibre->internalName, ST::string("AMMO12G"));
	EXPECT_EQ(clip->ammoType->getInternalName(), ST::string("AMMO_REGULAR"));

	// test FLECH clip parameters
	const MagazineModel* clipBuckshot = cm->getMagazineByName("CLIP12G_7_BUCKSHOT");
	EXPECT_EQ(clipBuckshot->calibre->internalName, ST::string("AMMO12G"));
	EXPECT_EQ(clipBuckshot->ammoType->getInternalName(), ST::string("AMMO_BUCKSHOT"));

	delete cm;
}

TEST(Items, bug120_cawsDefaultMag)
{
	DefaultContentManager * cm = DefaultContentManagerUT::createDefaultCMForTesting();
	ASSERT_TRUE(cm->loadGameData());

	GCM = cm;

	const WeaponModel *caws = cm->getWeaponByName("CAWS");
	ASSERT_TRUE(caws != NULL);

	const MagazineModel* flechClip = cm->getMagazineByName("CLIPCAWS_10_FLECH");
	const MagazineModel* sapClip = cm->getMagazineByName("CLIPCAWS_10_SAP");
	ASSERT_TRUE(flechClip != NULL);
	ASSERT_TRUE(sapClip != NULL);

	EXPECT_EQ(DefaultMagazine(caws->getItemIndex()), flechClip->getItemIndex());

	EXPECT_EQ(FindReplacementMagazine(sapClip->calibre, 10, AMMO_BUCKSHOT), flechClip->getItemIndex());
	EXPECT_EQ(FindReplacementMagazine(sapClip->calibre, 10, AMMO_SUPER_AP), sapClip->getItemIndex());

	delete cm;
}

TEST(Items, bug120_spas15DefaultMag)
{
	DefaultContentManager * cm = DefaultContentManagerUT::createDefaultCMForTesting();
	ASSERT_TRUE(cm->loadGameData());

	GCM = cm;

	const WeaponModel *spas15 = cm->getWeaponByName("SPAS15");
	ASSERT_TRUE(spas15 != NULL);

	const MagazineModel* clipBuckshot = cm->getMagazineByName("CLIP12G_7_BUCKSHOT");
	const MagazineModel* clip = cm->getMagazineByName("CLIP12G_7");
	ASSERT_TRUE(clipBuckshot != NULL);
	ASSERT_TRUE(clip != NULL);

	EXPECT_EQ(DefaultMagazine(spas15->getItemIndex()), clipBuckshot->getItemIndex());

	EXPECT_EQ(FindReplacementMagazine(clip->calibre, 7, AMMO_BUCKSHOT), clipBuckshot->getItemIndex());
	EXPECT_EQ(FindReplacementMagazine(clip->calibre, 7, AMMO_REGULAR),  clip->getItemIndex());

	delete cm;
}

TEST(Items, ValidLaunchable)
{
	DefaultContentManager * cm = DefaultContentManagerUT::createDefaultCMForTesting();
	ASSERT_TRUE(cm->loadGameData());

	GCM = cm;

	EXPECT_TRUE(ValidLaunchable(MORTAR_SHELL, MORTAR));
	EXPECT_FALSE(ValidLaunchable(MORTAR_SHELL, MORTAR_SHELL));
	EXPECT_FALSE(ValidLaunchable(MORTAR_SHELL, TANK_CANNON));
	EXPECT_FALSE(ValidLaunchable(MORTAR, MORTAR_SHELL));
	EXPECT_TRUE(ValidLaunchable(GL_HE_GRENADE, GLAUNCHER));
	EXPECT_TRUE(ValidLaunchable(GL_HE_GRENADE, UNDER_GLAUNCHER));

	// Check if the function handles some random garbage input
	EXPECT_FALSE(ValidLaunchable(BATTERIES, WINE));
	EXPECT_FALSE(ValidLaunchable(0xf123, 0x97b2));

	delete cm;
}

TEST(Items, GetLauncherFromLaunchable)
{
	DefaultContentManager * cm = DefaultContentManagerUT::createDefaultCMForTesting();
	ASSERT_TRUE(cm->loadGameData());

	GCM = cm;

	EXPECT_EQ(GetLauncherFromLaunchable(GL_TEARGAS_GRENADE), GLAUNCHER);
	EXPECT_EQ(GetLauncherFromLaunchable(MORTAR_SHELL), MORTAR);
	EXPECT_EQ(GetLauncherFromLaunchable(TANK_SHELL), TANK_CANNON);
	EXPECT_EQ(GetLauncherFromLaunchable(TANK_CANNON), NOTHING);

	// Check if the function handles some random garbage input
	EXPECT_EQ(GetLauncherFromLaunchable(G11), NOTHING);
	EXPECT_EQ(GetLauncherFromLaunchable(0xe941), NOTHING);

	delete cm;
}

TEST(Items, ValidAttachment)
{
	std::unique_ptr<DefaultContentManager> cm(DefaultContentManagerUT::createDefaultCMForTesting());
	ASSERT_TRUE(cm->loadGameData());
	auto const oldGCM = std::exchange(GCM, cm.release());

	auto id = [](char const* name) { return GCM->getItemByName(name)->getItemIndex(); };
	UINT16 const SPAS15 = id("SPAS15");
	UINT16 const M870   = id("M870");

	// Non-mount combinations: the arming/merge table still answers these.
	EXPECT_TRUE(ValidAttachment(ITEMDEFINE::DETONATOR, ITEMDEFINE::HMX));
	EXPECT_TRUE(ValidAttachment(ITEMDEFINE::DETONATOR, ITEMDEFINE::TNT));
	EXPECT_TRUE(ValidAttachment(ITEMDEFINE::CHEWING_GUM, ITEMDEFINE::FUMBLE_PAK));
	EXPECT_FALSE(ValidAttachment(0xf083, 0x8c12)); // Random junk crashes the old version of ValidAttachment

	// Mount-based compatibility: slot roles and mount kinds decide, there is
	// no per-weapon whitelist any more.
	EXPECT_TRUE(ValidAttachment(ITEMDEFINE::SILENCER, ITEMDEFINE::GLOCK_17));  // muzzle thread
	EXPECT_TRUE(ValidAttachment(ITEMDEFINE::SILENCER, ITEMDEFINE::G11));       // any platform with a muzzle slot
	EXPECT_TRUE(ValidAttachment(ITEMDEFINE::SNIPERSCOPE, ITEMDEFINE::G11));    // rail
	EXPECT_TRUE(ValidAttachment(ITEMDEFINE::BIPOD, ITEMDEFINE::MINIMI));       // underbarrel mount
	EXPECT_FALSE(ValidAttachment(ITEMDEFINE::SILENCER, SPAS15));               // choke thread, not muzzle thread
	EXPECT_FALSE(ValidAttachment(ITEMDEFINE::DUCKBILL, ITEMDEFINE::G11));      // and the other way round
	EXPECT_FALSE(ValidAttachment(ITEMDEFINE::SNIPERSCOPE, M870));              // shotguns have no optic slot
	EXPECT_FALSE(ValidAttachment(ITEMDEFINE::SILENCER, ITEMDEFINE::FIRSTAIDKIT)); // not a platform
	EXPECT_FALSE(ValidAttachment(ITEMDEFINE::AUTO_ROCKET_RIFLE, ITEMDEFINE::BRASS_KNUCKLES));

	// Helmets take goggles on their NVG mount - by mount type, no toggle.
	EXPECT_TRUE(ValidAttachment(ITEMDEFINE::UVGOGGLES, ITEMDEFINE::SPECTRA_HELMET));
	EXPECT_TRUE(ValidAttachment(ITEMDEFINE::SUNGOGGLES, ITEMDEFINE::KEVLAR_HELMET_18));
	EXPECT_TRUE(ValidAttachment(ITEMDEFINE::NIGHTGOGGLES, ITEMDEFINE::STEEL_HELMET));
	// Boosters no longer stick to leggings: pockets carry them.
	EXPECT_FALSE(ValidAttachment(ITEMDEFINE::ADRENALINE_BOOSTER, ITEMDEFINE::KEVLAR_LEGGINGS_Y));
	EXPECT_FALSE(ValidAttachment(ITEMDEFINE::SUNGOGGLES, ITEMDEFINE::SPECTRA_VEST));

	// Every vest is a plate carrier
	static_assert(ITEMDEFINE::SPECTRA_VEST_Y - ITEMDEFINE::FLAK_JACKET == 8);
	int count = 0;
	for (int i = ITEMDEFINE::FLAK_JACKET; i <= ITEMDEFINE::SPECTRA_VEST_Y; ++i)
	{
		if (ValidAttachment(ITEMDEFINE::CERAMIC_PLATES, i)) ++count;
	}
	EXPECT_EQ(count, 9);

	delete GCM;
	GCM = oldGCM;
}

TEST(Items, CompatibleFaceItem)
{
	EXPECT_TRUE(CompatibleFaceItem(ITEMDEFINE::NIGHTGOGGLES, ITEMDEFINE::EXTENDEDEAR));
	EXPECT_TRUE(CompatibleFaceItem(ITEMDEFINE::EXTENDEDEAR, ITEMDEFINE::NIGHTGOGGLES));
	EXPECT_FALSE(CompatibleFaceItem(ITEMDEFINE::EXTENDEDEAR, ITEMDEFINE::EXTENDEDEAR));
	EXPECT_TRUE(CompatibleFaceItem(ITEMDEFINE::WALKMAN, ITEMDEFINE::GASMASK));
	EXPECT_FALSE(CompatibleFaceItem(ITEMDEFINE::UVGOGGLES, ITEMDEFINE::RDX));
	EXPECT_TRUE(CompatibleFaceItem(0xda83, NOTHING)); // item2 == NOTHING is a special case
	EXPECT_FALSE(CompatibleFaceItem(0x75e4, 0xcafe));
	for (int i = 0; i <= 0xffff; ++i)
	{
		EXPECT_FALSE(CompatibleFaceItem(i, ITEMDEFINE::STEEL_HELMET));
	}
}

TEST(Items, Invalid_ItemIndex_Exception)
{
	std::unique_ptr<DefaultContentManager> cm(DefaultContentManagerUT::createDefaultCMForTesting());
	ASSERT_TRUE(cm->loadGameData());
	auto const oldGCM = std::exchange(GCM, cm.release());

	for (uint16_t i = UINT16_MAX; i >= MAXITEMS; --i)
	{
		EXPECT_THROW(GCM->getItem(i), NotFoundError);
	}

	EXPECT_NO_THROW(GCM->getItem(57000, ItemSystem::nothrow));
	EXPECT_EQ(GCM->getItem(38000, ItemSystem::nothrow), nullptr);

	delete GCM;
	GCM = oldGCM;
}

#endif
