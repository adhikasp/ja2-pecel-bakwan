#pragma once
// The parts of the native strategic map screen's view model that are plain rules over game-state values: grouping the
// team list, reading the legacy popup boxes (contract, move box), sorting the message log, fitting item art. They take
// small fixture structs instead of the game's globals, so that unit tests (MapScreenModel_unittest.cc) can check them
// against hand-made game states; MapScreenNative.cc fills the structs from the game.

#include <cstdint>
#include <string>
#include <vector>

namespace NativeUI::MapModel
{
	// ---- team list -------------------------------------------------------------------------------------------
	enum class Group { Squad, Other, Transit, Dead, Vehicles };
	struct MercFact
	{
		int         line = 0;          // gCharactersList index (what clicks use)
		std::string name;
		int         assignment = 0;    // enum Assignments (SQUAD_1 = 0 ... ON_DUTY = 20, IN_TRANSIT = 24, ASSIGNMENT_DEAD = 30)
		bool        vehicle = false;
		bool        dead = false;
	};
	struct TeamEntry
	{
		MercFact fact;
		Group    group = Group::Other;
		int      squad = -1;          // 0-based squad for Group::Squad
		bool     firstInGroup = false;
	};
	Group GroupOf(MercFact const&);
	/** The team list in the legacy order (already sorted by the legacy sort keys), grouped when @a grouped: squads by
	 * number, then the others, mercs in transit, the dead, the vehicles; each group keeps the legacy order inside. */
	std::vector<TeamEntry> GroupTeam(std::vector<MercFact> const& mercs, bool grouped);

	// ---- legacy popup boxes as modals ------------------------------------------------------------------------
	struct ContractOption { std::string label; int price = -1; }; // price -1: no price on the line
	/** "Offer One Week ( $8,800 )" -> { "Offer One Week", 8800 }. */
	ContractOption ParseContractLine(std::string const& line);
	struct MoveLine { std::string label; bool merc = false; bool checked = false; };
	/** A move box line: "   *Ivan*" is a merc (indented), selected (stars); "Squad 1" a group, not selected. */
	MoveLine ParseMoveLine(std::string const& line);

	// ---- message log -----------------------------------------------------------------------------------------
	enum class MessageKind { Info, Combat, Team, Money };
	/** @a red / @a dialog: the message's colour was the red one / its kind was MSG_DIALOG. */
	MessageKind Classify(bool red, bool dialog, std::string const& text);
	/** "Barry was wounded in D13." -> "D13"; "" when no sector is named. */
	std::string SectorNamed(std::string const& text);
	/** Game minute -> day number (1 based) and "hh:mm". */
	int         DayOf(uint32_t minute);
	std::string ClockOf(uint32_t minute);
	bool        Matches(std::string const& text, std::string const& query); // case-insensitive substring

	// ---- item art --------------------------------------------------------------------------------------------
	/** How many output pixels an item pixel covers at @a dpScale: a whole number 1..4 (2 at 1080p, 3 at 1440p). */
	int ItemPixelScale(float dpScale);
	struct ItemFit { int scale = 1, w = 0, h = 0, left = 0, top = 0; };
	/** An item picture of @a w x @a h pixels in a box of @a boxW x @a boxH dp: the whole-number scale, stepping down
	 * only when it would not fit (never below 1), and centred. Never a fraction, never stretched. */
	ItemFit FitItem(int w, int h, float boxW, float boxH, float dpScale);
}
