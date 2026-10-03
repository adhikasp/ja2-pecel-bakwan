// The native auto-resolve battle panel (docs/ui/autoresolve.md, Phase 7 of docs/plan/native-modern-game.md).
//
// How it works: the legacy auto-resolve keeps running underneath (AutoResolveScreenHandle is called every frame:
// the simulation, its hotkeys and all battle-end bookkeeping are unchanged). The native document covers the legacy
// blitter panel and draws the battle from AutoResolveBridge's snapshot; the buttons call the same legacy callbacks
// the legacy buttons would, so the native and the legacy UI run exactly the same game code.
#include "AutoResolveModel.h"
#include "NativeImages.h"
#include "NativeUIRuntime.h"
#include "ViewModel.h"

#include "AutoResolveBridge.h"
#include "Auto_Resolve.h"
#include "UiCore.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace NativeUI
{

namespace
{
	struct CellRow
	{
		int i = 0;
		std::string name, face, icon, health, health_class, status, cls;
		bool dead = false, unconscious = false, bleeding = false, hit = false, leader = false;
		bool robot = false, epc = false, retreating = false, retreated = false, clickable = false;
		bool merc = false, has_face = false;
		int hp = 0, en = 0, mor = 0;
		bool operator==(CellRow const&) const = default;
		static void Describe(RowFields<CellRow>& f)
		{
			f("i", &CellRow::i)("name", &CellRow::name)("face", &CellRow::face)("icon", &CellRow::icon)
			 ("health", &CellRow::health)("health_class", &CellRow::health_class)("status", &CellRow::status)("cls", &CellRow::cls)
			 ("dead", &CellRow::dead)("unconscious", &CellRow::unconscious)("bleeding", &CellRow::bleeding)("hit", &CellRow::hit)
			 ("leader", &CellRow::leader)("robot", &CellRow::robot)("epc", &CellRow::epc)("retreating", &CellRow::retreating)
			 ("retreated", &CellRow::retreated)("clickable", &CellRow::clickable)("merc", &CellRow::merc)("has_face", &CellRow::has_face)
			 ("hp", &CellRow::hp)("en", &CellRow::en)("mor", &CellRow::mor);
		}
	};

	class AutoResolveViewModel final : public ViewModel
	{
	public:
		AutoResolveViewModel() : ViewModel("autoresolve", TOPIC_ALL)
		{
			using namespace AutoResolveBridge;
			Command("speed", [this](Args const& a) {
				if (a.empty()) return;
				if (a[0] == "pause") Pause();
				else if (a[0] == "play") Play();
				else if (a[0] == "fast") Fast();
				else if (a[0] == "finish") Finish();
				Poke();
			});
			Command("retreat", [this](Args const& a) {
				if (a.empty()) return;
				if (a[0] == "all") RetreatAll();
				else RetreatMerc(std::atoi(a[0].c_str()));
				Poke();
			});
			Command("bandage", [this](Args const&) { Bandage(); Poke(); });
			Command("done",    [this](Args const&) { Done(); Poke(); });
			Command("yes",     [this](Args const&) { AcceptSurrender(); Poke(); });
			Command("no",      [this](Args const&) { RejectSurrender(); Poke(); });
			Labels();
		}

		/** Reads the game again now (after forwarding a command) instead of at the next frame. */
		void Poke() { Refresh(); }

		void Labels()
		{
			for (char const* k : { "pause", "play", "fast", "finish", "retreat", "retreat_all", "bandage", "done", "yes", "no",
				"mercs", "militia", "enemies", "your_forces", "enemies_forces", "battle", "time", "leader", "robot", "epc" })
			{
				labels[k] = Str(std::string("ar.") + k);
			}
		}

		void Describe(Fields& f) override
		{
			f.Field("active", active);
			f.Field("header", header); f.Field("sector", sector); f.Field("forces", forces); f.Field("forces_class", forcesClass);
			f.Field("result", result); f.Field("result_class", resultClass); f.Field("capture_text", captureText);
			f.Field("time_text", timeText); f.Field("surrender_text", surrenderText);
			f.Field("surrender_pending", surrenderPending);
			f.Field("paused", paused); f.Field("playing", playing); f.Field("fast", fast); f.Field("finished", finished);
			f.Field("show_speed", showSpeed); f.Field("show_retreat", showRetreat); f.Field("can_retreat", canRetreat);
			f.Field("show_bandage", showBandage); f.Field("can_bandage", canBandage);
			f.Field("show_done", showDone); f.Field("won", won);
			f.Field("alive_mercs", aliveMercs); f.Field("alive_militia", aliveMilitia); f.Field("alive_enemies", aliveEnemies);
			f.Field("has_mercs", hasMercs); f.Field("has_militia", hasMilitia); f.Field("has_enemies", hasEnemies);
			f.Rows("mercs", mercs); f.Rows("militia", militia); f.Rows("enemies", enemies);
			for (auto& [k, v] : labels) f.Field(("l_" + k).c_str(), v);
		}

		void Refresh() override
		{
			std::string const before = Snapshot().ToJson();
			AutoResolveBridge::View const v = AutoResolveBridge::GetView();
			active = v.active;
			header = v.header; sector = v.sector; forces = v.forces; forcesClass = v.forces_class;
			result = v.result; resultClass = v.result_class; captureText = v.capture_text;
			timeText = v.time_text; surrenderText = v.surrender_text; surrenderPending = v.surrender_pending;
			paused = v.paused; playing = v.playing; fast = v.fast; finished = v.finished;
			showSpeed = v.show_speed; showRetreat = v.show_retreat; canRetreat = v.can_retreat;
			showBandage = v.show_bandage; canBandage = v.can_bandage; showDone = v.show_done; won = v.won;
			aliveMercs = v.alive_mercs; aliveMilitia = v.alive_militia; aliveEnemies = v.alive_enemies;
			hasMercs = !v.mercs.empty(); hasMilitia = !v.militia.empty(); hasEnemies = !v.enemies.empty();
			mercs   = MakeRows(v.mercs, true);
			militia = MakeRows(v.militia, false);
			enemies = MakeRows(v.enemies, false);
			if (Snapshot().ToJson() != before) Changed();
		}

	private:
		static std::vector<CellRow> MakeRows(std::vector<AutoResolveBridge::Cell> const& in, bool const merc)
		{
			std::vector<CellRow> out;
			out.reserve(in.size());
			for (AutoResolveBridge::Cell const& c : in)
			{
				CellRow r;
				r.i = c.index; r.name = c.name; r.face = c.face; r.icon = c.icon;
				r.health = c.health; r.health_class = c.health_class; r.status = c.status;
				r.dead = c.dead; r.unconscious = c.unconscious; r.bleeding = c.bleeding; r.hit = c.hit;
				r.leader = c.leader; r.robot = c.robot; r.epc = c.epc;
				r.retreating = c.retreating; r.retreated = c.retreated; r.clickable = c.clickable;
				r.merc = merc; r.has_face = !c.face.empty();
				r.hp = c.hp; r.en = c.en; r.mor = c.mor;
				r.cls = AutoResolveModel::CellClass(c.dead, c.unconscious, c.bleeding, c.hit, c.leader,
					c.robot, c.epc, c.retreating, c.retreated, c.clickable);
				out.push_back(std::move(r));
			}
			return out;
		}

		bool active = false;
		std::string header, sector, forces, forcesClass, result, resultClass, captureText, timeText, surrenderText;
		bool surrenderPending = false, paused = false, playing = false, fast = false, finished = false;
		bool showSpeed = false, showRetreat = false, canRetreat = false, showBandage = false, canBandage = false, showDone = false, won = false;
		int aliveMercs = 0, aliveMilitia = 0, aliveEnemies = 0;
		bool hasMercs = false, hasMilitia = false, hasEnemies = false;
		std::vector<CellRow> mercs, militia, enemies;
		std::map<std::string, std::string> labels;
	};

	class AutoResolveNative final : public Screen
	{
	public:
		void Enter() override
		{
			nui::SetImageProvider(ProvideGameImage);
			RegisterFrontEndImages();
			m_vm = std::make_unique<AutoResolveViewModel>();
			m_binding = std::make_unique<Binding>(Context(), *m_vm);
			m_doc = LoadDocument("screens/autoresolve.rml");
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
		}

		ScreenID Handle() override
		{
			// the legacy screen runs every frame: the simulation, its hotkeys and the battle-end bookkeeping
			ScreenID const next = AutoResolveScreenHandle();
			if (next != AUTORESOLVE_SCREEN) return next;
			m_vm->Refresh();
			return AUTORESOLVE_SCREEN;
		}

		void Exit() override
		{
			CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
			m_vm.reset();
		}

	private:
		Rml::ElementDocument* m_doc = nullptr;
		std::unique_ptr<AutoResolveViewModel> m_vm;
		std::unique_ptr<Binding> m_binding;
	};
}

std::unique_ptr<Screen> CreateAutoResolveScreen()
{
	return std::make_unique<AutoResolveNative>();
}

}
