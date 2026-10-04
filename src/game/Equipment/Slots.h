#pragma once

#include <stdint.h>

class GamePolicy;
struct ItemModel;

namespace Equipment
{

// What a typed slot is for. This is the player-facing name of the slot.
enum class SlotRole : uint8_t
{
	Optic,
	Muzzle,
	Underbarrel,
	SideRail,
	Plate, // armour: plate carrier pocket
	Nvg,   // armour: helmet NVG mount
};

// What physical interface a slot offers. Compatibility is decided by this
// alone - never by a per-item whitelist. A shotgun's choke thread is not a
// rifle's muzzle thread, and that is why a duckbill fits one and a suppressor
// fits the other without any item list.
enum class MountKind : uint8_t
{
	Rail,
	MuzzleThread,
	ChokeThread,
	UnderbarrelMount,
	SideRailMount,
	PlatePocket,
	NvgMount,
};

// The maximum number of typed attachment slots on one host item. The
// magazine well is not an attachment slot: the magazine is the ammunition
// system's business.
constexpr int MAX_HOST_SLOTS = 4;

// One typed slot on a host item (a weapon platform or a plate carrier).
struct SlotDef
{
	SlotRole  role;
	MountKind mount;
};

// The typed slots one host item offers, in storage order: slot i of the
// platform is attachment position i of the item instance.
struct Platform
{
	SlotDef slots[MAX_HOST_SLOTS];
	uint8_t slotCount;

	int IndexOf(SlotRole role) const; // -1 when the platform has no such slot
};

// Leaf tunables. The rules are compiled and fixed; only these knobs move,
// from GamePolicy. Defaults match the default policy.
struct SlotPolicy
{
	int  slotBonus    = 0;     // added to every gun platform's slot count
	bool strictMounts = true;  // false: any attachment fits any slot role
};

// The slots a gun offers, by weapon class (HANDGUNCLASS, SMGCLASS, ...).
// Three to four attachment slots per class - five with the magazine well.
Platform GunPlatform(uint8_t weaponClass, const SlotPolicy& policy = SlotPolicy());

// A plate carrier vest: two plate pockets, front and back.
Platform PlateCarrierPlatform(const SlotPolicy& policy = SlotPolicy());

// A helmet: one NVG mount.
Platform HelmetPlatform(const SlotPolicy& policy = SlotPolicy());

// The typed slots an item offers as an attachment host: guns by weapon class,
// armour vests as plate carriers, helmets as NVG mounts, everything else none.
Platform SlotsFor(const ItemModel& item, const SlotPolicy& policy = SlotPolicy());

// The leaf tunables as the game policy carries them.
SlotPolicy TogglesFrom(const GamePolicy* policy);

const char* Describe(SlotRole role);
const char* Describe(MountKind mount);

// Stable key for UI strings ("optic", "side_rail", ...).
const char* RoleKey(SlotRole role);

}
