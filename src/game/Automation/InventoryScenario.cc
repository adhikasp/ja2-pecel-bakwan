#include "InventoryScenario.h"

#include "ContentManager.h"
#include "GameInstance.h"
#include "Interface.h"
#include "Interface_Items.h"
#include "Interface_Panels.h"
#include "InventoryAdapter.h"
#include "ItemModel.h"
#include "Items.h"
#include "Overhead.h"
#include "ScenarioItems.h"
#include "Soldier_Control.h"
#include "Squads.h"

#include <stdexcept>

// The Lua door onto the inventory core: ja2.inventory(), ja2.inventoryOp(), ja2.debug("hand", spec).

namespace Automation
{

using namespace Scenario;

namespace {

SOLDIERTYPE* FindMerc(std::string const& name)
{
	if (!name.empty())
	{
		FOR_EACH_IN_TEAM(s, OUR_TEAM)
		{
			if (s->name.to_std_string() == name) return s;
		}
		throw std::runtime_error("no merc named \"" + name + "\"");
	}
	if (gsCurInterfacePanel == SM_PANEL && gpSMCurrentMerc) return gpSMCurrentMerc;
	if (SOLDIERTYPE* const sel = GetSelectedMan()) return sel;
	throw std::runtime_error("no merc selected");
}

/** The single-merc panel shows @a s, as it does once a player opens his details: the pockets, the reach rules
 *  and the item pointer's cursor logic all read that panel's current merc. */
void ShowPanelFor(SOLDIERTYPE* const s)
{
	if (gsCurInterfacePanel != SM_PANEL) SetCurrentInterfacePanel(SM_PANEL);
	if (gpSMCurrentMerc != s) SetSMPanelCurrentMerc(s);
}

std::string MercName(int const id)
{
	if (id < 0 || id >= TOTAL_SOLDIERS) return std::string();
	return GetMan(static_cast<UINT>(id)).name.to_std_string();
}

sol::table OutcomeTable(sol::state_view L, InventoryOutcome const& o)
{
	sol::table t = L.create_table();
	t["ok"]     = o.ok;
	t["action"] = o.action;
	t["why"]    = o.why;
	t["item"]   = ItemName(o.item);
	t["apFrom"] = o.apFrom;
	t["apTo"]   = o.apTo;
	return t;
}

sol::table ObjectTable(sol::state_view L, OBJECTTYPE const& o)
{
	sol::table t = L.create_table();
	t["item"]   = ItemName(o.usItem);
	ItemModel const* const m = o.usItem != NOTHING ? GCM->getItem(o.usItem, ItemSystem::nothrow) : nullptr;
	t["name"]   = m ? m->getName().to_std_string() : std::string();
	t["count"]  = o.ubNumberOfObjects;
	t["status"] = o.bStatus[0];
	sol::table attach = L.create_table();
	int n = 1;
	for (int i = 0; i < MAX_ATTACHMENTS; ++i)
	{
		if (o.usAttachItem[i] != NOTHING) attach[n++] = ItemName(o.usAttachItem[i]);
	}
	t["attach"] = attach;
	return t;
}

bool Flag(sol::optional<sol::table> const& spec, char const* key)
{
	return spec && BoolField(*spec, key, false);
}

} // namespace

sol::table InventoryState(sol::state& L, std::string const& mercName)
{
	sol::table t = L.create_table();

	Equipment::HeldStack const& hand = InventoryHand();
	if (hand.item != NOTHING)
	{
		sol::table h = L.create_table();
		h["item"]     = ItemName(hand.item);
		ItemModel const* const m = GCM->getItem(hand.item, ItemSystem::nothrow);
		h["name"]     = m ? m->getName().to_std_string() : std::string();
		h["count"]    = hand.count;
		h["from"]     = MercName(hand.from.merc);
		h["fromSlot"] = hand.fromSlot;
		t["hand"]     = h;
	}

	Equipment::Question const& q = TacticalInventory().Pending();
	if (q.kind == Equipment::QuestionKind::Merge)               t["asking"] = "merge";
	else if (q.kind == Equipment::QuestionKind::PermanentAttachment) t["asking"] = "permanent";

	t["last"] = OutcomeTable(L, LastInventoryOutcome());

	SOLDIERTYPE* s = nullptr;
	try { s = FindMerc(mercName); }
	catch (std::runtime_error const&) { if (!mercName.empty()) throw; }
	if (s)
	{
		t["merc"] = s->name.to_std_string();
		t["ap"]   = s->bActionPoints;
		sol::table pockets = L.create_table();
		for (int i = 0; i < NUM_INV_SLOTS; ++i)
		{
			if (s->inv[i].usItem != NOTHING) pockets[i] = ObjectTable(L, s->inv[i]);
		}
		t["pockets"] = pockets;
	}

	sol::table sheet = L.create_table();
	sheet["open"] = InItemDescriptionBox() != FALSE;
	if (SOLDIERTYPE const* const d = NativeItemDescSoldier())
	{
		sheet["merc"] = d->name.to_std_string();
		InventoryMoneySplit const m = InventoryMoneyState();
		sol::table money = L.create_table();
		money["total"]     = m.total;
		money["remaining"] = m.remaining;
		money["removing"]  = m.removing;
		sheet["money"] = money;
	}
	t["sheet"] = sheet;
	return t;
}

sol::table InventoryOp(sol::state& L, std::string const& op, sol::optional<sol::table> spec)
{
	auto const text = [&](char const* key) { return spec ? StrField(*spec, key, "") : std::string(); };
	auto const num  = [&](char const* key, int fallback) { return spec ? IntField(*spec, key, fallback) : fallback; };

	if (op == "click")
	{
		SOLDIERTYPE* const s = FindMerc(text("merc"));
		// the pockets are the single-merc panel's: it shows the merc a click lands on, as a player's would
		ShowPanelFor(s);
		return OutcomeTable(L, InventorySlotClick(s, num("slot", -1), Flag(spec, "right"), Flag(spec, "ctrl")));
	}
	if (op == "answer")
	{
		return OutcomeTable(L, InventoryAnswer(spec ? BoolField(*spec, "yes", false) : false));
	}
	if (op == "attach")
	{
		InventoryAttachClick(num("index", -1), Flag(spec, "right"));
		return OutcomeTable(L, LastInventoryOutcome());
	}
	if (op == "unload")
	{
		InventoryUnload();
		return OutcomeTable(L, LastInventoryOutcome());
	}
	if (op == "money")
	{
		InventoryMoneyStep(num("which", -1), Flag(spec, "right"));
		return OutcomeTable(L, LastInventoryOutcome());
	}
	if (op == "close")
	{
		ItemDescNativeClose();
		return OutcomeTable(L, LastInventoryOutcome());
	}
	if (op == "move")
	{
		SOLDIERTYPE* const s = FindMerc(text("merc"));
		int const from = num("from", -1), to = num("to", -1);
		if (from < 0 || from >= NUM_INV_SLOTS || to < 0 || to >= NUM_INV_SLOTS)
			throw std::runtime_error("ja2.inventoryOp(\"move\"): from and to are pocket indexes");
		ShowPanelFor(s);
		InventoryMoveSlot(*s, from, to);
		return OutcomeTable(L, LastInventoryOutcome());
	}
	throw std::runtime_error("ja2.inventoryOp(\"" + op + "\"): unknown operation");
}

void StageHand(std::string const& mercName, std::string const& item, int const count)
{
	SOLDIERTYPE* const s = FindMerc(mercName);
	UINT16 const id = ItemByName(item);
	// a player holds an item with the details panel open
	ShowPanelFor(s);
	if (gpItemPointer) EndItemPointer();
	CreateItems(id, 100, static_cast<UINT8>(count > 0 ? count : 1), &gItemPointer);
	SetItemPointer(&gItemPointer, s);
	gbItemPointerSrcSlot = NO_SLOT;
}

} // namespace Automation
