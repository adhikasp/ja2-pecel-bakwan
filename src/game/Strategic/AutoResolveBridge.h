#pragma once
// What the native auto-resolve screen (NativeUI/AutoResolveNative.cc, docs/ui/autoresolve.md) needs from the legacy
// one. The simulation, its roster and all battle rules stay in Auto_Resolve.cc; this is a read-only snapshot plus the
// commands the native buttons send, so the native and the legacy UI run exactly the same game code.
//
// The legacy battle model (AUTORESOLVE_STRUCT and the SOLDIERCELL arrays) is file-local to Auto_Resolve.cc. A native
// screen in another translation unit can only see the snapshot below; nothing here uses RmlUi.

#include <string>
#include <vector>

namespace AutoResolveBridge
{
	enum BattleState
	{
		InProgress,
		Victory,
		Defeat,
		Retreat,
		Surrendered,
		Captured
	};

	enum Side { Merc, Militia, Enemy };

	/** One participant card. */
	struct Cell
	{
		int  side = 0;        // Side
		int  index = 0;       // index within its side (usable with RetreatMerc)
		bool clickable = false; // a player merc that can still be ordered to retreat
		bool dead = false, unconscious = false, hit = false, bleeding = false;
		bool robot = false, epc = false, leader = false;
		bool retreating = false, retreated = false;
		std::string name;         // nickname for mercs, the soldier name otherwise
		std::string face;         // "face-<n>" for mercs, empty otherwise
		std::string icon;         // design-system icon for militia/enemies
		std::string health;       // health text (or the name when dead)
		std::string health_class; // "ok" | "warn" | "danger" | "dead"
		std::string status;       // "Retreating" / "Retreated" (localized)
		int hp = 0, en = 0, mor = 0; // 0..100 for the bars (mercs)
	};

	/** Everything the native screen draws and every state it needs, read from the live battle. */
	struct View
	{
		bool active = false;
		int  state = InProgress; // BattleState
		std::string header, sector, forces, forces_class;
		std::string result, result_class, capture_text, time_text, surrender_text;
		bool surrender_pending = false;
		bool paused = false, playing = false, fast = false, finished = false;
		bool show_speed = false, show_retreat = false, can_retreat = false;
		bool show_bandage = false, can_bandage = false, show_done = false, won = false;
		int  alive_mercs = 0, alive_militia = 0, alive_enemies = 0;
		std::vector<Cell> mercs, militia, enemies;
	};

	/** The battle now, or active == false when there is none. */
	View GetView();

	// ---- commands (identical to the legacy buttons) ------------------------------------------------
	void Pause();
	void Play();
	void Fast();
	void Finish();
	/** Retreats every merc still fighting (the Retreat button). */
	void RetreatAll();
	/** Retreats merc @a index (a click on the merc's cell); no-op for an invalid/already-retreating merc. */
	void RetreatMerc(int index);
	void Bandage();
	void Done();
	void AcceptSurrender();
	void RejectSurrender();
}
