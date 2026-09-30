// Phase 1 style directions (docs/ui/style-directions.md): three visual directions for the native UI, each shown on
// the same three mock screens (main menu, tactical squad bar, strategic map sidebar + top bar). The markup is
// shared (assets/ui/mocks/<screen>.rml); the chosen direction B (Night Ops) is mocks/nightops.rcss. The
// data is placeholder, bound through an RmlUi data model like a Phase 2 view model would be.
#include "UiSpike.h"
#include "RmlCommon.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace spike {

char const* const StyleDirections[1] = { "b" }; // Night Ops, chosen (docs/ui/style-directions.md)
char const* const StyleScreens[3]    = { "mainmenu", "squadbar", "mapscreen" };

namespace {

struct DemoMerc
{
	std::string name, initials, face, assignment, location, destination, contract, weapon, ammo, state, moraleText;
	int  hp = 0, hpMax = 100, ap = 0, breath = 0, morale = 0;
	bool selected = false;
	std::string hpPct, breathPct, moralePct, bleedPct; // widths for the bars ("72%")
};

struct DemoSector
{
	std::string cls;   // "cell" + owner/feature classes
	std::string label; // town/feature initials, mostly empty
};

std::string Pct(int v, int max) { return std::to_string(std::clamp(max ? v * 100 / max : 0, 0, 100)) + "%"; }

std::vector<DemoMerc> FakeMercs()
{
	struct Row { char const* name; int face; int hp, hpMax, ap, breath, morale; char const* assign; char const* loc;
		char const* dest; char const* contract; char const* weapon; char const* ammo; char const* state; };
	static Row const rows[] = {
		{ "Ivan",    7, 88, 92, 21, 84, 78, "Squad 1",  "B13", "",     "11d",  "AK-74",       "30/30", "ok" },
		{ "Lynx",    2, 61, 80, 18, 70, 64, "Squad 1",  "B13", "",     "4d",   "Barrett",     "8/10",  "wounded" },
		{ "Grizzly", 3, 17, 95,  9, 35, 41, "Squad 1",  "B13", "",     "9d",   "M-60",        "40/100","critical" },
		{ "Shadow", 10, 74, 74, 24, 95, 90, "Squad 1",  "B13", "",     "2d",   "H&K MP5K",    "25/30", "ok" },
		{ "Fox",    14, 66, 70, 20, 22, 55, "Squad 1",  "B13", "",     "13d",  "Glock 18",    "15/19", "tired" },
		{ "Red",    11, 90, 90, 22, 90, 72, "Squad 1",  "B13", "",     "6d",   "C-7",         "30/30", "ok" },
		{ "Barry",   0, 80, 85,  0, 12, 60, "Sleep",    "A9",  "",     "7d",   "Mini Uzi",    "20/32", "asleep" },
		{ "Igor",    9, 70, 83,  0, 80, 68, "Train",    "D13", "",     "20d",  "SKS",         "10/10", "ok" },
		{ "Fidel",  13, 77, 88,  0, 90, 49, "Squad 2",  "C13", "D14",  "1d",   "FN FAL",      "20/20", "moving" },
	};
	auto moraleText = [](int m) { return m >= 80 ? "Great" : m >= 60 ? "Good" : m >= 45 ? "Stable" : m >= 30 ? "Poor" : "Awful"; };
	std::vector<DemoMerc> out;
	for (Row const& r : rows)
	{
		DemoMerc m;
		m.name = r.name;
		m.initials = std::string(r.name).substr(0, 2);
		m.face = HasImageProvider() ? "face-" + std::to_string(r.face) : "gen-silhouette";
		m.hp = r.hp; m.hpMax = r.hpMax; m.ap = r.ap; m.breath = r.breath; m.morale = r.morale;
		m.assignment = r.assign; m.location = r.loc; m.destination = r.dest; m.contract = r.contract;
		m.weapon = r.weapon; m.ammo = r.ammo; m.state = r.state; m.moraleText = moraleText(r.morale);
		m.hpPct = Pct(r.hp, 100); m.breathPct = Pct(r.breath, 100); m.moralePct = Pct(r.morale, 100);
		m.bleedPct = Pct(r.hpMax - r.hp, 100); // lost max: where bandaging can bring it back
		out.push_back(m);
	}
	out[0].selected = true;
	return out;
}

/** A 16x16 Arulco-like ownership map: enough shape to judge the palette, not the real map. */
std::vector<DemoSector> FakeSectors()
{
	std::vector<DemoSector> out(256);
	for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
	{
		DemoSector& s = out[size_t(y) * 16 + x];
		// coastline: west and south-west are sea
		bool const sea = x + y / 3 < 1 || (y > 11 && x < 2) || (y == 15 && x < 5);
		std::string c = "cell";
		if (sea) c += " sea";
		else if (y <= 3 && x >= 7 && x <= 13) c += " player";   // the north-east the player holds (Omerta..Drassen)
		else if (y >= 12 && x <= 6) c += " enemy strong";     // Meduna
		else if ((x * 7 + y * 13) % 9 == 0) c += " enemy";
		else if (y >= 6 && ((x * 5 + y * 3) % 11 == 0)) c += " fog";
		else c += " land";
		s.cls = c;
	}
	struct Town { int x, y; char const* label; };
	static Town const towns[] = { { 8, 0, "OM" }, { 12, 1, "DR" }, { 12, 2, "DR" }, { 7, 5, "CA" }, { 13, 7, "AL" },
		{ 2, 5, "GR" }, { 8, 8, "TX" }, { 10, 10, "BA" }, { 2, 11, "ME" }, { 3, 13, "ME" }, { 4, 2, "SM" } };
	for (Town const& t : towns)
	{
		DemoSector& s = out[size_t(t.y) * 16 + t.x];
		s.cls += " town";
		s.label = t.label;
	}
	out[size_t(3) * 16 + 14].cls += " sam";
	out[size_t(3) * 16 + 14].label = "SAM";
	out[size_t(1) * 16 + 12].cls += " selected";
	return out;
}

std::string ReadText(std::string const& path)
{
	std::ifstream f(path, std::ios::binary);
	if (!f) throw std::runtime_error("style demo: cannot read " + path);
	return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}


class StyleDemoScreen final : public Screen
{
public:
	StyleDemoScreen(SDL_Renderer* r, std::string direction, std::string screen) :
		Screen(SaveListModel{}), m_renderer(r), m_ri(r), m_direction(std::move(direction)), m_screen(std::move(screen))
	{
		if (std::find(std::begin(StyleDirections), std::end(StyleDirections), m_direction) == std::end(StyleDirections))
			throw std::runtime_error("style demo: unknown direction " + m_direction);
		if (std::find(std::begin(StyleScreens), std::end(StyleScreens), m_screen) == std::end(StyleScreens))
			throw std::runtime_error("style demo: unknown screen " + m_screen);
		InitRmlOnce();
		std::string const dir = StyleDir();
		LoadUiFonts();

		static int counter = 0;
		m_name = "styledemo" + std::to_string(counter++);
		m_ctx = Rml::CreateContext(m_name, { 1920, 1080 }, &m_ri);
		if (!m_ctx) throw std::runtime_error("RmlUi: CreateContext failed");

		m_mercs = FakeMercs();
		m_sectors = FakeSectors();
		bindModel();

		// The theme link is a placeholder in the shared markup; each direction is one RCSS file.
		std::string rml = ReadText(dir + "/mocks/" + m_screen + ".rml");
		std::string const placeholder = "theme.rcss";
		if (auto at = rml.find(placeholder); at != std::string::npos) rml.replace(at, placeholder.size(), "nightops.rcss");
		m_doc = m_ctx->LoadDocumentFromMemory(rml, "mocks/" + m_screen + ".rml"); // relative: see StyleFileInterface
		if (!m_doc) throw std::runtime_error("RmlUi: cannot load " + m_screen + ".rml");
		m_doc->SetClass("dir-" + m_direction, true);
		m_doc->Show();
	}

	~StyleDemoScreen() override
	{
		Rml::RemoveContext(m_name);
		Rml::ReleaseRenderManagers(); // before m_ri goes away
	}

	char const* toolkit() const override { return "style"; }

	void setSize(int w, int h, float uiScale) override
	{
		m_ctx->SetDimensions({ w, h });
		m_ctx->SetDensityIndependentPixelRatio(std::min(w / 1920.f, h / 1080.f) * uiScale);
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
		if (k == SDLK_ESCAPE) m_model.cancel();
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

private:
	void bindModel()
	{
		Rml::DataModelConstructor c = m_ctx->CreateDataModel("demo");
		if (auto s = c.RegisterStruct<DemoMerc>())
		{
			s.RegisterMember("name",        &DemoMerc::name);
			s.RegisterMember("initials",    &DemoMerc::initials);
			s.RegisterMember("face",        &DemoMerc::face);
			s.RegisterMember("assignment",  &DemoMerc::assignment);
			s.RegisterMember("location",    &DemoMerc::location);
			s.RegisterMember("destination", &DemoMerc::destination);
			s.RegisterMember("contract",    &DemoMerc::contract);
			s.RegisterMember("weapon",      &DemoMerc::weapon);
			s.RegisterMember("ammo",        &DemoMerc::ammo);
			s.RegisterMember("state",       &DemoMerc::state);
			s.RegisterMember("moraleText",  &DemoMerc::moraleText);
			s.RegisterMember("hp",          &DemoMerc::hp);
			s.RegisterMember("hpMax",       &DemoMerc::hpMax);
			s.RegisterMember("ap",          &DemoMerc::ap);
			s.RegisterMember("breath",      &DemoMerc::breath);
			s.RegisterMember("morale",      &DemoMerc::morale);
			s.RegisterMember("selected",    &DemoMerc::selected);
			s.RegisterMember("hpPct",       &DemoMerc::hpPct);
			s.RegisterMember("breathPct",   &DemoMerc::breathPct);
			s.RegisterMember("moralePct",   &DemoMerc::moralePct);
			s.RegisterMember("bleedPct",    &DemoMerc::bleedPct);
		}
		c.RegisterArray<std::vector<DemoMerc>>();
		if (auto s = c.RegisterStruct<DemoSector>())
		{
			s.RegisterMember("cls",   &DemoSector::cls);
			s.RegisterMember("label", &DemoSector::label);
		}
		c.RegisterArray<std::vector<DemoSector>>();
		c.Bind("mercs",   &m_mercs);
		c.Bind("sectors", &m_sectors);
		c.Bind("direction", &m_direction);
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

	SDL_Renderer*           m_renderer;
	SdlRenderInterface      m_ri;
	std::string             m_direction, m_screen, m_name;
	Rml::Context*           m_ctx = nullptr;
	Rml::ElementDocument*   m_doc = nullptr;
	std::vector<DemoMerc>   m_mercs;
	std::vector<DemoSector> m_sectors;
};

} // namespace

std::unique_ptr<Screen> CreateStyleDemoScreen(SDL_Renderer* r, std::string const& direction, std::string const& screen)
{
	return std::make_unique<StyleDemoScreen>(r, direction, screen);
}

} // namespace spike
