// The native tactical cursor (issue #318, docs/plan/native-tactical.md "CursorModel"): the tile marker, the move
// path as a line through the tile centres, the destination marker and the chip beside the pointer, drawn at output
// resolution over the world. What the pointer means is decided by Handle_UI; Tactical/CursorAdapter.cc turns it into
// a CursorFrame in world pixels and this file puts the frame on the screen:
//
//  - marker, path, destination: geometry of a small custom element (<cursorlayer>, drawn straight through the render
//    interface, so it does not depend on RmlUi transforms, which the software UI path does not draw);
//  - the chip: a box of the HUD document, rebuilt when its content changes;
//  - the pointer's shape: the overlay document's pointer (NativeUI.cc, SetCursorShape).
//
// World points go through WorldToOutput: world pixels -> UI (canvas) pixels by the layer layout -> output pixels.
#include "NativeUIRuntime.h"
#include "UiMesh.h"

#include "CursorAdapter.h"
#include "CursorModel.h"
#include "Handle_UI.h"
#include "UILayout.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>

namespace NativeUI
{
namespace
{
	struct Seg { Rml::Vector2f a, b; bool far = false; };

	// Everything the layer draws, in output pixels. Rebuilt every frame by TacticalCursorUpdate.
	struct LayerData
	{
		std::vector<Seg> path;
		std::vector<Rml::Vector2f> nodes;      // tile centres of the path, the near ones
		std::vector<Rml::Vector2f> farNodes;
		bool marker = false;
		Rml::Vector2f markerAt;
		CursorModel::Marker markerKind = CursorModel::Marker::None;
		bool dest = false, destAttack = false, markerWarn = false;
		Rml::Vector2f destAt;
		float diamondW = 40, diamondH = 20;   // a tile's diamond in output pixels
		float dp = 1;
		uint32_t revision = 0;
	};
	LayerData g_layer;

	Rml::ColourbPremultiplied Col(int r, int g, int b, int a)
	{
		return Rml::Colourb(Rml::byte(r), Rml::byte(g), Rml::byte(b), Rml::byte(a)).ToPremultiplied();
	}

	// the design tokens (assets/ui/tokens.rcss): brass, warn, danger
	constexpr int ACC[3] = { 0xD8, 0xB2, 0x5E };
	constexpr int WARN[3] = { 0xF0, 0xE4, 0x42 };
	constexpr int AMBER[3] = { 0xF0, 0xA8, 0x38 }; // the path beyond this turn's points
	constexpr int FOE[3] = { 0xFF, 0x6B, 0x3D };

	/** The layer element: draws g_layer. It fills its parent, takes no mouse. */
	class CursorLayer final : public Rml::Element
	{
	public:
		explicit CursorLayer(Rml::String const& tag) : Rml::Element(tag) {}

	protected:
		void OnRender() override
		{
			Rml::RenderManager* const rm = GetRenderManager();
			if (!rm) return;
			if (revision != g_layer.revision || !geometry)
			{
				revision = g_layer.revision;
				geometry = rm->MakeGeometry(Build());
			}
			if (geometry) geometry.Render(Rml::Vector2f(0, 0));
		}

	private:
		Rml::Geometry geometry;
		uint32_t revision = ~0u;

		static Rml::Mesh Build()
		{
			LayerData const& d = g_layer;
			MeshBuilder m;
			float const dp = d.dp;
			Rml::ColourbPremultiplied const shadow = Col(8, 5, 2, 150);
			Rml::ColourbPremultiplied const solid = Col(ACC[0], ACC[1], ACC[2], 255);
			Rml::ColourbPremultiplied const far = Col(AMBER[0], AMBER[1], AMBER[2], 235);

			// the path: solid for what this turn pays for, amber and dashed beyond; every tile centre a node
			float const w = std::max(3.f, 4.f * dp);
			for (Seg const& s : d.path)
			{
				if (s.far) m.Dashed(s.a, s.b, w + 2.f * dp, 7.f * dp, 5.f * dp, shadow);
				else m.Line(s.a, s.b, w + 2.f * dp, shadow);
			}
			for (Seg const& s : d.path)
			{
				if (s.far) m.Dashed(s.a, s.b, w, 7.f * dp, 5.f * dp, far);
				else m.Line(s.a, s.b, w, solid);
			}
			for (Rml::Vector2f const& p : d.nodes) { m.Disc(p, 5.f * dp, shadow); m.Disc(p, 3.6f * dp, solid); }
			for (Rml::Vector2f const& p : d.farNodes) { m.Disc(p, 5.f * dp, shadow); m.Disc(p, 3.6f * dp, far); }

			// where the merc goes to do it (the tile next to a door, a target): a smaller diamond
			if (d.dest)
			{
				int const* c = d.destAttack ? FOE : ACC;
				m.Diamond(d.destAt, d.diamondW * 0.62f, d.diamondH * 0.62f, std::max(1.5f, 2.f * dp), Col(c[0], c[1], c[2], 50), Col(c[0], c[1], c[2], 255));
			}

			// the tile under the pointer
			if (d.marker)
			{
				using K = CursorModel::Marker;
				float const lw = std::max(2.f, 2.5f * dp);
				Rml::Vector2f const c = d.markerAt;
				float const w2 = d.diamondW * 0.9f, h2 = d.diamondH * 0.9f;
				switch (d.markerKind)
				{
					case K::Confirm:
						m.Diamond(c, w2, h2, lw + 2.f * dp, Col(8, 5, 2, 0), shadow);
						m.Diamond(c, w2, h2, lw, Col(ACC[0], ACC[1], ACC[2], 110), solid);
						break;
					case K::Bad:
					{
						Rml::ColourbPremultiplied const red = Col(FOE[0], FOE[1], FOE[2], 255);
						m.Diamond(c, w2, h2, lw, Col(FOE[0], FOE[1], FOE[2], 60), red);
						float const k = h2 * 0.2f;
						m.Line(c + Rml::Vector2f{ -k, -k }, c + Rml::Vector2f{ k, k }, lw, red);
						m.Line(c + Rml::Vector2f{ -k, k }, c + Rml::Vector2f{ k, -k }, lw, red);
						break;
					}
					default:
					{
						// beyond this turn's points the marker is amber like the path
						int const* mc = d.markerWarn ? AMBER : ACC;
						m.Diamond(c, w2, h2, lw + 2.f * dp, Col(8, 5, 2, 0), shadow);
						m.Diamond(c, w2, h2, lw, Col(mc[0], mc[1], mc[2], 40), Col(mc[0], mc[1], mc[2], 240));
						break;
					}
				}
			}
			return std::move(m.mesh);
		}
	};

	bool g_registered = false;

	Rml::Vector2f WorldToOutput(CursorPoint const p) { return NativeUI::WorldToOutput(p.x, p.y); }

	std::string Fill(std::string fmt, std::string const& a, std::string const& b = {}, std::string const& c = {})
	{
		// "{}" placeholders, in order
		std::string out;
		int n = 0;
		for (size_t i = 0; i < fmt.size(); ++i)
		{
			if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}')
			{
				out += n == 0 ? a : n == 1 ? b : c;
				++n;
				++i;
			}
			else out += fmt[i];
		}
		return out;
	}

	std::string ToneClass(CursorModel::Tone t)
	{
		switch (t)
		{
			case CursorModel::Tone::Warn: return "warn";
			case CursorModel::Tone::No:   return "no";
			case CursorModel::Tone::Foe:  return "foe";
			default:                      return "ok";
		}
	}

	struct ChipState
	{
		std::string rml, cls;
		int x = -1, y = -1;
		bool shown = false;
	};
	ChipState g_chip, g_heldChip;
	HudChip g_hudChip;

	std::string ChipRml(CursorModel::State const& s)
	{
		using namespace CursorModel;
		std::string out;
		// the head: what a click does, to what, and where it lands
		std::string head = Str(std::string("tac.cur.") + ShapeName(s.shape));
		if (!s.target.empty() && s.shape == Shape::Burst)
			head = Fill(Str("tac.cur.burst_at"), s.target);
		else if (!s.target.empty() && s.shape == Shape::Fire)
			head = Fill(Str("tac.cur.fire_at"), s.target);
		else if (!s.target.empty() && s.shape == Shape::Punch)
			head = Fill(Str("tac.cur.punch_at"), s.target);
		else if (!s.target.empty() && s.shape == Shape::Blade)
			head = Fill(Str("tac.cur.stab_at"), s.target);
		bool headFromText = false;
		for (ChipLine const& l : s.lines)
		{
			if (l.kind == "where") { head += " · " + l.text; }
			// a held item: the game's own word for what the click does (Drop, Throw) names the action
			else if (l.kind == "text" && s.mode == Mode::Item && !headFromText) { head = l.text; headFromText = true; }
		}
		if (s.mode == Mode::Move || s.mode == Mode::MoveConfirm || s.mode == Mode::MoveAll)
		{
			if (s.mode == Mode::MoveAll) head += " · " + Str("tac.cur.all");
		}
		out += "<span class=\"h\">" + Escape(head) + "</span>";
		for (ChipLine const& l : s.lines)
		{
			std::string k, v, vc;
			if (l.kind == "hit") { k = Str("tac.cur.k_hit"); v = std::to_string(l.a) + " %"; }
			else if (l.kind == "aim")
			{
				k = Str("tac.cur.k_aim");
				for (int i = 0; i < l.b; ++i) v += i < l.a ? "●" : "○";
			}
			else if (l.kind == "ap")
			{
				k = Str("tac.cur.k_ap");
				v = l.b >= 0 ? Fill(Str("tac.cur.ap_left"), std::to_string(l.a), std::to_string(l.b)) : std::to_string(l.a);
				vc = l.tone == Tone::No ? "no" : "ok";
			}
			else if (l.kind == "ap_split")
			{
				k = Str("tac.cur.k_ap");
				v = Fill(Str("tac.cur.ap_split"), std::to_string(l.a + l.b), std::to_string(l.a), std::to_string(l.b));
				vc = "warn";
			}
			else if (l.kind == "range") { k = Str("tac.cur.k_range"); v = Fill(Str("tac.cur.range_tiles"), std::to_string(l.a)); }
			else if (l.kind == "text")
			{
				if (s.mode == Mode::Item && l.text == head.substr(0, l.text.size())) continue;
				k = Str("tac.cur.k_note");
				v = l.text;
			}
			else continue;
			out += "<div class=\"l\"><span class=\"k\">" + Escape(k) + "</span><span class=\"v " + vc + "\">" + Escape(v) + "</span></div>";
		}
		if (!s.why.empty() && s.tone != Tone::Ok && s.tone != Tone::Foe)
			out += "<span class=\"why\">" + Escape(Str("tac.cur.why." + s.why)) + "</span>";
		if (s.mode == Mode::Target && (s.shape == Shape::Fire || s.shape == Shape::Burst) && s.tone == Tone::Foe)
			out += "<span class=\"hint\">" + Escape(Str("tac.cur.hint_target")) + "</span>";
		return out;
	}
	/** Shows @a chip in @a el beside the pointer, on the side that has room; @a g is what was drawn last. */
	void PlaceChip(Rml::ElementDocument* const doc, Rml::Element* const el, ChipState const& chip, ChipState& g,
		Rml::Vector2f const mouse, float const dp)
	{
		Rml::Context* const ctx = Context();
		if (!el || !ctx) return;
		if (chip.shown != g.shown || chip.rml != g.rml || chip.cls != g.cls)
		{
			el->SetInnerRML(chip.rml);
			el->SetClass("shown", chip.shown);
			for (char const* c : { "ok", "warn", "no", "foe" }) el->SetClass(c, chip.shown && chip.cls == c);
			g.rml = chip.rml;
			g.cls = chip.cls;
			g.shown = chip.shown;
			Invalidate(2);
		}
		if (!chip.shown) return;
		float const out_w = float(ctx->GetDimensions().x), out_h = float(ctx->GetDimensions().y);
		float const cw = el->GetOffsetWidth() > 0 ? el->GetOffsetWidth() : 260.f * dp;
		float const ch = el->GetOffsetHeight() > 0 ? el->GetOffsetHeight() : 110.f * dp;
		// an item on the pointer rides at the right of it, 14 dp in: the chip starts after the picture
		float const carried = float(CursorItemWidth());
		float x = mouse.x + std::max(30.f * dp, 14.f * dp + carried + 10.f * dp), y = mouse.y + 10.f * dp;
		if (x + cw > out_w - 4.f * dp) x = mouse.x - 22.f * dp - cw;
		float floorY = out_h - 4.f * dp;
		// the world ends where the bar starts: a world chip that would run under it goes above the pointer
		if (el->GetId() == "tac.cursor.chip")
			if (Rml::Element* bar = doc->GetElementById("tac.bar")) floorY = std::min(floorY, bar->GetAbsoluteOffset(Rml::BoxArea::Border).y - 4.f * dp);
		if (y + ch > floorY) y = mouse.y - 10.f * dp - ch;
		// ... and keeps clear of the turn banner at the top
		if (Rml::Element* ban = doc->GetElementById("tac.turn"))
		{
			Rml::Vector2f const bo = ban->GetAbsoluteOffset(Rml::BoxArea::Border);
			float const bb = bo.y + ban->GetOffsetHeight() + 4.f * dp;
			if (ban->GetOffsetHeight() > 0 && x < bo.x + ban->GetOffsetWidth() && x + cw > bo.x && y < bb) y = bb;
		}
		x = std::floor(std::max(x, 4.f * dp));
		y = std::floor(std::max(y, 4.f * dp));
		if (int(x) != g.x || int(y) != g.y)
		{
			g.x = int(x);
			g.y = int(y);
			el->SetProperty(Rml::PropertyId::Left, Rml::Property(x, Rml::Unit::PX));
			el->SetProperty(Rml::PropertyId::Top, Rml::Property(y, Rml::Unit::PX));
			Invalidate(2);
		}
	}
}

void RegisterTacticalCursor()
{
	if (g_registered) return;
	g_registered = true;
	static Rml::ElementInstancerGeneric<CursorLayer> instancer;
	Rml::Factory::RegisterElementInstancer("cursorlayer", &instancer);
}

void SetHudChip(HudChip chip) { g_hudChip = std::move(chip); }

void TacticalCursorUpdate(Rml::ElementDocument* const doc)
{
	using namespace CursorModel;
	CursorFrame const& f = CurrentCursorFrame();
	Rml::Context* const ctx = Context();
	if (!doc || !ctx) return;

	// the pointer is over a part of the HUD: the world's cursor is not shown there
	bool const overHud = TacticalHudWantsMouse();
	bool const worldCursor = f.state.shown && !overHud;
	Rml::Vector2f const mouse = MousePosition();
	float const dp = DpScale();

	// ---- the layer
	LayerData next;
	next.dp = dp;
	if (worldCursor || (f.state.shown && f.state.mode == Mode::Busy))
	{
		// a tile of the world, in output pixels: the diamond is 40 x 20 world pixels
		Rml::Vector2f const o = WorldToOutput({ 0, 0 }), ex = WorldToOutput({ 40, 0 }), ey = WorldToOutput({ 0, 20 });
		next.diamondW = std::max(8.f, ex.x - o.x);
		next.diamondH = std::max(4.f, ey.y - o.y);
	}
	if (worldCursor)
	{
		if (f.path.size() >= 2)
		{
			for (size_t i = 1; i < f.path.size(); ++i)
			{
				Seg s;
				s.a = WorldToOutput(f.path[i - 1]);
				s.b = WorldToOutput(f.path[i]);
				// step i is paid for by this turn when it is within the plan's solid steps
				s.far = int(i) > f.plan.solid;
				next.path.push_back(s);
				// the destination has the marker: no node under it
				if (i + 1 < f.path.size()) (s.far ? next.farNodes : next.nodes).push_back(s.b);
			}
		}
		if (f.marker)
		{
			next.marker = true;
			next.markerAt = WorldToOutput(f.markerAt);
			next.markerKind = f.state.marker;
			next.markerWarn = f.state.tone == Tone::Warn;
		}
		if (f.dest)
		{
			next.dest = true;
			next.destAttack = f.destAttack;
			next.destAt = WorldToOutput(f.destAt);
		}
	}
	bool same = next.marker == g_layer.marker && next.markerWarn == g_layer.markerWarn && next.dest == g_layer.dest && next.destAttack == g_layer.destAttack &&
		next.markerKind == g_layer.markerKind && next.path.size() == g_layer.path.size() &&
		next.nodes.size() == g_layer.nodes.size() && next.farNodes.size() == g_layer.farNodes.size() &&
		next.dp == g_layer.dp && next.diamondW == g_layer.diamondW;
	if (same && next.marker) same = next.markerAt.x == g_layer.markerAt.x && next.markerAt.y == g_layer.markerAt.y;
	if (same && next.dest) same = next.destAt.x == g_layer.destAt.x && next.destAt.y == g_layer.destAt.y;
	if (same)
	{
		for (size_t i = 0; i < next.path.size() && same; ++i)
			same = next.path[i].a.x == g_layer.path[i].a.x && next.path[i].a.y == g_layer.path[i].a.y &&
				next.path[i].b.x == g_layer.path[i].b.x && next.path[i].b.y == g_layer.path[i].b.y && next.path[i].far == g_layer.path[i].far;
	}
	if (!same)
	{
		next.revision = g_layer.revision + 1;
		g_layer = std::move(next);
		Invalidate(2);
	}

	// ---- the pointer's shape
	if (worldCursor || (f.state.shown && f.state.mode == Mode::Busy))
	{
		bool const ring = f.state.mode == Mode::Target || f.state.mode == Mode::Melee || f.state.mode == Mode::Throw;
		std::string icon = f.state.shape == Shape::Pointer ? "" : ShapeIcon(f.state.shape);
		SetCursorShape(icon, ToneClass(f.state.tone), ring);
	}
	else
	{
		SetCursorShape({}, "ok", false);
	}

	// ---- the chips: the world's (under the HUD, over the tiles) and the held item's (over a pocket or a card, so
	// above every panel)
	ChipState world;
	if (worldCursor && f.state.chip && mouse.x >= 0 && !g_hudChip.shown)
	{
		world.shown = true;
		world.rml = ChipRml(f.state);
		world.cls = ToneClass(f.state.tone);
	}
	ChipState held;
	if (g_hudChip.shown && mouse.x >= 0)
	{
		held.shown = true;
		held.rml = "<span class=\"h\">" + Escape(g_hudChip.head) + "</span>";
		for (auto const& l : g_hudChip.lines)
			held.rml += "<div class=\"l\"><span class=\"k\">" + Escape(l.first) + "</span><span class=\"v " +
				(g_hudChip.tone == "no" ? "no" : "ok") + "\">" + Escape(l.second) + "</span></div>";
		if (!g_hudChip.why.empty()) held.rml += "<span class=\"why\">" + Escape(g_hudChip.why) + "</span>";
		held.cls = g_hudChip.tone;
	}
	PlaceChip(doc, doc->GetElementById("tac.cursor.chip"), world, g_chip, mouse, dp);
	PlaceChip(doc, doc->GetElementById("tac.drag.chip"), held, g_heldChip, mouse, dp);
}

}
