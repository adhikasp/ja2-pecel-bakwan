#pragma once
// View models over the game state that more than one native screen will use. They are also the Phase 2 examples of
// the framework: GameStatusViewModel follows live state through Notify, SaveSlotViewModel is built from a save file.

#include "ViewModel.h"

struct SAVED_GAME_HEADER;

namespace NativeUI
{
	/** "$12,345" (negative: "-$1,200"). */
	std::string FormatMoney(int amount);
	/** "Day 3, 07:05" */
	std::string FormatWhen(unsigned day, unsigned hour, unsigned minute);

	/** Day, time, money, sector and team size now ("status"). Topics: money, clock, sector, team. */
	class GameStatusViewModel final : public ViewModel
	{
	public:
		GameStatusViewModel();
		void Refresh() override;
		void Describe(Fields&) override;

		int         day = 0, hour = 0, minute = 0, money = 0, mercs = 0;
		std::string when, moneyText, sector;
	};

	/** One row of a save list, from the save's header ("saveslot"). */
	class SaveSlotViewModel final : public ViewModel
	{
	public:
		SaveSlotViewModel();
		void Load(std::string const& saveName, SAVED_GAME_HEADER const&);
		void Describe(Fields&) override;

		std::string name, description, when, sector, moneyText, difficulty, version;
		int         day = 0, mercs = 0, money = 0;
		bool        ironman = false;
	};

	/** Registers the factories above with automation (ja2.viewModel). */
	void RegisterGameViewModels();
}
