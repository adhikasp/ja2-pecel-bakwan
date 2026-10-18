// The document layout tables the native laptop reads (LaptopContent.h): that every record is used once, that
// a section's heading comes before its paragraphs, and that the A.I.M. alumni's face, name and bio are
// paired by one index. These are the invariants the legacy pages rely on; getting one wrong silently shows
// the wrong text, so they are checked here where no game data is needed.
#include "gtest/gtest.h"

#include "LaptopContent.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

using namespace LaptopContent;

namespace
{
/** "3.21" as the number the page prints, compared numerically (3.2 < 3.21 < 3.3). */
double Number(char const* text)
{
	if (!text || !*text) return -1;
	return std::stod(text);
}

/** The major part of "3.21" (3), or -1. */
int Major(char const* text)
{
	if (!text || !*text || !std::isdigit(static_cast<unsigned char>(*text))) return -1;
	return *text - '0';
}
}

TEST(LaptopContent, aimPolicyTableCoversEverySectionInOrder)
{
	auto const& sections = AimPolicySections();
	ASSERT_EQ(int(sections.size()), AimPolicySectionCount);

	std::set<int> records;
	for (int r : AimPolicyStatementParas())
		EXPECT_TRUE(records.insert(r).second) << "statement record " << r << " used twice";
	EXPECT_TRUE(records.insert(AimPolicyStatementRecord).second);

	for (int i = 0; i < int(sections.size()); ++i)
	{
		Section const& s = sections[i];
		EXPECT_TRUE(records.insert(s.heading).second) << "heading record " << s.heading << " used twice";
		ASSERT_FALSE(s.paras.empty());

		double previous = -1;
		for (Para const& p : s.paras)
		{
			EXPECT_TRUE(records.insert(p.record).second) << "paragraph record " << p.record << " used twice";
			EXPECT_LT(s.heading, p.record) << "a paragraph comes before its heading";

			// The numbered paragraphs step through their section (2.1, 2.11, 2.12, 2.2), the unnumbered
			// one (6.3, 9.3b) has none.
			if (!p.number || !*p.number) continue;
			EXPECT_EQ(Major(p.number), i + 1) << p.number << " is not in section " << i + 1;
			EXPECT_GT(Number(p.number), previous) << p.number << " is out of order";
			previous = Number(p.number);
		}
	}
	// The last record of the file is 45: every record above is inside aimpol.edt's 46 rows.
	EXPECT_LE(*std::max_element(records.begin(), records.end()), 45);
}

TEST(LaptopContent, aimHistoryAndImpAboutReadEveryRecordOnce)
{
	auto checkUnique =
		[](std::vector<Section> const& sections, int expected, bool ordered, int expectedParas, int lowest, int highest)
	{
		ASSERT_EQ(int(sections.size()), expected);
		std::set<int> records;
		int paras = 0;
		for (Section const& s : sections)
		{
			EXPECT_TRUE(records.insert(s.heading).second) << "heading record " << s.heading << " used twice";
			ASSERT_FALSE(s.paras.empty());
			for (Para const& p : s.paras)
			{
				++paras;
				EXPECT_TRUE(records.insert(p.record).second) << "paragraph record " << p.record << " used twice";
				if (ordered) EXPECT_LT(s.heading, p.record) << "a paragraph comes before its heading";
				EXPECT_GE(p.record, lowest) << "paragraph record " << p.record << " is below the table";
				EXPECT_LE(p.record, highest) << "paragraph record " << p.record << " is above the table";
			}
		}
		// The count pinches a record that was dropped from the table: uniqueness alone would not notice one.
		EXPECT_EQ(paras, expectedParas);
	};
	// The history reads forwards (heading then its paragraphs); the About Us headings are records 10, 17, 18,
	// 19 and 20 of imptext.edt, so they do not all sit before their text in the file.
	checkUnique(AimHistoryChapters(), AimHistoryChapterCount, true, 12, 6, 22); // AimHistoryTextLocations
	checkUnique(ImpAboutSections(), ImpAboutSectionCount, false, 6, 10, 20);    // IMP_ABOUT_US_1..12

	auto const& about = ImpAboutSections();
	// The two columns, in the order IMP_Text_System.cc lays them out: left 1 -> 2 and 10 -> 4; right 11 -> 5,
	// 8 -> 6, 9 -> 7 (IMP_ABOUT_US_1 being record 10). The first heading has two texts; the second is empty
	// in the shipped data but the legacy page draws it, so it stays in the table.
	int const headings[] = { 10, 19, 20, 17, 18 };
	for (int i = 0; i < ImpAboutSectionCount; ++i) EXPECT_EQ(about[i].heading, headings[i]);
	int const paras[] = { 11, 13, 14, 15, 16 };
	for (int i = 0; i < ImpAboutSectionCount; ++i) EXPECT_EQ(about[i].paras[0].record, paras[i]);
	EXPECT_EQ(int(about[0].paras.size()), 2);
	EXPECT_EQ(about[0].paras[1].record, 12);
}

TEST(LaptopContent, alumniFaceNameAndBioShareOneIndex)
{
	// AIMArchives.cc draws face i, the caption from row i and the bio from row i: the three line up, and
	// OLD_MERCS_COUNT is this same count, so the legacy page and the gallery cannot read different ranges.
	// 51 is what the shipped data holds: old_aim.sti has 51 frames and alumname.edt and alumni.edt have 51
	// rows each (the "102 rows" note in EDT.h is double the real count). A count above that would read past
	// all three, which is what this pins.
	EXPECT_EQ(AimAlumniCount, 51);
	for (int i = 0; i < AimAlumniCount; ++i)
	{
		EXPECT_EQ(AimAlumniFace(i), i);
		EXPECT_EQ(AimAlumniNameRow(i), i);
		EXPECT_EQ(AimAlumniBioRow(i), i);
	}
	EXPECT_EQ(AimAlumniFace(AimAlumniCount - 1), 50);
	EXPECT_EQ(AimAlumniBioRow(AimAlumniCount - 1), 50);
	EXPECT_NE(AimAlumniFaceFile(), nullptr);
}
