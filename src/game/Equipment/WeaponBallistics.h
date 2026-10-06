#pragma once

#include "DamagePipeline.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

struct OBJECTTYPE;

/** @file
 * The range and ballistics readout (issue #141, docs/plan/equipment-revamp.md):
 * a compact view over the damage pipeline (#260) so a player can see, before
 * committing, what a weapon does at range - hit chance, damage that reaches the
 * body, whether the round gets through the armour in the way, and how loud it
 * is - and can compare two weapons side by side.
 *
 * The pipeline is the only source of the numbers: `ComputeReadout` calls
 * `ResolveShot` once per range band, and `CompareReadouts` marks which of two
 * readouts is better on each axis. Nothing here reads a global or the RNG, so
 * the whole readout is a pure function of its inputs and a unit test can walk
 * it as a table.
 *
 * The bridge from the game's own weapon data to the pipeline's profile lives
 * here too (`WeaponProfileFor`). It is the interim mapping until the compiled
 * weapon catalog (#100) owns per-weapon stats; the tests pin its invariants so
 * replacing it with curated numbers stays a data change.
 */
namespace Equipment
{

/** The distances (tiles) the compact readout shows: point blank, close, the
 *  effective band of most weapons and two bands beyond it, so range falloff is
 *  visible. The screen may show fewer; these are the ones it is built around. */
constexpr int READOUT_BANDS[] = { 4, 8, 12, 16, 24, 32 };
constexpr size_t NUM_READOUT_BANDS = sizeof(READOUT_BANDS) / sizeof(READOUT_BANDS[0]);

/** One row of the readout: a distance and what the pipeline decided there.
 *  `residual` is penetration minus the armour's threshold - the "through by N"
 *  the readout shows, negative when the armour held. */
struct ReadoutBand
{
	int distance    = 0; // tiles
	int chanceToHit = 0; // % the roll is taken against
	int impact      = 0; // the damage the round wanted to do
	int damage      = 0; // what reached the body after the armour's absorption
	int penetration = 0; // the round's penetration power at this range
	int threshold   = 0; // what the armour in the way needed
	int residual    = 0; // penetration - threshold (through-by-N)
	int noise       = 0; // muzzle noise
	int wear        = 0; // condition points one shot costs
	ShotOutcome outcome = ShotOutcome::Unopposed;
	bool inRange    = true; // the round arrives at full delivery (distance <= effective range)
};

/** Everything the comparison needs about one weapon + ammo against one armour
 *  tier, at the readout's bands. `usable` is false when there is no weapon (or
 *  nothing to shoot it with), so the screen can say so instead of showing a
 *  row of zeroes. */
struct Readout
{
	bool usable = false;
	int  effectiveRange = 0; // tiles the round arrives at full delivery
	int  muzzleNoise    = 0; // the weapon's noise before the ammo's share
	int  condition      = 100;
	AmmoType ammo   = AmmoType::Ball;
	ArmourTier armour = ArmourTier::Unarmoured;
	std::vector<ReadoutBand> bands;
};

/** Which of two values is better at a row. `Even` when they are equal. */
enum class Better { A, B, Even };

/** Which value is better; `higherIsBetter` picks the direction. */
Better BetterValue(int a, int b, bool higherIsBetter);

/** One row of a comparison: who wins each axis at this distance. Noise is the
 *  only axis where lower is better. */
struct BandCompare
{
	int distance = 0;
	Better hit         = Better::Even;
	Better damage      = Better::Even;
	Better penetration = Better::Even;
	Better noise       = Better::Even;
};

/** A comparison of two readouts: both readouts, and the per-band winners the
 *  screen highlights. Bands line up by distance (the shorter list's length). */
struct Comparison
{
	Readout a, b;
	std::vector<BandCompare> bands;
};

/** The readout for one weapon + ammo against one armour tier, at @a distances.
 *  `aimChance` is the shooter's chance before the equipment's share (the
 *  pipeline's `ShotInput::aimChance`); the readout compares weapons, so a
 *  standard shooter is the point. Pure. */
Readout ComputeReadout(WeaponProfile const& weapon, AmmoProfile const& ammo,
	ArmourProfile const& armour, int weaponCondition, int aimChance,
	const std::vector<int>& distances, PipelineToggles const& toggles = PipelineToggles());

/** `ComputeReadout` at the default bands (READOUT_BANDS). */
Readout ComputeReadout(WeaponProfile const& weapon, AmmoProfile const& ammo,
	ArmourProfile const& armour, int weaponCondition, int aimChance,
	PipelineToggles const& toggles = PipelineToggles());

/** Compare two readouts at the same distances. */
Comparison CompareReadouts(Readout const& a, Readout const& b);

// --- the bridge from the game's weapon data to the pipeline's profile -------

/** The pipeline profile of a weapon from the game's own stats: `impact` is the
 *  weapon's ubImpact, `rangeTiles` its effective range in tiles (GunRange/10),
 *  `attackVolume` its ubAttackVolume and `reliability` its bReliability.
 *
 *  Penetration is derived from impact for now: the pipeline's separate
 *  penetration stat is what the compiled catalog (#100) will fill in, and until
 *  then the ammo type is what moves penetration between the counters. The
 *  invariants (a harder-hitting weapon penetrates more, a more reliable one
 *  wears less) are unit-tested so #100 changes numbers, not behavior. */
WeaponProfile WeaponProfileFromStats(int impact, int rangeTiles, int attackVolume, int reliability);

/** The pipeline profile of a real weapon item, as the game carries it, including
 *  a fitted suppressor's effect on the muzzle noise. Returns a profile with
 *  damage 0 when the item is not a gun (the caller decides what to show). */
WeaponProfile WeaponProfileFor(const OBJECTTYPE& gun);

/** The pipeline ammo type of one of the game's ammo indices (Weapons.h
 *  AMMO_REGULAR, AMMO_HP, AMMO_AP, ...). Ammunition that the small-arms
 *  pipeline does not model (buckshot, flechette, grenades) reads as ball. */
AmmoType AmmoTypeFromGameIndex(int gameAmmoIndex);

/** The muzzle noise a shot makes with a suppressor of @a suppressorCondition
 *  fitted, as the legacy shot reduces it (Weapons.cc UseGun), never more than
 *  the bare weapon's noise. `attackVolume` is the weapon's ubAttackVolume. */
int SilencedVolume(int attackVolume, int suppressorCondition);

} // namespace Equipment
