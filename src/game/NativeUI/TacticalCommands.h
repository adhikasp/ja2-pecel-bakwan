#pragma once
// The tactical commands as data (issue #319, docs/plan/native-tactical.md "TacticalCommands"). One compiled table of
// everything the tactical HUD's buttons and the keyboard can ask for: an id, a name ("stance.crouch"), the string key
// of its label, an icon, a default key, the element id of its HUD button and an availability function that answers
// {enabled, why}. The HUD draws a disabled button and puts the reason in its tooltip; the keys go through the same
// table, so a button can never do what its key cannot, and the other way round.
//
// No globals, no RmlUi, no legacy headers: the adapter (Tactical/TacticalCommandsAdapter.cc) fills a Context from the
// soldier and interface globals and executes a command through the legacy action function (HandleStanceChange-
// FromUIKeys, ClimbUpOrDown, ...) - never through a key event. Lua reads the table with ja2.tactical.commands() and
// runs one with ja2.tactical.run("stance.crouch").

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TacticalCommands
{
	/** The screen the commands belong to, in the KeyBindings registry (issue #48): a key runs one action per screen. */
	inline constexpr char const* SCOPE = "tactical";

	enum class Id : uint8_t
	{
		// a soldier's body
		StanceStand, StanceCrouch, StanceProne, StanceUp, StanceDown,
		Run, Stealth, Climb, Level,
		// what the pointer does next
		Burst, Look, Talk, SwapHands,
		// the turn and the game flow
		EndTurn, Map, Options, Overhead, Pause,
		// the HUD's own panels
		Detail, DetailClose, KeyRing, Log, Mute, Readout, Loadout,
		// who is selected
		MercNext, SquadNext, Locate,
		// what the world shows
		Roofs, Glow,
		// F1..F10 select the n-th merc of the squad bar, 1..0 the n-th squad
		Merc1, Merc2, Merc3, Merc4, Merc5, Merc6, Merc7, Merc8, Merc9, Merc10,
		Squad1, Squad2, Squad3, Squad4, Squad5, Squad6, Squad7, Squad8, Squad9, Squad10,
		Count
	};

	constexpr int COUNT = std::to_underlying(Id::Count);

	/** Why a command is off: a key under tac.cmd.why.* in the string table. */
	enum class Why : uint8_t
	{
		None,
		NoMerc,      // nobody is selected
		Dead,        // the merc is down
		NotYourTurn, // the enemy is moving
		HandFull,    // an item is held: put it down first
		OutOfReach,  // the pockets are out of reach (a pick-up menu is open, or the merc is cut off)
		Busy,        // a conversation or a cinematic
		Locked,      // the game flow disables the buttons (a message box, the overhead map)
		Robot,       // the robot cannot do this
		Vehicle,     // a vehicle cannot do this
		Water,       // not while in the water
		NoBurst,     // the weapon in hand has no burst
		NothingToClimb,
		NoRoof,      // nothing to look at on another level
		NoTurnBased, // the game does not enter turn-based combat from here
		Already,     // you are there already
		EmptyHands,  // nothing in either hand to swap
		NoStance,    // the stance cannot be taken here
		Nobody,      // there is no such merc or squad
		NoDetail     // the detail panel is not open
	};

	char const* WhyName(Why); // "not_your_turn", ...

	/** What the adapter reads out of the game for the availability functions. All plain data. */
	struct Context
	{
		bool hasMerc = false;       // someone is selected
		bool alive = false;         // and he is on his feet (life >= OKLIFE)
		bool combat = false;        // turn-based combat
		bool ourTurn = true;        // it is our team's turn (always true in real time)
		bool handFull = false;      // an item is held (the inventory core's hand)
		bool itemsLocked = false;   // the pockets are out of reach (gfSMDisableForItems)
		bool panelsLocked = false;  // the game flow disables the HUD's buttons (gfDisableTacticalPanelButtons)
		bool conversation = false;  // ENGAGED_IN_CONV
		bool pickupOpen = false;    // the pick-up menu is open
		bool meanwhile = false;     // a cinematic
		bool mapDisabled = false;   // the map screen is disabled by a battle
		bool overhead = false;      // the overhead map is open
		bool robot = false, vehicle = false, inWater = false;
		int  stance = 0;            // 0 stand, 1 crouch, 2 prone
		bool canCrouch = true, canProne = true;
		bool canClimb = false;      // a ledge, a roof edge or a fence next to him, and the points to use it
		bool canBurst = false;      // the weapon in hand (or its launcher) has a second mode
		bool canSwapHands = false;  // something in either hand, and not the robot
		bool onRoof = false;        // the cursor level is the roof
		bool roofAvailable = false; // there is a roof level to look at
		bool detailOpen = false;    // the merc detail panel is open
		bool muted = false;
		bool turnBasedAllowed = true; // the game policy lets the player enter turn-based combat
		bool pointerHolds = false;  // a drag or a menu holds the pointer
		bool keyRingOpen = false;
		uint16_t slotMask = 0;      // bit n: the n-th card of the squad bar has a merc
		bool otherSquads = false;   // another squad has someone in the sector
		bool otherMercs = false;    // another merc of the squad could be selected
	};

	struct Availability
	{
		bool enabled = true;
		Why  why = Why::None;
	};

	using AvailabilityFn = Availability (*)(Context const&, Id);

	struct Command
	{
		Id          id;
		char const* name;   // "stance.crouch"
		char const* group;  // "stance", "move", "act", "turn", "nav", "panel", "select", "view"
		char const* label;  // string key: tac.cmd.<name>
		char const* icon;   // an icon of the design system ("stance-crouch"); "" for none
		char const* key;    // the default key, "" for none: "c", "shift+space", "tab", "ctrl+q", "f3", "`"
		char const* hud;    // the element id of its HUD button, "" for none
		int         arg;    // the n of Merc<n> / Squad<n> (0-based), else 0
		AvailabilityFn availability;
	};

	/** The table, in Id order. */
	std::vector<Command> const& All();
	Command const& Get(Id);
	/** The command named @a name ("stance.crouch"), or nullptr. */
	Command const* Find(std::string_view name);
	/** The command a key runs: @a key is the key's name as KeyBindings::KeyName gives it, nullptr when none. The
	 * keys are the rebindable ones (KeyBindings): the default unless the player changed it. */
	Command const* ForKey(std::string_view key, bool ctrl, bool shift, bool alt);

	Availability Check(Context const&, Id);
	inline Availability Check(Context const& c, Command const& cmd) { return Check(c, cmd.id); }
}
