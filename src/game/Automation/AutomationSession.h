#pragma once

#include "TextRegistry.h"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

/** @file
 * The automation session: everything a driver can do to the running game.
 * The Lua API (AutomationLua.cc) and the TCP server are thin layers over this.
 */
namespace Automation
{
	/** A wait ran out of (virtual) time. */
	struct TimeoutError : std::runtime_error { using std::runtime_error::runtime_error; };
	/** The game asked to quit while the driver was still stepping it. */
	struct GameExitedError : std::runtime_error { using std::runtime_error::runtime_error; };
	/** The game threw; the session is dead. */
	struct GameCrashedError : std::runtime_error { using std::runtime_error::runtime_error; };
	/** A script expectation failed. */
	struct ExpectationError : std::runtime_error { using std::runtime_error::runtime_error; };

	/** Something clickable on screen: a mouse region, possibly a button. */
	struct Element
	{
		int         id;        // position in the region list, valid for this frame only
		std::string kind;      // "button" or "region"
		SDL_Rect    rect;
		std::string label;     // name, else button text, else text printed inside, else help text
		std::string name;      // MOUSE_REGION::Name (may be empty)
		std::string text;      // button text (may be empty)
		std::string help;      // fast-help (tooltip) text
		bool        enabled;
		bool        clickable; // a click at its centre would reach it
		int         priority;
		std::optional<SDL_Rect> labelFrom; // caption printed beside the control, if the label came from one
		std::string nativeId;  // kind "native": the element's id in the native UI (NativeUI.h)
		bool        focused = false;

		SDL_Point Center() const { return { rect.x + rect.w / 2, rect.y + rect.h / 2 }; }
	};

	/** How to find something on screen. */
	struct Locator
	{
		std::string text;               // matches element label/text/help or on-screen text
		std::string id;                 // a native element's id (exact), instead of text
		bool        exact = false;      // whole-string match instead of substring
		int         index = 1;          // pick the n-th match in reading order
		std::optional<SDL_Point> point; // explicit coordinates
		std::optional<SDL_Rect>  within;// restrict to this area

		std::string Describe() const;
	};

	/** What a locator resolved to. */
	struct Target
	{
		SDL_Point   point;
		std::string description;
		std::optional<Element> element;
	};

	namespace Session
	{
		/** Advance the game by @a frames frames. */
		void Step(unsigned frames = 1);
		/** Advance the game by (at least) @a ms of virtual time. */
		void Wait(unsigned ms);
		/** Step until @a predicate holds. Throws TimeoutError after @a timeoutMs of virtual time. */
		void WaitUntil(std::function<bool()> const& predicate, unsigned timeoutMs, std::string const& what);

		uint64_t Frame();
		uint64_t ElapsedMs();

		// --- Input (each call steps the frames a real user would take) ---
		void MouseMove(int x, int y);
		void MouseButton(int button, bool down);
		void Click(int x, int y, int button = 1, int count = 1);
		void Drag(int x0, int y0, int x1, int y1);
		void Wheel(int x, int y, int dy);
		/** @a combo is e.g. "ENTER", "a", "alt+c", "ctrl+shift+s". */
		void Key(std::string const& combo);
		void KeyDown(std::string const& key);
		void KeyUp(std::string const& key);
		void Type(std::string const& text);
		SDL_Point MousePos();

		// --- Looking at the screen ---
		std::string ScreenName();
		/** Text of the open message box (empty if none). */
		std::string MessageBoxText();
		std::vector<Element> Elements();
		/** Active regions/buttons (non-empty, enabled or not) not fully inside the screen. */
		std::vector<Element> OffscreenElements();
		/** OffscreenElements() as messages, plus the native UI's layout audit (clipped, overlapping, off screen,
		 * truncated text). Empty when the layout is fine. */
		std::vector<std::string> LayoutProblems();
		std::vector<TextRegistry::VisibleText> Texts();
		std::optional<Target> Find(Locator const&);
		/** Wait until the locator resolves (and is enabled), then return it. */
		Target Resolve(Locator const&, unsigned timeoutMs);
		bool IsIdle();
		void WaitIdle(unsigned timeoutMs);
		uint32_t Pixel(int x, int y); // 0xRRGGBB
		void Screenshot(std::string const& path);
		std::string ResolveOutputPath(std::string const& path);

		// --- Game ---
		std::vector<std::string> Saves();
		void Load(std::string const& save, unsigned timeoutMs);
		void Save(std::string const& name, std::string const& description);
		void Quit();

		/** Guard against stepping a crashed game. */
		bool Crashed();
	}

	/** Parse a key combo such as "alt+c". Returns false for unknown names. */
	bool ParseKeyCombo(std::string const& combo, SDL_Keycode& key, SDL_Keymod& mods);
}
