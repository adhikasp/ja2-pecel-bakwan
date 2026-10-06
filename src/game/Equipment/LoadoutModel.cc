#include "LoadoutModel.h"

#include "InventorySlots.h"
#include "Item_Types.h"
#include "WeaponBallistics.h"
#include "Weapons.h"

#include <algorithm>

namespace Equipment
{

WeightBand WeightBandOf(int const percent)
{
	if (percent > 100) return WeightBand::Overloaded;
	if (percent >= 90)  return WeightBand::Heavy;
	if (percent >= 50)  return WeightBand::Loaded;
	return WeightBand::Light;
}

char const* WeightBandKey(WeightBand const band)
{
	switch (band)
	{
		case WeightBand::Loaded:     return "loaded";
		case WeightBand::Heavy:      return "heavy";
		case WeightBand::Overloaded: return "overloaded";
		default:                     return "light";
	}
}

int CarryCapacityGrams(int const effectiveStrength)
{
	if (effectiveStrength <= 0) return 0;
	int strength = effectiveStrength;
	if (strength > 80) strength += strength - 80;
	return strength * 500;
}

ST::string WeightText(int const grams, bool const metric)
{
	return metric ? ST::format("{.1f} kg", grams / 1000.0)
	              : ST::format("{.1f} lb", grams / 453.59237);
}

int const* AmmoTypeOrder(int& count)
{
	// ball, AP, super AP, hollow point: the same order the readout cycles its ammo in
	static int const order[] = { AMMO_REGULAR, AMMO_AP, AMMO_SUPER_AP, AMMO_HP };
	count = 4;
	return order;
}

int NextAmmoType(int const current, std::vector<int> const& carried)
{
	int count = 0;
	int const* const order = AmmoTypeOrder(count);
	auto const has = [&carried](int const type) {
		return std::find(carried.begin(), carried.end(), type) != carried.end();
	};
	int start = 0;
	for (int i = 0; i < count; ++i) if (order[i] == current) { start = i; break; }
	for (int step = 1; step <= count; ++step)
	{
		int const type = order[(start + step) % count];
		if (type != current && has(type)) return type;
	}
	return -1;
}

char const* AmmoTypeKey(int const gameAmmoIndex)
{
	switch (AmmoTypeFromGameIndex(gameAmmoIndex))
	{
		case AmmoType::Piercing:    return "ap";
		case AmmoType::HollowPoint: return "hp";
		case AmmoType::Subsonic:    return "sub";
		default:                    return "ball";
	}
}

std::string SlotKey(int const pos) { return "i" + std::to_string(pos); }
std::string AttachmentKey(int const index) { return "a" + std::to_string(index); }

DragKey ParseDragKey(std::string const& key)
{
	DragKey parsed;
	if (key.size() < 2 || key.size() > 6) return parsed;
	int index = 0;
	for (size_t i = 1; i < key.size(); ++i)
	{
		if (key[i] < '0' || key[i] > '9') return parsed;
		index = index * 10 + (key[i] - '0');
	}
	if (key[0] == 'i' && index < NUM_INV_SLOTS)
	{
		parsed.kind  = DragKind::Slot;
		parsed.index = index;
	}
	else if (key[0] == 'a' && index < MAX_ATTACHMENTS)
	{
		parsed.kind  = DragKind::Attachment;
		parsed.index = index;
	}
	return parsed;
}

}
