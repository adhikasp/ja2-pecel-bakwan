// The save/load spike screen with a minimal in-house layer: a flex-like layout pass over a tree of boxes
// (dp units), immediate-mode widgets drawn with SDL_Renderer primitives, and FreeType text with a per-size
// glyph cache. It is deliberately small: the question is how much we would have to build ourselves.
#include "UiSpike.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace spike {
namespace {

struct Color { Uint8 r, g, b, a = 255; };
constexpr Color hex(unsigned v, Uint8 a = 255) { return { Uint8(v >> 16), Uint8(v >> 8), Uint8(v), a }; }

// The same tokens as savelist.rcss, so the two spikes are compared on the toolkit and not on the style.
namespace tok {
constexpr Color bg = hex(0x101418), panel = hex(0x1b2229), line = hex(0x2c3640), text = hex(0xe6e9ec),
	title = hex(0xf2f4f5), muted = hex(0x8d99a6), rowOdd = hex(0x1f272f), rowHover = hex(0x26313b),
	rowSel = hex(0x3a3220), accent = hex(0xd9a441), accentHover = hex(0xe8b654), onAccent = hex(0x1b1406),
	secondary = hex(0xaeb8c2), money = hex(0xb9d99a), btn = hex(0x2c3640), btnHover = hex(0x3a4652),
	btnDown = hex(0x222a32), danger = hex(0x8e3a2a), dangerHover = hex(0xa8472f), disabledBg = hex(0x232b33),
	disabledFg = hex(0x5c6772), track = hex(0x151b20), thumb = hex(0x3b4957), thumbHover = hex(0x52667a),
	modal = hex(0x1f272f), modalLine = hex(0x3b4957), tip = hex(0xf2f4f5), tipText = hex(0x101418);
}


/* ---- Text ------------------------------------------------------------------------------------------ */

class Font
{
public:
	Font(SDL_Renderer* r, std::vector<unsigned char> data) : m_r(r), m_data(std::move(data))
	{
		if (FT_Init_FreeType(&m_lib) || FT_New_Memory_Face(m_lib, m_data.data(), FT_Long(m_data.size()), 0, &m_face))
			throw std::runtime_error("in-house spike: FreeType init failed");
	}
	~Font()
	{
		for (auto& [k, g] : m_glyphs) if (g.tex) SDL_DestroyTexture(g.tex);
		FT_Done_Face(m_face);
		FT_Done_FreeType(m_lib);
	}

	struct Glyph { SDL_Texture* tex; int w, h, left, top; float advance; };

	Glyph const& glyph(int px, char32_t c)
	{
		auto const key = (uint64_t(px) << 32) | c;
		auto it = m_glyphs.find(key);
		if (it != m_glyphs.end()) return it->second;
		FT_Set_Pixel_Sizes(m_face, 0, FT_UInt(px));
		Glyph g{ nullptr, 0, 0, 0, 0, 0 };
		if (!FT_Load_Char(m_face, c, FT_LOAD_RENDER))
		{
			FT_Bitmap const& bm = m_face->glyph->bitmap;
			g.w = int(bm.width);
			g.h = int(bm.rows);
			g.left = m_face->glyph->bitmap_left;
			g.top  = m_face->glyph->bitmap_top;
			g.advance = m_face->glyph->advance.x / 64.f;
			if (g.w > 0 && g.h > 0)
			{
				std::vector<Uint32> rgba(size_t(g.w) * g.h);
				for (int y = 0; y < g.h; ++y)
					for (int x = 0; x < g.w; ++x)
						rgba[size_t(y) * g.w + x] = (Uint32(bm.buffer[y * bm.pitch + x]) << 24) | 0xFFFFFF;
				SDL_Surface* s = SDL_CreateSurfaceFrom(g.w, g.h, SDL_PIXELFORMAT_ARGB8888, rgba.data(), g.w * 4);
				g.tex = SDL_CreateTextureFromSurface(m_r, s);
				SDL_DestroySurface(s);
				SDL_SetTextureBlendMode(g.tex, SDL_BLENDMODE_BLEND);
			}
		}
		return m_glyphs.emplace(key, g).first->second;
	}

	float ascender(int px)
	{
		FT_Set_Pixel_Sizes(m_face, 0, FT_UInt(px));
		return m_face->size->metrics.ascender / 64.f;
	}
	float lineHeight(int px)
	{
		FT_Set_Pixel_Sizes(m_face, 0, FT_UInt(px));
		return m_face->size->metrics.height / 64.f;
	}

	float measure(int px, std::string const& s)
	{
		float w = 0;
		for (unsigned char c : s) w += glyph(px, c).advance;
		return w;
	}

	/** Draws with the top of the line box at y. */
	void draw(std::string const& s, int px, float x, float y, Color c)
	{
		float const base = std::round(y + ascender(px));
		float pen = std::round(x);
		for (unsigned char ch : s)
		{
			Glyph const& g = glyph(px, ch);
			if (g.tex)
			{
				SDL_SetTextureColorMod(g.tex, c.r, c.g, c.b);
				SDL_SetTextureAlphaMod(g.tex, c.a);
				SDL_FRect const dst{ std::round(pen + g.left), base - g.top, float(g.w), float(g.h) };
				SDL_RenderTexture(m_r, g.tex, nullptr, &dst);
			}
			pen += g.advance;
		}
	}

private:
	SDL_Renderer*              m_r;
	std::vector<unsigned char> m_data;
	FT_Library                 m_lib{};
	FT_Face                    m_face{};
	std::unordered_map<uint64_t, Glyph> m_glyphs;
};


/* ---- Layout: a tree of boxes, sized along the main axis by fixed size or flex-grow ----------------- */

struct Node
{
	std::string       id;
	bool              row  = false;
	float             size = -1; // along the parent's main axis (dp); -1 = grow
	float             grow = 0;
	float             padX = 0, padY = 0, gap = 0;
	std::vector<Node> kids;
	SDL_FRect         r{};
};

Node box(std::string id, float size, float grow = 0) { Node n; n.id = std::move(id); n.size = size; n.grow = grow; return n; }

void layout(Node& n, SDL_FRect r, float s, std::map<std::string, SDL_FRect>& out)
{
	n.r = r;
	if (!n.id.empty()) out[n.id] = r;
	SDL_FRect const c{ r.x + n.padX * s, r.y + n.padY * s, r.w - 2 * n.padX * s, r.h - 2 * n.padY * s };
	float const main = n.row ? c.w : c.h;
	float fixed = n.kids.empty() ? 0 : n.gap * s * float(n.kids.size() - 1);
	float grow = 0;
	for (Node const& k : n.kids)
	{
		if (k.size >= 0) fixed += k.size * s;
		else grow += std::max(k.grow, 1.0f);
	}
	float const free = std::max(0.0f, main - fixed);
	float pos = n.row ? c.x : c.y;
	for (Node& k : n.kids)
	{
		float const len = k.size >= 0 ? k.size * s : free * std::max(k.grow, 1.0f) / grow;
		SDL_FRect const kr = n.row ? SDL_FRect{ pos, c.y, len, c.h } : SDL_FRect{ c.x, pos, c.w, len };
		layout(k, kr, s, out);
		pos += len + n.gap * s;
	}
}

bool inside(SDL_FRect const& r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }


/* ---- The screen ------------------------------------------------------------------------------------ */

class InhouseScreen final : public Screen
{
public:
	InhouseScreen(SDL_Renderer* r, SaveListModel m) : Screen(std::move(m)), m_r(r),
		m_regular(r, LoadAssetBytes("LatoLatin-Regular.ttf")),
		m_bold(r, LoadAssetBytes("LatoLatin-Bold.ttf"))
	{}

	char const* toolkit() const override { return "inhouse"; }

	void setSize(int w, int h, float uiScale) override
	{
		m_w = float(w);
		m_h = float(h);
		m_s = std::min(w / 1920.f, h / 1080.f) * uiScale;
		relayout();
	}

	void mouseMove(float x, float y) override
	{
		m_mx = x;
		m_my = y;
		if (m_dragThumb)
		{
			float const track = m_rects["list"].h - m_thumb.h;
			if (track > 0) m_scroll = std::clamp(m_dragScroll0 + (y - m_dragY0) / track * maxScroll(), 0.f, maxScroll());
		}
		m_model.hovered = m_model.modal ? -1 : rowAt(x, y);
	}

	void mouseButton(float x, float y, bool down) override
	{
		mouseMove(x, y);
		if (!down)
		{
			if (m_pressed == hit(x, y)) click(m_pressed);
			m_pressed.clear();
			m_dragThumb = false;
			return;
		}
		m_pressed = hit(x, y);
		if (m_pressed == "thumb")
		{
			m_dragThumb   = true;
			m_dragY0      = y;
			m_dragScroll0 = m_scroll;
		}
		else if (m_pressed == "track")
		{
			m_scroll = std::clamp(m_scroll + (y < m_thumb.y ? -1 : 1) * m_rects["list"].h, 0.f, maxScroll());
		}
		else if (m_pressed.rfind("slot", 0) == 0)
		{
			int const i = std::stoi(m_pressed.substr(4));
			bool const dbl = i == m_lastClickRow && m_time - m_lastClickTime < 0.4;
			m_model.select(i);
			if (dbl) m_model.load();
			m_lastClickRow  = i;
			m_lastClickTime = m_time;
		}
	}

	void wheel(float dy) override
	{
		if (!m_model.modal) m_scroll = std::clamp(m_scroll + dy * rowH() * 3, 0.f, maxScroll());
		m_model.hovered = rowAt(m_mx, m_my);
	}

	void key(SDL_Keycode k) override
	{
		switch (k)
		{
			case SDLK_ESCAPE: m_model.cancel(); break;
			case SDLK_RETURN: if (m_model.modal) m_model.confirmDelete(); else m_model.load(); break;
			case SDLK_DELETE: m_model.askDelete(); break;
			case SDLK_DOWN:   m_model.select(std::min(m_model.selected + 1, int(m_model.slots.size()) - 1)); reveal(); break;
			case SDLK_UP:     m_model.select(std::max(m_model.selected - 1, 0)); reveal(); break;
			default: break;
		}
	}

	void update(double seconds) override { m_time += seconds; }

	void render() override
	{
		relayout();
		SDL_SetRenderClipRect(m_r, nullptr);
		fill({ 0, 0, m_w, m_h }, tok::bg);
		SDL_FRect const& p = m_rects["panel"];
		rounded(p, 10, tok::line);
		rounded(inset(p, 1 * m_s), 9, tok::panel);

		SDL_FRect const& hd = m_rects["header"];
		text(m_bold, "Load game", 34, hd.x, hd.y, tok::title);
		std::string const count = std::to_string(m_model.slots.size()) + " saves";
		text(m_regular, count, 18, hd.x + hd.w - m_regular.measure(px(18), count), hd.y + 12 * m_s, tok::muted);

		SDL_FRect const& cols = m_rects["columns"];
		columns(cols, 15, tok::muted, tok::muted, tok::muted, tok::muted, "Name", "Sector", "Game time", "Money");
		fill({ cols.x, cols.y + cols.h - 1 * m_s, cols.w, std::max(1.f, m_s) }, tok::line);

		renderList();

		SDL_FRect const& ft = m_rects["footer"];
		fill({ ft.x, ft.y, ft.w, std::max(1.f, m_s) }, tok::line);
		text(m_regular, m_model.status, 18, ft.x, ft.y + 18 * m_s + (46 * m_s - lh(18)) / 2, tok::muted);
		bool const none = m_model.selected < 0;
		button("load", "Load", "Enter", none ? 0 : 1);
		button("delete", "Delete", "Del", none ? 0 : 2);
		button("close", "Close", "Esc", 3);

		if (m_model.hovered >= 0 && !m_model.modal) renderTooltip();
		if (m_model.modal) renderModal();
	}

	std::vector<Element> elements() override
	{
		relayout();
		std::vector<Element> out;
		for (auto const& [id, r] : m_rects)
		{
			if (id.rfind("slot", 0) == 0 && !visibleRow(r)) continue;
			if (!m_model.modal && (id == "modal" || id == "cancel" || id == "confirm" || id == "backdrop")) continue;
			out.push_back({ id, r });
		}
		return out;
	}

private:
	int   px(float dp) const { return std::max(1, int(std::lround(dp * m_s))); }
	float lh(float dp)       { return m_regular.lineHeight(px(dp)); }
	float rowH() const       { return 46 * m_s; }
	float contentH() const   { return rowH() * float(m_model.slots.size()); }
	float maxScroll()        { return std::max(0.f, contentH() - m_rects["list"].h); }
	static SDL_FRect inset(SDL_FRect r, float d) { return { r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d }; }
	bool visibleRow(SDL_FRect const& r) { SDL_FRect const& l = m_rects["list"]; return r.y + r.h > l.y && r.y < l.y + l.h; }

	void relayout()
	{
		m_rects.clear();
		float const pw = 1200 * m_s, ph = 860 * m_s;
		Node panel = box("panel", -1);
		panel.padX = 32;
		panel.padY = 28;
		Node header = box("header", 52);
		Node columns = box("columns", 30);
		Node gap1 = box("", 6);
		Node list = box("list", -1, 1);
		Node gap2 = box("", 18);
		Node footer = box("footer", 64);
		footer.row = true;
		footer.gap = 12;
		footer.kids = { box("status", -1, 1), box("load", 150), box("delete", 150), box("close", 140) };
		panel.kids = { header, columns, gap1, list, gap2, footer };
		layout(panel, { std::round((m_w - pw) / 2), std::round((m_h - ph) / 2), pw, ph }, m_s, m_rects);
		// Buttons sit below the footer's divider
		for (char const* b : { "load", "delete", "close" })
		{
			SDL_FRect& r = m_rects[b];
			r.y += 18 * m_s;
			r.h  = 46 * m_s;
		}
		// Rows (virtual: only what is visible gets a rect) and the scrollbar
		SDL_FRect const l = m_rects["list"];
		m_scroll = std::clamp(m_scroll, 0.f, maxScroll());
		float const bar = 12 * m_s;
		bool const scrolls = contentH() > l.h;
		float const rowW = l.w - (scrolls ? bar + 4 * m_s : 0);
		int const first = int(m_scroll / rowH());
		for (int i = first; i < int(m_model.slots.size()); ++i)
		{
			float const y = l.y + i * rowH() - m_scroll;
			if (y > l.y + l.h) break;
			m_rects["slot" + std::to_string(i)] = { l.x, y, rowW, rowH() };
		}
		if (scrolls)
		{
			m_track = { l.x + l.w - bar, l.y, bar, l.h };
			float const th = std::max(40 * m_s, l.h * l.h / contentH());
			m_thumb = { m_track.x, l.y + (l.h - th) * (m_scroll / maxScroll()), bar, th };
			m_rects["track"] = m_track;
			m_rects["thumb"] = m_thumb;
		}
		if (m_model.modal)
		{
			m_rects["backdrop"] = { 0, 0, m_w, m_h };
			// Sized to its content: title, wrapped message, buttons
			float const textH = lh(26) + 12 * m_s + lh(18) * float(wrap(m_regular, 18, modalMessage(), 496 * m_s).size());
			float const h = 28 * m_s + textH + 26 * m_s + 46 * m_s + 28 * m_s;
			SDL_FRect const mr{ std::round(m_w / 2 - 280 * m_s), std::round(m_h / 2 - 120 * m_s), 560 * m_s, h };
			m_rects["modal"]   = mr;
			m_rects["confirm"] = { mr.x + mr.w - 32 * m_s - 120 * m_s, mr.y + mr.h - 28 * m_s - 46 * m_s, 120 * m_s, 46 * m_s };
			m_rects["cancel"]  = { m_rects["confirm"].x - 12 * m_s - 120 * m_s, m_rects["confirm"].y, 120 * m_s, 46 * m_s };
		}
	}

	std::string hit(float x, float y)
	{
		relayout();
		if (m_model.modal)
		{
			for (char const* id : { "confirm", "cancel" }) if (inside(m_rects[id], x, y)) return id;
			return "backdrop";
		}
		for (char const* id : { "thumb", "track", "load", "delete", "close" })
		{
			auto it = m_rects.find(id);
			if (it != m_rects.end() && inside(it->second, x, y)) return id;
		}
		int const row = rowAt(x, y);
		return row >= 0 ? "slot" + std::to_string(row) : std::string();
	}

	int rowAt(float x, float y)
	{
		SDL_FRect const& l = m_rects["list"];
		if (!inside(l, x, y) || (m_rects.count("track") && x >= m_track.x)) return -1;
		int const i = int((y - l.y + m_scroll) / rowH());
		return i < int(m_model.slots.size()) ? i : -1;
	}

	void click(std::string const& id)
	{
		if (id == "load") m_model.load();
		else if (id == "delete") m_model.askDelete();
		else if (id == "close" || id == "cancel") m_model.cancel();
		else if (id == "confirm") m_model.confirmDelete();
	}

	void reveal()
	{
		if (m_model.selected < 0) return;
		float const y = m_model.selected * rowH();
		float const h = m_rects["list"].h;
		if (y < m_scroll) m_scroll = y;
		else if (y + rowH() > m_scroll + h) m_scroll = y + rowH() - h;
	}

	void renderList()
	{
		SDL_FRect const l = m_rects["list"];
		SDL_Rect const clip{ int(l.x), int(l.y), int(std::ceil(l.w)), int(std::ceil(l.h)) };
		SDL_SetRenderClipRect(m_r, &clip);
		for (auto const& [id, r] : m_rects)
		{
			if (id.rfind("slot", 0) != 0) continue;
			int const i = std::stoi(id.substr(4));
			bool const sel = i == m_model.selected;
			Color const bgc = sel ? tok::rowSel : i == m_model.hovered ? tok::rowHover : i % 2 ? tok::rowOdd : tok::panel;
			if (bgc.r != tok::panel.r || bgc.g != tok::panel.g) rounded(r, 4, bgc);
			if (sel) fill({ r.x, r.y, 3 * m_s, r.h }, tok::accent);
			SaveSlot const& s = m_model.slots[i];
			std::string const& when = s.when;
			columns({ r.x + 4 * m_s, r.y, r.w - 4 * m_s, r.h }, 18, tok::text, tok::secondary, tok::secondary, tok::money,
				s.name, s.sector, when, "$" + std::to_string(s.money));
		}
		if (m_rects.count("track"))
		{
			rounded(m_track, 6, tok::track);
			bool const hot = m_dragThumb || inside(m_thumb, m_mx, m_my);
			rounded(m_thumb, 6, hot ? tok::thumbHover : tok::thumb);
		}
		SDL_SetRenderClipRect(m_r, nullptr);
	}

	/** Name | sector | time | money, at the widths of the RCSS version. */
	void columns(SDL_FRect r, float size, Color c1, Color c2, Color c3, Color c4,
		std::string const& a, std::string const& b, std::string const& c, std::string const& d)
	{
		float const x0 = r.x + 16 * m_s, x3 = r.x + r.w - 16 * m_s;
		float const wMoney = 130 * m_s, wWhen = 180 * m_s, wWhere = 230 * m_s;
		float const y = r.y + (r.h - lh(size)) / 2;
		text(m_regular, a, size, x0, y, c1);
		text(m_regular, b, size, x3 - wMoney - wWhen - wWhere, y, c2);
		text(m_regular, c, size, x3 - wMoney - wWhen, y, c3);
		text(m_regular, d, size, x3 - m_regular.measure(px(size), d), y, c4);
	}

	/** style: 0 disabled, 1 primary, 2 danger, 3 normal */
	void button(char const* id, std::string const& label, std::string const& shortcut, int style)
	{
		SDL_FRect const& r = m_rects[id];
		bool const hot = !m_model.modal && inside(r, m_mx, m_my);
		bool const down = hot && m_pressed == id;
		Color bgc = tok::btn, fg = tok::text;
		switch (style)
		{
			case 0: bgc = tok::disabledBg; fg = tok::disabledFg; break;
			case 1: bgc = hot ? tok::accentHover : tok::accent; fg = tok::onAccent; break;
			case 2: bgc = hot ? tok::dangerHover : tok::danger; break;
			default: bgc = down ? tok::btnDown : hot ? tok::btnHover : tok::btn; break;
		}
		rounded(r, 6, bgc);
		float const wl = m_bold.measure(px(18), label), ws = m_regular.measure(px(14), shortcut);
		float const x = r.x + (r.w - wl - 6 * m_s - ws) / 2;
		text(m_bold, label, 18, x, r.y + (r.h - lh(18)) / 2, fg);
		Color dim = fg;
		dim.a = 178;
		text(m_regular, shortcut, 14, x + wl + 6 * m_s, r.y + (r.h - lh(14)) / 2 + 2 * m_s, dim);
	}

	void renderTooltip()
	{
		std::string const t = m_model.tooltip(m_model.hovered);
		float const w = m_regular.measure(px(16), t) + 24 * m_s, h = lh(16) + 16 * m_s;
		SDL_FRect r{ m_mx + 18 * m_s, m_my + 22 * m_s, w, h };
		r.x = std::min(r.x, m_w - w - 4 * m_s);
		r.y = std::min(r.y, m_h - h - 4 * m_s);
		m_rects["tooltip"] = r;
		rounded(r, 5, tok::tip);
		text(m_regular, t, 16, r.x + 12 * m_s, r.y + 8 * m_s, tok::tipText);
	}

	void renderModal()
	{
		fill({ 0, 0, m_w, m_h }, hex(0x000000, 0xa8));
		SDL_FRect const mr = m_rects["modal"];
		rounded(mr, 10, tok::modalLine);
		rounded(inset(mr, 1 * m_s), 9, tok::modal);
		text(m_bold, "Delete this save?", 26, mr.x + 32 * m_s, mr.y + 28 * m_s, tok::text);
		float y = mr.y + 28 * m_s + lh(26) + 12 * m_s;
		for (std::string const& line : wrap(m_regular, 18, modalMessage(), mr.w - 64 * m_s))
		{
			text(m_regular, line, 18, mr.x + 32 * m_s, y, tok::secondary);
			y += lh(18);
		}
		for (char const* id : { "cancel", "confirm" })
		{
			SDL_FRect const& r = m_rects[id];
			bool const hot = inside(r, m_mx, m_my);
			bool const danger = std::string(id) == "confirm";
			rounded(r, 6, danger ? (hot ? tok::dangerHover : tok::danger) : (hot ? tok::btnHover : tok::btn));
			std::string const label = danger ? "Delete" : "Cancel";
			text(m_bold, label, 18, r.x + (r.w - m_bold.measure(px(18), label)) / 2, r.y + (r.h - lh(18)) / 2, tok::text);
		}
	}

	void text(Font& f, std::string const& s, float size, float x, float y, Color c) { f.draw(s, px(size), x, y, c); }

	std::string modalMessage() const
	{
		std::string const name = m_model.selected >= 0 ? m_model.slots[m_model.selected].name : "";
		return "\"" + name + "\" will be removed. This cannot be undone.";
	}

	/** Greedy word wrap (what RmlUi gives for free, with CSS white-space rules and hyphenation-free breaking). */
	std::vector<std::string> wrap(Font& f, float size, std::string const& s, float maxW)
	{
		std::vector<std::string> lines;
		std::string line, word;
		auto flush = [&] {
			if (word.empty()) return;
			std::string const tryLine = line.empty() ? word : line + " " + word;
			if (!line.empty() && f.measure(px(size), tryLine) > maxW) { lines.push_back(line); line = word; }
			else line = tryLine;
			word.clear();
		};
		for (char c : s)
		{
			if (c == ' ') flush();
			else word += c;
		}
		flush();
		if (!line.empty()) lines.push_back(line);
		return lines;
	}

	void fill(SDL_FRect r, Color c)
	{
		r = { std::round(r.x), std::round(r.y), std::round(r.w), std::round(r.h) };
		SDL_SetRenderDrawBlendMode(m_r, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(m_r, c.r, c.g, c.b, c.a);
		SDL_RenderFillRect(m_r, &r);
	}

	/** A filled rectangle with rounded corners: triangles for a centre cross and four corner fans. */
	void rounded(SDL_FRect r, float radiusDp, Color c)
	{
		// Whole pixels: SDL's software rasterizer leaves seams between triangles at fractional edges.
		r = { std::round(r.x), std::round(r.y), std::round(r.w), std::round(r.h) };
		float const rad = std::round(std::min({ radiusDp * m_s, r.w / 2, r.h / 2 }));
		if (rad < 1) { fill(r, c); return; }
		SDL_FColor const fc{ c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f };
		std::vector<SDL_Vertex> v;
		auto quad = [&](float x0, float y0, float x1, float y1) {
			SDL_Vertex const a{ { x0, y0 }, fc, {} }, b{ { x1, y0 }, fc, {} }, cc{ { x1, y1 }, fc, {} }, d{ { x0, y1 }, fc, {} };
			v.insert(v.end(), { a, b, cc, a, cc, d });
		};
		quad(r.x + rad, r.y, r.x + r.w - rad, r.y + r.h);
		quad(r.x, r.y + rad, r.x + rad, r.y + r.h - rad);
		quad(r.x + r.w - rad, r.y + rad, r.x + r.w, r.y + r.h - rad);
		int const seg = std::clamp(int(rad / 2), 3, 12);
		float const cx[4] = { r.x + rad, r.x + r.w - rad, r.x + r.w - rad, r.x + rad };
		float const cy[4] = { r.y + rad, r.y + rad, r.y + r.h - rad, r.y + r.h - rad };
		for (int k = 0; k < 4; ++k)
		{
			float const a0 = float(M_PI) + k * float(M_PI) / 2;
			for (int i = 0; i < seg; ++i)
			{
				float const t0 = a0 + (float(M_PI) / 2) * i / seg, t1 = a0 + (float(M_PI) / 2) * (i + 1) / seg;
				v.push_back({ { cx[k], cy[k] }, fc, {} });
				v.push_back({ { cx[k] + rad * std::cos(t0), cy[k] + rad * std::sin(t0) }, fc, {} });
				v.push_back({ { cx[k] + rad * std::cos(t1), cy[k] + rad * std::sin(t1) }, fc, {} });
			}
		}
		SDL_SetRenderDrawBlendMode(m_r, SDL_BLENDMODE_BLEND);
		SDL_RenderGeometry(m_r, nullptr, v.data(), int(v.size()), nullptr, 0);
	}

	SDL_Renderer* m_r;
	Font          m_regular;
	Font          m_bold;
	std::map<std::string, SDL_FRect> m_rects;
	SDL_FRect     m_track{}, m_thumb{};
	float  m_w = 1920, m_h = 1080, m_s = 1;
	float  m_mx = -1000, m_my = -1000;
	float  m_scroll = 0;
	bool   m_dragThumb = false;
	float  m_dragY0 = 0, m_dragScroll0 = 0;
	std::string m_pressed;
	double m_time = 0, m_lastClickTime = -10;
	int    m_lastClickRow = -1;
};

} // namespace

std::unique_ptr<Screen> CreateInhouseScreen(SDL_Renderer* r, SaveListModel m)
{
	return std::make_unique<InhouseScreen>(r, std::move(m));
}

} // namespace spike
