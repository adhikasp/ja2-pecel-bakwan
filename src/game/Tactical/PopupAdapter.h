#pragma once

#include "Observable.h"
#include "PopupModels.h"

#include <string_theory/string>

#include <string>
#include <vector>

/** @file
 * The tactical popups' adapter (issue #321). The menus and popups of tactical are models (NativeUI/PopupModels.h):
 * this header is what the native HUD and Lua read and call. The state lives where the legacy globals are
 * (Interface.cc: the action and door menus; Interface_Items.cc: the pick-up list, the stack popup and the key ring;
 * Interface_Dialogue.cc: the talk panel; Strategic_Exit_GUI.cc: the sector exit; Dialogue_Control.cc: the speaking
 * face and its subtitle). None of them makes a button or a mouse region for tactical: a choice arrives as a call
 * below, the answer is applied through the same legacy action as before, and the Observables are raised.
 */

enum class PopupKind : uint8_t { None, Action, Door, Pickup, Stack, KeyRing, Talk, Exit };
char const* PopupKindName(PopupKind);

/** One decision of a popup, as the Observables and Lua carry it. */
struct PopupEvent
{
	PopupKind   kind = PopupKind::None;
	std::string action;       // "open", "choose", "toggle", "take", "put", "use", "go", "close", ...
	std::string what;         // the row: "walk", "boot", "keyring", "friendly", "single", ...
	int         id = -1;      // the row's number where it has one (a pocket, a key slot, a page row)
	bool        ok = true;
	std::string why;          // the refusal, "" when it worked
};

/** Raised before a choice is applied (the popup is still as the player saw it). */
extern Observable<PopupEvent const&> BeforePopupChoice;
/** Raised after it was applied, or refused (ok == false, why says what). */
extern Observable<PopupEvent const&> OnPopupChoice;

/** What the last native popup click did: the Lua surface and the HUD's hint line read it. */
PopupEvent const& LastPopupOutcome();
/** Records an outcome and raises OnPopupChoice. Returns e.ok. */
bool FinishPopup(PopupEvent const& e);
/** Raises BeforePopupChoice. */
void BeginPopup(PopupEvent const& e);

/** A pop-up is open that takes the whole screen's pointer (the world must not see clicks). */
bool AnyTacticalPopupOpen();

// ---- the action and door menus (Interface.cc) -----------------------------------------------------------

struct PopupRow
{
	int         cmd = -1;        // PopupModels::ActionCmd or DoorCmd
	std::string name;            // "walk", "boot", ...
	int         group = 0;
	bool        enabled = true;
	std::string why;             // PopupModels::WhyKey, "" when on
	int         ap = -1;         // -1: free or out of combat
	ST::string  label, kbd, title;
	std::string icon;
};

struct MenuPopup
{
	bool        open = false;
	bool        door = false;    // the door menu, else the action menu
	int         x = 0, y = 0;    // where it goes, in UI pixels (the legacy anchor)
	ST::string  who;             // the merc it is for
	int         apLeft = -1;
	int         doorGrid = -1;   // the door menu: the door's tile
	int         doorLock = -1;   // ... and the lock it has (the key id that opens it)
	std::vector<PopupRow> rows;
};
MenuPopup CurrentMenuPopup();
/** Chooses a row of the open menu by its command. Refuses a row that is off. */
bool PopupMenuChoose(int cmd);
/** Closes the open menu as Cancel / Esc does. */
void PopupMenuCancel();

// ---- the pick-up list (Interface_Items.cc) --------------------------------------------------------------

struct PickupRowView
{
	int         row = -1;        // the row on the page
	int         item = 0;
	int         cond = 0;
	ST::string  name, count, title;
	bool        empty = true, sel = false, att = false;
};

struct PickupPopup
{
	bool        open = false;
	int         x = 0, y = 0;
	ST::string  who;
	int         total = 0, page = 0, pages = 0, selected = 0;
	bool        canUp = false, canDown = false, canTake = false, all = false;
	std::vector<PickupRowView> rows;
};
PickupPopup CurrentPickupPopup();
bool PickupToggle(int row);
bool PickupAll();
bool PickupScroll(int dir);
bool PickupTake();
void PickupCancel();
/** The pointer is over a row (-1: it left the list): the compatible ammo of the squad lights up. */
void PickupHover(int row);

// ---- the stack popup (Interface_Items.cc) ---------------------------------------------------------------

struct StackBox
{
	int         index = 0;
	bool        filled = false;
	int         status = 0;      // the object's condition (percent) or rounds of a magazine
	ST::string  text;            // "15/15", "80%"
	bool        low = false;
};

struct StackPopup
{
	bool        open = false;
	int         item = 0;
	int         slot = -1;       // the pocket it came out of
	ST::string  name, where;     // "9mm magazines", the merc
	int         slots = 0, count = 0, take = 0;
	bool        holding = false; // something is in the hand: a click puts it down
	bool        canMore = false, canLess = false;
	std::vector<StackBox> boxes;
};
StackPopup CurrentStackPopup();
/** A click on box @a i: takes that object into the hand, or puts the hand's object there. */
bool StackClickBox(int i);
/** Takes the "take n" count (the last n objects) into the hand. */
bool StackTakeSplit();
bool StackTakeAll();
bool StackSplitStep(int dir);
/** The box's description sheet (the right click). */
bool StackDescribe(int i);
void StackClose();

// ---- the key ring (Interface_Items.cc) ------------------------------------------------------------------

struct KeyView
{
	int         slot = 0, count = 0, keyId = 0, item = 0;
	ST::string  name, sector;    // "Small key", the sector it was found in ("A9")
	int         day = 0;         // the day it was found
	bool        fits = false, canUse = false;
	std::string why;             // why Use is off
};

struct KeyRingPopup
{
	bool        open = false;
	ST::string  who;
	bool        door = false;    // a door is in front of the merc
	bool        doorLocked = false;
	bool        holding = false;     // a key is in the hand: Put it back
	std::vector<KeyView> keys;
};
KeyRingPopup CurrentKeyRingPopup();
/** Uses the key on the locked door in front of the merc whose ring it is. */
bool KeyRingUse(int slot);
/** Takes the key into the hand (to hand it to a merc, or to drop it on a door). */
bool KeyRingTake(int slot);
bool KeyRingDescribe(int slot);
/** A key in the hand goes back on this ring. */
bool KeyRingPut();
void KeyRingClose();
/** A key in the hand dropped on the door at @a gridno: the gesture. Returns false when it cannot be done. */
bool UseHeldKeyOnDoor(int gridno);

// ---- the talk panel (Interface_Dialogue.cc) -------------------------------------------------------------

struct TalkRowView
{
	int         approach = 0;
	std::string name;
	ST::string  label;
	std::string key;             // "1".."6"
	bool        enabled = true;
	std::string why;
	ST::string  title;
};

struct TalkPopup
{
	bool        open = false;
	int         profile = -1;    // the NPC
	int         face = 0;        // the face file (FACES/<nn>.sti)
	ST::string  name, role;      // nickname, what the player knows about them
	ST::string  line;            // what they just said
	ST::string  previous;        // what the merc said before
	ST::string  asker;           // the merc talking to them
	bool        speaking = false;
	std::vector<TalkRowView> rows;
};
TalkPopup CurrentTalkPopup();
bool TalkChoose(int approach);
bool TalkWho();
/** Done: stops the line when one is playing, else closes the panel. */
bool TalkDone();
bool TalkSkip();

// ---- the speaking face and its subtitle (Dialogue_Control.cc) -------------------------------------------

struct SpeechView
{
	bool        shown = false;
	int         profile = -1;
	int         face = 0;
	ST::string  who, line, where;
	bool        soldier = false;  // one of our mercs in the sector: the subtitle rides over his head
	int         gridno = -1;
	bool        speaking = false;
};
SpeechView CurrentSpeech();
/** The click on the face: stop the line. */
void SpeechClick();

// ---- the sector exit menu (Strategic_Exit_GUI.cc) -------------------------------------------------------

struct ExitPopup
{
	bool        open = false;
	ST::string  direction;        // "north", "east", ... or "exit" for an exit grid
	ST::string  from, to;         // sector ids ("A9", "A10"); to is empty for an exit grid
	int         minutes = -1;     // the trip
	bool        shortTrip = false;
	ST::string  selected;         // the selected merc
	ST::string  selectedLabel, allLabel, loadLabel; // the legacy captions of the two radios and the box
	int         squadSize = 0;
	bool        singleSel = false, allSel = false, loadSel = false;
	bool        singleOff = false, allOff = false, loadOff = false;
	bool        loadNow = false;  // loading the neighbour is a short hop (else the trip is made on the map)
	std::string singleWhy, allWhy, loadWhy;   // PopupModels::WhyKey
	ST::string  singleTip, allTip, loadTip;   // the legacy help text, with the names in it
	bool        canGo = false;
	std::string jump;             // PopupModels::ExitJumpName: what OK does
};
ExitPopup CurrentExitPopup();
bool ExitChooseSingle();
bool ExitChooseAll();
bool ExitToggleLoad();
bool ExitGo();
void ExitCancel();
