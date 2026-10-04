#include "Slots.h"

#include "ArmourModel.h"
#include "GamePolicy.h"
#include "ItemModel.h"
#include "WeaponModels.h"
#include "Weapons.h"

namespace Equipment
{

namespace
{
	Platform MakePlatform(std::initializer_list<SlotDef> slots, const SlotPolicy& policy)
	{
		Platform p{};
		p.slotCount = 0;
		for (const SlotDef& slot : slots)
		{
			if (p.slotCount >= MAX_HOST_SLOTS) break;
			p.slots[p.slotCount++] = slot;
		}

		// The bonus knob may only grow or shrink a gun platform within the
		// schema's bounds: three to four attachment slots (five with the mag well).
		int count = p.slotCount;
		if (count > 0)
		{
			count += policy.slotBonus;
			if (count < 3) count = 3;
			if (count > MAX_HOST_SLOTS) count = MAX_HOST_SLOTS;
			p.slotCount = static_cast<uint8_t>(count);
		}
		return p;
	}
}

int Platform::IndexOf(SlotRole role) const
{
	for (int i = 0; i < slotCount; ++i)
	{
		if (slots[i].role == role) return i;
	}
	return -1;
}

Platform GunPlatform(uint8_t weaponClass, const SlotPolicy& policy)
{
	switch (weaponClass)
	{
		case HANDGUNCLASS:
		case SMGCLASS:
			return MakePlatform({
				{ SlotRole::Optic,    MountKind::Rail },
				{ SlotRole::Muzzle,   MountKind::MuzzleThread },
				{ SlotRole::SideRail, MountKind::SideRailMount },
			}, policy);

		case SHOTGUNCLASS:
			// The muzzle slot here is a choke thread: duckbills fit, rifle
			// suppressors do not. Underbarrel takes a bipod or a launcher.
			return MakePlatform({
				{ SlotRole::Muzzle,      MountKind::ChokeThread },
				{ SlotRole::Underbarrel, MountKind::UnderbarrelMount },
				{ SlotRole::SideRail,    MountKind::SideRailMount },
			}, policy);

		case RIFLECLASS:
		case MGCLASS:
			return MakePlatform({
				{ SlotRole::Optic,       MountKind::Rail },
				{ SlotRole::Muzzle,      MountKind::MuzzleThread },
				{ SlotRole::Underbarrel, MountKind::UnderbarrelMount },
				{ SlotRole::SideRail,    MountKind::SideRailMount },
			}, policy);

		default:
			// Melee and exotic weapons are not platforms.
			return MakePlatform({}, policy);
	}
}

Platform PlateCarrierPlatform(const SlotPolicy&)
{
	Platform p{};
	p.slotCount = 2;
	p.slots[0] = SlotDef{ SlotRole::Plate, MountKind::PlatePocket };
	p.slots[1] = SlotDef{ SlotRole::Plate, MountKind::PlatePocket };
	return p;
}

Platform HelmetPlatform(const SlotPolicy&)
{
	Platform p{};
	p.slotCount = 1;
	p.slots[0] = SlotDef{ SlotRole::Nvg, MountKind::NvgMount };
	return p;
}

Platform SlotsFor(const ItemModel& item, const SlotPolicy& policy)
{
	if (item.isGun() && item.asWeapon() != nullptr)
	{
		return GunPlatform(item.asWeapon()->ubWeaponClass, policy);
	}
	if (item.isArmour() && item.asArmour() != nullptr)
	{
		switch (item.asArmour()->getArmourClass())
		{
			case ARMOURCLASS_VEST:   return PlateCarrierPlatform(policy);
			case ARMOURCLASS_HELMET: return HelmetPlatform(policy);
			default: break;
		}
	}
	return MakePlatform({}, policy);
}

SlotPolicy TogglesFrom(const GamePolicy* policy)
{
	SlotPolicy toggles;
	if (policy == nullptr) return toggles;
	toggles.slotBonus    = policy->weapon_slot_bonus;
	toggles.strictMounts = policy->attachment_strict_mounts;
	return toggles;
}

const char* Describe(SlotRole role)
{
	switch (role)
	{
		case SlotRole::Optic:       return "optic";
		case SlotRole::Muzzle:      return "muzzle";
		case SlotRole::Underbarrel: return "underbarrel";
		case SlotRole::SideRail:    return "side rail";
		case SlotRole::Plate:       return "plate";
		case SlotRole::Nvg:         return "NVG mount";
	}
	return "slot";
}

const char* Describe(MountKind mount)
{
	switch (mount)
	{
		case MountKind::Rail:             return "rail";
		case MountKind::MuzzleThread:     return "muzzle thread";
		case MountKind::ChokeThread:      return "choke thread";
		case MountKind::UnderbarrelMount: return "underbarrel mount";
		case MountKind::SideRailMount:    return "side rail mount";
		case MountKind::PlatePocket:      return "plate pocket";
		case MountKind::NvgMount:         return "NVG mount";
	}
	return "mount";
}

const char* RoleKey(SlotRole role)
{
	switch (role)
	{
		case SlotRole::Optic:       return "optic";
		case SlotRole::Muzzle:      return "muzzle";
		case SlotRole::Underbarrel: return "underbarrel";
		case SlotRole::SideRail:    return "side_rail";
		case SlotRole::Plate:       return "plate";
		case SlotRole::Nvg:         return "nvg";
	}
	return "slot";
}

}
