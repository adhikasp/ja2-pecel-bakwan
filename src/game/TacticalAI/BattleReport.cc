#include "BattleReport.h"

#include "AI.h"
#include "Timer_Control.h"
#include "Overhead.h"
#include "Overhead_Types.h"
#include "Soldier_Ani.h"
#include "StrategicMap.h"
#include "Weapons.h"

#include <algorithm>
#include <utility>

namespace BattleReport
{

namespace
{
	// The recorder for the battle in progress (or the last one finished). One battle
	// per sector entry; the next Begin resets it.
	Recorder g_recorder;
	bool     g_listening = false;

	// An objective a scenario declared before combat started (ja2.debug("battle") with
	// start = false), kept until the battle it belongs to begins.
	int  g_pendingObjectiveGrid = -1;
	Side g_pendingObjectiveSide = Side::Player;

	/** Which side a soldier fights for, if the report counts him at all. */
	bool SideOf(SOLDIERTYPE const& s, Side& side)
	{
		switch (s.bTeam)
		{
			case OUR_TEAM:
			case MILITIA_TEAM: side = Side::Player; return true;
			case ENEMY_TEAM:   side = Side::Enemy;  return true;
			default:           return false;
		}
	}

	/** The soldiers of the two sides standing in the sector, as the report counts them. */
	std::vector<SoldierState> StandingSoldiers()
	{
		std::vector<SoldierState> out;
		FOR_EACH_SOLDIER(s)
		{
			if (!s->bInSector || s->bLife <= 0) continue;
			Side side;
			if (!SideOf(*s, side)) continue;
			out.push_back(SoldierState{side, int(s->bLife), int(s->bLifeMax)});
		}
		return out;
	}

	/** Whether the objective's side has a living soldier on the objective tile now. */
	bool ObjectiveHeld()
	{
		if (g_recorder.report().objectiveGrid < 0) return false;
		Side const side = static_cast<Side>(g_recorder.report().objectiveSide);
		bool held = false;
		FOR_EACH_SOLDIER(s)
		{
			if (!s->bInSector || s->bLife <= 0) continue;
			if (s->sGridNo != g_recorder.report().objectiveGrid) continue;
			Side soldierSide;
			if (SideOf(*s, soldierSide) && soldierSide == side) { held = true; break; }
		}
		return held;
	}

	// Subscribe when the first battle starts, not from a global constructor: the
	// observables live in Weapons.cc and Soldier_Ani.cc, and installing from a static
	// initialiser here could write into them before they are constructed.
	void ListenOnce()
	{
		if (g_listening) return;
		OnShotFired.addListener("tactical:battle-report", [](ShotFired const& fired) {
			if (!g_recorder.started() || g_recorder.finished()) return;
			if (!fired.shooter) return;
			Side side;
			if (!SideOf(*fired.shooter, side)) return;
			g_recorder.NoteShot(side, fired.hit != FALSE, int(GetJA2Clock()));
		});
		OnShotImpact.addListener("tactical:battle-report", [](ShotImpact const& impact) {
			if (!g_recorder.started() || g_recorder.finished()) return;
			if (!impact.shooter || !impact.target) return;
			Side shooter, target;
			if (!SideOf(*impact.shooter, shooter) || !SideOf(*impact.target, target)) return;
			g_recorder.NoteImpact(shooter, target, int(impact.impactAfterArmour));
		});
		OnSoldierDeath.addListener("tactical:battle-report", [](SOLDIERTYPE* soldier) {
			if (!soldier || !g_recorder.started() || g_recorder.finished()) return;
			Side side;
			if (!SideOf(*soldier, side)) return;
			g_recorder.NoteDeath(side, int(GetJA2Clock()));
		});
		g_listening = true;
	}
}

char const* SideName(Side side)
{
	return side == Side::Player ? "player" : "enemy";
}

Tally& Recorder::TallyFor(Side side)
{
	return side == Side::Player ? report_.player : report_.enemy;
}

int Recorder::MsSinceStart(int const ms) const
{
	return std::max(0, ms - report_.startMs);
}

void Recorder::Begin(std::string sector, int const ms, std::vector<SoldierState> const& roster)
{
	report_ = Report{};
	broken_.clear();
	report_.started      = true;
	report_.sector       = std::move(sector);
	report_.startMs = ms;
	report_.outcome      = "unresolved";
	report_.endedBy      = "in_progress";

	for (SoldierState const& s : roster)
	{
		Tally& t = TallyFor(s.side);
		++t.soldiers;
		t.lifeStart += s.life;
	}
}

void Recorder::NoteShot(Side const shooter, bool const hit, int const ms)
{
	if (!report_.started || report_.finished) return;
	Tally& t = TallyFor(shooter);
	++t.shots;
	if (hit) ++t.hits;
	if (report_.contactMs < 0) report_.contactMs = MsSinceStart(ms);
}

void Recorder::NoteImpact(Side const shooter, Side const target, int const damage)
{
	if (!report_.started || report_.finished) return;
	++TallyFor(shooter).impacts;
	TallyFor(shooter).damageDealt += damage;
	TallyFor(target).damageTaken += damage;
}

void Recorder::NoteDeath(Side const side, int const ms)
{
	if (!report_.started || report_.finished) return;
	++TallyFor(side).dead;
	if (report_.firstCasualtyMs < 0) report_.firstCasualtyMs = MsSinceStart(ms);
}

void Recorder::NoteBreak(Side const side, int const soldierId, int const ms)
{
	if (!report_.started || report_.finished) return;
	if (std::find(broken_.begin(), broken_.end(), soldierId) != broken_.end()) return;
	broken_.push_back(soldierId);
	++TallyFor(side).breaks;
	if (report_.firstBreakMs < 0) report_.firstBreakMs = MsSinceStart(ms);
}

void Recorder::NoteRound()
{
	if (!report_.started || report_.finished) return;
	++report_.rounds;
}

void Recorder::SetObjective(int const grid, Side const side)
{
	if (!report_.started) return;
	report_.objectiveGrid = grid;
	report_.objectiveSide = static_cast<int>(side);
}

void Recorder::Finish(int const ms, std::vector<SoldierState> const& living, bool const objectiveHeld)
{
	if (!report_.started || report_.finished) return;
	TallyLiving(living);
	report_.finished      = true;
	report_.endMs         = ms;
	report_.objectiveHeld = objectiveHeld;

	bool const playerLeft = report_.player.alive > 0;
	bool const enemyLeft  = report_.enemy.alive  > 0;
	if      (!enemyLeft && !playerLeft) { report_.outcome = "draw";   report_.endedBy = "wiped_out"; }
	else if (!enemyLeft)                { report_.outcome = "player"; report_.endedBy = "wiped_out"; }
	else if (!playerLeft)               { report_.outcome = "enemy";  report_.endedBy = "wiped_out"; }
	else                                { report_.outcome = "draw";   report_.endedBy = "lull"; }

	// Time to disengage: the first side to break off, or the end if nobody broke.
	report_.disengageMs = report_.firstBreakMs >= 0
		? report_.firstBreakMs
		: MsSinceStart(ms);
}

void Recorder::SnapshotLiving(std::vector<SoldierState> const& living)
{
	if (!report_.started || report_.finished) return;
	TallyLiving(living);
}

void Recorder::SetObjectiveHeld(bool const held)
{
	if (!report_.started || report_.finished) return;
	report_.objectiveHeld = held;
}

void Recorder::Resume()
{
	if (!report_.started || !report_.finished) return;
	report_.finished      = false;
	report_.outcome       = "unresolved";
	report_.endedBy       = "in_progress";
	report_.objectiveHeld = false;
}

void Recorder::TallyLiving(std::vector<SoldierState> const& living)
{
	report_.player.alive   = 0;
	report_.player.wounded = 0;
	report_.player.lifeEnd = 0;
	report_.enemy.alive    = 0;
	report_.enemy.wounded  = 0;
	report_.enemy.lifeEnd  = 0;
	for (SoldierState const& s : living)
	{
		Tally& t = TallyFor(s.side);
		++t.alive;
		if (s.life < s.lifeMax) ++t.wounded;
		t.lifeEnd += s.life;
	}
}

void BeginBattle()
{
	ListenOnce();
	std::string const sector = gWorldSector.AsShortString().to_std_string();

	// A lull is a pause, not a new battle: if the report was frozen by one in this
	// sector, the fight has resumed - continue it, so the battle's losses, damage and
	// time span the whole engagement. Anything else starts a fresh report.
	if (g_recorder.finished() && g_recorder.report().sector == sector
		&& g_recorder.report().endedBy == "lull")
	{
		g_recorder.Resume();
	}
	else
	{
		g_recorder.Begin(sector, int(GetJA2Clock()), StandingSoldiers());
	}

	if (g_pendingObjectiveGrid >= 0)
	{
		g_recorder.SetObjective(g_pendingObjectiveGrid, g_pendingObjectiveSide);
		g_pendingObjectiveGrid = -1;
	}
}

void FinishBattle()
{
	if (!g_recorder.started() || g_recorder.finished()) return;
	g_recorder.Finish(int(GetJA2Clock()), StandingSoldiers(), ObjectiveHeld());
}

void SamplePlayerTurn()
{
	if (!g_recorder.started() || g_recorder.finished()) return;
	g_recorder.NoteRound();

	// A break is the AI's own verdict: the soldier it rates HOPELESS is the one that
	// decides the fight is lost and runs. Sampling at the player's turn catches every
	// AI soldier's decision of the round (enemy and militia alike).
	int const ms = int(GetJA2Clock());
	FOR_EACH_SOLDIER(s)
	{
		if (!s->bInSector || s->bLife <= 0) continue;
		Side side;
		if (!SideOf(*s, side)) continue;
		if (CalcMorale(s) != MORALE_HOPELESS) continue;
		g_recorder.NoteBreak(side, s->ubID, ms);
	}
}

void SetObjective(int const grid, Side const side)
{
	if (g_recorder.started() && !g_recorder.finished())
	{
		g_recorder.SetObjective(grid, side);
	}
	else
	{
		// Declared before combat started (or after the last battle): the next battle
		// adopts it.
		g_pendingObjectiveGrid = grid;
		g_pendingObjectiveSide = side;
	}
}

Report const& Current()
{
	// A report read while the fight runs is a live snapshot: who is standing now, and
	// whether the objective is held now. Frozen once combat ends.
	if (g_recorder.started() && !g_recorder.finished())
	{
		g_recorder.SnapshotLiving(StandingSoldiers());
		g_recorder.SetObjectiveHeld(ObjectiveHeld());
	}
	return g_recorder.report();
}

bool HasReport()
{
	return g_recorder.started();
}

}
