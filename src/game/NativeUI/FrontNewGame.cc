// The native new-game settings screen (docs/ui/newgame.md): the four GIO choices as option cards, a summary, and the
// legacy confirmation chain (Iron Man / Dead is Dead warning, difficulty, Dead is Dead save name).
#include "NativeUIRuntime.h"
#include "ViewModel.h"

#include "Cursor_Control.h"
#include "GameSettings.h"
#include "Input.h"
#include "Intro.h"
#include "JAScreens.h"
#include "MainMenuScreen.h"
#include "MessageBoxScreen.h"
#include "Music_Control.h"
#include "Options_Screen.h"
#include "SaveLoadScreen.h"
#include "Sound_Control.h"
#include "Text.h"
#include "Video.h"

#include <algorithm>
#include <optional>

namespace NativeUI
{

class NewGameViewModel final : public ViewModel
{
public:
	NewGameViewModel() : ViewModel("newgame", TOPIC_SETTINGS)
	{
		Command("difficulty", [this](Args const& a) { if (!a.empty()) { difficulty = std::clamp(std::atoi(a[0].c_str()), 1, 3); Click(); } });
		Command("saving", [this](Args const& a) { if (!a.empty()) { saving = std::clamp(std::atoi(a[0].c_str()), 0, 2); Click(); } });
		Command("style", [this](Args const& a) { if (!a.empty()) { sciFi = a[0] == "1"; Click(); } });
		Command("guns", [this](Args const& a) { if (!a.empty()) { gunNut = a[0] == "1"; Click(); } });
		for (char const* c : { "start", "cancel" })
		{
			std::string const cmd = c;
			Command(cmd, [this, cmd](Args const&) { if (onAction) onAction(cmd); });
		}
	}

	void Load()
	{
		// defaults: the last settings (EnterGIOScreen)
		difficulty = std::clamp(int(gGameOptions.ubDifficultyLevel), 1, 3);
		saving = std::clamp(int(gGameOptions.ubGameSaveMode), 0, 2);
		sciFi = gGameOptions.fSciFi;
		gunNut = gGameOptions.fGunNut;
		auto g = [](int i) { return gzGIOScreenText[i].to_std_string(); };
		t = { { "title", g(GIO_INITIAL_GAME_SETTINGS) }, { "kicker", Str("newgame.kicker") }, { "dif", g(GIO_DIF_LEVEL_TEXT) },
			{ "novice", g(GIO_EASY_TEXT) }, { "experienced", g(GIO_MEDIUM_TEXT) }, { "expert", g(GIO_HARD_TEXT) },
			{ "extra", g(GIO_GAME_SAVE_STYLE_TEXT) }, { "anytime", g(GIO_SAVE_ANYWHERE_TEXT) }, { "ironman", g(GIO_IRON_MAN_TEXT) },
			{ "did", g(GIO_DEAD_IS_DEAD_TEXT) }, { "style", g(GIO_GAME_STYLE_TEXT) }, { "realistic", g(GIO_REALISTIC_TEXT) },
			{ "scifi", g(GIO_SCI_FI_TEXT) }, { "guns", g(GIO_GUN_OPTIONS_TEXT) }, { "normal", g(GIO_REDUCED_GUNS_TEXT) },
			{ "tons", g(GIO_GUN_NUT_TEXT) }, { "ok", g(GIO_OK_TEXT) }, { "cancel", g(GIO_CANCEL_TEXT) },
			{ "d_novice", Str("newgame.desc.novice") }, { "d_experienced", Str("newgame.desc.experienced") },
			{ "d_expert", Str("newgame.desc.expert") }, { "d_anytime", Str("newgame.desc.save_anytime") },
			{ "d_ironman", zNewTacticalMessages[TCTL_MSG__CANNOT_SAVE_DURING_COMBAT].to_std_string() },
			{ "d_did", zNewTacticalMessages[TCTL_MSG__CANNOT_LOAD_PREVIOUS_SAVE].to_std_string() },
			{ "d_realistic", Str("newgame.desc.realistic") }, { "d_scifi", Str("newgame.desc.scifi") },
			{ "d_normal", Str("newgame.desc.normal_guns") }, { "d_tons", Str("newgame.desc.tons_of_guns") },
			{ "start", Str("newgame.start") }, { "summary", Str("newgame.summary") }, { "fixed", Str("newgame.fixed") },
			{ "next", Str("newgame.next") }, { "next_group", Str("newgame.next_group") }, { "choose", Str("newgame.choose") },
			{ "s_difficulty", Str("options.difficulty") }, { "s_saving", Str("options.saving") }, { "s_style", Str("options.style") },
			{ "s_guns", Str("options.guns") } };
		Changed();
	}

	void Describe(Fields& f) override
	{
		f.Field("difficulty", difficulty); f.Field("saving", saving); f.Field("scifi", sciFi); f.Field("gun_nut", gunNut);
		for (auto& [k, v] : t) f.Field(("t_" + k).c_str(), v);
	}

	void Click()
	{
		PlayJA2Sample(BIG_SWITCH3_IN, BTNVOLUME, 1, MIDDLEPAN);
		Changed();
	}

	/** DoneFadeOutForExitGameInitOptionScreen */
	void Apply() const
	{
		gGameOptions.fGunNut = gunNut;
		gGameOptions.fSciFi = sciFi;
		gGameOptions.ubDifficultyLevel = UINT8(difficulty);
		gGameOptions.ubGameSaveMode = UINT8(saving);
	}

	int difficulty = 2, saving = 0;
	bool sciFi = false, gunNut = false;
	std::map<std::string, std::string> t;
	std::function<void(std::string const&)> onAction;
};


namespace
{
	enum class Step { None, SaveModeWarning, Difficulty, DeadIsDeadName };
	Step g_step = Step::None;
	bool g_answered = false, g_yes = false;

	void Answer(MessageBoxReturnValue const r)
	{
		g_answered = true;
		g_yes = r == MSG_BOX_RETURN_YES || r == MSG_BOX_RETURN_OK;
	}

	class NewGameScreen final : public Screen
	{
	public:
		void Enter() override
		{
			g_step = Step::None;
			g_answered = false;
			m_vm.Load();
			m_vm.onAction = [this](std::string const& a) { Action(a); };
			m_binding.emplace(Context(), m_vm);
			m_doc = LoadDocument("screens/newgame.rml");
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
			SetCompact(m_doc, "newgame");
		}

		ScreenID Handle() override
		{
			if (g_answered)
			{
				g_answered = false;
				Next(g_yes);
			}
			InputAtom e;
			while (DequeueEvent(&e))
			{
				bool const used = ProcessKey(e);
				// Esc and Enter act on release, so that the release does not reach the next screen or message box
				if (e.usEvent == KEY_UP && e.usParam == SDLK_ESCAPE) { Action("cancel"); continue; }
				if (e.usEvent == KEY_UP && (e.usParam == SDLK_RETURN || e.usParam == SDLK_KP_ENTER)) { Action("start"); continue; }
				(void)used;
			}
			SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR);
			return m_next;
		}

		void Resized() override { SetCompact(m_doc, "newgame"); }

		void Exit() override
		{
			CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
			if (m_next != MAINMENU_SCREEN) SetMusicMode(MUSIC_NONE); // ExitGIOScreen when starting
		}

	private:
		void Ask(Step const s, ST::string const& text, MessageBoxFlags const flags)
		{
			g_step = s;
			g_answered = false;
			DoMessageBox(MSG_BOX_BASIC_STYLE, text, GAME_INIT_OPTIONS_SCREEN, flags, Answer, nullptr);
		}

		void Action(std::string const& a)
		{
			if (m_next != GAME_INIT_OPTIONS_SCREEN || g_step != Step::None) return;
			if (a == "cancel")
			{
				m_next = MAINMENU_SCREEN;
			}
			else if (a == "start")
			{
				// BtnGIODoneCallback: the save mode warning first, then the difficulty
				if (m_vm.saving == DIF_IRON_MAN) Ask(Step::SaveModeWarning, str_iron_man_mode_warning, MSG_BOX_FLAG_YESNO);
				else if (m_vm.saving == DIF_DEAD_IS_DEAD) Ask(Step::SaveModeWarning, str_dead_is_dead_mode_warning, MSG_BOX_FLAG_YESNO);
				else Ask(Step::Difficulty, zGioDifConfirmText[m_vm.difficulty - 1], MSG_BOX_FLAG_YESNO);
			}
		}

		void Next(bool const yes)
		{
			Step const s = g_step;
			g_step = Step::None;
			switch (s)
			{
				case Step::SaveModeWarning:
					if (!yes) { m_vm.saving = DIF_CAN_SAVE; m_vm.Changed(); return; } // legacy: back to Save Anytime
					Ask(Step::Difficulty, zGioDifConfirmText[m_vm.difficulty - 1], MSG_BOX_FLAG_YESNO);
					return;
				case Step::Difficulty:
					if (!yes) return;
					if (m_vm.saving == DIF_DEAD_IS_DEAD) { Ask(Step::DeadIsDeadName, str_dead_is_dead_mode_enter_name, MSG_BOX_FLAG_OK); return; }
					Start();
					return;
				case Step::DeadIsDeadName:
					// ConfirmGioDeadIsDeadGoToSaveMessageBoxCallBack: the save screen knows it is a new game
					guiPreviousOptionScreen = GAME_INIT_OPTIONS_SCREEN;
					Start();
					return;
				default:
					return;
			}
		}

		void Start()
		{
			m_vm.Apply();
			SetIntroType(INTRO_BEGINING);
			m_next = m_vm.saving == DIF_DEAD_IS_DEAD ? SAVE_LOAD_SCREEN : INTRO_SCREEN;
			if (m_next == SAVE_LOAD_SCREEN) gfSaveGame = TRUE;
			ClearMainMenu();
		}

		NewGameViewModel m_vm;
		std::optional<Binding> m_binding;
		Rml::ElementDocument* m_doc = nullptr;
		ScreenID m_next = GAME_INIT_OPTIONS_SCREEN;
	};
}

std::unique_ptr<Screen> CreateNewGameScreen()
{
	return std::make_unique<NewGameScreen>();
}

}
