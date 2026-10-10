#include "PopupScenario.h"

#include "Interface_Dialogue.h"
#include "Interface_Items.h"
#include "Items.h"
#include "Keys.h"
#include "NPC.h"
#include "Overhead.h"
#include "PopupAdapter.h"
#include "ScenarioItems.h"
#include "Soldier_Control.h"
#include "Soldier_Profile.h"
#include "Soldier_Tile.h"
#include "Strategic_Exit_GUI.h"

#include <stdexcept>

// The Lua door onto the tactical popups: ja2.popup(), ja2.popupOp(), ja2.debug("talk"), ja2.debug("keys").

namespace Automation
{

using namespace Scenario;

namespace {

std::string Str(ST::string const& s) { return s.to_std_string(); }

sol::table EventTable(sol::state_view L, PopupEvent const& e)
{
	sol::table t = L.create_table();
	t["kind"]   = std::string(PopupKindName(e.kind));
	t["action"] = e.action;
	t["what"]   = e.what;
	t["id"]     = e.id;
	t["ok"]     = e.ok;
	t["why"]    = e.why;
	return t;
}

int ApproachByName(std::string const& n)
{
	for (int i = 0; i < int(PopupModels::Approach::Count); ++i) if (n == PopupModels::ApproachName(i)) return i;
	throw std::runtime_error("no approach \"" + n + "\"");
}

}

sol::table PopupState(sol::state& L)
{
	sol::table t = L.create_table();
	std::string kind = "none";
	sol::table open = L.create_table();

	MenuPopup const m = CurrentMenuPopup();
	if (m.open)
	{
		kind = m.door ? "door" : "action";
		open[kind] = true;
		sol::table menu = L.create_table();
		menu["door"] = m.door;
		menu["who"] = Str(m.who);
		menu["ap"] = m.apLeft;
		menu["doorGrid"] = m.doorGrid;
		menu["doorLock"] = m.doorLock;
		sol::table rows = L.create_table();
		int n = 1;
		for (PopupRow const& r : m.rows)
		{
			sol::table row = L.create_table();
			row["cmd"] = r.cmd;
			row["name"] = r.name;
			row["group"] = r.group;
			row["enabled"] = r.enabled;
			row["why"] = r.why;
			row["ap"] = r.ap;
			row["label"] = Str(r.label);
			rows[n++] = row;
		}
		menu["rows"] = rows;
		t["menu"] = menu;
	}

	PickupPopup const p = CurrentPickupPopup();
	if (p.open)
	{
		if (kind == "none") kind = "pickup";
		open["pickup"] = true;
		sol::table pk = L.create_table();
		pk["who"] = Str(p.who);
		pk["total"] = p.total;
		pk["page"] = p.page;
		pk["pages"] = p.pages;
		pk["selected"] = p.selected;
		pk["canUp"] = p.canUp;
		pk["canDown"] = p.canDown;
		pk["canTake"] = p.canTake;
		pk["all"] = p.all;
		sol::table rows = L.create_table();
		int n = 1;
		for (PickupRowView const& r : p.rows)
		{
			sol::table row = L.create_table();
			row["row"] = r.row;
			row["item"] = r.item;
			row["name"] = Str(r.name);
			row["count"] = Str(r.count);
			row["sel"] = r.sel;
			row["empty"] = r.empty;
			rows[n++] = row;
		}
		pk["rows"] = rows;
		t["pickup"] = pk;
	}

	StackPopup const s = CurrentStackPopup();
	if (s.open)
	{
		if (kind == "none") kind = "stack";
		open["stack"] = true;
		sol::table st = L.create_table();
		st["item"] = s.item;
		st["name"] = Str(s.name);
		st["slot"] = s.slot;
		st["slots"] = s.slots;
		st["count"] = s.count;
		st["take"] = s.take;
		st["holding"] = s.holding;
		sol::table boxes = L.create_table();
		int n = 1;
		for (StackBox const& b : s.boxes)
		{
			sol::table box = L.create_table();
			box["index"] = b.index;
			box["filled"] = b.filled;
			box["status"] = b.status;
			box["text"] = Str(b.text);
			boxes[n++] = box;
		}
		st["boxes"] = boxes;
		t["stack"] = st;
	}

	KeyRingPopup const k = CurrentKeyRingPopup();
	if (k.open)
	{
		if (kind == "none") kind = "keyring";
		open["keyring"] = true;
		sol::table kr = L.create_table();
		kr["who"] = Str(k.who);
		kr["door"] = k.door;
		kr["doorLocked"] = k.doorLocked;
		kr["holding"] = k.holding;
		sol::table keys = L.create_table();
		int n = 1;
		for (KeyView const& v : k.keys)
		{
			sol::table key = L.create_table();
			key["slot"] = v.slot;
			key["count"] = v.count;
			key["keyId"] = v.keyId;
			key["item"] = v.item;
			key["name"] = Str(v.name);
			key["sector"] = Str(v.sector);
			key["day"] = v.day;
			key["fits"] = v.fits;
			key["canUse"] = v.canUse;
			key["why"] = v.why;
			keys[n++] = key;
		}
		kr["keys"] = keys;
		t["keyring"] = kr;
	}

	TalkPopup const tk = CurrentTalkPopup();
	if (tk.open)
	{
		if (kind == "none") kind = "talk";
		open["talk"] = true;
		sol::table ta = L.create_table();
		ta["name"] = Str(tk.name);
		ta["profile"] = tk.profile;
		ta["line"] = Str(tk.line);
		ta["previous"] = Str(tk.previous);
		ta["asker"] = Str(tk.asker);
		ta["speaking"] = tk.speaking;
		sol::table rows = L.create_table();
		int n = 1;
		for (TalkRowView const& r : tk.rows)
		{
			sol::table row = L.create_table();
			row["approach"] = r.approach;
			row["name"] = r.name;
			row["label"] = Str(r.label);
			row["key"] = r.key;
			row["enabled"] = r.enabled;
			row["why"] = r.why;
			rows[n++] = row;
		}
		ta["rows"] = rows;
		t["talk"] = ta;
	}

	ExitPopup const e = CurrentExitPopup();
	if (e.open)
	{
		if (kind == "none") kind = "exit";
		open["exit"] = true;
		sol::table ex = L.create_table();
		ex["direction"] = Str(e.direction);
		ex["from"] = Str(e.from);
		ex["to"] = Str(e.to);
		ex["minutes"] = e.minutes;
		ex["shortTrip"] = e.shortTrip;
		ex["squadSize"] = e.squadSize;
		ex["single"] = e.singleSel;
		ex["all"] = e.allSel;
		ex["load"] = e.loadSel;
		ex["singleOff"] = e.singleOff;
		ex["allOff"] = e.allOff;
		ex["loadOff"] = e.loadOff;
		ex["singleWhy"] = e.singleWhy;
		ex["allWhy"] = e.allWhy;
		ex["loadWhy"] = e.loadWhy;
		ex["canGo"] = e.canGo;
		ex["jump"] = e.jump;
		t["exit"] = ex;
	}

	SpeechView const sp = CurrentSpeech();
	if (sp.shown)
	{
		open["speech"] = true;
		sol::table spt = L.create_table();
		spt["who"] = Str(sp.who);
		spt["line"] = Str(sp.line);
		spt["speaking"] = sp.speaking;
		spt["soldier"] = sp.soldier;
		t["speech"] = spt;
	}

	t["kind"] = kind;
	t["open"] = open;
	t["last"] = EventTable(L, LastPopupOutcome());
	return t;
}

sol::table PopupOp(sol::state& L, std::string const& op, sol::optional<sol::table> spec)
{
	sol::table const s = spec ? *spec : L.create_table();
	auto str = [&](char const* key) { return StrField(s, key, ""); };
	bool ok = false;
	std::string why;

	if (op == "menu")
	{
		// a row of the open menu, by name ("walk", "boot") or by number
		MenuPopup const m = CurrentMenuPopup();
		int cmd = -1;
		sol::object const o = s["cmd"];
		if (o.is<int>()) cmd = o.as<int>();
		else if (o.is<std::string>())
		{
			for (PopupRow const& r : m.rows) if (r.name == o.as<std::string>()) cmd = r.cmd;
			if (cmd < 0) throw std::runtime_error("ja2.popupOp(\"menu\"): the open menu has no \"" + o.as<std::string>() + "\"");
		}
		ok = PopupMenuChoose(cmd);
	}
	else if (op == "menu_cancel")
	{
		PopupMenuCancel();
		ok = true;
	}
	else if (op == "pickup")
	{
		std::string const a = str("action");
		if (a == "toggle")      ok = PickupToggle(IntField(s, "row", 0));
		else if (a == "all")    ok = PickupAll();
		else if (a == "scroll") ok = PickupScroll(IntField(s, "dir", 1));
		else if (a == "take")   ok = PickupTake();
		else if (a == "cancel") { PickupCancel(); ok = true; }
		else throw std::runtime_error("ja2.popupOp(\"pickup\"): unknown action \"" + a + "\"");
	}
	else if (op == "stack")
	{
		std::string const a = str("action");
		if (a == "click")         ok = StackClickBox(IntField(s, "i", 0));
		else if (a == "take")     ok = StackTakeSplit();
		else if (a == "all")      ok = StackTakeAll();
		else if (a == "more")     ok = StackSplitStep(1);
		else if (a == "less")     ok = StackSplitStep(-1);
		else if (a == "describe") ok = StackDescribe(IntField(s, "i", 0));
		else if (a == "close")    { StackClose(); ok = true; }
		else throw std::runtime_error("ja2.popupOp(\"stack\"): unknown action \"" + a + "\"");
	}
	else if (op == "keyring")
	{
		std::string const a = str("action");
		if (a == "use")           ok = KeyRingUse(IntField(s, "slot", 0));
		else if (a == "take")     ok = KeyRingTake(IntField(s, "slot", 0));
		else if (a == "describe") ok = KeyRingDescribe(IntField(s, "slot", 0));
		else if (a == "put")      ok = KeyRingPut();
		else if (a == "close")    { KeyRingClose(); ok = true; }
		else throw std::runtime_error("ja2.popupOp(\"keyring\"): unknown action \"" + a + "\"");
	}
	else if (op == "key_on_door")
	{
		ok = UseHeldKeyOnDoor(IntField(s, "grid", 0));
	}
	else if (op == "talk")
	{
		std::string const a = str("action");
		if (a == "choose")
		{
			sol::object const o = s["approach"];
			ok = TalkChoose(o.is<std::string>() ? ApproachByName(o.as<std::string>()) : IntField(s, "approach", 0));
		}
		else if (a == "who")  ok = TalkWho();
		else if (a == "done") ok = TalkDone();
		else if (a == "skip") ok = TalkSkip();
		else throw std::runtime_error("ja2.popupOp(\"talk\"): unknown action \"" + a + "\"");
	}
	else if (op == "exit")
	{
		std::string const a = str("action");
		if (a == "single")      ok = ExitChooseSingle();
		else if (a == "all")    ok = ExitChooseAll();
		else if (a == "load")   ok = ExitToggleLoad();
		else if (a == "go")     ok = ExitGo();
		else if (a == "cancel") { ExitCancel(); ok = true; }
		else throw std::runtime_error("ja2.popupOp(\"exit\"): unknown action \"" + a + "\"");
	}
	else if (op == "speech")
	{
		SpeechClick();
		ok = true;
	}
	else
	{
		throw std::runtime_error("ja2.popupOp: unknown op \"" + op + "\"");
	}

	PopupEvent const& last = LastPopupOutcome();
	sol::table r = L.create_table();
	r["ok"] = ok;
	r["why"] = ok ? std::string() : last.why;
	r["last"] = EventTable(L, last);
	return r;
}

void StartTalk(std::string const& npcName)
{
	SOLDIERTYPE* const sel = GetSelectedMan();
	if (!sel) throw std::runtime_error("ja2.debug(\"talk\"): no merc is selected");
	for (int p = 0; p < NUM_PROFILES; ++p)
	{
		MERCPROFILESTRUCT const& m = gMercProfiles[p];
		if (m.zNickname.to_std_string() != npcName && m.zName.to_std_string() != npcName) continue;
		SOLDIERTYPE* const npc = FindSoldierByProfileID(ProfileID(p));
		if (!npc || !npc->bInSector) continue;
		InitiateConversation(npc, sel, NPC_INITIAL_QUOTE);
		return;
	}
	throw std::runtime_error("ja2.debug(\"talk\"): no NPC named \"" + npcName + "\" is in the sector");
}

void GiveKeys(std::string const& merc, sol::table keys)
{
	SOLDIERTYPE* s = nullptr;
	if (merc.empty()) s = GetSelectedMan();
	else
	{
		FOR_EACH_IN_TEAM(m, OUR_TEAM) { if (m->name.to_std_string() == merc) { s = m; break; } }
	}
	if (!s) throw std::runtime_error("ja2.debug(\"keys\"): no such merc");
	for (size_t i = 1; i <= keys.size(); ++i)
	{
		sol::object const o = keys[i];
		if (!o.is<int>()) continue;
		OBJECTTYPE key{};
		CreateKeyObject(&key, 1, UINT8(o.as<int>()));
		if (!PutKeyOnRing(*s, key)) throw std::runtime_error("ja2.debug(\"keys\"): the ring is full");
	}
}

void TeleportMerc(std::string const& merc, int const grid)
{
	SOLDIERTYPE* s = nullptr;
	if (merc.empty()) s = GetSelectedMan();
	else
	{
		FOR_EACH_IN_TEAM(m, OUR_TEAM) { if (m->name.to_std_string() == merc) { s = m; break; } }
	}
	if (!s) throw std::runtime_error("ja2.debug(\"teleport\"): no such merc");
	if (!TeleportSoldier(*s, GridNo(grid), true)) throw std::runtime_error("ja2.debug(\"teleport\"): the tile cannot take him");
}

}
