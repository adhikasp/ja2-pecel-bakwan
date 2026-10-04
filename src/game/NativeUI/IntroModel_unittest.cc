#include "IntroModel.h"

#include <gtest/gtest.h>

namespace NativeUI
{

using IntroModel::Chain;
using IntroModel::ChromeVisible;
using IntroModel::Kind;
using IntroModel::Scene;

namespace
{
	std::vector<std::string> Ids(std::vector<Scene> const& chain)
	{
		std::vector<std::string> ids;
		for (Scene const& s : chain) ids.push_back(s.id);
		return ids;
	}
}

TEST(IntroModel, beginningChain)
{
	auto const chain = Chain(Kind::Beginning, false, false);
	EXPECT_EQ(Ids(chain), (std::vector<std::string>{ "rebel-cr", "omerta", "prague-cr", "prague" }));
	EXPECT_EQ(chain[1].file, "omerta.smk");
	EXPECT_EQ(IntroModel::ExitTarget(Kind::Beginning), INIT_SCREEN);
}

TEST(IntroModel, splashChain)
{
	auto const chain = Chain(Kind::Splash, false, false);
	EXPECT_EQ(Ids(chain), (std::vector<std::string>{ "splashscreen" }));
	EXPECT_EQ(chain[0].file, "splashscreen.smk");
	EXPECT_EQ(IntroModel::ExitTarget(Kind::Splash), INIT_SCREEN);
}

TEST(IntroModel, endingChainFollowsTheLiving)
{
	EXPECT_EQ(Ids(Chain(Kind::Ending, false, false)), (std::vector<std::string>{ "throne-mig", "heli-flyby", "heli-sky" }));
	EXPECT_EQ(Ids(Chain(Kind::Ending, true, false)), (std::vector<std::string>{ "throne-nomig", "heli-flyby", "heli-sky" }));
	EXPECT_EQ(Ids(Chain(Kind::Ending, false, true)), (std::vector<std::string>{ "throne-mig", "heli-flyby", "heli-nosky" }));
	EXPECT_EQ(Ids(Chain(Kind::Ending, true, true)), (std::vector<std::string>{ "throne-nomig", "heli-flyby", "heli-nosky" }));

	auto const chain = Chain(Kind::Ending, true, true);
	EXPECT_EQ(chain[0].file, "throne_nomig.smk");
	EXPECT_EQ(chain[2].file, "heli_nosky.smk");
}

TEST(IntroModel, endingHandsOverToTheEpilogue)
{
	EXPECT_EQ(IntroModel::ExitTarget(Kind::Ending), EPILOGUE_SCREEN);
}

TEST(IntroModel, chromeFadesOutWhenUntouched)
{
	EXPECT_TRUE(ChromeVisible(0, 0));        // just shown
	EXPECT_TRUE(ChromeVisible(1999, 0));     // still within the 2 s
	EXPECT_FALSE(ChromeVisible(2000, 0));    // faded out
	EXPECT_TRUE(ChromeVisible(2500, 1000));  // a key brings it back

	// the game clock wraps: the difference is what counts
	EXPECT_TRUE(ChromeVisible(500, 0xFFFFFF00u));
	EXPECT_FALSE(ChromeVisible(4000, 0xFFFFFF00u));
}

}
