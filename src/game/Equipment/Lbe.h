#pragma once

#include <stdint.h>

namespace Equipment
{

// The three places load-bearing equipment is worn. Each holds one LBE item,
// and each LBE item provides its own typed pockets.
enum class LbeKind : uint8_t
{
	Vest,
	Belt,
	Pack,
};

// What a pocket accepts. "Small" takes small things, "Large" takes anything
// up to large, "Magazine" takes nothing but magazines.
enum class PocketKind : uint8_t
{
	Small,
	Medium,
	Large,
	Magazine,
};

// How much room an item takes in a pocket.
enum class ItemSize : uint8_t
{
	Small,
	Medium,
	Large,
};

constexpr int MAX_LBE_POCKETS = 4; // pockets one LBE item provides
constexpr int NUM_LBE_SLOTS   = 3; // vest, belt, pack

// The compiled definition of one LBE item type.
struct LbeDef
{
	uint16_t   itemId;
	LbeKind   kind;
	PocketKind pockets[MAX_LBE_POCKETS];
	uint8_t   pocketCount;
};

const char* Describe(LbeKind kind);
const char* Describe(PocketKind kind);
const char* Describe(ItemSize size);

}
