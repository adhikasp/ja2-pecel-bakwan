#ifndef CAMERA_H
#define CAMERA_H

#include "WorldDef.h"

/**
 * The tactical camera's centre, and the small pure core that says where it may rest.
 *
 * The centre is a position in the game's world ("cell") coordinates: CELL_X_SIZE (= 10) units per tile, so a
 * tile is 10 x 10 and its centre is at tile * 10 + 5. That is where the camera used to be pinned - the type was
 * an INT16 cell, there was no fraction anywhere in it, and a pan was always a whole tile (see #38 and
 * docs/plan/native-modern-game-decisions.md). #37's strip cache draws a recorded row at an offset, which is
 * already a sub-tile camera, so the state now carries the fraction and the renderer rounds the centre to the
 * pixels it draws (RenderWorld.cc, CalcRenderParameters).
 */
namespace Camera
{
struct Centre
{
	double x = 0.0;
	double y = 0.0;
	constexpr bool operator==(Centre const&) const = default;
};

/**
	 * The camera is free: it rests where a pan leaves it, not only on a tile centre. This is decision B2, kept
	 * behind a flag because it is the one part of #38 that was called out as cheap to reverse: flipping it to
	 * false restores the pre-#38 settle, where every pan rounded to the nearest tile centre.
	 */
constexpr bool kFreeCamera = true;

/** A tile's centre: the position the legacy camera could only ever rest on. */
constexpr Centre FromTile(int const tileX, int const tileY)
{
	return { double(tileX) * CELL_X_SIZE + CELL_X_SIZE / 2.0, double(tileY) * CELL_Y_SIZE + CELL_Y_SIZE / 2.0 };
}

/** The nearest tile centre (the legacy settle). */
Centre SnapToTileCentre(Centre c);

/** Whether a centre sits exactly on a tile centre: true of every resting position before #38. */
bool OnTileCentre(Centre c);

/**
 * An eased centring move (decision A2): a discrete move of the view - locating a merc, a click on the radar, the
 * next turn's automatic centring - glides to its destination instead of jumping there. Held-key and moused-edge
 * scrolling stay direct: the input is already a per-frame step, and A3's inertia (a carried velocity) is
 * deliberately not here because it is wall-clock dependent and would make a headless run non-reproducible.
 *
 * The move is counted in frames, never in milliseconds, so the path is the same on a fast machine, a slow one and
 * the virtual clock of a driven run. It is a cubic ease-out: it covers most of the distance early and settles, and
 * the last frame puts the centre on the target exactly - an ease that only ever approaches its target is the
 * failure this type exists to avoid.
 */
constexpr int kEaseFrames = 12; // 200 ms at the game's 60 Hz frame

/**
 * A move of the centre from where it is to a target, one frame at a time. Start() once, Advance() once a frame.
 *
 *     Ease e;
 *     e.Start({0, 0}, {120, 0});
 *     while (e.Active()) centre = e.Advance();   // twelve frames; the last one is exactly {120, 0}
 */
class Ease
{
public:
	/** Start the move. Calling it again (a new target while one is in flight) starts again from @a from, so the
	 * centre never jumps. */
	void Start(Centre from, Centre target);
	/** Stop where the centre is: no further frames. */
	void Cancel() { m_active = false; }
	bool Active() const { return m_active; }
	/** The frame the move is on (0 before the first Advance, kEaseFrames when it has arrived). */
	int Frame() const { return m_frame; }
	/** Where the move is going (the last target when it is not active). */
	Centre Target() const { return m_target; }
	/** The centre for this frame. The kEaseFrames-th call returns the target exactly and deactivates the move. */
	Centre Advance();

private:
	Centre m_from{};
	Centre m_target{};
	int    m_frame = 0;
	bool   m_active = false;
};

/** Progress along an eased move after @a frame of @a frames: a cubic ease-out from 0 to exactly 1. */
double EaseProgress(int frame, int frames);
}

#endif
