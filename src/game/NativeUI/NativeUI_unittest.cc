#include "gtest/gtest.h"

#include "CreditsViewModel.h"
#include "GameViewModels.h"
#include "NativeUI.h"
#include "ViewModel.h"

#include "LaptopSave.h"
#include "SaveLoadGame.h"
#include "TestUtils.h"
#include "UiCore.h"

#include <SDL3/SDL.h>

using namespace NativeUI;

TEST(NativeUI, uiModeDefaultsConfigAndOverrides)
{
	EXPECT_EQ(ConfiguredMode("credits"), UiMode::Native);
	EXPECT_EQ(ConfiguredMode("msgbox"), UiMode::Legacy);
	Configure("msgbox=native\ncredits=legacy\nbogus=sideways\n", 1.25f);
	EXPECT_EQ(ConfiguredMode("msgbox"), UiMode::Native);
	EXPECT_EQ(ConfiguredMode("credits"), UiMode::Legacy);
	EXPECT_FLOAT_EQ(UserScale(), 1.25f);
	SetModeOverride("credits", UiMode::Native);
	EXPECT_EQ(ConfiguredMode("credits"), UiMode::Native);
	SetModeOverride("credits", std::nullopt);
	EXPECT_EQ(ConfiguredMode("credits"), UiMode::Legacy);
	EXPECT_THROW(SetModeOverride("nosuchscreen", UiMode::Native), std::invalid_argument);
	// headless unit tests have no video: native cannot run, so the legacy UI is used
	std::string reason;
	Configure("", 1.f);
	EXPECT_EQ(ResolveMode("credits", &reason), UiMode::Legacy);
	EXPECT_FALSE(reason.empty());
	EXPECT_EQ(ParseUiMode("native"), UiMode::Native);
	EXPECT_EQ(ParseUiMode("Native"), std::nullopt);
}

namespace
{
	struct Row
	{
		std::string name;
		int hp = 0;
		bool on = false;
		static void Describe(RowFields<Row>& f) { f("name", &Row::name)("hp", &Row::hp)("on", &Row::on); }
	};

	struct TestVm final : ViewModel
	{
		int refreshes = 0;
		std::string title = "t";
		int count = 0;
		std::vector<Row> rows{ { "Ivan", 88, true }, { "Lynx", 61, false } };
		TestVm() : ViewModel("test", TOPIC_MONEY)
		{
			Command("add", [this](Args const& a) { count += a.empty() ? 1 : std::stoi(a[0]); Changed(); });
		}
		void Refresh() override { ++refreshes; title = "refreshed"; }
		void Describe(Fields& f) override { f.Field("title", title); f.Field("count", count); f.Rows("rows", rows); }
	};
}

TEST(NativeUI, viewModelSnapshotCommandsAndNotify)
{
	TestVm vm;
	EXPECT_EQ(ViewModel::Find("test"), &vm);
	EXPECT_EQ(vm.Snapshot().ToJson(), R"({"title":"t","count":0,"rows":[{"name":"Ivan","hp":88,"on":true},{"name":"Lynx","hp":61,"on":false}]})");

	int changes = 0;
	vm.onChanged = [&] { ++changes; };
	EXPECT_TRUE(vm.Invoke("add", { "3" }));
	EXPECT_FALSE(vm.Invoke("nope"));
	EXPECT_EQ(vm.count, 3);
	EXPECT_EQ(changes, 1);

	vm.Update(); // stale since creation
	EXPECT_EQ(vm.refreshes, 1);
	vm.Update();
	EXPECT_EQ(vm.refreshes, 1); // nothing notified
	Notify(TOPIC_CLOCK);        // not subscribed
	ViewModel::UpdateAll();
	EXPECT_EQ(vm.refreshes, 1);
	Notify(TOPIC_MONEY);
	EXPECT_TRUE(vm.Stale());
	ViewModel::UpdateAll();
	EXPECT_EQ(vm.refreshes, 2);
	EXPECT_EQ(vm.Snapshot().Get("title")->str, "refreshed");
	vm.onChanged = nullptr;
}

TEST(NativeUI, viewModelBindsToRmlUi)
{
	// the binding helper makes a data model the RML can use; commands arrive as event callbacks
	SDL_Surface* s = SDL_CreateSurface(320, 200, SDL_PIXELFORMAT_ARGB8888);
	ASSERT_NE(s, nullptr);
	{
		nui::SdlRenderInterface ri(s);
		nui::InitRml();
		Rml::Context* ctx = Rml::CreateContext("vm-test", { 320, 200 }, &ri);
		ASSERT_NE(ctx, nullptr);
		TestVm vm;
		{
			Binding b(ctx, vm);
			Rml::ElementDocument* d = ctx->LoadDocumentFromMemory(
				"<rml><body><div data-model='test'><span id='t'>{{title}}</span><span id='n'>{{count}}</span>"
				"<span id='r' data-for='r : rows'>{{r.name}}</span><span id='go' data-event-click='add(2)'>+</span></div></body></rml>");
			ASSERT_NE(d, nullptr);
			d->Show();
			ctx->Update();
			EXPECT_EQ(d->GetElementById("t")->GetInnerRML(), "t");
			d->GetElementById("go")->Click();
			ctx->Update();
			EXPECT_EQ(vm.count, 2);
			EXPECT_EQ(d->GetElementById("n")->GetInnerRML(), "2");
			d->Close();
			ctx->Update();
		}
		Rml::RemoveContext("vm-test");
	}
	SDL_DestroySurface(s);
}

TEST(NativeUI, moneyAndTimeFormatting)
{
	EXPECT_EQ(FormatMoney(0), "$0");
	EXPECT_EQ(FormatMoney(13030), "$13,030");
	EXPECT_EQ(FormatMoney(1234567), "$1,234,567");
	EXPECT_EQ(FormatMoney(-1200), "-$1,200");
	EXPECT_EQ(FormatWhen(2, 9, 5), "Day 2, 09:05");
}

TEST(NativeUI, saveSlotViewModelFromSaveFixtures)
{
	struct Case { char const* file; char const* when; char const* sector; int mercs; char const* money; char const* difficulty; };
	Case const cases[] = {
		{ "unittests/saves/strac-win/SaveGame09.sav",       "Day 2, 09:51", "D15", 6, "$13,030", "easy" },
		{ "unittests/saves/strac-linux/SaveGame01.sav",     "Day 1, 01:00", "A9",  1, "$42,000", "easy" },
		{ "unittests/saves/vanilla-russian/SaveGame06.sav", "Day 1, 01:00", "A9",  1, "$32,000", "medium" },
	};
	for (Case const& c : cases)
	{
		SAVED_GAME_HEADER header;
		bool stracLinux = false;
		AutoSGPFile f(OpenTestResourceForReading(c.file));
		ExtractSavedGameHeaderFromFile(f, header, &stracLinux);
		SaveSlotViewModel vm;
		vm.Load("SaveGame", header);
		Value const v = vm.Snapshot();
		EXPECT_EQ(v.Get("when")->str, c.when) << c.file;
		EXPECT_EQ(v.Get("sector")->str, c.sector) << c.file;
		EXPECT_EQ(v.Get("mercs")->num, c.mercs) << c.file;
		EXPECT_EQ(v.Get("money_text")->str, c.money) << c.file;
		EXPECT_EQ(v.Get("difficulty")->str, c.difficulty) << c.file;
		EXPECT_FALSE(v.Get("ironman")->flag) << c.file;
	}
}

TEST(NativeUI, gameStatusViewModelFollowsMoney)
{
	INT32 const before = LaptopSaveInfo.iCurrentBalance;
	LaptopSaveInfo.iCurrentBalance = 4500;
	GameStatusViewModel vm;
	EXPECT_EQ(vm.moneyText, "$4,500");
	LaptopSaveInfo.iCurrentBalance = 12345;
	vm.Update();
	EXPECT_EQ(vm.moneyText, "$4,500"); // nobody said the money changed
	Notify(TOPIC_MONEY);
	ViewModel::UpdateAll();
	EXPECT_EQ(vm.moneyText, "$12,345");
	EXPECT_EQ(vm.Snapshot().Get("money")->num, 12345);
	LaptopSaveInfo.iCurrentBalance = before;
}

TEST(NativeUI, creditRecordsParse)
{
	std::vector<CreditLine> l;
	EXPECT_TRUE(ParseCreditRecord("@T,C144,R134,{;Game Design", l));
	EXPECT_FALSE(ParseCreditRecord("@D25,B50;", l));
	EXPECT_TRUE(ParseCreditRecord("Ian Currie", l));
	EXPECT_TRUE(ParseCreditRecord("@};Shaun Lyng", l));
	ASSERT_EQ(l.size(), 4u);
	EXPECT_EQ(l[0].kind, "title");
	EXPECT_EQ(l[0].text, "Game Design");
	EXPECT_EQ(l[1].kind, "line");
	EXPECT_EQ(l[1].text, "Ian Currie");
	EXPECT_EQ(l[2].text, "Shaun Lyng");
	EXPECT_EQ(l[3].kind, "gap");

	CreditsViewModel vm;
	vm.length = 100;
	vm.speed = 50;
	EXPECT_FALSE(vm.Advance(1));
	EXPECT_DOUBLE_EQ(vm.offset, 50);
	vm.Invoke("pause");
	EXPECT_FALSE(vm.Advance(10));
	EXPECT_DOUBLE_EQ(vm.offset, 50);
	vm.Invoke("pause");
	EXPECT_TRUE(vm.Advance(1));
	vm.Scroll(-500);
	EXPECT_DOUBLE_EQ(vm.offset, 0);
	bool back = false;
	vm.onBack = [&] { back = true; };
	vm.Invoke("back");
	EXPECT_TRUE(back);
}

// ---- Phase 3 front end

#include "FrontEndViewModels.h"
#include "NativeImages.h"

#include <ctime>

TEST(NativeUI, frontEndScreensAreNativeByDefault)
{
	for (char const* key : { "mainmenu", "options", "saveload", "newgame", "loadscreen" })
	{
		EXPECT_TRUE(IsModeKey(key)) << key;
		EXPECT_EQ(ConfiguredMode(key), UiMode::Native) << key;
	}
	EXPECT_STREQ(ScreenKey(MAINMENU_SCREEN), "mainmenu");
	EXPECT_STREQ(ScreenKey(OPTIONS_SCREEN), "options");
	EXPECT_STREQ(ScreenKey(SAVE_LOAD_SCREEN), "saveload");
	EXPECT_STREQ(ScreenKey(GAME_INIT_OPTIONS_SCREEN), "newgame");
	EXPECT_EQ(ScreenKey(VIDEO_OPTIONS_SCREEN), nullptr); // folded into the options screen
}

TEST(NativeUI, autoResolveScreenIsNativeByDefault)
{
	EXPECT_TRUE(IsModeKey("autoresolve"));
	EXPECT_EQ(ConfiguredMode("autoresolve"), UiMode::Native);
	EXPECT_STREQ(ScreenKey(AUTORESOLVE_SCREEN), "autoresolve");
}

TEST(NativeUI, reducedMotionSetting)
{
	SetReducedMotion(true);
	EXPECT_TRUE(ReducedMotion());
	SetReducedMotion(false);
	EXPECT_FALSE(ReducedMotion());
}

TEST(NativeUI, savedAtIsRelativeToNow)
{
	std::tm t{};
	t.tm_year = 2026 - 1900; t.tm_mon = 8; t.tm_mday = 30; t.tm_hour = 14; t.tm_min = 5; t.tm_isdst = -1;
	double const now = double(std::mktime(&t));
	EXPECT_EQ(FormatSavedAt(now - 60, now), "Today 14:04");
	EXPECT_EQ(FormatSavedAt(now - 86400, now), "Yesterday 14:05");
	EXPECT_EQ(FormatSavedAt(now - 3 * 86400, now), "3 days ago");
	EXPECT_EQ(FormatSavedAt(now - 30 * 86400, now).substr(0, 2), "31"); // 31 Aug 2026
	EXPECT_EQ(FormatSavedAt(0, now), "");
}

TEST(NativeUI, upliftDoublesAndKeepsEdges)
{
	// a 2x2 checker: Scale2x keeps the colours, and the size doubles per pass
	SDL_Surface* s = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
	ASSERT_NE(s, nullptr);
	auto* p = static_cast<Uint32*>(s->pixels);
	int const pitch = s->pitch / 4;
	p[0] = 0xFF0000FFu; p[1] = 0xFFFFFFFFu; p[pitch] = 0xFFFFFFFFu; p[pitch + 1] = 0xFF0000FFu;
	SDL_Surface* up = UpliftArt(s, 2);
	ASSERT_NE(up, nullptr);
	EXPECT_EQ(up->w, 8);
	EXPECT_EQ(up->h, 8);
	auto const* q = static_cast<Uint32 const*>(up->pixels);
	EXPECT_EQ(q[0], 0xFF0000FFu); // the corners keep their colour
	EXPECT_EQ(q[(up->pitch / 4) * 7 + 7], 0xFF0000FFu);
	EXPECT_EQ(q[7], 0xFFFFFFFFu);
	SDL_DestroySurface(up);
	EXPECT_EQ(UpliftArt(nullptr, 2), nullptr);
}

TEST(NativeUI, mainMenuViewModelWithoutSaves)
{
	MainMenuViewModel vm;
	std::string picked;
	vm.onPick = [&](std::string const& p) { picked = p; };
	vm.hasSaves = false;
	vm.Invoke("continue");
	EXPECT_EQ(picked, ""); // nothing to continue
	vm.Invoke("load");
	EXPECT_EQ(picked, "");
	vm.Invoke("new");
	EXPECT_EQ(picked, "new");
	vm.Invoke("quit");
	EXPECT_EQ(picked, "quit");
}
