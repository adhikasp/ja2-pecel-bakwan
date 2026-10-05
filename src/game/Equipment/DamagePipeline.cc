#include "DamagePipeline.h"

#include "GamePolicy.h"

#include <algorithm>

namespace Equipment
{

namespace
{
	// The ammunition table: the four types and how each is built. A type is a
	// trade-off, never a bonus - every row that is better at something is worse
	// at something else, which is the track's rule 3 ("a counter for every
	// strength") written as four rows.
	//
	// Calibers and magazines are #97's business; what the pipeline needs is the
	// relationship between the types, and that is here.
	const AmmoProfile AMMO[NUM_AMMO_TYPES] =
	{
		// type           dmg  pen noise range wear
		{ AmmoType::Ball,         100, 100, 100, 100, 100 },
		// The hard core goes through armour and does less to flesh, and it is
		// hard on the barrel.
		{ AmmoType::Piercing,      85, 150, 105, 100, 130 },
		// The mushroom does terrible damage to a man and almost nothing to a
		// plate.
		{ AmmoType::HollowPoint,  170,  60, 100, 100,  90 },
		// Quiet: a fifth of the noise, two thirds of the range, and a round that
		// arrives with little left.
		{ AmmoType::Subsonic,      80,  90,  20,  65, 110 },
	};

	// The armour tiers. A soft vest is protection against fragments and blades;
	// a plate is protection against a rifle round, until it is shot enough times
	// that its condition drops. Helmet and leggings are the same tiers at
	// different coverage (#101 curates the items).
	const ArmorProfile ARMOR[NUM_ARMOR_TIERS] =
	{
		// tier            prot  thr  degrade
		{ ArmorTier::Unarmoured,   0,  0,  0 },
		{ ArmorTier::Soft,        20, 18,  2 },
		{ ArmorTier::Plate,       60, 55,  6 },
	};

	const AmmoType AMMO_ORDER[NUM_AMMO_TYPES] =
	{
		AmmoType::Ball, AmmoType::Piercing, AmmoType::HollowPoint, AmmoType::Subsonic,
	};

	const ArmorTier ARMOR_ORDER[NUM_ARMOR_TIERS] =
	{
		ArmorTier::Unarmoured, ArmorTier::Soft, ArmorTier::Plate,
	};

	// The counter table: one explicit cell per ammo type per armour tier, as
	// (absorbPercent, fleshPercent, bluntPercent). See AmmoVersusArmor.
	//
	// This is the answer to "what is the counter for a plate?" and the reason
	// the pipeline cannot be four systems: the armour tier and the ammo type do
	// not have separate rules, they have one cell between them.
	const AmmoVersusArmor COUNTER[NUM_AMMO_TYPES][NUM_ARMOR_TIERS] =
	{
		//                     unarmoured      soft             plate
		/* ball     */ { {   0, 100,  0 }, { 100,  90, 25 }, { 100, 100,  0 } },
		// Armour piercing: half of the vest's protection and 60% of the plate's
		// is simply not there in its way, and what does get through hurts as much
		// as ball would have. Its price is paid on flesh: the hard core does 85%
		// of ball's damage to a bare man, which is why it is the worst round in
		// the game against somebody wearing nothing.
		/* piercing */ { {   0, 100,  0 }, {  50, 100, 15 }, {  40, 100,  0 } },
		// Hollow point: a plate soaks 175% of its protection because the bullet
		// is deforming against it, and only 45% of what gets through is real
		// damage. It is the best round against a bare man and the worst against
		// armour, which is the whole point of it.
		/* hollow   */ { {   0, 100,  0 }, { 150,  40, 40 }, { 175,  45,  0 } },
		// Subsonic: a slow, light round. Everything is a little worse and it is
		// a fifth as loud.
		/* subsonic */ { {   0, 100,  0 }, { 110,  90, 25 }, { 130,  80,  0 } },
	};

	// How much of the round's delivery is left at a given distance, in percent.
	// A fixed loss per tile past the weapon's effective range, down to a floor -
	// a rifle round at its limit is a bad shot, not no shot.
	constexpr int16_t FALLOFF_PER_TILE = 4;
	constexpr int16_t MIN_DELIVERY    = 25;

	int16_t Clamp(int32_t value, int32_t low, int32_t high)
	{
		return static_cast<int16_t>(std::max(low, std::min(high, value)));
	}

	int16_t Index(AmmoType ammo)  { return static_cast<int16_t>(ammo); }
	int16_t Index(ArmorTier tier) { return static_cast<int16_t>(tier); }
}

AmmoVersusArmor CounterFor(AmmoType ammo, ArmorTier tier)
{
	return COUNTER[Index(ammo)][Index(tier)];
}

AmmoProfile AmmoProfileFor(AmmoType type)
{
	return AMMO[Index(type)];
}

ArmorProfile ArmorProfileFor(ArmorTier tier)
{
	return ARMOR[Index(tier)];
}

const AmmoType* AllAmmoTypes(size_t& count)
{
	count = NUM_AMMO_TYPES;
	return AMMO_ORDER;
}

const ArmorTier* AllArmorTiers(size_t& count)
{
	count = NUM_ARMOR_TIERS;
	return ARMOR_ORDER;
}

ShotResult ResolveShot(const ShotInput& in, const PipelineToggles& toggles)
{
	ShotResult out;

	// --- 1. delivery: how much of the round arrives at all --------------------
	// The ammo's range share is applied first, so subsonic really does shorten
	// the weapon's reach rather than shortening it after the fact.
	int16_t const effectiveRange = std::max<int16_t>(1, static_cast<int16_t>(
		static_cast<int32_t>(in.weapon.range) * in.ammo.rangePercent / 100));

	int16_t delivery = 100;
	if (in.distance > effectiveRange)
	{
		int32_t const past = static_cast<int32_t>(in.distance - effectiveRange) * FALLOFF_PER_TILE
			* toggles.rangeFalloff / 100;
		delivery = std::max<int16_t>(MIN_DELIVERY, static_cast<int16_t>(100 - past));
	}

	// A worn gun does not send the round as hard, and never better than it did.
	int16_t const conditionFactor = static_cast<int16_t>(50 + in.weaponCondition / 2);
	int16_t const roundFactor = static_cast<int16_t>(
		static_cast<int32_t>(delivery) * conditionFactor / 100);

	// --- 2. impact: the damage the round wants to do ------------------------
	int16_t const energy = static_cast<int16_t>(
		static_cast<int32_t>(in.weapon.damage) * roundFactor / 100);
	out.impact = static_cast<int16_t>(
		static_cast<int32_t>(energy) * in.ammo.damagePercent / 100);

	// --- 3. absorption: what the armour in the way takes ---------------------
	// Armour condition enters here and in the threshold below, and nowhere else:
	// a worn plate both stops less and is easier to get through, which is what
	// makes shooting somebody's armour a way to win the fight.
	int16_t const armorFactor = Clamp(in.armorCondition, 0, 100);
	int16_t const protection  = static_cast<int16_t>(
		static_cast<int32_t>(in.armor.protection) * armorFactor / 100);
	int16_t const threshold   = static_cast<int16_t>(
		static_cast<int32_t>(in.armor.threshold) * armorFactor / 100);

	// A tier worn to nothing is not armour any more: read the unarmoured cell, so
	// a destroyed vest is exactly no vest rather than a vest with odd numbers.
	AmmoVersusArmor const counter = CounterFor(in.ammo.type,
		(threshold > 0) ? in.armor.tier : ArmorTier::Unarmoured);

	out.absorbed = std::min(out.impact, static_cast<int16_t>(
		static_cast<int32_t>(protection) * counter.absorbPercent / 100));
	out.through  = static_cast<int16_t>(out.impact - out.absorbed);
	out.penetration = static_cast<int16_t>(
		static_cast<int32_t>(in.weapon.penetration) * in.ammo.penetrationPercent * roundFactor / 10000);
	out.threshold   = threshold;
	// The surplus over the threshold. Zero when nothing was in the way: there
	// was no threshold to clear, so there is no surplus to report.
	out.residual    = (threshold == 0) ? 0 : static_cast<int16_t>(out.penetration - threshold);

	// --- 4. the threshold and residual, 5. the damage ------------------------
	// `flesh` is what the round does to a body once it is past the protection;
	// `brused` is what a stopped round still does, in proportion to how close it
	// came to getting through. Both are read from the same counter cell, so a
	// type cannot be good at one and bad at the other by accident.
	int32_t const flesh  = static_cast<int32_t>(out.through) * counter.fleshPercent / 100;
	int32_t const brused = flesh * counter.bluntPercent / 100;

	if (threshold == 0)
	{
		// Nothing was in the way, or what was in the way is no longer there.
		out.outcome = ShotOutcome::Unopposed;
		out.damage  = static_cast<int16_t>(flesh);
	}
	else if (out.residual >= 0)
	{
		// Through. The residual is reported rather than banked as damage: it is
		// the number the readout shows ("went through by 20"), so the player can
		// tell a round that just cleared the plate from one that ignored it. How
		// much actually hurt is the armour's subtraction from the round, not a
		// bonus on top of it, which is what keeps more armour from ever being
		// worse for the man wearing it.
		out.outcome = ShotOutcome::Penetrated;
		out.damage  = static_cast<int16_t>(flesh);
	}
	else
	{
		// Stopped. How close it came decides whether the impact left a mark: a
		// round a hair under the threshold bruises, one that never had a chance
		// does nothing at all.
		//
		// The dead zone is half the threshold. Below that the armour was never
		// really tested, so there is no shock to transmit - and it is what makes
		// `Stopped` reachable against a soft vest at all, rather than a vest that
		// stops everything a little.
		int32_t const reach = std::max<int32_t>(0, std::min(out.penetration, threshold) - threshold / 2);
		int32_t const proximity = reach * 200 / threshold;
		int32_t const bruise = brused * proximity / 100;
		out.outcome = (bruise > 0) ? ShotOutcome::BluntTrauma : ShotOutcome::Stopped;
		out.damage  = static_cast<int16_t>(bruise);
	}

	// --- 6. the cost of the shot --------------------------------------------
	// Wear scales with how worn the weapon already is: a neglected gun falls
	// apart faster than a maintained one, which is what the mechanic's skill
	// and the cleaning kit are buying.
	out.wear = std::max<int16_t>(0, static_cast<int16_t>(
		static_cast<int32_t>(in.weapon.wear) * in.ammo.wearPercent * (200 - in.weaponCondition)
		* toggles.wearRate / 1000000));
	out.armorWear = static_cast<int16_t>(
		static_cast<int32_t>(out.absorbed) * in.armor.degradePercent / 100);
	out.noise     = static_cast<int16_t>(
		static_cast<int32_t>(in.weapon.noise) * in.ammo.noisePercent / 100);

	// --- the chance, and the one roll ----------------------------------------
	// The shooter's chance is the input; the equipment's share of the answer is
	// the delivery (a spent round is hard to place) and the weapon's condition
	// (a loose gun wanders). Everything else about aiming is #102's.
	int16_t chance = in.aimChance - (100 - delivery) - (100 - in.weaponCondition) / 2;
	out.chanceToHit = std::max<int16_t>(1, std::min<int16_t>(99, chance));
	out.hit = in.roll < out.chanceToHit;

	if (!out.hit)
	{
		// The round left the barrel and cost what it cost; it just did not
		// arrive. Wear and noise still happened.
		out.damage   = 0;
		out.absorbed = 0;
		out.through  = 0;
		out.outcome  = ShotOutcome::Missed;
	}

	out.weaponConditionAfter = Clamp(in.weaponCondition - out.wear, 0, 100);
	out.armorConditionAfter  = Clamp(in.armorCondition - out.armorWear, 0, 100);
	return out;
}

PipelineToggles PipelineTogglesFrom(const GamePolicy* policy)
{
	PipelineToggles toggles;
	if (policy == nullptr) return toggles;
	toggles.wearRate     = policy->pipeline_wear_rate;
	toggles.rangeFalloff = policy->pipeline_range_falloff;
	return toggles;
}

const char* Describe(AmmoType ammo)
{
	switch (ammo)
	{
		case AmmoType::Ball:        return "ball";
		case AmmoType::Piercing:    return "armour piercing";
		case AmmoType::HollowPoint: return "hollow point";
		case AmmoType::Subsonic:    return "subsonic";
	}
	return "ball";
}

const char* Describe(ArmorTier tier)
{
	switch (tier)
	{
		case ArmorTier::Unarmoured: return "no armour";
		case ArmorTier::Soft:       return "soft armour";
		case ArmorTier::Plate:      return "plate";
	}
	return "no armour";
}

const char* Describe(ShotOutcome outcome)
{
	switch (outcome)
	{
		case ShotOutcome::Missed:      return "missed";
		case ShotOutcome::Unopposed:   return "hit nothing was wearing";
		case ShotOutcome::Penetrated:  return "penetrated";
		case ShotOutcome::BluntTrauma: return "blunt trauma";
		case ShotOutcome::Stopped:     return "stopped";
	}
	return "missed";
}

} // namespace Equipment
