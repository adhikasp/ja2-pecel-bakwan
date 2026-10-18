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
	auto checkUnique = [](std::vector<Section> const& sections, int expected, bool ordered)
	{
		ASSERT_EQ(int(sections.size()), expected);
		std::set<int> records;
		for (Section const& s : sections)
		{
			EXPECT_TRUE(records.insert(s.heading).second) << "heading record " << s.heading << " used twice";
			ASSERT_FALSE(s.paras.empty());
			for (Para const& p : s.paras)
			{
				EXPECT_TRUE(records.insert(p.record).second) << "paragraph record " << p.record << " used twice";
				if (ordered) EXPECT_LT(s.heading, p.record) << "a paragraph comes before its heading";
			}
		}
	};
	// The history reads forwards (heading then its paragraphs); the About Us headings are records 8, 9, 10
	// and 11 of imptext.edt, so they do not sit before their text in the file.
	checkUnique(AimHistoryChapters(), AimHistoryChapterCount, true);
	checkUnique(ImpAboutSections(), ImpAboutSectionCount, false);

	auto const& about = ImpAboutSections();
	// The two columns, in the order IMP_Text_System.cc lays them out: left 1 -> 2, 10 -> 4; right 11 -> 5,
	// 8 -> 6, 9 -> 7 (IMP_ABOUT_US_1 being record 10).
	int const headings[] = { 10, 19, 20, 17, 18 };
	for (int i = 0; i < ImpAboutSectionCount; ++i) EXPECT_EQ(about[i].heading, headings[i]);
	int const paras[] = { 11, 13, 14, 15, 16 };
	for (int i = 0; i < ImpAboutSectionCount; ++i) EXPECT_EQ(about[i].paras[0].record, paras[i]);
}

TEST(LaptopContent, alumniFaceNameAndBioShareOneIndex)
{
	// AIMArchives.cc draws face i, the caption from row i and the bio from row i: the three line up.
	EXPECT_EQ(AimAlumniCount, 51);
	for (int i = 0; i < AimAlumniCount; ++i)
	{
		EXPECT_EQ(AimAlumniFace(i), i);
		EXPECT_EQ(AimAlumniNameRow(i), i);
		EXPECT_EQ(AimAlumniBioRow(i), i);
	}
	// The last alumni is frame 50 of old_aim.sti; the .edt files carry 102 rows, so all 51 fit.
	EXPECT_EQ(AimAlumniFace(AimAlumniCount - 1), 50);
	EXPECT_LT(AimAlumniBioRow(AimAlumniCount - 1), 102);
	EXPECT_NE(AimAlumniFaceFile(), nullptr);
}
