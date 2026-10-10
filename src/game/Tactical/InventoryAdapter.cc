#include "InventoryAdapter.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "Handle_Items.h"
#include "Interface_Items.h"
#include "Isometric_Utils.h"
#include "ItemModel.h"
#include "Message.h"
#include "World_Items.h"
#include <string_theory/format>
#include "Interface_Panels.h"
#include "Items.h"
#include "Overhead.h"
#include "Points.h"
#include "Soldier_Control.h"

#include <memory>

// The adapter's shared state: the one core, the outcome of the last click and the Observables. The moves
// themselves are applied where the legacy statics are (Interface_Panels.cc, Interface_Items.cc).

Observable<InventoryMove const&> BeforeInventoryMove;
Observable<InventoryMove const&> OnInventoryMoved;

static InventoryOutcome g_lastOutcome;

static SOLDIERTYPE* MercById(int const id)
{
	return id >= 0 && id < TOTAL_SOLDIERS ? &GetMan(static_cast<UINT>(id)) : nullptr;
}

Equipment::InventoryCore& TacticalInventory()
{
	static Equipment::InventoryCore core = [] {
		Equipment::InvRelations r;
		r.canAttach = [](UINT16 const held, UINT16 const host) { return ValidAttachment(held, host); };
		r.canMerge  = [](UINT16 const held, UINT16 const target) { return ValidMerge(held, target) != FALSE; };
		r.fetish    = [](Equipment::InvParty const& m, int const slot, UINT16 const item) {
			SOLDIERTYPE const* const s = MercById(m.merc);
			return s && slot >= 0 && NailsVestBlocks(s, static_cast<UINT32>(slot), item);
		};
		r.fits = [](Equipment::InvParty const& m, int const slot, UINT16 const item) {
			SOLDIERTYPE* const s = MercById(m.merc);
			if (!s || slot < 0 || slot >= NUM_INV_SLOTS || !gpItemPointer || gpItemPointer->usItem != item) return true;
			return CanItemFitInPosition(s, gpItemPointer, static_cast<INT8>(slot), FALSE) != FALSE;
		};
		return Equipment::InventoryCore(std::move(r));
	}();
	return core;
}

Equipment::InvParty InventoryPartyOf(SOLDIERTYPE const* const s)
{
	Equipment::InvParty p;
	if (!s) return p;
	p.merc      = s->ubID;
	p.conscious = s->bLife >= CONSCIOUSNESS;
	// out of combat EnoughPoints always passes; in combat it is the 3 AP check
	p.canPay    = !p.conscious || EnoughPoints(s, Equipment::PASS_CHECK_AP, 0, FALSE);
	p.inReach   = gpSMCurrentMerc != s || !gfSMDisableForItems;
	return p;
}

void SyncInventoryHand()
{
	Equipment::HeldStack h;
	if (gpItemPointer && gpItemPointer->usItem != NOTHING)
	{
		h.item     = gpItemPointer->usItem;
		h.count    = gpItemPointer->ubNumberOfObjects;
		h.from     = InventoryPartyOf(gpItemPointerSoldier);
		h.fromSlot = gbItemPointerSrcSlot;
	}
	TacticalInventory().SetHand(h);
}

Equipment::HeldStack const& InventoryHand()
{
	// some legacy screens still write the pointer directly (the map, the shopkeeper): re-read it
	SyncInventoryHand();
	return TacticalInventory().Hand();
}

InventoryOutcome const& LastInventoryOutcome() { return g_lastOutcome; }
void RecordInventoryOutcome(InventoryOutcome const& o) { g_lastOutcome = o; }


// ---- one merc's own equipment ---------------------------------------------------------------------------------

namespace
{
	/** A one-object copy of the item in pocket @a from, removed from it: the unit of an attach move. */
	void SplitOne(SOLDIERTYPE& s, int const from, OBJECTTYPE* one)
	{
		OBJECTTYPE& src = s.inv[from];
		CreateItem(src.usItem, src.bStatus[0], one);
		one->ubNumberOfObjects = 1;
		RemoveObjs(&src, 1);
	}

	/** Where something that came out of a move goes when its natural place is gone: the preferred
	 *  slot when it still fits, else wherever it fits, else the ground, else back where it was. */
	void DisposeLeftover(SOLDIERTYPE& s, int const preferred, OBJECTTYPE& o)
	{
		if (o.usItem == NOTHING) return;
		if (preferred >= 0 && CanItemFitInPosition(&s, &o, INT8(preferred), FALSE))
		{
			s.inv[preferred] = o;
			o = OBJECTTYPE{};
			return;
		}
		if (AutoPlaceObject(&s, &o, FALSE))
		{
			o = OBJECTTYPE{};
			return;
		}
		if (s.sGridNo != NOWHERE)
		{
			ItemModel const* const m = GCM->getItem(o.usItem, ItemSystem::nothrow);
			AddItemToPool(s.sGridNo, &o, VISIBLE, s.bLevel, WORLD_ITEM_REACHABLE, 0);
			ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_UI_FEEDBACK,
				ST::format("{} goes on the ground (no room).", m ? m->getShortName() : ST::string()));
			o = OBJECTTYPE{};
			return;
		}
		if (preferred >= 0)
		{
			s.inv[preferred] = o;
			o = OBJECTTYPE{};
		}
	}

	void Raise(Observable<InventoryMove const&>& event, SOLDIERTYPE const& s, int const slot, UINT16 const item,
		char const* const action)
	{
		InventoryMove m;
		m.merc   = s.ubID;
		m.slot   = slot;
		m.item   = item;
		m.count  = 1;
		m.action = action;
		event(m);
	}

	bool Finish(bool const ok, SOLDIERTYPE const& s, int const slot, UINT16 const item, char const* const action)
	{
		InventoryOutcome o;
		o.ok     = ok;
		o.action = ok ? action : "refused";
		o.item   = item;
		RecordInventoryOutcome(o);
		if (ok) Raise(OnInventoryMoved, s, slot, item, action);
		return ok;
	}
}

bool InventoryMoveSlot(SOLDIERTYPE& s, int const from, int const to)
{
	UINT16 const item = s.inv[from].usItem;
	Raise(BeforeInventoryMove, s, to, item, "put");
	OBJECTTYPE carry = s.inv[from];
	DeleteObj(&s.inv[from]);
	if (!PlaceObject(&s, INT8(to), &carry))
	{
		s.inv[from] = carry;
		return Finish(false, s, to, item, "put");
	}
	DisposeLeftover(s, from, carry);
	return Finish(true, s, to, item, "put");
}

bool InventoryAttachFromSlot(SOLDIERTYPE& s, int const from, int const host)
{
	UINT16 const item = s.inv[from].usItem;
	Raise(BeforeInventoryMove, s, host, item, "attach");
	OBJECTTYPE carry;
	SplitOne(s, from, &carry);
	bool const ok = AttachObject(&s, &s.inv[host], &carry, 0) != FALSE;
	DisposeLeftover(s, from, carry);
	return Finish(ok, s, host, item, "attach");
}

bool InventoryDetach(SOLDIERTYPE& s, int const host, int const index, int const to)
{
	if (host < 0 || index < 0 || index >= MAX_ATTACHMENTS) return false;
	OBJECTTYPE const& gun = s.inv[host];
	if (gun.usAttachItem[index] == NOTHING) return false;
	UINT16 const item = gun.usAttachItem[index];
	Raise(BeforeInventoryMove, s, to, item, "detach");

	OBJECTTYPE carry;
	CreateItem(item, gun.bAttachStatus[index], &carry);
	if (!RemoveAttachment(&s.inv[host], INT8(index), &carry)) return Finish(false, s, to, item, "detach");
	if (!PlaceObject(&s, INT8(to), &carry))
	{
		AttachObject(&s, &s.inv[host], &carry, 0); // back where it was
		return Finish(false, s, to, item, "detach");
	}
	DisposeLeftover(s, -1, carry);
	return Finish(true, s, to, item, "detach");
}
