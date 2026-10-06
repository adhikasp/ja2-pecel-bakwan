#pragma once

#include <stddef.h>
#include <stdint.h>

class GamePolicy;

/** @file
 * The damage resolution pipeline: weapon x ammo x range x armour x condition ->
 * hit chance, penetration, damage, wear and noise, in one pure function
 * (issue #260, docs/plan/equipment-revamp.md).
 *
 * This is the interdependency hub of the equipment track. Ammunition (#97),
 * armour (#101), NCTH (#102) and condition (#98) are not four systems that
 * happen to agree: they are four inputs to this one calculation, and each of
 * them is a table row here. Nothing in this file reads a global, a soldier or
 * the RNG - the caller's roll is the only random value, which is what makes the
 * whole resolution drivable headless and assertable as a table.
 *
 * ## The resolution, in order
 *
 *  1. **Delivery.** How much of the round arrives. Range beyond the weapon's
 *     effective range costs a fixed percentage per tile (and subsonic shortens
 *     that range, which is the price of quiet); a worn weapon delivers less.
 *  2. **Impact.** The damage the round wants to do: delivery x the ammo's damage
 *     percentage. This is the number the armour is subtracted from.
 *  3. **Absorption.** What the armour in the way takes: its protection x the
 *     ammo's absorb percentage against that tier, capped at the impact. This is
 *     the counter table - see `CounterFor`.
 *  4. **Threshold and residual.** Penetration power at this range against the
 *     tier's threshold, both scaled by how worn the armour is. A round at or
 *     over the threshold gets through; the surplus over it is the *residual*,
 *     the number the readout shows so a player can tell "just cleared the
 *     plate" from "ignored it". A round under the threshold is stopped, and one
 *     that landed close to it still bruises (`BluntTrauma`).
 *  5. **Damage.** What reaches the body is the impact minus the absorption,
 *     scaled by the counter cell's flesh share. Armour is a subtraction from
 *     what the round does to a bare man and never an addition to it, so more
 *     armour is never worse for the man wearing it - a property of the function,
 *     not a hope about the numbers.
 *  6. **Cost.** Wear on the weapon (scaled by how worn it already is), wear on
 *     the armour (what it absorbed, at its degrade rate) and the muzzle noise.
 *
 * ## No randomness but the roll
 *
 * The only random value in a shot is `ShotInput::roll`. There is no ricochet,
 * no deflection, no second roll: the outcome is a pure function of the inputs,
 * so the same loadout at the same range with the same roll resolves the same
 * shot every time. That is what `ShotOutcome` is for - it names the results
 * the rest of the track reads instead of leaving them to be inferred from
 * arithmetic.
 */
namespace Equipment
{

/** The four ammunition types. The strengths are the counters to each other:
 *  ball is the baseline, piercing answers armour, hollow point answers flesh,
 *  and subsonic buys quiet with range and damage. */
enum class AmmoType : uint8_t
{
	Ball,
	Piercing,
	HollowPoint,
	Subsonic,
};

/** The armour tiers. `Unarmoured` is the tier nothing protects with, and it is
 *  a real row so "no armour" is a case of the table rather than a special case
 *  beside it. */
enum class ArmourTier : uint8_t
{
	Unarmoured,
	Soft,
	Plate,
};

constexpr int NUM_AMMO_TYPES = 4;
constexpr int NUM_ARMOUR_TIERS = 3;

/** A weapon platform, as the pipeline needs it. Content (#100) fills these in;
 *  the pipeline only reads them. */
struct WeaponProfile
{
	int16_t damage      = 40; // muzzle impact, before range and condition
	int16_t penetration = 50; // penetration power at the muzzle
	int16_t range       = 12; // tiles the round arrives at full delivery
	int16_t noise       = 50; // muzzle noise volume, before the ammo's share
	int16_t wear        = 2;  // condition points a shot costs at full condition
};

/** One ammunition type's multipliers, in percent. */
struct AmmoProfile
{
	AmmoType type          = AmmoType::Ball;
	int16_t  damagePercent = 100;      // against unarmoured flesh
	int16_t  penetrationPercent = 100; // against the tier's threshold
	int16_t  noisePercent  = 100;      // of the weapon's muzzle noise
	int16_t  rangePercent  = 100;      // of the weapon's effective range
	int16_t  wearPercent   = 100;      // of the weapon's per-shot wear
};

/** One armour tier's numbers. `threshold` is the penetration power a round
 *  needs to get through; `degradePercent` is the share of what it absorbed
 *  that comes off the armour's condition. */
struct ArmourProfile
{
	ArmourTier tier            = ArmourTier::Unarmoured;
	int16_t   protection      = 0; // damage it soaks at full condition
	int16_t   threshold       = 0; // penetration power needed to get through
	int16_t   degradePercent  = 0; // condition lost per point of damage absorbed
};

/** How one ammunition type answers one armour tier - the explicit counter for
 *  that pairing, and the whole reason the pipeline is one function.
 *
 *  - `absorbPercent` scales the tier's protection. Over 100 means the tier stops
 *    more than it would otherwise (hollow point mushrooms against a plate);
 *    under 100 means the round goes through more easily (armour piercing).
 *  - `fleshPercent` is the share of the round's damage that still reaches the
 *    body through that tier. 100 is no loss; a plate that a hollow point cannot
 *    defeat passes 45.
 *  - `bluntPercent` is the share of a *stopped* round's damage that still lands
 *    as bruising, scaled by how close the round came to the threshold. Zero for
 *    a plate: nothing bruises, or it is not a plate. */
struct AmmoVersusArmour
{
	int16_t absorbPercent;
	int16_t fleshPercent;
	int16_t bluntPercent;
};

/** The named results the rest of the track reads. There is deliberately no
 *  ricochet: the resolution is a pure function of its inputs and the one roll
 *  the caller took, so nothing can glance off. */
enum class ShotOutcome : uint8_t
{
	Missed,      // the roll did not connect
	Unopposed,   // it landed and nothing was in the way
	Penetrated,  // armour was in the way and the round got through it
	BluntTrauma, // armour was in the way and held; the impact still hurt
	Stopped,     // armour was in the way and nothing got through
};

constexpr int NUM_SHOT_OUTCOMES = 5;

/** One shot, as the pipeline sees it. */
struct ShotInput
{
	WeaponProfile weapon;
	AmmoProfile   ammo;
	ArmourProfile  armour;

	int16_t distance          = 8;  // tiles from the shooter to the target
	int16_t weaponCondition   = 100; // 0..100
	int16_t armourCondition    = 100; // 0..100, plate wear
	int16_t aimChance         = 70; // the shooter's chance before the equipment's share
	uint8_t roll              = 0;  // the only random value in a shot, 0..99
};

/** What the pipeline decided. Every field is a number a fixture or the readout
 *  can assert on, which is the point of resolving this in one place. */
struct ShotResult
{
	int16_t chanceToHit = 0;
	bool    hit         = false;
	ShotOutcome outcome = ShotOutcome::Missed;

	int16_t impact     = 0; // the damage the round wanted to do
	int16_t absorbed   = 0; // what the armour in the way took
	int16_t through    = 0; // what was left of the impact
	int16_t damage     = 0; // what the body took, after the armour's absorption

	int16_t penetration = 0; // the round's penetration power at this range
	int16_t threshold   = 0; // what the armour in the way needed
	int16_t residual    = 0; // penetration - threshold: how far past, or short

	int16_t wear        = 0; // condition points the shot cost the weapon
	int16_t armourWear   = 0; // condition points the round cost the armour
	int16_t noise       = 0; // muzzle noise volume

	int16_t weaponConditionAfter = 0;
	int16_t armourConditionAfter  = 0;
};

/** Leaf tunables. The rules are compiled and fixed; only these move, and they
 *  come from GamePolicy. Defaults match the default policy. */
struct PipelineToggles
{
	int16_t wearRate      = 100; // scales the wear every shot applies
	int16_t rangeFalloff  = 100; // scales the per-tile loss past the effective range
};

/** The one resolution: everything the pipeline decides about one shot, in one
 *  pure call. No globals, no game state, no RNG - `in.roll` is the only random
 *  value, and the caller takes it from the seeded stream. */
ShotResult ResolveShot(const ShotInput& in, const PipelineToggles& toggles = PipelineToggles());

/** The counter for one ammo type against one armour tier. The whole table,
 *  read one cell at a time. */
AmmoVersusArmour CounterFor(AmmoType ammo, ArmourTier tier);

/** The compiled ammunition table (#97 fills in the calibers and magazines;
 *  these four rows are the type-to-type relationships the pipeline needs). */
AmmoProfile AmmoProfileFor(AmmoType type);

/** The compiled armour tiers (#101 curates the items; these are the tiers). */
ArmourProfile ArmourProfileFor(ArmourTier tier);

/** Every ammo type and every tier, in table order, for a test that walks the
 *  whole matrix. */
const AmmoType* AllAmmoTypes(size_t& count);
const ArmourTier* AllArmourTiers(size_t& count);

/** Leaf tunables as the game policy carries them. Named apart from the other
 *  `TogglesFrom` in this namespace (the slot policy has one too) so a caller
 *  reading a call site can see which policy it is reaching for. */
PipelineToggles PipelineTogglesFrom(const GamePolicy* policy);

const char* Describe(AmmoType ammo);
const char* Describe(ArmourTier tier);
const char* Describe(ShotOutcome outcome);

} // namespace Equipment
