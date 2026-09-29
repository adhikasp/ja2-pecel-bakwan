#include "gtest/gtest.h"

#include "Automation.h"
#include "AutomationLua.h"
#include "AutomationSession.h"

#include <string>
#include <vector>

namespace
{
	// Parse a command line; returns the arguments left for the Rust parser.
	std::vector<std::string> Parse(std::vector<std::string> args, Automation::Options& o, std::string& error)
	{
		args.insert(args.begin(), "ja2");
		std::vector<char*> argv;
		for (auto& a : args) argv.push_back(a.data());
		argv.push_back(nullptr);
		int argc = static_cast<int>(args.size());
		EXPECT_TRUE(Automation::ParseCommandLine(argc, argv.data(), o, error) || !error.empty());
		return std::vector<std::string>(argv.begin() + 1, argv.begin() + argc);
	}
}


TEST(Automation, commandLineFlagsAreTakenOutOfArgv)
{
	Automation::Options o;
	std::string error;
	auto const rest = Parse({ "-res", "1280x720", "-run", "t.lua", "--seed", "7", "-arg", "a", "-arg", "b", "-show" }, o, error);
	EXPECT_EQ(error, "");
	EXPECT_EQ(rest, (std::vector<std::string>{ "-res", "1280x720" }));
	EXPECT_EQ(o.runScript, "t.lua");
	EXPECT_EQ(o.seed, 7u);
	EXPECT_EQ(o.scriptArgs, (std::vector<std::string>{ "a", "b" }));
	EXPECT_TRUE(o.Active());
	EXPECT_FALSE(o.Headless());
}

TEST(Automation, uitestIsAnAliasForRun)
{
	Automation::Options o;
	std::string error;
	Parse({ "-uitest", "old.txt" }, o, error);
	EXPECT_EQ(o.runScript, "old.txt");
	EXPECT_TRUE(o.Headless());
}

TEST(Automation, commandLineErrors)
{
	{
		Automation::Options o;
		std::string error;
		Parse({ "-run", "a.lua", "-serve", "0" }, o, error);
		EXPECT_NE(error, "");
	}
	{
		Automation::Options o;
		std::string error;
		Parse({ "-load", "Save1" }, o, error); // nothing to drive the game
		EXPECT_NE(error, "");
	}
	{
		Automation::Options o;
		std::string error;
		Parse({ "-run", "a.lua", "-seed", "x" }, o, error);
		EXPECT_NE(error, "");
	}
	{
		Automation::Options o;
		std::string error;
		Parse({ "-run" }, o, error);
		EXPECT_NE(error, "");
	}
}

TEST(Automation, keyCombos)
{
	SDL_Keycode key;
	SDL_Keymod mods;
	ASSERT_TRUE(Automation::ParseKeyCombo("alt+c", key, mods));
	EXPECT_EQ(key, SDLK_C);
	EXPECT_EQ(mods, SDL_KMOD_LALT);

	ASSERT_TRUE(Automation::ParseKeyCombo("Ctrl+Shift+S", key, mods));
	EXPECT_EQ(key, SDLK_S);
	EXPECT_EQ(mods, SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT);

	ASSERT_TRUE(Automation::ParseKeyCombo("ESC", key, mods));
	EXPECT_EQ(key, SDLK_ESCAPE);
	EXPECT_EQ(mods, SDL_KMOD_NONE);

	ASSERT_TRUE(Automation::ParseKeyCombo("+", key, mods)); // the plus key itself
	EXPECT_EQ(key, SDLK_PLUS);

	EXPECT_FALSE(Automation::ParseKeyCombo("hyper+x", key, mods));
	EXPECT_FALSE(Automation::ParseKeyCombo("nosuchkey", key, mods));
}

TEST(Automation, legacyScriptsTranslateToLua)
{
	std::string error;
	std::string const lua = Automation::TranslateLegacyScript(
		"# comment\n"
		"click 10 20\n"
		"key ESCAPE\n"
		"type Hello world\n"
		"waitpixel 1 2 #a0b0c0 16 5000   # trailing comment\n"
		"screenshot out.png\n", error);
	EXPECT_EQ(error, "");
	EXPECT_NE(lua.find("ja2.click(10, 20)"), std::string::npos);
	EXPECT_NE(lua.find("ja2.key(\"ESCAPE\")"), std::string::npos);
	EXPECT_NE(lua.find("ja2.type(\"Hello world\")"), std::string::npos);
	EXPECT_NE(lua.find("ja2.waitPixel(1, 2, \"#a0b0c0\", 16, 5000)"), std::string::npos);
	EXPECT_NE(lua.find("ja2.screenshot(\"out.png\")"), std::string::npos);

	Automation::TranslateLegacyScript("frobnicate 1 2\n", error);
	EXPECT_NE(error.find("unknown command"), std::string::npos);
}
