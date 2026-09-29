// Phase 1 design-system gallery (docs/ui/design-system.md): every component of assets/ui/components.rcss in every
// state, on six pages (controls, data, overlays, game, icons, tokens). The live parts (page tabs, toggle, slider,
// dropdown, table sorting, row selection, inventory drag and drop) are bound to a data model like a Phase 2 view
// model. layoutProblems() is the layout audit the tests run at every resolution and UI scale.
#include "UiSpike.h"
#include "RmlCommon.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>

namespace spike {

char const* const GalleryPages[6] = { "controls", "data", "overlays", "game", "icons", "tokens" };

std::vector<std::string> IconNames()
{
	std::vector<std::string> names;
	int count = 0;
	std::string const dir = StyleDir() + "/icons";
	if (char** files = SDL_GlobDirectory(dir.c_str(), "*.svg", 0, &count))
	{
		for (int i = 0; i < count; ++i)
		{
			std::string n = files[i];
			n = n.substr(n.find_last_of("/\\") + 1);
			names.push_back(n.substr(0, n.size() - 4));
		}
		SDL_free(files);
	}
	std::sort(names.begin(), names.end());
	return names;
}

namespace {

struct GRow { std::string name, assign, loc, morale, state; int hp = 0, contract = 0, salary = 0; };
struct GCard { std::string caption, name, face, icon, cls, weapon, ammo; int ap = 0, hp = 0, breath = 0, morale = 0; };
struct GSlot { std::string icon; int count = 0, cond = 0; };
struct GGroup { std::string title; std::vector<std::string> names; };
struct GKV { std::string name, value; };

/** Which group each icon belongs to (the needs list in docs/ui/design-system.md). Unlisted icons go to "Other". */
struct IconGroupDef { char const* title; std::vector<char const*> names; };
std::vector<IconGroupDef> const& IconGroupDefs()
{
	static std::vector<IconGroupDef> const defs = {
		{ "Actions", { "end-turn", "map", "laptop", "options", "save", "load", "close", "confirm", "add", "remove", "search",
			"filter", "sort-asc", "sort-desc", "chevron-left", "chevron-right", "chevron-up", "chevron-down", "menu", "more", "drag",
			"pause", "play", "fast-forward", "stance-stand", "stance-crouch", "stance-prone", "walk", "run", "sneak", "look", "talk",
			"target", "burst", "throw", "punch", "stab", "reload", "climb", "door", "key", "wire-cut", "repair", "remote", "bomb",
			"exit-sector", "wait", "inventory", "trade" } },
		{ "Status", { "health", "breath", "morale", "action-points", "bleeding", "drunk", "asleep", "fatigue", "suppressed",
			"wounded", "unconscious", "dead", "contract", "level-up", "ok", "info", "warning", "error", "lock" } },
		{ "Assignments", { "squad", "on-duty", "doctor", "patient", "vehicle", "in-transit", "repair", "train-self", "train-town",
			"train-teammate", "train-by-other", "hospital", "pow", "dead" } },
		{ "Item categories", { "gun", "launcher", "blade", "throwing-knife", "punch", "grenade", "bomb", "ammo", "armour",
			"medkit", "toolkit", "face-gear", "camouflage", "key", "money", "misc-item" } },
		{ "Map markers", { "town", "sam-site", "mine", "militia", "enemy", "enemy-group", "player-group", "destination",
			"waypoint", "merc-moving", "helicopter", "airport", "hospital", "prison", "vehicle", "unexplored", "loyalty",
			"sector-inventory" } },
	};
	return defs;
}

std::string ReadFile(std::string const& path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) throw std::runtime_error("gallery: cannot read " + path);
	return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

class GalleryScreen final : public Screen
{
public:
	GalleryScreen(SDL_Renderer* r, std::string page) : Screen(SaveListModel{}), m_renderer(r), m_ri(r), m_page(std::move(page))
	{
		if (std::find(std::begin(GalleryPages), std::end(GalleryPages), m_page) == std::end(GalleryPages))
			throw std::runtime_error("gallery: unknown page " + m_page);
		InitRmlOnce();
		LoadUiFonts();
		static int counter = 0;
		m_name = "gallery" + std::to_string(counter++);
		m_ctx = Rml::CreateContext(m_name, { 1920, 1080 }, &m_ri);
		if (!m_ctx) throw std::runtime_error("RmlUi: CreateContext failed");
		fill();
		bindModel();
		// the markup may use var(--token) in inline styles too
		std::string const rml = ExpandTokens(ReadFile(StyleDir() + "/gallery.rml"));
		m_doc = m_ctx->LoadDocumentFromMemory(rml, "gallery.rml");
		if (!m_doc) throw std::runtime_error("RmlUi: cannot load gallery.rml");
		m_doc->Show();
	}

	~GalleryScreen() override
	{
		Rml::RemoveContext(m_name);
		Rml::ReleaseRenderManagers();
	}

	char const* toolkit() const override { return "gallery"; }

	void setSize(int w, int h, float uiScale) override
	{
		m_w = w; m_h = h; m_scale = uiScale;
		m_ctx->SetDimensions({ w, h });
		float const dp = std::min(w / 1920.f, h / 1080.f) * uiScale;
		m_ctx->SetDensityIndependentPixelRatio(dp);
		char buf[96];
		std::snprintf(buf, sizeof buf, "%dx%d \xC2\xB7 UI %d%% \xC2\xB7 1dp = %.2fpx", w, h, int(std::lround(uiScale * 100)), dp);
		m_readout = buf;
		m_handle.DirtyVariable("readout");
	}

	void mouseMove(float x, float y) override { m_ctx->ProcessMouseMove(int(x), int(y), 0); }
	void mouseButton(float x, float y, bool down) override
	{
		mouseMove(x, y);
		if (down) m_ctx->ProcessMouseButtonDown(0, 0);
		else      m_ctx->ProcessMouseButtonUp(0, 0);
	}
	void wheel(float dy) override { m_ctx->ProcessMouseWheel(Rml::Vector2f(0, dy), 0); }
	void key(SDL_Keycode k) override
	{
		if (k == SDLK_ESCAPE)
		{
			if (m_dropdownOpen) { m_dropdownOpen = false; m_handle.DirtyVariable("dropdown_open"); }
			else m_model.cancel();
		}
		// Left/Right switch pages
		auto const it = std::find(std::begin(GalleryPages), std::end(GalleryPages), m_page);
		int const i = int(it - std::begin(GalleryPages));
		if (k == SDLK_RIGHT) go(GalleryPages[(i + 1) % 6]);
		if (k == SDLK_LEFT) go(GalleryPages[(i + 5) % 6]);
	}

	void update(double seconds) override
	{
		RmlClock().now += seconds;
		m_ctx->Update();
	}

	void render() override
	{
		SDL_SetRenderClipRect(m_renderer, nullptr);
		SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
		SDL_RenderClear(m_renderer);
		m_ctx->Render();
		SDL_SetRenderClipRect(m_renderer, nullptr);
	}

	std::vector<Element> elements() override
	{
		std::vector<Element> out;
		collect(m_doc, out);
		return out;
	}

	std::vector<std::string> layoutProblems() override
	{
		m_ctx->Update();
		std::vector<std::string> out;
		Rml::Element* page = m_doc->GetElementById("page");
		auto const box = [](Rml::Element* e) {
			Rml::Vector2f const p = e->GetAbsoluteOffset(Rml::BoxArea::Border);
			Rml::Vector2f const s = e->GetBox().GetSize(Rml::BoxArea::Border);
			return SDL_FRect{ p.x, p.y, s.x, s.y };
		};
		SDL_FRect const pageBox = page ? box(page) : SDL_FRect{ 0, 0, float(m_w), float(m_h) };
		// scrollbar gutter: content must leave room for it
		float const pageRight = pageBox.x + (page ? page->GetClientLeft() + page->GetClientWidth() : pageBox.w);
		auto const describe = [&](Rml::Element* e, char const* what) {
			std::string d = e->GetTagName();
			if (!e->GetId().empty()) d += "#" + e->GetId();
			else if (!e->GetClassNames().empty()) d += "." + e->GetClassNames();
			SDL_FRect const b = box(e);
			char buf[96];
			std::snprintf(buf, sizeof buf, " [%.0f,%.0f %.0fx%.0f]", b.x, b.y, b.w, b.h);
			out.push_back(std::string(what) + ": " + d + buf);
		};
		std::function<void(Rml::Element*)> walk = [&](Rml::Element* e) {
			if (out.size() >= 20 || !e->IsVisible(true)) return;
			SDL_FRect const b = box(e);
			if (b.w > 0.5f && b.h > 0.5f && e != m_doc)
			{
				// nearest ancestor that clips
				Rml::Element* clip = nullptr;
				for (Rml::Element* a = e->GetParentNode(); a; a = a->GetParentNode())
				{
					auto const& cv = a->GetComputedValues();
					if (cv.overflow_x() != Rml::Style::Overflow::Visible || cv.overflow_y() != Rml::Style::Overflow::Visible) { clip = a; break; }
				}
				float const tol = 1.5f;
				if (!clip)
				{
					if (b.x < -tol || b.y < -tol || b.x + b.w > m_w + tol || b.y + b.h > m_h + tol) describe(e, "off screen");
				}
				else if (clip == page)
				{
					// vertical scrolling is allowed (only at UI scales above 100%); horizontal overflow never
					float const sx = page->GetScrollLeft();
					if (b.x + sx < pageBox.x - tol || b.x + b.w + sx > pageRight + tol) describe(e, "overflows the page horizontally");
				}
			}
			for (int i = 0; i < e->GetNumChildren(); ++i) walk(e->GetChild(i));
		};
		walk(m_doc);
		if (page && m_scale <= 1.001f && page->GetScrollHeight() > page->GetClientHeight() + 1.5f)
		{
			char buf[128];
			std::snprintf(buf, sizeof buf, "page %s needs scrolling at 100%% (%.0f > %.0f)", m_page.c_str(),
				page->GetScrollHeight(), page->GetClientHeight());
			out.push_back(buf);
		}
		return out;
	}

private:
	void fill()
	{
		m_pages.assign(std::begin(GalleryPages), std::end(GalleryPages));
		m_options = { "Novice", "Experienced", "Expert", "Insane" };
		m_dropdownValue = "Experienced";
		m_rows = {
			{ "Ivan", "Squad 1", "B13", "Good", "", 88, 11, 1300 },
			{ "Lynx", "Squad 1", "B13", "Stable", "hover", 61, 4, 1800 },
			{ "Grizzly", "Squad 1", "B13", "Poor", "error", 17, 0, 1500 },
			{ "Shadow", "Squad 1", "B13", "Great", "focus", 74, 2, 1650 },
			{ "Fox", "Doctor", "B13", "Stable", "", 66, 13, 1400 },
			{ "Barry", "Sleep", "A9", "Good", "disabled", 80, 7, 1100 },
			{ "Igor", "Train town", "D13", "Good", "", 70, 20, 950 },
			{ "Fidel", "Squad 2", "C13", "Poor", "", 77, 1, 700 },
		};
		m_selected = "Ivan";
		sortRows();
		for (int i = 0; i < 30; ++i)
		{
			char buf[96];
			static char const* const msgs[] = { "Ivan finished repairing the M-60.", "Militia training complete in Drassen.",
				"Enemy patrol spotted near C13.", "Skyrider is ready at the airport.", "Lynx is bleeding.", "Contract offer from AIM." };
			std::snprintf(buf, sizeof buf, "%02d:%02d  %s", 6 + i / 3, (i * 17) % 60, msgs[i % 6]);
			m_log.push_back(buf);
		}
		bool const faces = HasImageProvider();
		auto face = [&](int n) { return faces ? "face-" + std::to_string(n) : std::string("gen-silhouette"); };
		m_cards = {
			{ "Default", "Lynx", face(2), "", "", "Barrett", "8/10", 18, 61, 70, 64 },
			{ "Hover", "Shadow", face(10), "", "hover", "MP5K", "25/30", 24, 74, 95, 90 },
			{ "Selected", "Ivan", face(7), "", "selected", "AK-74", "30/30", 21, 88, 84, 78 },
			{ "Focus", "Red", face(11), "", "focus", "C-7", "30/30", 22, 90, 90, 72 },
			{ "Critical", "Grizzly", face(3), "icon-bleeding", "critical", "M-60", "40/100", 9, 17, 35, 41 },
			{ "Asleep", "Fox", face(14), "icon-asleep", "asleep", "Glock 18", "15/19", 0, 66, 22, 55 },
			{ "Dead", "Barry", face(0), "icon-dead", "dead", "Mini Uzi", "0/32", 0, 0, 0, 0 },
		};
		m_inv = std::vector<GSlot>(12);
		m_inv[0] = { "icon-gun", 1, 92 }; m_inv[1] = { "icon-ammo", 3, 100 }; m_inv[2] = { "icon-armour", 1, 64 };
		m_inv[4] = { "icon-medkit", 1, 35 }; m_inv[5] = { "icon-grenade", 2, 100 }; m_inv[8] = { "icon-toolkit", 1, 80 };
		m_ground = std::vector<GSlot>(18);
		m_ground[0] = { "icon-launcher", 1, 70 }; m_ground[3] = { "icon-blade", 1, 55 }; m_ground[7] = { "icon-money", 1, 100 };
		m_ground[10] = { "icon-face-gear", 1, 90 }; m_ground[13] = { "icon-key", 1, 100 };
		m_invStatus = "Drag an item onto another slot: it moves (or swaps).";

		std::vector<std::string> const names = IconNames();
		std::vector<std::string> grouped;
		for (IconGroupDef const& d : IconGroupDefs())
		{
			GGroup g{ d.title, {} };
			for (char const* n : d.names)
				if (std::find(names.begin(), names.end(), n) != names.end()) { g.names.push_back(n); grouped.push_back(n); }
			m_iconGroups.push_back(g);
		}
		GGroup other{ "Other", {} };
		for (auto const& n : names)
			if (std::find(grouped.begin(), grouped.end(), n) == grouped.end()) other.names.push_back(n);
		if (!other.names.empty()) m_iconGroups.push_back(other);

		for (auto const& [k, v] : Tokens())
		{
			if (k.rfind("c-", 0) == 0 && v.rfind("#", 0) == 0) m_swatches.push_back({ "--" + k, v });
			if (k.rfind("sp-", 0) == 0) m_spacing.push_back({ "--" + k, v });
		}
		auto const order = [](GKV const& a, GKV const& b) {
			return std::atoi(a.value.c_str()) < std::atoi(b.value.c_str());
		};
		std::sort(m_spacing.begin(), m_spacing.end(), order);
		// swatches in token-file order read better; the map is alphabetical, so group by prefix
		static char const* const prefixes[] = { "--c-void", "--c-bg", "--c-panel", "--c-line", "--c-scrim", "--c-text", "--c-on",
			"--c-accent", "--c-select", "--c-focus", "--c-ok", "--c-info", "--c-warn", "--c-danger", "--c-stat" };
		std::vector<GKV> sorted;
		for (char const* p : prefixes)
			for (GKV const& s : m_swatches)
				if (s.name.rfind(p, 0) == 0 && std::find_if(sorted.begin(), sorted.end(), [&](GKV const& x) { return x.name == s.name; }) == sorted.end())
					sorted.push_back(s);
		m_swatches = sorted;
	}

	void sortRows()
	{
		auto key = [&](GRow const& r) -> std::string {
			char buf[16];
			if (m_sortKey == "hp") { std::snprintf(buf, sizeof buf, "%05d", r.hp); return buf; }
			if (m_sortKey == "contract") { std::snprintf(buf, sizeof buf, "%05d", r.contract); return buf; }
			if (m_sortKey == "salary") { std::snprintf(buf, sizeof buf, "%05d", r.salary); return buf; }
			if (m_sortKey == "assign") return r.assign + r.name;
			return r.name;
		};
		std::stable_sort(m_rows.begin(), m_rows.end(), [&](GRow const& a, GRow const& b) { return m_sortAsc ? key(a) < key(b) : key(b) < key(a); });
		m_sortIcon = m_sortAsc ? "icon-sort-asc" : "icon-sort-desc";
	}

	void go(std::string const& p)
	{
		m_page = p;
		m_handle.DirtyAllVariables();
	}

	void bindModel()
	{
		Rml::DataModelConstructor c = m_ctx->CreateDataModel("gallery");
		if (auto s = c.RegisterStruct<GRow>())
		{
			s.RegisterMember("name", &GRow::name); s.RegisterMember("assign", &GRow::assign); s.RegisterMember("loc", &GRow::loc);
			s.RegisterMember("morale", &GRow::morale); s.RegisterMember("state", &GRow::state); s.RegisterMember("hp", &GRow::hp);
			s.RegisterMember("contract", &GRow::contract); s.RegisterMember("salary", &GRow::salary);
		}
		c.RegisterArray<std::vector<GRow>>();
		if (auto s = c.RegisterStruct<GCard>())
		{
			s.RegisterMember("caption", &GCard::caption); s.RegisterMember("name", &GCard::name); s.RegisterMember("face", &GCard::face);
			s.RegisterMember("icon", &GCard::icon); s.RegisterMember("cls", &GCard::cls); s.RegisterMember("weapon", &GCard::weapon);
			s.RegisterMember("ammo", &GCard::ammo); s.RegisterMember("ap", &GCard::ap); s.RegisterMember("hp", &GCard::hp);
			s.RegisterMember("breath", &GCard::breath); s.RegisterMember("morale", &GCard::morale);
		}
		c.RegisterArray<std::vector<GCard>>();
		if (auto s = c.RegisterStruct<GSlot>())
		{
			s.RegisterMember("icon", &GSlot::icon); s.RegisterMember("count", &GSlot::count); s.RegisterMember("cond", &GSlot::cond);
		}
		c.RegisterArray<std::vector<GSlot>>();
		c.RegisterArray<std::vector<std::string>>();
		if (auto s = c.RegisterStruct<GGroup>())
		{
			s.RegisterMember("title", &GGroup::title); s.RegisterMember("names", &GGroup::names);
		}
		c.RegisterArray<std::vector<GGroup>>();
		if (auto s = c.RegisterStruct<GKV>())
		{
			s.RegisterMember("name", &GKV::name); s.RegisterMember("value", &GKV::value);
		}
		c.RegisterArray<std::vector<GKV>>();

		c.Bind("page", &m_page);
		c.Bind("pages", &m_pages);
		c.Bind("readout", &m_readout);
		c.Bind("toggle_on", &m_toggle);
		c.Bind("slider", &m_slider);
		c.Bind("dropdown_open", &m_dropdownOpen);
		c.Bind("dropdown_value", &m_dropdownValue);
		c.Bind("options", &m_options);
		c.Bind("sort_key", &m_sortKey);
		c.Bind("sort_icon", &m_sortIcon);
		c.Bind("selected_name", &m_selected);
		c.Bind("mercs", &m_rows);
		c.Bind("log", &m_log);
		c.Bind("cards", &m_cards);
		c.Bind("inv", &m_inv);
		c.Bind("ground", &m_ground);
		c.Bind("drag_grid", &m_dragGrid);
		c.Bind("drag_index", &m_dragIndex);
		c.Bind("inv_status", &m_invStatus);
		c.Bind("icon_groups", &m_iconGroups);
		c.Bind("swatches", &m_swatches);
		c.Bind("spacing", &m_spacing);

		auto str = [](Rml::VariantList const& a, size_t i) { return a.size() > i ? a[i].Get<Rml::String>() : Rml::String(); };
		auto num = [](Rml::VariantList const& a, size_t i) { return a.size() > i ? a[i].Get<int>() : -1; };
		c.BindEventCallback("go", [this, str](Rml::DataModelHandle, Rml::Event&, Rml::VariantList const& a) { go(str(a, 0)); });
		c.BindEventCallback("toggle", [this](Rml::DataModelHandle h, Rml::Event&, Rml::VariantList const&) { m_toggle = !m_toggle; h.DirtyVariable("toggle_on"); });
		c.BindEventCallback("slide", [this](Rml::DataModelHandle h, Rml::Event& ev, Rml::VariantList const&) {
			Rml::Element* e = ev.GetCurrentElement();
			float const x = ev.GetParameter<float>("mouse_x", 0) - e->GetAbsoluteOffset(Rml::BoxArea::Border).x;
			m_slider = std::clamp(int(std::lround(100 * x / std::max(1.f, e->GetBox().GetSize().x))), 0, 100);
			h.DirtyVariable("slider");
		});
		c.BindEventCallback("dropdown_toggle", [this](Rml::DataModelHandle h, Rml::Event&, Rml::VariantList const&) { m_dropdownOpen = !m_dropdownOpen; h.DirtyVariable("dropdown_open"); });
		c.BindEventCallback("dropdown_pick", [this, str](Rml::DataModelHandle h, Rml::Event&, Rml::VariantList const& a) {
			m_dropdownValue = str(a, 0); m_dropdownOpen = false; h.DirtyAllVariables();
		});
		c.BindEventCallback("sort", [this, str](Rml::DataModelHandle h, Rml::Event&, Rml::VariantList const& a) {
			std::string const k = str(a, 0);
			if (k == m_sortKey) m_sortAsc = !m_sortAsc;
			else { m_sortKey = k; m_sortAsc = true; }
			sortRows();
			h.DirtyAllVariables();
		});
		c.BindEventCallback("select", [this, str](Rml::DataModelHandle h, Rml::Event&, Rml::VariantList const& a) { m_selected = str(a, 0); h.DirtyVariable("selected_name"); });
		c.BindEventCallback("drag_start", [this, str, num](Rml::DataModelHandle h, Rml::Event&, Rml::VariantList const& a) {
			m_dragGrid = str(a, 0); m_dragIndex = num(a, 1); h.DirtyVariable("drag_grid"); h.DirtyVariable("drag_index");
		});
		c.BindEventCallback("drag_end", [this](Rml::DataModelHandle h, Rml::Event&, Rml::VariantList const&) {
			m_dragGrid.clear(); m_dragIndex = -1; h.DirtyVariable("drag_grid"); h.DirtyVariable("drag_index");
		});
		c.BindEventCallback("drop", [this, str, num](Rml::DataModelHandle h, Rml::Event&, Rml::VariantList const& a) {
			std::string const grid = str(a, 0);
			int const to = num(a, 1);
			auto& src = m_dragGrid == "inv" ? m_inv : m_ground;
			auto& dst = grid == "inv" ? m_inv : m_ground;
			if (m_dragIndex >= 0 && m_dragIndex < int(src.size()) && to >= 0 && to < int(dst.size()) && !(&src == &dst && to == m_dragIndex))
			{
				std::swap(src[size_t(m_dragIndex)], dst[size_t(to)]);
				m_invStatus = "Moved " + dst[size_t(to)].icon.substr(5) + " to " + grid + " slot " + std::to_string(to + 1) + ".";
				++m_moves;
				m_model.status = m_invStatus;
			}
			m_dragGrid.clear();
			m_dragIndex = -1;
			h.DirtyAllVariables();
		});
		m_handle = c.GetModelHandle();
	}

	void collect(Rml::Element* e, std::vector<Element>& out)
	{
		if (!e->GetId().empty() && e->IsVisible(true))
		{
			Rml::Vector2f const p = e->GetAbsoluteOffset(Rml::BoxArea::Border);
			Rml::Vector2f const s = e->GetBox().GetSize(Rml::BoxArea::Border);
			out.push_back({ e->GetId(), { p.x, p.y, s.x, s.y } });
		}
		for (int i = 0; i < e->GetNumChildren(); ++i) collect(e->GetChild(i), out);
	}

	SDL_Renderer*         m_renderer;
	SdlRenderInterface    m_ri;
	std::string           m_page, m_name, m_readout;
	Rml::Context*         m_ctx = nullptr;
	Rml::ElementDocument* m_doc = nullptr;
	Rml::DataModelHandle  m_handle;
	int   m_w = 1920, m_h = 1080;
	float m_scale = 1;

	std::vector<std::string> m_pages, m_options, m_log;
	bool        m_toggle = false, m_dropdownOpen = false, m_sortAsc = true;
	int         m_slider = 65;
	std::string m_dropdownValue, m_sortKey = "name", m_sortIcon, m_selected;
	std::vector<GRow>   m_rows;
	std::vector<GCard>  m_cards;
	std::vector<GSlot>  m_inv, m_ground;
	std::string         m_dragGrid, m_invStatus;
	int                 m_dragIndex = -1, m_moves = 0;
	std::vector<GGroup> m_iconGroups;
	std::vector<GKV>    m_swatches, m_spacing;
};

} // namespace

std::unique_ptr<Screen> CreateGalleryScreen(SDL_Renderer* r, std::string const& page)
{
	return std::make_unique<GalleryScreen>(r, page);
}

} // namespace spike
