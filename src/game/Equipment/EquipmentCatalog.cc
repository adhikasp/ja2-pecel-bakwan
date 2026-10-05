#include "EquipmentCatalog.h"

#include "Item_Types.h"

namespace Equipment
{

namespace
{
	// The compiled attachment set: what mounts where. The curated content -
	// what each attachment costs and buys - is #100's job; the schema (one
	// role, one mount, no item lists) is this issue's.
	const AttachmentDef ATTACHMENTS[] =
	{
		// item                  role              mount
		{ SNIPERSCOPE,           SlotRole::Optic,       MountKind::Rail },
		{ LASERSCOPE,            SlotRole::SideRail,    MountKind::SideRailMount },
		{ SILENCER,              SlotRole::Muzzle,      MountKind::MuzzleThread },
		{ GUN_BARREL_EXTENDER,   SlotRole::Muzzle,      MountKind::MuzzleThread },
		{ DUCKBILL,              SlotRole::Muzzle,      MountKind::ChokeThread },
		{ BIPOD,                 SlotRole::Underbarrel, MountKind::UnderbarrelMount },
		{ UNDER_GLAUNCHER,       SlotRole::Underbarrel, MountKind::UnderbarrelMount },
		{ CERAMIC_PLATES,        SlotRole::Plate,       MountKind::PlatePocket },
		{ NIGHTGOGGLES,          SlotRole::Nvg,         MountKind::NvgMount },
		{ UVGOGGLES,             SlotRole::Nvg,         MountKind::NvgMount },
		{ SUNGOGGLES,            SlotRole::Nvg,         MountKind::NvgMount },
	};

	// The compiled load-bearing set. Each item is worn in one LBE slot and
	// provides up to MAX_LBE_POCKETS typed pockets. Nesting depth one: these
	// pockets hold items, never more LBE.
	const LbeDef LBE_ITEMS[] =
	{
		// item      kind         pockets                                                count
		{ LBE_VEST,  LbeKind::Vest, { PocketKind::Medium, PocketKind::Medium, PocketKind::Small, PocketKind::Magazine }, 4 },
		{ LBE_BELT,  LbeKind::Belt, { PocketKind::Magazine, PocketKind::Magazine, PocketKind::Small, PocketKind::Small }, 4 },
		{ LBE_PACK,  LbeKind::Pack, { PocketKind::Large, PocketKind::Large, PocketKind::Medium, PocketKind::Medium }, 4 },
	};
}

const AttachmentDef* AttachmentFor(uint16_t itemId)
{
	for (const AttachmentDef& def : ATTACHMENTS)
	{
		if (def.itemId == itemId) return &def;
	}
	return nullptr;
}

const AttachmentDef* AllAttachments(size_t& count)
{
	count = sizeof(ATTACHMENTS) / sizeof(ATTACHMENTS[0]);
	return ATTACHMENTS;
}

const LbeDef* LbeFor(uint16_t itemId)
{
	for (const LbeDef& def : LBE_ITEMS)
	{
		if (def.itemId == itemId) return &def;
	}
	return nullptr;
}

const LbeDef* AllLbe(size_t& count)
{
	count = sizeof(LBE_ITEMS) / sizeof(LBE_ITEMS[0]);
	return LBE_ITEMS;
}

}
