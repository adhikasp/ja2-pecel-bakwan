#include "WeaponBallistics.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "ItemModel.h"
#include "Items.h"
#include "WeaponModels.h"
#include "Weapons.h"

#include <algorithm>

namespace Equipment
{

namespace
{
	int16_t Clamp16(int value, int low, int high)
	{
		return static_cast<int16_t>(std::max(low, std::min(high, value)));
	}

	/** Weapons.cc's WEAPON_STATUS_MOD: below 85 the status scales, at or above
	 *  it counts as new. */
	int StatusMod(int status)
	{
		return status >= 85 ? 100 : (status * 100) / 85;
	}

	/** An empty profile: a weapon with no damage, so `Readout::usable` is false.
	 *  Everything downstream treats it as "nothing to show". */
	WeaponProfile NoProfile()
	{
		WeaponProfile p;
		p.damage = 0;
		p.penetration = 0;
		p.range = 1;
		p.noise = 0;
		p.wear = 0;
		return p;
	}
}

WeaponProfile WeaponProfileFromStats(int const impact, int const rangeTiles, int const attackVolume, int const reliability)
{
	if (impact <= 0) return NoProfile();
	WeaponProfile p;
	p.damage      = Clamp16(impact, 1, 32767);
	// The interim penetration bridge: the pipeline's separate penetration stat
	// is what the compiled catalog (#100) fills in. Until then a round's
	// penetration is its impact, and the ammo type moves it between counters.
	p.penetration = p.damage;
	p.range       = Clamp16(rangeTiles, 1, 32767);
	p.noise       = Clamp16(attackVolume, 0, 32767);
	// A reliable weapon wears more slowly; a neglected one faster. `wear` is the
	// condition points one shot costs at full condition.
	p.wear        = Clamp16(4 - reliability, 1, 32767);
	return p;
}

WeaponProfile WeaponProfileFor(const OBJECTTYPE& gun)
{
	if (GCM == nullptr) return NoProfile();
	ItemModel const* const item = GCM->getItem(gun.usItem);
	if (item == nullptr || !item->isGun()) return NoProfile();
	WeaponModel const* const w = GCM->getWeapon(gun.usItem);
	if (w == nullptr) return NoProfile();

	WeaponProfile const p = WeaponProfileFromStats(
		w->ubImpact, int(GunRange(gun)) / 10, w->ubAttackVolume, item->getReliability());
	if (p.damage <= 0) return p;

	// A fitted suppressor lowers the muzzle noise exactly as the shot does
	// (Weapons.cc UseGun), so the readout and the fight agree.
	INT8 const sil = FindAttachment(&gun, SILENCER);
	if (sil != ITEM_NOT_FOUND)
	{
		WeaponProfile quiet = p;
		quiet.noise = Clamp16(SilencedVolume(w->ubAttackVolume, gun.bAttachStatus[sil]), 0, 32767);
		return quiet;
	}
	return p;
}

AmmoType AmmoTypeFromGameIndex(int const gameAmmoIndex)
{
	switch (gameAmmoIndex)
	{
		case AMMO_HP:       return AmmoType::HollowPoint;
		case AMMO_AP:
		case AMMO_SUPER_AP: return AmmoType::Piercing;
		default:            return AmmoType::Ball; // regular, and what the pipeline does not model
	}
}

int SilencedVolume(int const attackVolume, int const suppressorCondition)
{
	if (attackVolume <= 1) return attackVolume;
	int const mod = StatusMod(std::clamp(suppressorCondition, 0, 100));
	// The legacy formula (Weapons.cc UseGun) divides by (100 / (volume - 1)) and a
	// very worn suppressor can make that arithmetic raise the volume; a readout
	// must not show a suppressor making a gun louder, so it never exceeds the bare
	// weapon's noise.
	int const quiet = 1 + ((100 - mod) / (100 / (attackVolume - 1)));
	return std::min(attackVolume, quiet);
}

Better BetterValue(int const a, int const b, bool const higherIsBetter)
{
	if (a == b) return Better::Even;
	bool const aBetter = higherIsBetter ? a > b : a < b;
	return aBetter ? Better::A : Better::B;
}

Readout ComputeReadout(WeaponProfile const& weapon, AmmoProfile const& ammo,
	ArmourProfile const& armour, int const weaponCondition, int const aimChance,
	const std::vector<int>& distances, PipelineToggles const& toggles)
{
	Readout r;
	r.usable         = weapon.damage > 0;
	r.ammo           = ammo.type;
	r.armour         = armour.tier;
	r.condition      = std::clamp(weaponCondition, 0, 100);
	r.muzzleNoise    = weapon.noise;
	r.effectiveRange = std::max(1, weapon.range * ammo.rangePercent / 100);

	r.bands.reserve(distances.size());
	for (int const distance : distances)
	{
		ShotInput in;
		in.weapon          = weapon;
		in.ammo            = ammo;
		in.armour          = armour;
		in.distance        = Clamp16(distance, 0, 32767);
		in.weaponCondition = r.condition;
		in.armourCondition = 100; // a fresh target: the readout compares weapons, not wear
		in.aimChance       = Clamp16(aimChance, 1, 99);
		in.roll            = 0;   // the readout is the aimed case: what a connecting round does

		ShotResult const s = ResolveShot(in, toggles);
		ReadoutBand b;
		b.distance    = distance;
		b.chanceToHit = s.chanceToHit;
		b.impact      = s.impact;
		b.damage      = s.damage;
		b.penetration = s.penetration;
		b.threshold   = s.threshold;
		b.residual    = s.residual;
		b.noise       = s.noise;
		b.wear        = s.wear;
		b.outcome     = s.outcome;
		b.inRange     = distance <= r.effectiveRange;
		r.bands.push_back(b);
	}
	return r;
}

Readout ComputeReadout(WeaponProfile const& weapon, AmmoProfile const& ammo,
	ArmourProfile const& armour, int const weaponCondition, int const aimChance,
	PipelineToggles const& toggles)
{
	std::vector<int> distances(READOUT_BANDS, READOUT_BANDS + NUM_READOUT_BANDS);
	return ComputeReadout(weapon, ammo, armour, weaponCondition, aimChance, distances, toggles);
}

Comparison CompareReadouts(Readout const& a, Readout const& b)
{
	Comparison c;
	c.a = a;
	c.b = b;
	size_t const n = std::min(a.bands.size(), b.bands.size());
	c.bands.reserve(n);
	for (size_t i = 0; i < n; ++i)
	{
		ReadoutBand const& ba = a.bands[i];
		ReadoutBand const& bb = b.bands[i];
		BandCompare bc;
		bc.distance    = ba.distance;
		bc.hit         = BetterValue(ba.chanceToHit, bb.chanceToHit, true);
		bc.damage      = BetterValue(ba.damage, bb.damage, true);
		bc.penetration = BetterValue(ba.penetration, bb.penetration, true);
		bc.noise       = BetterValue(ba.noise, bb.noise, false); // quieter is better
		c.bands.push_back(bc);
	}
	return c;
}

} // namespace Equipment
