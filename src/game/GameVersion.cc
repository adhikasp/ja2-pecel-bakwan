#include "GameVersion.h"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define FULL_VERSION "Pecel Bakwan " TOSTRING(GAME_VERSION)

//
// Keeps track of the game version
//

const char g_version_label[] = FULL_VERSION;

namespace
{
	std::string g_version_label_override;
	std::string g_version_label_builtin = g_version_label;
}

std::string const& VersionLabel()
{
	return g_version_label_override.empty() ? g_version_label_builtin : g_version_label_override;
}

void SetVersionLabelOverride(std::string label)
{
	g_version_label_override = std::move(label);
}

// This version is written into the save files.
// It should remain the same otherwise there will be warning on
// loading the game.
char const g_version_number[16] = "Build 04.12.02";


#ifdef WITH_UNITTESTS
#include "gtest/gtest.h"

TEST(GameVersion, asserts)
{
	EXPECT_EQ(lengthof(g_version_number), 16u);
}

TEST(GameVersion, theShownLabelIsOverridableSoGoldensDoNotCarryACommitSha)
{
	std::string const built_in = VersionLabel();
	EXPECT_EQ(built_in, g_version_label);
	SetVersionLabelOverride("Pecel Bakwan golden");
	EXPECT_EQ(VersionLabel(), "Pecel Bakwan golden");
	SetVersionLabelOverride("");
	EXPECT_EQ(VersionLabel(), built_in);
}

#endif
