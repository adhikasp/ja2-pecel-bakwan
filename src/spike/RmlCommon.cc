#include "RmlCommon.h"
#include "UiSpike.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>

namespace spike {

// On SDL's software renderer (headless screenshots, the game's frame-buffer path) RmlUi's geometry is rasterized
// here instead of by SDL_RenderGeometry, whose software rasterizer is not watertight: rounded boxes and snapped
// quads showed stray lines and seams. This one uses edge functions with a top-left fill rule, so triangles that
// share an edge cover every pixel exactly once. GPU renderers keep using SDL_RenderGeometry.
SdlRenderInterface::SdlRenderInterface(SDL_Renderer* r) : m_r(r)
{
	if (std::string(SDL_GetRendererName(r)) == SDL_SOFTWARE_RENDERER)
	{
		m_target = static_cast<SDL_Surface*>(SDL_GetPointerProperty(SDL_GetRendererProperties(r), SDL_PROP_RENDERER_SURFACE_POINTER, nullptr));
		if (m_target && m_target->format != SDL_PIXELFORMAT_ARGB8888 && m_target->format != SDL_PIXELFORMAT_XRGB8888) m_target = nullptr;
	}
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

void SdlRenderInterface::Rasterize(Geometry const& g, Rml::Vector2f t, SDL_Texture* texture)
{
	SDL_Surface* const dst = m_target;
	CpuTexture const* tex = nullptr;
	if (texture)
	{
		auto it = m_cpu.find(texture);
		if (it == m_cpu.end()) return;
		tex = &it->second;
	}
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
		int const minX = std::max(clipL, int(std::floor(std::min({ tri[0].x, tri[1].x, tri[2].x }))));
		int const maxX = std::min(clipR - 1, int(std::ceil(std::max({ tri[0].x, tri[1].x, tri[2].x }))));
		int const minY = std::max(clipT, int(std::floor(std::min({ tri[0].y, tri[1].y, tri[2].y }))));
		int const maxY = std::min(clipB - 1, int(std::ceil(std::max({ tri[0].y, tri[1].y, tri[2].y }))));
		if (minX > maxX || minY > maxY) continue;
		// which edges own the pixels exactly on them (top-left rule, expressed via the canonical orientation)
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
					float s[4];
					sample(tri[0].u * b0 + tri[1].u * b1 + tri[2].u * b2, tri[0].v * b0 + tri[1].v * b1 + tri[2].v * b2, s);
					// premultiplied vertex colour x premultiplied texel
					r *= s[0]; gg *= s[1]; b *= s[2]; a *= s[3];
				}
				if (a <= 0.f) continue;
				Uint32 const d = row[x];
				float const k = 1.f - std::min(a, 1.f);
				auto const blend = [&](float src, int shift) {
					float const dc = ((d >> shift) & 0xFF) / 255.f;
					return Uint32(std::clamp(int((src + dc * k) * 255.f + 0.5f), 0, 255)) << shift;
				};
				row[x] = 0xFF000000u | blend(r, 16) | blend(gg, 8) | blend(b, 0);
			}
		}
	}
}

void SdlRenderInterface::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f t, Rml::TextureHandle texture)
{
	Geometry const& g = *reinterpret_cast<Geometry*>(handle);
	SDL_Texture* const tex = reinterpret_cast<SDL_Texture*>(texture);
	if (m_target)
	{
		SDL_FlushRenderer(m_r); // keep the order with anything drawn through SDL (the clear)
		Rasterize(g, t, tex);
		return;
	}
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
	bool const wrap = tex && m_cpu.count(tex) && m_cpu[tex].repeat;
	if (wrap) SDL_SetRenderTextureAddressMode(m_r, SDL_TEXTURE_ADDRESS_WRAP, SDL_TEXTURE_ADDRESS_WRAP);
	SDL_SetRenderDrawBlendMode(m_r, SDL_BLENDMODE_BLEND);
	SDL_RenderGeometry(m_r, tex, m_vertices.data(), int(m_vertices.size()), g.indices.data(), int(g.indices.size()));
	if (wrap) SDL_SetRenderTextureAddressMode(m_r, SDL_TEXTURE_ADDRESS_AUTO, SDL_TEXTURE_ADDRESS_AUTO);
}

SDL_Texture* SdlRenderInterface::MakeTexture(unsigned char const* rgba, int w, int h, bool repeat)
{
	// straight-alpha RGBA32 in; the CPU copy feeds the software rasterizer
	std::vector<unsigned char> copy(rgba, rgba + size_t(w) * h * 4);
	SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, copy.data(), w * 4);
	SDL_Texture* tex = s ? SDL_CreateTextureFromSurface(m_r, s) : nullptr;
	if (s) SDL_DestroySurface(s);
	if (!tex) return nullptr;
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
	m_cpu[tex] = CpuTexture{ w, h, repeat, std::move(copy) };
	return tex;
}

Rml::TextureHandle SdlRenderInterface::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source)
{
	// RmlUi joins the source with the document's path: our name is the last path component.
	std::string const name = source.substr(source.find_last_of("/\\") + 1);
	SDL_Texture* tex = nullptr;
	if (name.rfind("gen-", 0) == 0)
	{
		int w = 0, h = 0;
		bool repeat = false;
		std::vector<unsigned char> const pixels = GenerateProcedural(name.substr(4), w, h, repeat);
		if (pixels.empty()) return {};
		tex = MakeTexture(pixels.data(), w, h, repeat);
		dimensions = { w, h };
	}
	else if (name.rfind("face-", 0) == 0)
	{
		SDL_Surface* s = ProvideImage(name);
		if (!s) return {};
		SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(s);
		if (!rgba) return {};
		std::vector<unsigned char> pixels(size_t(rgba->w) * rgba->h * 4);
		for (int y = 0; y < rgba->h; ++y)
			std::copy_n(static_cast<unsigned char const*>(rgba->pixels) + size_t(y) * rgba->pitch, size_t(rgba->w) * 4, &pixels[size_t(y) * rgba->w * 4]);
		tex = MakeTexture(pixels.data(), rgba->w, rgba->h, false);
		dimensions = { rgba->w, rgba->h };
		SDL_DestroySurface(rgba);
	}
	return reinterpret_cast<Rml::TextureHandle>(tex);
}

Rml::TextureHandle SdlRenderInterface::GenerateTexture(Rml::Span<const Rml::byte> src, Rml::Vector2i dim)
{
	std::vector<Rml::byte> straight(src.begin(), src.end());
	for (size_t i = 0; i + 3 < straight.size(); i += 4)
	{
		unsigned const a = straight[i + 3];
		for (int c = 0; c < 3; ++c) straight[i + c] = Rml::byte(a ? std::min(255u, straight[i + c] * 255u / a) : 0);
	}
	SDL_Texture* tex = MakeTexture(straight.data(), dim.x, dim.y, false);
	if (tex) SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
	return reinterpret_cast<Rml::TextureHandle>(tex);
}

void SdlRenderInterface::ReleaseTexture(Rml::TextureHandle t)
{
	m_cpu.erase(reinterpret_cast<SDL_Texture*>(t));
	SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(t));
}

void SdlRenderInterface::EnableScissorRegion(bool enable)
{
	m_scissorOn = enable;
	SDL_SetRenderClipRect(m_r, enable ? &m_scissor : nullptr);
}

void SdlRenderInterface::SetScissorRegion(Rml::Rectanglei r)
{
	m_scissor = { r.Left(), r.Top(), r.Width(), r.Height() };
	if (m_scissorOn) SDL_SetRenderClipRect(m_r, &m_scissor);
}


bool VirtualClock::LogMessage(Rml::Log::Type type, const Rml::String& message)
{
	if (type <= Rml::Log::LT_WARNING)
	{
		++warnings;
		SDL_Log("RmlUi: %s", message.c_str());
	}
	return true;
}

namespace {
/** Relative paths (documents, style sheets) resolve against the style directory; RmlUi's URL parsing does not
 * accept Windows drive letters, so documents are loaded by relative name. */
class StyleFileInterface : public Rml::FileInterface
{
public:
	Rml::FileHandle Open(const Rml::String& path) override
	{
		bool const absolute = !path.empty() && (path[0] == '/' || (path.size() > 1 && path[1] == ':'));
		std::string const full = absolute ? path : StyleDir() + "/" + path;
		return reinterpret_cast<Rml::FileHandle>(std::fopen(full.c_str(), "rb"));
	}
	void Close(Rml::FileHandle f) override { std::fclose(reinterpret_cast<FILE*>(f)); }
	size_t Read(void* buffer, size_t size, Rml::FileHandle f) override { return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(f)); }
	bool Seek(Rml::FileHandle f, long offset, int origin) override { return std::fseek(reinterpret_cast<FILE*>(f), offset, origin) == 0; }
	size_t Tell(Rml::FileHandle f) override { return size_t(std::ftell(reinterpret_cast<FILE*>(f))); }
};
}

VirtualClock& RmlClock()
{
	static VirtualClock clock;
	return clock;
}

int  RmlWarnings()      { return RmlClock().warnings; }
void ResetRmlWarnings() { RmlClock().warnings = 0; }

void InitRmlOnce()
{
	static bool done = false;
	if (done) return;
	done = true;
	Rml::SetSystemInterface(&RmlClock());
	static StyleFileInterface files;
	Rml::SetFileInterface(&files);
	Rml::Initialise();
	// Font data must outlive Rml::Shutdown, which we never call: static storage.
	static std::vector<unsigned char> regular = LoadAssetBytes("LatoLatin-Regular.ttf");
	static std::vector<unsigned char> bold    = LoadAssetBytes("LatoLatin-Bold.ttf");
	Rml::LoadFontFace({ regular.data(), regular.size() }, "Lato", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true);
	Rml::LoadFontFace({ bold.data(), bold.size() }, "Lato", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold);
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

} // namespace spike
