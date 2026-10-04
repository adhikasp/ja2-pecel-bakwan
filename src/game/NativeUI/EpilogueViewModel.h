#pragma once
// The victory epilogue's view model (docs/ui/epilogue.md): the campaign's last page as data — the four stat cards,
// the war effort and those who served. Read once, when the screen opens, so the page cannot change under the
// reader; EpilogueModel turns the raw counters into the numbers, this turns them into words.

#include "EpilogueModel.h"
#include "ViewModel.h"

#include <functional>
#include <string>
#include <vector>

namespace NativeUI
{

	struct EpilogueStatRow
	{
		std::string key;    // "days" | "sectors" | "killed" | "served"
		std::string label;
		int         value = 0;
		std::string sub;    // the detail line under the number, empty when there is none
		static void Describe(RowFields<EpilogueStatRow>& f)
		{
			f("key", &EpilogueStatRow::key)("label", &EpilogueStatRow::label)
			 ("value", &EpilogueStatRow::value)("sub", &EpilogueStatRow::sub);
		}
	};

	struct EpilogueChipRow
	{
		int         profile = 0;
		std::string name;
		std::string face;  // "face-<profile>" (NativeImages)
		std::string fate;  // the tooltip's body: "came home" / "fell in Arulco"
		static void Describe(RowFields<EpilogueChipRow>& f)
		{
			f("profile", &EpilogueChipRow::profile)("name", &EpilogueChipRow::name)
			 ("face", &EpilogueChipRow::face)("fate", &EpilogueChipRow::fate);
		}
	};

	class EpilogueViewModel : public ViewModel
	{
	public:
		EpilogueViewModel();
		void Describe(Fields&) override;

		/** Reads the campaign as it is right now (before ReStartingGame, which LeaveEpilogue runs). */
		void Load();

		std::string title, sub, text;
		std::vector<EpilogueStatRow> stats;
		std::vector<EpilogueChipRow> survivors, fallen;
		bool hasSurvivors = false, hasFallen = false;
		int  days = 0, sectors = 0, killed = 0, served = 0, fell = 0; // the cards' numbers, as plain fields
		std::string effortLabel, homeLabel, fallenLabel, fallenBadge;
		int    effort = 0;      // CurrentPlayerProgressPercentage(), 0-100
		std::string effortPct;  // "78%"
		std::string continueLabel, menuLabel;

		/** Set by the screen: LeaveEpilogue(...) for the button. */
		std::function<void()> onContinue, onMenu;
	};

}
