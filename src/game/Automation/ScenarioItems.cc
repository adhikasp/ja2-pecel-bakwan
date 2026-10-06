#include "ScenarioItems.h"

#include "Equipment/AttachmentRules.h"
#include "Equipment/EquipmentCatalog.h"
#include "Equipment/Lbe.h"
#include "Equipment/Slots.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "AmmoTypeModel.h"
#include "Item_Types.h"
#include "ItemModel.h"
#include "Items.h"
#include "MagazineModel.h"
#include "Soldier_Control.h"
#include "WeaponModels.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Automation::Scenario
{

bool AsInt(sol::object const& o, int& out)
{
	if (o.is<int>())       { out = o.as<int>();          return true; }
	if (o.is<double>())    { out = int(o.as<double>());  return true; }
	if (o.is<long long>()) { out = int(o.as<long long>()); return true; }
	return false;
}

int IntField(sol::table const& spec, char const* const key, int const fallback)
{
	sol::object const o = spec[key];
	int v;
	return AsInt(o, v) ? v : fallback;
}

bool BoolField(sol::table const& spec, char const* const key, bool const fallback)
{
	sol::object const o = spec[key];
	return o.is<bool>() ? o.as<bool>() : fallback;
}

std::string StrField(sol::table const& spec, char const* const key, std::string const& fallback)
{
	sol::object const o = spec[key];
	return o.is<std::string>() ? o.as<std::string>() : fallback;
}

std::string ArmourLevel(sol::object const& o, std::string const& fallback)
{
	if (o.is<bool>())        return o.as<bool>() ? "kevlar" : "none";
	if (o.is<std::string>()) return o.as<std::string>();
	return fallback;
}

UINT16 ItemByName(std::string const& name)
{
	ItemModel const* const item = GCM->getItemByName(ST::string(name));
	if (!item) throw std::runtime_error("unknown item \"" + name + "\"");
	return item->getItemIndex();
}

std::string ItemName(UINT16 const item)
{
	if (item == NOTHING) return std::string();
	ItemModel const* const model = GCM->getItem(item);
	return model ? model->getInternalName().to_std_string() : std::string();
}

void ClearSlot(SOLDIERTYPE& s, UINT8 const slot) { s.inv[slot] = OBJECTTYPE{}; }

void PutInSlot(SOLDIERTYPE& s, UINT8 const slot, UINT16 const item, UINT8 const count)
{
	ClearSlot(s, slot);
	OBJECTTYPE obj;
	CreateItems(item, 100, count, &obj);
	if (!PlaceObject(&s, slot, &obj))
	{
		// some kits refuse a slot; let the normal rules find room
		AutoPlaceObject(&s, &obj, TRUE);
	}
}

void GiveGun(SOLDIERTYPE& s, UINT16 const gun)
{
	PutInSlot(s, HANDPOS, gun, 1);
	OBJECTTYPE mag;
	CreateItems(DefaultMagazine(gun), 100, 2, &mag);
	AutoPlaceObject(&s, &mag, TRUE);
}

void GiveItem(SOLDIERTYPE& s, UINT16 const item)
{
	OBJECTTYPE obj;
	CreateItems(item, 100, 1, &obj);
	AutoPlaceObject(&s, &obj, TRUE);
}

void EquipArmour(SOLDIERTYPE& s, std::string const& level)
{
	UINT16 vest = NOTHING, helmet = NOTHING, legs = NOTHING;
	if (level == "spectra")
	{
		vest = ItemByName("SPECTRA_VEST"); helmet = ItemByName("SPECTRA_HELMET"); legs = ItemByName("SPECTRA_LEGGINGS");
	}
	else if (level != "none" && !level.empty())
	{
		vest = ItemByName("KEVLAR_VEST"); helmet = ItemByName("KEVLAR_HELMET"); legs = ItemByName("KEVLAR_LEGGINGS");
	}
	ClearSlot(s, VESTPOS); ClearSlot(s, HELMETPOS); ClearSlot(s, LEGPOS);
	if (vest   != NOTHING) PutInSlot(s, VESTPOS,   vest,   1);
	if (helmet != NOTHING) PutInSlot(s, HELMETPOS, helmet, 1);
	if (legs   != NOTHING) PutInSlot(s, LEGPOS,    legs,   1);
}

void ApplyStats(SOLDIERTYPE& s, sol::table const& t)
{
	auto seti = [&](char const* const key, INT8& field, int const lo, int const hi) {
		sol::object const o = t[key];
		int v;
		if (AsInt(o, v)) field = INT8(std::clamp(v, lo, hi));
	};
	seti("marksmanship", s.bMarksmanship, 1, 100);
	seti("agility",      s.bAgility,      1, 100);
	seti("dexterity",    s.bDexterity,    1, 100);
	seti("strength",     s.bStrength,     1, 100);
	seti("leadership",   s.bLeadership,   1, 100);
	seti("wisdom",       s.bWisdom,       1, 100);
	seti("medical",      s.bMedical,      0, 100);
	seti("mechanical",   s.bMechanical,   0, 100);
	seti("explosive",    s.bExplosive,    0, 100);
	seti("morale",       s.bMorale,       0, 100);
	seti("level",        s.bExpLevel,     1, 10);
	sol::object const healthObj = t["health"];
	int health;
	if (AsInt(healthObj, health))
	{
		health = std::clamp(health, 1, 100);
		s.bLifeMax = INT8(health);
		s.bLife    = INT8(health);
		s.bBleeding = 0;
	}
	s.bBreathMax = 100;
	s.bBreath    = 100;
}

// --- The equipment schema ---------------------------------------------------

namespace
{
	bool RoleFromKey(std::string const& key, Equipment::SlotRole& out)
	{
		static const std::pair<char const*, Equipment::SlotRole> ROLES[] = {
			{ "optic",       Equipment::SlotRole::Optic },
			{ "muzzle",      Equipment::SlotRole::Muzzle },
			{ "underbarrel", Equipment::SlotRole::Underbarrel },
			{ "side_rail",   Equipment::SlotRole::SideRail },
			{ "plate",       Equipment::SlotRole::Plate },
			{ "nvg",         Equipment::SlotRole::Nvg },
		};
		for (auto const& r : ROLES)
		{
			if (key == r.first) { out = r.second; return true; }
		}
		return false;
	}

	bool LbeSlotFromKey(std::string const& key, UINT8& out)
	{
		if (key == "vest") { out = LBE_VESTPOS; return true; }
		if (key == "belt") { out = LBE_BELTPOS; return true; }
		if (key == "pack") { out = LBE_PACKPOS; return true; }
		return false;
	}

	bool PocketFromKey(std::string const& key, UINT8& out)
	{
		if (key.rfind("POCK", 0) != 0) return false;
		int const n = std::atoi(key.c_str() + 4);
		if (n < 1 || n > 12) return false;
		out = static_cast<UINT8>(POCK1POS + n - 1);
		return true;
	}

	// Strict placement for fixtures: no fallback search, a refusal is a refusal.
	bool PutInSlotStrict(SOLDIERTYPE& s, UINT8 const slot, UINT16 const item, UINT8 const count)
	{
		ClearSlot(s, slot);
		OBJECTTYPE obj;
		CreateItems(item, 100, count, &obj);
		if (!PlaceObject(&s, slot, &obj)) return false;
		return obj.ubNumberOfObjects == 0;
	}
}

// The magazine a fixture asked for: either a magazine item's own name
// ("CLIP556_30_AP") or an ammo type's name ("AMMO_AP"), in which case a magazine of
// that type for the weapon's own calibre is found. NOTHING when there is nothing by
// that name, or the weapon has no magazine of that type.
UINT16 MagazineFor(std::string const& name, UINT16 const gun)
{
	// A magazine name resolves as an item; anything else is taken as an ammo type.
	if (ItemModel const* const item = GCM->items()->optionalByName(ST::string(name)))
	{
		return item->asAmmo() ? item->getItemIndex() : NOTHING;
	}

	WeaponModel const* const weapon = GCM->getWeapon(gun);
	if (!weapon || !weapon->calibre) return NOTHING;

	auto const* const ammoTypes = GCM->ammoTypes();
	auto const* const magazines = GCM->magazines();
	if (!ammoTypes || !magazines) return NOTHING;
	AmmoTypeModel const* const ammoType = ammoTypes->optionalByName(ST::string(name));
	if (!ammoType) return NOTHING;

	// Strictly a magazine of this ammo type for this calibre, preferring the weapon's
	// own magazine size. The game's own finder is lenient and substitutes another
	// magazine, which would quietly give a fixture the wrong ammunition; here a
	// weapon with no magazine for the type asked for is a refusal.
	UINT16 fallback = NOTHING;
	for (auto const& magazine : *magazines)
	{
		MagazineModel const& model = *magazine;
		if (model.calibre != weapon->calibre) continue;
		if (!model.ammoType || model.ammoType->index != ammoType->index) continue;
		if (model.dontUseAsDefaultMagazine) continue;
		if (model.capacity == weapon->ubMagSize) return model.getItemIndex();
		if (fallback == NOTHING) fallback = model.getItemIndex();
	}
	return fallback;
}

// An ammo type's internal name ("AMMO_AP"), or "" for an index the content manager
// does not know. The magazine a weapon is loaded with does not say what it holds --
// CLIP9_30 and CLIP9_30_AP differ only in the suffix a fixture guessed -- so the
// pipeline assertions read the type itself.
std::string AmmoTypeName(UINT8 const index)
{
	auto const* const ammoTypes = GCM->ammoTypes();
	if (!ammoTypes) return std::string();
	AmmoTypeModel const* const ammoType = ammoTypes->optionalById(index);
	return ammoType ? ammoType->getInternalName().to_std_string() : std::string();
}

void ApplyEquipment(SOLDIERTYPE& s, sol::table const& spec, std::vector<std::string>& problems)
{
	// The weapon's condition, so a fixture can hand over a neglected gun and watch
	// the pipeline deal with it (and the lane can assert the wear it caused).
	sol::object const conditionObj = spec["condition"];
	int condition;
	if (AsInt(conditionObj, condition) && s.inv[HANDPOS].usItem != NOTHING)
	{
		s.inv[HANDPOS].bStatus[0] = INT8(std::clamp(condition, 1, 100));
	}

	// The magazine in the gun, by its internal name, or by an ammo type when the
	// fixture names the type ("AMMO_AP") and lets us find the magazine for the
	// weapon's calibre and magazine size.
	sol::object const ammoObj = spec["ammo"];
	std::string ammoName = ammoObj.is<std::string>() ? ammoObj.as<std::string>() : "";
	if (ammoObj.is<sol::table>()) ammoName = StrField(ammoObj.as<sol::table>(), "item", "");
	if (!ammoName.empty())
	{
		if (s.inv[HANDPOS].usItem == NOTHING)
		{
			problems.push_back(ammoName + ": there is no weapon to load it into");
		}
		else
		{
			OBJECTTYPE& gun = s.inv[HANDPOS];
			UINT16 const magazine = MagazineFor(ammoName, gun.usItem);
			if (magazine == NOTHING)
			{
				problems.push_back(ammoName + ": no such magazine, or none for this weapon's calibre");
			}
			else
			{
				gun.usGunAmmoItem  = magazine;
				gun.ubGunAmmoType  = GCM->getItem(magazine)->asAmmo()->ammoType->index;
				gun.ubGunShotsLeft = nonNull(GCM->getItem(magazine)->asAmmo())->capacity;
				gun.bGunAmmoStatus = 100;
			}
		}
	}

	// Attachments, keyed by the slot role. They mount on the held gun - or on
	// the worn armour for the plate and NVG mounts.
	sol::object const attObj = spec["attachments"];
	if (attObj.is<sol::table>())
	{
		for (auto const& kv : attObj.as<sol::table>())
		{
			std::string const roleKey = kv.first.as<std::string>();
			std::string const itemName = kv.second.as<std::string>();
			Equipment::SlotRole role;
			if (!RoleFromKey(roleKey, role))
			{
				problems.push_back(roleKey + ": unknown slot role");
				continue;
			}
			UINT16 const attachItem = ItemByName(itemName);
			OBJECTTYPE* host =
				role == Equipment::SlotRole::Plate ? &s.inv[VESTPOS] :
				role == Equipment::SlotRole::Nvg   ? &s.inv[HELMETPOS] : &s.inv[HANDPOS];
			if (host->usItem == NOTHING)
			{
				problems.push_back(itemName + ": nothing to mount it on");
				continue;
			}
			Equipment::AttachmentDef const* const def = Equipment::AttachmentFor(attachItem);
			if (def == nullptr)
			{
				problems.push_back(itemName + ": does not mount on anything");
				continue;
			}
			Equipment::SlotPolicy const toggles = Equipment::TogglesFrom(GCM->getGamePolicy());
			Equipment::Platform const platform = Equipment::SlotsFor(*GCM->getItem(host->usItem), toggles);
			int const index = platform.IndexOf(role);
			if (index < 0)
			{
				problems.push_back(itemName + ": no " + roleKey + " slot on " + ItemName(host->usItem));
				continue;
			}
			uint16_t present[Equipment::MAX_HOST_SLOTS] = {};
			for (int i = 0; i < Equipment::MAX_HOST_SLOTS; ++i) present[i] = host->usAttachItem[i];
			Equipment::AttachResult const result = Equipment::CanAttachAt(platform, index, *def, present, true, toggles);
			if (!result.ok)
			{
				problems.push_back(itemName + ": " + Equipment::Describe(result.reason));
				continue;
			}
			host->usAttachItem[index]  = attachItem;
			host->bAttachStatus[index] = 100;
		}
	}

	// Worn load-bearing gear, keyed by "vest", "belt" and "pack".
	sol::object const lbeObj = spec["lbe"];
	if (lbeObj.is<sol::table>())
	{
		for (auto const& kv : lbeObj.as<sol::table>())
		{
			std::string const kindKey = kv.first.as<std::string>();
			std::string const itemName = kv.second.as<std::string>();
			UINT8 slot;
			if (!LbeSlotFromKey(kindKey, slot))
			{
				problems.push_back(kindKey + ": unknown LBE slot");
				continue;
			}
			UINT16 const item = ItemByName(itemName);
			Equipment::LbeDef const* const def = Equipment::LbeFor(item);
			if (def == nullptr)
			{
				problems.push_back(itemName + ": not load-bearing gear");
				continue;
			}
			if (!PutInSlotStrict(s, slot, item, 1))
			{
				problems.push_back(itemName + ": does not go in the " + kindKey + " slot");
			}
		}
	}

	// Pocket contents, keyed by "POCK1".."POCK12"; the worn LBE provides the
	// pocket and the pocket rules decide what fits.
	sol::object const pocketsObj = spec["pockets"];
	if (pocketsObj.is<sol::table>())
	{
		for (auto const& kv : pocketsObj.as<sol::table>())
		{
			std::string const slotKey = kv.first.as<std::string>();
			std::string itemName;
			int count = 1;
			if (kv.second.is<std::string>()) itemName = kv.second.as<std::string>();
			else if (kv.second.is<sol::table>())
			{
				sol::table const e = kv.second.as<sol::table>();
				itemName = StrField(e, "item", "");
				count = IntField(e, "count", 1);
			}
			UINT8 slot;
			if (!PocketFromKey(slotKey, slot))
			{
				problems.push_back(slotKey + ": unknown pocket");
				continue;
			}
			if (itemName.empty()) continue;
			UINT16 const item = ItemByName(itemName);
			if (!PutInSlotStrict(s, slot, item, UINT8(std::clamp(count, 1, 8))))
			{
				problems.push_back(itemName + ": does not fit " + slotKey);
			}
		}
	}
}

sol::table LoadoutTable(sol::state_view L, SOLDIERTYPE const& s)
{
	sol::table t = L.create_table();
	t["weapon"] = ItemName(s.inv[HANDPOS].usItem);

	// What the gun is loaded with and what condition it is in: the two inputs a
	// pipeline assertion reads back after a lane has been fired.
	t["ammo"]     = ItemName(s.inv[HANDPOS].usGunAmmoItem);
	t["ammoType"] = AmmoTypeName(s.inv[HANDPOS].ubGunAmmoType);
	t["rounds"]   = static_cast<int>(s.inv[HANDPOS].ubGunShotsLeft);
	t["condition"] = static_cast<int>(s.inv[HANDPOS].bGunStatus);

	// Attachments keyed by slot role: the gun's typed slots, and the plate/NVG
	// mounts of the worn armour.
	sol::table atts = L.create_table();
	for (UINT8 const hostSlot : { UINT8(HANDPOS), UINT8(VESTPOS), UINT8(HELMETPOS) })
	{
		if (s.inv[hostSlot].usItem == NOTHING) continue;
		Equipment::Platform const platform = Equipment::SlotsFor(*GCM->getItem(s.inv[hostSlot].usItem));
		for (int i = 0; i < platform.slotCount; ++i)
		{
			if (s.inv[hostSlot].usAttachItem[i] != NOTHING)
			{
				atts[Equipment::RoleKey(platform.slots[i].role)] = ItemName(s.inv[hostSlot].usAttachItem[i]);
			}
		}
	}
	t["attachments"] = atts;

	sol::table lbe = L.create_table();
	if (s.inv[LBE_VESTPOS].usItem != NOTHING) lbe["vest"] = ItemName(s.inv[LBE_VESTPOS].usItem);
	if (s.inv[LBE_BELTPOS].usItem != NOTHING) lbe["belt"] = ItemName(s.inv[LBE_BELTPOS].usItem);
	if (s.inv[LBE_PACKPOS].usItem != NOTHING) lbe["pack"] = ItemName(s.inv[LBE_PACKPOS].usItem);
	t["lbe"] = lbe;

	sol::table pockets = L.create_table();
	for (int n = 1; n <= 12; ++n)
	{
		UINT8 const slot = static_cast<UINT8>(POCK1POS + n - 1);
		if (s.inv[slot].usItem != NOTHING)
		{
			pockets["POCK" + std::to_string(n)] = ItemName(s.inv[slot].usItem);
		}
	}
	t["pockets"] = pockets;

	// The armour in the way, by where it is worn, with its condition: the third
	// input to the damage pipeline, read back the same way as the gun.
	sol::table armour = L.create_table();
	auto worn = [&](char const* const where, UINT8 const slot) {
		if (s.inv[slot].usItem == NOTHING) return;
		sol::table piece = L.create_table();
		piece["item"] = ItemName(s.inv[slot].usItem);
		piece["condition"] = static_cast<int>(s.inv[slot].bStatus[0]);
		armour[where] = piece;
	};
	worn("vest", VESTPOS);
	worn("helmet", HELMETPOS);
	worn("legs", LEGPOS);
	t["armour"] = armour;
	return t;
}

}
