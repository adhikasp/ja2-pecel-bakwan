#pragma once
// The tactical world overlays as data (issue #322, docs/plan/native-tactical.md "OverlayModel"): the things the
// legacy RenderTopmostTacticalInterface drew in canvas pixels over the world - the locator rings, the flashing new
// items, the burst impacts, the up/down arrows, the rubber band, the list of items under the cursor and the pause
// box. The legacy code keeps deciding *when* an overlay exists (timers, flags); this core says what it is made of
// and where the lists go. No globals, no RmlUi: the adapter (Tactical/OverlayAdapter.cc) fills a Frame from the
// legacy globals, the view (NativeUI/TacticalOverlays.cc) draws it, and Lua reads it as data (ja2.overlays()).

#include <cstdint>
#include <string>
#include <vector>

namespace OverlayModel
{
	/** A point in world pixels (what the world renderer draws in, before the world-to-output mapping). */
	struct Point { float x = 0, y = 0; };

	// ------------------------------------------------------------------ locators

	/** Who a locator ring is for: a merc of ours or a neutral (accent), an enemy (danger), a place or item (info). */
	enum class Tone : uint8_t { Friend, Foe, Place };
	enum class LocatorKind : uint8_t { Merc, Place, Item };

	struct Locator
	{
		LocatorKind kind = LocatorKind::Place;
		Tone        tone = Tone::Place;
		Point       at;
		int         frame = 0; // 0..FRAMES-1: where in the pulse (the legacy radio.sti frame)
		int         gridNo = -1;
	};

	constexpr int FRAMES = 5; // frames of a locator's pulse

	/** One ring of a locator's pulse: the radius in dp and the opacity 0..1 at @a frame. The ring widens and fades. */
	struct RingPhase { float radiusDp; float alpha; };
	RingPhase RingAt(int frame);

	// ------------------------------------------------------------------ up and down arrows

	/** One chevron of the arrow stack. */
	enum class ArrowTone : uint8_t { Plain, Yellow, Green };
	struct Arrow
	{
		bool                   up = true;
		bool                   climb = false; // the climb arrow (a ledge, a roof edge), not a level change
		std::vector<ArrowTone> tones;         // one chevron each, nearest the merc first
	};

	/** Decodes guiShowUPDownArrows (the ARROWS_* flags of Interface.h) into the arrows to draw: at most one up and
	 * one down. Hidden up or down arrows (ARROWS_HIDE_*) draw nothing, whatever else is set. */
	std::vector<Arrow> ArrowsFor(uint32_t flags);

	// ------------------------------------------------------------------ rubber band

	struct Band
	{
		bool  valid = false; // there is a box to draw
		float l = 0, t = 0, r = 0, b = 0;
	};
	/** The box of a drag from (x0, y0) to (x1, y1): corners put in order, and not valid when the drag has no area on
	 * either axis (a click). */
	Band NormalizeBand(float x0, float y0, float x1, float y1);
	/** The band's border glow 0..1 at @a ms: a triangle wave, one step per 60 ms over the legacy 12 colour steps. */
	float BandGlow(uint32_t ms);

	// ------------------------------------------------------------------ item lists

	constexpr int MAX_LISTED = 8; // rows of a list; the rest is "+N more"

	struct PoolRow
	{
		int         item = 0;  // item index, for the picture
		std::string name;
		int         count = 1; // number of objects in the stack
	};
	struct PoolList
	{
		std::vector<PoolRow> rows;
		int                  hidden = 0; // items past MAX_LISTED
	};
	/** The first MAX_LISTED of @a all and the number left out. */
	PoolList BuildPool(std::vector<PoolRow> const& all);

	struct Rect { float x = 0, y = 0, w = 0, h = 0; };
	/** Where a list of size w x h goes for an anchor point (output pixels): to the right of it with @a gap between,
	 * or to the left when it would not fit; centred on it vertically, then kept inside @a bounds. */
	Rect PlaceList(float anchorX, float anchorY, float w, float h, Rect bounds, float gap);

	/** The list under the cursor or beside a flashing item. */
	struct PoolBox
	{
		bool     atPointer = false; // anchored to the mouse, not to a world point
		Point    at;                // world point (when !atPointer)
		int      gridNo = -1;
		PoolList list;
	};

	// ------------------------------------------------------------------ everything for one frame

	struct BurstMark
	{
		Point at;
		int   gridNo = -1;
	};

	/** A civilian's line, drawn as a bubble over his head. */
	struct Speech
	{
		std::string text;
		Point       at; // above his head, world pixels
	};

	struct Frame
	{
		std::vector<Locator>   locators;
		std::vector<BurstMark> bursts;
		std::vector<Arrow>     arrows;
		Point                  arrowsAt;       // the selected merc (world pixels), arrows hang on him
		bool                   hasArrows = false;
		Band                   band;           // in the same pixels the drag was read in (canvas)
		std::vector<PoolBox>   pools;
		std::vector<Speech>    speech;
		bool                   paused = false; // the player paused the game
	};

	char const* ToneName(Tone);
	char const* KindName(LocatorKind);
	char const* ArrowToneName(ArrowTone);
}
