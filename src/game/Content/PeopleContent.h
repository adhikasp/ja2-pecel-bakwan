#pragma once

#include "JA2Types.h"
#include "Quests.h"

#include <cstdint>
#include <vector>

/** @file
 * Native people & quest content registry (issue #156).
 *
 * One compiled source of truth for the named characters and the quests they
 * carry. It replaces the four-layer lookup (roster JSON, binary/JSON script
 * records, the Quests enum + QuestText, and scattered C++ triggers) with typed
 * cross-references that unit tests can assert over and automation can dump.
 *
 * The table in PeopleContent.cc is generated once by
 * `tools/generate_people_content.py` from those four layers; from then on it is
 * edited directly, natively, like the rest of the gameplay content.
 */
namespace People
{
	/** What kind of character profile this is (native mirror of MercType). */
	enum class NpcKind : uint8_t
	{
		Aim,
		Merc,
		Imp,
		Rpc,
		Npc,
		Vehicle,
		Reserved,
	};

	/** How a character relates to a quest. */
	enum class QuestRole : uint8_t
	{
		Giver,     // a script record startQuest's it
		Resolver,  // a script record endQuest's it
		Dialogue,  // a script record is only reachable while the quest has a state
	};

	/** A quest state transition. */
	enum class QuestChange : uint8_t { Start, End };

	/** The generic lifecycle every quest shares. */
	enum class QuestStage : uint8_t { NotStarted, InProgress, Done };

	/** A typed link from a character to one of its quest roles. */
	struct QuestLink
	{
		Quests    quest;
		QuestRole role;
	};

	/** A typed quest transition, carried by a dialogue action. */
	struct QuestTransition
	{
		Quests      quest;
		QuestChange change;
	};

	/** A named character compiled from the roster. */
	struct NpcDef
	{
		ProfileID              id;
		NpcKind                kind;
		const char*            name;          // internal name, e.g. "MIGUEL"
		const char*            homeSectors;   // comma separated sectors, "" if placed by map data
		bool                   placedAtStart; // placed at game init (roster placement)
		uint8_t                schedule;      // 0 = no daily schedule
		uint8_t                opinionSeed;   // initial opinion of the player (feeds issue #119)
		std::vector<QuestLink> quests;
	};

	/** A quest compiled from the Quests enum, the script records and the triggers. */
	struct QuestDef
	{
		Quests                  id;
		const char*             name;          // "FOOD_ROUTE"
		const char*             title;         // "Food Route"
		std::vector<ProfileID>  givers;        // who starts it
		std::vector<ProfileID>  resolvers;     // who ends it
		bool                    selfResolving; // no person starts or ends it; the game does
		std::vector<ProfileID>  dialogue;      // whose dialogue is gated on its state
		std::vector<Quests>     prerequisites; // quests that must be done/underway first
		const char*             reward;        // what the player gets
		const char*             reputationHook;// town whose loyalty it feeds, "" if none
		const char*             deedHook;      // fact/history entry it sets, "" if none
		const char*             note;          // where the hardcoded trigger lives
		std::vector<QuestStage> stages{ QuestStage::NotStarted, QuestStage::InProgress, QuestStage::Done };
	};

	/** All named characters, by profile id. */
	std::vector<NpcDef> const& NpcDefs();

	/** All quests, by quest id. */
	std::vector<QuestDef> const& QuestDefs();

	NpcDef const*   FindNpc(ProfileID id);
	QuestDef const* FindQuest(Quests id);

	/** The internal name of a profile, or "" if it has none. */
	const char* NpcName(ProfileID id);
	/** The display title of a quest, or "" if it has none. */
	const char* QuestTitle(Quests id);

	/** Print names for the automation dump. */
	const char* NpcKindName(NpcKind kind);
	const char* QuestRoleName(QuestRole role);
	const char* QuestStageName(QuestStage stage);

	/** Apply a typed quest transition through the real quest engine. */
	void ApplyQuestTransition(QuestTransition const& transition, const SGPSector& sector);

	/** Apply a transition by quest id. */
	inline void ApplyQuestTransition(Quests quest, QuestChange change, const SGPSector& sector)
	{
		ApplyQuestTransition(QuestTransition{ quest, change }, sector);
	}

	/** Apply a transition from a legacy raw quest id (NO_QUEST = do nothing). */
	void ApplyQuestTransition(uint8_t rawQuest, QuestChange change, const SGPSector& sector);

	/** The defs marked NpcKind::Reserved, for the "every profile is accounted for" test. */
	bool IsReservedProfile(ProfileID id);
}
