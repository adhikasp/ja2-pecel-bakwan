#include "OverlayScenario.h"

#include "OverlayAdapter.h"

namespace Automation
{

sol::table OverlayState(sol::state& L)
{
	using namespace OverlayModel;
	Frame const& f = CurrentOverlayFrame();
	sol::table t = L.create_table();
	t["paused"] = f.paused;

	sol::table locs = L.create_table();
	int n = 1;
	for (Locator const& l : f.locators)
	{
		sol::table r = L.create_table();
		r["kind"] = std::string(KindName(l.kind));
		r["tone"] = std::string(ToneName(l.tone));
		r["frame"] = l.frame;
		r["gridNo"] = l.gridNo;
		r["x"] = l.at.x;
		r["y"] = l.at.y;
		locs[n++] = r;
	}
	t["locators"] = locs;

	sol::table bursts = L.create_table();
	n = 1;
	for (BurstMark const& b : f.bursts)
	{
		sol::table r = L.create_table();
		r["gridNo"] = b.gridNo;
		r["x"] = b.at.x;
		r["y"] = b.at.y;
		bursts[n++] = r;
	}
	t["bursts"] = bursts;

	sol::table arrows = L.create_table();
	n = 1;
	for (Arrow const& a : f.arrows)
	{
		sol::table r = L.create_table();
		r["dir"] = std::string(a.up ? "up" : "down");
		r["climb"] = a.climb;
		sol::table tones = L.create_table();
		int k = 1;
		for (ArrowTone const tone : a.tones) tones[k++] = std::string(ArrowToneName(tone));
		r["tones"] = tones;
		arrows[n++] = r;
	}
	t["arrows"] = arrows;

	if (f.band.valid)
	{
		sol::table b = L.create_table();
		b["l"] = f.band.l;
		b["t"] = f.band.t;
		b["r"] = f.band.r;
		b["b"] = f.band.b;
		t["band"] = b;
	}

	sol::table pools = L.create_table();
	n = 1;
	for (PoolBox const& p : f.pools)
	{
		sol::table r = L.create_table();
		r["pointer"] = p.atPointer;
		r["gridNo"] = p.gridNo;
		r["hidden"] = p.list.hidden;
		sol::table items = L.create_table();
		int k = 1;
		for (PoolRow const& row : p.list.rows)
		{
			sol::table it = L.create_table();
			it["item"] = row.item;
			it["name"] = row.name;
			it["count"] = row.count;
			items[k++] = it;
		}
		r["items"] = items;
		pools[n++] = r;
	}
	t["pools"] = pools;

	sol::table speech = L.create_table();
	n = 1;
	for (Speech const& sp : f.speech)
	{
		sol::table r = L.create_table();
		r["text"] = sp.text;
		r["x"] = sp.at.x;
		r["y"] = sp.at.y;
		speech[n++] = r;
	}
	t["speech"] = speech;
	return t;
}

}
