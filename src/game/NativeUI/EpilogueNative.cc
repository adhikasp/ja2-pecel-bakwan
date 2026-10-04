// The native victory epilogue (docs/ui/epilogue.md): the campaign's last page before the credits — how long the
// war took, what it cost and who came home. It reads the campaign once, when it opens, and restarts it when it
// leaves (LeaveEpilogue), exactly where the ending chain used to.
#include "EpilogueViewModel.h"
#include "NativeImages.h"
#include "NativeUIRuntime.h"

#include "Campaign.h"
#include "Epilogue.h"
#include "Game_Clock.h"
#include "Input.h"
#include "Overhead.h"
#include "Soldier_Profile.h"
#include "StrategicMap.h"
#include "Strategic_Status.h"
#include "UiCore.h"

#include <string_theory/format>

#include <algorithm>
#include <memory>
#include <optional>

namespace NativeUI
{

EpilogueViewModel::EpilogueViewModel() : ViewModel("epilogue", 0)
{
	Command("continue", [this](Args const&) { if (onContinue) onContinue(); });
	Command("menu", [this](Args const&) { if (onMenu) onMenu(); });
}

void EpilogueViewModel::Describe(Fields& f)
{
	f.Field("title", title);
	f.Field("sub", sub);
	f.Field("text", text);
	f.Rows("stats", stats);
	f.Rows("survivors", survivors);
	f.Rows("fallen", fallen);
	f.Field("has_survivors", hasSurvivors);
	f.Field("has_fallen", hasFallen);
	f.Field("days", days);
	f.Field("sectors", sectors);
	f.Field("killed", killed);
	f.Field("served", served);
	f.Field("fell", fell);
	f.Field("effort", effort);
	f.Field("effort_pct", effortPct);
	f.Field("effort_label", effortLabel);
	f.Field("home_label", homeLabel);
	f.Field("fallen_label", fallenLabel);
	f.Field("fallen_badge", fallenBadge);
	f.Field("continue_label", continueLabel);
	f.Field("menu_label", menuLabel);
}

void EpilogueViewModel::Load()
{
	EpilogueModel::Raw raw;
	raw.days = int(GetWorldDay());
	for (int y = 1; y <= 16; ++y)
	{
		for (int x = 1; x <= 16; ++x)
		{
			if (!StrategicMap[SGPSector(x, y, 0).AsStrategicIndex()].fEnemyControlled) ++raw.sectors;
		}
	}
	raw.killedAdmin = gStrategicStatus.usEnemiesKilled[ENEMY_KILLED_TOTAL][ENEMY_RANK_ADMIN];
	raw.killedTroop = gStrategicStatus.usEnemiesKilled[ENEMY_KILLED_TOTAL][ENEMY_RANK_TROOP];
	raw.killedElite = gStrategicStatus.usEnemiesKilled[ENEMY_KILLED_TOTAL][ENEMY_RANK_ELITE];
	// the war-effort estimate needs a campaign behind it (it reads the mines); a screen opened without one shows 0
	raw.effort = gTacticalStatus.fHasAGameBeenStarted ? CurrentPlayerProgressPercentage() : 0;

	survivors.clear();
	fallen.clear();
	for (int p = 0; p < NUM_PROFILES; ++p)
	{
		MERCPROFILESTRUCT const& prof = gMercProfiles[p];
		if (!(prof.ubMiscFlags & PROFILE_MISC_FLAG_RECRUITED)) continue;
		bool const fell = prof.bMercStatus == MERC_IS_DEAD;
		EpilogueChipRow chip;
		chip.profile = p;
		chip.name = !prof.zNickname.empty() ? prof.zNickname.to_std_string() : prof.zName.to_std_string();
		chip.face = "face-" + std::to_string(p);
		chip.fate = Str(fell ? "epilogue.fate_fallen" : "epilogue.fate_home");
		++raw.served;
		if (fell) ++raw.fell;
		(fell ? fallen : survivors).push_back(std::move(chip));
	}

	stats.clear();
	for (EpilogueModel::Stat const& s : EpilogueModel::Stats(raw))
	{
		EpilogueStatRow row;
		row.key   = s.key;
		row.value = s.value;
		row.label = Str("epilogue." + s.key);
		if (s.key == "sectors") row.sub = ST::format(Str("epilogue.sectors_sub").c_str(), s.subValue).to_std_string();
		else if (s.key == "killed") row.sub = ST::format(Str("epilogue.killed_sub").c_str(),
			raw.killedAdmin, raw.killedTroop, raw.killedElite).to_std_string();
		else if (s.key == "served" && s.subValue > 0)
			row.sub = ST::format(Str("epilogue.served_sub").c_str(), s.subValue).to_std_string();
		stats.push_back(std::move(row));
	}

	effort = EpilogueModel::EffortPercent(raw);
	days = raw.days;
	sectors = raw.sectors;
	killed = EpilogueModel::KilledTotal(raw);
	served = raw.served;
	fell = raw.fell;
	effortPct     = ST::format("{}%", effort).to_std_string();
	effortLabel   = Str("epilogue.effort");
	homeLabel     = Str("epilogue.home");
	fallenLabel   = Str("epilogue.fallen");
	fallenBadge   = Str("epilogue.fallen_badge");
	hasSurvivors  = !survivors.empty();
	hasFallen     = !fallen.empty();
	continueLabel = Str("epilogue.continue");
	menuLabel     = Str("epilogue.menu");
	title = Str("epilogue.title");
	sub   = Str("epilogue.sub");
	text  = Str("epilogue.text");
	Changed();
}


namespace
{
	class EpilogueScreen final : public Screen
	{
	public:
		void Enter() override
		{
			nui::SetImageProvider(ProvideGameImage);
			m_vm.Load(); // the campaign is still the one the ending chain left behind
			m_vm.onContinue = [this] { Leave(CREDIT_SCREEN); };
			m_vm.onMenu     = [this] { Leave(MAINMENU_SCREEN); };
			m_binding.emplace(Context(), m_vm);
			m_doc = LoadDocument("screens/epilogue.rml");
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
			Measure();
			if (Rml::Element* fill = m_doc->GetElementById("epilogue.effort-fill"))
			{
				fill->SetProperty(Rml::PropertyId::Width, Rml::Property(float(m_vm.effort), Rml::Unit::PERCENT));
			}
		}

		ScreenID Handle() override
		{
			InputAtom e;
			while (DequeueEvent(&e))
			{
				bool const used = ProcessKey(e);
				if (e.usEvent == KEY_UP && e.usParam == SDLK_ESCAPE) Leave(MAINMENU_SCREEN); // on release, like the front end
				if (used || e.usEvent != KEY_UP) continue;
				if (e.usParam == SDLK_RETURN || e.usParam == SDLK_KP_ENTER) Leave(CREDIT_SCREEN);
			}
			return m_done ? m_next : EPILOGUE_SCREEN;
		}

		void Exit() override
		{
			CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
		}

		void Resized() override { Measure(); }

	private:
		/** Narrow layouts (big UI scales, small windows) shrink the roster chips; the page scrolls when it must. */
		void Measure()
		{
			SetCompact(m_doc, "epilogue", 1500);
		}

		void Leave(ScreenID const next)
		{
			if (m_done) return;
			m_next = LeaveEpilogue(next); // restarts the campaign, as the ending chain always did
			m_done = true;
		}

		EpilogueViewModel m_vm;
		std::optional<Binding> m_binding;
		Rml::ElementDocument* m_doc = nullptr;
		bool     m_done = false;
		ScreenID m_next = EPILOGUE_SCREEN;
	};
}

std::unique_ptr<Screen> CreateEpilogueScreen()
{
	return std::make_unique<EpilogueScreen>();
}

namespace { bool const g_registered = (RegisterViewModelFactory("epilogue", [] {
	auto vm = std::make_unique<EpilogueViewModel>();
	vm->Load();
	return std::unique_ptr<ViewModel>(std::move(vm));
}), true); }

}
