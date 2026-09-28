#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/** @file
 * Driving the game from a script or another process.
 *
 *   ja2 -run script.lua [-load SAVE] [-seed N] [-show] ...   batch: run a Lua script, exit with its result
 *   ja2 -serve PORT [-session-file F] ...                   interactive: line-delimited JSON over TCP
 *
 * Both modes run the real game on a virtual clock (it only moves when the
 * driver steps frames), without a window or audio device unless -show is
 * given. See docs/automation.md.
 */
namespace Automation
{
	struct Options
	{
		std::string runScript;              // -run FILE (.lua, or legacy .txt)
		std::vector<std::string> scriptArgs; // -arg VALUE (repeatable) -> ja2.args
		std::vector<std::string> luaPaths;   // -lua-path DIR (repeatable): where require() looks
		std::string serve;                  // -serve PORT (0 = pick a free port)
		std::string sessionFile;            // -session-file FILE: written once listening
		bool        show = false;           // -show: open a window instead of running headless
		std::optional<uint32_t> seed;       // -seed N
		std::string home;                   // -home DIR: config/save directory for this session
		std::string logFile;                // -log FILE
		std::string load;                   // -load SAVE: load this save after boot
		bool        noIntro = false;        // -no-intro: skip the splash/intro videos
		double      frameMs = 1000.0 / 60;  // -frame-ms MS: virtual time per frame
		std::string outDir;                 // -out DIR: where relative artifact paths go
		double      timeoutS = 0;           // -timeout S: wall-clock watchdog (0 = none)

		bool Active() const { return !runScript.empty() || !serve.empty(); }
		uint32_t Seed() const { return seed.value_or(1); }
		bool Headless() const { return Active() && !show; }
	};

	/** Remove automation flags from argv (the Rust parser does not know them).
	 * Returns false and sets @a error on invalid usage. */
	bool ParseCommandLine(int& argc, char** argv, Options& out, std::string& error);

	/** Help text for the automation flags. */
	char const* Usage();

	/** Apply options that must be in place before the engine initialises
	 * (home directory, clock, random seed). */
	void PreInit(Options const&);

	Options const& GetOptions();

	/** Run the script or the server; called once the game is initialised.
	 * Returns the process exit code. */
	int Run();

	/** Process exit codes. */
	enum ExitCode : int
	{
		EXIT_PASSED       = 0,
		EXIT_TEST_FAILED  = 1, // an expectation in the script failed
		EXIT_SCRIPT_ERROR = 2, // bad script, bad arguments, missing save, ...
		EXIT_GAME_ERROR   = 3, // the game itself threw
		EXIT_TIMEOUT      = 4, // a wait timed out or the watchdog fired
	};
}
