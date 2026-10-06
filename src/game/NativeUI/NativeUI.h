#pragma once
// The native UI runtime (docs/plan/native-modern-game.md, Phase 2). RmlUi documents drawn over (or instead of) the
// legacy screens at the output's own resolution, in dp: 1dp = min(w / 1920, h / 1080) x the user's UI scale, never
// less than a 1280x720 dp layout. This header has no RmlUi types: the game uses it; screens and overlays use
// NativeUIRuntime.h.
//
// - ui_mode: every native-capable screen or overlay has a key ("credits", "msgbox", ...) resolved to legacy or native
//   at screen entry (ResolveMode): Lua override > ja2.json "ui_mode" > the default. Native needs the runtime built in
//   and an output of at least 1280x720 pixels; otherwise the legacy UI is used.
// - Routing: GameLoop calls HandleScreen for the current screen; a native screen then owns input and drawing.
// - Automation: Elements()/Texts() expose native elements by id; LayoutAudit() checks clipping, overlap, off-screen
//   and truncated text.

#include "ScreenIDs.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct InputAtom;

namespace NativeUI
{
	// ---- ui_mode ---------------------------------------------------------------------------------------------
	enum class UiMode { Legacy, Native };
	char const* ToString(UiMode);
	std::optional<UiMode> ParseUiMode(std::string const&);

	struct ModeKey
	{
		char const* key;
		char const* what;
		UiMode      defaultMode;
	};
	/** Every key ui_mode knows, with its default. */
	std::vector<ModeKey> const& ModeKeys();
	bool IsModeKey(std::string const& key);

	/** From ja2.json at startup: "key=mode" lines and the native UI scale. Unknown keys are logged and kept. */
	void Configure(std::string const& modePairs, float nativeUiScale);
	/** Accessibility: no transitions or automatic scrolling in the native UI (ja2.json "reduced_motion"). */
	bool ReducedMotion();
	void SetReducedMotion(bool);

	/** Runtime override (Lua ja2.setUiMode); nullopt removes it. Throws std::invalid_argument on an unknown key. */
	void SetModeOverride(std::string const& key, std::optional<UiMode>);
	/** What is asked for: override > config > default. */
	UiMode ConfiguredMode(std::string const& key);
	/** What is used: ConfiguredMode, unless the native UI cannot run (then legacy, see *reason). */
	UiMode ResolveMode(std::string const& key, std::string* reason = nullptr);

	// ---- runtime ---------------------------------------------------------------------------------------------
	constexpr int MIN_WIDTH  = 1280;
	constexpr int MIN_HEIGHT = 720;

	bool Built();     // compiled with RmlUi (WITH_NATIVE_UI)
	/** Built, the UI assets are there and the output is at least MIN_WIDTH x MIN_HEIGHT pixels. */
	bool Available(std::string* reason = nullptr);
	float UserScale();
	void  SetUserScale(float); // clamped to 0.5 .. 3
	/** Pixels per dp now (0 before the runtime started). */
	float DpScale();
	struct Info
	{
		bool        running = false;
		std::string renderer; // "gpu (<driver>)" or "software"
		int         width = 0, height = 0; // output pixels
		float       dp = 0, userScale = 1;
		std::string screen;   // the native screen's key, or ""
		std::vector<std::string> documents;
		int         warnings = 0;
	};
	Info GetInfo();

	/** Once per game loop, before input: keeps the size, scale and clock up to date. */
	void BeginFrame();
	/** What BeginFrame does without the game loop: the view models are refreshed and what they changed is laid out
	 * and drawn, so a capture right after a driven command shows the current state. No game state moves (a loading
	 * screen is captured where it is, not closed). */
	void CaptureFrame();
	/** A native screen or modal takes the mouse: legacy regions get nothing. */
	bool CapturesMouse();
	/** A mouse event from the input queue (only when CapturesMouse()). */
	void HandleMouseEvent(InputAtom const&);
	/** The mouse position in canvas pixels (every frame). */
	void MouseMoved(int canvasX, int canvasY);
	/** A keyboard event for the focused native document; true if it used it. */
	bool HandleKeyEvent(InputAtom const&);

	/** GameLoop's screen dispatch: the native implementation of @a id when its ui_mode resolves to native at entry,
	 * else @a legacy. */
	ScreenID HandleScreen(ScreenID id, ScreenID (*legacy)());
	/** Shows a static design mock (an RML file under the UI dir, e.g. "mocks/phase3/mainmenu.rml") over the current
	 * screen until Esc (M2 wireframes). Throws if the native UI cannot run. */
	void OpenMock(std::string const& path);
	/** The output was laid out again (video settings): a screen with a native version picks legacy or native anew. */
	void ScreenRelaidOut();
	/** The ui_mode key of a screen with a native implementation, or nullptr. */
	char const* ScreenKey(ScreenID);

	// ---- shared overlays ---------------------------------------------------------------------------------------
	enum class ToastKind { Info, Ok, Warn, Danger };
	/** A screen message as a toast (bottom right, dismissed after a few seconds of game time). */
	void Toast(std::string const& text, ToastKind = ToastKind::Info, std::string const& title = {});
	/** Screen messages go to toasts (a native screen is showing, or "toasts" resolves to native). */
	bool ToastsActive();
	/** Fast help (tooltips) of legacy mouse regions drawn natively, when "tooltip" resolves to native. The text uses
	 * the legacy markup (| bold, ^ green, ~ red, newlines). Returns false when the legacy tooltip should draw. */
	bool ShowFastHelp(char32_t const* text, int regionX, int regionY, int regionW, int regionH);
	void HideFastHelp();

	// Message box: MessageBoxScreen.cc asks, at DoMessageBox, whether this box is native.
	struct MessageBoxButton { std::string id, label, key; int result; bool primary; };
	bool MessageBoxWanted();
	void OpenMessageBox(std::string const& text, std::vector<MessageBoxButton> const& buttons, bool danger);
	bool MessageBoxOpen();
	/** The text of the open native message box (automation). */
	std::string MessageBoxText();
	/** The result of the button pressed (0 while none was). */
	int  MessageBoxResult();
	void CloseMessageBox();

	// ---- front-end screens (Phase 3) --------------------------------------------------------------------------
	/** The pre-game setup screen (game directory, save directory, resource version, mods, logs), shown before any
	 * game data is loaded when the configured game directory cannot be used. @a engineOptions is the EngineOptions*.
	 * Returns true when the player saved a valid configuration and asked to start the game (the caller relaunches);
	 * false when they quit. Stops (returns false) when the native UI cannot run here. */
	bool RunSetup(void* engineOptions);
	/** Keeps a small copy of the game picture now (the map or tactical) for the next save's thumbnail. */
	void SnapshotGameFrame();
	/** Writes the kept picture as a PNG to @a path (next to a save; the save format is unchanged). */
	void WriteSaveThumbnail(std::string const& path);
	/** The loading screen for load screen @a id, when "loadscreen" resolves to native; false: draw the legacy one.
	 * Loading blocks the game loop: the screen draws itself at once and at every progress step. */
	bool ShowLoadingScreen(int id);
	/** The step text of the loading progress bar (SetRelativeStartAndEndPercentage). */
	void LoadingStep(std::string const& text);
	/** Loading progress 0..1; false when the native loading screen is not showing. */
	bool LoadingProgress(double fraction);

	// ---- tactical HUD (Phase 5, docs/ui/tactical.md) ----------------------------------------------------------
	/** The native tactical HUD is showing ("tactical" resolves to native on GAME_SCREEN): the legacy HUD keeps its
	 * logic and regions but draws nothing the native one shows (names over mercs, message lines, turn banner). */
	bool TacticalHudActive();
	/** H: open or close the message log of the native HUD. */
	void TacticalHudToggleLog();

	// ---- weapon readout (issue #141, docs/plan/equipment-revamp.md) --------------------------------------------
	/** The range and ballistics readout: a compact comparison of two weapons over
	 * the damage pipeline, opened over the tactical HUD or the map screen. */
	void OpenWeaponReadout();
	void CloseWeaponReadout();
	bool WeaponReadoutActive();

	// ---- loadout (issue #262, docs/plan/equipment-revamp.md) ----------------------------------------------------
	/** The native loadout screen: the merc paperdoll, the LBE pockets, the weapon platform with
	 *  its typed slots and the live load readout, opened over the tactical HUD or the map screen. */
	void OpenLoadout();
	void CloseLoadout();
	bool LoadoutActive();

	// ---- change notification (view models, ViewModel.h) -------------------------------------------------------
	enum Topic : uint32_t
	{
		TOPIC_MONEY    = 1u << 0, // the balance changed
		TOPIC_CLOCK    = 1u << 1, // game time moved on (a minute)
		TOPIC_TEAM     = 1u << 2, // hired, fired, died, assignment
		TOPIC_SECTOR   = 1u << 3, // the loaded or selected sector changed
		TOPIC_MESSAGES = 1u << 4, // a screen message was added
		TOPIC_SETTINGS = 1u << 5, // options, video settings, language
		TOPIC_ALL      = ~0u,
	};
	/** Game code: something of @a topics changed (where it sets its own "dirty" flags). Cheap; safe any time. */
	void Notify(uint32_t topics);

	// ---- automation ------------------------------------------------------------------------------------------
	struct ElementInfo
	{
		std::string id, label, text, help, role, document;
		int  x = 0, y = 0, w = 0, h = 0; // canvas pixels (what clicks use)
		bool enabled = true, clickable = false, focused = false;
	};
	std::vector<ElementInfo> Elements();
	struct TextInfo { std::string text; int x, y, w, h; };
	std::vector<TextInfo> Texts();
	/** Layout problems of the native documents now: off screen, clipped, overlapping, truncated text. */
	std::vector<std::string> LayoutAudit();
	/** Focus an element by id (keyboard focus, visible ring). */
	bool Focus(std::string const& id);
	std::string FocusedId();
}
