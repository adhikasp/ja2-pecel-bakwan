#include "SectorStock.h"

#include "AmmoTypeModel.h"
#include "Assignments.h"
#include "Auto_Resolve.h"
#include "CalibreModel.h"
#include "ContentManager.h"
#include "GameInstance.h"
#include "Handle_Items.h"
#include "Interface_Panels.h"
#include "InventorySlots.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Item_Types.h"
#include "Items.h"
#include "MagazineModel.h"
#include "Map_Information.h"
#include "MapScreen.h"
#include "Map_Screen_Interface.h"
#include "Map_Screen_Interface_Map_Inventory.h"
#include "Overhead.h"
#include "Soldier_Control.h"
#include "StrategicMap.h"
#include "ScreenIDs.h"
#include "Tactical_Save.h"
#include "WeaponModels.h"
#include "Weapons.h"
#include "World_Items.h"

#include <algorithm>

// Equipment/Stash.h holds the rules with no game state in them. This file is the only place that
// knows a stash is a list of WORLDITEMs in a sector, that a live merc has to stand in the sector to
// use it, and that repairing costs his repair points.

namespace SectorStock
{

namespace {

// ---- the item facts -----------------------------------------------------------------------------
// The whole adapter's knowledge of the content: the rules never touch the item registry themselves.

Equipment::StashTraits TraitsOf(uint16_t const item)
{
	Equipment::StashTraits t;
	ItemModel const* const model = GCM->getItem(item, ItemSystem::nothrow);
	if (!model) return t;

	t.stackLimit = model->getPerPocket();
	t.itemClass  = model->getItemClass();
	t.name       = model->getShortName().to_std_string();
	t.money      = model->isMoney();

	// the base weight of one item; the stash rules only ever total whole items, never attachments
	t.weight = uint16_t(100 * model->getWeight());

	if (model->getFlags() & ITEM_REPAIRABLE)
	{
		t.repairable = true;
		t.repairEase = model->getRepairEase();
	}

	if (MagazineModel const* const mag = model->asAmmo())
	{
		t.magazine = true;
		t.capacity = mag->capacity;
		t.calibre  = mag->calibre ? mag->calibre->index : 0;
		t.ammoType = mag->ammoType ? mag->ammoType->index : 0;
	}

	if (WeaponModel const* const gun = model->asWeapon())
	{
		t.gun     = true;
		t.magSize = gun->ubMagSize;
		t.calibre = gun->calibre ? gun->calibre->index : 0;
	}

	return t;
}

Equipment::StashTraitsLookup MakeLookup()
{
	return TraitsOf;
}

// ---- one pile, two shapes ----------------------------------------------------------------------
// The core's StashPile and the game's WORLDITEM hold the same thing. The subtlety is the union in
// OBJECTTYPE: a gun's condition (bGunStatus) and a magazine's rounds (ubShotsLeft[0]) share offset
// 2, while a gun's rounds (ubGunShotsLeft) sit at offset 4. So a gun gets its own branch rather than
// a blind array copy, and a money pile its own too.

bool IsGun(uint16_t const item)
{
	ItemModel const* const model = GCM->getItem(item, ItemSystem::nothrow);
	return model && model->isGun();
}

bool IsMoney(uint16_t const item)
{
	ItemModel const* const model = GCM->getItem(item, ItemSystem::nothrow);
	return model && model->isMoney();
}

Equipment::StashPile ToPile(OBJECTTYPE const& o, INT16 const gridNo, UINT8 const level,
		INT8 const zHeight, UINT16 const flags)
{
	Equipment::StashPile p;
	p.itemId    = o.usItem;
	p.count     = o.ubNumberOfObjects;
	p.gridNo    = gridNo;
	p.level     = level;
	p.zHeight   = zHeight;
	p.flags     = flags;
	p.reachable = (flags & WORLD_ITEM_REACHABLE) != 0;
	if (!p.count) return p;

	if (IsGun(p.itemId))
	{
		p.status[0] = o.bGunStatus;
		p.rounds[0] = o.ubGunShotsLeft;
		p.ammoItem  = o.usGunAmmoItem;
	}
	else if (IsMoney(p.itemId))
	{
		p.status[0] = 100;
		p.money     = o.uiMoneyAmount;
	}
	else
	{
		for (uint8_t i = 0; i < p.count; ++i)
		{
			p.status[i] = o.bStatus[i];
			p.rounds[i] = o.ubShotsLeft[i];
		}
	}
	for (int i = 0; i < MAX_ATTACHMENTS; ++i)
	{
		p.attach[i]       = o.usAttachItem[i];
		p.attachStatus[i] = o.bAttachStatus[i];
	}
	return p;
}

OBJECTTYPE ObjectOf(Equipment::StashPile const& p)
{
	OBJECTTYPE o{};
	o.usItem            = p.itemId;
	o.ubNumberOfObjects = p.count;

	if (IsGun(p.itemId))
	{
		o.bGunStatus     = p.status[0];
		o.ubGunShotsLeft = p.rounds[0];
		o.usGunAmmoItem  = p.ammoItem;
	}
	else if (IsMoney(p.itemId))
	{
		o.bStatus[0]    = 100;
		o.uiMoneyAmount = p.money;
	}
	else
	{
		for (uint8_t i = 0; i < p.count && i < MAX_OBJECTS_PER_SLOT; ++i)
		{
			o.bStatus[i]    = p.status[i];
			o.ubShotsLeft[i] = p.rounds[i];
		}
	}
	for (int i = 0; i < MAX_ATTACHMENTS; ++i)
	{
		o.usAttachItem[i]  = p.attach[i];
		o.bAttachStatus[i] = p.attachStatus[i];
	}
	return o;
}

// ---- the stash, and who owns it ------------------------------------------------------------------

Equipment::StashTraitsLookup const& TheTraits()
{
	static Equipment::StashTraitsLookup const lookup = MakeLookup();
	return lookup;
}

SGPSector g_sector = NO_SECTOR;
Equipment::Stash g_stash;
bool g_loaded = false;             // the stash matches the sector's items
Equipment::StashSort g_sort = Equipment::StashSort::Type;
Report g_last;

std::vector<Equipment::StashPile> Load(SGPSector const& sector)
{
	std::vector<Equipment::StashPile> out;
	std::vector<WORLDITEM> const items =
		sector == gWorldSector ? gWorldItems : LoadWorldItemsFromTempItemFile(sector);
	for (WORLDITEM const& w : items)
	{
		if (!w.fExists) continue;
		if (!IsMapScreenWorldItemVisibleInMapInventory(w)) continue;
		out.push_back(ToPile(w.o, w.sGridNo, w.ubLevel, w.bRenderZHeightAboveLevel, w.usFlags));
	}
	return out;
}

void Save(SGPSector const& sector, Equipment::Stash const& stash)
{
	std::vector<WORLDITEM> out;
	auto keep = [&](std::vector<WORLDITEM> const& items) {
		for (WORLDITEM const& w : items)
		{
			if (w.fExists && IsMapScreenWorldItemVisibleInMapInventory(w)) continue;
			out.push_back(w);
		}
	};

	if (sector == gWorldSector)
	{
		keep(gWorldItems);
		for (Equipment::StashPile const& p : stash)
		{
			WORLDITEM w{};
			w.fExists                  = TRUE;
			w.sGridNo                  = p.gridNo;
			w.ubLevel                  = p.level;
			w.bRenderZHeightAboveLevel = p.zHeight;
			w.usFlags                  = p.flags;
			w.bVisible                 = VISIBLE;
			w.o                        = ObjectOf(p);
			out.push_back(w);
		}
		RefreshItemPools(out);
	}
	else
	{
		keep(LoadWorldItemsFromTempItemFile(sector));
		for (Equipment::StashPile const& p : stash)
		{
			WORLDITEM w{};
			w.fExists                  = TRUE;
			w.sGridNo                  = p.gridNo;
			w.ubLevel                  = p.level;
			w.bRenderZHeightAboveLevel = p.zHeight;
			w.usFlags                  = p.flags;
			w.bVisible                 = VISIBLE;
			w.o                        = ObjectOf(p);
			out.push_back(w);
		}
		SaveWorldItemsToTempItemFile(sector, out);
	}
}

/**
 * The stash of the selected sector.
 *
 * The panel owns the visible items while it is open, so the stash and the panel's pool list are the
 * same data in two shapes: reading pulls the panel's list in (a mark stays with the pile it was put
 * on, matched by position, because neither side reorders on its own), and writing pushes the stash
 * back out. With the panel closed the stash is loaded from the sector's items and saved back to
 * them, which is what lets a mass operation run - and be asserted - without the panel at all.
 */
Equipment::Stash& Stash()
{
	if (fShowMapInventoryPool)
	{
		Equipment::Stash pulled;
		pulled.reserve(pInventoryPoolList.size());
		for (size_t i = 0; i < pInventoryPoolList.size(); ++i)
		{
			WORLDITEM const& w = pInventoryPoolList[i];
			Equipment::StashPile p = ToPile(w.o, w.sGridNo, w.ubLevel, w.bRenderZHeightAboveLevel, w.usFlags);
			p.marked = i < g_stash.size() && g_stash[i].itemId == p.itemId && g_stash[i].marked;
			pulled.push_back(p);
		}
		g_stash  = std::move(pulled);
		g_sector = sSelMap;
		g_loaded = true;
		return g_stash;
	}

	if (!g_loaded || !(sSelMap == g_sector))
	{
		g_stash  = Load(sSelMap);
		g_sector = sSelMap;
		g_loaded = true;
	}
	return g_stash;
}

/** Write the stash to the open panel, which draws it. */
void Publish()
{
	if (!fShowMapInventoryPool) return;
	pInventoryPoolList.clear();
	for (Equipment::StashPile const& p : g_stash) // never Stash(): the panel is empty until this runs
	{
		WORLDITEM w{};
		w.fExists                  = TRUE;
		w.sGridNo                  = p.gridNo;
		w.ubLevel                  = p.level;
		w.bRenderZHeightAboveLevel = p.zHeight;
		w.usFlags                  = p.flags;
		w.bVisible                 = VISIBLE;
		w.o                        = ObjectOf(p);
		pInventoryPoolList.push_back(w);
	}
	SectorInventoryPoolResized();
}

/** Save the stash to the sector and republish it: the end of every operation. */
void Commit()
{
	Save(g_sector, g_stash); // g_stash, not Stash(): Publish() must not pull the panel back in
	Publish();
}

Report Remember(Equipment::StashReport const& r)
{
	g_last.items       = r.items;
	g_last.rounds      = r.rounds;
	g_last.guns        = r.guns;
	g_last.magazines   = r.magazines;
	g_last.repaired    = r.repaired;
	g_last.pointsSpent = r.pointsSpent;
	g_last.pointsLeft  = r.pointsLeft;
	g_last.money       = r.money;
	g_last.note        = r.note;
	return g_last;
}

SOLDIERTYPE* MercHere()
{
	SOLDIERTYPE* const s = GetSelectedInfoChar();
	if (!s) return nullptr;
	if (s->sSector.x != sSelMap.x || s->sSector.y != sSelMap.y
	 || s->sSector.z != iCurrentMapSectorZ || s->fBetweenSectors)
	{
		return nullptr;
	}
	return s;
}

/** Where a staged pile lands: under the squad in the sector, or the map's centre if nobody of ours
 * is standing there. */
INT16 StageGridNo()
{
	FOR_EACH_IN_TEAM(s, OUR_TEAM)
	{
		if (s->sSector.x != sSelMap.x || s->sSector.y != sSelMap.y) continue;
		if (s->sSector.z != iCurrentMapSectorZ || s->fBetweenSectors) continue;
		if (s->sGridNo != NOWHERE) return s->sGridNo;
	}
	return INT16(gMapInformation.sCenterGridNo);
}

} // namespace

// --- reading --------------------------------------------------------------------------------------

bool CanOperate()
{
	if (!MercHere()) return false;
	SGPSector battle;
	return !GetCurrentBattleSectorXYZAndReturnTRUEIfThereIsABattle(battle)
		|| sSelMap.x != battle.x || sSelMap.y != battle.y || iCurrentMapSectorZ != battle.z;
}

StashView View()
{
	StashView v;
	v.sort   = g_sort;
	v.sector = sSelMap.AsShortString().to_std_string();
	Equipment::Stash const& stash = Stash();
	for (size_t i = 0; i < stash.size(); ++i)
	{
		Equipment::StashPile const& p = stash[i];
		if (p.itemId == 0 || p.count == 0) continue;
		PileView row;
		row.index     = int(i);
		row.item      = p.itemId;
		row.count     = p.count;
		if (ItemModel const* const model = GCM->getItem(p.itemId, ItemSystem::nothrow))
			row.name = model->getShortName().to_std_string();
		row.condition = p.status[0];
		row.money     = p.money;
		row.gridNo    = uint16_t(p.gridNo);
		row.reachable = p.reachable;
		row.marked    = p.marked;
		row.rounds = -1;
		if (IsGun(p.itemId))
		{
			row.rounds = p.rounds[0];
		}
		else if (ItemModel const* const model = GCM->getItem(p.itemId, ItemSystem::nothrow))
		{
			if (model->asAmmo()) row.rounds = p.rounds[0];
		}
		++v.pileCount;
		v.itemCount += p.count;
		v.money     += p.money;
		if (p.marked) ++v.marked;
		v.piles.push_back(row);
	}
	v.weight = Equipment::StashWeight(stash, TheTraits());
	return v;
}

Equipment::StashSort Sort()  { return g_sort; }
Report                LastReport() { return g_last; }
const Equipment::StashTraitsLookup& Traits() { return TheTraits(); }

void SortBy(Equipment::StashSort const sort)
{
	g_sort = sort;
	Equipment::SortStash(Stash(), sort, TheTraits());
	Commit();
}

// --- selection ------------------------------------------------------------------------------------

void ToggleMark(int const index)
{
	Equipment::Stash& stash = Stash();
	if (index < 0 || size_t(index) >= stash.size()) return;
	Equipment::ToggleMark(stash, size_t(index));
	Publish();
}

void MarkAll(bool const on)
{
	Equipment::MarkReachable(Stash(), on);
	Publish();
}

void InvertMarks()
{
	Equipment::InvertMarks(Stash());
	Publish();
}

void Clear()
{
	Stash().clear();
	Commit();
}

void PanelOpened(SGPSector const& sector)
{
	g_stash = Load(sector);
	// The panel opens in the order the player last chose, which is also the order the stash is kept
	// in - one sort for both, so the two can never disagree.
	Equipment::SortStash(g_stash, g_sort, TheTraits());
	g_sector = sector;
	g_loaded = true;
	Publish();
}

void PanelClosed()
{
	g_loaded = false;
}

// --- the mass operations --------------------------------------------------------------------------

Report MergeStacks()
{
	Equipment::StashReport const r = Equipment::MergeStash(Stash(), TheTraits());
	Commit();
	return Remember(r);
}

Report FillMagazines()
{
	Equipment::StashReport const r = Equipment::FillMagazines(Stash(), TheTraits());
	Commit();
	return Remember(r);
}

Report RepairStash()
{
	SOLDIERTYPE* const s = MercHere();
	g_last.note = "stash.note.no_merc";
	if (!s) return g_last;

	// His repair rate for one pass over the stash, as the repair assignment computes it: without a
	// toolkit he cannot repair at all, which is what makes this a mechanic's loop and not free.
	UINT16 max_pts = 0;
	int const points = CalculateRepairPointsForRepairman(s, &max_pts, TRUE);
	Equipment::StashReport const r = Equipment::RepairStash(Stash(), TheTraits(), points);
	Commit();
	return Remember(r);
}

Report TakeMarked()
{
	SOLDIERTYPE* const s = MercHere();
	g_last.note = "stash.note.no_merc";
	if (!s) return g_last;

	// Selective pickup: the marked piles leave the sector, as many items of each as the merc's
	// pockets and his back take. Whatever he refuses stays where it lay.
	Equipment::StashReport r;
	r.note = "stash.note.taken";
	for (Equipment::StashPile& p : Stash())
	{
		if (!p.marked || p.itemId == 0 || p.count == 0) continue;

		while (p.count)
		{
			uint8_t const before = p.count;
			OBJECTTYPE o = ObjectOf(p);
			if (AutoPlaceObject(s, &o, FALSE))
			{
				p.count = 0;
			}
			else
			{
				// one item at a time: a pile of four clips may only fit as two of them
				o.ubNumberOfObjects = 1;
				o.bStatus[0]        = p.status[0];
				if (!AutoPlaceObject(s, &o, FALSE)) break;
				for (uint8_t k = 0; k + 1 < p.count; ++k)
				{
					p.status[k] = p.status[k + 1];
					p.rounds[k] = p.rounds[k + 1];
				}
				--p.count;
			}
			r.items += before - p.count;
		}
		// whatever he could not take stays in the sector, only its mark goes
		p.marked = false;
	}

	Commit();
	fTeamPanelDirty = TRUE;
	ReevaluateItemHatches(s, FALSE);
	return Remember(r);
}

Report DropAll()
{
	SOLDIERTYPE* const s = MercHere();
	g_last.note = "stash.note.no_merc";
	if (!s) return g_last;

	// Drop-all is the same mass move as a pickup, with everything he carries marked and the sector
	// as the target: like gear pools onto the piles already lying there.
	Equipment::Stash carried;
	for (int i = 0; i < NUM_INV_SLOTS; ++i)
	{
		OBJECTTYPE& o = s->inv[i];
		if (o.usItem == NOTHING || o.ubNumberOfObjects == 0) continue;
		Equipment::StashPile p = ToPile(o, INT16(NOWHERE), 0, 0, WORLD_ITEM_REACHABLE);
		p.reachable = true;
		p.marked    = true;
		carried.push_back(p);
		DeleteObj(&o);
	}

	Equipment::StashReport const r = Equipment::MoveMarked(carried, Stash(), TheTraits());
	Commit();
	fTeamPanelDirty = TRUE;
	ReevaluateItemHatches(s, FALSE);
	return Remember(r);
}

// --- staging ---------------------------------------------------------------------------------------

void StageItems(std::vector<std::string> const& items, std::vector<int> const& counts,
		std::vector<int> const& conditions, std::vector<int> const& money,
		std::string const& hold)
{
	if (!hold.empty())
	{
		// the selected merc takes it in hand; a mass repair needs his toolkit there
		if (SOLDIERTYPE* const s = MercHere())
		{
			if (ItemModel const* const model = GCM->getItemByName(ST::string(hold)))
			{
				CreateItem(model->getItemIndex(), 100, &s->inv[HANDPOS]);
				fTeamPanelDirty = TRUE;
			}
		}
	}
	for (size_t i = 0; i < items.size(); ++i)
	{
		ItemModel const* const model = GCM->getItemByName(ST::string(items[i]));
		if (!model) continue;
		Equipment::StashPile p;
		p.itemId    = model->getItemIndex();
		p.reachable = true;
		p.flags     = WORLD_ITEM_REACHABLE | WORLD_ITEM_GRIDNO_NOT_SET_USE_ENTRY_POINT;
		// A staged pile has no tile of its own. The squad's own tile is where gear a merc sets
		// down lands, and it is the one tile certain to be reachable and in the open.
		p.gridNo    = StageGridNo();
		int const condition = i < conditions.size() ? conditions[i] : 100;

		if (model->isMoney())
		{
			// money is one pile of a sum, not a count of items
			p.count     = 1;
			p.money     = uint32_t(std::max(0, money.empty() ? 0 : money[i < money.size() ? i : 0]));
			p.status[0] = 100;
			Stash().push_back(p);
			continue;
		}

		p.count = uint8_t(std::clamp(i < counts.size() ? counts[i] : 1, 1, MAX_OBJECTS_PER_SLOT));
		for (uint8_t k = 0; k < p.count; ++k)
		{
			p.status[k] = int8_t(std::clamp(condition, 1, 100));
			if (MagazineModel const* const mag = model->asAmmo()) p.rounds[k] = uint8_t(mag->capacity);
		}
		Stash().push_back(p);
	}
	Commit();
}

} // namespace SectorStock