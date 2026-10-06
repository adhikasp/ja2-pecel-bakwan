#include "StockScenario.h"

#include "ContentManager.h"
#include "Equipment/Stash.h"
#include "GameInstance.h"
#include "ItemModel.h"
#include "ScenarioItems.h"
#include "SectorStock.h"

#include <algorithm>
#include <stdexcept>

// The Lua door onto the sector stash's mass operations: ja2.stock(), ja2.stockOp() and
// ja2.debug("stock", spec). The rules are in Equipment/Stash.h, the game-side adapter in
// Strategic/SectorStock.cc; this only turns them into tables a script can read.

namespace Automation
{

using namespace Scenario;

namespace {

sol::table ReportTable(sol::state_view L, SectorStock::Report const& r)
{
	sol::table t = L.create_table();
	t["items"]       = r.items;
	t["rounds"]      = r.rounds;
	t["guns"]        = r.guns;
	t["magazines"]   = r.magazines;
	t["repaired"]    = r.repaired;
	t["pointsSpent"] = r.pointsSpent;
	t["pointsLeft"]  = r.pointsLeft;
	t["money"]       = r.money;
	t["note"]        = r.note;
	return t;
}

std::vector<std::string> StringList(sol::object const& o)
{
	std::vector<std::string> out;
	if (!o.is<sol::table>()) return out;
	sol::table t = o.as<sol::table>();
	for (size_t i = 0; i < t.size(); ++i)
	{
		sol::object const v = t.get<sol::object>(i + 1);
		if (v.is<std::string>()) out.push_back(v.as<std::string>());
	}
	return out;
}

std::vector<int> IntList(sol::object const& o)
{
	std::vector<int> out;
	if (!o.is<sol::table>()) return out;
	sol::table t = o.as<sol::table>();
	for (size_t i = 0; i < t.size(); ++i)
	{
		int v = 0;
		if (AsInt(t.get<sol::object>(i + 1), v)) out.push_back(v);
	}
	return out;
}

} // namespace

void StageStock(sol::table const& spec)
{
	std::vector<std::string> const items = StringList(spec["items"]);
	if (items.empty()) throw std::runtime_error("ja2.debug(\"stock\", spec): items are required");

	// resolve every name first, so a typo does not stage half a crate
	for (std::string const& name : items) ItemByName(name);

	SectorStock::StageItems(items, IntList(spec["count"]), IntList(spec["condition"]),
		IntList(spec["money"]), StrField(spec, "hold", ""));
}

sol::table StockState(sol::state& L)
{
	SectorStock::StashView const v = SectorStock::View();

	sol::table t = L.create_table();
	t["sector"]    = v.sector;
	t["sort"]      = Equipment::Describe(v.sort);
	t["money"]     = v.money;
	t["weight"]    = v.weight;
	t["pileCount"] = v.pileCount;
	t["itemCount"] = v.itemCount;
	t["marked"]    = v.marked;
	t["canOperate"] = SectorStock::CanOperate();
	t["report"]    = ReportTable(L, SectorStock::LastReport());

	// a Lua array is 1-based, so the piles come back in the order the panel shows them
	sol::table piles = L.create_table();
	int n = 1;
	for (SectorStock::PileView const& p : v.piles)
	{
		sol::table row = L.create_table();
		row["index"]     = p.index;
		row["item"]      = p.item;
		row["name"]      = p.name;      // the short name the panel shows
		row["internalName"] = ItemName(p.item);
		row["count"]     = p.count;
		row["condition"] = p.condition;
		row["rounds"]    = p.rounds;
		row["money"]     = p.money;
		row["marked"]    = p.marked;
		row["reachable"] = p.reachable;
		piles[n++] = row;
	}
	t["piles"] = piles;
	return t;
}

sol::table StockOp(sol::state& L, std::string const& op, std::string const& arg)
{
	SectorStock::Report r;

	if (op == "mark")
	{
		SectorStock::ToggleMark(std::atoi(arg.c_str()));
		r       = SectorStock::LastReport();
		r.note  = "stash.note.marked";
	}
	else if (op == "markall")
	{
		SectorStock::MarkAll(arg != "0");
		r      = SectorStock::LastReport();
		r.note = "stash.note.marked";
	}
	else if (op == "invert")
	{
		SectorStock::InvertMarks();
		r      = SectorStock::LastReport();
		r.note = "stash.note.marked";
	}
	else if (op == "sort")
	{
		Equipment::StashSort const key =
			arg == "name"      ? Equipment::StashSort::Name
			: arg == "condition" ? Equipment::StashSort::Condition
			: arg == "count"   ? Equipment::StashSort::Count
			: arg == "type" || arg.empty() ? Equipment::StashSort::Type
			: throw std::runtime_error("ja2.stockOp(\"sort\", \"" + arg + "\"): unknown sort");
		SectorStock::SortBy(key);
		r      = SectorStock::LastReport();
		r.note = "stash.note.sorted";
	}
	else if (op == "merge")  r = SectorStock::MergeStacks();
	else if (op == "load")   r = SectorStock::FillMagazines();
	else if (op == "clear")  { SectorStock::Clear(); r = SectorStock::LastReport(); r.note = "stash.note.cleared"; }
	else if (op == "repair") r = SectorStock::RepairStash();
	else if (op == "take")   r = SectorStock::TakeMarked();
	else if (op == "drop")   r = SectorStock::DropAll();
	else throw std::runtime_error("ja2.stockOp(\"" + op + "\"): unknown operation");

	return ReportTable(L, r);
}

} // namespace Automation