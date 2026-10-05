#pragma once

#include "Slots.h"

#include <stdint.h>

namespace Equipment
{

// What an attachment is and where it mounts. Compiled content: compatibility
// is decided by this pair alone, never by a per-weapon whitelist.
struct AttachmentDef
{
	uint16_t  itemId;
	SlotRole  role;
	MountKind mount;
};

enum class AttachReject : uint8_t
{
	None,
	NotAnAttachment, // the item mounts nowhere
	NoSlot,          // the platform has no slot with this role
	MountMismatch,   // the slot is the right role but the wrong interface
	Occupied,        // the slot already holds an attachment and replacement is off
};

struct AttachResult
{
	bool         ok;
	AttachReject reason;
	int          index; // host slot index the attachment would take, -1 on reject
};

// Can `attach` go into slot `index` of `hostSlots`, given that `present[i]`
// is the item id currently in slot i (0 when empty)? Pure: no game state.
AttachResult CanAttachAt(const Platform& hostSlots, int index, const AttachmentDef& attach,
		const uint16_t* present, bool replace = true, const SlotPolicy& policy = SlotPolicy());

// Can `attach` go anywhere on this host? Takes the first free matching slot
// (or the matching slot itself when replacing is allowed).
AttachResult CanAttach(const Platform& hostSlots, const AttachmentDef& attach,
		const uint16_t* present, bool replace = true, const SlotPolicy& policy = SlotPolicy());

const char* Describe(AttachReject reason);

}
