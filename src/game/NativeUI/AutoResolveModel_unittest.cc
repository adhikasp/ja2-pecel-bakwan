#include "gtest/gtest.h"

#include "AutoResolveModel.h"

using namespace NativeUI::AutoResolveModel;

TEST(AutoResolveModel, percentIsClamped)
{
	EXPECT_EQ(Percent(50, 100), 50);
	EXPECT_EQ(Percent(0, 0), 0);
	EXPECT_EQ(Percent(5, 0), 0);
	EXPECT_EQ(Percent(-1, 100), 0);
	EXPECT_EQ(Percent(150, 100), 100);
}

TEST(AutoResolveModel, forcesColourUsesLegacyThresholds)
{
	// good*3 <= bad*2: the enemy clearly outnumbers us
	EXPECT_STREQ(ForcesClass(2, 4), "danger");
	// good*2 >= bad*3: we clearly outnumber the enemy
	EXPECT_STREQ(ForcesClass(6, 4), "ok");
	// in between: even
	EXPECT_STREQ(ForcesClass(4, 4), "warn");
	// exactly the legacy thresholds: the first branch catches 0 <= 0, so an empty field is red
	EXPECT_STREQ(ForcesClass(0, 0), "danger");
}

TEST(AutoResolveModel, cellClassCarriesEveryFlag)
{
	EXPECT_EQ(CellClass(false, false, false, false, false, false, false, false, false, true), "ar-cellwrap clickable");
	EXPECT_EQ(CellClass(false, false, false, false, false, false, false, false, false, false), "ar-cellwrap is-disabled");
	std::string const all = CellClass(true, true, true, true, true, true, true, true, true, false);
	EXPECT_NE(all.find("dead"), std::string::npos);
	EXPECT_NE(all.find("unconscious"), std::string::npos);
	EXPECT_NE(all.find("bleeding"), std::string::npos);
	EXPECT_NE(all.find("hit"), std::string::npos);
	EXPECT_NE(all.find("leader"), std::string::npos);
	EXPECT_NE(all.find("robot"), std::string::npos);
	EXPECT_NE(all.find("epc"), std::string::npos);
	EXPECT_NE(all.find("retreating"), std::string::npos);
	EXPECT_NE(all.find("retreated"), std::string::npos);
	EXPECT_NE(CellClass(false, false, false, false, true, false, false, false, false, true).find("leader"), std::string::npos);
}
