#pragma once

#include "SDL3/SDL.h"
#include <string>
#include <vector>

/** @file
 * In-process E2E screen-automation driver for JA2 Stracciatella.
 *
 * Parses a simple Playwright-like script and drives the game by injecting
 * synthetic SDL events and reading pixels from ScreenBuffer.
 *
 * Script format:
 *   move X Y           Move cursor to logical pixel
 *   click X Y          Left press+release at (X,Y)
 *   rclick X Y         Right press+release at (X,Y)
 *   key NAME           Press+release a key (ESCAPE,ENTER,F1,A,...)
 *   type text...       Send text input (name fields)
 *   wait MS            Sleep milliseconds
 *   waitpixel X Y #RGB [tol] [timeoutMs]  Block until pixel matches
 *   waitstable X Y [timeoutMs]            Block until pixel stops changing
 *   assertpixel X Y #RGB [tol]            Pass/fail assert on a pixel
 *   screenshot path    Dump current frame
 *
 * Lines starting with '#' are comments. Blank lines are ignored.
 */

// --- Globals set by SGP.cc when -uitest is detected ---
extern bool        g_uitest_mode;
extern std::string g_uitest_script_path;

// --- Script command types ---
enum class UITestCmdType {
    MOVE,
    CLICK,
    RCLICK,
    KEY,
    TYPE,
    WAIT,
    WAITPIXEL,
    WAITSTABLE,
    ASSERTPIXEL,
    SCREENSHOT,
};

struct UITestPixelTarget {
    Uint8 r, g, b;
    Uint8 tolerance; // default 8
};

struct UITestCommand {
    UITestCmdType type;
    // MOVE, CLICK, RCLICK, ASSERTPIXEL, WAITPIXEL, WAITSTABLE
    int x = 0;
    int y = 0;
    // KEY
    SDL_Keycode keycode = SDLK_UNKNOWN;
    // TYPE
    std::string text;
    // WAIT, WAITPIXEL (timeoutMs), WAITSTABLE (timeoutMs)
    int ms = 0;
    // ASSERTPIXEL, WAITPIXEL
    UITestPixelTarget pixel{0, 0, 0, 8};
    // WAITPIXEL, WAITSTABLE
    int timeoutMs = 0;
    // SCREENSHOT
    std::string screenshotPath;
};

// --- The driver ---
class UITestDriver {
public:
    /** Parse a script file at @a path. Returns false on parse error. */
    bool loadScript(const std::string& path);

    /** Called once per MainLoop() iteration, before SDL_PollEvent.
     *  Injects events, checks pixels, manages wait states. */
    void pump();

    /** Number of failed assertions so far. */
    int failures() const { return m_failures; }

    /** True when the script is exhausted. */
    bool finished() const { return m_finished; }

private:
    bool parseLine(const std::string& line, int lineNum, UITestCommand& cmd);
    void execute(const UITestCommand& cmd);
    void moveTo(int x, int y);
    void clickAt(int x, int y, Uint8 button);
    void pressKey(SDL_Keycode kc);
    void typeText(const std::string& t);
    void assertPixel(int x, int y, const UITestPixelTarget& target);
    void screenshot(const std::string& path);

    // Read a pixel from ScreenBuffer at logical (x,y), return false on error.
    bool readPixel(int x, int y, Uint8& r, Uint8& g, Uint8& b);

    std::vector<UITestCommand> m_commands;
    size_t m_pc = 0; // program counter (index into m_commands)

    // Wait state
    bool  m_waiting = false;
    Uint32 m_waitUntil = 0; // SDL_GetTicks() deadline

    // Click release state (one-pump gap between down and up)
    struct PendingRelease {
        bool active = false;
        int x = 0;
        int y = 0;
        Uint8 button = SDL_BUTTON_LEFT;
    };
    PendingRelease m_pendingRelease;

    // Waitpixel state
    bool m_waitingForPixel = false;
    int m_waitPixelX = 0;
    int m_waitPixelY = 0;
    UITestPixelTarget m_waitPixelTarget{0, 0, 0, 8};
    Uint32 m_waitPixelDeadline = 0;

    // Waitstable state
    bool m_waitingStable = false;
    int m_waitStableX = 0;
    int m_waitStableY = 0;
    Uint32 m_waitStableDeadline = 0;
    Uint32 m_waitStableLastColor = 0;
    int m_waitStableStableFrames = 0;

    int m_failures = 0;
    bool m_finished = false;
};