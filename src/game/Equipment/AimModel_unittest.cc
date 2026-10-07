#include "gtest/gtest.h"

#include "AimModel.h"

using namespace Equipment;

// The game's weapon classes (Weapons.h), as the core sees them.
namespace
{
	constexpr int HANDGUN = 1;
	constexpr int SMG     = 2;
	constexpr int RIFLE   = 3;
	constexpr int MG      = 4;
	constexpr int SHOTGUN = 5;
	constexpr int LAUNCHER = 6; // KNIFECLASS numerically; the map sends unknowns to Other

	// The default policy's aim bonus per standard AP.
	constexpr int PER_AP = 10;
}

// --- disciplines ------------------------------------------------------------

TEST(AimModel, WeaponClassMapsToDiscipline)
{
	EXPECT_EQ(DisciplineForWeapon(HANDGUN, 12), AimDiscipline::Handgun);
	EXPECT_EQ(DisciplineForWeapon(SMG,     12), AimDiscipline::SMG);
	EXPECT_EQ(DisciplineForWeapon(SHOTGUN, 12), AimDiscipline::Shotgun);
	EXPECT_EQ(DisciplineForWeapon(MG,      12), AimDiscipline::MachineGun);
	EXPECT_EQ(DisciplineForWeapon(99,      12), AimDiscipline::Other);
}

TEST(AimModel, ALongRifleIsAMarksmanWeapon)
{
	// Same class in the data; the role follows the reach.
	EXPECT_EQ(DisciplineForWeapon(RIFLE, MARKSMAN_RANGE - 1), AimDiscipline::Rifle);
	EXPECT_EQ(DisciplineForWeapon(RIFLE, MARKSMAN_RANGE),     AimDiscipline::Marksman);
	EXPECT_EQ(DisciplineForWeapon(RIFLE, MARKSMAN_RANGE + 20), AimDiscipline::Marksman);
}

// --- the aim ceiling --------------------------------------------------------

TEST(AimModel, MarksmanHasTheHighestCeiling)
{
	// The owner's decision: marksman/sniper are extended past the field weapons.
	int const marksman = AimCeiling(AimDiscipline::Marksman);
	for (AimDiscipline d : { AimDiscipline::Handgun, AimDiscipline::SMG, AimDiscipline::Shotgun,
			AimDiscipline::Rifle, AimDiscipline::MachineGun, AimDiscipline::Launcher,
			AimDiscipline::Other })
	{
		EXPECT_LT(AimCeiling(d), marksman) << Describe(d) << " should cap below a marksman";
	}
	EXPECT_EQ(marksman, AIM_LEVEL_MAX);
}

TEST(AimModel, CeilingsAreBoundedAndNonNegative)
{
	for (AimDiscipline d : { AimDiscipline::Handgun, AimDiscipline::SMG, AimDiscipline::Shotgun,
			AimDiscipline::Rifle, AimDiscipline::Marksman, AimDiscipline::MachineGun,
			AimDiscipline::Launcher, AimDiscipline::Other })
	{
		EXPECT_GE(AimCeiling(d), 0) << Describe(d);
		EXPECT_LE(AimCeiling(d), AIM_LEVEL_MAX) << Describe(d);
	}
}

// --- the aim curve ----------------------------------------------------------

TEST(AimModel, AimBonusIsMonotonic)
{
	for (int level = 1; level <= AIM_LEVEL_MAX; ++level)
	{
		EXPECT_GT(AimBonus(level, PER_AP, 100), AimBonus(level - 1, PER_AP, 100))
			<< "click " << level << " should be worth more than " << (level - 1);
	}
}

TEST(AimModel, AimBonusDiminishes)
{
	// The n-th click is never worth more than the one before it: aiming always
	// helps, but never more than the click that got you on target.
	int previous_click = AimBonus(1, PER_AP, 100);
	EXPECT_GT(previous_click, 0);
	for (int level = 2; level <= AIM_LEVEL_MAX; ++level)
	{
		int const click = AimBonus(level, PER_AP, 100) - AimBonus(level - 1, PER_AP, 100);
		EXPECT_LE(click, previous_click) << "click " << level << " did not diminish";
		EXPECT_GE(click, 1) << "click " << level << " should still help";
		previous_click = click;
	}
}

TEST(AimModel, FourClicksKeepsTheVanillaBonus)
{
	// The default policy's flat bonus was 4 AP * 10 = 40; the curve is pinned to
	// that at four clicks so the change is in the shape, not the size.
	EXPECT_EQ(AimBonus(4, PER_AP, 100), 4 * PER_AP);
}

TEST(AimModel, AimBonusIsZeroOrLessWithoutSpending)
{
	EXPECT_EQ(AimBonus(0, PER_AP, 100), 0);
	EXPECT_EQ(AimBonus(-3, PER_AP, 100), 0);
}

TEST(AimModel, AimBonusIsCappedAtTheEngineMaximum)
{
	EXPECT_EQ(AimBonus(AIM_LEVEL_MAX + 5, PER_AP, 100), AimBonus(AIM_LEVEL_MAX, PER_AP, 100));
}

TEST(AimModel, AMarksmanGetsMoreFromTheSameAim)
{
	// The role distinction: same AP, a bigger payoff for the precision weapon.
	EXPECT_GT(AimBonus(4, PER_AP, AimScale(AimDiscipline::Marksman)),
	          AimBonus(4, PER_AP, AimScale(AimDiscipline::Rifle)));
	EXPECT_GT(AimBonus(4, PER_AP, AimScale(AimDiscipline::Rifle)),
	          AimBonus(4, PER_AP, AimScale(AimDiscipline::MachineGun)));
}

// --- recoil -----------------------------------------------------------------

TEST(AimModel, WeaponRecoilIsMonotonic)
{
	EXPECT_LE(RecoilForWeapon(RIFLE, 0, 10), RecoilForWeapon(RIFLE, 0, 60));
	EXPECT_LE(RecoilForWeapon(RIFLE, 0, 30), RecoilForWeapon(RIFLE, 30, 30));
}

TEST(AimModel, RecoilGrowsShotByShotAndIsBounded)
{
	int recoil = 0;
	for (int shot = 0; shot < 12; ++shot)
	{
		int const gain = RecoilGain(AimDiscipline::Rifle, 5, 0);
		EXPECT_GE(gain, 1);
		recoil = RecoilAfterShot(recoil, gain);
		EXPECT_GE(recoil, 0);
		EXPECT_LE(recoil, 100);
	}
	EXPECT_GT(recoil, 0) << "a long burst should build a pool";
	EXPECT_LE(recoil, 100);
}

TEST(AimModel, MitigationLowersTheRecoilGain)
{
	int const bare   = RecoilGain(AimDiscipline::MachineGun, 5, 0);
	int const braced = RecoilGain(AimDiscipline::MachineGun, 5,
		RecoilMitigation(RecoilMitigators{ 100, 10, 2, 2, true, true }));
	EXPECT_LT(braced, bare) << "a strong, prone, bipod, autoweapons shooter should recoil less";
}

TEST(AimModel, MitigationPartsAddUpAndAreCapped)
{
	RecoilMitigators none;
	EXPECT_EQ(RecoilMitigation(none), 0);

	RecoilMitigators all;
	all.strength = 100; all.level = 10; all.autoTrait = 2;
	all.bodyHeight = 2; all.bipod = true; all.foregrip = true;
	EXPECT_LE(RecoilMitigation(all), 75);
	EXPECT_GT(RecoilMitigation(all), RecoilMitigation(none));
}

TEST(AimModel, RecoilAlwaysCostsAtLeastOnePoint)
{
	// Even a perfect brace leaves the muzzle moving a little: the pool can
	// never be zero-cost, or holding the trigger would be free.
	RecoilMitigators perfect;
	perfect.strength = 100; perfect.level = 10; perfect.autoTrait = 2;
	perfect.bodyHeight = 2; perfect.bipod = true; perfect.foregrip = true;
	EXPECT_GE(RecoilGain(AimDiscipline::Rifle, 0, RecoilMitigation(perfect)), 1);
}

TEST(AimModel, RecoilDecaysWhileNotFiring)
{
	// The owner's decision: decay, not a reset, so a burst carries into the next
	// exchange (an interrupt, an overwatch shot).
	EXPECT_LT(RecoilDecay(40, 1), 40);
	EXPECT_GT(RecoilDecay(40, 1), 0);
	EXPECT_EQ(RecoilDecay(15, 1), 0); // floored at zero
	EXPECT_EQ(RecoilDecay(0, 3), 0);
}

TEST(AimModel, RecoilPenaltyTracksThePool)
{
	EXPECT_EQ(RecoilPenalty(0), 0);
	EXPECT_GT(RecoilPenalty(40), RecoilPenalty(20));
	EXPECT_EQ(RecoilPenalty(200), 100); // clamped
}

// --- autofire gating --------------------------------------------------------

TEST(AimModel, OnlyRiflesAndMachineGunsAutofire)
{
	EXPECT_TRUE(SupportsAutofire(AimDiscipline::Rifle));
	EXPECT_TRUE(SupportsAutofire(AimDiscipline::MachineGun));
	EXPECT_FALSE(SupportsAutofire(AimDiscipline::Marksman)) << "a precision weapon is not a volume weapon";
	EXPECT_FALSE(SupportsAutofire(AimDiscipline::SMG));
	EXPECT_FALSE(SupportsAutofire(AimDiscipline::Shotgun));
	EXPECT_FALSE(SupportsAutofire(AimDiscipline::Handgun));
	EXPECT_FALSE(SupportsAutofire(AimDiscipline::Launcher));
	EXPECT_FALSE(SupportsAutofire(AimDiscipline::Other));
}
