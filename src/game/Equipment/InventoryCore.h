#pragma once
// The inventory core (issue #317, docs/plan/native-tactical.md "InventoryCore"): the rules of moving
// an item between pockets, mercs and the hand, with no game state in them. The hand-held stack, the
// questions a move asks the player, the money split, and the verdict for every click are decided
// here; the adapter (Tactical/InventoryAdapter.cc, Interface_Panels.cc, Interface_Items.cc) fills the
// plain inputs from the live soldiers, applies the verdict to SOLDIERTYPE::inv and the item pools,
// raises the Observables and plays the quote. InventoryCore_unittest.cc asserts every rule against
// hand-made values.

#include <cstdint>
#include <functional>
#include <string>

namespace Equipment
{

// What the AP check of one move costs, per merc, when two different mercs are involved
// (SMInvClickCallbackPrimary in the legacy panel: "3 to be able to, 2 to do it").
constexpr int PASS_CHECK_AP  = 3;
constexpr int PASS_COST_AP   = 2;

enum class InvWhy : uint8_t
{
	None,
	HandEmpty,       // nothing in the hand to put down
	NothingHere,     // the pocket is empty
	NailsVest,       // Nails never takes his vest off
	NoAP,            // the merc handing the item over cannot pay
	NoAPTarget,      // the merc receiving it cannot pay
	Unconscious,     // an unconscious merc cannot use it
	NotAttachable,   // the sheet's attachment slot takes nothing of this kind
	HandFull,        // the hand already holds something
	NothingToUnload, // the gun is empty
	NothingToTake,   // an empty attachment position
	MoneyShort,      // the pile has less than the step
	MoneyCap,        // more than one pocket holds
	OutOfReach,      // the merc is too far away, or out of sight, to hand things to
	DoesNotFit,      // the pocket does not take this item (size, pocket kind, worn gear)
};

/** The refusal in plain words (English; the HUD maps the enum to its own strings). */
const char* Describe(InvWhy why);

/** One merc as the rules see him. */
struct InvParty
{
	int  merc      = -1;    // the soldier's id, -1 for nobody (a sector pile, the map)
	bool conscious = true;  // a conscious merc pays AP, an unconscious one is just a body
	bool canPay    = true;  // the 3 AP check passed (EnoughPoints in combat, always true out of it)
	bool inReach   = true;  // close enough and in sight: the panel's pockets are live
};

/** The stack in the hand. The OBJECTTYPE itself stays in the adapter; this is its identity. */
struct HeldStack
{
	uint16_t item      = 0;   // 0: nothing
	int      count     = 0;
	InvParty from;            // whose pocket it came out of
	int      fromSlot  = -1;  // the pocket, -1 when it came from the ground or the key ring

	bool Empty() const { return item == 0 || count <= 0; }
};

/** What the item rules say. The adapter supplies the game's tables; a test supplies fakes. */
struct InvRelations
{
	std::function<bool(uint16_t held, uint16_t host)>   canAttach; // ValidAttachment
	std::function<bool(uint16_t held, uint16_t target)> canMerge;  // ValidMerge
	/** Nails and his vest: true when taking @a replacement into @a slot of @a merc is refused. */
	std::function<bool(InvParty const& merc, int slot, uint16_t replacement)> fetish;
	/** Whether the hand's item fits pocket @a slot of @a merc (CanItemFitInPosition). Unset: everything fits. */
	std::function<bool(InvParty const& merc, int slot, uint16_t item)> fits;
};

// ---- the verdicts --------------------------------------------------------------------------------

enum class TakeKind : uint8_t { Refused, Take, CleanUp };
struct TakeVerdict
{
	TakeKind kind = TakeKind::Refused;
	InvWhy   why  = InvWhy::None;
};

enum class PlaceKind : uint8_t
{
	Refused,
	Put,      // PlaceObject: into the pocket, swapping what was there
	Attach,   // attach the held item to the item that is there: open the sheet
	AskMerge, // a question: ValidMerge, the player decides
	CleanUp,  // Ctrl+click: gather the stack in the pocket and the hand
};
struct PlaceVerdict
{
	PlaceKind kind    = PlaceKind::Refused;
	InvWhy    why     = InvWhy::None;
	int       apFrom  = 0;      // charged to the merc the item came from once it is down
	int       apTo    = 0;      // charged to the merc it went to
	bool      passed  = false;  // the "item passed to merc" message is due
	bool      fetish  = false;  // the refusal is the vest fetish: the adapter plays Nails' line
};

enum class ActivateKind : uint8_t { Nothing, OpenStack, Describe };

enum class QuestionKind : uint8_t { None, Merge, PermanentAttachment };
struct Question
{
	QuestionKind kind = QuestionKind::None;
	int          merc = -1;
	int          slot = -1;
	uint16_t     held = 0;
	uint16_t     host = 0;
	int          apFrom = 0;     // when the answer is no the item is put down instead, and this is charged
	int          apTo   = 0;
};

enum class SheetAttachKind : uint8_t { Refused, Attach, AskPermanent, Detach, Ignored };
struct SheetAttachVerdict
{
	SheetAttachKind kind = SheetAttachKind::Refused;
	InvWhy          why  = InvWhy::None;
};

struct SheetAttachRequest
{
	bool sheetIsAttachment = false; // the sheet describes an attachment itself: nothing mounts on it
	bool shopOwned         = false; // the shopkeeper's item: look, don't touch
	bool handFull          = false;
	bool payerCanPay       = true;  // the reload AP (plus the pick-up AP when taking one out)
	bool handInseparable   = false; // the held item cannot be taken off again
	bool inseparableValid  = false; // and it does attach to this host
	bool slotOccupied      = false; // something is mounted at the position
};

struct UnloadVerdict
{
	bool   ok  = false;
	InvWhy why = InvWhy::None;
};

// ---- the money split -----------------------------------------------------------------------------

/** The slider the money sheet shows: how much stays, how much goes into the hand. */
class MoneySplit
{
public:
	MoneySplit() = default;
	/** @a maxPerSlot 0 means no cap; a withdrawal from the account is capped at one pocket's worth. */
	MoneySplit(uint32_t total, uint32_t maxPerSlot = 0)
		: total_(total), remaining_(total), maxPerSlot_(maxPerSlot) {}

	/** A split in progress: @a removing of @a total already moved to the hand. */
	static MoneySplit Of(uint32_t total, uint32_t removing, uint32_t maxPerSlot = 0)
	{
		MoneySplit m(total, maxPerSlot);
		m.removing_  = removing <= total ? removing : total;
		m.remaining_ = total - m.removing_;
		return m;
	}

	uint32_t Total() const     { return total_; }
	uint32_t Remaining() const { return remaining_; }
	uint32_t Removing() const  { return removing_; }

	/** Move @a amount from the pile to the hand (left click). */
	InvWhy Add(uint32_t amount);
	/** Put @a amount back (right click). */
	InvWhy Remove(uint32_t amount);
	/** The steps the sheet offers are only enabled when the pile can afford them. */
	bool CanAdd(uint32_t amount) const { return amount != 0 && remaining_ >= amount; }

private:
	uint32_t total_ = 0, remaining_ = 0, removing_ = 0, maxPerSlot_ = 0;
};

// ---- the core ------------------------------------------------------------------------------------

class InventoryCore
{
public:
	explicit InventoryCore(InvRelations rules = {}) : rules_(std::move(rules)) {}

	void SetRules(InvRelations rules) { rules_ = std::move(rules); }

	// the hand
	HeldStack const& Hand() const { return hand_; }
	bool Holding() const          { return !hand_.Empty(); }
	/** Whatever the adapter put in the hand (or the map's cursor): the one place that says what it is. */
	void SetHand(HeldStack const& s) { hand_ = s; }
	void ClearHand()                 { hand_ = HeldStack{}; }

	// the verdicts. They look; they never change the hand.
	/** Picking up from @a slot of @a holder, which holds @a item. */
	TakeVerdict  PlanTake(InvParty const& holder, int slot, uint16_t item, bool ctrl) const;
	/** Putting the hand down into @a slot of @a target, which holds @a occupant (0 when empty).
	 *  @a hostSlot: the pocket is one that takes attachments (hands, helmet, vest, legs). */
	PlaceVerdict PlanPlace(InvParty const& target, int slot, uint16_t occupant, bool hostSlot, bool ctrl) const;
	/** The right click on a pocket: a stack opens the popup, everything else its description. */
	ActivateKind PlanActivate(uint16_t item, int count, int slotLimit, bool popupAllowed) const;
	/** Dropping the hand on a merc's portrait: camo, a canteen, a drug. */
	InvWhy       PlanApply(InvParty const& target) const;
	/** Giving the hand to another merc by dropping it on his face. */
	PlaceVerdict PlanGive(InvParty const& target) const;
	/** The sheet's attachment position: attach the hand, or take the attachment out. */
	SheetAttachVerdict PlanSheetAttach(SheetAttachRequest const& r) const;
	/** The sheet's unload button. */
	UnloadVerdict PlanUnload(bool isGun, bool hasRounds, bool handFull) const;

	// the question a move asked and is waiting on
	void Ask(Question const& q) { pending_ = q; }
	Question const& Pending() const { return pending_; }
	/** Takes the question away, returning it so the adapter can act on the answer. */
	Question Answer() { Question q = pending_; pending_ = Question{}; return q; }
	bool Asking() const { return pending_.kind != QuestionKind::None; }

private:
	InvRelations rules_;
	HeldStack    hand_;
	Question     pending_;
};

}
