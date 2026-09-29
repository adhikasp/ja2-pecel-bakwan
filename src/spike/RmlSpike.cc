// The save/load spike screen in RmlUi: savelist.rml + savelist.rcss (embedded, overridable on disk) bound
// to SaveListModel through an RmlUi data model. Rendering goes through a small SDL_Renderer render interface
// (derived from RmlUi's Backends/RmlUi_Renderer_SDL.cpp, without SDL_image).
#include "UiSpike.h"
#include "RmlCommon.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>

namespace spike {
namespace {

class RmlScreen final : public Screen
{
public:
	RmlScreen(SDL_Renderer* r, SaveListModel m) : Screen(std::move(m)), m_renderer(r), m_ri(r)
	{
		InitRmlOnce();
		static int counter = 0;
		m_name = "uispike" + std::to_string(counter++);
		m_ctx = Rml::CreateContext(m_name, { 1920, 1080 }, &m_ri);
		if (!m_ctx) throw std::runtime_error("RmlUi: CreateContext failed");
		bindModel();
		// The RCSS is referenced from the RML by name; inline it so both come from the same asset source.
		std::string rml = LoadAssetText("savelist.rml");
		std::string const css = LoadAssetText("savelist.rcss");
		std::string const link = "<link type=\"text/rcss\" href=\"savelist.rcss\"/>";
		auto const at = rml.find(link);
		if (at != std::string::npos) rml.replace(at, link.size(), "<style>" + css + "</style>");
		m_doc = m_ctx->LoadDocumentFromMemory(rml, "savelist.rml");
		if (!m_doc) throw std::runtime_error("RmlUi: cannot load savelist.rml");
		m_doc->Show();
	}

	~RmlScreen() override
	{
		Rml::RemoveContext(m_name);
		Rml::ReleaseRenderManagers(); // before m_ri goes away
	}

	char const* toolkit() const override { return "rml"; }

	void setSize(int w, int h, float uiScale) override
	{
		m_w = w;
		m_h = h;
		m_ctx->SetDimensions({ w, h });
		m_ctx->SetDensityIndependentPixelRatio(std::min(w / 1920.f, h / 1080.f) * uiScale);
	}

	void mouseMove(float x, float y) override
	{
		m_mx = x;
		m_my = y;
		m_ctx->ProcessMouseMove(int(x), int(y), 0);
	}
	void mouseButton(float x, float y, bool down) override
	{
		mouseMove(x, y);
		if (down) m_ctx->ProcessMouseButtonDown(0, 0);
		else      m_ctx->ProcessMouseButtonUp(0, 0);
	}
	void wheel(float dy) override { m_ctx->ProcessMouseWheel(Rml::Vector2f(0, dy), 0); }

	void key(SDL_Keycode k) override
	{
		switch (k)
		{
			case SDLK_ESCAPE: m_model.cancel(); break;
			case SDLK_RETURN: if (m_model.modal) m_model.confirmDelete(); else m_model.load(); break;
			case SDLK_DELETE: m_model.askDelete(); break;
			case SDLK_DOWN:   m_model.select(std::min(m_model.selected + 1, int(m_model.slots.size()) - 1)); scrollToSelected(); break;
			case SDLK_UP:     m_model.select(std::max(m_model.selected - 1, 0)); scrollToSelected(); break;
			default: return;
		}
		m_handle.DirtyAllVariables();
	}

	void update(double seconds) override
	{
		RmlClock().now += seconds;
		// The tooltip follows the mouse; place it in dp so the RCSS stays resolution independent.
		float const dp = m_ctx->GetDensityIndependentPixelRatio();
		float const tipX = (m_mx + 18 * dp) / dp;
		float const tipY = (m_my + 22 * dp) / dp;
		if (tipX != m_tipX || tipY != m_tipY)
		{
			m_tipX = tipX;
			m_tipY = tipY;
			m_handle.DirtyVariable("tip_x");
			m_handle.DirtyVariable("tip_y");
		}
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
		Rml::DataModelConstructor c = m_ctx->CreateDataModel("saves");
		if (auto s = c.RegisterStruct<SaveSlot>())
		{
			s.RegisterMember("name",   &SaveSlot::name);
			s.RegisterMember("sector", &SaveSlot::sector);
			s.RegisterMember("day",    &SaveSlot::day);
			s.RegisterMember("hour",   &SaveSlot::hour);
			s.RegisterMember("minute", &SaveSlot::minute);
			s.RegisterMember("money",  &SaveSlot::money);
			s.RegisterMember("mercs",  &SaveSlot::mercs);
			s.RegisterMember("when",   &SaveSlot::when);
		}
		c.RegisterArray<std::vector<SaveSlot>>();
		c.Bind("slots",    &m_model.slots);
		c.Bind("selected", &m_model.selected);
		c.Bind("hovered",  &m_model.hovered);
		c.Bind("modal",    &m_model.modal);
		c.Bind("status",   &m_model.status);
		c.Bind("tip_x",    &m_tipX);
		c.Bind("tip_y",    &m_tipY);
		c.BindFunc("tip", [this](Rml::Variant& v) { v = m_model.tooltip(m_model.hovered); });
		c.BindFunc("selected_name", [this](Rml::Variant& v) {
			v = m_model.selected >= 0 ? m_model.slots[m_model.selected].name : Rml::String();
		});
		auto const command = [this](char const* name, auto fn) {
			m_ctxCommands.push_back(name);
			return [this, fn](Rml::DataModelHandle h, Rml::Event&, Rml::VariantList const& args) {
				fn(args);
				h.DirtyAllVariables();
			};
		};
		c.BindEventCallback("select",  command("select",  [this](auto const& a) { m_model.select(a.empty() ? -1 : a[0].template Get<int>()); }));
		c.BindEventCallback("open",    command("open",    [this](auto const& a) { m_model.select(a.empty() ? -1 : a[0].template Get<int>()); m_model.load(); }));
		c.BindEventCallback("hover",   command("hover",   [this](auto const& a) { m_model.hovered = a.empty() ? -1 : a[0].template Get<int>(); }));
		c.BindEventCallback("unhover", command("unhover", [this](auto const&)   { m_model.hovered = -1; }));
		c.BindEventCallback("load",    command("load",    [this](auto const&)   { m_model.load(); }));
		c.BindEventCallback("ask_delete", command("ask_delete", [this](auto const&) { m_model.askDelete(); }));
		c.BindEventCallback("confirm", command("confirm", [this](auto const&)   { m_model.confirmDelete(); }));
		c.BindEventCallback("cancel",  command("cancel",  [this](auto const&)   { m_model.cancel(); }));
		m_handle = c.GetModelHandle();
	}

	void scrollToSelected()
	{
		if (m_model.selected < 0) return;
		m_ctx->Update(); // make sure the rows exist
		if (Rml::Element* e = m_doc->GetElementById("slot" + std::to_string(m_model.selected))) e->ScrollIntoView(false);
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

	SDL_Renderer*            m_renderer;
	SdlRenderInterface       m_ri;
	std::string              m_name;
	Rml::Context*            m_ctx = nullptr;
	Rml::ElementDocument*    m_doc = nullptr;
	Rml::DataModelHandle     m_handle;
	std::vector<std::string> m_ctxCommands;
	int   m_w = 0, m_h = 0;
	float m_mx = -1000, m_my = -1000;
	float m_tipX = 0, m_tipY = 0;
};

} // namespace

std::unique_ptr<Screen> CreateRmlScreen(SDL_Renderer* r, SaveListModel m)
{
	return std::make_unique<RmlScreen>(r, std::move(m));
}

} // namespace spike
