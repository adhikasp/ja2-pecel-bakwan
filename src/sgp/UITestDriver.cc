#include "UITestDriver.h"
#include "Input.h"
#include "Logger.h"
#include "Video.h"
#include "UILayout.h"

#include <SDL.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

// stb_image_write — single-header JPEG/PNG writer (pulled into .cc only)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// --- Global flag and path ---
bool        g_uitest_mode      = false;
std::string g_uitest_script_path;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Split a string by whitespace, respecting nothing fancy (plain tokens). */
static std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

/** Parse a hex colour like #1f6f1f or #ff00ff into RGB. Returns false on failure. */
static bool parseHexColor(const std::string& s, Uint8& r, Uint8& g, Uint8& b) {
    if (s.empty() || s[0] != '#') return false;
    unsigned long v = std::strtoul(s.c_str() + 1, nullptr, 16);
    size_t len = s.size() - 1;
    if (len == 6) {
        r = static_cast<Uint8>((v >> 16) & 0xff);
        g = static_cast<Uint8>((v >> 8)  & 0xff);
        b = static_cast<Uint8>((v)       & 0xff);
        return true;
    }
    if (len == 3) {
        r = static_cast<Uint8>(((v >> 8) & 0xf) * 17);
        g = static_cast<Uint8>(((v >> 4) & 0xf) * 17);
        b = static_cast<Uint8>((v       & 0xf) * 17);
        return true;
    }
    return false;
}

/** Map a key name string to SDL_Keycode. */
static SDL_Keycode nameToKeycode(const std::string& name) {
    if (name == "ESCAPE")     return SDLK_ESCAPE;
    if (name == "ENTER")      return SDLK_RETURN;
    if (name == "SPACE")      return SDLK_SPACE;
    if (name == "TAB")        return SDLK_TAB;
    if (name == "BACKSPACE")  return SDLK_BACKSPACE;
    if (name == "DELETE")     return SDLK_DELETE;
    if (name == "UP")         return SDLK_UP;
    if (name == "DOWN")       return SDLK_DOWN;
    if (name == "LEFT")       return SDLK_LEFT;
    if (name == "RIGHT")      return SDLK_RIGHT;
    if (name == "HOME")       return SDLK_HOME;
    if (name == "END")        return SDLK_END;
    if (name == "PAGEUP")     return SDLK_PAGEUP;
    if (name == "PAGEDOWN")   return SDLK_PAGEDOWN;
    if (name == "F1")         return SDLK_F1;
    if (name == "F2")         return SDLK_F2;
    if (name == "F3")         return SDLK_F3;
    if (name == "F4")         return SDLK_F4;
    if (name == "F5")         return SDLK_F5;
    if (name == "F6")         return SDLK_F6;
    if (name == "F7")         return SDLK_F7;
    if (name == "F8")         return SDLK_F8;
    if (name == "F9")         return SDLK_F9;
    if (name == "F10")        return SDLK_F10;
    if (name == "F11")        return SDLK_F11;
    if (name == "F12")        return SDLK_F12;
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') return static_cast<SDL_Keycode>(SDLK_a + (c - 'a'));
        if (c >= 'A' && c <= 'Z') return static_cast<SDL_Keycode>(SDLK_a + (c - 'A'));
        if (c >= '0' && c <= '9') return static_cast<SDL_Keycode>(SDLK_0 + (c - '0'));
    }
    return SDLK_UNKNOWN;
}

/** Format RGB as a hex colour string like "#1f6f1f". */
static std::string hexColor(Uint8 r, Uint8 g, Uint8 b) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

bool UITestDriver::loadScript(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        SLOGE("[uitest] Cannot open script: {}", path);
        // Mark finished so pump() bails out immediately and does not overwrite
        // the setup-error exit code (2) the caller will set.
        m_finished = true;
        return false;
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(f, line)) {
        ++lineNum;
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue; // blank
        std::string trimmed = line.substr(start);

        // Comment
        if (trimmed[0] == '#') continue;

        UITestCommand cmd;
        if (!parseLine(trimmed, lineNum, cmd)) {
            m_finished = true;
            return false;
        }
        m_commands.push_back(std::move(cmd));
    }

    SLOGI("[uitest] Loaded {} command(s) from {}", m_commands.size(), path);
    return true;
}

bool UITestDriver::parseLine(const std::string& line, int lineNum, UITestCommand& cmd) {
    auto tokens = tokenize(line);
    if (tokens.empty()) return true; // should not happen

    const std::string& verb = tokens[0];

    auto usageErr = [&](const std::string& msg) {
        SLOGE("[uitest] Line {}: {}  (got: {})", lineNum, msg, line);
        return false;
    };

    if (verb == "move") {
        if (tokens.size() < 3) return usageErr("move requires X Y");
        cmd.type = UITestCmdType::MOVE;
        cmd.x = std::stoi(tokens[1]);
        cmd.y = std::stoi(tokens[2]);
        return true;
    }

    if (verb == "click") {
        if (tokens.size() < 3) return usageErr("click requires X Y");
        cmd.type = UITestCmdType::CLICK;
        cmd.x = std::stoi(tokens[1]);
        cmd.y = std::stoi(tokens[2]);
        return true;
    }

    if (verb == "rclick") {
        if (tokens.size() < 3) return usageErr("rclick requires X Y");
        cmd.type = UITestCmdType::RCLICK;
        cmd.x = std::stoi(tokens[1]);
        cmd.y = std::stoi(tokens[2]);
        return true;
    }

    if (verb == "key") {
        if (tokens.size() < 2) return usageErr("key requires NAME");
        cmd.type = UITestCmdType::KEY;
        cmd.keycode = nameToKeycode(tokens[1]);
        if (cmd.keycode == SDLK_UNKNOWN) {
            return usageErr("unknown key name '" + tokens[1] + "'");
        }
        return true;
    }

    if (verb == "type") {
        if (tokens.size() < 2) return usageErr("type requires text");
        cmd.type = UITestCmdType::TYPE;
        // Reconstruct the text from remaining tokens (preserving spaces)
        cmd.text = tokens[1];
        for (size_t i = 2; i < tokens.size(); ++i) {
            cmd.text += " " + tokens[i];
        }
        return true;
    }

    if (verb == "wait") {
        if (tokens.size() < 2) return usageErr("wait requires MS");
        cmd.type = UITestCmdType::WAIT;
        cmd.ms = std::stoi(tokens[1]);
        return true;
    }

    if (verb == "waitpixel") {
        if (tokens.size() < 4) return usageErr("waitpixel requires X Y #RGB [tol] [timeoutMs]");
        cmd.type = UITestCmdType::WAITPIXEL;
        cmd.x = std::stoi(tokens[1]);
        cmd.y = std::stoi(tokens[2]);
        if (!parseHexColor(tokens[3], cmd.pixel.r, cmd.pixel.g, cmd.pixel.b)) {
            return usageErr("invalid colour '" + tokens[3] + "' (expected #RRGGBB)");
        }
        cmd.pixel.tolerance = 8;
        cmd.timeoutMs = 30000;
        if (tokens.size() >= 5) cmd.pixel.tolerance = static_cast<Uint8>(std::stoi(tokens[4]));
        if (tokens.size() >= 6) cmd.timeoutMs = std::stoi(tokens[5]);
        return true;
    }

    if (verb == "waitstable") {
        if (tokens.size() < 3) return usageErr("waitstable requires X Y [timeoutMs]");
        cmd.type = UITestCmdType::WAITSTABLE;
        cmd.x = std::stoi(tokens[1]);
        cmd.y = std::stoi(tokens[2]);
        cmd.timeoutMs = 10000;
        if (tokens.size() >= 4) cmd.timeoutMs = std::stoi(tokens[3]);
        return true;
    }

    if (verb == "assertpixel") {
        if (tokens.size() < 4) return usageErr("assertpixel requires X Y #RGB [tol]");
        cmd.type = UITestCmdType::ASSERTPIXEL;
        cmd.x = std::stoi(tokens[1]);
        cmd.y = std::stoi(tokens[2]);
        if (!parseHexColor(tokens[3], cmd.pixel.r, cmd.pixel.g, cmd.pixel.b)) {
            return usageErr("invalid colour '" + tokens[3] + "' (expected #RRGGBB)");
        }
        cmd.pixel.tolerance = 8;
        if (tokens.size() >= 5) cmd.pixel.tolerance = static_cast<Uint8>(std::stoi(tokens[4]));
        return true;
    }

    if (verb == "screenshot") {
        if (tokens.size() < 2) return usageErr("screenshot requires path");
        cmd.type = UITestCmdType::SCREENSHOT;
        cmd.screenshotPath = tokens[1];
        return true;
    }

    return usageErr("unknown command '" + verb + "'");
}

// ---------------------------------------------------------------------------
// Pump — called once per MainLoop iteration before SDL_PollEvent
// ---------------------------------------------------------------------------

void UITestDriver::pump() {
    if (m_finished) return;

    // 1. Handle pending button release (one-pump gap after down)
    if (m_pendingRelease.active) {
        SDL_Event up{};
        up.type = SDL_MOUSEBUTTONUP;
        up.button.button = m_pendingRelease.button;
        up.button.x = static_cast<Uint32>(m_pendingRelease.x);
        up.button.y = static_cast<Uint32>(m_pendingRelease.y);
        SDL_PushEvent(&up);
        m_pendingRelease.active = false;
        // The click/rclick command deferred advancing the program counter until
        // its release fired; advance it now so the script proceeds. Without this
        // the same click is re-executed every pump and the script never moves on.
        ++m_pc;
        return; // yield after release
    }

    // 2. Handle active wait timer
    if (m_waiting) {
        if (SDL_GetTicks() >= m_waitUntil) {
            m_waiting = false;
            ++m_pc;
        }
        return;
    }

    // 3. Handle waitpixel
    if (m_waitingForPixel) {
        Uint8 r = 0, g = 0, b = 0;
        if (readPixel(m_waitPixelX, m_waitPixelY, r, g, b)) {
            auto diff = [](Uint8 a, Uint8 b) -> int { return a > b ? a - b : b - a; };
            if (diff(r, m_waitPixelTarget.r) <= m_waitPixelTarget.tolerance &&
                diff(g, m_waitPixelTarget.g) <= m_waitPixelTarget.tolerance &&
                diff(b, m_waitPixelTarget.b) <= m_waitPixelTarget.tolerance) {
                SLOGD("[uitest] waitpixel({},{}) matched", m_waitPixelX, m_waitPixelY);
                m_waitingForPixel = false;
                ++m_pc;
                return;
            }
        }
        if (SDL_GetTicks() >= m_waitPixelDeadline) {
            Uint8 r = 0, g = 0, b = 0;
            readPixel(m_waitPixelX, m_waitPixelY, r, g, b);
            SLOGE("[uitest] [FAIL] waitpixel({},{}) timeout - expected {}, got {}",
                  m_waitPixelX, m_waitPixelY,
                  hexColor(m_waitPixelTarget.r, m_waitPixelTarget.g, m_waitPixelTarget.b),
                  hexColor(r, g, b));
            ++m_failures;
            m_waitingForPixel = false;
            ++m_pc;
        }
        return;
    }

    // 4. Handle waitstable
    if (m_waitingStable) {
        Uint8 r = 0, g = 0, b = 0;
        Uint32 currentColor = 0;
        if (readPixel(m_waitStableX, m_waitStableY, r, g, b)) {
            currentColor = (static_cast<Uint32>(r) << 16) | (static_cast<Uint32>(g) << 8) | b;
        }
        if (currentColor == m_waitStableLastColor) {
            ++m_waitStableStableFrames;
            if (m_waitStableStableFrames >= 3) {
                SLOGD("[uitest] waitstable({},{}) stable after {} frames", m_waitStableX, m_waitStableY, m_waitStableStableFrames);
                m_waitingStable = false;
                ++m_pc;
                return;
            }
        } else {
            m_waitStableStableFrames = 0;
            m_waitStableLastColor = currentColor;
        }

        if (SDL_GetTicks() >= m_waitStableDeadline) {
            SLOGE("[uitest] [FAIL] waitstable({},{}) timeout", m_waitStableX, m_waitStableY);
            ++m_failures;
            m_waitingStable = false;
            ++m_pc;
        }
        return;
    }

    // 5. All commands exhausted?
    if (m_pc >= m_commands.size()) {
        SLOGI("[uitest] Script complete - {} failure(s) of {} total command(s)", m_failures, m_commands.size());
        m_finished = true;
        // Propagate the failure count as the process exit code: 0 = all asserts
        // passed, 1 = at least one assertion failed. This lets ctest/CI fail the
        // build on a regression.
        extern void uitestSetExitCode(int code);
        uitestSetExitCode(m_failures > 0 ? 1 : 0);
        extern void requestGameExit();
        requestGameExit();
        return;
    }

    // 6. Execute current command
    const auto& cmd = m_commands[m_pc];
    execute(cmd);
}

// ---------------------------------------------------------------------------
// Command execution
// ---------------------------------------------------------------------------

void UITestDriver::execute(const UITestCommand& cmd) {
    switch (cmd.type) {
    case UITestCmdType::MOVE:
        SLOGD("[uitest] move {} {}", cmd.x, cmd.y);
        moveTo(cmd.x, cmd.y);
        ++m_pc;
        break;

    case UITestCmdType::CLICK:
        SLOGD("[uitest] click {} {}", cmd.x, cmd.y);
        clickAt(cmd.x, cmd.y, SDL_BUTTON_LEFT);
        // pc advances when the pending release fires
        break;

    case UITestCmdType::RCLICK:
        SLOGD("[uitest] rclick {} {}", cmd.x, cmd.y);
        clickAt(cmd.x, cmd.y, SDL_BUTTON_RIGHT);
        break;

    case UITestCmdType::KEY:
        SLOGD("[uitest] key {}", SDL_GetKeyName(cmd.keycode));
        pressKey(cmd.keycode);
        ++m_pc;
        break;

    case UITestCmdType::TYPE:
        SLOGD("[uitest] type \"{}\"", cmd.text);
        typeText(cmd.text);
        ++m_pc;
        break;

    case UITestCmdType::WAIT:
        SLOGD("[uitest] wait {} ms", cmd.ms);
        m_waitUntil = SDL_GetTicks() + static_cast<Uint32>(cmd.ms);
        m_waiting = true;
        // pc not advanced yet - will advance when timer expires
        break;

    case UITestCmdType::WAITPIXEL:
        SLOGD("[uitest] waitpixel {} {} {} tol {} timeout {}",
              cmd.x, cmd.y,
              hexColor(cmd.pixel.r, cmd.pixel.g, cmd.pixel.b),
              cmd.pixel.tolerance, cmd.timeoutMs);
        m_waitPixelX = cmd.x;
        m_waitPixelY = cmd.y;
        m_waitPixelTarget = cmd.pixel;
        m_waitPixelDeadline = SDL_GetTicks() + static_cast<Uint32>(cmd.timeoutMs);
        m_waitingForPixel = true;
        break;

    case UITestCmdType::WAITSTABLE:
        SLOGD("[uitest] waitstable {} {} timeout {}", cmd.x, cmd.y, cmd.timeoutMs);
        m_waitStableX = cmd.x;
        m_waitStableY = cmd.y;
        m_waitStableDeadline = SDL_GetTicks() + static_cast<Uint32>(cmd.timeoutMs);
        m_waitStableLastColor = 0xFFFFFFFF; // impossible value to force first read
        m_waitStableStableFrames = 0;
        m_waitingStable = true;
        break;

    case UITestCmdType::ASSERTPIXEL:
        assertPixel(cmd.x, cmd.y, cmd.pixel);
        ++m_pc;
        break;

    case UITestCmdType::SCREENSHOT:
        SLOGD("[uitest] screenshot {}", cmd.screenshotPath);
        screenshot(cmd.screenshotPath);
        ++m_pc;
        break;
    }
}

// ---------------------------------------------------------------------------
// Input injection helpers
// ---------------------------------------------------------------------------

void UITestDriver::moveTo(int x, int y) {
    SDL_Event e{};
    e.type = SDL_MOUSEMOTION;
    e.motion.x = static_cast<Sint32>(x);
    e.motion.y = static_cast<Sint32>(y);
    e.motion.xrel = 0;
    e.motion.yrel = 0;
    SDL_PushEvent(&e);

    // Also update the internal cursor position so the UI responds immediately
    extern UINT16 gusMouseXPos;
    extern UINT16 gusMouseYPos;
    gusMouseXPos = static_cast<UINT16>(x);
    gusMouseYPos = static_cast<UINT16>(y);
}

void UITestDriver::clickAt(int x, int y, Uint8 button) {
    // First move the cursor
    moveTo(x, y);

    // Push button down
    SDL_Event down{};
    down.type = SDL_MOUSEBUTTONDOWN;
    down.button.button = button;
    down.button.x = static_cast<Uint32>(x);
    down.button.y = static_cast<Uint32>(y);
    down.button.clicks = 1;
    SDL_PushEvent(&down);

    // Schedule the release for the next pump() call (one-pump gap)
    m_pendingRelease = {true, x, y, button};
}

void UITestDriver::pressKey(SDL_Keycode kc) {
    SDL_Event down{};
    down.type = SDL_KEYDOWN;
    down.key.keysym.sym = kc;
    down.key.keysym.scancode = SDL_GetScancodeFromKey(kc);
    down.key.state = SDL_PRESSED;
    SDL_PushEvent(&down);

    SDL_Event up{};
    up.type = SDL_KEYUP;
    up.key.keysym.sym = kc;
    up.key.keysym.scancode = SDL_GetScancodeFromKey(kc);
    up.key.state = SDL_RELEASED;
    SDL_PushEvent(&up);
}

void UITestDriver::typeText(const std::string& t) {
    // Push SDL_TEXTINPUT events, one per (multi-byte) character in the string.
    // For ASCII text this pushes one event per character.
    // SDL expects UTF-8 text. We chunk the string naively; for non-ASCII text
    // this may need to be smarter but is fine for IMP name entry etc.
    for (size_t i = 0; i < t.size(); ) {
        // Determine UTF-8 sequence length from the leading byte
        unsigned char c = static_cast<unsigned char>(t[i]);
        int seqLen = 1;
        if      ((c & 0xF8) == 0xF0) seqLen = 4; // 4-byte
        else if ((c & 0xF0) == 0xE0) seqLen = 3; // 3-byte
        else if ((c & 0xE0) == 0xC0) seqLen = 2; // 2-byte

        std::string ch = t.substr(i, seqLen);
        SDL_Event e{};
        e.type = SDL_TEXTINPUT;
        std::strncpy(e.text.text, ch.c_str(), sizeof(e.text.text) - 1);
        e.text.text[sizeof(e.text.text) - 1] = '\0';
        SDL_PushEvent(&e);
        i += seqLen;
    }
}

// ---------------------------------------------------------------------------
// ScreenBuffer pixel reading
// ---------------------------------------------------------------------------

bool UITestDriver::readPixel(int x, int y, Uint8& r, Uint8& g, Uint8& b) {
    SDL_Surface* sb = GetScreenBufferForTest();
    if (!sb) {
        SLOGE("[uitest] ScreenBuffer not available for pixel read");
        return false;
    }

    // Bounds check
    if (x < 0 || x >= sb->w || y < 0 || y >= sb->h) {
        SLOGE("[uitest] readPixel({},{}) out of bounds ({},{})", x, y, sb->w, sb->h);
        return false;
    }

    if (SDL_LockSurface(sb) != 0) {
        SLOGE("[uitest] SDL_LockSurface failed: {}", SDL_GetError());
        return false;
    }

    Uint8* p = static_cast<Uint8*>(sb->pixels)
               + y * sb->pitch
               + x * sb->format->BytesPerPixel;
    Uint32 raw = 0;
    switch (sb->format->BytesPerPixel) {
    case 1: raw = *p; break;
    case 2: raw = *reinterpret_cast<Uint16*>(p); break;
    case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        raw = (p[0] << 16) | (p[1] << 8) | p[2];
#else
        raw = p[0] | (p[1] << 8) | (p[2] << 16);
#endif
        break;
    case 4: raw = *reinterpret_cast<Uint32*>(p); break;
    }

    SDL_GetRGB(raw, sb->format, &r, &g, &b);
    SDL_UnlockSurface(sb);
    return true;
}

void UITestDriver::assertPixel(int x, int y, const UITestPixelTarget& target) {
    Uint8 r = 0, g = 0, b = 0;
    if (!readPixel(x, y, r, g, b)) {
        // readPixel already logged the error
        SLOGE("[uitest] [FAIL] pixel({},{}) - could not read (expected {}, tol {})",
              x, y, hexColor(target.r, target.g, target.b), target.tolerance);
        ++m_failures;
        return;
    }

    auto diff = [](Uint8 a, Uint8 b) -> int { return a > b ? a - b : b - a; };
    bool ok = diff(r, target.r) <= target.tolerance &&
              diff(g, target.g) <= target.tolerance &&
              diff(b, target.b) <= target.tolerance;

    if (ok) {
        SLOGI("[uitest] [PASS] pixel({},{}) == {} (got {}, tol {})",
              x, y,
              hexColor(target.r, target.g, target.b),
              hexColor(r, g, b),
              target.tolerance);
    } else {
        SLOGE("[uitest] [FAIL] pixel({},{}) expected {}, got {}, tol {}",
              x, y,
              hexColor(target.r, target.g, target.b),
              hexColor(r, g, b),
              target.tolerance);
        ++m_failures;
    }
}

void UITestDriver::screenshot(const std::string& path) {
    SDL_Surface* sb = GetScreenBufferForTest();
    if (!sb) {
        SLOGE("[uitest] screenshot: ScreenBuffer not available");
        return;
    }

    // Derive basename by stripping any existing extension
    std::string base = path;
    std::string ext;
    auto dot = base.rfind('.');
    if (dot != std::string::npos && dot > base.rfind('/')) {
        ext = base.substr(dot);
        base = base.substr(0, dot);
    }

    // 1. BMP (always available via SDL)
    std::string bmpPath = base + ".bmp";
    SDL_SaveBMP(sb, bmpPath.c_str());
    SLOGI("[uitest] screenshot saved to {}", bmpPath);

    // 2. JPEG (optimized for quick visual review by AI agents)
    if (SDL_LockSurface(sb) != 0) {
        SLOGE("[uitest] screenshot: SDL_LockSurface failed: {}", SDL_GetError());
        return;
    }

    // Convert surface pixels to RGB24 for stb
    int w = sb->w, h = sb->h;
    std::vector<unsigned char> rgb(w * h * 3);
    for (int y = 0; y < h; ++y) {
        Uint8* row = static_cast<Uint8*>(sb->pixels) + y * sb->pitch;
        for (int x = 0; x < w; ++x) {
            Uint32 raw = 0;
            switch (sb->format->BytesPerPixel) {
            case 1: raw = row[x]; break;
            case 2: raw = reinterpret_cast<Uint16*>(row)[x]; break;
            case 4: raw = reinterpret_cast<Uint32*>(row)[x]; break;
            default: {
                Uint8* p = row + x * sb->format->BytesPerPixel;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                raw = (p[0] << 16) | (p[1] << 8) | p[2];
#else
                raw = p[0] | (p[1] << 8) | (p[2] << 16);
#endif
                break;
            }
            }
            Uint8 r, g, b;
            SDL_GetRGB(raw, sb->format, &r, &g, &b);
            size_t idx = static_cast<size_t>(y) * w * 3 + static_cast<size_t>(x) * 3;
            rgb[idx + 0] = r;
            rgb[idx + 1] = g;
            rgb[idx + 2] = b;
        }
    }
    SDL_UnlockSurface(sb);

    std::string jpgPath = base + ".jpg";
    stbi_write_jpg(jpgPath.c_str(), w, h, 3, rgb.data(), 85);
    SLOGI("[uitest] screenshot saved to {} ({}x{}, quality 85)", jpgPath, w, h);
}