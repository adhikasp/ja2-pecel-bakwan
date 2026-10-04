#include "gtest/gtest.h"

#include "CampaignScenario.h"
#include "ScenarioItems.h"

#include "Campaign_Types.h"
#include "DefaultContentManagerUT.h"
#include "JA2Types.h"
#include "Quests.h"

#include <stdexcept>

// The campaign harness resolves names and reconciles counters on the live globals.
// These tests pin the pure pieces; the e2e scenario (campaign_state.lua) proves the
// whole staged state survives a save/load round trip.
using CampaignScenarioTest = DefaultContentManagerUT::BaseTest;

TEST_F(CampaignScenarioTest, itemNamesRoundTrip)
{
	UINT16 const g11 = Automation::Scenario::ItemByName("G11");
	EXPECT_EQ(Automation::Scenario::ItemName(g11), "G11");
	EXPECT_THROW(Automation::Scenario::ItemByName("NO_SUCH_ITEM_XYZ"), std::runtime_error);
}

TEST_F(CampaignScenarioTest, questAndFactNamesResolveWithOrWithoutPrefix)
{
	EXPECT_EQ(Automation::Scenario::ResolveQuest("HELD_IN_ALMA"), QUEST_HELD_IN_ALMA);
	EXPECT_EQ(Automation::Scenario::ResolveQuest("QUEST_KINGPIN_MONEY"), QUEST_KINGPIN_MONEY);
	EXPECT_THROW(Automation::Scenario::ResolveQuest("NOT_A_QUEST"), std::runtime_error);

	EXPECT_EQ(Automation::Scenario::ResolveFact("FACT_PABLO_PUNISHED_BY_PLAYER"), FACT_PABLO_PUNISHED_BY_PLAYER);
	EXPECT_EQ(Automation::Scenario::ResolveFact("PABLO_PUNISHED_BY_PLAYER"), FACT_PABLO_PUNISHED_BY_PLAYER);
	EXPECT_THROW(Automation::Scenario::ResolveFact("FACT_NOT_A_FACT"), std::runtime_error);
}

TEST_F(CampaignScenarioTest, garrisonKeepsInBattleCountersInStepAndClamps)
{
	SGPSector const sector(9, 1); // A9
	Automation::Scenario::SetSectorGarrison(sector, 4, 5, 6);
	SECTORINFO const& si = SectorInfo[sector.AsByte()];
	EXPECT_EQ(si.ubNumAdmins, 4);
	EXPECT_EQ(si.ubNumTroops, 5);
	EXPECT_EQ(si.ubNumElites, 6);
	EXPECT_EQ(si.ubAdminsInBattle, si.ubNumAdmins);
	EXPECT_EQ(si.ubTroopsInBattle, si.ubNumTroops);
	EXPECT_EQ(si.ubElitesInBattle, si.ubNumElites);

	Automation::Scenario::SetSectorGarrison(sector, -5, 300, 0);
	SECTORINFO const& si2 = SectorInfo[sector.AsByte()];
	EXPECT_EQ(si2.ubNumAdmins, 0);
	EXPECT_EQ(si2.ubNumTroops, 255);
	EXPECT_EQ(si2.ubNumElites, 0);
	EXPECT_EQ(si2.ubAdminsInBattle, si2.ubNumAdmins);
	EXPECT_EQ(si2.ubTroopsInBattle, si2.ubNumTroops);
	EXPECT_EQ(si2.ubElitesInBattle, si2.ubNumElites);
}
