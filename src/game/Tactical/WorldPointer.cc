#include "WorldPointer.h"

#include "Cursor_Control.h"
#include "Handle_UI.h"
#include "JAScreens.h"
#include "MouseSystem.h"
#include "NativeUI.h"
#include "ScreenIDs.h"
#include "Timer_Control.h"
#include "UILayout.h"

namespace
{
	PointerRouter::Presence g_presence;
	UINT32 g_buttons = 0;
	INT16  g_lastX = -1;
	INT16  g_lastY = -1;

	// a double tap: down, up, down within this long
	constexpr UINT32 DOUBLE_TAP_MS = 600;
	UINT32 g_tapDownAt = 0;
	bool   g_tapUp = false;

	bool TacticalScreen()
	{
		return guiCurrentScreen == GAME_SCREEN || guiCurrentScreen == EDIT_SCREEN;
	}

	/** What the legacy viewport region was: the topmost region at the pointer, if it is normal priority or above, hides
	 * the world under it. */
	bool BlockedByRegion(INT16 const x, INT16 const y)
	{
		MOUSE_REGION const* const r = MSYS_RegionAt(x, y);
		return r && r->PriorityLevel >= MSYS_PRIORITY_NORMAL;
	}

	PointerRouter::Edges ReadEdges()
	{
		MousePosF const p = GetMousePosF();
		return PointerRouter::EdgesOf(p.x, p.y, static_cast<float>(SCREEN_WIDTH), static_cast<float>(SCREEN_HEIGHT),
			PointerRouter::EdgeStripCanvas(g_ui.m_uiScale));
	}
}


namespace WorldPointer
{

void Frame()
{
	if (!TacticalScreen())
	{
		Reset();
		return;
	}

	SGPPoint const p = GetMousePos();
	PointerRouter::Input in;
	in.insideWorld     = p.iX <= ViewportRight() && p.iY <= ViewportBottom();
	in.overHud         = NativeUI::CapturesMouse();
	in.atEdge          = ReadEdges().Any();
	in.blockedByRegion = BlockedByRegion(p.iX, p.iY);
	bool const over = PointerRouter::Route(in) == PointerRouter::Owner::World;

	switch (g_presence.Update(over))
	{
		case PointerRouter::Change::Gained:
			g_lastX = static_cast<INT16>(p.iX);
			g_lastY = static_cast<INT16>(p.iY);
			TacticalViewPortMovementCallback(g_lastX, g_lastY, MSYS_CALLBACK_REASON_GAIN_MOUSE | MSYS_CALLBACK_REASON_MOVE);
			break;

		case PointerRouter::Change::Lost:
			g_buttons = 0;
			TacticalViewPortMovementCallback(g_lastX, g_lastY, MSYS_CALLBACK_REASON_LOST_MOUSE);
			return;

		case PointerRouter::Change::None:
			break;
	}
	if (!over) return;

	g_buttons = 0;
	if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))   g_buttons |= MSYS_LEFT_BUTTON;
	if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))  g_buttons |= MSYS_RIGHT_BUTTON;
	if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) g_buttons |= MSYS_MIDDLE_BUTTON;

	if (p.iX != g_lastX || p.iY != g_lastY)
	{
		g_lastX = static_cast<INT16>(p.iX);
		g_lastY = static_cast<INT16>(p.iY);
		TacticalViewPortMovementCallback(g_lastX, g_lastY, MSYS_CALLBACK_REASON_MOVE);
	}
}


void Event(InputAtom const& e)
{
	if (!g_presence.In()) return;

	UINT32 reason = MSYS_CALLBACK_REASON_NONE;
	switch (e.usEvent)
	{
		case MOUSE_BUTTON_DOWN:
			reason = e.usParam == MOUSE_BUTTON_LEFT   ? MSYS_CALLBACK_REASON_LBUTTON_DWN :
			         e.usParam == MOUSE_BUTTON_RIGHT  ? MSYS_CALLBACK_REASON_RBUTTON_DWN :
			         e.usParam == MOUSE_BUTTON_MIDDLE ? MSYS_CALLBACK_REASON_MBUTTON_DWN : 0;
			break;

		case MOUSE_BUTTON_UP:
			reason = e.usParam == MOUSE_BUTTON_LEFT   ? MSYS_CALLBACK_REASON_LBUTTON_UP :
			         e.usParam == MOUSE_BUTTON_RIGHT  ? MSYS_CALLBACK_REASON_RBUTTON_UP :
			         e.usParam == MOUSE_BUTTON_MIDDLE ? MSYS_CALLBACK_REASON_MBUTTON_UP : 0;
			break;

		case MOUSE_WHEEL_UP:   reason = MSYS_CALLBACK_REASON_WHEEL_UP;   break;
		case MOUSE_WHEEL_DOWN: reason = MSYS_CALLBACK_REASON_WHEEL_DOWN; break;

		case TOUCH_FINGER_DOWN:
		{
			reason = MSYS_CALLBACK_REASON_TFINGER_DWN;
			UINT32 const now = GetJA2Clock();
			if (g_tapUp && now <= g_tapDownAt + DOUBLE_TAP_MS)
			{
				reason |= MSYS_CALLBACK_REASON_TFINGER_DOUBLETAP;
				g_tapUp = false;
				g_tapDownAt = 0;
			}
			else
			{
				g_tapDownAt = now;
				g_tapUp = false;
			}
			break;
		}

		case TOUCH_FINGER_UP:
			reason = MSYS_CALLBACK_REASON_TFINGER_UP;
			if (GetJA2Clock() <= g_tapDownAt + DOUBLE_TAP_MS) g_tapUp = true;
			else { g_tapUp = false; g_tapDownAt = 0; }
			break;

		case TOUCH_FINGER_REPEAT: reason = MSYS_CALLBACK_REASON_TFINGER_REPEAT; break;

		default: break;
	}
	if (reason != MSYS_CALLBACK_REASON_NONE) TacticalViewPortTouchCallback(reason);
}


bool In() { return g_presence.In(); }

UINT32 Buttons() { return g_presence.In() ? g_buttons : 0; }

INT16 ViewportRight()  { return static_cast<INT16>(gsVIEWPORT_END_X); }
INT16 ViewportBottom() { return static_cast<INT16>(gsVIEWPORT_WINDOW_END_Y); }

LayerPoint World()
{
	// the touch UI keeps the cursor where the finger left it
	SGPPoint const c = GetCursorPos();
	SGPPoint const m = GetMousePos();
	if (c.iX != m.iX || c.iY != m.iY) return g_ui.uiToWorld(c.iX, c.iY);
	MousePosF const f = GetMousePosF();
	return g_ui.uiToWorldF(f.x, f.y);
}

PointerRouter::Edges Edges() { return TacticalScreen() ? ReadEdges() : PointerRouter::Edges{}; }

void Reset()
{
	g_presence.Reset();
	g_buttons = 0;
	g_lastX = g_lastY = -1;
	g_tapUp = false;
	g_tapDownAt = 0;
}

}
