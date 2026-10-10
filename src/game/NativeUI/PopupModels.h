#pragma once
// The tactical popups as data (issue #321, docs/plan/native-tactical.md "PopupModels"): the action menu, the door
// menu, the pick-up list, the stack popup, the key ring, the talk panel and the sector exit menu. What each one
// offers, what is off and why, and what a click means are decided here, with no globals and no RmlUi. The adapters
// (Tactical/PopupAdapter.cc and the legacy files that own the soldier and item globals) fill the plain inputs, apply
// the answer and raise the Observables; the native HUD (NativeUI/TacticalHud.cc) draws the rows; Lua reads them
// through ja2.popup(). PopupModels_unittest.cc asserts every rule against hand-made values.

#include <cstdint>
#include <string>
#include <vector>

namespace PopupModels
{

/** Why a row is off. Codes are the keys of the HUD's "tac.why.<code>" strings (WhyKey). */
enum class Why : uint8_t
{
	None,
	NotHere,        // "no": not available for this merc here
	Vehicle,        // a vehicle cannot do this
	Robot,          // the robot cannot
	ControlRobot,   // the robot has nobody to control it
	Water,          // not in water
	Escort,         // an escorted person cannot
	Stance,         // the stance is not possible here
	NoUse,          // nothing in the hand to act with
	NoKey,          // no key for this door
	NoCrowbar,
	NoLockpick,
	NoCharge,
	Examined,       // already examined
	NoTrap,         // no trap found yet
	Diagonal,       // not from this angle
	NoAp,           // not enough action points
	NoDoor,         // point at a locked door first
	Unlocked,       // the door is not locked
	WrongKey,       // this key does not fit that door
	Speaking,       // wait for the speaker to finish
	MustTravel,     // the neighbour is too far to load: it is a trip on the map
	Hostile,        // cannot leave several squads in a hostile sector
	MustLoad,       // the whole squad goes, so the next sector loads
	MustBeEscorted, // the selected merc is an escort: he goes with the squad
	WouldIsolate,   // he cannot go alone: he would leave an escort alone
	WouldIsolateMany,
	NeedsTogether,  // everyone must be together to go as a squad
	Count
};
char const* WhyKey(Why);

/** One row of a menu. `cmd` is an ActionCmd or a DoorCmd (the menu says which); `group` splits the menu. */
struct Row
{
	int  cmd     = 0;
	int  group   = 0;
	bool enabled = true;
	Why  why     = Why::None;
	int  ap      = -1; // what it costs, -1 free
};
struct Menu
{
	std::vector<Row> rows;
	Row const* Find(int cmd) const;
	/** The row exists and is on. */
	bool CanChoose(int cmd) const;
};

// ---- the action menu (a right click held on the terrain, U) --------------------------------------------

enum class ActionCmd : uint8_t { Walk, Run, Sneak, Crawl, Act, Look, Talk, Hand, Cancel, Count };
char const* ActionCmdName(int cmd); // "walk", "run", ... (Lua, ids)

/** What the merc holds, as far as the Act row is concerned. */
enum class HandItem : uint8_t { Nothing, Toolkit, Wirecutters, Punch, Gun, Blade, Explosive, Medkit, Other };

struct ActionInput
{
	bool     vehicle = false;
	bool     robot = false;
	bool     robotUncontrolled = false;
	bool     escort = false;     // an EPC
	bool     inWater = false;
	bool     canCrouch = true;
	bool     canProne = true;
	HandItem hand = HandItem::Nothing;
	int      actAp = -1;         // shooting or stabbing with the item in hand
};
/** Move (walk, run, sneak, crawl), Act (action, look, talk, hand), Cancel. */
Menu BuildActionMenu(ActionInput const&);

// ---- the door menu (clicking a door) ------------------------------------------------------------------

enum class DoorCmd : uint8_t { Open, Examine, Untrap, Keyring, Lockpick, Crowbar, Boot, Explosive, Cancel, Count };
char const* DoorCmdName(int cmd);

enum class Trap : uint8_t { Unknown, ProvedTrapped, ProvedUntrapped };

struct DoorInput
{
	bool closing = false;       // the door is open: the menu offers to close it
	bool escort = false;
	bool hasKey = false;
	bool hasCrowbar = false;
	bool hasLockpick = false;
	bool hasCharge = false;
	bool diagonal = false;      // the merc faces a diagonal: only open and cancel work
	Trap trap = Trap::Unknown;
	int  ap[int(DoorCmd::Count)] = {};         // what each action costs
	bool affordable[int(DoorCmd::Count)] = { true, true, true, true, true, true, true, true, true };
};
/** What can be done with the door (Open, Examine, Untrap), the tools (key ring, lockpick, crowbar, boot, charge), Cancel. */
Menu BuildDoorMenu(DoorInput const&);

// ---- the pick-up list ---------------------------------------------------------------------------------

constexpr int PICKUP_PAGE = 6;

/** The items on the ground, a page of PICKUP_PAGE at a time, and which are ticked. */
class PickupList
{
public:
	PickupList() = default;
	explicit PickupList(int total) : selected_(total > 0 ? total : 0, false) {}

	int  Total() const { return int(selected_.size()); }
	int  Pages() const { return (Total() + PICKUP_PAGE - 1) / PICKUP_PAGE; }
	int  Page() const { return page_; }
	/** First item index of the page and how many rows it has. */
	int  First() const { return page_ * PICKUP_PAGE; }
	int  Rows() const { int const n = Total() - First(); return n < 0 ? 0 : n < PICKUP_PAGE ? n : PICKUP_PAGE; }
	bool CanUp() const { return page_ > 0; }
	bool CanDown() const { return page_ + 1 < Pages(); }
	void Scroll(int dir);

	/** Row @a row of the page (0..Rows()-1). False when there is no such row. */
	bool Toggle(int row);
	bool IsSelected(int item) const { return item >= 0 && item < Total() && selected_[item]; }
	bool RowSelected(int row) const { return row >= 0 && row < Rows() && selected_[First() + row]; }
	int  Count() const;
	bool Any() const { return Count() > 0; }
	bool AllSelected() const { return Total() > 0 && Count() == Total(); }
	/** The All button: everything, or - when everything is ticked - nothing. */
	void ToggleAll();
	std::vector<bool> const& Selected() const { return selected_; }

private:
	std::vector<bool> selected_;
	int page_ = 0;
};

// ---- the stack popup ----------------------------------------------------------------------------------

/** What a click on box @a i of a stack popup of @a slots boxes holding @a count objects does. */
enum class StackClick : uint8_t { Nothing, Take, Put };
StackClick PlanStackClick(int i, int count, bool holding);

/** The "take n" stepper: how many of the stack go into the hand. */
class StackSplit
{
public:
	StackSplit() = default;
	explicit StackSplit(int count) : count_(count < 0 ? 0 : count), take_(count > 0 ? 1 : 0) {}
	int  Count() const { return count_; }
	int  Take() const { return take_; }
	bool CanMore() const { return take_ < count_; }
	bool CanLess() const { return take_ > 1; }
	void More() { if (CanMore()) ++take_; }
	void Less() { if (CanLess()) --take_; }
	/** The popup lost an object: keep the split inside it. */
	void SetCount(int count);

private:
	int count_ = 0, take_ = 0;
};

// ---- the key ring -------------------------------------------------------------------------------------

struct KeyIn
{
	int slot  = 0;  // position on the ring
	int count = 0;
	int keyId = 0;  // KeyTable index, which is the lock id it opens
};

struct KeyRowOut
{
	int  slot = 0, count = 0, keyId = 0;
	bool fits = false;     // it opens the door in front of the merc
	bool canUse = false;
	Why  why = Why::None;  // why Use is off
};

struct KeyRingInput
{
	std::vector<KeyIn> keys;
	bool doorInFront = false;   // a door is next to the merc, where he faces
	bool doorLocked = false;
	int  doorLockId = -1;
	bool canPay = true;         // the merc can pay the unlock's AP
};
/** One row per key with Use's verdict. */
std::vector<KeyRowOut> BuildKeyRing(KeyRingInput const&);
/** Dropping a key on a door (the gesture): the same verdict as the Use button for that key. */
KeyRowOut PlanKeyOnDoor(KeyIn const& key, KeyRingInput const& ring);

// ---- the talk panel -----------------------------------------------------------------------------------

/** In the order the panel shows them (keys 1..6); not the legacy order of the approach table. */
enum class Approach : uint8_t { Friendly, Direct, Threaten, Give, Recruit, Repeat, Count };
char const* ApproachName(int a);

struct TalkInput
{
	bool speaking = false;   // the NPC (or a queued line) is talking: the approaches wait
	bool dealer = false;     // "Give" is "Trade" with an arms dealer
};
struct TalkRow { int approach; bool enabled; Why why; };
std::vector<TalkRow> BuildTalkMenu(TalkInput const&);

// ---- the sector exit menu -----------------------------------------------------------------------------

enum class ExitJump : uint8_t { None, AllLoad, AllNoLoad, SingleLoad, SingleNoLoad };

struct ExitInput
{
	bool okSingle = false;           // OKForSectorExit said 1: only the selected merc may go
	bool okAll = false;              // ... said 2: the squad goes together
	bool robotUncontrolled = false;
	bool shortTrip = false;          // the neighbour is five minutes away or less: it may load now
	bool combat = false;
	int  controllableMercs = 0;      // our mercs who can act (in combat)
	bool multipleSquads = false;     // other mercs of ours in the sector are not in this squad
	bool enemyInSector = false;
	int  militiaInSector = 0;
	bool selectedIsEscort = false;
	int  squadSize = 0;              // the selected merc's squad, controllable
	bool otherMercInSquad = false;   // a merc who is not an escort shares the selected merc's assignment
	int  escortsInSquad = 0;         // escorts that share it (when no other merc does)
};

struct ExitState
{
	bool single = false, all = false, load = false;
	bool singleOn = false, allOn = false;        // what the legacy dialogue called fSingleMoveOn/fAllMoveOn
	bool singleOff = false, allOff = false, loadOff = false;
	bool loadHint = false;                       // load is a five minute hop, not a map trip ("go to the sector")
	Why  singleWhy = Why::None, allWhy = Why::None, loadWhy = Why::None;
	int  squadSize = 0;
};

/** The dialogue as it opens. */
ExitState OpenExit(ExitInput const&);
/** The Selected merc radio. Ignored when it is off. */
void ChooseSingle(ExitState&, ExitInput const&);
/** The Everyone-in-the-squad radio. */
void ChooseAll(ExitState&, ExitInput const&);
/** The load checkbox. Ignored when it is off. */
void ToggleLoad(ExitState&);
/** What OK does. */
ExitJump Confirm(ExitState const&);
char const* ExitJumpName(ExitJump);

}
