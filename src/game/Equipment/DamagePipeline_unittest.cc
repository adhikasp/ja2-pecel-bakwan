#include "gtest/gtest.h"

#include "DamagePipeline.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace Equipment;

namespace
{
	// The reference weapon the golden table is measured with: a rifle, new, at
	// eight tiles. Every cell of the table is this weapon against one ammo type
	// and one armour tier, so a change anywhere in the pipeline shows up as a
	// diff in one readable block.
	const WeaponProfile RIFLE{ 40, 50, 12, 50, 2 };

	ShotInput Shot(AmmoType ammo, ArmorTier tier)
	{
		ShotInput in;
		in.weapon           = RIFLE;
		in.ammo             = AmmoProfileFor(ammo);
		in.armor            = ArmorProfileFor(tier);
		in.distance         = 8;
		in.weaponCondition  = 100;
		in.armorCondition   = 100;
		in.aimChance        = 70;
		in.roll             = 0; // the lowest roll: measure the shot, not the roll
		return in;
	}

	// The golden table, rendered as text. One line per ammo type per armour
	// tier, in the pipeline's own table order, so the whole matrix is one
	// EXPECT and a change to the balance is a readable diff.
	std::string GoldenTable()
	{
		std::string out;
		char line[256];
		size_t ammoCount = 0, tierCount = 0;
		const AmmoType*  ammoTypes  = AllAmmoTypes(ammoCount);
		const ArmorTier* armorTiers = AllArmorTiers(tierCount);
		for (size_t a = 0; a < ammoCount; ++a)
		{
			for (size_t t = 0; t < tierCount; ++t)
			{
				ShotResult const r = ResolveShot(Shot(ammoTypes[a], armorTiers[t]));
				snprintf(line, sizeof(line),
					"%-11s %-12s chance %3d  impact %3d  absorbed %3d  damage %3d  "
					"pen %3d  threshold %3d  residual %4d  wear %2d  armour %2d  noise %3d  %s\n",
					Describe(ammoTypes[a]), Describe(armorTiers[t]),
					int(r.chanceToHit), int(r.impact), int(r.absorbed), int(r.damage),
					int(r.penetration), int(r.threshold), int(r.residual),
					int(r.wear), int(r.armorWear), int(r.noise), Describe(r.outcome));
				out += line;
			}
		}
		return out;
	}

	// The six reference weapons the invariants are swept over: a pistol, an SMG,
	// a rifle, a battle rifle, a shotgun and a machine gun. The point is not
	// these six but that the invariants are properties of the *table*, so they
	// are checked against a range of platforms rather than one.
	const WeaponProfile PLATFORMS[] =
	{
		{ 25, 25,  8, 30, 1 }, // pistol
		{ 30, 30, 10, 50, 1 }, // smg
		{ 40, 50, 12, 50, 2 }, // rifle
		{ 55, 75, 16, 75, 2 }, // battle rifle
		{ 45, 35, 14, 90, 3 }, // shotgun
		{ 70, 95, 20, 90, 3 }, // machine gun
	};

	struct Cell { AmmoType ammo; ArmorTier tier; int distance; int weaponCond; int armorCond; };

	std::vector<Cell> EveryCell()
	{
		std::vector<Cell> cells;
		static const int DISTANCES[]   = { 0, 2, 5, 8, 12, 16, 20, 30, 44 };
		static const int CONDITIONS[] = { 1, 10, 25, 50, 75, 100 };
		static const int ARMOUR[]      = { 1, 25, 50, 75, 100 };
		size_t ammoCount = 0, tierCount = 0;
		const AmmoType*  ammoTypes  = AllAmmoTypes(ammoCount);
		const ArmorTier* armorTiers = AllArmorTiers(tierCount);
		for (size_t a = 0; a < ammoCount; ++a)
		{
			for (size_t t = 0; t < tierCount; ++t)
			{
				for (int d : DISTANCES)
				{
					for (int c : CONDITIONS)
					{
						for (int ac : ARMOUR)
						{
							cells.push_back({ ammoTypes[a], armorTiers[t], d, c, ac });
						}
					}
				}
			}
		}
		return cells;
	}

	ShotResult Resolve(const Cell& cell, const WeaponProfile& weapon = RIFLE)
	{
		ShotInput in;
		in.weapon          = weapon;
		in.ammo            = AmmoProfileFor(cell.ammo);
		in.armor           = ArmorProfileFor(cell.tier);
		in.distance        = cell.distance;
		in.weaponCondition = cell.weaponCond;
		in.armorCondition  = cell.armorCond;
		in.aimChance       = 70;
		in.roll            = 0;
		return ResolveShot(in);
	}

	std::string Where(const Cell& c)
	{
		return std::string(Describe(c.ammo)) + " vs " + Describe(c.tier)
			+ " at " + std::to_string(c.distance) + " tiles, weapon " + std::to_string(c.weaponCond)
			+ ", armour " + std::to_string(c.armorCond);
	}
}

// --- the golden table --------------------------------------------------------

TEST(DamagePipeline, theTableIsTheGoldenTable)
{
	// Every ammo type against every armour tier, in one block. This is the
	// balance of the track in twelve lines, and a diff here is a decision
	// someone made rather than a regression nobody noticed.
	EXPECT_EQ(GoldenTable(),
		"ball        no armour    chance  70  impact  40  absorbed   0  damage  40  pen  50  threshold   0  residual    0  wear  2  armour  0  noise  50  hit nothing was wearing\n"
		"ball        soft armour  chance  70  impact  40  absorbed  20  damage  18  pen  50  threshold  18  residual   32  wear  2  armour  0  noise  50  penetrated\n"
		"ball        plate        chance  70  impact  40  absorbed  40  damage   0  pen  50  threshold  55  residual   -5  wear  2  armour  2  noise  50  stopped\n"
		"armour piercing no armour    chance  70  impact  34  absorbed   0  damage  34  pen  75  threshold   0  residual    0  wear  2  armour  0  noise  52  hit nothing was wearing\n"
		"armour piercing soft armour  chance  70  impact  34  absorbed  10  damage  24  pen  75  threshold  18  residual   57  wear  2  armour  0  noise  52  penetrated\n"
		"armour piercing plate        chance  70  impact  34  absorbed  24  damage  10  pen  75  threshold  55  residual   20  wear  2  armour  1  noise  52  penetrated\n"
		"hollow point no armour    chance  70  impact  68  absorbed   0  damage  68  pen  30  threshold   0  residual    0  wear  1  armour  0  noise  50  hit nothing was wearing\n"
		"hollow point soft armour  chance  70  impact  68  absorbed  30  damage  15  pen  30  threshold  18  residual   12  wear  1  armour  0  noise  50  penetrated\n"
		"hollow point plate        chance  70  impact  68  absorbed  68  damage   0  pen  30  threshold  55  residual  -25  wear  1  armour  4  noise  50  stopped\n"
		"subsonic    no armour    chance  66  impact  30  absorbed   0  damage  30  pen  43  threshold   0  residual    0  wear  2  armour  0  noise  10  hit nothing was wearing\n"
		"subsonic    soft armour  chance  66  impact  30  absorbed  22  damage   7  pen  43  threshold  18  residual   25  wear  2  armour  0  noise  10  penetrated\n"
		"subsonic    plate        chance  66  impact  30  absorbed  30  damage   0  pen  43  threshold  55  residual  -12  wear  2  armour  1  noise  10  stopped\n");
}

// --- the counters the design asks for ---------------------------------------

TEST(DamagePipeline, hollowPointIsTheCounterForTheUnarmoured)
{
	// The unarmoured target: hollow point does the most damage, and ball is the
	// middle. This is the whole reason hollow point exists.
	ShotResult const hp   = ResolveShot(Shot(AmmoType::HollowPoint, ArmorTier::Unarmoured));
	ShotResult const ball = ResolveShot(Shot(AmmoType::Ball,        ArmorTier::Unarmoured));
	ShotResult const ap   = ResolveShot(Shot(AmmoType::Piercing,    ArmorTier::Unarmoured));
	EXPECT_GT(hp.damage, ball.damage);
	EXPECT_GT(hp.damage, ap.damage);
	EXPECT_GT(ball.damage, ap.damage);
}

TEST(DamagePipeline, piercingIsTheCounterForArmour)
{
	// Both tiers: against armour the armour absorbs less of a piercing round, so
	// more of it arrives. The e2e fixtures assert the same relation through the
	// real game; this asserts it in the table the real game reads.
	for (ArmorTier tier : { ArmorTier::Soft, ArmorTier::Plate })
	{
		ShotResult const ap   = ResolveShot(Shot(AmmoType::Piercing, tier));
		ShotResult const ball = ResolveShot(Shot(AmmoType::Ball,    tier));
		ShotResult const hp   = ResolveShot(Shot(AmmoType::HollowPoint, tier));
		EXPECT_LT(ap.absorbed, ball.absorbed) << Describe(tier);
		EXPECT_LT(ap.absorbed, hp.absorbed)   << Describe(tier);
		EXPECT_GE(ap.damage, ball.damage)    << Describe(tier);
		EXPECT_GE(ap.damage, hp.damage)      << Describe(tier);
	}
}

TEST(DamagePipeline, piercingUnderperformsOnFlesh)
{
	// The price of the counter: a piercing round carries a hard core, and a hard
	// core does less to a man than a bullet designed to mushroom.
	ShotResult const ap   = ResolveShot(Shot(AmmoType::Piercing,    ArmorTier::Unarmoured));
	ShotResult const ball = ResolveShot(Shot(AmmoType::Ball,        ArmorTier::Unarmoured));
	EXPECT_LT(ap.damage, ball.damage);
	EXPECT_LT(ap.damage, ResolveShot(Shot(AmmoType::HollowPoint, ArmorTier::Unarmoured)).damage);
}

TEST(DamagePipeline, hollowPointIsStoppedByAPlate)
{
	// The hard version of the hollow-point mistake: against a plate it gets
	// nothing through at all, where ball is also stopped but piercing is not.
	ShotResult const hp = ResolveShot(Shot(AmmoType::HollowPoint, ArmorTier::Plate));
	EXPECT_EQ(hp.outcome, ShotOutcome::Stopped);
	EXPECT_EQ(hp.damage, 0);
	// It is stopped by the plate, not by a lack of trying: the round arrived.
	EXPECT_GT(hp.impact, 0);
	EXPECT_EQ(hp.absorbed, hp.impact);
}

TEST(DamagePipeline, subsonicBuysQuietWithRangeAndDamage)
{
	ShotResult const quiet = ResolveShot(Shot(AmmoType::Subsonic, ArmorTier::Unarmoured));
	ShotResult const ball  = ResolveShot(Shot(AmmoType::Ball,     ArmorTier::Unarmoured));
	EXPECT_LT(quiet.noise, ball.noise / 2);
	EXPECT_LT(quiet.damage, ball.damage);
	// And the range it pays for it with is the effective range, not the muzzle
	// report: at twenty tiles a subsonic round has less left than at eight.
	ShotInput far  = Shot(AmmoType::Subsonic, ArmorTier::Unarmoured);
	ShotInput near = Shot(AmmoType::Subsonic, ArmorTier::Unarmoured);
	far.distance  = 20;
	near.distance = 8;
	EXPECT_LT(ResolveShot(far).impact, ResolveShot(near).impact);
	EXPECT_LT(AmmoProfileFor(AmmoType::Subsonic).rangePercent,
	         AmmoProfileFor(AmmoType::Ball).rangePercent);
}

TEST(DamagePipeline, everyStrengthHasACounterAndEveryCounterIsPaidFor)
{
	// Track rule 3 as a schema invariant: each type is the best answer to
	// something and the worst answer to something else, in the table itself.
	// The best answer at each tier: hollow point owns the unarmoured, piercing
	// owns both tiers of armour.
	EXPECT_GT(ResolveShot(Shot(AmmoType::HollowPoint, ArmorTier::Unarmoured)).damage,
	          ResolveShot(Shot(AmmoType::Piercing,     ArmorTier::Unarmoured)).damage);
	EXPECT_GT(ResolveShot(Shot(AmmoType::Piercing, ArmorTier::Plate)).damage,
	          ResolveShot(Shot(AmmoType::HollowPoint, ArmorTier::Plate)).damage);
	EXPECT_GT(ResolveShot(Shot(AmmoType::Piercing, ArmorTier::Soft)).damage,
	          ResolveShot(Shot(AmmoType::HollowPoint, ArmorTier::Soft)).damage);
	// And every type's row of the counter table differs from ball's, or it is
	// the baseline wearing a different name and the type is not a type. The
	// unarmoured cell is the same by design (100% of the round to bare flesh
	// for everyone); the comparison is per row, not per cell.
	size_t ammoCount = 0, tierCount = 0;
	AllAmmoTypes(ammoCount);
	AllArmorTiers(tierCount);
	for (size_t a = 0; a < ammoCount; ++a)
	{
		AmmoType const ammo = static_cast<AmmoType>(a);
		if (ammo == AmmoType::Ball) continue;
		bool rowDiffers = false;
		for (size_t t = 0; t < tierCount; ++t)
		{
			AmmoVersusArmor const cell = CounterFor(ammo, static_cast<ArmorTier>(t));
			AmmoVersusArmor const base = CounterFor(AmmoType::Ball, static_cast<ArmorTier>(t));
			rowDiffers = rowDiffers
				|| cell.absorbPercent != base.absorbPercent
				|| cell.fleshPercent  != base.fleshPercent
				|| cell.bluntPercent  != base.bluntPercent;
		}
		EXPECT_TRUE(rowDiffers)
			<< Describe(ammo) << " is ball's numbers everywhere: a type that differs nowhere is not a type";
	}
}

// --- the invariants, over the whole space -----------------------------------

TEST(DamagePipeline, piercingIsNeverWorseThanBallAgainstArmour)
{
	// The invariant the issue names, swept over six platforms, every ammo and
	// armour combination, nine distances, six weapon conditions and five armour
	// conditions.
	//
	// It has two halves, because AP's trade-off is paid on flesh: the counter
	// cells make AP always absorb less and penetrate more than ball against
	// armour, at any condition; and with the armour doing its job (full
	// condition) AP therefore does at least as much damage. The damage half is
	// checked on fresh armour, where the vest or plate is actually a barrier.
	for (const WeaponProfile& weapon : PLATFORMS)
	{
		for (const Cell& cell : EveryCell())
		{
			if (cell.tier == ArmorTier::Unarmoured) continue;
			// A tier at nothing of its condition left is not armour any more;
			// the invariant is about armour that is actually in the way.
			if (ArmorProfileFor(cell.tier).threshold * cell.armorCond / 100 == 0) continue;
			Cell const ball = { AmmoType::Ball, cell.tier, cell.distance, cell.weaponCond, cell.armorCond };
			Cell const ap   = { AmmoType::Piercing, cell.tier, cell.distance, cell.weaponCond, cell.armorCond };
			ShotResult const apShot   = Resolve(ap, weapon);
			ShotResult const ballShot = Resolve(ball, weapon);
			EXPECT_LE(apShot.absorbed, ballShot.absorbed) << Where(cell);
			EXPECT_GE(apShot.penetration, ballShot.penetration) << Where(cell);
			if (cell.armorCond == 100)
			{
				EXPECT_GE(apShot.damage, ballShot.damage)
					<< Where(cell) << ", " << weapon.damage << " damage platform";
			}
		}
	}
}

TEST(DamagePipeline, hollowPointAbsorbsMoreThanBallAgainstArmour)
{
	// The other half of the same structural counter: hollow point is the round
	// armour is best at stopping, at any condition, and its penetration is the
	// worst of the three.
	for (const Cell& cell : EveryCell())
	{
		if (cell.tier == ArmorTier::Unarmoured) continue;
		if (ArmorProfileFor(cell.tier).threshold * cell.armorCond / 100 == 0) continue;
		Cell const ball = { AmmoType::Ball, cell.tier, cell.distance, cell.weaponCond, cell.armorCond };
		Cell const hp   = { AmmoType::HollowPoint, cell.tier, cell.distance, cell.weaponCond, cell.armorCond };
		ShotResult const hpShot   = Resolve(hp);
		ShotResult const ballShot = Resolve(ball);
		EXPECT_GE(hpShot.absorbed, ballShot.absorbed) << Where(cell);
		EXPECT_LE(hpShot.penetration, ballShot.penetration) << Where(cell);
	}
}

TEST(DamagePipeline, hollowPointIsNeverBetterThanBallAgainstAPlate)
{
	for (const WeaponProfile& weapon : PLATFORMS)
	{
		for (const Cell& cell : EveryCell())
		{
			if (cell.ammo != AmmoType::HollowPoint || cell.tier != ArmorTier::Plate) continue;
			if (ArmorProfileFor(cell.tier).threshold * cell.armorCond / 100 == 0) continue;
			Cell const baseline = { AmmoType::Ball, cell.tier, cell.distance, cell.weaponCond, cell.armorCond };
			EXPECT_LE(Resolve(cell, weapon).damage, Resolve(baseline, weapon).damage)
				<< Where(cell) << ", " << weapon.damage << " damage platform";
		}
	}
}

TEST(DamagePipeline, hollowPointIsNeverWorseThanBallAgainstTheUnarmoured)
{
	for (const WeaponProfile& weapon : PLATFORMS)
	{
		for (const Cell& cell : EveryCell())
		{
			if (cell.ammo != AmmoType::HollowPoint || cell.tier != ArmorTier::Unarmoured) continue;
			Cell const baseline = { AmmoType::Ball, cell.tier, cell.distance, cell.weaponCond, cell.armorCond };
			EXPECT_GE(Resolve(cell, weapon).damage, Resolve(baseline, weapon).damage)
				<< Where(cell) << ", " << weapon.damage << " damage platform";
		}
	}
}

TEST(DamagePipeline, piercingIsNeverBetterThanBallAgainstTheUnarmoured)
{
	for (const WeaponProfile& weapon : PLATFORMS)
	{
		for (const Cell& cell : EveryCell())
		{
			if (cell.ammo != AmmoType::Piercing || cell.tier != ArmorTier::Unarmoured) continue;
			Cell const baseline = { AmmoType::Ball, cell.tier, cell.distance, cell.weaponCond, cell.armorCond };
			EXPECT_LE(Resolve(cell, weapon).damage, Resolve(baseline, weapon).damage)
				<< Where(cell) << ", " << weapon.damage << " damage platform";
		}
	}
}

TEST(DamagePipeline, moreArmourNeverHelpsTheTarget)
{
	// Monotonicity in the tier: soft is never worse for the man wearing it than
	// nothing, and plate is never worse than soft. A pipeline where armour
	// could make a target easier to kill would be a bug in the table.
	for (const WeaponProfile& weapon : PLATFORMS)
	{
		size_t ammoCount = 0;
		const AmmoType* ammoTypes = AllAmmoTypes(ammoCount);
		for (size_t a = 0; a < ammoCount; ++a)
		{
			for (int d : { 0, 4, 8, 12, 20, 30 })
			{
				for (int c : { 25, 50, 100 })
				{
					ShotInput in;
					in.weapon          = weapon;
					in.ammo            = AmmoProfileFor(ammoTypes[a]);
					in.distance        = d;
					in.weaponCondition = c;
					in.armorCondition  = 100;
					in.roll            = 0;
					in.armor.tier      = ArmorTier::Unarmoured;
					int16_t const open = ResolveShot(in).damage;
					in.armor = ArmorProfileFor(ArmorTier::Soft);
					int16_t const soft = ResolveShot(in).damage;
					in.armor = ArmorProfileFor(ArmorTier::Plate);
					int16_t const plate = ResolveShot(in).damage;
					// Armour helps the man wearing it, so more of it can only ever
					// mean less damage reaching him. A pipeline where putting on a
					// plate made a target easier to kill is a bug in the table.
					EXPECT_GE(open, soft)  << Describe(ammoTypes[a]) << " at " << d << " tiles, condition " << c;
					EXPECT_GE(soft, plate) << Describe(ammoTypes[a]) << " at " << d << " tiles, condition " << c;
				}
			}
		}
	}
}

// --- range, condition, and the readout --------------------------------------

TEST(DamagePipeline, rangeCostsDamageAndTheChanceToHit)
{
	ShotInput near = Shot(AmmoType::Ball, ArmorTier::Unarmoured);
	ShotInput far  = Shot(AmmoType::Ball, ArmorTier::Unarmoured);
	far.distance = 30;
	ShotResult const close = ResolveShot(near);
	ShotResult const distant = ResolveShot(far);
	EXPECT_GT(close.impact, distant.impact);
	EXPECT_GT(close.chanceToHit, distant.chanceToHit);
	// A spent round is still a round: the falloff has a floor, so a shot at any
	// range is a shot rather than a wasted trigger pull.
	EXPECT_GT(distant.impact, 0);
}

TEST(DamagePipeline, rangeFallsOffAtTheWeaponsEffectiveRangeNotTheAmmoReport)
{
	// The subsonic round's shorter range is its own, applied to the weapon's: at
	// nine tiles the rifle has all of its range left and the subsonic round does
	// not, because 12 tiles of rifle is 7.8 tiles of subsonic.
	ShotInput in = Shot(AmmoType::Subsonic, ArmorTier::Unarmoured);
	in.distance = 9;
	ShotResult const quiet = ResolveShot(in);
	in.ammo = AmmoProfileFor(AmmoType::Ball);
	ShotResult const ball = ResolveShot(in);
	EXPECT_LT(quiet.impact, ball.impact);
	EXPECT_GT(quiet.impact, 0);
}

TEST(DamagePipeline, aWornWeaponDeliversLessAndCostsMore)
{
	ShotInput fresh = Shot(AmmoType::Ball, ArmorTier::Unarmoured);
	fresh.weaponCondition = 100;
	ShotInput worn = fresh;
	worn.weaponCondition = 20;
	ShotResult const good = ResolveShot(fresh);
	ShotResult const bad  = ResolveShot(worn);
	EXPECT_GT(good.impact, bad.impact);
	EXPECT_GT(good.chanceToHit, bad.chanceToHit); // a loose gun wanders
	// Condition that pays rent: a neglected weapon wears faster.
	EXPECT_GT(bad.wear, good.wear);
	EXPECT_EQ(bad.weaponConditionAfter, 20 - bad.wear);
}

TEST(DamagePipeline, wornArmourProtectsLessAndIsEasierToPunchThrough)
{
	// Plate wear enters in exactly two places, and both are the defender's
	// problem: a shot plate soaks less, and the power needed to get through it
	// drops.
	// A round that an intact plate stops outright, against the same round at the
	// same range through a plate that has been shot at: the chewed plate asks
	// less, soaks less and lets it through. This is why shooting somebody's
	// armour is a way to win a fight.
	ShotInput intact = Shot(AmmoType::HollowPoint, ArmorTier::Plate);
	intact.armorCondition = 100;
	ShotInput chewed = intact;
	chewed.armorCondition = 20;
	ShotResult const whole = ResolveShot(intact);
	ShotResult const battered = ResolveShot(chewed);
	EXPECT_GT(whole.absorbed, battered.absorbed);
	EXPECT_GT(whole.threshold, battered.threshold);
	EXPECT_EQ(whole.outcome, ShotOutcome::Stopped);
	EXPECT_EQ(whole.damage, 0);
	EXPECT_GT(battered.damage, 0);
	EXPECT_EQ(battered.armorConditionAfter, 20 - battered.armorWear);
}

TEST(DamagePipeline, armourDegradesByWhatItAbsorbed)
{
	// The wear on armour is what it stopped, at the tier's own rate, so a plate
	// degrades faster than a vest and a round that gets stopped does the most.
	ShotResult const stopped = ResolveShot(Shot(AmmoType::HollowPoint, ArmorTier::Plate));
	ShotResult const open    = ResolveShot(Shot(AmmoType::HollowPoint, ArmorTier::Unarmoured));
	EXPECT_GT(stopped.armorWear, open.armorWear);
	EXPECT_EQ(stopped.armorWear, stopped.absorbed * ArmorProfileFor(ArmorTier::Plate).degradePercent / 100);
}

TEST(DamagePipeline, aMissCostsTheShotButNotTheDamage)
{
	// The roll is a separate decision from what the round does: a miss still
	// leaves the barrel, so it still makes noise and still wears the gun, it
	// just does no harm.
	ShotInput in = Shot(AmmoType::Ball, ArmorTier::Soft);
	in.roll = 99;
	ShotResult const missed = ResolveShot(in);
	EXPECT_FALSE(missed.hit);
	EXPECT_EQ(missed.outcome, ShotOutcome::Missed);
	EXPECT_EQ(missed.damage, 0);
	EXPECT_GT(missed.wear, 0);
	EXPECT_GT(missed.noise, 0);
	// Same loadout, same seed, different roll: only the outcome changes.
	in.roll = 0;
	ShotResult const landed = ResolveShot(in);
	EXPECT_TRUE(landed.hit);
	EXPECT_EQ(landed.wear, missed.wear);
	EXPECT_EQ(landed.noise, missed.noise);
	EXPECT_GT(landed.damage, 0);
}

// --- determinism, and the absence of a ricochet -----------------------------

TEST(DamagePipeline, theResolutionIsPure)
{
	// The property the whole track's testability rests on: the same loadout, the
	// same range, the same roll resolves the same shot. No globals, no game
	// state, no hidden dice.
	ShotInput const in = Shot(AmmoType::Piercing, ArmorTier::Plate);
	ShotResult const first = ResolveShot(in);
	for (int i = 0; i < 16; ++i)
	{
		ShotResult const again = ResolveShot(in);
		EXPECT_EQ(again.chanceToHit, first.chanceToHit);
		EXPECT_EQ(again.hit, first.hit);
		EXPECT_EQ(again.outcome, first.outcome);
		EXPECT_EQ(again.impact, first.impact);
		EXPECT_EQ(again.absorbed, first.absorbed);
		EXPECT_EQ(again.through, first.through);
		EXPECT_EQ(again.damage, first.damage);
		EXPECT_EQ(again.penetration, first.penetration);
		EXPECT_EQ(again.threshold, first.threshold);
		EXPECT_EQ(again.residual, first.residual);
		EXPECT_EQ(again.wear, first.wear);
		EXPECT_EQ(again.armorWear, first.armorWear);
		EXPECT_EQ(again.noise, first.noise);
		EXPECT_EQ(again.weaponConditionAfter, first.weaponConditionAfter);
		EXPECT_EQ(again.armorConditionAfter, first.armorConditionAfter);
	}
}

TEST(DamagePipeline, thereIsNoRicochet)
{
	// A ricochet would be a second random value: the same shot against the same
	// armour glancing off or biting in. The outcome set is the five named
	// results and nothing else, so a round either gets through or it does not.
	// Named here rather than left to be inferred from arithmetic, because the
	// rest of the track reads these names.
	ShotResult const penetrated = ResolveShot(Shot(AmmoType::Piercing, ArmorTier::Plate));
	ShotResult const stopped    = ResolveShot(Shot(AmmoType::HollowPoint, ArmorTier::Plate));
	EXPECT_EQ(penetrated.outcome, ShotOutcome::Penetrated);
	EXPECT_EQ(stopped.outcome, ShotOutcome::Stopped);
	// Every outcome is named, and no two names collide: the track reads these
	// strings, so a duplicate would silently merge two results.
	ShotOutcome const all[] =
	{
		ShotOutcome::Missed, ShotOutcome::Unopposed, ShotOutcome::Penetrated,
		ShotOutcome::BluntTrauma, ShotOutcome::Stopped,
	};
	ASSERT_EQ(sizeof(all) / sizeof(all[0]), size_t{ NUM_SHOT_OUTCOMES });
	for (size_t i = 0; i < NUM_SHOT_OUTCOMES; ++i)
	{
		EXPECT_NE(std::string(Describe(all[i])), std::string());
		for (size_t j = i + 1; j < NUM_SHOT_OUTCOMES; ++j)
		{
			EXPECT_STRNE(Describe(all[i]), Describe(all[j]));
		}
	}
	EXPECT_STREQ(Describe(ShotOutcome::Penetrated),  "penetrated");
	EXPECT_STREQ(Describe(ShotOutcome::Stopped),     "stopped");
	EXPECT_STREQ(Describe(ShotOutcome::BluntTrauma), "blunt trauma");
	EXPECT_STREQ(Describe(ShotOutcome::Missed),      "missed");
	EXPECT_STREQ(Describe(ShotOutcome::Unopposed),   "hit nothing was wearing");
}

TEST(DamagePipeline, aStoppedRoundCanStillBruise)
{
	// Blunt trauma is the named outcome between the two: armour held, and the
	// impact still hurt. A round that came close to the threshold bruises; one
	// that never had a chance does not, and that difference is the tier's
	// `bluntPercent` read as a number.
	ShotInput near = Shot(AmmoType::HollowPoint, ArmorTier::Soft);
	near.weapon.penetration = 29;  // 29 x 60% = 17: just under the vest's 18
	ShotInput far = near;
	far.weapon.penetration = 5;    // nowhere near it
	ShotResult const grazed   = ResolveShot(near);
	ShotResult const hopeless = ResolveShot(far);
	EXPECT_EQ(grazed.outcome, ShotOutcome::BluntTrauma);
	EXPECT_GT(grazed.damage, 0);
	// The closer it came, the more it hurt. A round that never had a chance is
	// stopped outright - and both are stopped, which is the difference the
	// residual makes on the way in.
	EXPECT_LT(grazed.residual, 0);
	EXPECT_LT(hopeless.residual, 0);
	EXPECT_GT(grazed.damage, hopeless.damage);
	EXPECT_EQ(hopeless.damage, 0);
	EXPECT_EQ(hopeless.outcome, ShotOutcome::Stopped);
}

TEST(DamagePipeline, aPlateNeverBruses)
{
	// Nothing bruised through a plate, or it is not a plate. A vest is the tier
	// that bruises, and that is the difference between the two.
	ShotInput near = Shot(AmmoType::HollowPoint, ArmorTier::Soft);
	near.weapon.penetration = 29; // 29 x 60% = 17, just under the vest's 18
	ShotInput plate = Shot(AmmoType::HollowPoint, ArmorTier::Plate);
	plate.weapon.penetration = 90; // 90 x 60% = 54, just under the plate's 55
	EXPECT_EQ(ResolveShot(near).outcome, ShotOutcome::BluntTrauma);
	EXPECT_EQ(ResolveShot(plate).outcome, ShotOutcome::Stopped);
	EXPECT_EQ(CounterFor(AmmoType::HollowPoint, ArmorTier::Plate).bluntPercent, 0);
	EXPECT_GT(CounterFor(AmmoType::HollowPoint, ArmorTier::Soft).bluntPercent, 0);
}

// --- the residual, and the number a player reads ----------------------------

TEST(DamagePipeline, surplusPenetrationIsANumberThePlayerCanRead)
{
	// The residual is the surplus over the threshold: the readout's "through by
	// N" that tells a round that just cleared the plate from one that ignored
	// it. It is reported, not banked as damage - the armour's subtraction is the
	// whole of what the round loses, which is what keeps more armour from ever
	// being worse.
	ShotInput barely = Shot(AmmoType::Ball, ArmorTier::Soft);
	barely.weapon.penetration = 19; // threshold 18: one point of surplus
	ShotInput plenty = Shot(AmmoType::Ball, ArmorTier::Soft);
	plenty.weapon.penetration = 45;
	ShotResult const grazed  = ResolveShot(barely);
	ShotResult const through = ResolveShot(plenty);
	EXPECT_EQ(grazed.residual, 1);
	EXPECT_EQ(grazed.residual, grazed.penetration - grazed.threshold);
	EXPECT_GT(through.residual, grazed.residual);
	// Both got through, so both do what got through; the surplus is the surplus.
	EXPECT_EQ(through.damage, grazed.damage);
	EXPECT_EQ(grazed.outcome, ShotOutcome::Penetrated);
	EXPECT_EQ(through.outcome, ShotOutcome::Penetrated);
}

TEST(DamagePipeline, aDestroyedLayerIsNoLayerAtAll)
{
	// Armour worn to nothing falls back to the unarmoured cell, so a destroyed
	// vest is exactly no vest rather than a vest that changes which round is
	// best by existing. This is the one place the counter table is chosen by
	// condition as well as tier.
	ShotInput bare   = Shot(AmmoType::HollowPoint, ArmorTier::Unarmoured);
	ShotInput ruined = Shot(AmmoType::HollowPoint, ArmorTier::Soft);
	ruined.armorCondition = 0;
	ShotResult const open  = ResolveShot(bare);
	ShotResult const gone  = ResolveShot(ruined);
	EXPECT_EQ(gone.outcome, ShotOutcome::Unopposed);
	EXPECT_EQ(gone.damage, open.damage);
	EXPECT_EQ(gone.absorbed, 0);
	EXPECT_EQ(gone.threshold, 0);
	EXPECT_EQ(gone.armorWear, 0);
}

TEST(DamagePipeline, theReadoutHasEverythingAPlayerNeedsBeforeCommitting)
{
	// Track rule 5, "readout before commit": one call, every number the
	// comparison UI and the AI's target choice read, and none of them needing a
	// second system to agree with.
	ShotInput in = Shot(AmmoType::Piercing, ArmorTier::Plate);
	in.distance = 14;
	in.weaponCondition = 65;
	in.armorCondition = 80;
	ShotResult const r = ResolveShot(in);
	EXPECT_GT(r.chanceToHit, 0);
	EXPECT_LE(r.chanceToHit, 99);
	EXPECT_GT(r.penetration, 0);
	EXPECT_GT(r.threshold, 0);
	EXPECT_GT(r.damage, 0);
	EXPECT_GT(r.impact, r.damage);
	EXPECT_GT(r.noise, 0);
	EXPECT_GT(r.wear, 0);
	EXPECT_EQ(r.weaponConditionAfter, 65 - r.wear);
	EXPECT_EQ(r.armorConditionAfter, 80 - r.armorWear);
}

// --- the compiled tables, and the toggles -----------------------------------

TEST(DamagePipeline, theTableIsTotal)
{
	// Every ammo type has a profile and a counter against every tier, and every
	// tier has a profile. A hole here is a crash in a firefight, not a fallback.
	size_t ammoCount = 0, tierCount = 0;
	const AmmoType*  ammoTypes  = AllAmmoTypes(ammoCount);
	const ArmorTier* armorTiers = AllArmorTiers(tierCount);
	ASSERT_EQ(ammoCount, size_t{ NUM_AMMO_TYPES });
	ASSERT_EQ(tierCount, size_t{ NUM_ARMOR_TIERS });
	for (size_t a = 0; a < ammoCount; ++a)
	{
		AmmoProfile const ammo = AmmoProfileFor(ammoTypes[a]);
		EXPECT_EQ(ammo.type, ammoTypes[a]);
		EXPECT_GT(ammo.damagePercent, 0);
		EXPECT_GT(ammo.penetrationPercent, 0);
		EXPECT_GT(ammo.noisePercent, 0);
		EXPECT_GT(ammo.rangePercent, 0);
		EXPECT_GT(ammo.wearPercent, 0);
		for (size_t t = 0; t < tierCount; ++t)
		{
			AmmoVersusArmor const cell = CounterFor(ammoTypes[a], armorTiers[t]);
			EXPECT_GE(cell.absorbPercent, 0) << Describe(ammoTypes[a]) << " vs " << Describe(armorTiers[t]);
			EXPECT_GT(cell.fleshPercent, 0)  << Describe(ammoTypes[a]) << " vs " << Describe(armorTiers[t]);
			EXPECT_GE(cell.bluntPercent, 0) << Describe(ammoTypes[a]) << " vs " << Describe(armorTiers[t]);
		}
	}
	for (size_t t = 0; t < tierCount; ++t)
	{
		ArmorProfile const tier = ArmorProfileFor(armorTiers[t]);
		EXPECT_EQ(tier.tier, armorTiers[t]);
		EXPECT_GE(tier.protection, 0);
		EXPECT_GE(tier.threshold, 0);
		EXPECT_GE(tier.degradePercent, 0);
	}
	// Armour gets harder, and a harder tier is not a softer one.
	for (size_t t = 1; t < tierCount; ++t)
	{
		EXPECT_GE(ArmorProfileFor(armorTiers[t]).protection, ArmorProfileFor(armorTiers[t - 1]).protection);
		EXPECT_GE(ArmorProfileFor(armorTiers[t]).threshold,  ArmorProfileFor(armorTiers[t - 1]).threshold);
	}
}

TEST(DamagePipeline, theLeafTunablesOnlyMoveWearAndFalloff)
{
	// Track rule 6: the rules are compiled, and only the leaf numbers move. A
	// toggle that changed what a round did to a body would not be a leaf.
	ShotInput const in = Shot(AmmoType::Ball, ArmorTier::Soft);
	PipelineToggles fast;
	fast.wearRate     = 300;
	fast.rangeFalloff = 300;
	ShotResult const base = ResolveShot(in);
	ShotResult const tuned = ResolveShot(in, fast);
	EXPECT_EQ(tuned.damage, base.damage);
	EXPECT_EQ(tuned.impact, base.impact);
	EXPECT_EQ(tuned.absorbed, base.absorbed);
	EXPECT_EQ(tuned.penetration, base.penetration);
	EXPECT_GT(tuned.wear, base.wear);
	EXPECT_EQ(tuned.wear, base.wear * 3);

	// And the falloff toggle is read past the effective range only.
	ShotInput close = in;
	ShotInput far   = in;
	far.distance = 30;
	EXPECT_EQ(ResolveShot(close, fast).impact, ResolveShot(close).impact);
	EXPECT_LT(ResolveShot(far, fast).impact, ResolveShot(far).impact);
}

TEST(DamagePipeline, theTogglesDefaultToTheDefaultPolicy)
{
	PipelineToggles const toggles;
	EXPECT_EQ(toggles.wearRate, 100);
	EXPECT_EQ(toggles.rangeFalloff, 100);
	EXPECT_EQ(toggles.wearRate, PipelineTogglesFrom(nullptr).wearRate);
	EXPECT_EQ(toggles.rangeFalloff, PipelineTogglesFrom(nullptr).rangeFalloff);
}
