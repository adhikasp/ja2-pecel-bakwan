#pragma once
// Who owns the mouse on the tactical screen (issue #319, docs/plan/native-tactical.md "Input: who owns the mouse").
// One decision per frame, in this order: a modal native layer (an open menu or popup, a message box) takes
// everything; else the HUD when the pointer is over one of its "hit" elements; else the world. The outer 2 output
// pixels of the screen belong to the world whatever the HUD has there - that is where the view scrolls and the
// sector exit arrows show. No globals, no RmlUi: the adapter (Tactical/WorldPointer.cc) fills the input.

#include <cstdint>

namespace PointerRouter
{
	enum class Owner : uint8_t { None, Modal, Hud, World };

	struct Input
	{
		bool insideWorld = false;      // inside the world's rectangle on the screen (the viewport)
		bool modalOpen = false;        // a native menu, popup or message box is open
		bool overHud = false;          // over a "hit" element of the HUD
		bool atEdge = false;           // within the edge strip of the screen
		bool blockedByRegion = false;  // a legacy region of normal or higher priority is under the pointer
	};

	/** The owner of the pointer. A modal layer first; the edge strip belongs to the world, the HUD never blocks it. */
	Owner Route(Input const&);

	/** Which edges of a @a w x @a h area the point (@a x, @a y) is within @a strip of. */
	struct Edges
	{
		bool left = false, right = false, top = false, bottom = false;
		bool Any() const { return left || right || top || bottom; }
	};
	Edges EdgesOf(float x, float y, float w, float h, float strip);

	/** The edge strip in output pixels. */
	constexpr float EDGE_STRIP_PX = 2.f;

	/** The same strip in canvas pixels for a canvas that is shown at @a uiScale output pixels per canvas pixel. */
	inline float EdgeStripCanvas(int const uiScale) { return EDGE_STRIP_PX / static_cast<float>(uiScale < 1 ? 1 : uiScale); }

	/** Follows whether the pointer is over the world, and says when it comes and goes (the world's "mouse in" and
	 * "mouse lost" events). */
	enum class Change : uint8_t { None, Gained, Lost };

	class Presence
	{
	public:
		Change Update(bool const over)
		{
			if (over == m_in) return Change::None;
			m_in = over;
			return over ? Change::Gained : Change::Lost;
		}
		bool In() const { return m_in; }
		void Reset() { m_in = false; }

	private:
		bool m_in = false;
	};
}
