#pragma once
// The native credits screen's view model (docs/ui/credits.md).

#include "ViewModel.h"

#include <functional>

namespace NativeUI
{
	struct CreditLine
	{
		std::string kind; // "title" (section heading), "line", "gap" (end of a section)
		std::string text;
		static void Describe(RowFields<CreditLine>& f) { f("kind", &CreditLine::kind)("text", &CreditLine::text); }
	};

	struct CreditPerson
	{
		int         index = 0;
		std::string name, title, funny, face, faceBlink;
		bool        blink = false;
		static void Describe(RowFields<CreditPerson>& f)
		{
			f("index", &CreditPerson::index)("name", &CreditPerson::name)("title", &CreditPerson::title)("funny", &CreditPerson::funny)
			 ("face", &CreditPerson::face)("face_blink", &CreditPerson::faceBlink)("blink", &CreditPerson::blink);
		}
	};

	/** One record of the credits file (EDTFile::CREDITS): "@T,{;Text" style codes, see Credits.cc. Returns false for
	 * a record that only sets parameters. Section end ("}") adds a gap after the text. */
	bool ParseCreditRecord(std::string const& record, std::vector<CreditLine>& out);

	class CreditsViewModel final : public ViewModel
	{
	public:
		CreditsViewModel();
		void Describe(Fields&) override;

		/** From the credits file and the name tables (the game's language). */
		void Load();
		/** Advances the reel by @a seconds (not while paused); returns true when it has run out. */
		bool Advance(double seconds);
		/** Scrolls by @a dp (the wheel and arrow keys), clamped to the reel. */
		void Scroll(double dp);

		std::vector<CreditLine>   lines;
		std::vector<CreditPerson> people;
		int         selected = -1; // hovered or focused person
		std::string selName, selTitle, selFunny;
		bool        paused = false;
		bool        finished = false;
		double      offset = 0;      // dp scrolled from the start (the reel starts below its window)
		double      length = 0;      // dp to scroll until the last line has left (set by the screen from the layout)
		double      speed = 60;      // dp per second
		std::string title, pauseLabel, backLabel, teamLabel, hint, pausedText;

		/** Set by the screen: back to the main menu. */
		std::function<void()> onBack;

		void Select(int person);
	};
}
