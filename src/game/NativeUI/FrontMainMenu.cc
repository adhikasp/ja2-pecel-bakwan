// The native main menu (docs/ui/mainmenu.md): the original art from the player's data (uplifted at runtime) behind
// a typeset menu, with Continue (the legacy Alt+click "load the last save") and a card about the newest save.
#include "FrontEndViewModels.h"
#include "NativeImages.h"
#include "NativeUIRuntime.h"
#include "GameViewModels.h"

#include "Cursor_Control.h"
#include "GameSettings.h"
#include "GameVersion.h"
#include "Input.h"
#include "JAScreens.h"
#include "MainMenuScreen.h"
#include "Music_Control.h"
#include "Options_Screen.h"
#include "SaveLoadGame.h"
#include "SaveLoadScreen.h"
#include "SGP.h"
#include "Text.h"
#include "Video.h"

#include <string_theory/format>

#include <ctime>
#include <optional>

namespace NativeUI
{

std::string FormatSavedAt(double const modified, double const now)
{
	if (modified <= 0) return {};
	time_t const t = time_t(modified), n = time_t(now);
	std::tm lt{}, ln{};
#ifdef _WIN32
	localtime_s(&lt, &t);
	localtime_s(&ln, &n);
#else
	localtime_r(&t, &lt);
	localtime_r(&n, &ln);
#endif
	char hm[16];
	std::strftime(hm, sizeof hm, "%H:%M", &lt);
	// calendar days between the two
	std::tm a = lt, b = ln;
	a.tm_hour = b.tm_hour = 12; a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0;
	int const days = int(std::lround(std::difftime(std::mktime(&b), std::mktime(&a)) / 86400.0));
	if (days <= 0) return std::string("Today ") + hm;
	if (days == 1) return std::string("Yesterday ") + hm;
	if (days < 7) return std::to_string(days) + " days ago";
	char d[32];
	std::strftime(d, sizeof d, "%d %b %Y", &lt);
	return d;
}

SaveRow MakeSaveRow(SaveGameInfo const& s, int const index)
{
	SAVED_GAME_HEADER const& h = s.header();
	SaveRow r;
	r.index   = index;
	r.file    = s.name().to_std_string();
	r.name    = h.sSavedGameDesc.to_std_string();
	r.when    = FormatWhen(h.uiDay, h.ubHour, h.ubMin);
	r.day     = int(h.uiDay);
	r.minutes = int(h.uiDay) * 1440 + h.ubHour * 60 + h.ubMin;
	r.sector  = SaveLoadSectorText(h).to_std_string();
	r.team    = h.ubNumOfMercsOnPlayersTeam;
	r.mercs   = std::to_string(r.team);
	r.balance = h.iCurrentBalance;
	r.money   = FormatMoney(h.iCurrentBalance);
	r.modified = SaveLoadModifiedTime(s.name());
	r.saved   = FormatSavedAt(r.modified, double(std::time(nullptr)));
	r.quick   = IsQuickSaveName(s.name());
	r.autoSave = IsAutoSaveName(s.name());
	GAME_OPTIONS const& o = h.sInitialGameOptions;
	int const d = std::clamp(int(o.ubDifficultyLevel), 1, 3);
	r.difficulty = gzGIOScreenText[GIO_EASY_TEXT + d - 1].to_std_string();
	r.ironman    = o.ubGameSaveMode == DIF_IRON_MAN;
	r.deadIsDead = o.ubGameSaveMode == DIF_DEAD_IS_DEAD;
	r.saving = gzGIOScreenText[r.ironman ? GIO_IRON_MAN_TEXT : r.deadIsDead ? GIO_DEAD_IS_DEAD_TEXT : GIO_SAVE_ANYWHERE_TEXT].to_std_string();
	r.gunsStyle = (o.fGunNut ? zSaveLoadText[SLG_ADDITIONAL_GUNS] : zSaveLoadText[SLG_NORMAL_GUNS]).to_std_string() + " · " +
		(o.fSciFi ? zSaveLoadText[SLG_SCIFI] : zSaveLoadText[SLG_REALISTIC]).to_std_string();
	if (s.mods().empty())
	{
		r.mods = zSaveLoadText[SLG_NO_MODS].to_std_string();
	}
	else
	{
		for (auto const& [mod, version] : s.mods())
		{
			if (!r.mods.empty()) r.mods += ", ";
			r.mods += (mod + " (" + version + ")").to_std_string();
		}
	}
	r.thumb = "save-thumb-" + r.file + "@" + std::to_string(static_cast<long long>(r.modified));
	return r;
}

MainMenuViewModel::MainMenuViewModel() : ViewModel("mainmenu", TOPIC_SETTINGS)
{
	for (char const* c : { "continue", "new", "load", "options", "credits", "quit" })
	{
		std::string const cmd = c;
		Command(cmd, [this, cmd](Args const&) {
			if ((cmd == "continue" || cmd == "load") && !hasSaves) return;
			if (onPick) onPick(cmd);
		});
	}
}

void MainMenuViewModel::Refresh()
{
	kicker   = Str("mainmenu.kicker");
	title    = Str("mainmenu.title");
	subtitle = Str("mainmenu.subtitle");
	version  = ST::format("{}", g_version_label).to_std_string();
	copyright = gzCopyrightText.to_std_string();
	lContinue = Str("mainmenu.continue"); lNew = Str("mainmenu.new"); lLoad = Str("mainmenu.load");
	lOptions = Str("mainmenu.options"); lCredits = Str("mainmenu.credits"); lQuit = Str("mainmenu.quit");
	lLast = Str("mainmenu.last_save"); lContinueLoads = Str("mainmenu.continue_loads");
	lTime = Str("mainmenu.game_time"); lSector = Str("mainmenu.sector"); lTeam = Str("mainmenu.team");
	lBalance = Str("mainmenu.balance"); lGame = Str("mainmenu.game");
	lChoose = Str("mainmenu.choose"); lSelect = Str("mainmenu.select"); lQuitHint = Str("mainmenu.quit_hint");
	newMeta = Str("mainmenu.new_meta"); optionsMeta = Str("mainmenu.options_meta");
	creditsMeta = Str("mainmenu.credits_meta"); quitMeta = Str("mainmenu.quit_meta");

	std::vector<SaveGameInfo> const saves = SaveLoadListSaves(false);
	saveCount = int(saves.size());
	hasSaves  = saveCount > 0;
	newest.clear();
	if (!hasSaves)
	{
		continueMeta = loadMeta = Str("mainmenu.no_saves");
		lastName = lastWhen = lastSector = lastTeam = lastMoney = lastGame = "";
		return;
	}
	SaveRow const r = MakeSaveRow(saves.front(), 0);
	newest = r.file;
	continueMeta = ST::format("{} · {}", r.when, r.sector).to_std_string();
	loadMeta = saveCount == 1 ? Str("mainmenu.one_save") : ST::format(Str("mainmenu.saves").c_str(), saveCount).to_std_string();
	lastName = r.name;
	lastWhen = r.when;
	lastSector = r.sector;
	lastTeam = r.team == 1 ? ("1 " + MercAccountText[MERC_ACCOUNT_MERC].to_std_string()) : (r.mercs + " " + pMessageStrings[MSG_MERCS].to_std_string());
	lastMoney = r.money;
	lastGame = r.difficulty + " · " + r.saving + " · " + r.gunsStyle;
}

void MainMenuViewModel::Describe(Fields& f)
{
	f.Field("has_saves", hasSaves);
	f.Field("save_count", saveCount);
	f.Field("newest", newest);
	f.Field("kicker", kicker); f.Field("title", title); f.Field("subtitle", subtitle);
	f.Field("version", version); f.Field("copyright", copyright);
	f.Field("l_continue", lContinue); f.Field("l_new", lNew); f.Field("l_load", lLoad); f.Field("l_options", lOptions);
	f.Field("l_credits", lCredits); f.Field("l_quit", lQuit); f.Field("l_last", lLast); f.Field("l_continue_loads", lContinueLoads);
	f.Field("l_time", lTime); f.Field("l_sector", lSector); f.Field("l_team", lTeam); f.Field("l_balance", lBalance);
	f.Field("l_game", lGame); f.Field("l_choose", lChoose); f.Field("l_select", lSelect); f.Field("l_quit_hint", lQuitHint);
	f.Field("continue_meta", continueMeta); f.Field("new_meta", newMeta); f.Field("load_meta", loadMeta);
	f.Field("options_meta", optionsMeta); f.Field("credits_meta", creditsMeta); f.Field("quit_meta", quitMeta);
	f.Field("last_name", lastName); f.Field("last_when", lastWhen); f.Field("last_sector", lastSector);
	f.Field("last_team", lastTeam); f.Field("last_money", lastMoney); f.Field("last_game", lastGame);
}


namespace
{
	class MainMenuScreen final : public Screen
	{
	public:
		void Enter() override
		{
			// what InitMainMenu does besides its buttons
			gfLoadGameUponEntry = FALSE;
			InitGameOptions();
			// as InitMainMenu: the release of the key that brought us here (Esc) must not quit
			DequeueAllInputEvents();
			RemoveLegacyMainMenu();
			m_vm.onPick = [this](std::string const& what) { Pick(what); };
			m_vm.Update(true);
			m_binding.emplace(Context(), m_vm);
			m_doc = LoadDocument("screens/mainmenu.rml");
			m_shown = false;
			gfNativeMainMenuActive = true;
		}

		ScreenID Handle() override
		{
			// the splash and its fade come first, as in the legacy menu
			if (HandleMainMenuSplash())
			{
				if (m_doc->IsVisible()) m_doc->Hide();
				DequeueAllInputEvents();
				return MAINMENU_SCREEN;
			}
			if (!m_shown)
			{
				m_shown = true;
				SetMusicMode(MUSIC_MAIN_MENU);
				m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
				Resized();
				if (Rml::Element* e = m_doc->GetElementById(m_vm.hasSaves ? "mainmenu.continue" : "mainmenu.new")) e->Focus(false);
			}

			InputAtom e;
			while (DequeueEvent(&e))
			{
				bool const used = ProcessKey(e);
				if (e.usEvent != KEY_UP) continue;
				bool const alt = e.usKeyState & ALT_DOWN, ctrl = e.usKeyState & CTRL_DOWN;
				switch (e.usParam)
				{
					case SDLK_ESCAPE: Pick("quit"); break; // legacy: on release, no question asked
					case SDLK_Q:      if (ctrl) Pick("quit"); break;
					case SDLK_C:      m_vm.Invoke(alt ? "continue" : "continue"); break;
					case SDLK_N:      Pick("new"); break;
					case SDLK_L:      m_vm.Invoke("load"); break;
					case SDLK_O:      Pick("options"); break;
					case SDLK_S:      Pick("credits"); break;
					default: (void)used; break;
				}
			}
			SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR);
			return m_next;
		}

		void Exit() override
		{
			gfNativeMainMenuActive = false;
			CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
		}

		void Resized() override
		{
			if (!m_doc) return;
			float const widthDp = Context()->GetDimensions().x / std::max(0.01f, DpScale());
			if (Rml::Element* root = m_doc->GetElementById("mainmenu")) root->SetClass("compact", widthDp < 1500);
		}

	private:
		void Pick(std::string const& what)
		{
			if (m_next != MAINMENU_SCREEN) return;
			if (what == "continue")
			{
				// the legacy Alt+C: the save/load screen loads the newest save as soon as it is entered
				guiPreviousOptionScreen = MAINMENU_SCREEN;
				SaveLoadArmLoadUponEntry(m_vm.newest);
				m_next = SAVE_LOAD_SCREEN;
			}
			else if (what == "new")
			{
				m_next = GAME_INIT_OPTIONS_SCREEN;
			}
			else if (what == "load")
			{
				guiPreviousOptionScreen = MAINMENU_SCREEN;
				gfSaveGame = FALSE;
				m_next = SAVE_LOAD_SCREEN;
			}
			else if (what == "options")
			{
				guiPreviousOptionScreen = MAINMENU_SCREEN;
				m_next = OPTIONS_SCREEN;
			}
			else if (what == "credits")
			{
				m_next = CREDIT_SCREEN;
			}
			else if (what == "quit")
			{
				requestGameExit();
			}
		}

		MainMenuViewModel m_vm;
		std::optional<Binding> m_binding;
		Rml::ElementDocument* m_doc = nullptr;
		bool m_shown = false;
		ScreenID m_next = MAINMENU_SCREEN;
	};
}

namespace
{
	bool const g_registered = (RegisterViewModelFactory("mainmenu", [] { return std::unique_ptr<ViewModel>(std::make_unique<MainMenuViewModel>()); }), true);
}

std::unique_ptr<Screen> CreateMainMenuScreen()
{
	return std::make_unique<MainMenuScreen>();
}

}
