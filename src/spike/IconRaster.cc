// Rasterizer for the design system's icons (assets/ui/icons/*.svg, docs/ui/design-system.md). It covers the SVG
// subset the icon set is written in: <path> (M L H V C S Q T A Z, absolute and relative), <circle>, <ellipse>,
// <rect> (with rx), <line>, <polyline>, <polygon>; fill / stroke (none | currentColor), stroke-width,
// stroke-dasharray, opacity and transform="rotate(a cx cy)" on shapes; presentation attributes on <svg> are
// inherited. Joins and caps are always round. Output is white with coverage in alpha, so the UI tints it
// (image-color). Anti-aliasing: 4x4 samples per pixel.
#include "UiSpike.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace spike {
namespace {

struct Pt { float x, y; };
using Poly = std::vector<Pt>;

struct Shape
{
	std::vector<Poly> fill;   // closed subpaths
	std::vector<Poly> stroke; // polylines (closed ones repeat the first point)
	float strokeWidth = 2;
	float opacity = 1;
	bool  doFill = false, doStroke = true;
	float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
};

std::string Attr(std::string const& tag, char const* name)
{
	std::string const key = std::string(" ") + name + "=\"";
	size_t p = tag.find(key);
	if (p == std::string::npos) return {};
	p += key.size();
	size_t const e = tag.find('"', p);
	return e == std::string::npos ? std::string() : tag.substr(p, e - p);
}

float Num(std::string const& s, float def) { return s.empty() ? def : std::strtof(s.c_str(), nullptr); }

/** Number scanner for path data and point lists ("M8 12l3 3", "a2 2 0 014 0", "1.5.5") */
struct Scanner
{
	char const* p;
	void skip() { while (*p && (std::isspace((unsigned char)*p) || *p == ',')) ++p; }
	bool number()
	{
		skip();
		return *p && (std::isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.');
	}
	float next()
	{
		skip();
		char const* s = p;
		if (*p == '-' || *p == '+') ++p;
		bool dot = false;
		while (std::isdigit((unsigned char)*p) || (*p == '.' && !dot)) { if (*p == '.') dot = true; ++p; }
		if (*p == 'e' || *p == 'E') { ++p; if (*p == '-' || *p == '+') ++p; while (std::isdigit((unsigned char)*p)) ++p; }
		return std::strtof(std::string(s, p).c_str(), nullptr);
	}
	float flag() { skip(); float const f = *p == '1' ? 1.f : 0.f; if (*p) ++p; return f; }
};

void Cubic(Poly& out, Pt a, Pt b, Pt c, Pt d)
{
	for (int i = 1; i <= 16; ++i)
	{
		float const t = i / 16.f, u = 1 - t;
		out.push_back({ u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
		                u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y });
	}
}

void Arc(Poly& out, Pt a, float rx, float ry, float phiDeg, bool large, bool sweep, Pt b)
{
	// endpoint -> centre parameterisation (SVG 1.1 implementation notes F.6.5)
	if (rx == 0 || ry == 0) { out.push_back(b); return; }
	rx = std::abs(rx); ry = std::abs(ry);
	float const phi = phiDeg * 3.14159265f / 180, cs = std::cos(phi), sn = std::sin(phi);
	float const dx = (a.x - b.x) / 2, dy = (a.y - b.y) / 2;
	float const x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
	float const lam = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
	if (lam > 1) { rx *= std::sqrt(lam); ry *= std::sqrt(lam); }
	float num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
	float const den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
	float co = den > 0 ? std::sqrt(std::max(0.f, num / den)) : 0;
	if (large == sweep) co = -co;
	float const cxp = co * rx * y1 / ry, cyp = -co * ry * x1 / rx;
	float const cx = cs * cxp - sn * cyp + (a.x + b.x) / 2, cy = sn * cxp + cs * cyp + (a.y + b.y) / 2;
	auto ang = [](float ux, float uy, float vx, float vy) {
		return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
	};
	float const t1 = ang(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry);
	float dt = ang((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
	if (!sweep && dt > 0) dt -= 2 * 3.14159265f;
	if (sweep && dt < 0) dt += 2 * 3.14159265f;
	int const n = std::max(4, int(std::abs(dt) / 0.2f));
	for (int i = 1; i <= n; ++i)
	{
		float const t = t1 + dt * i / n;
		out.push_back({ cx + rx * std::cos(t) * cs - ry * std::sin(t) * sn, cy + rx * std::cos(t) * sn + ry * std::sin(t) * cs });
	}
}

/** Parses path data into subpaths; closed ones are flagged. */
void ParsePath(std::string const& d, std::vector<std::pair<Poly, bool>>& out)
{
	Scanner s{ d.c_str() };
	Pt cur{ 0, 0 }, start{ 0, 0 }, ctrl{ 0, 0 };
	char cmd = 0, prev = 0;
	Poly poly;
	auto flush = [&](bool closed) {
		if (poly.size() > 1) out.push_back({ poly, closed });
		poly.clear();
	};
	while (true)
	{
		s.skip();
		if (!*s.p) break;
		if (std::isalpha((unsigned char)*s.p)) { cmd = *s.p++; }
		else if (!cmd) break;
		bool const rel = std::islower((unsigned char)cmd);
		char const c = char(std::toupper((unsigned char)cmd));
		auto P = [&](float x, float y) { return rel ? Pt{ cur.x + x, cur.y + y } : Pt{ x, y }; };
		switch (c)
		{
			case 'M': { flush(false); float x = s.next(), y = s.next(); cur = start = P(x, y); poly.push_back(cur); cmd = rel ? 'l' : 'L'; break; }
			case 'L': { float x = s.next(), y = s.next(); cur = P(x, y); poly.push_back(cur); break; }
			case 'H': { float x = s.next(); cur = { rel ? cur.x + x : x, cur.y }; poly.push_back(cur); break; }
			case 'V': { float y = s.next(); cur = { cur.x, rel ? cur.y + y : y }; poly.push_back(cur); break; }
			case 'C': { float a = s.next(), b = s.next(), c2 = s.next(), d2 = s.next(), e = s.next(), f = s.next();
				Pt p1 = P(a, b), p2 = P(c2, d2), p3 = P(e, f); Cubic(poly, cur, p1, p2, p3); ctrl = p2; cur = p3; break; }
			case 'S': { float c2 = s.next(), d2 = s.next(), e = s.next(), f = s.next();
				Pt p1 = (prev == 'C' || prev == 'S') ? Pt{ 2 * cur.x - ctrl.x, 2 * cur.y - ctrl.y } : cur;
				Pt p2 = P(c2, d2), p3 = P(e, f); Cubic(poly, cur, p1, p2, p3); ctrl = p2; cur = p3; break; }
			case 'Q': { float a = s.next(), b = s.next(), e = s.next(), f = s.next();
				Pt q = P(a, b), p3 = P(e, f);
				Cubic(poly, cur, { cur.x + 2.f / 3 * (q.x - cur.x), cur.y + 2.f / 3 * (q.y - cur.y) },
					{ p3.x + 2.f / 3 * (q.x - p3.x), p3.y + 2.f / 3 * (q.y - p3.y) }, p3);
				ctrl = q; cur = p3; break; }
			case 'T': { float e = s.next(), f = s.next();
				Pt q = (prev == 'Q' || prev == 'T') ? Pt{ 2 * cur.x - ctrl.x, 2 * cur.y - ctrl.y } : cur, p3 = P(e, f);
				Cubic(poly, cur, { cur.x + 2.f / 3 * (q.x - cur.x), cur.y + 2.f / 3 * (q.y - cur.y) },
					{ p3.x + 2.f / 3 * (q.x - p3.x), p3.y + 2.f / 3 * (q.y - p3.y) }, p3);
				ctrl = q; cur = p3; break; }
			case 'A': { float rx = s.next(), ry = s.next(), rot = s.next(); bool large = s.flag() > 0, sweep = s.flag() > 0;
				float x = s.next(), y = s.next(); Pt p = P(x, y); Arc(poly, cur, rx, ry, rot, large, sweep, p); cur = p; break; }
			case 'Z': { if (!poly.empty()) poly.push_back(start); flush(true); cur = start; poly.push_back(cur); cmd = 0; break; }
			default: return;
		}
		prev = c;
		if (c != 'Z' && !s.number() && !(std::isalpha((unsigned char)*s.p))) { s.skip(); }
	}
	flush(false);
}

Poly Ellipse(float cx, float cy, float rx, float ry)
{
	Poly p;
	for (int i = 0; i <= 64; ++i)
	{
		float const t = i * 2 * 3.14159265f / 64;
		p.push_back({ cx + rx * std::cos(t), cy + ry * std::sin(t) });
	}
	return p;
}

Poly Dash(Poly const& in, std::vector<float> const& pattern, std::vector<Poly>& out)
{
	// splits a polyline into dashes; returns nothing (dashes are appended to out)
	float total = 0;
	for (float v : pattern) total += v;
	if (pattern.empty() || total <= 0) { out.push_back(in); return {}; }
	size_t pi = 0;
	float left = pattern[0];
	bool on = true;
	Poly cur{ in.front() };
	for (size_t i = 1; i < in.size(); ++i)
	{
		Pt a = in[i - 1];
		Pt const b = in[i];
		float seg = std::hypot(b.x - a.x, b.y - a.y);
		while (seg > left)
		{
			float const t = left / seg;
			Pt const m{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
			if (on) { cur.push_back(m); out.push_back(cur); }
			cur = { m };
			on = !on;
			seg -= left;
			a = m;
			pi = (pi + 1) % pattern.size();
			left = pattern[pi];
		}
		left -= seg;
		if (on) cur.push_back(b);
		else cur = { b };
	}
	if (on && cur.size() > 1) out.push_back(cur);
	return {};
}

float SegDist2(Pt p, Pt a, Pt b)
{
	float const vx = b.x - a.x, vy = b.y - a.y, wx = p.x - a.x, wy = p.y - a.y;
	float const l = vx * vx + vy * vy;
	float t = l > 0 ? (wx * vx + wy * vy) / l : 0;
	t = std::clamp(t, 0.f, 1.f);
	float const dx = wx - t * vx, dy = wy - t * vy;
	return dx * dx + dy * dy;
}

bool Inside(Pt p, std::vector<Poly> const& polys)
{
	int winding = 0;
	for (Poly const& poly : polys)
	{
		for (size_t i = 0; i < poly.size(); ++i)
		{
			Pt const a = poly[i], b = poly[(i + 1) % poly.size()];
			if (a.y <= p.y) { if (b.y > p.y && (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y) > 0) ++winding; }
			else if (b.y <= p.y && (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y) < 0) --winding;
		}
	}
	return winding != 0;
}

} // namespace

std::vector<unsigned char> RasterizeSvg(std::string const& svg, int size, std::string* error)
{
	auto fail = [&](std::string const& why) { if (error) *error = why; return std::vector<unsigned char>{}; };
	size_t const root = svg.find("<svg");
	if (root == std::string::npos) return fail("no <svg>");
	std::string const rootTag = svg.substr(root, svg.find('>', root) - root);
	float vbX = 0, vbY = 0, vbW = 24, vbH = 24;
	if (std::string const vb = Attr(rootTag, "viewBox"); !vb.empty())
	{
		Scanner s{ vb.c_str() };
		vbX = s.next(); vbY = s.next(); vbW = s.next(); vbH = s.next();
	}
	std::string const rootFill = Attr(rootTag, "fill"), rootStroke = Attr(rootTag, "stroke");
	float const rootWidth = Num(Attr(rootTag, "stroke-width"), 1);

	std::vector<Shape> shapes;
	size_t pos = root + rootTag.size();
	while ((pos = svg.find('<', pos)) != std::string::npos)
	{
		size_t const end = svg.find('>', pos);
		if (end == std::string::npos) break;
		std::string tag = " " + svg.substr(pos + 1, end - pos - 1);
		pos = end;
		size_t n = 1;
		while (n < tag.size() && std::isalpha((unsigned char)tag[n])) ++n;
		std::string const name = tag.substr(1, n - 1);
		tag = tag.substr(n); // attributes, starting with a space
		if (name.empty() || name == "svg" || name == "g") continue;

		Shape sh;
		std::string const fill = Attr(tag, "fill").empty() ? rootFill : Attr(tag, "fill");
		std::string const stroke = Attr(tag, "stroke").empty() ? rootStroke : Attr(tag, "stroke");
		sh.doFill = !fill.empty() && fill != "none";
		sh.doStroke = !stroke.empty() && stroke != "none";
		sh.strokeWidth = Num(Attr(tag, "stroke-width"), rootWidth);
		sh.opacity = Num(Attr(tag, "opacity"), 1);

		std::vector<std::pair<Poly, bool>> polys;
		if (name == "path") ParsePath(Attr(tag, "d"), polys);
		else if (name == "circle")
		{
			float const r = Num(Attr(tag, "r"), 0);
			polys.push_back({ Ellipse(Num(Attr(tag, "cx"), 0), Num(Attr(tag, "cy"), 0), r, r), true });
		}
		else if (name == "ellipse")
			polys.push_back({ Ellipse(Num(Attr(tag, "cx"), 0), Num(Attr(tag, "cy"), 0), Num(Attr(tag, "rx"), 0), Num(Attr(tag, "ry"), 0)), true });
		else if (name == "rect")
		{
			float const x = Num(Attr(tag, "x"), 0), y = Num(Attr(tag, "y"), 0), w = Num(Attr(tag, "width"), 0), h = Num(Attr(tag, "height"), 0);
			float const r = std::min({ Num(Attr(tag, "rx"), 0), w / 2, h / 2 });
			Poly p;
			if (r <= 0) p = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h }, { x, y } };
			else
			{
				p.push_back({ x + r, y });
				p.push_back({ x + w - r, y }); Arc(p, p.back(), r, r, 0, false, true, { x + w, y + r });
				p.push_back({ x + w, y + h - r }); Arc(p, p.back(), r, r, 0, false, true, { x + w - r, y + h });
				p.push_back({ x + r, y + h }); Arc(p, p.back(), r, r, 0, false, true, { x, y + h - r });
				p.push_back({ x, y + r }); Arc(p, p.back(), r, r, 0, false, true, { x + r, y });
			}
			polys.push_back({ p, true });
		}
		else if (name == "line")
			polys.push_back({ { { Num(Attr(tag, "x1"), 0), Num(Attr(tag, "y1"), 0) }, { Num(Attr(tag, "x2"), 0), Num(Attr(tag, "y2"), 0) } }, false });
		else if (name == "polyline" || name == "polygon")
		{
			std::string const pts = Attr(tag, "points");
			Scanner s{ pts.c_str() };
			Poly p;
			while (s.number()) { float x = s.next(); float y = s.next(); p.push_back({ x, y }); }
			bool const closed = name == "polygon";
			if (closed && !p.empty()) p.push_back(p.front());
			polys.push_back({ p, closed });
		}
		else continue;

		// transform="rotate(a cx cy)"
		if (std::string const tr = Attr(tag, "transform"); tr.rfind("rotate(", 0) == 0)
		{
			Scanner s{ tr.c_str() + 7 };
			float const a = s.next() * 3.14159265f / 180;
			float const cx = s.number() ? s.next() : 0, cy = s.number() ? s.next() : 0;
			for (auto& [poly, closed] : polys)
				for (Pt& p : poly)
				{
					float const x = p.x - cx, y = p.y - cy;
					p = { cx + x * std::cos(a) - y * std::sin(a), cy + x * std::sin(a) + y * std::cos(a) };
				}
		}
		std::vector<float> dashes;
		if (std::string const da = Attr(tag, "stroke-dasharray"); !da.empty())
		{
			Scanner s{ da.c_str() };
			while (s.number()) dashes.push_back(s.next());
		}
		for (auto& [poly, closed] : polys)
		{
			if (poly.size() < 2) continue;
			if (sh.doFill) sh.fill.push_back(poly);
			if (sh.doStroke) Dash(poly, dashes, sh.stroke);
			for (Pt const& p : poly)
			{
				sh.minX = std::min(sh.minX, p.x); sh.minY = std::min(sh.minY, p.y);
				sh.maxX = std::max(sh.maxX, p.x); sh.maxY = std::max(sh.maxY, p.y);
			}
		}
		if (!sh.fill.empty() || !sh.stroke.empty()) shapes.push_back(std::move(sh));
	}
	if (shapes.empty()) return fail("no drawable shapes");

	std::vector<unsigned char> out(size_t(size) * size * 4, 0);
	float const sx = vbW / size, sy = vbH / size;
	int const S = 4;
	for (int y = 0; y < size; ++y)
	{
		for (int x = 0; x < size; ++x)
		{
			float cov = 0;
			for (int j = 0; j < S; ++j)
			{
				for (int i = 0; i < S; ++i)
				{
					Pt const p{ vbX + (x + (i + 0.5f) / S) * sx, vbY + (y + (j + 0.5f) / S) * sy };
					float a = 0;
					for (Shape const& sh : shapes)
					{
						float const pad = sh.strokeWidth;
						if (p.x < sh.minX - pad || p.x > sh.maxX + pad || p.y < sh.minY - pad || p.y > sh.maxY + pad) continue;
						bool hit = !sh.fill.empty() && Inside(p, sh.fill);
						if (!hit)
						{
							float const r2 = sh.strokeWidth * sh.strokeWidth / 4;
							for (Poly const& poly : sh.stroke)
							{
								for (size_t k = 1; k < poly.size() && !hit; ++k) hit = SegDist2(p, poly[k - 1], poly[k]) <= r2;
								if (poly.size() == 1) hit = SegDist2(p, poly[0], poly[0]) <= r2;
								if (hit) break;
							}
						}
						if (hit) a = a + sh.opacity * (1 - a);
					}
					cov += a;
				}
			}
			cov /= S * S;
			unsigned char* px = &out[(size_t(y) * size + x) * 4];
			px[0] = px[1] = px[2] = 255;
			px[3] = (unsigned char)std::clamp(int(cov * 255 + 0.5f), 0, 255);
		}
	}
	return out;
}

} // namespace spike
