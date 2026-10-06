#pragma once

#include "JA2Types.h"

#include <string>
#include <vector>

struct SOLDIERTYPE;

/** @file
 * The battle report: what happened in a fight, as data (issue #59,
 * docs/plan/ai-evaluation.md). The recorder starts when combat starts, listens to the
 * battle's own events while it runs, samples the AI's morale verdicts at each player
 * turn, and freezes when combat ends. Scenarios read it back with
 * `ja2.battleReport()`; the AI evaluation matrix (`tools/ai_eval.py`) turns a set of
 * battles into a table.
 *
 * The core (`Recorder`) is pure: it takes sides, life totals and times, and knows
 * nothing about the world. The adapter below it (the free functions) is what the
 * combat code and the Lua surface call, and what maps soldiers and events onto the
 * recorder. The report is session data - it is not saved - and the next battle starts
 * a fresh one.
 */
namespace BattleReport
{
	/** The two sides a battle report covers. The player's side is the mercs and the
	 * militia; the enemy's side is the enemy team. Other teams are not counted. */
	enum class Side { Player, Enemy };

	/** "player" / "enemy", the name the Lua surface and the report use. */
	char const* SideName(Side side);

	/** One soldier as the report counts him: which side, and his life. */
	struct SoldierState
	{
		Side side;
		int  life    = 0;
		int  lifeMax = 0;
	};

	/** What one side did and lost. */
	struct Tally
	{
		int soldiers  = 0;  // soldiers of the side in the sector when the battle started
		int alive     = 0;  // standing when it ended
		int dead      = 0;  // lost during the battle, counted from the death events
		int wounded   = 0;  // alive, below full life
		int lifeStart = 0;
		int lifeEnd   = 0;
		int shots     = 0;  // trigger pulls (rounds that left the barrel)
		int hits      = 0;  // trigger pulls whose roll connected
		int impacts   = 0;  // rounds that arrived on the other side
		int damageDealt = 0;
		int damageTaken = 0;
		int breaks    = 0;  // soldiers the AI rated HOPELESS, first time only
	};

	/** The battle, from combat start to combat end. Times are engine milliseconds
	 * (GetJA2Clock, the clock that runs during turn-based combat; the world clock is
	 * paused then). */
	struct Report
	{
		bool started  = false;
		bool finished = false;
		std::string sector;
		int startMs = 0;
		int endMs   = 0;
		int rounds  = 0;
		std::string outcome;  // player | enemy | draw | unresolved
		std::string endedBy;  // wiped_out | lull | in_progress
		int contactMs       = -1;  // the first shot, -1 if none
		int firstCasualtyMs = -1;
		int firstBreakMs    = -1;
		int disengageMs     = -1;  // the first break, or the end of combat
		int  objectiveGrid = -1;   // -1 when the scenario declared none
		int  objectiveSide = 0;    // a Side, valid when objectiveGrid is set
		bool objectiveHeld = false;
		Tally player;
		Tally enemy;
	};

	/** The recorder. Begin / Finish bracket a battle; the Note* calls are its events;
	 * everything is ignored before a battle starts or after it is frozen. */
	class Recorder
	{
	public:
		/** Start a battle: the roster is the soldiers standing in the sector, with
		 * their sides and life. Resets whatever the last battle left. */
		void Begin(std::string sector, int ms, std::vector<SoldierState> const& roster);

		/** A trigger pull by @a shooter; @a hit is whether the roll connected. */
		void NoteShot(Side shooter, bool hit, int ms);

		/** A round from @a shooter arriving on @a target. */
		void NoteImpact(Side shooter, Side target, int damage);

		/** A soldier lost. @a ms is the engine time, for the first casualty. */
		void NoteDeath(Side side, int ms);

		/** A soldier the AI rates HOPELESS (his run-away verdict), by soldier id so a
		 * soldier is counted once however long he stays hopeless. */
		void NoteBreak(Side side, int soldierId, int ms);

		/** A player turn began (one round of the battle). */
		void NoteRound();

		/** Declare the battle's objective: the side that must hold @a grid. */
		void SetObjective(int grid, Side side);

		/** Freeze the battle: the soldiers still standing and whether the objective's
		 * side held it. Computes the outcome, the times and the disengage. */
		void Finish(int ms, std::vector<SoldierState> const& living, bool objectiveHeld);

		/** Refresh who is standing (alive/wounded/lifeEnd) while the battle runs, so a
		 * report read mid-fight is a live snapshot; ignored once frozen. */
		void SnapshotLiving(std::vector<SoldierState> const& living);

		/** Record whether the objective's side holds it right now; ignored once frozen. */
		void SetObjectiveHeld(bool held);

		/** Continue a battle that was frozen by a lull: a lull is a pause, not a new
		 * battle, so its tallies, times and breaks carry on. */
		void Resume();

		Report const& report() const { return report_; }
		bool started() const { return report_.started; }
		bool finished() const { return report_.finished; }

	private:
		Tally& TallyFor(Side side);
		void TallyLiving(std::vector<SoldierState> const& living);
		int MsSinceStart(int ms) const;

		Report report_;
		std::vector<int> broken_;  // soldier ids already counted, so a break is one event
	};

	// --- the world adapter ------------------------------------------------------
	// The game's side: one recorder for the battle in progress, wired to the combat
	// lifecycle and to the shot/impact/death events. The Lua surface reads it with
	// Current()/HasReport().

	/** Combat started: begin a report from the soldiers in the sector. */
	void BeginBattle();

	/** Combat ended: freeze the report, computing the outcome from who is standing. */
	void FinishBattle();

	/** The player's turn began: count the round and sample the AI's morale verdicts. */
	void SamplePlayerTurn();

	/** Declare the current (or next) battle's objective. */
	void SetObjective(int grid, Side side);

	/** The battle in progress, or the last one finished. HasReport() is false until a
	 * battle has started in this session. */
	Report const& Current();
	bool HasReport();
}
