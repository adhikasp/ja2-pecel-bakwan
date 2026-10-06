// The tactical AI's decision scalars are arithmetic that crosses modules: the threat
// value is built from the action-point cost in Points.cc and the armour rating in
// Weapons.cc, the morale verdict is a ratio of those threat values weighted by the
// opplist knowledge in OppList.cc, and the interrupt duel is level/knowledge/range
// arithmetic in TeamTurns.cc. These tests pin the calculations the audit documents
// (docs/plan/tactical-ai.md, sections 5, 9 and 10) and, in particular, that the AI
// picks up a change made in the module it borrows from.
//
// No map is loaded: the tests exercise the arithmetic-only paths (threat with
// reduceForCover = FALSE, interrupt points without the NIGHTOPS light lookup), so they
// run as ordinary gtest without a sector.

#include "gtest/gtest.h"

#include "AI.h"
#include "AIInternals.h"
#include "Assignments.h"
#include "DefaultContentManagerUT.h"
#include "Isometric_Utils.h"
#include "OppList.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Control.h"
#include "TeamTurns.h"
#include "Types.h"
#include "Weapons.h"

#include <string>

namespace
{
	using AICalculationsTest = DefaultContentManagerUT::BaseTest;

	UINT16 Item(std::string const& name)
	{
		ItemModel const* const model = GCM->getItemByName(ST::string(name));
		EXPECT_NE(model, nullptr) << "unknown item " << name;
		return model ? model->getItemIndex() : 0;
	}

	INT32 WeaponDeadliness(UINT16 const item)
	{
		return GCM->getWeapon(item)->ubDeadliness;
	}

	// A soldier with every field the AI mathematics reads set to a known value. The
	// caller overrides what the test is about.
	SOLDIERTYPE Man(Team const team, Side const side, UINT8 const id)
	{
		SOLDIERTYPE s{};
		s.bActive = TRUE;
		s.bInSector = TRUE;
		s.ubID = id;
		s.bTeam = team;
		s.bSide = side;
		s.ubSoldierClass = SOLDIER_CLASS_ARMY;
		s.ubCivilianGroup = NON_CIV_GROUP;
		s.ubProfile = NO_PROFILE;
		s.ubBodyType = 0; // REGMALE
		// An assignment below ON_DUTY puts the soldier on the map, which is what makes
		// the threat value count his gear (AIUtils.cc: bAssignment < ON_DUTY).
		s.bAssignment = SQUAD_1;
		s.bUnderFire = TRUE; // neutralises the un-fired-upon morale bonus
		s.bLife = 75;
		s.bLifeMax = 75;
		s.bBreath = 75;
		s.bBreathMax = 75;
		s.bExpLevel = 5;
		s.bAgility = 60;
		s.bDexterity = 60;
		s.bMarksmanship = 50;
		s.bActionPoints = 20;
		s.bInitialActionPoints = 25;
		s.bDirection = SOUTH;
		s.sLastTarget = NOWHERE;
		s.bOppCnt = 0;
		s.bShock = 0;
		s.bBleeding = 0;
		// CUNNINGSOLO is the one attitude that does not nudge the morale category, so
		// the raw band is visible in CalcMorale's result.
		s.bAttitude = CUNNINGSOLO;
		return s;
	}

	void GiveGun(SOLDIERTYPE& s, UINT16 const gun)
	{
		s.inv[HANDPOS].usItem = gun;
		s.inv[HANDPOS].ubNumberOfObjects = 1;
		s.inv[HANDPOS].bStatus[0] = 100;
	}

	// The documented morale score, evaluated by hand for one soldier opposed by one
	// known opponent and counting itself as its only same-side friend:
	//
	//   their total threat = pct * threat(opponent)
	//   our total threat   = pct * threat(opponent) * pct * threat(self)
	//   score              = 100 * our / their / their = 100 * threat(self)/threat(opponent)
	//
	// The knowledge percentage pct cancels, so this isolates the ratio of the two
	// threat values - a value the audit documents and the modules supply.
	INT32 RawMoraleScore(SOLDIERTYPE& self, SOLDIERTYPE& opponent)
	{
		INT32 const selfThreat = CalcManThreatValue(&self, opponent.sGridNo, FALSE, &self);
		INT32 const oppThreat = CalcManThreatValue(&opponent, self.sGridNo, FALSE, &self);
		EXPECT_GT(oppThreat, 0);
		return oppThreat > 0 ? 100 * selfThreat / oppThreat : 0;
	}

	INT8 MoraleBand(INT32 const score)
	{
		if (score <= 25) return MORALE_HOPELESS;
		if (score <= 50) return MORALE_WORRIED;
		if (score <= 150) return MORALE_NORMAL;
		if (score <= 300) return MORALE_CONFIDENT;
		return MORALE_FEARLESS;
	}

	// The two soldiers used by the morale and interrupt tests: an army soldier under
	// evaluation ("us") and a player soldier opposite ("them"). Both are added to the
	// active merc list so CalcMorale's FOR_EACH_MERC sees them, and removed afterwards.
	struct DuelFixture : AICalculationsTest
	{
		SOLDIERTYPE us = Man(ENEMY_TEAM, Side::ENEMY, 0);
		SOLDIERTYPE them = Man(OUR_TEAM, Side::FRIENDLY, 1);

		void SetUp() override
		{
			AICalculationsTest::SetUp();
			us.sGridNo = 160 * 10;
			them.sGridNo = 160 * 20;
			us.bDirection = EAST;
			them.bDirection = WEST;
			GiveGun(us, Item("MP5K"));
			GiveGun(them, Item("MP5K"));
			us.bOppList[them.ubID] = SEEN_CURRENTLY;
			them.bOppList[us.ubID] = SEEN_CURRENTLY;
			// Knowledge is global state; clear it so each test starts from a clean
			// slate rather than whatever the previous one left behind.
			gbPublicOpplist[us.bTeam][them.ubID] = NOT_HEARD_OR_SEEN;
			gbPublicOpplist[them.bTeam][us.ubID] = NOT_HEARD_OR_SEEN;
			gbSeenOpponents[us.ubID][them.ubID] = 0;
			gbSeenOpponents[them.ubID][us.ubID] = 0;
			AddMercSlot(&us);
			AddMercSlot(&them);
		}

		void TearDown() override
		{
			RemoveMercSlot(&us);
			RemoveMercSlot(&them);
			AICalculationsTest::TearDown();
		}
	};
}

// ---------------------------------------------------------------------------------
// Threat value (docs/plan/tactical-ai.md section 5): the AI's scalar "how dangerous"
// ---------------------------------------------------------------------------------

TEST_F(AICalculationsTest, threatValueMatchesTheAuditedFormula)
{
	SOLDIERTYPE me = Man(OUR_TEAM, Side::FRIENDLY, 0);
	SOLDIERTYPE enemy = Man(ENEMY_TEAM, Side::ENEMY, 1);
	me.sGridNo = 160 * 10;
	enemy.sGridNo = 160 * 20;
	GiveGun(enemy, Item("MP5K"));

	// The formula borrows three things from other modules: the action points the
	// Points module computes, the protection percentage the Weapons/armour content
	// computes, and the weapon deadliness the externalized item data carries.
	INT32 const ap = CalcActionPoints(&enemy);
	INT32 const armour = ArmourPercent(&enemy);
	INT32 const deadliness = WeaponDeadliness(enemy.inv[HANDPOS].usItem);

	INT32 const expected =
		enemy.bExpLevel +
		ap +
		enemy.bActionPoints / 2 +
		enemy.bLife / 10 +
		armour / 4 +
		enemy.bMarksmanship / 5 +
		deadliness -
		enemy.bBleeding / 5 -
		(100 - enemy.bBreath) / 10 -
		enemy.bShock;

	EXPECT_EQ(CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me), expected);
	EXPECT_GT(expected, 0);
}

TEST_F(AICalculationsTest, threatValueCountsTheActionPointsThePointsModuleComputes)
{
	SOLDIERTYPE me = Man(OUR_TEAM, Side::FRIENDLY, 0);
	SOLDIERTYPE enemy = Man(ENEMY_TEAM, Side::ENEMY, 1);
	me.sGridNo = 160 * 10;
	enemy.sGridNo = 160 * 20;
	GiveGun(enemy, Item("MP5K"));

	INT32 const before = CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me);
	INT32 const apBefore = CalcActionPoints(&enemy);

	enemy.bAgility += 20; // agility feeds the Points module's AP formula only

	INT32 const after = CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me);
	INT32 const apAfter = CalcActionPoints(&enemy);

	ASSERT_NE(apBefore, apAfter) << "the agility change must actually move CalcActionPoints";
	EXPECT_EQ(after - before, apAfter - apBefore);
}

TEST_F(AICalculationsTest, threatValueCountsTheArmourTheWeaponsModuleComputes)
{
	SOLDIERTYPE me = Man(OUR_TEAM, Side::FRIENDLY, 0);
	SOLDIERTYPE enemy = Man(ENEMY_TEAM, Side::ENEMY, 1);
	me.sGridNo = 160 * 10;
	enemy.sGridNo = 160 * 20;
	GiveGun(enemy, Item("MP5K"));

	INT32 const bare = CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me);
	INT32 const armourBare = ArmourPercent(&enemy);

	enemy.inv[VESTPOS].usItem = Item("KEVLAR_VEST");
	enemy.inv[VESTPOS].ubNumberOfObjects = 1;
	enemy.inv[VESTPOS].bStatus[0] = 100;

	INT32 const armoured = CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me);
	INT32 const armourClad = ArmourPercent(&enemy);

	ASSERT_NE(armourBare, armourClad) << "the vest must actually move ArmourPercent";
	EXPECT_EQ(armoured - bare, armourClad / 4 - armourBare / 4);
}

TEST_F(AICalculationsTest, threatValueCountsTheWeaponDeadlinessFromTheContent)
{
	SOLDIERTYPE me = Man(OUR_TEAM, Side::FRIENDLY, 0);
	SOLDIERTYPE enemy = Man(ENEMY_TEAM, Side::ENEMY, 1);
	me.sGridNo = 160 * 10;
	enemy.sGridNo = 160 * 20;
	GiveGun(enemy, Item("MP5K"));

	INT32 const before = CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me);
	INT32 const deadlinessBefore = WeaponDeadliness(enemy.inv[HANDPOS].usItem);

	GiveGun(enemy, Item("THOMPSON"));

	INT32 const after = CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me);
	INT32 const deadlinessAfter = WeaponDeadliness(enemy.inv[HANDPOS].usItem);

	ASSERT_NE(deadlinessBefore, deadlinessAfter);
	EXPECT_EQ(after - before, deadlinessAfter - deadlinessBefore);
}

TEST_F(AICalculationsTest, threatValueRewardsAnOpponentFacingMe)
{
	SOLDIERTYPE me = Man(OUR_TEAM, Side::FRIENDLY, 0);
	SOLDIERTYPE enemy = Man(ENEMY_TEAM, Side::ENEMY, 1);
	me.sGridNo = 160 * 10;
	enemy.sGridNo = 160 * 20;

	// Facing exactly towards me adds 5%; the base value is read with a facing that
	// does not (see Man()).
	enemy.bDirection = NORTH; // the direction from the enemy to me
	int const facing = CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me);
	enemy.bDirection = EAST;
	int const away = CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me);

	EXPECT_EQ(facing, away + away / 20);
}

TEST_F(AICalculationsTest, threatValueRewardsTheLastTargetTile)
{
	SOLDIERTYPE me = Man(OUR_TEAM, Side::FRIENDLY, 0);
	SOLDIERTYPE enemy = Man(ENEMY_TEAM, Side::ENEMY, 1);
	me.sGridNo = 160 * 10;
	enemy.sGridNo = 160 * 20;

	int const neutral = CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me);
	enemy.sLastTarget = me.sGridNo;
	int const aimed = CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me);

	EXPECT_EQ(aimed, neutral + neutral / 10);
}

TEST_F(AICalculationsTest, inactiveAndDeadMenAreNoThreatAndTheLivingNeverFallBelowOne)
{
	SOLDIERTYPE me = Man(OUR_TEAM, Side::FRIENDLY, 0);
	SOLDIERTYPE enemy = Man(ENEMY_TEAM, Side::ENEMY, 1);
	me.sGridNo = 160 * 10;
	enemy.sGridNo = 160 * 20;

	enemy.bInSector = FALSE;
	EXPECT_EQ(CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me), -999);

	enemy.bInSector = TRUE;
	enemy.bLife = 0;
	EXPECT_EQ(CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me), -999);

	// A living man beaten down to nothing still floors at 1.
	enemy = Man(ENEMY_TEAM, Side::ENEMY, 1);
	enemy.sGridNo = 160 * 20;
	enemy.bExpLevel = 0;
	enemy.bAgility = 0;
	enemy.bDexterity = 0;
	enemy.bLifeMax = 1;
	enemy.bLife = 1;
	enemy.bBreath = 0;
	enemy.bActionPoints = 0;
	enemy.bMarksmanship = 0;
	enemy.bShock = 100;
	EXPECT_EQ(CalcManThreatValue(&enemy, me.sGridNo, FALSE, &me), 1);
}

// ---------------------------------------------------------------------------------
// Morale (docs/plan/tactical-ai.md section 9.1)
// ---------------------------------------------------------------------------------

TEST_F(DuelFixture, moraleIsFearlessWithoutAKnownOpponent)
{
	// Forget the opponent entirely: no opponent is known, so the score is the "500"
	// that the code assigns when there is no threat at all.
	us.bOppList[them.ubID] = NOT_HEARD_OR_SEEN;
	gbPublicOpplist[us.bTeam][them.ubID] = NOT_HEARD_OR_SEEN;
	gbSeenOpponents[us.ubID][them.ubID] = 0;

	EXPECT_EQ(CalcMorale(&us), MORALE_FEARLESS);
}

TEST_F(DuelFixture, moraleIsTheRatioOfTheTwoThreatValues)
{
	INT32 const raw = RawMoraleScore(us, them);
	EXPECT_EQ(CalcMorale(&us), MoraleBand(raw));
}

TEST_F(DuelFixture, moraleFallsWhenTheOpponentIsTheMoreDangerousMan)
{
	// A weak soldier against a far better one: the score is 100 * our threat / theirs,
	// so it must drop bands.
	us.bExpLevel = 1;
	us.bAgility = 0;
	us.bDexterity = 0;
	us.bMarksmanship = 0;

	them.bExpLevel = 10;
	them.bMarksmanship = 100;
	them.bAgility = 100;
	them.bDexterity = 100;
	them.bLifeMax = 100;
	them.bLife = 100;
	GiveGun(them, Item("C7"));

	INT32 const raw = RawMoraleScore(us, them);
	EXPECT_LE(raw, 50) << "this fixture is supposed to be outmatched into WORRIED or worse";
	EXPECT_EQ(CalcMorale(&us), MoraleBand(raw));
	EXPECT_LT(CalcMorale(&us), MORALE_NORMAL);
}

TEST_F(DuelFixture, moraleTracksTheOpponentsArmourFromTheWeaponsModule)
{
	// Arming the opponent up is a Weapons-module change; it must reach morale through
	// the threat value, because morale is the ratio of the two threat values.
	INT32 const before = RawMoraleScore(us, them);
	them.inv[VESTPOS].usItem = Item("KEVLAR_VEST");
	them.inv[VESTPOS].ubNumberOfObjects = 1;
	them.inv[VESTPOS].bStatus[0] = 100;
	INT32 const after = RawMoraleScore(us, them);

	EXPECT_LT(after, before);
	EXPECT_EQ(CalcMorale(&us), MoraleBand(after));
}

TEST_F(DuelFixture, moraleIsReadFromTheTeamsPublicKnowledgeToo)
{
	// The AI has not personally seen the opponent, but the team has radioed it in.
	us.bOppList[them.ubID] = NOT_HEARD_OR_SEEN;
	gbPublicOpplist[us.bTeam][them.ubID] = SEEN_CURRENTLY;
	gbSeenOpponents[us.ubID][them.ubID] = 0;

	// Without the public knowledge this is a fearless soldier (previous test); with
	// it the radioed opponent counts, so morale is no longer fearless.
	EXPECT_NE(CalcMorale(&us), MORALE_FEARLESS);
}

TEST_F(DuelFixture, administratorNervesAreSteeledByTwoCategories)
{
	INT32 const base = CalcMorale(&us);
	us.ubSoldierClass = SOLDIER_CLASS_ADMINISTRATOR;
	EXPECT_EQ(CalcMorale(&us), base + 2);
}

TEST_F(DuelFixture, defensiveAttitudeCostsACategoryAndAggressionBuysOne)
{
	us.bAttitude = CUNNINGSOLO;
	INT32 const neutral = CalcMorale(&us);
	us.bAttitude = DEFENSIVE;
	EXPECT_EQ(CalcMorale(&us), neutral - 1);
	us.bAttitude = AGGRESSIVE;
	EXPECT_EQ(CalcMorale(&us), neutral + 1);
}

TEST_F(DuelFixture, aBraveSoldierNeverBreaks)
{
	// Force the score hopeless by stripping us down, then check BRAVESOLO refuses to
	// break (it is raised to WORRIED instead).
	them.bExpLevel = 10;
	them.bMarksmanship = 95;
	GiveGun(them, Item("C7"));
	us.bExpLevel = 1;
	us.bMarksmanship = 0;
	us.bAgility = 0;
	us.bDexterity = 0;
	us.bActionPoints = 0;
	us.bLife = 5;
	us.bBreath = 5;
	us.bAttitude = BRAVESOLO;

	EXPECT_EQ(CalcMorale(&us), MORALE_WORRIED);
}

TEST_F(DuelFixture, anEnemyWithNoUsableWeaponIsHopeless)
{
	us.inv[HANDPOS] = OBJECTTYPE{};
	EXPECT_EQ(CalcMorale(&us), MORALE_HOPELESS);
}

// ---------------------------------------------------------------------------------
// Interrupt duel (docs/plan/tactical-ai.md section 10.2)
// ---------------------------------------------------------------------------------

TEST_F(DuelFixture, interruptPointsAreTheExperienceLevelWhenNothingElseApplies)
{
	us.bExpLevel = 6;
	EXPECT_EQ(CalcInterruptDuelPts(&us, &them, FALSE), 6);
}

TEST_F(DuelFixture, interruptPointsIgnoreDexterityAgilityAndActionPoints)
{
	INT32 const base = CalcInterruptDuelPts(&us, &them, FALSE);

	us.bDexterity += 40;
	us.bAgility += 40;
	us.bActionPoints += 20;
	EXPECT_EQ(CalcInterruptDuelPts(&us, &them, FALSE), base)
		<< "the audit records that these do not enter the duel formula";

	// Experience is the base term, so it does.
	us.bExpLevel += 2;
	EXPECT_EQ(CalcInterruptDuelPts(&us, &them, FALSE), base + 2);
}

TEST_F(DuelFixture, interruptPointsPenaliseNoiseOnlyHearingAndCrowds)
{
	INT32 const base = CalcInterruptDuelPts(&us, &them, FALSE);

	us.bOppList[them.ubID] = HEARD_THIS_TURN;
	EXPECT_EQ(CalcInterruptDuelPts(&us, &them, FALSE), base - 1);

	us.bOppList[them.ubID] = SEEN_CURRENTLY;
	us.bOppCnt = 3;
	EXPECT_EQ(CalcInterruptDuelPts(&us, &them, FALSE), base - 1);
}

TEST_F(DuelFixture, interruptPointsDropWithShockAndAreSetToMinusTenForOnDutyPCs)
{
	INT32 const base = CalcInterruptDuelPts(&us, &them, FALSE);
	us.bShock = 3;
	EXPECT_EQ(CalcInterruptDuelPts(&us, &them, FALSE), base - 3);

	// A player soldier at or above ON_DUTY is hard-set to -10.
	them.bShock = 0;
	them.uiStatusFlags |= SOLDIER_PC;
	them.bAssignment = ON_DUTY;
	EXPECT_EQ(CalcInterruptDuelPts(&them, &us, FALSE), -10);
}

TEST_F(DuelFixture, interruptPointsAreCappedJustBelowAutomatic)
{
	us.bExpLevel = AUTOMATIC_INTERRUPT + 20;
	EXPECT_EQ(CalcInterruptDuelPts(&us, &them, FALSE), AUTOMATIC_INTERRUPT - 1);
}
