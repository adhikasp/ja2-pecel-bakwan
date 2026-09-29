#include "gtest/gtest.h"

#include "TextRegistry.h"
#include "VSurface.h"

#include <memory>

namespace
{
	constexpr UINT16 INK = 0xFFFF; // white
	constexpr UINT16 BG  = 0x0000; // black

	void Fill(SGPVSurface& s, SDL_Rect const& r, UINT16 const colour)
	{
		SDL_Surface const& surface = s.GetSDLSurface();
		for (int y = r.y; y < r.y + r.h; ++y)
		{
			auto* row = reinterpret_cast<UINT16*>(static_cast<UINT8*>(surface.pixels) + y * surface.pitch);
			for (int x = r.x; x < r.x + r.w; ++x) row[x] = colour;
		}
	}

	// "Print" a string: a rectangle whose left half is ink.
	void Print(SGPVSurface& s, SDL_Rect const& r, char const* text, UINT16 const ink = INK)
	{
		Fill(s, r, BG);
		Fill(s, SDL_Rect{ r.x, r.y, r.w / 2, r.h }, ink);
		TextRegistry::OnPrint(&s, r, text, ink);
	}

	struct TextRegistryTest : ::testing::Test
	{
		std::unique_ptr<SGPVSurface> screen{ new SGPVSurface(64, 32, 16) };
		SDL_Rect const nowhere{ 0, 0, 0, 0 };

		void SetUp() override
		{
			TextRegistry::SetEnabled(true);
			Fill(*screen, SDL_Rect{ 0, 0, 64, 32 }, BG);
		}
		void TearDown() override { TextRegistry::SetEnabled(false); }

		std::vector<TextRegistry::VisibleText> Visible()
		{
			return TextRegistry::Visible(&screen->GetSDLSurface(), nowhere);
		}
	};
}


TEST_F(TextRegistryTest, printedTextIsVisible)
{
	Print(*screen, SDL_Rect{ 2, 2, 20, 8 }, "Hello");
	auto const v = Visible();
	ASSERT_EQ(v.size(), 1u);
	EXPECT_EQ(v[0].text, "Hello");
	EXPECT_EQ(v[0].rect.x, 2);
	EXPECT_EQ(v[0].rect.w, 20);
}

TEST_F(TextRegistryTest, overdrawnTextIsGone)
{
	Print(*screen, SDL_Rect{ 2, 2, 20, 8 }, "Hello");
	Fill(*screen, SDL_Rect{ 0, 0, 64, 32 }, 0x1234); // e.g. a dialog drawn on top
	EXPECT_TRUE(Visible().empty());
}

TEST_F(TextRegistryTest, reprintingReplacesTheOldString)
{
	Print(*screen, SDL_Rect{ 2, 2, 20, 8 }, "Day 1, 07:00");
	Print(*screen, SDL_Rect{ 2, 2, 20, 8 }, "Day 1, 07:01");
	auto const v = Visible();
	ASSERT_EQ(v.size(), 1u);
	EXPECT_EQ(v[0].text, "Day 1, 07:01");
}

TEST_F(TextRegistryTest, textThatLeftNoInkIsNotVisible)
{
	// Printed while clipped away: nothing landed, the snapshot is background.
	TextRegistry::OnPrint(screen.get(), SDL_Rect{ 2, 2, 20, 8 }, "Ghost", INK);
	EXPECT_TRUE(Visible().empty());
}

TEST_F(TextRegistryTest, charactersPrintedOneByOneAreJoined)
{
	Print(*screen, SDL_Rect{ 2, 12, 6, 8 }, "B");
	Print(*screen, SDL_Rect{ 8, 12, 6, 8 }, "o");
	Print(*screen, SDL_Rect{ 14, 12, 6, 8 }, "b");
	auto const v = Visible();
	ASSERT_EQ(v.size(), 1u);
	EXPECT_EQ(v[0].text, "Bob");
	EXPECT_EQ(v[0].rect.w, 18);
}

TEST_F(TextRegistryTest, leaderDotsAreDropped)
{
	Print(*screen, SDL_Rect{ 2, 22, 4, 8 }, ".");
	Print(*screen, SDL_Rect{ 30, 22, 20, 8 }, "82");
	auto const v = Visible();
	ASSERT_EQ(v.size(), 1u);
	EXPECT_EQ(v[0].text, "82");
}

TEST_F(TextRegistryTest, blitsCarryTextAlong)
{
	std::unique_ptr<SGPVSurface> page{ new SGPVSurface(32, 16, 16) };
	Fill(*page, SDL_Rect{ 0, 0, 32, 16 }, BG);
	Print(*page, SDL_Rect{ 1, 1, 10, 8 }, "Link");
	EXPECT_TRUE(Visible().empty()); // not on screen yet

	BltVideoSurface(screen.get(), page.get(), 20, 10, nullptr);
	auto const v = Visible();
	ASSERT_EQ(v.size(), 1u);
	EXPECT_EQ(v[0].text, "Link");
	EXPECT_EQ(v[0].rect.x, 21);
	EXPECT_EQ(v[0].rect.y, 11);
}

TEST_F(TextRegistryTest, disabledRegistryRecordsNothing)
{
	TextRegistry::SetEnabled(false);
	Print(*screen, SDL_Rect{ 2, 2, 20, 8 }, "Hello");
	TextRegistry::SetEnabled(true);
	EXPECT_TRUE(Visible().empty());
}
