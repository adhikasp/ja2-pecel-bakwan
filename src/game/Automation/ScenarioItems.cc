#include "ScenarioItems.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "Item_Types.h"
#include "ItemModel.h"
#include "Items.h"
#include "Soldier_Control.h"

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

}
