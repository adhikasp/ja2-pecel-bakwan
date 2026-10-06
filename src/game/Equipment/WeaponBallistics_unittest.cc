#include "gtest/gtest.h"

#include "WeaponBallistics.h"

#include "Weapons.h" // AMMO_REGULAR, AMMO_HP, AMMO_AP, AMMO_SUPER_AP

#include <algorithm>

using namespace Equipment;

namespace
{
	// A reference rifle and SMG, as the pipeline sees them: damage, penetration,
	// range (tiles), noise, wear.
	const WeaponProfile RIFLE{ 40, 50, 12, 50, 2 };
	const WeaponProfile SHOTGUN{ 45, 35, 12, 90, 3 };

	Readout ReadoutFor(WeaponProfile const& w, AmmoType ammo, ArmourTier tier)
	{
		return ComputeReadout(w, AmmoProfileFor(ammo), ArmourProfileFor(tier), 100, 70);
	}

	const ReadoutBand* BandAt(Readout const& r, int distance)
	{
		for (ReadoutBand const& b : r.bands) if (b.distance == distance) return &b;
		return nullptr;
	}
}

// --- the readout itself ------------------------------------------------------

TEST(WeaponBallistics, aReadoutHasARowPerBand)
{
	Readout const r = ReadoutFor(RIFLE, AmmoType::Ball, ArmourTier::Unarmoured);
	EXPECT_TRUE(r.usable);
	ASSERT_EQ(r.bands.size(), NUM_READOUT_BANDS);
	for (size_t i = 0; i < r.bands.size(); ++i)
	{
		EXPECT_EQ(r.bands[i].distance, READOUT_BANDS[i]);
		EXPECT_GE(r.bands[i].chanceToHit, 1);
		EXPECT_LE(r.bands[i].chanceToHit, 99);
		EXPECT_GT(r.bands[i].impact, 0);
		EXPECT_GT(r.bands[i].damage, 0);
		EXPECT_GT(r.bands[i].noise, 0);
	}
}

TEST(WeaponBallistics, aWeaponWithNoDamageIsNotUsable)
{
	WeaponProfile const none = WeaponProfileFromStats(0, 10, 50, 0);
	Readout const r = ComputeReadout(none, AmmoProfileFor(AmmoType::Ball),
		ArmourProfileFor(ArmourTier::Unarmoured), 100, 70);
	EXPECT_FALSE(r.usable);
}

TEST(WeaponBallistics, rangeFallsOffPastTheEffectiveRange)
{
	Readout const r = ReadoutFor(RIFLE, AmmoType::Ball, ArmourTier::Unarmoured);
	ReadoutBand const* near = BandAt(r, 4);
	ReadoutBand const* far  = BandAt(r, 32);
	ASSERT_NE(near, nullptr);
	ASSERT_NE(far, nullptr);
	EXPECT_TRUE(near->inRange);
	EXPECT_FALSE(far->inRange); // the rifle's effective range is 12 tiles
	EXPECT_LT(far->damage, near->damage);
	EXPECT_LT(far->chanceToHit, near->chanceToHit);
}

TEST(WeaponBallistics, piercingOutPenetratesHollowPointAgainstAPlate)
{
	Readout const ap = ReadoutFor(RIFLE, AmmoType::Piercing, ArmourTier::Plate);
	Readout const hp = ReadoutFor(RIFLE, AmmoType::HollowPoint, ArmourTier::Plate);
	for (size_t i = 0; i < ap.bands.size(); ++i)
	{
		EXPECT_GT(ap.bands[i].penetration, hp.bands[i].penetration) << "at band " << i;
		// AP only does more to the body where it actually got through; past its
		// effective range both rounds are stopped and do nothing.
		if (ap.bands[i].residual >= 0)
		{
			EXPECT_GT(ap.bands[i].damage, hp.bands[i].damage) << "at band " << i;
		}
	}
}

TEST(WeaponBallistics, hollowPointDoesMoreToTheUnarmoured)
{
	Readout const hp   = ReadoutFor(RIFLE, AmmoType::HollowPoint, ArmourTier::Unarmoured);
	Readout const ball = ReadoutFor(RIFLE, AmmoType::Ball, ArmourTier::Unarmoured);
	ReadoutBand const* near_hp   = BandAt(hp, 4);
	ReadoutBand const* near_ball = BandAt(ball, 4);
	ASSERT_NE(near_hp, nullptr);
	ASSERT_NE(near_ball, nullptr);
	EXPECT_GT(near_hp->damage, near_ball->damage);
}

TEST(WeaponBallistics, subsonicIsTheQuietest)
{
	Readout const quiet = ReadoutFor(RIFLE, AmmoType::Subsonic, ArmourTier::Unarmoured);
	Readout const loud  = ReadoutFor(RIFLE, AmmoType::Ball, ArmourTier::Unarmoured);
	for (size_t i = 0; i < quiet.bands.size(); ++i)
	{
		EXPECT_LT(quiet.bands[i].noise, loud.bands[i].noise) << "at band " << i;
	}
	EXPECT_LT(quiet.effectiveRange, loud.effectiveRange);
}

// --- the comparison ----------------------------------------------------------

TEST(WeaponBallistics, theComparisonMarksTheBetterValueOnEachAxis)
{
	// The shotgun does more to flesh, the rifle penetrates more and is quieter.
	Comparison const c = CompareReadouts(
		ReadoutFor(SHOTGUN, AmmoType::Ball, ArmourTier::Unarmoured),
		ReadoutFor(RIFLE,   AmmoType::Ball, ArmourTier::Unarmoured));
	ASSERT_EQ(c.bands.size(), NUM_READOUT_BANDS);
	for (BandCompare const& b : c.bands)
	{
		EXPECT_EQ(b.damage, Better::A) << "at " << b.distance;
		EXPECT_EQ(b.penetration, Better::B) << "at " << b.distance;
		EXPECT_EQ(b.noise, Better::B) << "at " << b.distance; // quieter wins
	}
}

TEST(WeaponBallistics, equalReadoutsCompareEven)
{
	Comparison const c = CompareReadouts(
		ReadoutFor(RIFLE, AmmoType::Ball, ArmourTier::Soft),
		ReadoutFor(RIFLE, AmmoType::Ball, ArmourTier::Soft));
	for (BandCompare const& b : c.bands)
	{
		EXPECT_EQ(b.hit, Better::Even);
		EXPECT_EQ(b.damage, Better::Even);
		EXPECT_EQ(b.penetration, Better::Even);
		EXPECT_EQ(b.noise, Better::Even);
	}
}

TEST(WeaponBallistics, betterValuePicksTheDirection)
{
	EXPECT_EQ(BetterValue(5, 3, true), Better::A);
	EXPECT_EQ(BetterValue(3, 5, true), Better::B);
	EXPECT_EQ(BetterValue(5, 3, false), Better::B);
	EXPECT_EQ(BetterValue(3, 5, false), Better::A);
	EXPECT_EQ(BetterValue(4, 4, true), Better::Even);
}

// --- the bridge from the game's stats ---------------------------------------

TEST(WeaponBallistics, aHarderHittingWeaponDoesMoreAndPenetratesMore)
{
	WeaponProfile const light = WeaponProfileFromStats(23, 20, 75, 0);
	WeaponProfile const heavy = WeaponProfileFromStats(40, 25, 77, 0);
	EXPECT_GT(heavy.damage, light.damage);
	EXPECT_GE(heavy.penetration, light.penetration);
	EXPECT_GT(heavy.range, light.range);
}

TEST(WeaponBallistics, aMoreReliableWeaponWearsLess)
{
	EXPECT_LT(WeaponProfileFromStats(30, 20, 75, 3).wear, WeaponProfileFromStats(30, 20, 75, -3).wear);
	EXPECT_GE(WeaponProfileFromStats(30, 20, 75, -9).wear, 1); // never negative
}

TEST(WeaponBallistics, theAmmoIndicesMapToThePipelineTypes)
{
	EXPECT_EQ(AmmoTypeFromGameIndex(AMMO_REGULAR),  AmmoType::Ball);
	EXPECT_EQ(AmmoTypeFromGameIndex(AMMO_HP),       AmmoType::HollowPoint);
	EXPECT_EQ(AmmoTypeFromGameIndex(AMMO_AP),       AmmoType::Piercing);
	EXPECT_EQ(AmmoTypeFromGameIndex(AMMO_SUPER_AP), AmmoType::Piercing);
	EXPECT_EQ(AmmoTypeFromGameIndex(AMMO_BUCKSHOT), AmmoType::Ball); // not modelled by the small-arms pipeline
}

TEST(WeaponBallistics, aSuppressorLowersTheVolumeAndNeverRaisesIt)
{
	EXPECT_LT(SilencedVolume(75, 100), 75);
	EXPECT_EQ(SilencedVolume(75, 100), 1);
	EXPECT_GT(SilencedVolume(75, 50), SilencedVolume(75, 100)); // a worn suppressor works less
	EXPECT_LE(SilencedVolume(75, 0), 75);
	EXPECT_EQ(SilencedVolume(1, 100), 1); // no divide by zero
	EXPECT_EQ(SilencedVolume(0, 100), 0);
}
