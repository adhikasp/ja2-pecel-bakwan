#pragma once

// Where things live on a soldier: worn gear, worn load-bearing equipment and
// the typed pockets that LBE provides.

enum InvSlotPos
{
	HELMETPOS = 0,
	VESTPOS,
	LEGPOS,
	HEAD1POS,
	HEAD2POS,
	HANDPOS,
	SECONDHANDPOS,

	// Load-bearing equipment: one worn item each, and the typed pockets those
	// items provide. The vest owns POCK1-4, the belt POCK5-8 and the pack
	// POCK9-12; a window's pockets only exist while its LBE item is worn.
	LBE_VESTPOS,
	LBE_BELTPOS,
	LBE_PACKPOS,
	POCK1POS,
	POCK2POS,
	POCK3POS,
	POCK4POS,
	POCK5POS,
	POCK6POS,
	POCK7POS,
	POCK8POS,
	POCK9POS,
	POCK10POS,
	POCK11POS,
	POCK12POS, // = 21, so 22 slots

	NUM_INV_SLOTS,
};

// Each worn LBE item provides up to four typed pockets in its window.
constexpr int LBE_WINDOW_SIZE = 4;

// The binary formats that came with the original game carry 19 inventory slots:
// worn gear, four big pockets, eight small pockets. That data is frozen - the
// sector maps in particular are read straight out of the .slf archives - so the
// three worn LBE slots have no place in it. They are inserted in front of the
// pockets, so a binary pocket index is the slot index from POCK1POS on, and the
// three LBE slots start empty (the basic load-bearing set fills them).
constexpr int BINARY_INV_SLOTS = 19;

constexpr int BinaryInvSlotToSlot(int binary_slot)
{
	return binary_slot < LBE_VESTPOS ? binary_slot : binary_slot + (LBE_PACKPOS - LBE_VESTPOS + 1);
}
