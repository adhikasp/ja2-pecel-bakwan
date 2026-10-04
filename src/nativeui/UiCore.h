#pragma once
// The native UI's toolkit layer (docs/plan/native-modern-game.md, Phase 2): RmlUi on an SDL_Renderer. Nothing here
// knows the game, so the game (src/game/NativeUI), the ja2-spike tool and the unit tests share it.
//
// - SdlRenderInterface draws RmlUi through any SDL_Renderer. On a GPU renderer it uses SDL_RenderGeometry; on SDL's
//   software renderer, or on a plain ARGB8888 surface (SetSoftwareTarget), it rasterizes the geometry itself,
//   watertight and with premultiplied alpha, so a transparent layer can be composed over the game afterwards.
// - The design system (assets/ui: tokens, components, icons, fonts) is read from StyleDir() at runtime. Style
//   sheets go through the token preprocessor (ExpandTokens), icons are SVGs rasterized at load.

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nui {

class SdlRenderInterface : public Rml::RenderInterface
{
public:
	/** Draws through @a r. A software renderer on an ARGB8888/XRGB8888 surface is rasterized directly. */
	explicit SdlRenderInterface(SDL_Renderer* r);
	/** Draws into an ARGB8888 surface with premultiplied alpha, with no SDL_Renderer at all (textures stay on the
	 * CPU). The surface must outlive the interface or be replaced with SetSoftwareTarget. */
	explicit SdlRenderInterface(SDL_Surface* target);
	~SdlRenderInterface() override;

	void SetSoftwareTarget(SDL_Surface* target);
	SDL_Surface* SoftwareTarget() const { return m_target; }
	bool IsSoftware() const { return m_target != nullptr; }
	SDL_Renderer* Renderer() const { return m_r; }

	/** Union of what was drawn since the last call (software targets; empty when nothing was drawn). */
	SDL_Rect TakeDrawnBounds();

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle g) override;
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f t, Rml::TextureHandle texture) override;
	/** "face-<n>" and other host images ask the image provider (SetImageProvider); "gen-<name>" is a procedural
	 * texture (grain, scan, hatch, grid, topo, vignette, ...); "icon-<name>[@px]" is an SVG icon of the design system.
	 * There is no file loading: game art never comes from the repository. */
	Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> src, Rml::Vector2i dim) override;
	void ReleaseTexture(Rml::TextureHandle t) override;
	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei r) override;

private:
	struct Geometry { Rml::Span<const Rml::Vertex> vertices; Rml::Span<const int> indices; };
	struct CpuTexture { int w = 0, h = 0; bool repeat = false; std::vector<unsigned char> rgba; SDL_Texture* gpu = nullptr; };
	CpuTexture* MakeTexture(unsigned char const* rgba, int w, int h, bool repeat, bool nearest = false);
	void Rasterize(Geometry const&, Rml::Vector2f t, CpuTexture const*);

	SDL_Renderer*           m_r = nullptr;
	SDL_Surface*            m_target = nullptr; // software: we rasterize ourselves
	SDL_Rect                m_scissor{};
	bool                    m_scissorOn = false;
	SDL_Rect                m_drawn{ 0, 0, 0, 0 };
	std::vector<SDL_Vertex> m_vertices;
	std::vector<CpuTexture*> m_textures;
};

/** Time comes from the host (the game's virtual clock), so animations are deterministic headless. */
class VirtualClock : public Rml::SystemInterface
{
public:
	double now = 0;
	double GetElapsedTime() override { return now; }
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
	void SetClipboardText(const Rml::String& text) override;
	void GetClipboardText(Rml::String& text) override;
	void ActivateKeyboard(Rml::Vector2f caret_position, float line_height) override;
	void DeactivateKeyboard() override;
	int warnings = 0; // warnings and errors logged so far (tests assert documents load cleanly)
	/** Called when a text field gains/loses keyboard focus (the host starts/stops SDL text input). */
	std::function<void(bool active)> onKeyboard;
	/** Receives every warning/error RmlUi logs, besides SDL_Log. */
	std::function<void(std::string const&)> onWarning;
};

VirtualClock& RmlClock();

/** Rml::Initialise with our system and file interfaces, once. */
void InitRml();

/** Loads a font file once (the bytes stay alive for the process). Returns false if the file is missing. */
bool LoadFontFileOnce(std::string const& path, std::string const& family, Rml::Style::FontStyle style,
	Rml::Style::FontWeight weight, bool fallback = false);

/** Registers the design system's faces: Barlow, Barlow Condensed, Share Tech Mono, and the fallback faces for
 * glyphs they lack (Fira Sans: Latin Extended, Cyrillic, Greek; Noto Sans SC: Chinese, when downloaded). */
void LoadUiFonts();

/** Design tokens from <StyleDir>/tokens.rcss ("--name: value;"), and var(--name) substitution in RCSS text. Unknown
 * tokens count as RmlUi warnings. */
std::map<std::string, std::string> const& Tokens();
std::string ExpandTokens(std::string text);

/** Procedural texture ("gen-<name>" in RCSS) as straight-alpha RGBA32. Unknown name -> empty result. */
std::vector<unsigned char> GenerateProcedural(std::string const& name, int& w, int& h, bool& repeat);

/** Rasterizes an icon SVG (the subset the icon set uses) to size x size straight-alpha RGBA, white with
 * coverage in alpha. Empty on error (reason in *error). */
std::vector<unsigned char> RasterizeSvg(std::string const& svg, int size, std::string* error = nullptr);

/** Where the UI (design system, screens, gallery, mocks) is read from. Default: <base path>/ui, or $JA2_UI_DIR. */
void        SetStyleDir(std::string dir);
std::string StyleDir();

/** Supplies images the host owns (merc faces from the player's game data) for sources that are not "gen-" or
 * "icon-". Returns a new RGBA surface (the caller frees it), or nullptr (the image is missing). */
using ImageProvider = std::function<SDL_Surface*(std::string const& name)>;
void         SetImageProvider(ImageProvider);
bool         HasImageProvider();
SDL_Surface* ProvideImage(std::string const& name);

/** Resolves a texture source name (the last path component) to straight-alpha RGBA32 (procedural, SVG icon, or
 * host image). Returns false when unknown or missing. Used by the render interfaces. */
bool ResolveTextureSource(Rml::String const& source, std::vector<unsigned char>& rgba, Rml::Vector2i& dimensions, bool& repeat);

/** RmlUi warnings and errors logged since the last reset. */
int  RmlWarnings();
void ResetRmlWarnings();

/** Reads a text file under StyleDir(); empty if it is missing. */
std::string ReadUiFile(std::string const& relative);

} // namespace nui
