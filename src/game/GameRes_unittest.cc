#include "gtest/gtest.h"

#include "GameRes.h"

// The pure half of the multilingual graphic choice: version -> path (no file
// system), and path + "does it exist" -> the path to load (#411). The start-up
// splash is the caller that made this a bug; see JA2_Splash.cc and SGP.cc.

TEST(GameResTest, versionPicksTheLocalisedGraphic)
{
	EXPECT_STREQ(GetMLGFilenameForVersion(MLG_SPLASH, GameVersion::ENGLISH), "interface/splash.sti");
	EXPECT_STREQ(GetMLGFilenameForVersion(MLG_SPLASH, GameVersion::GERMAN), "german/splash_german.sti");
	EXPECT_STREQ(GetMLGFilenameForVersion(MLG_SPLASH, GameVersion::POLISH), "polish/splash_polish.sti");
	EXPECT_STREQ(GetMLGFilenameForVersion(MLG_SPLASH, GameVersion::RUSSIAN), "russian/splash_russian.sti");
	EXPECT_STREQ(GetMLGFilenameForVersion(MLG_SPLASH, GameVersion::DUTCH), "dutch/splash_dutch.sti");
}

TEST(GameResTest, absentLocalisedGraphicFallsBackToEnglish)
{
	// The English install: no german/, polish/ or russian/ directory, so the
	// localised path is not there. The English graphic is used instead of
	// aborting the process.
	EXPECT_STREQ(ResolveMLGFilename(MLG_SPLASH, GameVersion::GERMAN, false), "interface/splash.sti");
	EXPECT_STREQ(ResolveMLGFilename(MLG_SPLASH, GameVersion::POLISH, false), "interface/splash.sti");
	EXPECT_STREQ(ResolveMLGFilename(MLG_SPLASH, GameVersion::RUSSIAN, false), "interface/splash.sti");
	// Any other localised art, too: the main menu title words are the next load
	// after the splash.
	EXPECT_STREQ(ResolveMLGFilename(MLG_TITLETEXT, GameVersion::GERMAN, false), "loadscreens/titletext.sti");
}

TEST(GameResTest, presentLocalisedGraphicIsUsed)
{
	// A localised install keeps its own art.
	EXPECT_STREQ(ResolveMLGFilename(MLG_SPLASH, GameVersion::GERMAN, true), "german/splash_german.sti");
	EXPECT_STREQ(ResolveMLGFilename(MLG_SPLASH, GameVersion::DUTCH, true), "dutch/splash_dutch.sti");
}

TEST(GameResTest, englishIsUnaffected)
{
	EXPECT_STREQ(ResolveMLGFilename(MLG_SPLASH, GameVersion::ENGLISH, false), "interface/splash.sti");
	EXPECT_STREQ(ResolveMLGFilename(MLG_SPLASH, GameVersion::ENGLISH, true), "interface/splash.sti");
	// A graphic that is the same file in every version still resolves to it.
	EXPECT_STREQ(ResolveMLGFilename(MLG_AIMSYMBOL, GameVersion::GERMAN, false), "laptop/aimsymbol.sti");
}
