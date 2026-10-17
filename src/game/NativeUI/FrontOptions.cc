// The native options screen (docs/ui/options.md): the 23 game toggles, the three volumes and the video settings
// (the legacy Video sub-screen folded in) on five pages, with the help text of the setting under the pointer.
#include "NativeImages.h"
#include "NativeUIRuntime.h"
#include "ViewModel.h"
#include "KeyBindings.h"

#include "Ambient_Control.h"
#include "Map_Information.h"
#include "Message.h"
#include "Timer_Control.h"
#include "ContentManager.h"
#include "Cursor_Control.h"
#include "Game_Clock.h"
#include "GameInstance.h"
#include "GameScreen.h"
#include "GameSettings.h"
#include "Gap.h"
#include "Handle_Items.h"
#include "Headless.h"
#include "Input.h"
#include "JAScreens.h"
#include "MessageBoxScreen.h"
#include "Music_Control.h"
#include "Options_Screen.h"
#include "SaveLoadScreen.h"
#include "SmokeEffects.h"
#include "Sound_Control.h"
#include "SoundMan.h"
#include "Game_Init.h"
#include "Text.h"
#include "UILayout.h"
#include "Video.h"
#include "VideoOptionsScreen.h"
#include "WorldDat.h"
#include "WorldDef.h"
#include "WorldMan.h"
#include "Directories.h"
#include "Timer.h"

#include <string_theory/format>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace NativeUI
{

struct OptionRow
{
	int opt = 0;
	std::string group, name, help, kbd;
	bool on = false;
	static void Describe(RowFields<OptionRow>& f)
	{
		f("opt", &OptionRow::opt)("group", &OptionRow::group)("name", &OptionRow::name)("help", &OptionRow::help)
		 ("kbd", &OptionRow::kbd)("on", &OptionRow::on);
	}
};

struct ChoiceRow
{
	int index = 0;
	std::string group, label, value;
	bool selected = false;
	static void Describe(RowFields<ChoiceRow>& f)
	{
		f("index", &ChoiceRow::index)("group", &ChoiceRow::group)("label", &ChoiceRow::label)("value", &ChoiceRow::value)
		 ("selected", &ChoiceRow::selected);
	}
};

struct KeyRow
{
	std::string group, name, k1, k2;
	static void Describe(RowFields<KeyRow>& f)
	{
		f("group", &KeyRow::group)("name", &KeyRow::name)("k1", &KeyRow::k1)("k2", &KeyRow::k2);
	}
};

/** One rebindable action on the Controls page (issue #48): its row. */
struct BindRow
{
	std::string group, name, label, key; // key is the effective key as printed, or the "unbound" string
	bool bound = false, changed = false, lost = false;
	static void Describe(RowFields<BindRow>& f)
	{
		f("group", &BindRow::group)("name", &BindRow::name)("label", &BindRow::label)("key", &BindRow::key);
		f("bound", &BindRow::bound)("changed", &BindRow::changed)("lost", &BindRow::lost);
	}
};

namespace
{
	// where each toggle goes (docs/ui/options.md §2), in the order shown
	struct Placement { int opt; char const* group; char const* kbd; };
	Placement const PLACEMENT[] = {
		{ TOPTION_SHOW_MISSES, "combat", "" }, { TOPTION_HIDE_BULLETS, "combat", "" }, { TOPTION_TRACKING_MODE, "combat", "" },
		{ TOPTION_RTCONFIRM, "combat", "" }, { TOPTION_ALWAYS_SHOW_MOVEMENT_PATH, "combat", "Shift" },
		{ TOPTION_SLEEPWAKE_NOTIFICATION, "campaign", "" }, { TOPTION_USE_METRIC_SYSTEM, "campaign", "" },
		{ TOPTION_BLOOD_N_GORE, "campaign", "" },
		{ TOPTION_ANIMATE_SMOKE, "world", "" }, { TOPTION_TOGGLE_TREE_TOPS, "world", "T" }, { TOPTION_TOGGLE_WIREFRAME, "world", "" },
		{ TOPTION_GLOW_ITEMS, "world", "" }, { TOPTION_3D_CURSOR, "world", "Home" }, { TOPTION_MERC_CASTS_LIGHT, "world", "G" },
		{ TOPTION_MERC_ALWAYS_LIGHT_UP, "world", "" },
		{ TOPTION_SPEECH, "dialogue", "" }, { TOPTION_SUBTITLES, "dialogue", "" }, { TOPTION_KEY_ADVANCE_SPEECH, "dialogue", "" },
		{ TOPTION_MUTE_CONFIRMATIONS, "dialogue", "" },
		{ TOPTION_DONT_MOVE_MOUSE, "mouse", "" }, { TOPTION_OLD_SELECTION_METHOD, "mouse", "" }, { TOPTION_SMART_CURSOR, "mouse", "" },
		{ TOPTION_SNAP_CURSOR_TO_DOOR, "mouse", "" },
		// mirrored on the accessibility page
		{ TOPTION_SUBTITLES, "reading", "" }, { TOPTION_KEY_ADVANCE_SPEECH, "reading", "" },
	};
	static_assert(std::size(PLACEMENT) == NUM_GAME_OPTIONS + 2, "every toggle has a place");

	struct Res { int w, h; };
	std::vector<Res> Resolutions()
	{
		std::vector<Res> r = { {0, 0}, {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1080}, {2560, 1440},
			{3440, 1440}, {3840, 2160} };
		VideoDisplaySettings const now = VideoGetDisplaySettings();
		if (std::none_of(r.begin(), r.end(), [&](Res const& x) { return x.w == now.resX && x.h == now.resY; })) r.push_back({ now.resX, now.resY });
		return r;
	}
	constexpr int FPS_CHOICES[] = { 30, 60, 120, 144, 0 };
	constexpr float UI_SCALES[] = { 1.f, 1.25f, 1.5f, 2.f };

	int Pct(UINT32 const v) { return int(std::lround(v * 100.0 / MAXVOLUME)); }
	UINT32 Vol(int const pct) { return UINT32(std::lround(std::clamp(pct, 0, 100) * MAXVOLUME / 100.0)); }
}

class OptionsViewModel final : public ViewModel
{
public:
	OptionsViewModel() : ViewModel("options", TOPIC_SETTINGS)
	{
		Command("tab", [this](Args const& a) { if (!a.empty()) { tab = a[0]; openMenu.clear(); SetHelp(-1); Changed(); } });
		Command("toggle", [this](Args const& a) { if (!a.empty()) Toggle(std::atoi(a[0].c_str())); });
		Command("help", [this](Args const& a) { SetHelp(a.empty() ? -1 : std::atoi(a[0].c_str())); });
		Command("help_text", [this](Args const& a) { if (a.size() >= 2) { helpTitle = a[0]; helpText = a[1]; helpMeta = ""; Changed(); } });
		Command("vol", [this](Args const& a) { if (a.size() >= 2) SetVolume(a[0], std::atoi(a[1].c_str())); });
		Command("vol_at", [this](Args const& a) { if (a.size() >= 2 && volumeAt) SetVolume(a[0], volumeAt(a[0], std::atof(a[1].c_str()))); });
		Command("sample", [this](Args const& a) { if (!a.empty()) Sample(a[0]); });
		Command("menu", [this](Args const& a) { std::string const m = a.empty() ? "" : a[0]; openMenu = openMenu == m ? "" : m; Changed(); });
		Command("choose", [this](Args const& a) { if (a.size() >= 2) Choose(a[0], std::atoi(a[1].c_str())); });
		Command("motion", [this](Args const&) { reducedMotion = !reducedMotion; SetReducedMotion(reducedMotion); SaveNative(); Changed(); });
		// the rebindable keys (issue #48 slice 1): the page is a view of the KeyBindings registry
		Command("capture", [this](Args const& a) { Capture(a.empty() ? "" : a[0]); });
		Command("capture_cancel", [this](Args const&) { CaptureCancel(); });
		Command("bind_refused", [this](Args const&) { BindRefused(); });
		Command("reset", [this](Args const& a) { ResetKey(a.empty() ? "" : a[0]); });
		Command("reset_all", [this](Args const&) { ResetAllKeys(); });
		Command("key_help", [this](Args const& a) { SetKeyHelp(a.empty() ? "" : a[0]); });
		for (char const* c : { "save", "load", "quit", "done" })
		{
			std::string const cmd = c;
			Command(cmd, [this, cmd](Args const&) { if (cmd == "save" && !canSave) return; if (onAction) onAction(cmd); });
		}
	}

	void Load(bool fromMenu)
	{
		inGame = !fromMenu;
		canSave = inGame && CanGameBeSaved() && gGameOptions.ubGameSaveMode != DIF_DEAD_IS_DEAD;
		title = Str("options.title");
		kicker = inGame ? ST::format("{} · {}", Str("options.in_game"), FormatNow()).to_std_string() : Str("options.from_menu");
		lSave = zOptionsText[OPT_SAVE_GAME].to_std_string(); lLoad = zOptionsText[OPT_LOAD_GAME].to_std_string();
		lQuit = zOptionsText[OPT_MAIN_MENU].to_std_string(); lDone = zOptionsText[OPT_DONE].to_std_string();
		lEffects = zOptionsText[OPT_SOUND_FX].to_std_string(); lSpeech = zOptionsText[OPT_SPEECH].to_std_string();
		lMusic = zOptionsText[OPT_MUSIC].to_std_string();
		difficulty = gzGIOScreenText[GIO_EASY_TEXT + std::clamp(int(gGameOptions.ubDifficultyLevel), 1, 3) - 1].to_std_string();
		style = (gGameOptions.fSciFi ? gzGIOScreenText[GIO_SCI_FI_TEXT] : gzGIOScreenText[GIO_REALISTIC_TEXT]).to_std_string();
		guns = (gGameOptions.fGunNut ? gzGIOScreenText[GIO_GUN_NUT_TEXT] : gzGIOScreenText[GIO_REDUCED_GUNS_TEXT]).to_std_string();
		saving = gzGIOScreenText[gGameOptions.ubGameSaveMode == DIF_IRON_MAN ? GIO_IRON_MAN_TEXT :
			gGameOptions.ubGameSaveMode == DIF_DEAD_IS_DEAD ? GIO_DEAD_IS_DEAD_TEXT : GIO_SAVE_ANYWHERE_TEXT].to_std_string();
		reducedMotion = ReducedMotion();
		// the shortcuts no table drives yet: the tactical screen's, which is all that is left of them (the map's
		// became actions in #48 slice 2). They are still raw key codes, so they stay a reference until that screen's
		// slice gives them actions too.
		keys.clear();
		for (auto const& [k, a, b] : std::initializer_list<std::tuple<char const*, char const*, char const*>>{
				 { "options.key.reload", "R", "" },
				 { "options.key.quick_save", "Alt", "S" },
				 { "options.key.quick_load", "Alt", "L" },
				 { "options.key.cursor3d", "Home", "" } })
			keys.push_back({ "reference", Str(k), a, b });
		for (char const* k :
			 { "gameplay", "video", "audio", "controls", "access", "combat", "campaign", "this_campaign", "display",
			   "world", "volume", "dialogue", "mouse", "keys_reference", "keys_tactical", "reading", "motion" })
		{
			labels[k] = Str(std::string(k).find('_') == std::string::npos && std::string("gameplay video audio controls access").find(k) != std::string::npos
				? std::string("options.tab.") + k : std::string("options.group.") + k);
		}
		for (char const* g : { "stance", "move", "act", "turn", "nav", "panel", "select", "view", "map_time",
				"map_log", "map_view", "map_team", "map_squad", "map_act", "map_nav" })
		{
			labels[std::string("keys_") + g] = Str(std::string("options.group.keys_") + g);
		}
		Refresh();
		SetHelp(-1);
	}

	void Refresh() override
	{
		options.clear();
		for (Placement const& p : PLACEMENT)
		{
			OptionRow r;
			r.opt = p.opt;
			r.group = p.group;
			r.name = zOptionsToggleText[p.opt].to_std_string();
			r.help = zOptionsScreenHelpText[p.opt].to_std_string();
			r.help.erase(std::remove(r.help.begin(), r.help.end(), '|'), r.help.end());
			r.kbd = ToggleHint(p.opt, p.kbd);
			r.on = gGameSettings.fOptions[p.opt];
			options.push_back(r);
		}
		effects = Pct(GetSoundEffectsVolume());
		speech = Pct(GetSpeechVolume());
		music = Pct(MusicGetVolume());
		effectsText = std::to_string(effects) + "%"; speechText = std::to_string(speech) + "%"; musicText = std::to_string(music) + "%";

		// video
		VideoDisplaySettings const v = VideoGetDisplaySettings();
		choices.clear();
		auto const res = Resolutions();
		for (size_t i = 0; i < res.size(); ++i)
		{
			bool const sel = res[i].w == v.resX && res[i].h == v.resY;
			std::string const label = res[i].w == 0 ? Str("options.resolution_auto") : ST::format("{}x{}", res[i].w, res[i].h).to_std_string();
			choices.push_back({ int(i), "res", label, "", sel });
			if (sel) resText = label;
		}
		int i = 0;
		for (auto const& [m, k] : { std::pair{ WindowMode::Windowed, "options.window.windowed" }, std::pair{ WindowMode::BorderlessDesktop, "options.window.borderless" },
			std::pair{ WindowMode::Fullscreen, "options.window.fullscreen" } })
			choices.push_back({ i++, "window", Str(k), std::to_string(std::to_underlying(m)), v.windowMode == m });
		i = 0;
		int32_t const fps = VideoGetTargetFPS();
		for (int f : FPS_CHOICES) choices.push_back({ i++, "fps", f ? std::to_string(f) : Str("options.fps_unlimited"), std::to_string(f), f == fps });
		i = 0;
		VideoScaleQuality const q = VideoGetScaleQuality();
		for (auto const& [m, k] : { std::pair{ VideoScaleQuality::PERFECT, "options.filter.perfect" }, std::pair{ VideoScaleQuality::NEAR_PERFECT, "options.filter.sharp" },
			std::pair{ VideoScaleQuality::LINEAR, "options.filter.linear" } })
			choices.push_back({ i++, "filter", Str(k), std::to_string(std::to_underlying(m)), q == m });
		for (int z = 0; z <= VideoLayout::MAX_UI_SCALE; ++z)
			choices.push_back({ z, "zoom", z == 0 ? Str("options.world_zoom_auto") : std::to_string(z) + "x", std::to_string(z), v.worldZoom == z });
		i = 0;
		for (float s : UI_SCALES)
			choices.push_back({ i++, "uiscale", std::to_string(int(std::lround(s * 100))) + "%", "", std::abs(UserScale() - s) < 0.01f });
		for (ChoiceRow const& c : choices) if (c.group == "fps" && c.selected) fpsText = c.label;
		for (ChoiceRow const& c : choices) if (c.group == "filter" && c.selected) filterText = c.label;
		effectNow = ST::format(Str("options.video_help").c_str(),
			ST::format("{}x{} · UI {}x · {}", SCREEN_WIDTH, SCREEN_HEIGHT, g_ui.m_uiScale,
				VideoIsLayered() ? ST::format("world {}x", g_ui.m_worldZoom) : ST::string("world with the UI"))).to_std_string();

		// the rebindable actions (issue #48): every action of the registry, with the key it runs now
		binds.clear();
		for (KeyBindings::Action const& a : KeyBindings::Bindings().Actions())
		{
			BindRow r;
			r.group = a.group;
			r.name = a.name;
			r.label = Str(a.label);
			r.bound = !a.key.empty();
			r.key = r.bound ? KeyBindings::KeyLabel(a.key) : Str("options.bind.unbound");
			r.changed = a.changed;
			r.lost = !m_lost.empty() && a.name == m_lost;
			binds.push_back(std::move(r));
		}
	}

	void Describe(Fields& f) override
	{
		f.Field("tab", tab);
		f.Field("title", title); f.Field("kicker", kicker);
		f.Field("in_game", inGame); f.Field("can_save", canSave);
		f.Rows("options", options);
		f.Rows("choices", choices);
		f.Rows("keys", keys);
		f.Rows("binds", binds);
		f.Field("open_menu", openMenu);
		f.Field("capturing", capturing);
		f.Field("capture_label", captureLabel);
		f.Field("bind_warn", bindWarn);
		f.Field("res_text", resText); f.Field("fps_text", fpsText); f.Field("filter_text", filterText); f.Field("effect_now", effectNow);
		f.Field("effects", effects); f.Field("speech", speech); f.Field("music", music);
		f.Field("effects_text", effectsText); f.Field("speech_text", speechText); f.Field("music_text", musicText);
		f.Field("help_title", helpTitle); f.Field("help_text", helpText); f.Field("help_meta", helpMeta); f.Field("help_warn", helpWarn);
		f.Field("difficulty", difficulty); f.Field("style", style); f.Field("guns", guns); f.Field("saving", saving);
		f.Field("reduced_motion", reducedMotion);
		f.Field("l_save", lSave); f.Field("l_load", lLoad); f.Field("l_quit", lQuit); f.Field("l_done", lDone);
		f.Field("l_effects", lEffects); f.Field("l_speech", lSpeech); f.Field("l_music", lMusic);
		for (auto& [k, v] : labels) f.Field(("l_" + k).c_str(), v);
		for (auto& [k, v] : strs) f.Field(k.c_str(), v);
	}

	void Toggle(int const opt)
	{
		if (opt < 0 || opt >= NUM_GAME_OPTIONS) return;
		bool const state = !gGameSettings.fOptions[opt];
		gGameSettings.fOptions[opt] = state;
		// Speech or SubTitles must stay on (HandleOptionToggle)
		if (!state && ((opt == TOPTION_SPEECH && !gGameSettings.fOptions[TOPTION_SUBTITLES]) ||
			(opt == TOPTION_SUBTITLES && !gGameSettings.fOptions[TOPTION_SPEECH])))
		{
			gGameSettings.fOptions[opt] = TRUE;
			helpWarn = true;
			if (onRefused) onRefused();
		}
		PlayJA2Sample(state ? BIG_SWITCH3_IN : BIG_SWITCH3_OUT, BTNVOLUME, 1, MIDDLEPAN);
		SaveGameSettings();
		Refresh();
		SetHelp(opt);
	}

	void SetHelp(int const opt)
	{
		helpWarn = false;
		if (opt < 0)
		{
			helpTitle = Str("options.tab." + tab);
			helpText = Str("options.help_hint");
			helpMeta = "";
		}
		else
		{
			for (OptionRow const& r : options)
			{
				if (r.opt != opt) continue;
				helpTitle = r.name;
				helpText = r.help;
				helpMeta = "";
				if (opt == TOPTION_SPEECH || opt == TOPTION_SUBTITLES)
				{
					helpMeta = zOptionsText[OPT_NEED_AT_LEAST_SPEECH_OR_SUBTITLE_OPTION_ON].to_std_string();
				}
			}
		}
		Changed();
	}

	void SetVolume(std::string const& which, int const pct)
	{
		UINT32 const v = Vol(pct);
		if (which == "effects") { SetSoundEffectsVolume(v); m_fxMoved = GetJA2Clock(); }
		else if (which == "speech") { SetSpeechVolume(v); m_speechMoved = GetJA2Clock(); }
		else if (which == "music") MusicSetVolume(v);
		SaveGameSettings();
		Refresh();
		Changed();
	}

	void Sample(std::string const& which)
	{
		if (which == "effects") PlayJA2SampleFromFile(SOUNDSDIR "/weapons/lmg reload.wav", HIGHVOLUME, 1, MIDDLEPAN);
		else if (which == "speech")
			PlayJA2GapSample(BATTLESNDSDIR "/m_cool.wav", HIGHVOLUME, 1, MIDDLEPAN, nullptr);
	}

	/** After a volume stops moving: the sample, as the legacy slider does (HandleSliderBarMovementSounds). */
	void Tick()
	{
		uint32_t const now = GetJA2Clock();
		if (m_fxMoved && now - m_fxMoved > 150)
		{
			m_fxMoved = 0;
			if (!DidGameJustStart() && gfWorldLoaded) HandleNewSectorAmbience(gTilesets[giCurrentTilesetID].ubAmbientID);
			Sample("effects");
		}
		if (m_speechMoved && now - m_speechMoved > 150)
		{
			m_speechMoved = 0;
			Sample("speech");
		}
	}

	void Choose(std::string const& group, int const index)
	{
		openMenu.clear();
		VideoDisplaySettings want = VideoGetDisplaySettings();
		VideoScaleQuality quality = VideoGetScaleQuality();
		ChoiceRow const* c = nullptr;
		for (ChoiceRow const& r : choices) if (r.group == group && r.index == index) c = &r;
		if (!c) { Changed(); return; }
		if (group == "uiscale")
		{
			SetUserScale(UI_SCALES[std::clamp(index, 0, int(std::size(UI_SCALES)) - 1)]);
			SaveNative();
			Refresh();
			Changed();
			return;
		}
		if (group == "fps")
		{
			VideoSetTargetFPS(std::atoi(c->value.c_str()));
			Refresh();
			Changed();
			return;
		}
		if (group == "res")
		{
			auto const res = Resolutions();
			want.resX = res[index].w;
			want.resY = res[index].h;
		}
		else if (group == "window") want.windowMode = WindowMode(std::atoi(c->value.c_str()));
		else if (group == "filter") quality = VideoScaleQuality(std::atoi(c->value.c_str()));
		else if (group == "zoom") want.worldZoom = std::atoi(c->value.c_str());
		RequestVideoSettings(want, quality, !sgp::IsHeadless());
		pendingVideo = true;
		Changed();
	}

	void SaveNative()
	{
		if (GCM && !sgp::IsHeadless()) GCM->saveNativeUiSettings(UserScale(), ReducedMotion());
	}

	// --- the rebindable keys (issue #48 slice 1) -------------------------------------------------------------------

	/** The page asks for a key: the capture state names the action and the modal goes up. */
	void Capture(std::string const& name)
	{
		KeyBindings::Action const* const a = KeyBindings::Bindings().Find(name);
		if (!a) return;
		capturing = name;
		captureLabel = Str(a->label);
		bindWarn.clear();
		m_lost.clear();
		helpWarn = false;
		helpTitle = captureLabel;
		helpText = Str("options.bind.help");
		helpMeta.clear();
		Changed();
	}

	void CaptureCancel()
	{
		capturing.clear();
		bindWarn.clear();
		SetHelp(-1);
		Changed();
	}

	/** A key that cannot be bound: say so, leave the capture up. */
	void BindRefused()
	{
		bindWarn = Str("options.bind.bad_key");
		Changed();
	}

	/** The key the player pressed, while @a capturing is up: bind it and close the capture. */
	void BindKey(std::string const& key)
	{
		if (capturing.empty()) return;
		std::string const name = capturing;
		KeyBindings::Registry& reg = KeyBindings::Bindings();
		KeyBindings::BindOutcome const out = reg.Bind(name, key);
		if (out.result == KeyBindings::BindResult::BadKey)
		{
			BindRefused();
			return;
		}
		capturing.clear();
		bindWarn.clear();
		m_lost = out.loser;
		if (!out.loser.empty())
		{
			KeyBindings::Action const* const l = reg.Find(out.loser);
			bindWarn = ST::format(Str("options.bind.conflict").c_str(), l ? Str(l->label) : out.loser).to_std_string();
		}
		SaveKeys();
		Refresh();
		SetKeyHelp(name);
		Changed();
	}

	void ResetKey(std::string const& name)
	{
		KeyBindings::Registry& reg = KeyBindings::Bindings();
		KeyBindings::BindOutcome const out = reg.Reset(name);
		m_lost.clear();
		bindWarn.clear();
		// a reset that takes the default key back from another action leaves it unbound: say so, as a bind does
		if (!out.loser.empty())
		{
			KeyBindings::Action const* const l = reg.Find(out.loser);
			m_lost = out.loser;
			bindWarn =
				ST::format(Str("options.bind.reset_loser").c_str(), l ? Str(l->label) : out.loser).to_std_string();
		}
		SaveKeys();
		Refresh();
		Changed();
	}

	void ResetAllKeys()
	{
		KeyBindings::Bindings().ResetAll();
		KeyChanged();
	}

	/** What every change to a binding shares: nothing was left unbound by a reset, save, redraw. */
	void KeyChanged()
	{
		m_lost.clear();
		bindWarn.clear();
		SaveKeys();
		Refresh();
		Changed();
	}

	void SetKeyHelp(std::string const& name)
	{
		KeyBindings::Action const* const a = KeyBindings::Bindings().Find(name);
		if (!a) return;
		auto const keyLabel = [](std::string const& key)
		{ return key.empty() ? Str("options.bind.unbound") : KeyBindings::KeyLabel(key); };
		helpWarn = false;
		helpTitle = Str(a->label);
		helpText = ST::format(Str("options.bind.key_help").c_str(), keyLabel(a->key)).to_std_string();
		if (a->changed)
			helpText += " " + ST::format(Str("options.bind.key_default").c_str(), keyLabel(a->def)).to_std_string();
		helpMeta.clear();
		Changed();
	}

	/** The binding is saved where the native UI's other settings are: ja2.json (never from a headless run). */
	void SaveKeys()
	{
		if (GCM && !sgp::IsHeadless()) GCM->saveKeyBindings(KeyBindings::Bindings().Overrides());
	}

	bool Capturing() const { return !capturing.empty(); }

	/** One input event while a key is being captured; the screen's loop gives it every event and nothing else. */
	void CaptureEvent(InputAtom const& e)
	{
		if (e.usEvent == KEY_UP)
		{
			// Esc cancels on its release, as the screen's own Esc does, so its release does not reach the next screen
			if (e.usParam == SDLK_ESCAPE) CaptureCancel();
			return;
		}
		if (e.usEvent != KEY_DOWN) return;    // the first press binds; a repeat does nothing
		if (e.usParam == SDLK_ESCAPE) return; // wait for the release
		std::string const k =
			KeyBindings::KeyFromInput(e.usParam, (e.usKeyState & CTRL_DOWN) != 0, (e.usKeyState & SHIFT_DOWN) != 0,
									  (e.usKeyState & ALT_DOWN) != 0);
		if (k.empty())
		{
			BindRefused();
			return;
		}
		BindKey(k);
	}

	/** A toggle's hint chip: the command's key when the same action is in the command table (Tree tops is
	 * view.roofs), so rebinding it cannot leave this page's hint stale either (issue #48). */
	static std::string ToggleHint(int const opt, char const* const kbd)
	{
		if (opt != TOPTION_TOGGLE_TREE_TOPS) return kbd;
		std::string const key = KeyBindings::Bindings().Key("view.roofs");
		return key.empty() ? std::string() : KeyBindings::KeyLabel(key);
	}

	std::string FormatNow() const
	{
		return ST::format("{} {}, {02d}:{02d}", pMessageStrings[MSG_DAY], GetWorldDay(), GetWorldHour(), GetWorldMinutesInDay() % 60).to_std_string();
	}

	std::string tab = "gameplay", title, kicker, openMenu;
	bool inGame = false, canSave = false, reducedMotion = false, helpWarn = false, pendingVideo = false;
	std::vector<OptionRow> options;
	std::vector<ChoiceRow> choices;
	std::vector<KeyRow> keys;
	std::vector<BindRow> binds;
	std::string capturing, captureLabel, bindWarn;
	std::string resText, fpsText, filterText, effectNow;
	int effects = 0, speech = 0, music = 0;
	std::string effectsText, speechText, musicText;
	std::string helpTitle, helpText, helpMeta;
	std::string difficulty, style, guns, saving;
	std::string lSave, lLoad, lQuit, lDone, lEffects, lSpeech, lMusic;
	std::map<std::string, std::string> labels,
		strs = { { "s_resolution", "" },    { "s_window", "" },       { "s_fps", "" },
				 { "s_filter", "" },        { "s_zoom", "" },         { "s_ui_size", "" },
				 { "s_difficulty", "" },    { "s_style", "" },        { "s_guns", "" },
				 { "s_saving", "" },        { "s_motion", "" },       { "s_motion_help", "" },
				 { "s_colour_note", "" },   { "s_keys_note", "" },    { "s_page", "" },
				 { "s_next", "" },          { "s_change", "" },       { "s_saved_note", "" },
				 { "s_play", "" },          { "s_ui_size_help", "" }, { "s_bind_title", "" },
				 { "s_bind_hint", "" },     { "s_bind_press", "" },   { "s_bind_cancel", "" },
				 { "s_bind_reset", "" },    { "s_bind_unbound", "" }, { "s_keys_reference_note", "" },
				 { "s_bind_reset_row", "" } };

	std::function<void(std::string const&)> onAction;
	std::function<void()> onRefused;
	std::function<int(std::string const&, double)> volumeAt;

	void LoadStrs()
	{
		static std::pair<char const*, char const*> const map[] = { { "s_resolution", "options.resolution" },
																   { "s_window", "options.window" },
																   { "s_fps", "options.fps" },
																   { "s_filter", "options.filter" },
																   { "s_zoom", "options.world_zoom" },
																   { "s_ui_size", "options.ui_size" },
																   { "s_difficulty", "options.difficulty" },
																   { "s_style", "options.style" },
																   { "s_guns", "options.guns" },
																   { "s_saving", "options.saving" },
																   { "s_motion", "options.reduced_motion" },
																   { "s_motion_help", "options.reduced_motion_help" },
																   { "s_colour_note", "options.colour_note" },
																   { "s_keys_note", "options.keys_note" },
																   { "s_page", "options.page" },
																   { "s_next", "options.next" },
																   { "s_change", "options.change" },
																   { "s_saved_note", "options.saved_note" },
																   { "s_play", "options.play_sample" },
																   { "s_ui_size_help", "options.ui_size_help" },
																   { "s_bind_title", "options.bind.title" },
																   { "s_bind_hint", "options.bind.hint" },
																   { "s_bind_press", "options.bind.press" },
																   { "s_bind_cancel", "options.bind.cancel" },
																   { "s_bind_reset", "options.bind.reset" },
																   { "s_bind_unbound", "options.bind.unbound" },
																   { "s_keys_reference_note",
																	 "options.keys_reference_note" },
																   { "s_bind_reset_row", "options.bind.reset_row" } };
		for (auto const& [k, v] : map) strs[k] = Str(v);
	}

private:
	uint32_t m_fxMoved = 0, m_speechMoved = 0;
	std::string m_lost; // the action the last bind left unbound (its row is marked until the next change)
};


namespace
{
	OptionsViewModel* g_options = nullptr;
	bool g_quitConfirmed = false;

	void ConfirmQuit(MessageBoxReturnValue const r)
	{
		if (r == MSG_BOX_RETURN_YES) g_quitConfirmed = true;
	}

	class OptionsScreen final : public Screen
	{
	public:
		void Enter() override
		{
			// what EnterOptionsScreen and OptionsScreenHandle's entry do besides the widgets
			if (guiPreviousOptionScreen == GAME_SCREEN || guiPreviousOptionScreen == MAP_SCREEN) SnapshotGameFrame();
			PauseGame();
			StopAmbients();
			RemoveMouseRegionForPauseOfClock();
			DisableScrollMessages();
			m_treeTops = gGameSettings.fOptions[TOPTION_TOGGLE_TREE_TOPS];
			m_glow = gGameSettings.fOptions[TOPTION_GLOW_ITEMS];
			m_smoke = gGameSettings.fOptions[TOPTION_ANIMATE_SMOKE];
			g_quitConfirmed = false;
			g_options = &m_vm;
			m_vm.LoadStrs();
			m_vm.Load(guiPreviousOptionScreen == MAINMENU_SCREEN);
			m_vm.onAction = [this](std::string const& a) { Action(a); };
			m_vm.onRefused = [] {
				DoMessageBox(MSG_BOX_BASIC_STYLE, zOptionsText[OPT_NEED_AT_LEAST_SPEECH_OR_SUBTITLE_OPTION_ON], OPTIONS_SCREEN, MSG_BOX_FLAG_OK, nullptr, nullptr);
			};
			m_vm.volumeAt = [this](std::string const& which, double x) {
				Rml::Element* e = m_doc ? m_doc->GetElementById("options.audio." + which + ".track") : nullptr;
				if (!e) return 0;
				float const left = e->GetAbsoluteOffset(Rml::BoxArea::Border).x, w = e->GetBox().GetSize(Rml::BoxArea::Border).x;
				return int(std::lround(std::clamp((x - left) / std::max(1.f, w), 0.0, 1.0) * 100));
			};
			m_binding.emplace(Context(), m_vm);
			m_doc = LoadDocument("screens/options.rml");
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
			SetCompact(m_doc, "options");
		}

		ScreenID Handle() override
		{
			if (g_quitConfirmed)
			{
				// ConfirmQuitToMainMenuMessageBoxCallBack
				g_quitConfirmed = false;
				DoDeadIsDeadSaveIfNecessary();
				ReStartingGame();
				m_next = MAINMENU_SCREEN;
			}
			if (m_vm.pendingVideo && !VideoChangePending())
			{
				m_vm.pendingVideo = false;
				m_vm.Refresh();
				m_vm.Changed();
			}
			m_vm.Tick();
			InputAtom e;
			while (DequeueEvent(&e))
			{
				// a key being captured takes every event: nothing else sees the press (issue #48)
				if (m_vm.Capturing())
				{
					m_vm.CaptureEvent(e);
					continue;
				}
				bool const used = ProcessKey(e);
				if (e.usEvent == KEY_UP && e.usParam == SDLK_ESCAPE)
				{
					// on release, so that the release does not reach the screen we go back to (the main menu quits on it)
					if (!m_vm.openMenu.empty()) m_vm.Invoke("menu"); else Action("done");
					continue;
				}
				if (used || e.usEvent != KEY_DOWN) continue;
				switch (e.usParam)
				{
					case SDLK_S: m_vm.Invoke("save"); break;
					case SDLK_L: Action("load"); break;
					case SDLK_V: m_vm.Invoke("tab", { "video" }); break;
					case SDLK_1: m_vm.Invoke("tab", { "gameplay" }); break;
					case SDLK_2: m_vm.Invoke("tab", { "video" }); break;
					case SDLK_3: m_vm.Invoke("tab", { "audio" }); break;
					case SDLK_4: m_vm.Invoke("tab", { "controls" }); break;
					case SDLK_5: m_vm.Invoke("tab", { "access" }); break;
					default: break;
				}
			}
			SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR);
			return m_next;
		}

		void Resized() override { SetCompact(m_doc, "options"); }

		void Exit() override
		{
			CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
			g_options = nullptr;
			// ExitOptionsScreen
			SaveGameSettings();
			CreateMouseRegionForPauseOfClock();
			if (m_next == GAME_SCREEN) EnterTacticalScreen();
			if (m_treeTops != gGameSettings.fOptions[TOPTION_TOGGLE_TREE_TOPS] && gfWorldLoaded) SetTreeTopStateForMap();
			if (m_glow != gGameSettings.fOptions[TOPTION_GLOW_ITEMS] && gfWorldLoaded) ToggleItemGlow(gGameSettings.fOptions[TOPTION_GLOW_ITEMS]);
			if (m_smoke != gGameSettings.fOptions[TOPTION_ANIMATE_SMOKE] && gfWorldLoaded) UpdateSmokeEffectGraphics();
			UnPauseGame();
		}

	private:
		void Action(std::string const& a)
		{
			if (m_next != OPTIONS_SCREEN) return;
			if (a == "save")
			{
				gfSaveGame = TRUE;
				m_next = SAVE_LOAD_SCREEN;
			}
			else if (a == "load")
			{
				gfSaveGame = FALSE;
				m_next = SAVE_LOAD_SCREEN;
			}
			else if (a == "quit")
			{
				DoMessageBox(MSG_BOX_BASIC_STYLE, zOptionsText[OPT_RETURN_TO_MAIN], OPTIONS_SCREEN, MSG_BOX_FLAG_YESNO, ConfirmQuit, nullptr);
			}
			else if (a == "done")
			{
				m_next = guiPreviousOptionScreen;
			}
		}

		OptionsViewModel m_vm;
		std::optional<Binding> m_binding;
		Rml::ElementDocument* m_doc = nullptr;
		ScreenID m_next = OPTIONS_SCREEN;
		bool m_treeTops = false, m_glow = false, m_smoke = false;
	};
}

namespace
{
	// automation: ja2.viewModel("options") outside the screen shows the settings and this campaign's options
	bool const g_registered = (RegisterViewModelFactory("options", [] {
		auto vm = std::make_unique<OptionsViewModel>();
		vm->LoadStrs();
		vm->Load(guiCurrentScreen == MAINMENU_SCREEN);
		return std::unique_ptr<ViewModel>(std::move(vm));
	}), true);
}

std::unique_ptr<Screen> CreateOptionsScreen()
{
	return std::make_unique<OptionsScreen>();
}

}
