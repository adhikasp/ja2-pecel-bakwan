#include "GameViewModels.h"

#include "Game_Clock.h"
#include "GameSettings.h"
#include "LaptopSave.h"
#include "Overhead.h"
#include "SaveLoadGame.h"
#include "Soldier_Control.h"
#include "StrategicMap.h"

#include <cstdio>

namespace NativeUI
{

std::string FormatMoney(int const amount)
{
	unsigned v = amount < 0 ? unsigned(-(long long)amount) : unsigned(amount);
	std::string digits = std::to_string(v);
	std::string out;
	int n = 0;
	for (auto it = digits.rbegin(); it != digits.rend(); ++it, ++n)
	{
		if (n && n % 3 == 0) out.insert(out.begin(), ',');
		out.insert(out.begin(), *it);
	}
	return (amount < 0 ? "-$" : "$") + out;
}

std::string FormatWhen(unsigned const day, unsigned const hour, unsigned const minute)
{
	char buf[48];
	std::snprintf(buf, sizeof buf, "Day %u, %02u:%02u", day, hour, minute);
	return buf;
}

GameStatusViewModel::GameStatusViewModel() : ViewModel("status", TOPIC_MONEY | TOPIC_CLOCK | TOPIC_SECTOR | TOPIC_TEAM)
{
	Update(true);
}

void GameStatusViewModel::Refresh()
{
	day    = int(GetWorldDay());
	hour   = int(GetWorldHour());
	minute = int(GetWorldMinutesInDay() % 60);
	when   = FormatWhen(day, hour, minute);
	money  = LaptopSaveInfo.iCurrentBalance;
	moneyText = FormatMoney(money);
	sector = gWorldSector.IsValid() ? gWorldSector.AsShortString().to_std_string() : std::string();
	mercs = 0;
	CFOR_EACH_IN_TEAM(s, OUR_TEAM) { ++mercs; }
}

void GameStatusViewModel::Describe(Fields& f)
{
	f.Field("day", day);
	f.Field("hour", hour);
	f.Field("minute", minute);
	f.Field("when", when);
	f.Field("money", money);
	f.Field("money_text", moneyText);
	f.Field("sector", sector);
	f.Field("mercs", mercs);
}

SaveSlotViewModel::SaveSlotViewModel() : ViewModel("saveslot", 0) {}

void SaveSlotViewModel::Load(std::string const& saveName, SAVED_GAME_HEADER const& h)
{
	name        = saveName;
	description = h.sSavedGameDesc.to_std_string();
	day         = int(h.uiDay);
	when        = FormatWhen(h.uiDay, h.ubHour, h.ubMin);
	sector      = h.sSector.AsShortString().to_std_string();
	mercs       = h.ubNumOfMercsOnPlayersTeam;
	money       = h.iCurrentBalance;
	moneyText   = FormatMoney(money);
	switch (h.sInitialGameOptions.ubDifficultyLevel)
	{
		case DIF_LEVEL_EASY:   difficulty = "easy";   break;
		case DIF_LEVEL_MEDIUM: difficulty = "medium"; break;
		case DIF_LEVEL_HARD:   difficulty = "hard";   break;
		default:               difficulty = "unknown"; break;
	}
	ironman = h.sInitialGameOptions.ubGameSaveMode != DIF_CAN_SAVE;
	version = h.zGameVersionNumber;
	Changed();
}

void SaveSlotViewModel::Describe(Fields& f)
{
	f.Field("name", name);
	f.Field("description", description);
	f.Field("when", when);
	f.Field("day", day);
	f.Field("sector", sector);
	f.Field("mercs", mercs);
	f.Field("money", money);
	f.Field("money_text", moneyText);
	f.Field("difficulty", difficulty);
	f.Field("ironman", ironman);
	f.Field("version", version);
}

void RegisterGameViewModels()
{
	RegisterViewModelFactory("status", [] { return std::make_unique<GameStatusViewModel>(); });
}

namespace { bool const g_registered = (RegisterGameViewModels(), true); }

}
