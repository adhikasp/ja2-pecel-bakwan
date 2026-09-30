#pragma once
// View models of the Phase 3 front-end screens (docs/ui/mainmenu.md, options.md, saveload.md, newgame.md). They read
// the game state into fields and their commands call the game's own functions; the screens (MainMenuScreen.cc, ...)
// bind them to RML and route the screen changes.

#include "ViewModel.h"

#include <functional>

class SaveGameInfo;

namespace NativeUI
{
	/** What a save shows in lists and cards. */
	struct SaveRow
	{
		int         index = 0;
		std::string name, file, when, sector, mercs, money, saved, difficulty, saving, gunsStyle, mods, thumb, tags;
		bool        quick = false, autoSave = false, ironman = false, deadIsDead = false, selected = false;
		double      modified = 0; // seconds since the epoch
		int         day = 0, minutes = 0, team = 0, balance = 0;
		static void Describe(RowFields<SaveRow>& f)
		{
			f("index", &SaveRow::index)("name", &SaveRow::name)("file", &SaveRow::file)("when", &SaveRow::when)
			 ("sector", &SaveRow::sector)("mercs", &SaveRow::mercs)("money", &SaveRow::money)("saved", &SaveRow::saved)
			 ("difficulty", &SaveRow::difficulty)("saving", &SaveRow::saving)("guns_style", &SaveRow::gunsStyle)
			 ("mods", &SaveRow::mods)("thumb", &SaveRow::thumb)("quick", &SaveRow::quick)("auto", &SaveRow::autoSave)
			 ("ironman", &SaveRow::ironman)("did", &SaveRow::deadIsDead)("selected", &SaveRow::selected);
		}
	};
	/** A save's header as a row (sector names need the underground list: see SaveLoadScreen.cc). */
	SaveRow MakeSaveRow(SaveGameInfo const&, int index);
	/** "Yesterday 21:02", "3 days ago", "10 Sep 2026" relative to @a now (seconds since the epoch). */
	std::string FormatSavedAt(double modified, double now);

	class MainMenuViewModel final : public ViewModel
	{
	public:
		MainMenuViewModel();
		void Refresh() override;
		void Describe(Fields&) override;

		bool hasSaves = false;
		int  saveCount = 0;
		std::string newest; // save name Continue loads
		std::string kicker, title, subtitle, version, copyright;
		std::string lContinue, lNew, lLoad, lOptions, lCredits, lQuit, lLast, lContinueLoads, lTime, lSector, lTeam, lBalance, lGame,
			lChoose, lSelect, lQuitHint;
		std::string continueMeta, newMeta, loadMeta, optionsMeta, creditsMeta, quitMeta;
		std::string lastName, lastWhen, lastSector, lastTeam, lastMoney, lastGame;

		/** Set by the screen: a command picked a screen (or Quit). */
		std::function<void(std::string const&)> onPick;
	};
}
