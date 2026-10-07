#pragma once

#include <stdint.h>

/** @file
 * The aim, recoil and rate-of-fire rules: what the shooter's chance is before
 * the equipment's share, how a burst's accuracy walks away from it, and which
 * weapons may lay down automatic fire (issue #102, docs/plan/equipment-revamp.md).
 *
 * This is the pure half above `DamagePipeline` (#260): the pipeline owns what
 * the round does once it is on its way (delivery, penetration, damage, wear,
 * noise) and takes `ShotInput::aimChance` as an input. This file produces that
 * input - aim levels, the aim bonus curve, recoil and its decay - and gates the
 * fire disciplines by weapon role. It reads no global, no soldier and no RNG,
 * so a unit test can walk every rule as a table and the fight and the readout
 * (#141) agree on the numbers.
 *
 * ## The three decisions this file encodes
 *
 *  1. **Aim is a diminishing purchase.** The n-th extra AP spent aiming is
 *     worth less than the one before it (`AimBonus`), so a shot is a choice
 *     between time and accuracy rather than a slider that always pays. The
 *     curve equals the vanilla flat bonus at four clicks, so the default feel
 *     is kept while the tail stays useful for a marksman.
 *  2. **Recoil is a pool, not a per-shot penalty.** Automatic fire adds to a
 *     recoil pool that costs accuracy (`RecoilPenalty`); the pool decays while
 *     the soldier is not firing (`RecoilDecay`), so it carries a burst into the
 *     next exchange (an interrupt, an overwatch shot) instead of resetting.
 *  3. **Automatic fire is a role.** Only the weapons built to lay down volume
 *     (`SupportsAutofire`) may use the continuous mode; everything else gets
 *     the fixed burst its data declares.
 */
namespace Equipment
{

/** The fire discipline of a weapon, derived from its class. Autofire is gated
 *  by this, and the aim curve is scaled by it, so a weapon's role - not a
 *  per-item table - decides how it aims. */
enum class AimDiscipline : uint8_t
{
	Handgun,
	SMG,
	Shotgun,
	Rifle,      // assault rifle: the default autofire role
	Marksman,   // precision rifle/sniper: the highest aim ceiling
	MachineGun, // volume of fire: autofire, but a low precision ceiling
	Launcher,
	Other,
};

/** The engine's hard cap on extra AP spent aiming. The player UI tops out a
 *  level below this today; the core supports the tail so a marksman's aim can
 *  keep improving without a rule change. */
constexpr int AIM_LEVEL_MAX = 8;

/** The weapon class -> discipline map. `weaponClass` is one of the game's
 *  WEAPON CLASSES (Weapons.h: HANDGUNCLASS, SMGCLASS, RIFLECLASS, MGCLASS,
 *  SHOTGUNCLASS, ...); the numeric values are stable enum order, kept as an
 *  int here so this core does not include the game's headers. A rifle whose
 *  effective range reaches `MARKSMAN_RANGE` is a `Marksman` rather than a
 *  `Rifle`, which is the role distinction #261 curates the catalog around. */
constexpr int MARKSMAN_RANGE = 40; // tiles at which a rifle is a marksman's
AimDiscipline DisciplineForWeapon(int weaponClass, int rangeTiles);

/** The most extra AP this discipline may spend aiming (0..AIM_LEVEL_MAX). A
 *  close-quarters weapon cannot be aimed as far as a marksman's. */
int AimCeiling(AimDiscipline discipline);

/** The percent added by @a level extra AP spent aiming, at @a perLevel the
 *  policy's aim bonus per standard AP and @a scalePercent the discipline's
 *  multiplier. Diminishing and monotonic: each click is worth at least one
 *  point and no more than the click before it, so the first buys the most and
 *  the tail stays useful without ever being free. At four clicks with
 *  `scalePercent == 100` it equals the vanilla `4 * perLevel`. */
int AimBonus(int level, int perLevel, int scalePercent);

/** The discipline's aim bonus multiplier, in percent. Marksman keeps the most
 *  of its aim (`AimBonus` grows highest), a machine gun the least. */
int AimScale(AimDiscipline discipline);

/** The recoil one round adds before mitigation: the discipline's base plus the
 *  weapon's own kick. `weaponRecoil` is the bridge from the legacy weapon data
 *  (see `RecoilForWeapon`). */
int RecoilPerShot(AimDiscipline discipline, int weaponRecoil);

/** The weapon's own recoil contribution from its legacy stats: its per-shot
 *  burst penalty and (weakly) its impact. Monotonic, so the compiled catalog
 *  (#100) can replace the numbers without changing the rule. */
int RecoilForWeapon(int weaponClass, int burstPenalty, int impact);

/** How much of the recoil the shooter shrugs off, in percent, from strength,
 *  experience, the `AUTO_WEAPS` trait, stance and a fitted foregrip/bipod.
 *  Body height is the game's ANIM_STAND/ANIM_CROUCH/ANIM_PRONE values. */
struct RecoilMitigators
{
	int  strength      = 50;   // effective strength, 0..100
	int  level         = 1;    // experience level, 1..10
	int  autoTrait     = 0;    // number of AUTO_WEAPS traits (0..2)
	int  bodyHeight    = 0;    // ANIM_STAND (0) / ANIM_CROUCH (1) / ANIM_PRONE (2)
	bool bipod         = false;
	bool foregrip      = false;
};
int RecoilMitigation(const RecoilMitigators& in);

/** The recoil one round adds after `mitigationPercent` is taken off, never
 *  less than one so a shot always leaves the barrel a little further off. */
int RecoilGain(AimDiscipline discipline, int weaponRecoil, int mitigationPercent);

/** The pool after one more shot, capped at 100. */
int RecoilAfterShot(int current, int gain);

/** The pool after @a steps of not firing. Decay is a floor at zero. */
int RecoilDecay(int current, int steps);

/** How much accuracy the pool currently costs, in percent (0..100). This is
 *  the number the adapter subtracts from the chance and the readout shows. */
int RecoilPenalty(int recoil);

/** The recoil a pool of a fixed size costs per step: the constant `RecoilDecay`
 *  removes. A named value so the readout and the tests share it. */
constexpr int RECOIL_DECAY_PER_TURN = 20;

/** Whether this discipline may use continuous automatic fire (issue #102's
 *  role gate). Machine guns and assault rifles, and only those. */
bool SupportsAutofire(AimDiscipline discipline);

const char* Describe(AimDiscipline discipline);

} // namespace Equipment
