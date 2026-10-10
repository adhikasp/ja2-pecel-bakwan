#include "CursorScenario.h"

#include "CursorAdapter.h"

namespace Automation
{

sol::table CursorState(sol::state& L)
{
	CursorFrame const& f = CurrentCursorFrame();
	CursorModel::State const& s = f.state;
	sol::table t = L.create_table();
	t["shown"]  = s.shown;
	t["mode"]   = std::string(CursorModel::ModeName(s.mode));
	t["shape"]  = std::string(CursorModel::ShapeName(s.shape));
	t["tone"]   = std::string(CursorModel::ToneName(s.tone));
	t["marker"] = std::string(CursorModel::MarkerName(s.marker));
	t["ap"]     = s.ap;
	t["apLeft"] = s.apLeft;
	t["hit"]    = s.hit;
	t["aim"]    = s.aim;
	t["why"]    = s.why;
	t["target"] = s.target;
	t["tile"]   = f.cursorTile == NOWHERE ? -1 : int(f.cursorTile);
	t["id"]     = f.uiCursor;
	t["chip"]   = s.chip;
	t["inWorld"] = f.inWorld;
	t["overHud"] = f.overHud;

	sol::table lines = L.create_table();
	int n = 1;
	for (CursorModel::ChipLine const& l : s.lines)
	{
		sol::table r = L.create_table();
		r["kind"] = l.kind;
		r["a"]    = l.a;
		r["b"]    = l.b;
		r["text"] = l.text;
		r["tone"] = std::string(CursorModel::ToneName(l.tone));
		lines[n++] = r;
	}
	t["lines"] = lines;

	if (f.plan.steps > 0)
	{
		sol::table p = L.create_table();
		p["steps"]  = f.plan.steps;
		p["solid"]  = f.plan.solid;
		p["total"]  = f.plan.total;
		p["now"]    = f.plan.now;
		p["next"]   = f.plan.next;
		p["beyond"] = f.plan.beyond;
		t["path"] = p;
	}
	if (f.marker)
	{
		sol::table m = L.create_table();
		m["x"] = f.markerAt.x;
		m["y"] = f.markerAt.y;
		t["markerAt"] = m;
	}
	if (f.dest)
	{
		sol::table d = L.create_table();
		d["x"] = f.destAt.x;
		d["y"] = f.destAt.y;
		d["attack"] = f.destAttack;
		t["destAt"] = d;
	}
	return t;
}

}
