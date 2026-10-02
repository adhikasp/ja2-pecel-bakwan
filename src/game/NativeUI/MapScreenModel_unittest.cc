#include "gtest/gtest.h"

#include "MapScreenModel.h"

using namespace NativeUI::MapModel;

namespace
{
	// game-state fixtures: the team of the parity tour's campaign plus the cases the tour cannot reach cheaply
	std::vector<MercFact> Team()
	{
		return {
			{ 0, "Barry",   1, false, false },  // squad 2
			{ 1, "Ivan",    0, false, false },  // squad 1
			{ 2, "Vicki",  21, false, false },  // doctor
			{ 3, "Scope",  24, false, false },  // in transit
			{ 4, "Lynx",    0, false, false },  // squad 1
			{ 5, "Reaper", 30, false, true  },  // dead
			{ 6, "Hummer", 23, true,  false },  // a vehicle
		};
	}
}

TEST(MapScreenModel, teamGroupsKeepLegacyOrderAndLines)
{
	auto const g = GroupTeam(Team(), true);
	ASSERT_EQ(g.size(), 7u);
	std::vector<std::string> names;
	for (auto const& e : g) names.push_back(e.fact.name);
	EXPECT_EQ(names, (std::vector<std::string>{ "Ivan", "Lynx", "Barry", "Vicki", "Scope", "Reaper", "Hummer" }));
	EXPECT_TRUE(g[0].firstInGroup);   // squad 1
	EXPECT_FALSE(g[1].firstInGroup);
	EXPECT_TRUE(g[2].firstInGroup);   // squad 2
	EXPECT_EQ(g[3].group, Group::Other);
	EXPECT_EQ(g[4].group, Group::Transit);
	EXPECT_EQ(g[5].group, Group::Dead);
	EXPECT_EQ(g[6].group, Group::Vehicles);
	EXPECT_EQ(g[1].fact.line, 4);     // clicks still go to the legacy line
}

TEST(MapScreenModel, teamUngroupedIsTheLegacyList)
{
	auto const g = GroupTeam(Team(), false);
	for (size_t i = 0; i < g.size(); ++i) EXPECT_EQ(g[i].fact.line, int(i));
}

TEST(MapScreenModel, legacyBoxLines)
{
	auto const c = ParseContractLine("Offer One Week ( $8,800 )");
	EXPECT_EQ(c.label, "Offer One Week");
	EXPECT_EQ(c.price, 8800);
	EXPECT_EQ(ParseContractLine("Dismiss").price, -1);
	auto const m = ParseMoveLine("   *Ivan*");
	EXPECT_TRUE(m.merc);
	EXPECT_TRUE(m.checked);
	EXPECT_EQ(m.label, "Ivan");
	auto const s = ParseMoveLine("Squad  1");
	EXPECT_FALSE(s.merc);
	EXPECT_FALSE(s.checked);
}

TEST(MapScreenModel, messageLog)
{
	EXPECT_EQ(Classify(true, false, "Lynx was wounded"), MessageKind::Combat);
	EXPECT_EQ(Classify(false, false, "Daily income: $3,450"), MessageKind::Money);
	EXPECT_EQ(Classify(false, true, "Ivan: \"Enemy\""), MessageKind::Team);
	EXPECT_EQ(SectorNamed("Barry was wounded in D13."), "D13");
	EXPECT_EQ(SectorNamed("Day 12 in A9"), "A9");
	EXPECT_EQ(SectorNamed("AIM hired"), "");
	EXPECT_EQ(DayOf(24 * 60 + 7 * 60), 1); // the campaign starts on day 1, 07:00
	EXPECT_EQ(DayOf(2 * 24 * 60 + 5), 2);
	EXPECT_EQ(ClockOf(7 * 60 + 5), "07:05");
	EXPECT_TRUE(Matches("Travel route confirmed", "ROUTE"));
	EXPECT_FALSE(Matches("Travel route confirmed", "militia"));
}

TEST(MapScreenModel, itemArtWholeNumbersOnly)
{
	EXPECT_EQ(ItemPixelScale(1.f), 2);       // 1080p
	EXPECT_EQ(ItemPixelScale(4.f / 3.f), 3); // 1440p
	EXPECT_EQ(ItemPixelScale(2.f), 4);       // 4K
	EXPECT_EQ(ItemPixelScale(2.f / 3.f), 1); // 720p
	// a clip (10x22) stays small and centred in a 64x52 dp slot at 1080p
	auto const clip = FitItem(10, 22, 64, 52, 1.f);
	EXPECT_EQ(clip.scale, 2);
	EXPECT_EQ(clip.w, 20);
	EXPECT_EQ(clip.left, 22);
	// a rifle (60x22) that would not fit at 3x in a 64 dp slot at 1440p steps down to 1x, never a fraction
	auto const rifle = FitItem(60, 22, 64, 52, 4.f / 3.f);
	EXPECT_EQ(rifle.scale, 1);
	EXPECT_EQ(rifle.w, 60);
}
