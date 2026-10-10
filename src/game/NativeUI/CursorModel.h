#pragma once
// The tactical cursor as data (issue #318, docs/plan/native-tactical.md "CursorModel"). Handle_UI keeps deciding what
// the pointer means (a UICursorID, the action points, the texts the legacy cursor wrote next to itself); this core
// says how it looks and reads: a shape, a tone, a marker on the tile and the lines of the chip. No globals, no
// RmlUi: the adapter (Tactical/CursorAdapter.cc) fills an Input from the legacy globals, the view draws a State, and
// Lua reads it as { mode, shape, ap, hit, why }.

#include <cstdint>
#include <string>
#include <vector>

namespace CursorModel
{
	/** What the pointer shows. The view maps a shape to an icon of the design system. */
	enum class Shape : uint8_t
	{
		None,    // the legacy cursor hid itself (nothing to draw)
		Pointer, // the arrow
		Walk, Run, Sneak, Crawl, Drive,
		Blocked, // no path / cannot go there
		Fire, Burst,
		Throw, Launch,
		Punch, Blade,
		FirstAid, Hand, Door, Talk, Give,
		Reload, Repair, Wirecut, Jar, Bomb, Remote, Can, Fuel, Vehicle,
		Look, Exit, NoExit, Swap, Jump,
		Invalid, Busy, Drop,
		Count
	};

	/** can do / with a catch / cannot / hostile (an attack). */
	enum class Tone : uint8_t { Ok, Warn, No, Foe };

	/** The coarse meaning, for Lua and for the view's decisions. */
	enum class Mode : uint8_t { None, Pointer, Move, MoveConfirm, MoveAll, Target, Melee, Throw, Use, Talk, Exit, Item, Busy };

	/** What goes on the tile under the pointer (the native marker replaces the snapping tile cursor). */
	enum class Marker : uint8_t { None, Tile, Confirm, Bad };

	struct Spec
	{
		int         id;     // the UICursorID, which is also the index in the table
		Shape       shape;
		Tone        tone;
		Mode        mode;
		Marker      marker;
		int         aim;    // the aim step 1..9 a target cursor stands for (0: not an aim cursor)
		char const* why;    // why the tone is not Ok ("" when it is): a key under tac.cur.why.*
	};

	/** The spec of a UICursorID; NO_UICURSOR's for an id out of range. Every id has one (see the unit test). */
	Spec const& SpecFor(int uiCursorId);
	int         SpecCount();

	char const* ShapeName(Shape);   // "walk", "fire", ... (Lua and the string table: tac.cur.<name>)
	char const* ToneName(Tone);
	char const* ModeName(Mode);
	char const* MarkerName(Marker);
	/** The icon of the design system for a shape (a file under assets/ui/icons without "icon-" and ".svg"), "" for none. */
	char const* ShapeIcon(Shape);

	/** What the legacy cursor knew, read from its globals by the adapter. */
	struct Input
	{
		int         id = 0;            // UICursorID
		bool        combat = false;    // turn based: action points are in play
		bool        showAp = false;    // gfUIDisplayActionPoints
		int         ap = 0;            // gsCurrentActionPoints: what the action costs
		bool        apInvalid = false; // gfUIDisplayActionPointsInvalid: the merc cannot pay it
		int         apLeft = -1;       // the selected merc's action points, -1 unknown
		std::string location;          // hit location ("Torso"), SetHitLocationText
		std::string tile;              // interactive tile text ("Drop", a door), SetIntTileLocationText
		std::string tile2;             // SetIntTileLocation2Text
		std::string chance;            // chance to hit as the legacy text ("64%"), SetChanceToHitText
		int         held = 0;          // an item is held by the pointer: 1 drop or throw, 2 a throw that cannot get there, 3 give
		bool        busy = false;      // the UI is locked (the enemy's turn, an animation): nothing can be clicked
		std::string target;            // the name of what is under the pointer, "" none
	};

	/** One line of the chip. kind: "hit" (a = percent), "aim" (a = steps done, b = steps in all), "ap" (a = cost,
	 * b = left or -1), "where" (text = hit location), "text" (text). */
	struct ChipLine
	{
		std::string kind;
		int         a = 0, b = 0;
		std::string text;
		Tone        tone = Tone::Ok;
	};

	struct State
	{
		bool        shown = false; // there is a cursor over the world at all
		Mode        mode = Mode::None;
		Shape       shape = Shape::None;
		Tone        tone = Tone::Ok;
		Marker      marker = Marker::None;
		int         ap = -1;       // the cost shown, -1 none
		int         apLeft = -1;
		int         hit = -1;      // chance to hit in percent, -1 none
		int         aim = 0;       // aim step 1..9, 0 none
		std::string why;           // "" when the tone is Ok
		std::string target;        // what is under the pointer ("" none)
		std::vector<ChipLine> lines;
		bool        chip = false;  // the chip is worth showing
	};

	/** Reads: the id's spec, then the AP, hit and texts of the input. A cursor that costs more than the merc has
	 * is a No with why "no_ap", whatever its id said. */
	State Evaluate(Input const&);

	/** Parses the legacy chance text ("64%", " 7 %") to a percent, -1 when it is not one. */
	int ParseChance(std::string const&);

	/** A move path as the cursor draws it. @a cumulativeAp is the cost of the path up to each step (the destination
	 * last), @a totalAp what the action costs in all (it may add a door, a stance change), @a budget the merc's
	 * action points. In real time nothing is limited: every step is solid. */
	struct PathPlan
	{
		int  steps = 0;   // steps of the path
		int  solid = 0;   // the first `solid` steps are this turn's
		int  total = 0;   // AP for all of it
		int  now = 0;     // AP spent this turn
		int  next = 0;    // AP that spills into the next turn
		bool beyond = false;
	};
	PathPlan PlanPath(std::vector<int> const& cumulativeAp, int totalAp, int budget, bool combat);
}
