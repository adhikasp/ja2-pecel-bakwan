// The native world overlays (issue #322, docs/plan/native-tactical.md "OverlayModel"): the locator rings, the burst
// impacts, the up/down arrows and the rubber band as geometry of a custom element (<overlaylayer>, drawn straight
// through the render interface like the cursor's path), the lists of items under the cursor or beside a new item as
// HUD markup, and the pause banner. What exists is decided by the legacy code (Tactical/OverlayAdapter.cc builds
// the frame); this file puts the frame on the screen. World points go through WorldToOutput.
#include "NativeUIRuntime.h"

#include "NativeImages.h"
#include "OverlayAdapter.h"
#include "OverlayModel.h"
#include "Timer_Control.h"
#include "UiMesh.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>

namespace NativeUI
{
namespace
{
	using namespace OverlayModel;

	// the design tokens (assets/ui/tokens.rcss)
	constexpr int ACC[3] = { 0xD8, 0xB2, 0x5E };   // brass: our mercs
	constexpr int FOE[3] = { 0xFF, 0x6B, 0x3D };   // danger: hostile
	constexpr int INFO[3] = { 0x56, 0xB4, 0xE9 };  // info: a place or an item
	constexpr int WARN[3] = { 0xF0, 0xE4, 0x42 };
	constexpr int OK[3] = { 0x2B, 0xD4, 0xA0 };

	struct Ring { Rml::Vector2f at; float r; float alpha; int const* col; };
	struct Chev { Rml::Vector2f at; bool up; int const* col; };

	// Everything the layer draws, in output pixels. Rebuilt every frame the frame changes.
	struct LayerData
	{
		std::vector<Ring> rings;
		std::vector<Rml::Vector2f> bursts;
		std::vector<Chev> chevrons;
		bool band = false;
		float bl = 0, bt = 0, br = 0, bb = 0, glow = 0;
		float dp = 1;
		uint32_t revision = 0;
	};
	LayerData g_layer;

	int const* ToneColour(Tone t)
	{
		switch (t)
		{
			case Tone::Friend: return ACC;
			case Tone::Foe:    return FOE;
			default:           return INFO;
		}
	}

	/** The layer element: draws g_layer. It fills its parent, takes no mouse. */
	class OverlayLayer final : public Rml::Element
	{
	public:
		explicit OverlayLayer(Rml::String const& tag) : Rml::Element(tag) {}

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
			auto const shadow = MeshCol(8, 5, 2, 150);

			// the band: a wash, then a glowing border
			if (d.band)
			{
				float const l = d.bl, t = d.bt, r = d.br, b = d.bb;
				m.Quad({ l, t }, { r, t }, { r, b }, { l, b }, MeshCol(ACC[0], ACC[1], ACC[2], 28));
				float const lw = std::max(2.f, 2.f * dp);
				int const k = int(std::lround(150 + 105 * d.glow));
				auto const edge = MeshCol(ACC[0] * k / 255, ACC[1] * k / 255, ACC[2] * k / 255, 255);
				m.Line({ l, t }, { r, t }, lw, edge);
				m.Line({ r, t }, { r, b }, lw, edge);
				m.Line({ r, b }, { l, b }, lw, edge);
				m.Line({ l, b }, { l, t }, lw, edge);
				float const j = lw * 0.5f;
				for (Rml::Vector2f const c : { Rml::Vector2f{ l, t }, Rml::Vector2f{ r, t }, Rml::Vector2f{ r, b }, Rml::Vector2f{ l, b } })
					m.Disc(c, j, edge);
			}

			// the locator rings: two, the second a pulse behind the first
			for (Ring const& g : d.rings)
			{
				int const a = int(std::lround(255 * g.alpha));
				float const w = std::max(3.f, 3.5f * dp);
				// a wash inside, so the ring reads over any ground
				MeshRing(m, g.at, 0.f, g.r - w, MeshCol(g.col[0], g.col[1], g.col[2], a * 40 / 255));
				MeshRing(m, g.at, g.r - w - dp, g.r + dp, MeshCol(8, 5, 2, a * 140 / 255));
				MeshRing(m, g.at, g.r - w, g.r, MeshCol(g.col[0], g.col[1], g.col[2], a));
				// a steady inner ring, so a locator at its faintest frame is still findable
				MeshRing(m, g.at, 8.f * dp - dp, 8.f * dp + dp, MeshCol(g.col[0], g.col[1], g.col[2], 110));
			}

			// the burst impacts: a cross on the tile
			for (Rml::Vector2f const& p : d.bursts)
			{
				float const k = 9.f * dp, w = std::max(3.f, 3.5f * dp);
				m.Line(p + Rml::Vector2f{ -k, -k }, p + Rml::Vector2f{ k, k }, w + 2.f * dp, shadow);
				m.Line(p + Rml::Vector2f{ -k, k }, p + Rml::Vector2f{ k, -k }, w + 2.f * dp, shadow);
				m.Line(p + Rml::Vector2f{ -k, -k }, p + Rml::Vector2f{ k, k }, w, MeshCol(FOE[0], FOE[1], FOE[2], 255));
				m.Line(p + Rml::Vector2f{ -k, k }, p + Rml::Vector2f{ k, -k }, w, MeshCol(FOE[0], FOE[1], FOE[2], 255));
			}

			// the arrows: a chevron per step
			for (Chev const& c : d.chevrons)
			{
				float const hw = 16.f * dp, hh = 10.f * dp, w = std::max(3.f, 4.f * dp);
				float const s = c.up ? -1.f : 1.f;
				Rml::Vector2f const l{ c.at.x - hw, c.at.y - s * hh * 0.5f }, tip{ c.at.x, c.at.y + s * hh * 0.5f },
					r{ c.at.x + hw, c.at.y - s * hh * 0.5f };
				m.Line(l, tip, w + 2.f * dp, shadow);
				m.Line(tip, r, w + 2.f * dp, shadow);
				m.Disc(tip, (w + 2.f * dp) * 0.5f, shadow);
				auto const col = MeshCol(c.col[0], c.col[1], c.col[2], 255);
				m.Line(l, tip, w, col);
				m.Line(tip, r, w, col);
				m.Disc(tip, w * 0.5f, col);
			}
			return std::move(m.mesh);
		}
	};

	bool g_registered = false;

	struct Html
	{
		std::string rml;
		bool paused = false;
	};
	Html g_html;

	int const* ArrowColour(ArrowTone t)
	{
		switch (t)
		{
			case ArrowTone::Yellow: return WARN;
			case ArrowTone::Green:  return OK;
			default:                return ACC;
		}
	}

	/** A picture no bigger than maxW x maxH dp: the largest integer scale that fits (at least 1x). */
	struct Pic { std::string src; int w = 0, h = 0; };
	Pic FitPic(std::string const& name, float const base, float const maxW, float const maxH)
	{
		auto const [w, h] = PictureBaseSize(name);
		if (!w) return {};
		float const dp = std::max(0.01f, DpScale());
		int k = std::max(1, int(std::floor(base * dp + 0.001f)));
		while (k > 1 && (w * k > maxW * dp || h * k > maxH * dp)) --k;
		return { name + "@" + std::to_string(k), w * k, h * k };
	}

	// measures of a list, in dp (the rules of .ovl-pool in tactical.rcss)
	constexpr float POOL_W = 250.f, ROW_H = 30.f, MORE_H = 20.f, POOL_PAD = 4.f, POOL_TOP = 3.f;

	std::string Num(int v) { return std::to_string(v); }

	// the lowest a bubble's bottom edge may sit: above the bar
	float floorBottom(float const outH, float const floorY) { return std::max(0.f, outH - floorY); }

	std::string Fill(std::string fmt, std::string const& a)
	{
		size_t const i = fmt.find("{}");
		if (i != std::string::npos) fmt.replace(i, 2, a);
		return fmt;
	}

	std::string PoolRml(PoolList const& l, Rect const r)
	{
		std::string out = "<div class=\"ovl-pool\" style=\"left: " + Num(int(r.x)) + "px; top: " + Num(int(r.y)) + "px;\">";
		for (PoolRow const& row : l.rows)
		{
			out += "<div class=\"r\"><div class=\"pc\">";
			Pic const p = FitPic("nitem-" + Num(row.item), 1, 40, 24);
			if (!p.src.empty())
				out += "<img class=\"pic\" src=\"" + p.src + "\" style=\"width: " + Num(p.w) + "px; height: " + Num(p.h) + "px;\"/>";
			out += "</div><span class=\"a\">" + Escape(row.name) + "</span>";
			if (row.count > 1) out += "<span class=\"q\">" + Num(row.count) + "</span>";
			out += "</div>";
		}
		if (l.hidden > 0) out += "<span class=\"more\">" + Escape(Fill(Str("tac.ovl.more"), Num(l.hidden))) + "</span>";
		out += "</div>";
		return out;
	}
}

void RegisterTacticalOverlays()
{
	if (g_registered) return;
	g_registered = true;
	static Rml::ElementInstancerGeneric<OverlayLayer> instancer;
	Rml::Factory::RegisterElementInstancer("overlaylayer", &instancer);
}

void TacticalOverlaysUpdate(Rml::ElementDocument* const doc)
{
	Rml::Context* const ctx = Context();
	if (!doc || !ctx) return;
	Frame const& f = CurrentOverlayFrame();
	float const dp = std::max(0.01f, DpScale());
	float const outW = float(ctx->GetDimensions().x), outH = float(ctx->GetDimensions().y);
	uint32_t const now = GetJA2Clock();

	// ---- the layer
	LayerData next;
	next.dp = dp;
	for (Locator const& l : f.locators)
	{
		RingPhase const p = RingAt(l.frame);
		// a locator is a ring on a point: the merc's body, a tile, a new item
		next.rings.push_back({ WorldToOutput(l.at.x, l.at.y), p.radiusDp * dp, p.alpha, ToneColour(l.tone) });
	}
	for (BurstMark const& b : f.bursts) next.bursts.push_back(WorldToOutput(b.at.x, b.at.y));
	if (f.hasArrows)
	{
		Rml::Vector2f const m = WorldToOutput(f.arrowsAt.x, f.arrowsAt.y);
		for (Arrow const& a : f.arrows)
		{
			// up arrows float over his head, down arrows under his feet; a ledge arrow sits beside him
			float const step = 14.f * dp;
			float const base = a.climb ? 34.f * dp : 72.f * dp;
			for (size_t i = 0; i < a.tones.size(); ++i)
			{
				float const y = a.up ? m.y - base - float(i) * step : m.y + 12.f * dp + base - 30.f * dp + float(i) * step;
				next.chevrons.push_back({ { m.x + (a.climb ? 26.f * dp : 0.f), y }, a.up, ArrowColour(a.tones[i]) });
			}
		}
	}
	if (f.band.valid)
	{
		Rml::Vector2f const a = CanvasToOutput(f.band.l, f.band.t), b = CanvasToOutput(f.band.r, f.band.b);
		next.band = true;
		next.bl = a.x; next.bt = a.y; next.br = b.x; next.bb = b.y;
		next.glow = BandGlow(now);
	}
	bool const any = !next.rings.empty() || !next.bursts.empty() || !next.chevrons.empty() || next.band;
	bool const had = !g_layer.rings.empty() || !g_layer.bursts.empty() || !g_layer.chevrons.empty() || g_layer.band;
	// the pulse and the glow move on their own: while anything is up the layer is rebuilt every frame
	if (any || had)
	{
		next.revision = g_layer.revision + 1;
		g_layer = std::move(next);
		Invalidate(2);
	}

	// ---- the lists
	std::string rml;
	bool const overHud = TacticalHudWantsMouse();
	float floorY = outH - 4.f * dp;
	if (Rml::Element* bar = doc->GetElementById("tac.bar"))
		floorY = std::min(floorY, bar->GetAbsoluteOffset(Rml::BoxArea::Border).y - 4.f * dp);
	Rect const bounds{ 4.f * dp, 4.f * dp, outW - 8.f * dp, std::max(0.f, floorY - 4.f * dp) };
	Rml::Vector2f const mouse = MousePosition();
	for (PoolBox const& p : f.pools)
	{
		float const h = (float(p.list.rows.size()) * ROW_H + (p.list.hidden > 0 ? MORE_H : 0.f) + 2 * POOL_PAD + POOL_TOP) * dp;
		Rect r;
		if (p.atPointer)
		{
			if (overHud || mouse.x < 0) continue;
			// to the right of the pointer, clear of the item it may carry
			r = PlaceList(mouse.x, mouse.y, POOL_W * dp, h, bounds, 18.f * dp + float(CursorItemWidth()));
		}
		else
		{
			Rml::Vector2f const at = WorldToOutput(p.at.x, p.at.y);
			if (at.x < -60 * dp || at.x > outW + 60 * dp || at.y < -60 * dp || at.y > outH + 60 * dp) continue;
			r = PlaceList(at.x, at.y, POOL_W * dp, h, bounds, 38.f * dp);
		}
		rml += PoolRml(p.list, r);
	}
	// a civilian's line: a bubble that grows upward from just over his head
	for (Speech const& sp : f.speech)
	{
		Rml::Vector2f const at = WorldToOutput(sp.at.x, sp.at.y);
		float const w = 220.f * dp;
		float const x = std::clamp(at.x - w * 0.5f, 4.f * dp, std::max(4.f * dp, outW - w - 4.f * dp));
		float const bottom = std::clamp(outH - at.y, floorBottom(outH, floorY), outH);
		rml += "<div class=\"ovl-say\" style=\"left: " + Num(int(x)) + "px; bottom: " + Num(int(bottom)) + "px;\">" + Escape(sp.text) + "</div>";
	}
	if (rml != g_html.rml)
	{
		g_html.rml = rml;
		if (Rml::Element* e = doc->GetElementById("tac.overlay.pools")) e->SetInnerRML(rml);
		Invalidate(2);
	}

	// ---- the pause banner
	if (f.paused != g_html.paused)
	{
		g_html.paused = f.paused;
		if (Rml::Element* e = doc->GetElementById("tac.paused"))
		{
			e->SetClass("shown", f.paused);
			if (f.paused)
				e->SetInnerRML("<div class=\"pill\"><img class=\"icon\" src=\"icon-pause\"/><span class=\"t\">" + Escape(Str("tac.ovl.paused")) +
					"</span><span class=\"m\">" + Escape(Str("tac.ovl.resume")) + "</span></div>");
		}
		Invalidate(2);
	}
}

}
