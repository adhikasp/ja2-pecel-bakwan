#pragma once

#include "AttachmentRules.h"
#include "Lbe.h"

#include <stddef.h>
#include <stdint.h>

namespace Equipment
{

// The compiled equipment catalog. The rules data - which item mounts where,
// which pockets an LBE item provides - lives here in C++, with schema-
// invariant tests. Stats and trade-offs are content work (#100, #101).

// The attachment definition of an item, or null when it mounts nowhere.
const AttachmentDef* AttachmentFor(uint16_t itemId);
const AttachmentDef* AllAttachments(size_t& count);

// The LBE definition of an item, or null when it is not load-bearing gear.
const LbeDef* LbeFor(uint16_t itemId);
const LbeDef* AllLbe(size_t& count);

}
