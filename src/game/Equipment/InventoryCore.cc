#include "InventoryCore.h"

namespace Equipment
{

const char* Describe(InvWhy const why)
{
	switch (why)
	{
		case InvWhy::None:            return "";
		case InvWhy::HandEmpty:       return "nothing in the hand";
		case InvWhy::NothingHere:     return "the pocket is empty";
		case InvWhy::NailsVest:       return "he is not taking that vest off";
		case InvWhy::NoAP:            return "not enough action points";
		case InvWhy::NoAPTarget:      return "the other merc has not enough action points";
		case InvWhy::Unconscious:     return "he is out cold";
		case InvWhy::NotAttachable:   return "it does not attach to this";
		case InvWhy::HandFull:        return "the hand is full";
		case InvWhy::NothingToUnload: return "nothing to unload";
		case InvWhy::NothingToTake:   return "nothing is mounted there";
		case InvWhy::MoneyShort:      return "not enough money in the pile";
		case InvWhy::MoneyCap:        return "a pocket holds no more than that";
		case InvWhy::OutOfReach:      return "too far away";
		case InvWhy::DoesNotFit:      return "it does not fit there";
	}
	return "";
}

InvWhy MoneySplit::Add(uint32_t const amount)
{
	if (amount == 0 || remaining_ < amount) return InvWhy::MoneyShort;
	if (maxPerSlot_ != 0 && removing_ + amount > maxPerSlot_) return InvWhy::MoneyCap;
	remaining_ -= amount;
	removing_  += amount;
	return InvWhy::None;
}

InvWhy MoneySplit::Remove(uint32_t const amount)
{
	if (amount == 0 || removing_ < amount) return InvWhy::MoneyShort;
	remaining_ += amount;
	removing_  -= amount;
	return InvWhy::None;
}

TakeVerdict InventoryCore::PlanTake(InvParty const& holder, int const slot, uint16_t const item, bool const ctrl) const
{
	if (!holder.inReach) return { TakeKind::Refused, InvWhy::OutOfReach };
	if (item == 0) return { TakeKind::Refused, InvWhy::NothingHere };
	if (rules_.fetish && rules_.fetish(holder, slot, 0)) return { TakeKind::Refused, InvWhy::NailsVest };
	if (ctrl) return { TakeKind::CleanUp, InvWhy::None };
	return { TakeKind::Take, InvWhy::None };
}

PlaceVerdict InventoryCore::PlanPlace(InvParty const& target, int const slot, uint16_t const occupant,
	bool const hostSlot, bool const ctrl) const
{
	PlaceVerdict v;
	if (hand_.Empty())
	{
		v.why = InvWhy::HandEmpty;
		return v;
	}

	if (!target.inReach)
	{
		v.why = InvWhy::OutOfReach;
		return v;
	}

	// Passing an item between two mercs: each conscious one needs 3 AP to start and pays 2 to finish.
	// A hand that came off the ground or the map has no merc behind it and costs nothing.
	bool const crossing = hand_.from.merc >= 0 && hand_.from.merc != target.merc;
	if (crossing)
	{
		if (hand_.from.conscious && !hand_.from.canPay) { v.why = InvWhy::NoAP; return v; }
		if (target.conscious && !target.canPay)         { v.why = InvWhy::NoAPTarget; return v; }
	}

	if (rules_.fetish && rules_.fetish(target, slot, hand_.item))
	{
		v.why    = InvWhy::NailsVest;
		v.fetish = true;
		return v;
	}

	int const apFrom = crossing && hand_.from.conscious ? PASS_COST_AP : 0;
	int const apTo   = crossing && target.conscious     ? PASS_COST_AP : 0;

	if (hostSlot && occupant != 0)
	{
		// dropping on a worn or held item attaches it, else merges it (after a question)
		if (rules_.canAttach && rules_.canAttach(hand_.item, occupant))
		{
			v.kind = PlaceKind::Attach;
			return v;
		}
		if (rules_.canMerge && rules_.canMerge(hand_.item, occupant))
		{
			v.kind   = PlaceKind::AskMerge;
			v.apFrom = apFrom;
			v.apTo   = apTo;
			v.passed = crossing;
			return v;
		}
	}

	if (ctrl)
	{
		v.kind = PlaceKind::CleanUp;
		return v;
	}

	if (rules_.fits && !rules_.fits(target, slot, hand_.item))
	{
		v.why = InvWhy::DoesNotFit;
		return v;
	}

	v.kind   = PlaceKind::Put;
	v.apFrom = apFrom;
	v.apTo   = apTo;
	v.passed = crossing;
	return v;
}

ActivateKind InventoryCore::PlanActivate(uint16_t const item, int const count, int const slotLimit,
	bool const popupAllowed) const
{
	if (item == 0) return ActivateKind::Nothing;
	if (count > 1 && slotLimit > 0 && popupAllowed) return ActivateKind::OpenStack;
	return ActivateKind::Describe;
}

InvWhy InventoryCore::PlanApply(InvParty const& target) const
{
	if (hand_.Empty()) return InvWhy::HandEmpty;
	if (!target.conscious) return InvWhy::Unconscious;
	return InvWhy::None;
}

SheetAttachVerdict InventoryCore::PlanSheetAttach(SheetAttachRequest const& r) const
{
	if (r.sheetIsAttachment || r.shopOwned) return { SheetAttachKind::Ignored, InvWhy::None };
	if (r.handFull)
	{
		if (!r.payerCanPay) return { SheetAttachKind::Refused, InvWhy::NoAP };
		if (r.handInseparable && r.inseparableValid) return { SheetAttachKind::AskPermanent, InvWhy::None };
		return { SheetAttachKind::Attach, InvWhy::None };
	}
	if (!r.payerCanPay) return { SheetAttachKind::Refused, InvWhy::NoAP };
	if (!r.slotOccupied) return { SheetAttachKind::Refused, InvWhy::NothingToTake };
	return { SheetAttachKind::Detach, InvWhy::None };
}

UnloadVerdict InventoryCore::PlanUnload(bool const isGun, bool const hasRounds, bool const handFull) const
{
	if (handFull) return { false, InvWhy::HandFull };
	if (!isGun || !hasRounds) return { false, InvWhy::NothingToUnload };
	return { true, InvWhy::None };
}

}
