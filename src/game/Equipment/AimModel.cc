#include "AimModel.h"

#include <algorithm>

namespace Equipment
{

namespace
{
	// The game's WEAPON CLASSES (Weapons.h). Spelled out here so this pure core
	// does not pull in the game headers; the enum order is stable.
	constexpr int NOGUNCLASS   = 0;
	constexpr int HANDGUNCLASS = 1;
	constexpr int SMGCLASS     = 2;
	constexpr int RIFLECLASS   = 3;
	constexpr int MGCLASS      = 4;
	constexpr int SHOTGUNCLASS = 5;
	// KNIFECLASS = 6, MONSTERCLASS = 7 land in Other.

	// The aim curve is a sum of diminishing per-click increments; see AimBonus.
	// At four clicks on the default policy it adds up to exactly the vanilla
	// flat bonus (4 * perLevel), and the tail keeps improving more slowly.

	// The body-height values the game uses for stance (gAnimControl .ubEndHeight).
	constexpr int HEIGHT_STAND  = 0;
	constexpr int HEIGHT_CROUCH = 1;
	constexpr int HEIGHT_PRONE  = 2;

	int Clamp(int v, int lo, int hi)
	{
		return std::max(lo, std::min(hi, v));
	}

	int DisciplineIndex(AimDiscipline d) { return static_cast<int>(d); }
}

AimDiscipline DisciplineForWeapon(int const weaponClass, int const rangeTiles)
{
	switch (weaponClass)
	{
		case HANDGUNCLASS: return AimDiscipline::Handgun;
		case SMGCLASS:     return AimDiscipline::SMG;
		case SHOTGUNCLASS: return AimDiscipline::Shotgun;
		case MGCLASS:      return AimDiscipline::MachineGun;
		case RIFLECLASS:
			// A long-reaching rifle is a marksman's weapon: same class in the
			// data, a different role in the fight.
			return rangeTiles >= MARKSMAN_RANGE ? AimDiscipline::Marksman : AimDiscipline::Rifle;
		case NOGUNCLASS:   return AimDiscipline::Other;
		default:           return AimDiscipline::Other;
	}
}

namespace
{
	// Per-discipline aim ceilings, in extra AP (0..AIM_LEVEL_MAX). The order is
	// the AimDiscipline enum: Handgun, SMG, Shotgun, Rifle, Marksman, MG, Launcher, Other.
	const int AIM_CEILING[] =
	{
		4, // Handgun
		3, // SMG     - close quarters, you point it
		2, // Shotgun - spread and close
		5, // Rifle   - assault
		8, // Marksman - the precision ceiling, the whole point of the role
		3, // MG      - volume, not precision
		2, // Launcher
		3, // Other
	};

	// Per-discipline aim multipliers, in percent. A marksman gets more out of
	// the same AP; a machine gun less.
	const int AIM_SCALE[] =
	{
		90,  // Handgun
		70,  // SMG
		80,  // Shotgun
		100, // Rifle
		140, // Marksman
		70,  // MG
		50,  // Launcher
		100, // Other
	};

	// Per-discipline recoil one round adds, before the weapon's own kick.
	const int RECOIL_BASE[] =
	{
		10, // Handgun
		7,  // SMG
		16, // Shotgun
		9,  // Rifle
		13, // Marksman (a big round)
		5,  // MG (heavy, braced, and meant to climb a little)
		20, // Launcher
		10, // Other
	};
}

int AimCeiling(AimDiscipline const discipline)
{
	return AIM_CEILING[DisciplineIndex(discipline)];
}

int AimScale(AimDiscipline const discipline)
{
	return AIM_SCALE[DisciplineIndex(discipline)];
}

int AimBonus(int const level, int const perLevel, int const scalePercent)
{
	if (level <= 0 || perLevel <= 0) return 0;
	int const capped = std::min(level, AIM_LEVEL_MAX);
	int const scaled = perLevel * std::max(0, scalePercent) / 100;
	if (scaled <= 0) return 0;
	// Sum the per-click increments, each strictly smaller than the one before:
	// click k is worth `scaled * 12 / ((k+1)(k+2))`, floored at 1 so aiming
	// always helps. Summing the (integer) increments is what keeps the total
	// monotonic and diminishing at the same time; a single closed-form division
	// would round its way into a bump.
	int total = 0;
	for (int k = 1; k <= capped; ++k)
	{
		total += std::max(1, scaled * 12 / ((k + 1) * (k + 2)));
	}
	return total;
}

int RecoilForWeapon(int const weaponClass, int const burstPenalty, int const impact)
{
	(void)weaponClass;
	// The legacy burst penalty is the closest thing the data has to a kick, and
	// impact tracks the round's mass, so the two together are monotonic: a
	// harder-hitting, harder-to-control weapon recoils more. #100 replaces the
	// numbers, not the rule.
	return Clamp(burstPenalty / 3 + impact / 20, 0, 25);
}

int RecoilPerShot(AimDiscipline const discipline, int const weaponRecoil)
{
	return std::max(0, RECOIL_BASE[DisciplineIndex(discipline)] + std::max(0, weaponRecoil));
}

int RecoilMitigation(const RecoilMitigators& in)
{
	int mitigation = 0;
	// Strength: a strong shooter absorbs more (up to 20%).
	mitigation += Clamp((in.strength - 50) / 5, 0, 20);
	// Experience: he has fired this weapon before (up to 18%).
	mitigation += Clamp((in.level - 1) * 2, 0, 18);
	// The autoweapons trait is exactly this (up to 40% for two traits).
	mitigation += Clamp(in.autoTrait, 0, 2) * 20;
	// A braced position: prone beats crouched beats standing.
	if      (in.bodyHeight == HEIGHT_PRONE)  mitigation += 20;
	else if (in.bodyHeight == HEIGHT_CROUCH) mitigation += 10;
	if (in.bipod)    mitigation += 15;
	if (in.foregrip) mitigation += 15;
	return Clamp(mitigation, 0, 75);
}

int RecoilGain(AimDiscipline const discipline, int const weaponRecoil, int const mitigationPercent)
{
	int const shot = RecoilPerShot(discipline, weaponRecoil);
	int const left = shot * (100 - Clamp(mitigationPercent, 0, 100)) / 100;
	return std::max(1, left); // a shot always leaves the barrel a little further off
}

int RecoilAfterShot(int const current, int const gain)
{
	return Clamp(current + std::max(0, gain), 0, 100);
}

int RecoilDecay(int const current, int const steps)
{
	return Clamp(current - RECOIL_DECAY_PER_TURN * std::max(0, steps), 0, 100);
}

int RecoilPenalty(int const recoil)
{
	return Clamp(recoil, 0, 100);
}

bool SupportsAutofire(AimDiscipline const discipline)
{
	// Only the weapons built to lay down volume of fire may use it, which is
	// what makes the role meaningful (issue #102's decision).
	return discipline == AimDiscipline::Rifle || discipline == AimDiscipline::MachineGun;
}

const char* Describe(AimDiscipline const discipline)
{
	switch (discipline)
	{
		case AimDiscipline::Handgun:    return "handgun";
		case AimDiscipline::SMG:        return "SMG";
		case AimDiscipline::Shotgun:    return "shotgun";
		case AimDiscipline::Rifle:      return "rifle";
		case AimDiscipline::Marksman:   return "marksman";
		case AimDiscipline::MachineGun: return "machine gun";
		case AimDiscipline::Launcher:   return "launcher";
		case AimDiscipline::Other:      return "other";
	}
	return "other";
}

} // namespace Equipment
