#include "OverlayModel.h"

#include <algorithm>
#include <cmath>

namespace OverlayModel
{

RingPhase RingAt(int const frame)
{
	int const f = std::clamp(frame, 0, FRAMES - 1);
	float const t = float(f) / float(FRAMES - 1); // 0 .. 1
	return { 22.f + 28.f * t, 1.f - 0.75f * t };
}

namespace
{
	// the legacy flags of Interface.h
	constexpr uint32_t HIDE_UP = 0x00000002, HIDE_DOWN = 0x00000004, UP_BESIDE = 0x00000008, DOWN_BESIDE = 0x00000020,
		UP_ABOVE_Y = 0x00000040, DOWN_BELOW_Y = 0x00000080, DOWN_BELOW_YG = 0x00000400, DOWN_BELOW_GG = 0x00000800,
		UP_ABOVE_YY = 0x00020000, DOWN_BELOW_YY = 0x00040000, UP_ABOVE_CLIMB = 0x00080000, DOWN_CLIMB = 0x02000000;
}

std::vector<Arrow> ArrowsFor(uint32_t const flags)
{
	std::vector<Arrow> out;
	// the legacy test: both hidden means nothing at all; one hidden does not stop the other's flags
	if ((flags & HIDE_UP) && (flags & HIDE_DOWN)) return out;

	Arrow up;
	up.up = true;
	if (flags & UP_ABOVE_YY) up.tones = { ArrowTone::Plain, ArrowTone::Plain };
	else if (flags & UP_ABOVE_CLIMB) { up.tones = { ArrowTone::Plain }; up.climb = true; }
	else if (flags & (UP_BESIDE | UP_ABOVE_Y)) up.tones = { ArrowTone::Plain };
	if (!up.tones.empty()) out.push_back(std::move(up));

	Arrow down;
	down.up = false;
	if (flags & DOWN_CLIMB) { down.tones = { ArrowTone::Plain }; down.climb = true; }
	else if (flags & DOWN_BELOW_YG) down.tones = { ArrowTone::Green, ArrowTone::Yellow };
	else if (flags & DOWN_BELOW_GG) down.tones = { ArrowTone::Green, ArrowTone::Green };
	else if (flags & DOWN_BELOW_YY) down.tones = { ArrowTone::Yellow, ArrowTone::Yellow };
	else if (flags & (DOWN_BESIDE | DOWN_BELOW_Y)) down.tones = { ArrowTone::Yellow };
	if (!down.tones.empty()) out.push_back(std::move(down));
	return out;
}

Band NormalizeBand(float const x0, float const y0, float const x1, float const y1)
{
	Band b;
	b.l = std::min(x0, x1);
	b.r = std::max(x0, x1);
	b.t = std::min(y0, y1);
	b.b = std::max(y0, y1);
	// the legacy rule: nothing for a click; a thin drag along one axis is still a box (a line)
	b.valid = !(b.l == b.r && b.t == b.b);
	return b;
}

float BandGlow(uint32_t const ms)
{
	constexpr int STEPS = 12;
	int const step = int((ms / 60) % STEPS);
	// the legacy colours rise over 6 steps and fall over 6: 0 1 2 3 4 5 6 5 4 3 2 1 over 6
	int const d = step <= 6 ? step : STEPS - step;
	return float(d) / 6.f;
}

PoolList BuildPool(std::vector<PoolRow> const& all)
{
	PoolList out;
	for (PoolRow const& r : all)
	{
		if (int(out.rows.size()) == MAX_LISTED) { ++out.hidden; continue; }
		out.rows.push_back(r);
	}
	return out;
}

Rect PlaceList(float const ax, float const ay, float const w, float const h, Rect const bounds, float const gap)
{
	Rect r;
	r.w = w;
	r.h = h;
	r.x = ax + gap;
	if (r.x + w > bounds.x + bounds.w) r.x = ax - gap - w; // no room on the right: the left
	r.y = ay - h * 0.5f;
	r.x = std::clamp(r.x, bounds.x, std::max(bounds.x, bounds.x + bounds.w - w));
	r.y = std::clamp(r.y, bounds.y, std::max(bounds.y, bounds.y + bounds.h - h));
	return r;
}

char const* ToneName(Tone const t)
{
	switch (t)
	{
		case Tone::Friend: return "friend";
		case Tone::Foe:    return "foe";
		default:           return "place";
	}
}

char const* KindName(LocatorKind const k)
{
	switch (k)
	{
		case LocatorKind::Merc: return "merc";
		case LocatorKind::Item: return "item";
		default:                return "place";
	}
}

char const* ArrowToneName(ArrowTone const t)
{
	switch (t)
	{
		case ArrowTone::Yellow: return "yellow";
		case ArrowTone::Green:  return "green";
		default:                return "plain";
	}
}

}
