#include "gtest/gtest.h"

#include "MapScreenMessageRecord.h"

#include <vector>

// The save blob writes one fixed-size record per map-screen message and reads them back in order
// (Message.cc). A wrong size or field order does not fail there, it silently shifts every message
// after the first, so these tests pin both against a sequence.

namespace
{

std::vector<uint8_t> Encode(std::vector<MapScreenMessageRecord> const& records)
{
	std::vector<uint8_t> blob(records.size() * MAP_SCREEN_MESSAGE_RECORD_SIZE, 0xff);
	for (size_t i = 0; i < records.size(); ++i)
	{
		WriteMapScreenMessageRecord(blob.data() + i * MAP_SCREEN_MESSAGE_RECORD_SIZE, records[i]);
	}
	return blob;
}

} // namespace

TEST(MapScreenMessageRecord, sizeIsTheLegacyBlobPlusTheTwoNewFields)
{
	// The legacy record was 28 bytes; the game minute and the kind add 4 + 1 at the end.
	EXPECT_EQ(MAP_SCREEN_MESSAGE_RECORD_SIZE, 33u);
}

TEST(MapScreenMessageRecord, roundTripsEveryField)
{
	// MSG_DIALOG: a kind that is not 0, so a dropped or mis-ordered field cannot pass by luck.
	MapScreenMessageRecord const in{ 123456u, 0x1234u, TRUE, 12 * 24 * 60 + 10 * 60 + 30, 1 };
	std::vector<uint8_t> const blob = Encode({ in });

	MapScreenMessageRecord const out = ReadMapScreenMessageRecord(blob.data());
	EXPECT_EQ(out.timeOfLastUpdate, in.timeOfLastUpdate);
	EXPECT_EQ(out.color, in.color);
	EXPECT_EQ(out.beginningOfNewString, in.beginningOfNewString);
	EXPECT_EQ(out.gameMinute, in.gameMinute);
	EXPECT_EQ(out.priority, in.priority);
}

TEST(MapScreenMessageRecord, consecutiveRecordsStayAligned)
{
	// Three messages of one day, with a wrapped continuation line: if a record's size were off, the
	// second and third would decode shifted instead of the whole set being read back.
	std::vector<MapScreenMessageRecord> const in{
		{ 100, 0x00ff, TRUE, 12 * 24 * 60 + 9 * 60, 0 /* MSG_INTERFACE */ },
		{ 200, 0xffff, TRUE, 12 * 24 * 60 + 10 * 60 + 30, 1 /* MSG_DIALOG */ },
		{ 300, 0xffff, FALSE, 12 * 24 * 60 + 10 * 60 + 30, 1 /* MSG_DIALOG */ },
	};
	std::vector<uint8_t> const blob = Encode(in);
	ASSERT_EQ(blob.size(), in.size() * MAP_SCREEN_MESSAGE_RECORD_SIZE);

	for (size_t i = 0; i < in.size(); ++i)
	{
		MapScreenMessageRecord const out = ReadMapScreenMessageRecord(blob.data() + i * MAP_SCREEN_MESSAGE_RECORD_SIZE);
		EXPECT_EQ(out.timeOfLastUpdate, in[i].timeOfLastUpdate) << "record " << i;
		EXPECT_EQ(out.color, in[i].color) << "record " << i;
		EXPECT_EQ(out.beginningOfNewString, in[i].beginningOfNewString) << "record " << i;
		EXPECT_EQ(out.gameMinute, in[i].gameMinute) << "record " << i;
		EXPECT_EQ(out.priority, in[i].priority) << "record " << i;
	}
}

TEST(MapScreenMessageRecord, reservedBytesAreWrittenZero)
{
	// The legacy gaps stay as zero padding, so nothing leaks into the file from the stack.
	MapScreenMessageRecord const in{ 1, 2, TRUE, 3, 4 };
	std::vector<uint8_t> const blob = Encode({ in });

	for (size_t i = 0; i < 4; ++i) EXPECT_EQ(blob[i], 0) << "leading reserved byte " << i;
	for (size_t i = 8; i < 24; ++i) EXPECT_EQ(blob[i], 0) << "middle reserved byte " << i;
	EXPECT_EQ(blob[27], 0) << "trailing reserved byte";
}
