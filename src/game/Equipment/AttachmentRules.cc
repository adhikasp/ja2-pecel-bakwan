#include "AttachmentRules.h"

namespace Equipment
{

AttachResult CanAttachAt(const Platform& hostSlots, int index, const AttachmentDef& attach,
		const uint16_t* present, bool replace, const SlotPolicy& policy)
{
	if (index < 0 || index >= hostSlots.slotCount)
	{
		return { false, AttachReject::NoSlot, -1 };
	}

	const SlotDef& slot = hostSlots.slots[index];
	if (slot.role != attach.role)
	{
		return { false, AttachReject::NoSlot, -1 };
	}
	if (policy.strictMounts && slot.mount != attach.mount)
	{
		return { false, AttachReject::MountMismatch, -1 };
	}

	// One of each attachment anywhere on the host is unnecessary with typed
	// slots - a second silencer or laser has no slot to go to - while two
	// plate pockets legitimately take two plates.
	if (present[index] != 0 && !replace)
	{
		return { false, AttachReject::Occupied, -1 };
	}
	return { true, AttachReject::None, index };
}

AttachResult CanAttach(const Platform& hostSlots, const AttachmentDef& attach,
		const uint16_t* present, bool replace, const SlotPolicy& policy)
{
	bool sawRole = false;
	bool sawMount = false;
	int freeIndex = -1;
	for (int i = 0; i < hostSlots.slotCount; ++i)
	{
		const SlotDef& slot = hostSlots.slots[i];
		if (slot.role != attach.role) continue;
		sawRole = true;
		if (policy.strictMounts && slot.mount != attach.mount) continue;
		sawMount = true;
		if (present[i] == 0)
		{
			freeIndex = i;
			break;
		}
		if (replace && freeIndex < 0)
		{
			freeIndex = i;
		}
	}

	if (freeIndex < 0)
	{
		// Distinguish "no slot for this", "right slot, wrong interface" and
		// "the right slot is taken".
		AttachReject reason = !sawRole  ? AttachReject::NoSlot
			: !sawMount ? AttachReject::MountMismatch
			: AttachReject::Occupied;
		return { false, reason, -1 };
	}
	return CanAttachAt(hostSlots, freeIndex, attach, present, replace, policy);
}

const char* Describe(AttachReject reason)
{
	switch (reason)
	{
		case AttachReject::None:           return "";
		case AttachReject::NotAnAttachment: return "this item does not mount on anything";
		case AttachReject::NoSlot:         return "this weapon has no slot for it";
		case AttachReject::MountMismatch:  return "it does not fit this kind of mount";
		case AttachReject::Occupied:       return "this slot is already taken";
	}
	return "it does not fit";
}

}
