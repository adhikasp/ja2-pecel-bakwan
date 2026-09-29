#pragma once
// RmlUi plumbing shared by the Phase 0 save/load spike (RmlSpike.cc) and the Phase 1 style demos (StyleDemo.cc):
// an SDL_Renderer render interface, the virtual clock and one-time initialisation.

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <vector>

namespace spike {

class SdlRenderInterface : public Rml::RenderInterface
{
public:
	explicit SdlRenderInterface(SDL_Renderer* r);

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle g) override;
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f t, Rml::TextureHandle texture) override;
	/** "face-<n>" asks the image provider (SetImageProvider); "gen-<name>" is a procedural texture (grain, scan,
	 * hatch, grid, topo, vignette). There is no file loading: game art never comes from the repository. */
	Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> src, Rml::Vector2i dim) override;
	void ReleaseTexture(Rml::TextureHandle t) override;
	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei r) override;

private:
	struct Geometry { Rml::Span<const Rml::Vertex> vertices; Rml::Span<const int> indices; };
	struct CpuTexture { int w = 0, h = 0; bool repeat = false; std::vector<unsigned char> rgba; };
	SDL_Texture* MakeTexture(unsigned char const* rgba, int w, int h, bool repeat);
	void Rasterize(Geometry const&, Rml::Vector2f t, SDL_Texture*);

	SDL_Renderer*           m_r;
	SDL_Surface*            m_target = nullptr; // set on the software renderer: we rasterize ourselves
	SDL_Rect                m_scissor{};
	bool                    m_scissorOn = false;
	std::vector<SDL_Vertex> m_vertices;
	std::map<SDL_Texture*, CpuTexture> m_cpu; // every texture, straight-alpha RGBA; also marks repeating ones
};

/** Time comes from the host (the game's virtual clock), so animations are deterministic headless. */
class VirtualClock : public Rml::SystemInterface
{
public:
	double now = 0;
	double GetElapsedTime() override { return now; }
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
	int warnings = 0; // warnings and errors logged so far (tests assert the demos load cleanly)
};

VirtualClock& RmlClock();

/** Rml::Initialise and the spike's Lato faces, once. */
void InitRmlOnce();

/** Loads a font file once (the bytes stay alive for the process). Returns false if the file is missing. */
bool LoadFontFileOnce(std::string const& path, std::string const& family, Rml::Style::FontStyle style,
	Rml::Style::FontWeight weight, bool fallback = false);

/** Design tokens from <StyleDir>/tokens.rcss ("--name: value;"), and var(--name) substitution in RCSS text. */
std::map<std::string, std::string> const& Tokens();
std::string ExpandTokens(std::string text);

/** Registers the design system's faces: Barlow, Barlow Condensed, Share Tech Mono, and the fallback faces for
 * glyphs they lack (Fira Sans: Latin Extended, Cyrillic, Greek; Noto Sans SC: Chinese, when downloaded). */
void LoadUiFonts();

/** Procedural texture as straight-alpha RGBA32 (exposed for tests). Unknown name → empty result. */
std::vector<unsigned char> GenerateProcedural(std::string const& name, int& w, int& h, bool& repeat);

} // namespace spike
