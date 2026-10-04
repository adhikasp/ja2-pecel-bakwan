#include "LoadoutBuilder.h"

#include "AttachmentRules.h"
#include "EquipmentCatalog.h"

namespace Equipment
{

namespace
{
	// The LBE slot order is vest, belt, pack - the same order as LbeKind.
	const LbeKind LBE_SLOT_KIND[NUM_LBE_SLOTS] = { LbeKind::Vest, LbeKind::Belt, LbeKind::Pack };
}

LoadoutReport ValidateLoadout(const LoadoutSpec& spec, const TraitsLookup& lookup, const SlotPolicy& policy)
{
	LoadoutReport report;
	report.ok = true;

	auto reject = [&report](LoadoutReject kind, int index, int pocket, const char* text)
	{
		report.ok = false;
		report.problems.push_back({ kind, index, pocket, text });
	};

	// The weapon platform and its typed attachments.
	Platform host;
	if (spec.weapon != 0)
	{
		ItemTraits weapon = lookup(spec.weapon);
		if (!weapon.known)
		{
			reject(LoadoutReject::UnknownItem, -1, -1, "unknown weapon");
		}
		else if (weapon.host.slotCount == 0)
		{
			reject(LoadoutReject::NotAWeapon, -1, -1, "this item takes no attachments");
		}
		host = weapon.host;
	}

	for (int i = 0; i < MAX_HOST_SLOTS; ++i)
	{
		if (spec.weaponAttachments[i] == 0) continue;
		if (spec.weapon == 0)
		{
			reject(LoadoutReject::NotAWeapon, i, -1, "no weapon to mount this on");
			continue;
		}
		const AttachmentDef* attach = AttachmentFor(spec.weaponAttachments[i]);
		if (attach == nullptr)
		{
			reject(LoadoutReject::BadAttachment, i, -1, "this item does not mount on anything");
			continue;
		}
		// The slot is validated against what the other slots carry; its own
		// entry is what is being placed.
		uint16_t present[MAX_HOST_SLOTS] = {};
		for (int j = 0; j < MAX_HOST_SLOTS; ++j) present[j] = (j == i) ? 0 : spec.weaponAttachments[j];
		AttachResult result = CanAttachAt(host, i, *attach, present, false, policy);
		if (!result.ok)
		{
			reject(LoadoutReject::BadAttachment, i, -1, Describe(result.reason));
		}
	}

	// The worn load-bearing gear and the pockets it provides.
	for (int slot = 0; slot < NUM_LBE_SLOTS; ++slot)
	{
		const LbeDef* lbe = nullptr;
		if (spec.lbe[slot] != 0)
		{
			lbe = LbeFor(spec.lbe[slot]);
			if (lbe == nullptr)
			{
				reject(LoadoutReject::BadLbe, slot, -1, "this is not load-bearing gear");
			}
			else if (lbe->kind != LBE_SLOT_KIND[slot])
			{
				reject(LoadoutReject::BadLbe, slot, -1, "this gear is worn elsewhere");
			}
		}

		for (int pocket = 0; pocket < MAX_LBE_POCKETS; ++pocket)
		{
			const LoadoutEntry& entry = spec.pockets[slot][pocket];
			if (entry.itemId == 0) continue;

			if (lbe == nullptr)
			{
				reject(LoadoutReject::BadPocketItem, slot, pocket, "no pockets here");
				continue;
			}
			if (pocket >= lbe->pocketCount)
			{
				reject(LoadoutReject::BadPocketItem, slot, pocket, "this gear has no such pocket");
				continue;
			}

			ItemTraits traits = lookup(entry.itemId);
			if (!traits.known)
			{
				reject(LoadoutReject::UnknownItem, slot, pocket, "unknown item");
				continue;
			}
			FitResult fit = CanFit(lbe->pockets[pocket], traits, 0);
			if (!fit.ok)
			{
				reject(LoadoutReject::BadPocketItem, slot, pocket, Describe(fit.reason));
				continue;
			}
			if (entry.count > traits.stackLimit)
			{
				reject(LoadoutReject::BadPocketItem, slot, pocket, Describe(FitReject::StackFull));
			}
		}
	}

	return report;
}

const char* Describe(LoadoutReject reason)
{
	switch (reason)
	{
		case LoadoutReject::None:          return "";
		case LoadoutReject::UnknownItem:   return "unknown item";
		case LoadoutReject::NotAWeapon:    return "this item takes no attachments";
		case LoadoutReject::BadAttachment: return "this attachment does not fit";
		case LoadoutReject::BadLbe:        return "this gear is not worn here";
		case LoadoutReject::BadPocketItem: return "this item does not fit the pocket";
	}
	return "this loadout does not work";
}

}
