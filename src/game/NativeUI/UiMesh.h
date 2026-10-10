#pragma once
// A tiny triangle-mesh builder for the native tactical layers (the cursor's path and marker, the world overlays):
// lines, dashes, discs, rings and diamonds in output pixels, drawn straight through the render interface so they
// do not depend on RmlUi transforms (the software UI path does not draw them).

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>

namespace NativeUI
{
	inline Rml::ColourbPremultiplied MeshCol(int r, int g, int b, int a)
	{
		return Rml::Colourb(Rml::byte(r), Rml::byte(g), Rml::byte(b), Rml::byte(a)).ToPremultiplied();
	}

	struct MeshBuilder
	{
		Rml::Mesh mesh;

		void Tri(Rml::Vector2f a, Rml::Vector2f b, Rml::Vector2f c, Rml::ColourbPremultiplied col)
		{
			int const i = int(mesh.vertices.size());
			for (Rml::Vector2f p : { a, b, c })
			{
				Rml::Vertex v;
				v.position = p;
				v.colour = col;
				v.tex_coord = { 0, 0 };
				mesh.vertices.push_back(v);
			}
			mesh.indices.insert(mesh.indices.end(), { i, i + 1, i + 2 });
		}

		void Quad(Rml::Vector2f a, Rml::Vector2f b, Rml::Vector2f c, Rml::Vector2f d, Rml::ColourbPremultiplied col)
		{
			Tri(a, b, c, col);
			Tri(a, c, d, col);
		}

		void Line(Rml::Vector2f a, Rml::Vector2f b, float width, Rml::ColourbPremultiplied col)
		{
			Rml::Vector2f d = b - a;
			float const len = std::sqrt(d.x * d.x + d.y * d.y);
			if (len < 0.01f) return;
			d = d * (1.f / len);
			Rml::Vector2f const n{ -d.y * width * 0.5f, d.x * width * 0.5f };
			Quad(a + n, b + n, b - n, a - n, col);
		}

		// a dashed line: `dash` on, `gap` off, along a -> b
		void Dashed(Rml::Vector2f a, Rml::Vector2f b, float width, float dash, float gap, Rml::ColourbPremultiplied col)
		{
			Rml::Vector2f d = b - a;
			float const len = std::sqrt(d.x * d.x + d.y * d.y);
			if (len < 0.01f) return;
			d = d * (1.f / len);
			for (float t = 0; t < len; t += dash + gap)
				Line(a + d * t, a + d * std::min(t + dash, len), width, col);
		}

		void Disc(Rml::Vector2f c, float r, Rml::ColourbPremultiplied col)
		{
			constexpr int N = 14;
			for (int i = 0; i < N; ++i)
			{
				float const a0 = 6.2831853f * i / N, a1 = 6.2831853f * (i + 1) / N;
				Tri(c, c + Rml::Vector2f{ std::cos(a0) * r, std::sin(a0) * r }, c + Rml::Vector2f{ std::cos(a1) * r, std::sin(a1) * r }, col);
			}
		}

		// the diamond of a tile: filled, then the outline
		void Diamond(Rml::Vector2f c, float w, float h, float lineW, Rml::ColourbPremultiplied fill, Rml::ColourbPremultiplied line)
		{
			Rml::Vector2f const t{ c.x, c.y - h * 0.5f }, r{ c.x + w * 0.5f, c.y }, b{ c.x, c.y + h * 0.5f }, l{ c.x - w * 0.5f, c.y };
			Quad(t, r, b, l, fill);
			Line(t, r, lineW, line);
			Line(r, b, lineW, line);
			Line(b, l, lineW, line);
			Line(l, t, lineW, line);
			// the joins
			float const j = lineW * 0.5f;
			Disc(t, j, line); Disc(r, j, line); Disc(b, j, line); Disc(l, j, line);
		}
	};

	/** A ring (an annulus) of the given radii around c. */
	inline void MeshRing(MeshBuilder& m, Rml::Vector2f c, float rIn, float rOut, Rml::ColourbPremultiplied col)
	{
		constexpr int N = 40;
		for (int i = 0; i < N; ++i)
		{
			float const a0 = 6.2831853f * i / N, a1 = 6.2831853f * (i + 1) / N;
			Rml::Vector2f const p0{ std::cos(a0), std::sin(a0) }, p1{ std::cos(a1), std::sin(a1) };
			m.Quad(c + p0 * rIn, c + p0 * rOut, c + p1 * rOut, c + p1 * rIn, col);
		}
	}
}
