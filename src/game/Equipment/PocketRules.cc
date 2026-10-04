#include "PocketRules.h"

#include "EquipmentCatalog.h"
#include "ItemModel.h"

namespace Equipment
{

FitResult CanFit(PocketKind pocket, const ItemTraits& item, uint16_t inPocket)
{
	// Nesting depth one is structural: a pouch holds items, never a pouch.
	if (item.isLbe) return { false, FitReject::NoNesting };

	switch (pocket)
	{
		case PocketKind::Small:
			if (item.size != ItemSize::Small) return { false, FitReject::TooBig };
			break;

		case PocketKind::Medium:
			if (item.size == ItemSize::Large) return { false, FitReject::TooBig };
			break;

		case PocketKind::Large:
			break; // takes anything that is not load-bearing gear

		case PocketKind::Magazine:
			if (!item.isMagazine) return { false, FitReject::WrongKind };
			break;
	}

	if (inPocket >= item.stackLimit) return { false, FitReject::StackFull };
	return { true, FitReject::None };
}

FitResult CanFit(PocketKind pocket, const ItemModel& item, uint16_t inPocket)
{
	ItemTraits traits = TraitsOf(item);
	// A small pocket holds half of what a full pocket holds, at least one.
	if (pocket == PocketKind::Small && traits.stackLimit > 1)
	{
		traits.stackLimit /= 2;
	}
	return CanFit(pocket, traits, inPocket);
}

ItemSize SizeOf(const ItemModel& item)
{
	if (item.isGun() || item.isLauncher() || item.isArmour()) return ItemSize::Large;
	if (item.isAmmo()) return ItemSize::Small;
	if (item.isGrenade() || item.isBomb() || item.isExplosive()) return ItemSize::Medium;
	if (item.isBlade() || item.isThrowingKnife() || item.isThrown()) return ItemSize::Medium;

	// Everything else is judged by its weight.
	uint8_t weight = item.getWeight(); // hectograms
	if (weight <= 5)  return ItemSize::Small;
	if (weight <= 20) return ItemSize::Medium;
	return ItemSize::Large;
}

ItemTraits TraitsOf(const ItemModel& item)
{
	ItemTraits traits;
	traits.size       = SizeOf(item);
	traits.isMagazine = item.isAmmo();
	traits.isLbe      = LbeFor(item.getId()) != nullptr;
	traits.stackLimit = item.getPerPocket() > 0 ? item.getPerPocket() : 1;
	traits.host       = SlotsFor(item);
	return traits;
}

const char* Describe(FitReject reason)
{
	switch (reason)
	{
		case FitReject::None:       return "";
		case FitReject::NoNesting:  return "pouches hold items, not more pouches";
		case FitReject::WrongKind:  return "this pocket holds magazines only";
		case FitReject::TooBig:     return "this item is too big for the pocket";
		case FitReject::StackFull:  return "this pocket is full";
	}
	return "it does not fit";
}

}
