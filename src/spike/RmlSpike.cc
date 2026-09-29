// The save/load spike screen in RmlUi: savelist.rml + savelist.rcss (embedded, overridable on disk) bound
// to SaveListModel through an RmlUi data model. Rendering goes through a small SDL_Renderer render interface
// (derived from RmlUi's Backends/RmlUi_Renderer_SDL.cpp, without SDL_image).
#include "UiSpike.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>

namespace spike {
namespace {

class SdlRenderInterface : public Rml::RenderInterface
{
public:
	// SDL's software rasterizer leaves seams between triangles at fractional coordinates (visible as dark
	// lines along rounded borders), so on that renderer vertices are snapped to whole pixels.
	explicit SdlRenderInterface(SDL_Renderer* r) : m_r(r), m_snap(std::string(SDL_GetRendererName(r)) == SDL_SOFTWARE_RENDERER) {}

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i) override
	{
		return reinterpret_cast<Rml::CompiledGeometryHandle>(new Geometry{ v, i });
	}
	void ReleaseGeometry(Rml::CompiledGeometryHandle g) override { delete reinterpret_cast<Geometry*>(g); }

	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f t, Rml::TextureHandle texture) override
	{
		// RmlUi hands out premultiplied colours. SDL's software renderer does not honour premultiplied blending
		// for geometry, so everything is converted to straight alpha and drawn with SDL_BLENDMODE_BLEND.
		Geometry const& g = *reinterpret_cast<Geometry*>(handle);
		m_vertices.resize(g.vertices.size());
		for (size_t i = 0; i < g.vertices.size(); ++i)
		{
			Rml::Vertex const& v = g.vertices[i];
			float const a = v.colour.alpha / 255.f;
			float const k = a > 0 ? 1.f / (255.f * a) : 0.f;
			m_vertices[i].position  = { v.position.x + t.x, v.position.y + t.y };
			if (m_snap) m_vertices[i].position = { std::round(m_vertices[i].position.x), std::round(m_vertices[i].position.y) };
			m_vertices[i].tex_coord = { v.tex_coord.x, v.tex_coord.y };
			m_vertices[i].color     = { std::min(1.f, v.colour.red * k), std::min(1.f, v.colour.green * k), std::min(1.f, v.colour.blue * k), a };
		}
		SDL_SetRenderDrawBlendMode(m_r, SDL_BLENDMODE_BLEND);
		SDL_RenderGeometry(m_r, reinterpret_cast<SDL_Texture*>(texture), m_vertices.data(), int(m_vertices.size()),
			g.indices.data(), int(g.indices.size()));
	}

	Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return {}; } // no images in the spike

	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> src, Rml::Vector2i dim) override
	{
		std::vector<Rml::byte> straight(src.begin(), src.end());
		for (size_t i = 0; i + 3 < straight.size(); i += 4)
		{
			unsigned const a = straight[i + 3];
			for (int c = 0; c < 3; ++c) straight[i + c] = Rml::byte(a ? std::min(255u, straight[i + c] * 255u / a) : 0);
		}
		SDL_Surface* s = SDL_CreateSurfaceFrom(dim.x, dim.y, SDL_PIXELFORMAT_RGBA32, straight.data(), dim.x * 4);
		SDL_Texture* tex = SDL_CreateTextureFromSurface(m_r, s);
		SDL_DestroySurface(s);
		if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
		return reinterpret_cast<Rml::TextureHandle>(tex);
	}
	void ReleaseTexture(Rml::TextureHandle t) override { SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(t)); }

	void EnableScissorRegion(bool enable) override
	{
		m_scissorOn = enable;
		SDL_SetRenderClipRect(m_r, enable ? &m_scissor : nullptr);
	}
	void SetScissorRegion(Rml::Rectanglei r) override
	{
		m_scissor = { r.Left(), r.Top(), r.Width(), r.Height() };
		if (m_scissorOn) SDL_SetRenderClipRect(m_r, &m_scissor);
	}

private:
	struct Geometry { Rml::Span<const Rml::Vertex> vertices; Rml::Span<const int> indices; };
	SDL_Renderer*           m_r;
	bool                    m_snap;
	SDL_Rect                m_scissor{};
	bool                    m_scissorOn = false;
	std::vector<SDL_Vertex> m_vertices;
};

/** Time comes from the host (the game's virtual clock), so animations are deterministic headless. */
class VirtualClock : public Rml::SystemInterface
{
public:
	double now = 0;
	double GetElapsedTime() override { return now; }
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
	{
		if (type <= Rml::Log::LT_WARNING) SDL_Log("RmlUi: %s", message.c_str());
		return true;
	}
};

VirtualClock& Clock()
{
	static VirtualClock clock;
	return clock;
}

void InitRmlOnce()
{
	static bool done = false;
	if (done) return;
	done = true;
	Rml::SetSystemInterface(&Clock());
	Rml::Initialise();
	// Font data must outlive Rml::Shutdown, which we never call: static storage.
	static std::vector<unsigned char> regular = LoadAssetBytes("LatoLatin-Regular.ttf");
	static std::vector<unsigned char> bold    = LoadAssetBytes("LatoLatin-Bold.ttf");
	Rml::LoadFontFace({ regular.data(), regular.size() }, "Lato", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true);
	Rml::LoadFontFace({ bold.data(), bold.size() }, "Lato", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold);
}


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
		Clock().now += seconds;
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
