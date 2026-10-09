#include "UiCore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>

namespace nui {

// ---------------------------------------------------------------------------------------------------------------
// Where the UI lives, and host images

static std::string   g_styleDir;
static ImageProvider g_imageProvider;

void SetStyleDir(std::string dir) { g_styleDir = std::move(dir); }

std::string StyleDir()
{
	std::string d = g_styleDir;
	if (d.empty())
	{
		if (char const* env = std::getenv("JA2_UI_DIR")) d = env;
	}
	if (d.empty())
	{
		char const* base = SDL_GetBasePath();
		d = std::string(base ? base : "./") + "ui";
	}
	std::replace(d.begin(), d.end(), '\\', '/');
	while (!d.empty() && d.back() == '/') d.pop_back();
	return d;
}

void SetImageProvider(ImageProvider p) { g_imageProvider = std::move(p); }
bool HasImageProvider() { return bool(g_imageProvider); }
SDL_Surface* ProvideImage(std::string const& name) { return g_imageProvider ? g_imageProvider(name) : nullptr; }

std::string ReadUiFile(std::string const& relative)
{
	std::ifstream f(StyleDir() + "/" + relative, std::ios::binary);
	if (!f) return {};
	return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

// ---------------------------------------------------------------------------------------------------------------
// Render interface

// On a software target (headless screenshots, the game's frame-buffer path) RmlUi's geometry is rasterized here
// instead of by SDL_RenderGeometry, whose software rasterizer is not watertight: rounded boxes and snapped quads
// showed stray lines and seams. This one uses edge functions with a top-left fill rule, so triangles that share an
// edge cover every pixel exactly once. The target holds premultiplied ARGB, so a transparent layer (a modal's scrim,
// a toast) can be composed over the game afterwards. GPU renderers keep using SDL_RenderGeometry.
SdlRenderInterface::SdlRenderInterface(SDL_Renderer* r) : m_r(r)
{
	if (std::string(SDL_GetRendererName(r)) == SDL_SOFTWARE_RENDERER)
	{
		SDL_Surface* s = static_cast<SDL_Surface*>(SDL_GetPointerProperty(SDL_GetRendererProperties(r), SDL_PROP_RENDERER_SURFACE_POINTER, nullptr));
		if (s && (s->format == SDL_PIXELFORMAT_ARGB8888 || s->format == SDL_PIXELFORMAT_XRGB8888)) m_target = s;
	}
}

SdlRenderInterface::SdlRenderInterface(SDL_Surface* target)
{
	SetSoftwareTarget(target);
}

SdlRenderInterface::~SdlRenderInterface()
{
	for (CpuTexture* t : m_textures)
	{
		if (t->gpu) SDL_DestroyTexture(t->gpu);
		delete t;
	}
}

void SdlRenderInterface::SetSoftwareTarget(SDL_Surface* target)
{
	if (target && target->format != SDL_PIXELFORMAT_ARGB8888 && target->format != SDL_PIXELFORMAT_XRGB8888)
		throw std::runtime_error("ui: a software target must be ARGB8888");
	m_target = target;
	m_drawn = { 0, 0, 0, 0 };
}

SDL_Rect SdlRenderInterface::TakeDrawnBounds()
{
	SDL_Rect const r = m_drawn;
	m_drawn = { 0, 0, 0, 0 };
	return r;
}

Rml::CompiledGeometryHandle SdlRenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> v, Rml::Span<const int> i)
{
	return reinterpret_cast<Rml::CompiledGeometryHandle>(new Geometry{ v, i });
}

void SdlRenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle g)
{
	delete reinterpret_cast<Geometry*>(g);
}

namespace {
struct RVertex { float x, y, u, v, r, g, b, a; };

/** Edge function evaluated the same way for both triangles sharing the edge (endpoints in canonical order), so
 * the two results are exact negatives and the fill rule decides ownership. */
inline float Edge(RVertex const& p, RVertex const& q, float x, float y, bool& flipped)
{
	flipped = (q.y < p.y) || (q.y == p.y && q.x < p.x);
	RVertex const& a = flipped ? q : p;
	RVertex const& b = flipped ? p : q;
	float const e = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
	return flipped ? -e : e;
}
}

void SdlRenderInterface::Rasterize(Geometry const& g, Rml::Vector2f t, CpuTexture const* tex)
{
	SDL_Surface* const dst = m_target;
	int clipL = 0, clipT = 0, clipR = dst->w, clipB = dst->h;
	if (m_scissorOn)
	{
		clipL = std::max(clipL, m_scissor.x);
		clipT = std::max(clipT, m_scissor.y);
		clipR = std::min(clipR, m_scissor.x + m_scissor.w);
		clipB = std::min(clipB, m_scissor.y + m_scissor.h);
	}
	auto const sample = [&](float u, float v, float out[4]) {
		// bilinear, clamped or wrapped
		float const fx = u * tex->w - 0.5f, fy = v * tex->h - 0.5f;
		int const x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
		float const ax = fx - x0, ay = fy - y0;
		auto const fetch = [&](int x, int y, float w) {
			if (tex->repeat) { x = ((x % tex->w) + tex->w) % tex->w; y = ((y % tex->h) + tex->h) % tex->h; }
			else { x = std::clamp(x, 0, tex->w - 1); y = std::clamp(y, 0, tex->h - 1); }
			unsigned char const* p = &tex->rgba[(size_t(y) * tex->w + x) * 4];
			// premultiply while filtering, so transparent texels do not bleed their colour
			float const a = p[3] / 255.f * w;
			out[0] += p[0] / 255.f * a; out[1] += p[1] / 255.f * a; out[2] += p[2] / 255.f * a; out[3] += a;
		};
		out[0] = out[1] = out[2] = out[3] = 0;
		fetch(x0, y0, (1 - ax) * (1 - ay));
		fetch(x0 + 1, y0, ax * (1 - ay));
		fetch(x0, y0 + 1, (1 - ax) * ay);
		fetch(x0 + 1, y0 + 1, ax * ay);
	};

	RVertex tri[3];
	for (size_t i = 0; i + 2 < g.indices.size(); i += 3)
	{
		for (int k = 0; k < 3; ++k)
		{
			Rml::Vertex const& v = g.vertices[g.indices[i + k]];
			// RmlUi colours are premultiplied
			tri[k] = { v.position.x + t.x, v.position.y + t.y, v.tex_coord.x, v.tex_coord.y,
				v.colour.red / 255.f, v.colour.green / 255.f, v.colour.blue / 255.f, v.colour.alpha / 255.f };
		}
		float const area = (tri[1].x - tri[0].x) * (tri[2].y - tri[0].y) - (tri[2].x - tri[0].x) * (tri[1].y - tri[0].y);
		if (area == 0) continue;
		if (area < 0) std::swap(tri[1], tri[2]); // counter-clockwise in y-down: positive edge functions inside
		float const inv = 1.f / std::abs(area);
		// texture footprint of one pixel (in uv), for minification
		int taps = 1;
		float du = 0, dv = 0;
		if (tex)
		{
			float const uvArea = std::abs((tri[1].u - tri[0].u) * (tri[2].v - tri[0].v) - (tri[2].u - tri[0].u) * (tri[1].v - tri[0].v));
			float const texelsPerPixel = std::sqrt(uvArea * tex->w * tex->h / std::abs(area));
			if (texelsPerPixel > 1.25f)
			{
				taps = std::min(4, int(std::ceil(texelsPerPixel)));
				du = texelsPerPixel / tex->w;
				dv = texelsPerPixel / tex->h;
			}
		}
		int const minX = std::max(clipL, int(std::floor(std::min({ tri[0].x, tri[1].x, tri[2].x }))));
		int const maxX = std::min(clipR - 1, int(std::ceil(std::max({ tri[0].x, tri[1].x, tri[2].x }))));
		int const minY = std::max(clipT, int(std::floor(std::min({ tri[0].y, tri[1].y, tri[2].y }))));
		int const maxY = std::min(clipB - 1, int(std::ceil(std::max({ tri[0].y, tri[1].y, tri[2].y }))));
		if (minX > maxX || minY > maxY) continue;
		SDL_Rect const box{ minX, minY, maxX - minX + 1, maxY - minY + 1 };
		if (m_drawn.w <= 0) m_drawn = box;
		else SDL_GetRectUnion(&m_drawn, &box, &m_drawn);
		for (int y = minY; y <= maxY; ++y)
		{
			auto* row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(dst->pixels) + size_t(y) * dst->pitch);
			float const py = y + 0.5f;
			for (int x = minX; x <= maxX; ++x)
			{
				float const px = x + 0.5f;
				bool f0, f1, f2;
				float const w0 = Edge(tri[1], tri[2], px, py, f0);
				float const w1 = Edge(tri[2], tri[0], px, py, f1);
				float const w2 = Edge(tri[0], tri[1], px, py, f2);
				// on an edge, the triangle whose edge runs in canonical direction owns the pixel
				if (w0 < 0 || w1 < 0 || w2 < 0) continue;
				if ((w0 == 0 && f0) || (w1 == 0 && f1) || (w2 == 0 && f2)) continue;
				float const b0 = w0 * inv, b1 = w1 * inv, b2 = w2 * inv;
				float r = tri[0].r * b0 + tri[1].r * b1 + tri[2].r * b2;
				float gg = tri[0].g * b0 + tri[1].g * b1 + tri[2].g * b2;
				float b = tri[0].b * b0 + tri[1].b * b1 + tri[2].b * b2;
				float a = tri[0].a * b0 + tri[1].a * b1 + tri[2].a * b2;
				if (tex)
				{
					float s[4] = { 0, 0, 0, 0 };
					float const u = tri[0].u * b0 + tri[1].u * b1 + tri[2].u * b2, v = tri[0].v * b0 + tri[1].v * b1 + tri[2].v * b2;
					if (taps == 1) sample(u, v, s);
					else
					{
						// box filter over the pixel's footprint in the texture (icons and faces drawn smaller than their size)
						for (int j = 0; j < taps; ++j)
							for (int i = 0; i < taps; ++i)
							{
								float tt[4];
								sample(u + ((i + 0.5f) / taps - 0.5f) * du, v + ((j + 0.5f) / taps - 0.5f) * dv, tt);
								for (int c = 0; c < 4; ++c) s[c] += tt[c] / float(taps * taps);
							}
					}
					// premultiplied vertex colour x premultiplied texel
					r *= s[0]; gg *= s[1]; b *= s[2]; a *= s[3];
				}
				if (a <= 0.f) continue;
				// premultiplied "over": out = src + dst * (1 - src alpha), alpha included
				Uint32 const d = row[x];
				float const k = 1.f - std::min(a, 1.f);
				auto const blend = [&](float src, int shift) {
					float const dc = ((d >> shift) & 0xFF) / 255.f;
					return Uint32(std::clamp(int((src + dc * k) * 255.f + 0.5f), 0, 255)) << shift;
				};
				row[x] = blend(a, 24) | blend(r, 16) | blend(gg, 8) | blend(b, 0);
			}
		}
	}
}

void SdlRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f t, Rml::TextureHandle texture)
{
	Geometry const& g = *reinterpret_cast<Geometry*>(handle);
	CpuTexture const* const tex = reinterpret_cast<CpuTexture const*>(texture);
	if (m_target)
	{
		if (m_r) SDL_FlushRenderer(m_r); // keep the order with anything drawn through SDL (the clear)
		Rasterize(g, t, tex);
		return;
	}
	if (!m_r) return;
	// GPU renderers: RmlUi hands out premultiplied colours; convert to straight alpha for SDL_BLENDMODE_BLEND.
	m_vertices.resize(g.vertices.size());
	for (size_t i = 0; i < g.vertices.size(); ++i)
	{
		Rml::Vertex const& v = g.vertices[i];
		float const a = v.colour.alpha / 255.f;
		float const k = a > 0 ? 1.f / (255.f * a) : 0.f;
		m_vertices[i].position  = { v.position.x + t.x, v.position.y + t.y };
		m_vertices[i].tex_coord = { v.tex_coord.x, v.tex_coord.y };
		m_vertices[i].color     = { std::min(1.f, v.colour.red * k), std::min(1.f, v.colour.green * k), std::min(1.f, v.colour.blue * k), a };
	}
	SDL_Texture* const gpu = tex ? tex->gpu : nullptr;
	if (tex && !gpu) return;
	bool const wrap = tex && tex->repeat;
	if (wrap) SDL_SetRenderTextureAddressMode(m_r, SDL_TEXTURE_ADDRESS_WRAP, SDL_TEXTURE_ADDRESS_WRAP);
	SDL_SetRenderDrawBlendMode(m_r, SDL_BLENDMODE_BLEND);
	SDL_RenderGeometry(m_r, gpu, m_vertices.data(), int(m_vertices.size()), g.indices.data(), int(g.indices.size()));
	if (wrap) SDL_SetRenderTextureAddressMode(m_r, SDL_TEXTURE_ADDRESS_AUTO, SDL_TEXTURE_ADDRESS_AUTO);
}

SdlRenderInterface::CpuTexture* SdlRenderInterface::MakeTexture(unsigned char const* rgba, int w, int h, bool repeat, bool nearest)
{
	// straight-alpha RGBA32 in; the CPU copy feeds the software rasterizer, the GPU texture the renderer
	auto* t = new CpuTexture{ w, h, repeat, std::vector<unsigned char>(rgba, rgba + size_t(w) * h * 4), nullptr };
	if (m_r && !m_target)
	{
		SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, t->rgba.data(), w * 4);
		t->gpu = s ? SDL_CreateTextureFromSurface(m_r, s) : nullptr;
		if (s) SDL_DestroySurface(s);
		if (!t->gpu) { delete t; return nullptr; }
		SDL_SetTextureBlendMode(t->gpu, SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(t->gpu, nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
	}
	m_textures.push_back(t);
	return t;
}

/** Rasterized icons are cached for the process: the SVG files do not change while it runs. */
static std::string IconPixels(std::string const& icon, int size)
{
	static std::map<std::string, std::string> cache;
	std::string const key = StyleDir() + "|" + icon + "@" + std::to_string(size);
	if (auto it = cache.find(key); it != cache.end()) return it->second;
	std::ifstream in(StyleDir() + "/icons/" + icon + ".svg", std::ios::binary);
	std::string out;
	if (in)
	{
		std::string const svg{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
		std::string error;
		auto const px = RasterizeSvg(svg, size, &error);
		if (px.empty()) SDL_Log("RmlUi: icon %s: %s", icon.c_str(), error.c_str());
		out.assign(px.begin(), px.end());
	}
	if (out.empty())
	{
		SDL_Log("RmlUi: no icon %s", icon.c_str());
		++RmlClock().warnings;
	}
	return cache[key] = out;
}

/** Resolves a texture source name (the last path component of the URL) to straight-alpha RGBA32: the design
 * system's procedural textures ("gen-...") and SVG icons ("icon-...@<px>"), or a host image (merc faces and
 * other art from the player's game data). Returns false when the source is unknown or missing. */
bool ResolveTextureSource(Rml::String const& source, std::vector<unsigned char>& rgba, Rml::Vector2i& dimensions, bool& repeat)
{
	// RmlUi joins the source with the document's path: our name is the last path component.
	std::string const name = source.substr(source.find_last_of("/\\") + 1);
	if (name.rfind("gen-", 0) == 0)
	{
		int w = 0, h = 0;
		rgba = GenerateProcedural(name.substr(4), w, h, repeat);
		dimensions = { w, h };
		return !rgba.empty();
	}
	if (name.rfind("icon-", 0) == 0)
	{
		// icon-<name> or icon-<name>@<px>: the design system's SVG icons, rasterized at load (default 96 px)
		std::string icon = name.substr(5);
		int size = 96;
		if (size_t const at = icon.find('@'); at != std::string::npos) { size = std::clamp(std::atoi(icon.c_str() + at + 1), 8, 512); icon.resize(at); }
		std::string const pixels = IconPixels(icon, size);
		if (pixels.empty()) return false;
		rgba.assign(pixels.begin(), pixels.end());
		dimensions = { size, size };
		repeat = false;
		return true;
	}
	// host images: merc faces and other art from the player's game data
	SDL_Surface* s = ProvideImage(name);
	if (!s) return false;
	SDL_Surface* converted = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
	SDL_DestroySurface(s);
	if (!converted) return false;
	rgba.resize(size_t(converted->w) * converted->h * 4);
	for (int y = 0; y < converted->h; ++y)
		std::copy_n(static_cast<unsigned char const*>(converted->pixels) + size_t(y) * converted->pitch,
			size_t(converted->w) * 4, &rgba[size_t(y) * converted->w * 4]);
	dimensions = { converted->w, converted->h };
	repeat = false;
	SDL_DestroySurface(converted);
	return true;
}

Rml::TextureHandle SdlRenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source)
{
	std::vector<unsigned char> pixels;
	bool repeat = false;
	if (!ResolveTextureSource(source, pixels, dimensions, repeat)) return {};
	return reinterpret_cast<Rml::TextureHandle>(MakeTexture(pixels.data(), dimensions.x, dimensions.y, repeat));
}

Rml::TextureHandle SdlRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> src, Rml::Vector2i dim)
{
	std::vector<Rml::byte> straight(src.begin(), src.end());
	for (size_t i = 0; i + 3 < straight.size(); i += 4)
	{
		unsigned const a = straight[i + 3];
		for (int c = 0; c < 3; ++c) straight[i + c] = Rml::byte(a ? std::min(255u, straight[i + c] * 255u / a) : 0);
	}
	return reinterpret_cast<Rml::TextureHandle>(MakeTexture(straight.data(), dim.x, dim.y, false, true));
}

void SdlRenderInterface::ReleaseTexture(Rml::TextureHandle handle)
{
	auto* t = reinterpret_cast<CpuTexture*>(handle);
	auto it = std::find(m_textures.begin(), m_textures.end(), t);
	if (it == m_textures.end()) return;
	m_textures.erase(it);
	if (t->gpu) SDL_DestroyTexture(t->gpu);
	delete t;
}

void SdlRenderInterface::EnableScissorRegion(bool enable)
{
	m_scissorOn = enable;
	if (m_r && !m_target) SDL_SetRenderClipRect(m_r, enable ? &m_scissor : nullptr);
}

void SdlRenderInterface::SetScissorRegion(Rml::Rectanglei r)
{
	m_scissor = { r.Left(), r.Top(), r.Width(), r.Height() };
	if (m_scissorOn && m_r && !m_target) SDL_SetRenderClipRect(m_r, &m_scissor);
}

// ---------------------------------------------------------------------------------------------------------------
// System interface

bool VirtualClock::LogMessage(Rml::Log::Type type, const Rml::String& message)
{
	if (type <= Rml::Log::LT_WARNING)
	{
		++warnings;
		SDL_Log("RmlUi: %s", message.c_str());
		if (onWarning) onWarning(message);
	}
	return true;
}

void VirtualClock::SetClipboardText(const Rml::String& text)
{
	if (SDL_WasInit(SDL_INIT_VIDEO)) SDL_SetClipboardText(text.c_str());
}

void VirtualClock::GetClipboardText(Rml::String& text)
{
	text.clear();
	if (!SDL_WasInit(SDL_INIT_VIDEO)) return;
	if (char* t = SDL_GetClipboardText())
	{
		text = t;
		SDL_free(t);
	}
}

void VirtualClock::ActivateKeyboard(Rml::Vector2f, float)
{
	if (onKeyboard) onKeyboard(true);
}

void VirtualClock::DeactivateKeyboard()
{
	if (onKeyboard) onKeyboard(false);
}

namespace {
/** Relative paths (documents, style sheets) resolve against the UI directory; RmlUi's URL parsing does not accept
 * Windows drive letters, so documents are loaded by relative name. Style sheets go through the token
 * preprocessor (ExpandTokens): RCSS has no custom properties, so var(--name) is substituted from tokens.rcss. */
class StyleFileInterface : public Rml::FileInterface
{
	struct File { std::string data; size_t pos = 0; };

public:
	Rml::FileHandle Open(const Rml::String& path) override
	{
		bool const absolute = !path.empty() && (path[0] == '/' || (path.size() > 1 && path[1] == ':'));
		std::string const full = absolute ? path : StyleDir() + "/" + path;
		std::ifstream in(full, std::ios::binary);
		if (!in) return {};
		auto* f = new File{ { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() } };
		if (full.size() > 5 && full.compare(full.size() - 5, 5, ".rcss") == 0) f->data = ExpandTokens(f->data);
		return reinterpret_cast<Rml::FileHandle>(f);
	}
	void Close(Rml::FileHandle h) override { delete reinterpret_cast<File*>(h); }
	size_t Read(void* buffer, size_t size, Rml::FileHandle h) override
	{
		File& f = *reinterpret_cast<File*>(h);
		size_t const n = std::min(size, f.data.size() - f.pos);
		std::memcpy(buffer, f.data.data() + f.pos, n);
		f.pos += n;
		return n;
	}
	bool Seek(Rml::FileHandle h, long offset, int origin) override
	{
		File& f = *reinterpret_cast<File*>(h);
		long const base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? long(f.pos) : long(f.data.size());
		long const to = base + offset;
		if (to < 0 || to > long(f.data.size())) return false;
		f.pos = size_t(to);
		return true;
	}
	size_t Tell(Rml::FileHandle h) override { return reinterpret_cast<File*>(h)->pos; }
};
}

std::map<std::string, std::string> const& Tokens()
{
	// read once per style directory; "--name: value;" declarations, comments allowed
	static std::string dir;
	static std::map<std::string, std::string> tokens;
	if (dir == StyleDir() && !tokens.empty()) return tokens;
	dir = StyleDir();
	tokens.clear();
	std::ifstream in(dir + "/tokens.rcss", std::ios::binary);
	std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
	for (size_t c; (c = text.find("/*")) != std::string::npos;)
	{
		size_t const e = text.find("*/", c);
		text.erase(c, e == std::string::npos ? std::string::npos : e + 2 - c);
	}
	size_t p = 0;
	while ((p = text.find("--", p)) != std::string::npos)
	{
		size_t const colon = text.find(':', p), semi = text.find(';', p);
		if (colon == std::string::npos || semi == std::string::npos || colon > semi) break;
		auto trim = [](std::string s) {
			s.erase(0, s.find_first_not_of(" \t\r\n"));
			s.erase(s.find_last_not_of(" \t\r\n") + 1);
			return s;
		};
		tokens[trim(text.substr(p + 2, colon - p - 2))] = trim(text.substr(colon + 1, semi - colon - 1));
		p = semi + 1;
	}
	return tokens;
}

std::string ExpandTokens(std::string text)
{
	auto const& tokens = Tokens();
	for (int pass = 0; pass < 8; ++pass) // tokens may refer to tokens
	{
		bool changed = false;
		size_t p = 0;
		while ((p = text.find("var(--", p)) != std::string::npos)
		{
			size_t const e = text.find(')', p);
			if (e == std::string::npos) break;
			std::string const name = text.substr(p + 6, e - p - 6);
			auto it = tokens.find(name);
			if (it == tokens.end())
			{
				SDL_Log("RmlUi: unknown design token --%s", name.c_str());
				++RmlClock().warnings;
				p = e;
				continue;
			}
			text.replace(p, e + 1 - p, it->second);
			changed = true;
		}
		if (!changed) break;
	}
	return text;
}

VirtualClock& RmlClock()
{
	static VirtualClock clock;
	return clock;
}

int  RmlWarnings()      { return RmlClock().warnings; }
void ResetRmlWarnings() { RmlClock().warnings = 0; }

void InitRml()
{
	static bool done = false;
	if (done) return;
	done = true;
	Rml::SetSystemInterface(&RmlClock());
	static StyleFileInterface files;
	Rml::SetFileInterface(&files);
	Rml::Initialise();
}

void LoadUiFonts()
{
	using S = Rml::Style::FontStyle;
	using W = Rml::Style::FontWeight;
	struct F { char const* file; char const* family; W weight; bool fallback; bool required; };
	static F const fonts[] = {
		{ "barlow/Barlow-Regular.ttf",                    "Barlow",           W::Normal, false, true },
		{ "barlow/Barlow-Medium.ttf",                     "Barlow",           W(500),    false, true },
		{ "barlow/Barlow-SemiBold.ttf",                   "Barlow",           W(600),    false, true },
		{ "barlow/Barlow-Bold.ttf",                       "Barlow",           W::Bold,   false, true },
		{ "barlowcondensed/BarlowCondensed-Medium.ttf",   "Barlow Condensed", W(500),    false, true },
		{ "barlowcondensed/BarlowCondensed-SemiBold.ttf", "Barlow Condensed", W(600),    false, true },
		{ "barlowcondensed/BarlowCondensed-Bold.ttf",     "Barlow Condensed", W::Bold,   false, true },
		{ "sharetechmono/ShareTechMono-Regular.ttf",      "Share Tech Mono",  W::Normal, false, true },
		// Field Kit voices: stencilled crate lettering, embossed plate lettering, typed dossier
		{ "blackopsone/BlackOpsOne-Regular.ttf",          "Black Ops One",    W::Normal, false, true },
		{ "chakrapetch/ChakraPetch-SemiBold.ttf",         "Chakra Petch",     W(600),    false, true },
		{ "chakrapetch/ChakraPetch-Bold.ttf",             "Chakra Petch",     W::Bold,   false, true },
		{ "specialelite/SpecialElite-Regular.ttf",        "Special Elite",    W::Normal, false, true },
		// fallbacks, in order: glyphs the faces above lack (Polish, Russian, ...) and Chinese
		{ "firasans/FiraSans-Regular.ttf",                "Fira Sans",        W::Normal, true,  true },
		{ "firasans/FiraSans-SemiBold.ttf",               "Fira Sans",        W(600),    false, true },
		{ "firasans/FiraSans-Bold.ttf",                   "Fira Sans",        W::Bold,   false, true },
		{ "notosanssc/NotoSansSC[wght].ttf",              "Noto Sans SC",     W::Normal, true,  false },
	};
	std::string const dir = StyleDir() + "/fonts/";
	for (F const& f : fonts)
	{
		if (!LoadFontFileOnce(dir + f.file, f.family, S::Normal, f.weight, f.fallback) && f.required)
			throw std::runtime_error("ui: cannot load font " + dir + f.file);
	}
}

bool LoadFontFileOnce(std::string const& path, std::string const& family, Rml::Style::FontStyle style,
	Rml::Style::FontWeight weight, bool fallback)
{
	static std::map<std::string, bool> loaded;
	static std::deque<std::vector<unsigned char>> storage; // stable addresses
	auto const key = path + "|" + family;
	if (auto it = loaded.find(key); it != loaded.end()) return it->second;
	std::ifstream f(path, std::ios::binary);
	bool ok = false;
	if (f)
	{
		storage.emplace_back(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		auto const& bytes = storage.back();
		ok = Rml::LoadFontFace({ bytes.data(), bytes.size() }, family, style, weight, fallback);
	}
	loaded[key] = ok;
	return ok;
}


namespace {
uint32_t Hash(int x, int y, uint32_t seed)
{
	uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + seed * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return h ^ (h >> 16);
}

float Rand01(int x, int y, uint32_t seed) { return (Hash(x, y, seed) & 0xFFFF) / 65535.f; }

/** Smooth value noise, tiling with the given period (in cells). */
float ValueNoise(float x, float y, int period, uint32_t seed)
{
	int const x0 = int(std::floor(x)), y0 = int(std::floor(y));
	float const fx = x - x0, fy = y - y0;
	auto const v = [&](int i, int j) { return Rand01(((i % period) + period) % period, ((j % period) + period) % period, seed); };
	float const sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
	float const a = v(x0, y0) + (v(x0 + 1, y0) - v(x0, y0)) * sx;
	float const b = v(x0, y0 + 1) + (v(x0 + 1, y0 + 1) - v(x0, y0 + 1)) * sx;
	return a + (b - a) * sy;
}

void Put(std::vector<unsigned char>& px, int w, int x, int y, int r, int g, int b, float a)
{
	size_t const i = (size_t(y) * w + x) * 4;
	px[i] = (unsigned char)r; px[i + 1] = (unsigned char)g; px[i + 2] = (unsigned char)b;
	px[i + 3] = (unsigned char)std::clamp(int(a * 255.f + 0.5f), 0, 255);
}

/** Composites a colour over the pixel (straight alpha, "over"). */
void Over(std::vector<unsigned char>& px, int w, int x, int y, float r, float g, float b, float a)
{
	if (a <= 0) return;
	a = std::min(a, 1.f);
	size_t const i = (size_t(y) * w + x) * 4;
	float const da = px[i + 3] / 255.f, oa = a + da * (1 - a);
	auto const mix = [&](float s, unsigned char d) {
		return (unsigned char)std::clamp(int((s * a + d * da * (1 - a)) / oa + 0.5f), 0, 255);
	};
	px[i] = mix(r, px[i]); px[i + 1] = mix(g, px[i + 1]); px[i + 2] = mix(b, px[i + 2]);
	px[i + 3] = (unsigned char)std::clamp(int(oa * 255.f + 0.5f), 0, 255);
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

/** A slotted screw head (the corner screws of every JA2 panel): a domed steel-brass head lit from the top left,
 * its own drop shadow, a dark rim and the slot at the given angle. */
void Screw(std::vector<unsigned char>& px, int w, int h, float cx, float cy, float r, float angle)
{
	float const sx = std::cos(angle), sy = std::sin(angle);
	for (int y = std::max(0, int(cy - r - 3)); y < std::min(h, int(cy + r + 4)); ++y)
		for (int x = std::max(0, int(cx - r - 3)); x < std::min(w, int(cx + r + 4)); ++x)
		{
			float const dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = std::sqrt(dx * dx + dy * dy);
			float const sd = std::sqrt((dx - 1.2f) * (dx - 1.2f) + (dy - 1.5f) * (dy - 1.5f));
			Over(px, w, x, y, 4, 3, 2, 0.6f * std::clamp(r + 1.2f - sd, 0.f, 1.f)); // drop shadow
			float const cov = std::clamp(r + 0.5f - d, 0.f, 1.f);
			if (cov <= 0) continue;
			float const lit = std::clamp(0.5f - 0.5f * (dx + dy) / (r * 1.2f), 0.f, 1.f); // dome, light top left
			float cr = Lerp(52, 214, lit), cg = Lerp(42, 190, lit), cb = Lerp(28, 136, lit);
			if (d > r - 1.4f) { cr *= 0.45f; cg *= 0.45f; cb *= 0.45f; }               // rim
			float const across = dx * sy - dy * sx;                                       // signed distance to the slot
			if (std::abs(across) < 1.0f && d < r - 1.2f) { cr = 16; cg = 11; cb = 6; }
			else if (across > 0.9f && across < 1.9f && d < r - 1.4f) { cr = std::min(255.f, cr * 1.35f); cg = std::min(255.f, cg * 1.35f); cb = std::min(255.f, cb * 1.3f); }
			Over(px, w, x, y, cr, cg, cb, cov);
		}
}

/** Distance of (x, y) to the nearest edge of a w x h texture, and whether that edge is lit (top or left). */
int EdgeDistance(int x, int y, int w, int h, bool& lit)
{
	int const t = y, l = x, b = h - 1 - y, r = w - 1 - x;
	int const d = std::min({ t, l, b, r });
	lit = d == t || d == l; // the mitred diagonal of the top-right and bottom-left corners counts as lit
	return d;
}
}

std::vector<unsigned char> GenerateProcedural(std::string const& name, int& w, int& h, bool& repeat)
{
	std::vector<unsigned char> px;
	auto size = [&](int W, int H, bool rep) { w = W; h = H; repeat = rep; px.assign(size_t(W) * H * 4, 0); };
	if (name == "grain" || name == "grain-light")
	{
		// paper / film grain: sparse dark (or light) specks over fibre-like low-frequency blotches
		size(256, 256, true);
		bool const light = name == "grain-light";
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float const blotch = ValueNoise(x / 32.f, y / 32.f, 8, 7) * 0.05f;
			float const speck = Rand01(x, y, 3) > 0.985f ? 0.10f : Rand01(x, y, 5) * 0.035f;
			int const c = light ? 255 : 40;
			Put(px, w, x, y, c, light ? 250 : 30, light ? 240 : 10, blotch + speck);
		}
	}
	else if (name == "scan")
	{
		size(4, 4, true); // CRT/NVG scanlines: one dark row in four
		for (int x = 0; x < 4; ++x) Put(px, w, x, 3, 0, 0, 0, 0.22f);
	}
	else if (name == "hatch")
	{
		size(24, 24, true); // hazard / redaction stripes
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
			if ((x + y) % 24 < 9) Put(px, w, x, y, 0, 0, 0, 0.35f);
	}
	else if (name == "mottle")
	{
		// weathered gunmetal grunge (JA2's PANELTEX and panel faces): fine blotchy olive-brown wear with pits and
		// flecks, drawn over a flat surface colour so every panel picks up the wear
		// Field Kit: finer blotches than a camouflage pattern, plus the scratches and scuffs of a field-worn panel
		size(256, 256, true);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float n = 0, amp = 0.5f, f = 1.f / 16;
			int period = 16;
			for (int o = 0; o < 4; ++o, amp *= 0.55f, f *= 2, period *= 2) n += ValueNoise(x * f, y * f, period, 23 + o) * amp;
			n = std::clamp(n + (ValueNoise(x / 64.f, y / 64.f, 4, 19) - 0.5f) * 0.12f, 0.f, 1.f);
			float const speck = Rand01(x, y, 29);
			if (n > 0.55f) Put(px, w, x, y, 196, 172, 122, (n - 0.55f) * 0.34f); // worn light patches
			else if (n < 0.44f) Put(px, w, x, y, 12, 8, 4, (0.44f - n) * 0.62f); // grime
			if (speck > 0.996f) Put(px, w, x, y, 12, 8, 4, 0.34f);              // pits
			else if (speck < 0.005f) Put(px, w, x, y, 214, 192, 146, 0.20f);    // metal flecks
		}
		// scratches: thin bright gouges with a dark lower lip, mostly along one direction, wrapping for the tile
		for (int s = 0; s < 48; ++s)
		{
			float const x0 = Rand01(s, 1, 211) * w, y0 = Rand01(s, 2, 211) * h;
			float const ang = (Rand01(s, 3, 211) < 0.75f ? -0.35f : 1.2f) + (Rand01(s, 4, 211) - 0.5f) * 0.6f;
			float const len = 5 + Rand01(s, 5, 211) * Rand01(s, 6, 211) * 34;
			float const bright = 0.04f + Rand01(s, 7, 211) * 0.07f;
			for (float t = 0; t < len; t += 0.5f)
			{
				int const x = (int(x0 + std::cos(ang) * t) % w + w) % w, y = (int(y0 + std::sin(ang) * t) % h + h) % h;
				float const fade = std::sin(3.14159f * t / len); // tapered ends
				Over(px, w, x, y, 226, 206, 160, bright * fade);
				Over(px, w, x, (y + 1) % h, 6, 4, 2, bright * 0.9f * fade);
			}
		}
	}
	else if (name == "frame")
	{
		// a moulded panel frame for a nine-patch (border 32 texels, drawn at about 20dp: the original frames are
		// 8-10 px at 640x480): black outline, lit outer bevel, a worn brass-olive face with an engraved seam, the
		// inner bevel falling into the panel, the shadow it casts, and a slotted screw in each corner. Lit from the
		// top left like every JA2 frame.
		size(128, 128, false);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			bool lit = false;
			int const d = EdgeDistance(x, y, w, h, lit);
			float const grain = (Rand01(x, y, 307) - 0.5f) * 16;
			if (d < 2) Put(px, w, x, y, 6, 4, 3, 1.f);
			else if (d < 7)
			{
				float const t = (d - 2) / 4.f;
				if (lit) Put(px, w, x, y, int(Lerp(222, 120, t)), int(Lerp(192, 98, t)), int(Lerp(124, 58, t)), 1.f);
				else Put(px, w, x, y, int(Lerp(24, 40, t)), int(Lerp(17, 30, t)), int(Lerp(9, 17, t)), 1.f);
			}
			else if (d < 22)
			{
				// a rounded moulding: brighter towards the outer bevel on the lit sides, darker on the shaded ones
				float const shade = 1.f + 0.22f * std::cos(3.14159f * (d - 7) / 15.f) * (lit ? 1.f : -0.7f);
				float r = 84 * shade + grain, g = 67 * shade + grain * 0.8f, b = 40 * shade + grain * 0.5f;
				if (d == 14 || d == 15) { r *= 0.30f; g *= 0.30f; b *= 0.30f; }          // engraved seam
				else if (d == 16) { r *= 1.5f; g *= 1.45f; b *= 1.35f; }                  // its lit lip
				Put(px, w, x, y, int(std::clamp(r, 0.f, 255.f)), int(std::clamp(g, 0.f, 255.f)), int(std::clamp(b, 0.f, 255.f)), 1.f);
			}
			else if (d < 26)
			{
				float const t = (d - 22) / 3.f;
				if (lit) Put(px, w, x, y, int(Lerp(30, 10, t)), int(Lerp(21, 7, t)), int(Lerp(12, 4, t)), 1.f);
				else Put(px, w, x, y, int(Lerp(176, 104, t)), int(Lerp(148, 84, t)), int(Lerp(94, 48, t)), 1.f);
			}
			else if (d < 27) Put(px, w, x, y, 5, 3, 2, 0.95f);
			else if (x >= 27 && y >= 27 && x < w - 27 && y < h - 27)
			{
				// the shadow the frame casts on the panel, on the top and left only
				float const st = y < 32 ? std::pow(1.f - (y - 27) / 5.f, 2.f) * 0.55f : 0.f;
				float const sl = x < 32 ? std::pow(1.f - (x - 27) / 5.f, 2.f) * 0.55f : 0.f;
				Put(px, w, x, y, 4, 3, 2, std::max(st, sl));
			}
		}
		Screw(px, w, h, 14.5f, 14.5f, 7.f, 0.5f);
		Screw(px, w, h, w - 14.5f, 14.5f, 7.f, 1.9f);
		Screw(px, w, h, 14.5f, h - 14.5f, 7.f, -0.4f);
		Screw(px, w, h, w - 14.5f, h - 14.5f, 7.f, 1.1f);
	}
	else if (name == "frame-plain")
	{
		// a thinner moulding without screws, for small plates (cards, menus, label plates): border 16 texels,
		// drawn at about 8dp
		size(64, 64, false);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			bool lit = false;
			int const d = EdgeDistance(x, y, w, h, lit);
			float const grain = (Rand01(x, y, 311) - 0.5f) * 12;
			if (d < 2) Put(px, w, x, y, 6, 4, 3, 1.f);
			else if (d < 5)
			{
				float const t = (d - 2) / 2.f;
				if (lit) Put(px, w, x, y, int(Lerp(214, 132, t)), int(Lerp(184, 106, t)), int(Lerp(118, 62, t)), 1.f);
				else Put(px, w, x, y, 26, 18, 10, 1.f);
			}
			else if (d < 10)
			{
				float const shade = lit ? 1.12f : 0.8f;
				Put(px, w, x, y, int(std::clamp(78 * shade + grain, 0.f, 255.f)), int(std::clamp(62 * shade + grain * 0.8f, 0.f, 255.f)),
					int(std::clamp(37 * shade + grain * 0.5f, 0.f, 255.f)), 1.f);
			}
			else if (d < 12)
			{
				if (lit) Put(px, w, x, y, 12, 8, 4, 1.f);
				else Put(px, w, x, y, 140, 114, 68, 1.f);
			}
			else if (d < 16 && x >= 12 && y >= 12 && x < w - 12 && y < h - 12)
			{
				float const st = y < 16 ? (1.f - (y - 12) / 4.f) * 0.45f : 0.f;
				float const sl = x < 16 ? (1.f - (x - 12) / 4.f) * 0.45f : 0.f;
				Put(px, w, x, y, 4, 3, 2, std::max(st, sl));
			}
		}
	}
	else if (name == "well")
	{
		// a recess cut into the plate (portrait wells, lists, the map): dark top and left wall with a soft inner
		// shadow, a lit lip on the bottom and right
		size(64, 64, false);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			bool lit = false;
			int const d = EdgeDistance(x, y, w, h, lit);
			if (d < 2) { if (lit) Put(px, w, x, y, 5, 3, 2, 1.f); else Put(px, w, x, y, 150, 124, 76, 0.85f); }
			else if (d < 3) { if (lit) Put(px, w, x, y, 8, 5, 3, 0.9f); else Put(px, w, x, y, 70, 54, 30, 0.7f); }
			else
			{
				float const st = y < 16 ? std::pow(1.f - (y - 3) / 13.f, 2.f) * 0.6f : 0.f;
				float const sl = x < 16 ? std::pow(1.f - (x - 3) / 13.f, 2.f) * 0.6f : 0.f;
				if (x < w - 3 && y < h - 3) Put(px, w, x, y, 3, 2, 1, std::max(st, sl));
			}
		}
	}
	else if (name == "brackets")
	{
		// targeting brackets for the selected thing: amber L corners with a dark keyline, nothing between them
		size(48, 48, false);
		int const arm = 15, th = 4;
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			int const cx = std::min(x, w - 1 - x), cy = std::min(y, h - 1 - y);
			bool const on = (cx < arm && cy < th) || (cy < arm && cx < th);
			bool const key = !on && ((cx < arm + 1 && cy < th + 1) || (cy < arm + 1 && cx < th + 1));
			if (on) Put(px, w, x, y, 242, 194, 48, 1.f);
			else if (key) Put(px, w, x, y, 10, 7, 3, 0.85f);
		}
	}
	else if (name == "hazard")
	{
		// worn hazard stripes (danger, enemy contact): amber and black at 45 degrees, scuffed
		size(40, 40, true);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			bool const amber = (x + y) % 40 < 20;
			float const wear = Rand01(x, y, 401), blot = ValueNoise(x / 5.f, y / 5.f, 8, 403);
			float k = amber ? 1.f : 0.f;
			if (amber && (wear > 0.97f || blot < 0.22f)) k = 0.55f;
			Put(px, w, x, y, int(Lerp(22, 226, k)), int(Lerp(17, 168, k)), int(Lerp(10, 36, k)), 1.f);
		}
	}
	else if (name == "granite")
	{
		// the black speckled stone in the wells of the original options and load screens
		size(256, 256, true);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float const blot = ValueNoise(x / 32.f, y / 32.f, 8, 503) * 0.7f + ValueNoise(x / 4.f, y / 4.f, 64, 509) * 0.3f;
			float const r = Rand01(x, y, 521), cell = Rand01(x / 2, y / 2, 523);
			Put(px, w, x, y, 2, 2, 1, std::clamp((0.5f - blot) * 0.5f, 0.f, 0.3f));
			if (cell > 0.965f) Over(px, w, x, y, 132, 124, 108, 0.12f + (cell - 0.965f) * 6.f);
			else if (r > 0.94f) Over(px, w, x, y, 150, 140, 120, 0.08f + (r - 0.94f) * 3.f);
		}
	}
	else if (name == "doll")
	{
		// the paper doll behind the worn-gear slots: a soldier's front silhouette (head, torso with vest seams, arms,
		// legs, boots) as a stencilled fill with an etched outline and faint cross-hatching. White: tint it with
		// image-color. Built from capsules and ellipses as signed distances, so the edges are anti-aliased.
		size(240, 440, false);
		auto capsule = [](float px_, float py_, float ax, float ay, float bx, float by, float r) {
			float const pax = px_ - ax, pay = py_ - ay, bax = bx - ax, bay = by - ay;
			float const t = std::clamp((pax * bax + pay * bay) / (bax * bax + bay * bay), 0.f, 1.f);
			float const dx = pax - bax * t, dy = pay - bay * t;
			return std::sqrt(dx * dx + dy * dy) - r;
		};
		auto ellipse = [](float px_, float py_, float cx, float cy, float rx, float ry) {
			float const dx = (px_ - cx) / rx, dy = (py_ - cy) / ry;
			return (std::sqrt(dx * dx + dy * dy) - 1.f) * std::min(rx, ry);
		};
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float const fx = x + 0.5f, fy = y + 0.5f;
			float d = ellipse(fx, fy, 120, 46, 25, 31);                                   // head
			d = std::min(d, capsule(fx, fy, 120, 70, 120, 92, 13));                       // neck
			d = std::min(d, capsule(fx, fy, 84, 112, 156, 112, 22));                      // shoulders
			d = std::min(d, capsule(fx, fy, 120, 118, 120, 210, 38));                     // chest and belly
			d = std::min(d, capsule(fx, fy, 104, 214, 136, 214, 30));                     // hips
			d = std::min(d, capsule(fx, fy, 68, 118, 52, 196, 15));                       // upper arms
			d = std::min(d, capsule(fx, fy, 172, 118, 188, 196, 15));
			d = std::min(d, capsule(fx, fy, 52, 196, 44, 262, 12));                       // forearms
			d = std::min(d, capsule(fx, fy, 188, 196, 196, 262, 12));
			d = std::min(d, ellipse(fx, fy, 42, 276, 12, 15));                            // hands
			d = std::min(d, ellipse(fx, fy, 198, 276, 12, 15));
			d = std::min(d, capsule(fx, fy, 103, 236, 98, 330, 19));                      // thighs
			d = std::min(d, capsule(fx, fy, 137, 236, 142, 330, 19));
			d = std::min(d, capsule(fx, fy, 98, 330, 96, 400, 15));                       // shins
			d = std::min(d, capsule(fx, fy, 142, 330, 144, 400, 15));
			d = std::min(d, capsule(fx, fy, 88, 412, 104, 412, 13));                      // boots
			d = std::min(d, capsule(fx, fy, 136, 412, 152, 412, 13));
			float const inside = std::clamp(0.5f - d, 0.f, 1.f);
			float const edge = std::clamp(1.6f - std::abs(d + 1.2f), 0.f, 1.f);          // etched outline
			float a = inside * 0.15f;
			if (inside > 0 && (x + y) % 9 == 0) a += inside * 0.06f;                      // hatching
			bool const seam = (std::abs(fy - 214) < 1.2f && std::abs(fx - 120) < 44)       // belt line
				|| (std::abs(fx - 120) < 1.0f && fy > 100 && fy < 206);                     // vest zip
			if (seam && inside > 0) a += 0.18f;
			a = std::max(a, edge * 0.6f);
			if (a > 0) Put(px, w, x, y, 255, 255, 255, a);
		}
	}
	else if (name == "screw")
	{
		size(32, 32, false); // one screw head, for plates and labels
		Screw(px, w, h, 15.f, 15.f, 10.f, 0.7f);
	}
	else if (name == "leather")
	{
		// dark pebbled leather (JA2 popup and talk-box backgrounds): clumped grain over broad shading
		size(256, 256, true);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float const broad = ValueNoise(x / 96.f, y / 96.f, 8, 31) * 0.6f + ValueNoise(x / 24.f, y / 24.f, 16, 37) * 0.4f;
			float const fine = Rand01(x, y, 41);
			if (fine > 0.72f) Put(px, w, x, y, 214, 186, 132, 0.05f + fine * 0.05f);
			else Put(px, w, x, y, 10, 6, 3, (0.10f + broad * 0.14f) * (0.6f + fine * 0.8f));
		}
	}
	else if (name == "brushed")
	{
		// brushed steel: 2px horizontal streaks (tiling across, repeating bands down)
		size(256, 256, true);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			uint32_t const band = uint32_t(y / 2);
			float const streak = ValueNoise(x / 32.f, 0.f, 8, 101 + band % 97);
			float const tone = 0.5f + (Rand01(0, y / 2, 61) - 0.5f) * 0.9f;
			float const v = std::clamp(tone * 0.6f + streak * 0.4f, 0.f, 1.f);
			if (v > 0.55f) Put(px, w, x, y, 226, 210, 172, (v - 0.55f) * 0.50f);
			else if (v < 0.45f) Put(px, w, x, y, 8, 5, 2, (0.45f - v) * 0.70f);
		}
	}
	else if (name == "wood")
	{
		// varnished board (JA2's map desk and laptop): warm grain lines with a slow warp
		size(256, 256, true);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float const warp = ValueNoise(x / 128.f, y / 32.f, 8, 43) * 6.f;
			float const rings = std::sin((y + warp) * 0.55f) * 0.5f + 0.5f;
			float const fine = ValueNoise(x / 8.f, y / 64.f, 32, 47);
			float const line = std::clamp(rings * 0.7f + fine * 0.3f, 0.f, 1.f);
			if (line > 0.62f) Put(px, w, x, y, 20, 11, 5, (line - 0.62f) * 1.4f);       // dark grain
			else if (line < 0.38f) Put(px, w, x, y, 150, 106, 58, (0.38f - line) * 0.8f); // worn lighter grain
		}
	}
	else if (name == "groove")
	{
		// an engraved seam (JA2 panels are carved with a dark line and a lit lip): tile it along a divider
		size(4, 4, true);
		for (int x = 0; x < 4; ++x)
		{
			Put(px, w, x, 0, 10, 6, 3, 0.55f);
			Put(px, w, x, 1, 196, 168, 112, 0.28f);
		}
	}
	else if (name == "grid")
	{
		size(64, 64, true); // plotting grid: major line every 64, minor every 16
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			if (x == 0 || y == 0) Put(px, w, x, y, 255, 255, 255, 0.10f);
			else if (x % 16 == 0 || y % 16 == 0) Put(px, w, x, y, 255, 255, 255, 0.04f);
		}
	}
	else if (name == "topo" || name == "topo-dark")
	{
		// topographic contour lines from fractal value noise (tiles: the noise period matches the size)
		size(512, 512, true);
		bool const dark = name == "topo-dark";
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float n = 0, amp = 0.6f, f = 1.f / 128;
			int period = 4;
			for (int o = 0; o < 4; ++o, amp *= 0.5f, f *= 2, period *= 2) n += ValueNoise(x * f, y * f, period, 11 + o) * amp;
			float const bands = n * 14.f;
			float const d = std::abs(bands - std::round(bands)); // distance to the nearest contour
			float const line = std::clamp(1.f - d / 0.07f, 0.f, 1.f);
			bool const major = int(std::round(bands)) % 5 == 0;
			int const c = dark ? 30 : 255;
			Put(px, w, x, y, c, dark ? 26 : 255, dark ? 18 : 255, line * (major ? 0.16f : 0.08f));
		}
	}
	else if (name == "vignette")
	{
		size(256, 256, false);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float const dx = (x + 0.5f) / w * 2 - 1, dy = (y + 0.5f) / h * 2 - 1;
			float const d = std::sqrt(dx * dx * 0.8f + dy * dy);
			float const t = std::clamp((d - 0.55f) / 0.75f, 0.f, 1.f);
			Put(px, w, x, y, 0, 0, 0, t * t * (3 - 2 * t) * 0.75f);
		}
	}
	else if (name == "fade-top" || name == "fade-bottom")
	{
		// a vertical ramp from opaque to transparent (white: tint it with image-color), laid over the ends of a
		// scrolling area so text fades out instead of being cut
		size(4, 128, false);
		bool const top = name == "fade-top";
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float const t = (y + 0.5f) / h;
			float const a = top ? 1.f - t : t;
			Put(px, w, x, y, 255, 255, 255, a * a * (3 - 2 * a));
		}
	}
	else if (name == "rays")
	{
		// sunburst from below the bottom centre: 90s travel-poster wedges
		size(1024, 512, false);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float const a = std::atan2(float(h * 1.15f - y), float(x - w / 2));
			bool const on = int(std::floor(a / (3.14159265f / 28))) % 2 == 0;
			if (on) Put(px, w, x, y, 255, 236, 200, 0.05f);
		}
	}
	else if (name == "dots")
	{
		size(10, 10, true); // halftone dot
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float const dx = x - 4.5f, dy = y - 4.5f;
			if (dx * dx + dy * dy < 4.2f) Put(px, w, x, y, 0, 0, 0, 0.18f);
		}
	}
	else if (name == "silhouette")
	{
		// head-and-shoulders placeholder for portraits when there is no game data
		size(120, 132, false);
		for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
		{
			float const hx = (x - 60) / 26.f, hy = (y - 50) / 32.f;
			float const sx = (x - 60) / 56.f, sy = (y - 138) / 44.f;
			bool const inside = hx * hx + hy * hy < 1 || (sx * sx + sy * sy < 1 && y > 88);
			if (inside) Put(px, w, x, y, 255, 255, 255, 0.22f);
		}
	}
	else
	{
		w = h = 0;
		repeat = false;
	}
	return px;
}

} // namespace nui
