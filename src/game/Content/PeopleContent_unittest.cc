#ifdef WITH_UNITTESTS

#include "PeopleContent.h"

#include "QuestText.h"
#include "Soldier_Profile.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <set>

namespace
{
	bool HasLink(People::NpcDef const& npc, Quests quest, People::QuestRole role)
	{
		return std::any_of(npc.quests.begin(), npc.quests.end(),
			[&](People::QuestLink const& l) { return l.quest == quest && l.role == role; });
	}

	bool Contains(std::vector<ProfileID> const& v, ProfileID id)
	{
		return std::find(v.begin(), v.end(), id) != v.end();
	}

	People::NpcDef const& Npc(char const* name)
	{
		for (People::NpcDef const& n : People::NpcDefs())
		{
			if (std::string(n.name) == name) return n;
		}
		ADD_FAILURE() << "no NpcDef named " << name;
		return People::NpcDefs().front();
	}
}

// --- the roster is ported whole -------------------------------------------------

TEST(PeopleContent, EveryRosterProfileHasADef)
{
	// 159 declared profiles in mercs-profile-info.json (the #110 inventory).
	EXPECT_EQ(People::NpcDefs().size(), 159u);

	std::set<ProfileID> ids;
	for (People::NpcDef const& n : People::NpcDefs())
	{
		EXPECT_LT(n.id, NUM_PROFILES);
		EXPECT_FALSE(std::string(n.name).empty()) << int(n.id);
		EXPECT_TRUE(ids.insert(n.id).second) << "duplicate profile id " << int(n.id);
	}
}

TEST(PeopleContent, FindNpcResolvesEveryDef)
{
	for (People::NpcDef const& n : People::NpcDefs())
	{
		ASSERT_EQ(People::FindNpc(n.id), &n) << int(n.id);
		EXPECT_STREQ(People::NpcName(n.id), n.name);
	}
}

TEST(PeopleContent, KindsAreNamed)
{
	for (People::NpcDef const& n : People::NpcDefs())
	{
		EXPECT_NE(std::string(People::NpcKindName(n.kind)), "") << int(n.id);
	}
	EXPECT_TRUE(People::IsReservedProfile(NPC164));
	EXPECT_FALSE(People::IsReservedProfile(MIGUEL));
}

// --- the quests are ported whole ------------------------------------------------

TEST(PeopleContent, EveryQuestSlotHasADef)
{
	// 24 used quest slots in the #110 inventory (0-22 plus KILL_DEIDRANNA).
	EXPECT_EQ(People::QuestDefs().size(), 24u);

	std::set<Quests> ids;
	for (People::QuestDef const& q : People::QuestDefs())
	{
		EXPECT_TRUE(ids.insert(q.id).second) << q.name;
	}
}

TEST(PeopleContent, TitlesMatchQuestText)
{
	// Cross-check the ported titles against the layer they came from.
	for (People::QuestDef const& q : People::QuestDefs())
	{
		ASSERT_EQ(People::QuestTitle(q.id), q.title);
		EXPECT_EQ(std::string(q.title), QuestDescText[q.id].to_std_string()) << q.name;
	}
}

TEST(PeopleContent, EveryReferenceResolves)
{
	for (People::NpcDef const& n : People::NpcDefs())
	{
		for (People::QuestLink const& l : n.quests)
		{
			EXPECT_NE(People::FindQuest(l.quest), nullptr)
				<< n.name << " references undefined quest " << int(l.quest);
			EXPECT_NE(std::string(People::QuestRoleName(l.role)), "");
		}
	}

	for (People::QuestDef const& q : People::QuestDefs())
	{
		for (ProfileID id : q.givers)   EXPECT_NE(People::FindNpc(id), nullptr) << q.name;
		for (ProfileID id : q.resolvers) EXPECT_NE(People::FindNpc(id), nullptr) << q.name;
		for (ProfileID id : q.dialogue)  EXPECT_NE(People::FindNpc(id), nullptr) << q.name;
		for (Quests p : q.prerequisites) EXPECT_NE(People::FindQuest(p), nullptr) << q.name;
	}
}

TEST(PeopleContent, EveryQuestHasAGiverAndAResolverOrIsSelfResolving)
{
	for (People::QuestDef const& q : People::QuestDefs())
	{
		bool const hasGiver    = !q.givers.empty();
		bool const hasResolver = !q.resolvers.empty();
		EXPECT_TRUE((hasGiver && hasResolver) || q.selfResolving)
			<< q.name << ": givers=" << q.givers.size() << " resolvers=" << q.resolvers.size();
		EXPECT_EQ(q.selfResolving, !(hasGiver && hasResolver)) << q.name;
		EXPECT_EQ(q.stages.size(), 3u) << q.name;
	}
}

// --- the port matches the #110 finding ------------------------------------------

TEST(PeopleContent, ScriptRecordLinksMatchTheInventory)
{
	// Binary .npc + JSON overrides, straight from the research tables.
	EXPECT_TRUE(HasLink(Npc("MIGUEL"),   QUEST_FOOD_ROUTE,      People::QuestRole::Giver));
	EXPECT_TRUE(HasLink(Npc("MIGUEL"),   QUEST_DELIVER_LETTER,  People::QuestRole::Resolver));
	EXPECT_TRUE(HasLink(Npc("AUNTIE"),   QUEST_BLOODCATS,       People::QuestRole::Giver));
	EXPECT_TRUE(HasLink(Npc("AUNTIE"),   QUEST_BLOODCATS,       People::QuestRole::Resolver));
	EXPECT_TRUE(HasLink(Npc("CARMEN"),   QUEST_KILL_TERRORISTS, People::QuestRole::Giver));
	EXPECT_TRUE(HasLink(Npc("SHANK"),    QUEST_FREE_SHANK,      People::QuestRole::Giver));
	EXPECT_TRUE(HasLink(Npc("RAT"),      QUEST_FIND_HERMIT,     People::QuestRole::Giver));
	EXPECT_TRUE(HasLink(Npc("GABBY"),    QUEST_FIND_HERMIT,     People::QuestRole::Resolver));
	EXPECT_TRUE(HasLink(Npc("WALDO"),    QUEST_CHOPPER_PILOT,   People::QuestRole::Giver));
	EXPECT_TRUE(HasLink(Npc("MADLAB"),   QUEST_DELIVER_VIDEO_CAMERA, People::QuestRole::Giver));

	// JSON overrides must win over the binary records.
	EXPECT_TRUE(HasLink(Npc("MARIA"),    QUEST_RESCUE_MARIA,    People::QuestRole::Dialogue));
	EXPECT_TRUE(HasLink(Npc("MARIA"),    QUEST_DELIVER_LETTER,  People::QuestRole::Dialogue));
	EXPECT_TRUE(HasLink(Npc("JOEY"),     QUEST_RUNAWAY_JOEY,    People::QuestRole::Resolver));
	EXPECT_TRUE(HasLink(Npc("SKYRIDER"), QUEST_CHOPPER_PILOT,   People::QuestRole::Resolver));
	EXPECT_TRUE(HasLink(Npc("SKYRIDER"), QUEST_ESCORT_SKYRIDER, People::QuestRole::Resolver));
	EXPECT_TRUE(HasLink(Npc("MARTHA"),   QUEST_RUNAWAY_JOEY,    People::QuestRole::Giver));
	EXPECT_TRUE(HasLink(Npc("JOHN"),     QUEST_ESCORT_TOURISTS, People::QuestRole::Resolver));
	EXPECT_TRUE(HasLink(Npc("MARY"),     QUEST_ESCORT_TOURISTS, People::QuestRole::Resolver));
}

TEST(PeopleContent, QuestAggregatesMatchTheInventory)
{
	People::QuestDef const* const letter = People::FindQuest(QUEST_DELIVER_LETTER);
	ASSERT_NE(letter, nullptr);
	EXPECT_TRUE(letter->selfResolving); // started at game init
	EXPECT_TRUE(Contains(letter->resolvers, MIGUEL));
	for (ProfileID id : { CARLOS, DIMITRI, DYNAMO, MARIA, FATIMA, PACOS })
	{
		EXPECT_TRUE(Contains(letter->dialogue, id)) << People::NpcName(id);
	}

	People::QuestDef const* const money = People::FindQuest(QUEST_KINGPIN_MONEY);
	ASSERT_NE(money, nullptr);
	EXPECT_TRUE(Contains(money->givers, KINGPIN));
	EXPECT_TRUE(Contains(money->givers, DARREN));
	EXPECT_TRUE(Contains(money->givers, FRANK));
	EXPECT_TRUE(Contains(money->resolvers, KINGPIN));

	People::QuestDef const* const creatures = People::FindQuest(QUEST_CREATURES);
	ASSERT_NE(creatures, nullptr);
	for (ProfileID id : { FRED, MATT, OSWALD, CALVIN, CARL })
	{
		EXPECT_TRUE(Contains(creatures->givers, id)) << People::NpcName(id);
	}
	EXPECT_TRUE(creatures->selfResolving); // ended by the CREATURES meanwhile
}

TEST(PeopleContent, PlacementSectorsArePorted)
{
	EXPECT_STREQ(Npc("HAMOUS").homeSectors, "G6,F12,D7,D3,D9");
	EXPECT_FALSE(Npc("HAMOUS").placedAtStart);
	EXPECT_STREQ(Npc("SKYRIDER").homeSectors, "B15,E14,D12,C16");
	EXPECT_TRUE(Npc("SKYRIDER").placedAtStart);
	EXPECT_STREQ(Npc("MADLAB").homeSectors, "H7,H16,I11,E4");
	EXPECT_STREQ(Npc("GABBY").homeSectors, "H11,I4");
	EXPECT_STREQ(Npc("MIGUEL").homeSectors, ""); // placed by map data
}

#endif
