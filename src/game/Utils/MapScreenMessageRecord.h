#ifndef __MAP_SCREEN_MESSAGE_RECORD_H
#define __MAP_SCREEN_MESSAGE_RECORD_H

#include "Debug.h"
#include "LoadSaveData.h"
#include "Types.h"

#include <cstddef>

// One map-screen message in a saved game: the fixed-size record the save blob carries after the
// message's UTF-16 text (SaveMapScreenMessagesToSaveGameFile / LoadMapScreenMessagesFromSaveGameFile
// in Message.cc, one record per message in the 256-slot list).
//
//   timeOfLastUpdate, color, beginningOfNewString   the scroll system's own bookkeeping
//   gameMinute   the game minute it was added (0: unknown), which the native log renders as its day
//                heading and clock time
//   priority     its kind, one of the MSG_* constants in Message.h (MSG_INTERFACE, MSG_DIALOG, ...);
//                this is what puts a merc's dialogue under the log's Team tab
//
// The size and the field order live here once, because the writer and the reader share them. They used
// to spell the same layout out twice, and a disagreement would not fail: it would shift every message
// after the first and corrupt the whole log.
struct MapScreenMessageRecord
{
	UINT32 timeOfLastUpdate;
	UINT16 color;
	BOOLEAN beginningOfNewString;
	UINT32 gameMinute;
	UINT8 priority;
};

// 4, 16 and 1 are the reserved bytes the legacy layout kept between its fields; they stay, written as
// zero, so the record only ever grows at the end. The blob was 28 bytes before the log's game minute
// and kind were saved (issue #43).
inline constexpr std::size_t MAP_SCREEN_MESSAGE_RECORD_SIZE = 4 + 4 + 16 + 2 + 1 + 1 + 4 + 1;
static_assert(MAP_SCREEN_MESSAGE_RECORD_SIZE == 33, "the record layout changed: fix both sides and this size");

inline MapScreenMessageRecord ReadMapScreenMessageRecord(uint8_t const* const data)
{
	MapScreenMessageRecord r{};
	DataReader d{ data };
	EXTR_SKIP(d, 4)
	EXTR_U32(d, r.timeOfLastUpdate)
	EXTR_SKIP(d, 16)
	EXTR_U16(d, r.color)
	EXTR_BOOL(d, r.beginningOfNewString)
	EXTR_SKIP(d, 1)
	EXTR_U32(d, r.gameMinute)
	EXTR_U8(d, r.priority)
	Assert(d.getConsumed() == MAP_SCREEN_MESSAGE_RECORD_SIZE);
	return r;
}

inline void WriteMapScreenMessageRecord(uint8_t* const data, MapScreenMessageRecord const& r)
{
	DataWriter d{ data };
	INJ_SKIP(d, 4)
	INJ_U32(d, r.timeOfLastUpdate)
	INJ_SKIP(d, 16)
	INJ_U16(d, r.color)
	INJ_BOOL(d, r.beginningOfNewString)
	INJ_SKIP(d, 1)
	INJ_U32(d, r.gameMinute)
	INJ_U8(d, r.priority)
	Assert(d.getConsumed() == MAP_SCREEN_MESSAGE_RECORD_SIZE);
}

#endif
