#include "gtest/gtest.h"

#include "BattleReport.h"

// The battle report core is pure: it takes sides, life totals and times, and the tests
// drive it without a world. The world adapter (combat lifecycle, shot/impact/death
// events) is exercised by the battle e2e scenarios (tests/e2e/battle_ai_eval.lua).

using namespace BattleReport;

namespace
{
	SoldierState Man(Side const side, int const life, int const lifeMax = 100)
	{
		return SoldierState{side, life, lifeMax};
	}

	// A battle with two player soldiers and two enemies, all at full life.
	Recorder Started()
	{
		Recorder r;
		r.Begin("E11", 1000, { Man(Side::Player, 100), Man(Side::Player, 100),
		                       Man(Side::Enemy, 100), Man(Side::Enemy, 100) });
		return r;
	}
}

TEST(BattleReportRecorder, beginCountsTheRoster)
{
	Recorder const r = Started();
	EXPECT_TRUE(r.started());
	EXPECT_FALSE(r.finished());
	EXPECT_EQ(r.report().sector, "E11");
	EXPECT_EQ(r.report().startMs, 1000);
	EXPECT_EQ(r.report().outcome, "unresolved");
	EXPECT_EQ(r.report().endedBy, "in_progress");
	EXPECT_EQ(r.report().player.soldiers, 2);
	EXPECT_EQ(r.report().player.lifeStart, 200);
	EXPECT_EQ(r.report().enemy.soldiers, 2);
	EXPECT_EQ(r.report().enemy.lifeStart, 200);
}

TEST(BattleReportRecorder, shotsAndImpactsAreAttributed)
{
	Recorder r = Started();
	r.NoteShot(Side::Player, true, 1010);
	r.NoteShot(Side::Player, false, 1011);
	r.NoteShot(Side::Enemy, true, 1012);
	EXPECT_EQ(r.report().player.shots, 2);
	EXPECT_EQ(r.report().player.hits, 1);
	EXPECT_EQ(r.report().enemy.shots, 1);
	EXPECT_EQ(r.report().enemy.hits, 1);
	EXPECT_EQ(r.report().contactMs, 10); // the first shot, not the latest

	r.NoteImpact(Side::Player, Side::Enemy, 30);
	r.NoteImpact(Side::Enemy, Side::Player, 12);
	EXPECT_EQ(r.report().player.impacts, 1);
	EXPECT_EQ(r.report().player.damageDealt, 30);
	EXPECT_EQ(r.report().player.damageTaken, 12);
	EXPECT_EQ(r.report().enemy.impacts, 1);
	EXPECT_EQ(r.report().enemy.damageDealt, 12);
	EXPECT_EQ(r.report().enemy.damageTaken, 30);
	// One round, counted once from each end.
	EXPECT_EQ(r.report().player.damageDealt, r.report().enemy.damageTaken);
	EXPECT_EQ(r.report().enemy.damageDealt, r.report().player.damageTaken);
}

TEST(BattleReportRecorder, deathsCountAndTimestampTheFirst)
{
	Recorder r = Started();
	r.NoteDeath(Side::Enemy, 1030);
	r.NoteDeath(Side::Enemy, 1050);
	r.NoteDeath(Side::Player, 1060);
	EXPECT_EQ(r.report().enemy.dead, 2);
	EXPECT_EQ(r.report().player.dead, 1);
	EXPECT_EQ(r.report().firstCasualtyMs, 30);
}

TEST(BattleReportRecorder, aBreakIsOneEventPerSoldier)
{
	Recorder r = Started();
	r.NoteBreak(Side::Enemy, 7, 1020);
	r.NoteBreak(Side::Enemy, 7, 1025); // the same soldier stays broken: not a second break
	r.NoteBreak(Side::Enemy, 9, 1040);
	r.NoteBreak(Side::Player, 2, 1050);
	EXPECT_EQ(r.report().enemy.breaks, 2);
	EXPECT_EQ(r.report().player.breaks, 1);
	EXPECT_EQ(r.report().firstBreakMs, 20);
}

TEST(BattleReportRecorder, roundsCountPlayerTurns)
{
	Recorder r = Started();
	EXPECT_EQ(r.report().rounds, 0);
	r.NoteRound();
	r.NoteRound();
	r.NoteRound();
	EXPECT_EQ(r.report().rounds, 3);
}

TEST(BattleReportRecorder, finishTalliesTheLiving)
{
	Recorder r = Started();
	r.NoteDeath(Side::Enemy, 1030);
	r.Finish(1100, { Man(Side::Player, 90), Man(Side::Player, 100),
	                Man(Side::Enemy, 55, 100) }, false);

	EXPECT_TRUE(r.finished());
	EXPECT_EQ(r.report().player.alive, 2);
	EXPECT_EQ(r.report().player.wounded, 1);
	EXPECT_EQ(r.report().player.lifeEnd, 190);
	EXPECT_EQ(r.report().enemy.alive, 1);
	EXPECT_EQ(r.report().enemy.wounded, 1);
	EXPECT_EQ(r.report().enemy.dead, 1); // from the death events
	EXPECT_EQ(r.report().endMs, 1100);
}

TEST(BattleReportRecorder, finishComputesTheOutcome)
{
	{
		Recorder r = Started();
		r.NoteDeath(Side::Enemy, 1010);
		r.Finish(1100, { Man(Side::Player, 100), Man(Side::Player, 100) }, false);
		EXPECT_EQ(r.report().outcome, "player");
		EXPECT_EQ(r.report().endedBy, "wiped_out");
	}
	{
		Recorder r = Started();
		r.Finish(1100, { Man(Side::Enemy, 100), Man(Side::Enemy, 100) }, false);
		EXPECT_EQ(r.report().outcome, "enemy");
		EXPECT_EQ(r.report().endedBy, "wiped_out");
	}
	{
		Recorder r = Started();
		r.Finish(1100, { Man(Side::Player, 100), Man(Side::Enemy, 100) }, false);
		EXPECT_EQ(r.report().outcome, "draw");
		EXPECT_EQ(r.report().endedBy, "lull");
	}
	{
		Recorder r = Started();
		r.Finish(1100, {}, false);
		EXPECT_EQ(r.report().outcome, "draw");
		EXPECT_EQ(r.report().endedBy, "wiped_out");
	}
}

TEST(BattleReportRecorder, disengageIsTheFirstBreakOrTheEnd)
{
	{
		Recorder r = Started();
		r.NoteBreak(Side::Enemy, 3, 1025);
		r.Finish(1100, { Man(Side::Player, 100), Man(Side::Enemy, 100) }, false);
		EXPECT_EQ(r.report().disengageMs, 25); // the first side to break off
	}
	{
		Recorder r = Started();
		r.Finish(1100, { Man(Side::Player, 100), Man(Side::Enemy, 100) }, false);
		EXPECT_EQ(r.report().disengageMs, 100); // nobody broke: the end of the fight
	}
}

TEST(BattleReportRecorder, objectiveIsHeldWhenTheSideStandsOnIt)
{
	Recorder r = Started();
	r.SetObjective(12714, Side::Player);
	EXPECT_EQ(r.report().objectiveGrid, 12714);
	EXPECT_EQ(r.report().objectiveSide, static_cast<int>(Side::Player));

	r.Finish(1100, { Man(Side::Player, 100) }, true);
	EXPECT_TRUE(r.report().objectiveHeld);
}

TEST(BattleReportRecorder, aFinishedBattleIsFrozen)
{
	Recorder r = Started();
	r.NoteShot(Side::Player, true, 1010);
	r.Finish(1100, { Man(Side::Player, 100) }, false);
	r.NoteShot(Side::Player, true, 1200);
	r.NoteImpact(Side::Player, Side::Enemy, 50);
	r.NoteDeath(Side::Player, 1300);
	r.NoteRound();
	EXPECT_EQ(r.report().player.shots, 1);
	EXPECT_EQ(r.report().player.impacts, 0);
	EXPECT_EQ(r.report().player.dead, 0);
	EXPECT_EQ(r.report().rounds, 0);
}

TEST(BattleReportRecorder, aLiveSnapshotRefreshesTheStanding)
{
	Recorder r = Started();
	r.NoteDeath(Side::Enemy, 1010);
	r.SnapshotLiving({ Man(Side::Player, 80), Man(Side::Enemy, 90, 100) });
	EXPECT_EQ(r.report().player.alive, 1);
	EXPECT_EQ(r.report().player.wounded, 1);
	EXPECT_EQ(r.report().enemy.alive, 1);
	EXPECT_EQ(r.report().enemy.dead, 1);
	r.SetObjectiveHeld(true);
	EXPECT_TRUE(r.report().objectiveHeld);
	EXPECT_FALSE(r.finished());
	EXPECT_EQ(r.report().outcome, "unresolved"); // a snapshot does not decide the battle

	// A frozen report ignores both: the end state is the record.
	r.Finish(1100, { Man(Side::Player, 80) }, false);
	r.SnapshotLiving({ Man(Side::Enemy, 90) });
	r.SetObjectiveHeld(true);
	EXPECT_EQ(r.report().player.alive, 1);
	EXPECT_EQ(r.report().enemy.alive, 0);
	EXPECT_FALSE(r.report().objectiveHeld);
}

TEST(BattleReportRecorder, aLullResumesTheSameBattle)
{
	Recorder r = Started();
	r.NoteShot(Side::Player, true, 1010);
	r.NoteDeath(Side::Enemy, 1020);
	r.Finish(1100, { Man(Side::Player, 100), Man(Side::Enemy, 100) }, false);
	EXPECT_EQ(r.report().outcome, "draw");
	EXPECT_EQ(r.report().endedBy, "lull");

	r.Resume();
	EXPECT_FALSE(r.finished());
	EXPECT_EQ(r.report().outcome, "unresolved");
	EXPECT_EQ(r.report().endedBy, "in_progress");
	EXPECT_EQ(r.report().startMs, 1000);      // the battle's own start, not the resume
	EXPECT_EQ(r.report().player.shots, 1);    // and its tallies carry on
	EXPECT_EQ(r.report().enemy.dead, 1);

	r.NoteShot(Side::Player, true, 1200);
	r.Finish(1300, { Man(Side::Player, 100) }, false);
	EXPECT_EQ(r.report().outcome, "player");
	EXPECT_EQ(r.report().player.shots, 2);
	EXPECT_EQ(r.report().endMs, 1300);
}

TEST(BattleReportRecorder, theNextBattleStartsFresh)
{
	Recorder r = Started();
	r.NoteShot(Side::Player, true, 1010);
	r.NoteDeath(Side::Enemy, 1020);
	r.Finish(1100, { Man(Side::Player, 100) }, false);

	r.Begin("H10", 2000, { Man(Side::Enemy, 80) });
	EXPECT_EQ(r.report().sector, "H10");
	EXPECT_EQ(r.report().startMs, 2000);
	EXPECT_EQ(r.report().player.soldiers, 0);
	EXPECT_EQ(r.report().player.shots, 0);
	EXPECT_EQ(r.report().enemy.soldiers, 1);
	EXPECT_EQ(r.report().enemy.dead, 0);
	EXPECT_FALSE(r.finished());
	// A break from the last battle does not carry its soldier ids over.
	r.NoteBreak(Side::Enemy, 7, 2010);
	EXPECT_EQ(r.report().enemy.breaks, 1);
}

TEST(BattleReportRecorder, eventsBeforeABattleAreIgnored)
{
	Recorder r;
	r.NoteShot(Side::Player, true, 10);
	r.NoteDeath(Side::Enemy, 11);
	r.NoteRound();
	r.SetObjective(1234, Side::Enemy);
	r.Finish(20, { Man(Side::Player, 100) }, true);
	EXPECT_FALSE(r.started());
	EXPECT_FALSE(r.finished());
	EXPECT_EQ(r.report().objectiveGrid, -1);
}
