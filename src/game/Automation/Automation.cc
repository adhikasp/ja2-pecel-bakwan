#include "Automation.h"
#include "AutomationLua.h"
#include "AutomationSession.h"

#include "Clock.h"
#include "Headless.h"
#include "Logger.h"
#include "TextRegistry.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace Automation
{

namespace
{
	Options g_options;

	bool IsFlag(char const* arg, char const* name)
	{
		// Accept both -flag (like the other ja2 options) and --flag.
		if (arg[0] != '-') return false;
		char const* a = arg[1] == '-' ? arg + 2 : arg + 1;
		return std::strcmp(a, name) == 0;
	}

	void SetEnv(char const* name, std::string const& value)
	{
#ifdef _WIN32
		_putenv_s(name, value.c_str());
#else
		setenv(name, value.c_str(), 1);
#endif
	}

	void StartWatchdog(double const seconds)
	{
		if (seconds <= 0) return;
		std::thread([seconds] {
			std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
			std::fprintf(stderr, "automation: watchdog fired after %.0f s of wall-clock time\n", seconds);
			SLOGE("automation: watchdog fired after {} s of wall-clock time", seconds);
			std::fflush(stderr);
			std::_Exit(EXIT_TIMEOUT);
		}).detach();
	}

	int ExitCodeFor(FailureKind const kind)
	{
		switch (kind)
		{
			case FailureKind::Expectation: return EXIT_TEST_FAILED;
			case FailureKind::Timeout:     return EXIT_TIMEOUT;
			case FailureKind::Crash:       return EXIT_GAME_ERROR;
			default:                       return EXIT_SCRIPT_ERROR;
		}
	}

	int RunScript(std::string const& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			std::fprintf(stderr, "automation: cannot open script %s\n", path.c_str());
			return EXIT_SCRIPT_ERROR;
		}
		std::stringstream buffer;
		buffer << file.rdbuf();
		std::string source = buffer.str();

		if (std::filesystem::path(path).extension() == ".txt")
		{
			std::string error;
			source = TranslateLegacyScript(source, error);
			if (!error.empty())
			{
				std::fprintf(stderr, "automation: %s: %s\n", path.c_str(), error.c_str());
				return EXIT_SCRIPT_ERROR;
			}
		}

		SLOGI("[automation] running {}", path);
		// Run with debug.traceback as the message handler so that a failure
		// points at the line of the script that caused it.
		sol::load_result chunk = Lua().load(source, "@" + path);
		if (!chunk.valid())
		{
			sol::error const err = chunk;
			std::fprintf(stderr, "automation: %s\n", err.what());
			return EXIT_SCRIPT_ERROR;
		}
		sol::protected_function script = chunk;
		script.set_error_handler(Lua()["debug"]["traceback"]);
		sol::protected_function_result const result = script();

		if (!result.valid())
		{
			sol::error const err = result;
			int const code = ExitCodeFor(LastFailure());
			std::fprintf(stderr, "automation: %s FAILED (exit %d)\n%s\n", path.c_str(), code, err.what());
			SLOGE("[automation] {} failed: {}", path, err.what());
			try
			{
				// Leave evidence behind for whoever reads the failure.
				std::string const shot = Session::ResolveOutputPath("failure.png");
				Session::Screenshot(shot);
				std::fprintf(stderr, "automation: screen %s, screenshot %s\n", Session::ScreenName().c_str(), shot.c_str());
			}
			catch (...) {}
			return code;
		}

		if (CheckFailures() > 0)
		{
			std::fprintf(stderr, "automation: %s FAILED: %d check(s) failed\n", path.c_str(), CheckFailures());
			return EXIT_TEST_FAILED;
		}
		std::fprintf(stderr, "automation: %s passed (%llu frames, %llu ms game time)\n", path.c_str(),
			static_cast<unsigned long long>(Session::Frame()), static_cast<unsigned long long>(Session::ElapsedMs()));
		return EXIT_PASSED;
	}
}


char const* Usage()
{
	return
		"Automation (run the game driven by a script or another program):\n"
		"  -run FILE          run a Lua script (or a legacy .txt e2e script) and exit with its result\n"
		"  -serve PORT        serve line-delimited JSON requests on 127.0.0.1:PORT (0 = any free port)\n"
		"  -session-file F    with -serve: write {\"port\":..,\"pid\":..} to F once listening\n"
		"  -arg VALUE         pass VALUE to the script (ja2.args); repeatable\n"
		"  -lua-path DIR      also look for require()d modules in DIR; repeatable\n"
		"  -load SAVE         load this save game before running the script / serving\n"
		"  -seed N            seed the random number generator (default 1)\n"
		"  -show              show a window instead of running headless\n"
		"  -home DIR          use DIR as the configuration/save directory (like JA2_HOME)\n"
		"  -log FILE          write the log to FILE\n"
		"  -out DIR           directory for screenshots and other relative output paths\n"
		"  -no-intro          skip the splash screen and intro videos\n"
		"  -frame-ms MS       virtual time per frame (default 16.667)\n"
		"  -timeout S         kill the session after S seconds of wall-clock time\n"
		"Exit codes: 0 passed, 1 test failed, 2 script/setup error, 3 game error, 4 timeout.\n";
}


bool ParseCommandLine(int& argc, char** argv, Options& o, std::string& error)
{
	int dst = 1;
	for (int src = 1; src < argc; ++src)
	{
		char const* a = argv[src];
		auto value = [&](std::string& into) {
			if (src + 1 >= argc)
			{
				error = std::string(a) + " needs a value";
				return false;
			}
			into = argv[++src];
			return true;
		};
		std::string v;

		if (IsFlag(a, "run") || IsFlag(a, "uitest")) { if (!value(o.runScript)) return false; }
		else if (IsFlag(a, "serve"))        { if (!value(o.serve)) return false; }
		else if (IsFlag(a, "session-file")) { if (!value(o.sessionFile)) return false; }
		else if (IsFlag(a, "arg"))          { if (!value(v)) return false; o.scriptArgs.push_back(v); }
		else if (IsFlag(a, "lua-path"))     { if (!value(v)) return false; o.luaPaths.push_back(v); }
		else if (IsFlag(a, "load"))         { if (!value(o.load)) return false; }
		else if (IsFlag(a, "home"))         { if (!value(o.home)) return false; }
		else if (IsFlag(a, "log"))          { if (!value(o.logFile)) return false; }
		else if (IsFlag(a, "out"))          { if (!value(o.outDir)) return false; }
		else if (IsFlag(a, "show"))         { o.show = true; }
		else if (IsFlag(a, "headless"))     { o.show = false; }
		else if (IsFlag(a, "no-intro"))     { o.noIntro = true; }
		else if (IsFlag(a, "seed") || IsFlag(a, "frame-ms") || IsFlag(a, "timeout"))
		{
			if (!value(v)) return false;
			char* end = nullptr;
			double const d = std::strtod(v.c_str(), &end);
			if (end == v.c_str() || *end != '\0' || d < 0)
			{
				error = std::string(a) + ": not a valid number: " + v;
				return false;
			}
			if (IsFlag(a, "seed"))          o.seed = static_cast<uint32_t>(d);
			else if (IsFlag(a, "frame-ms")) o.frameMs = d;
			else                            o.timeoutS = d;
		}
		else
		{
			argv[dst++] = argv[src];
		}
	}
	argv[dst] = nullptr;
	argc = dst;

	bool const needsDriver = !o.load.empty() || o.noIntro || !o.sessionFile.empty() || !o.scriptArgs.empty();
	if (!o.Active() && needsDriver)
	{
		error = "-load, -no-intro, -arg and -session-file need -run or -serve";
		return false;
	}
	if (!o.runScript.empty() && !o.serve.empty())
	{
		error = "use either -run or -serve, not both";
		return false;
	}
	if (o.frameMs <= 0) o.frameMs = 1000.0 / 60;
	return true;
}


void PreInit(Options const& o)
{
	g_options = o;
	if (!o.home.empty())
	{
		std::error_code ec;
		std::filesystem::create_directories(o.home, ec);
		SetEnv("JA2_HOME", o.home);
	}
	if (!o.Active()) return;

	sgp::SetHeadless(o.Headless());
	sgp::Clock::EnableVirtual(std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::duration<double, std::milli>(o.frameMs)));
	TextRegistry::SetEnabled(true);
}


Options const& GetOptions() { return g_options; }


int Run()
{
	Options const& o = g_options;
	// The engine was seeded with this during start-up (see main()).
	uint32_t const seed = o.Seed();
	SLOGI("[automation] {} mode, seed {}", o.Headless() ? "headless" : "windowed", seed);
	StartWatchdog(o.timeoutS);

	std::vector<std::string> luaPaths;
	if (!o.runScript.empty())
	{
		luaPaths.push_back(std::filesystem::absolute(o.runScript).parent_path().generic_string());
	}
	for (auto const& p : o.luaPaths) luaPaths.push_back(std::filesystem::absolute(p).generic_string());
	InitLua(luaPaths);

	// Hand over a game that is ready for input: the loaded save, or else the
	// main menu once it is up.
	try
	{
		if (o.load.empty()) Session::WaitIdle(180'000);
	}
	catch (std::exception const& e)
	{
		std::fprintf(stderr, "automation: the game did not start: %s\n", e.what());
		return dynamic_cast<GameCrashedError const*>(&e) ? EXIT_GAME_ERROR : EXIT_TIMEOUT;
	}

	if (!o.load.empty())
	{
		try
		{
			Session::Load(o.load, 180'000);
		}
		catch (std::exception const& e)
		{
			std::fprintf(stderr, "automation: loading \"%s\" failed: %s\n", o.load.c_str(), e.what());
			return dynamic_cast<TimeoutError const*>(&e) ? EXIT_TIMEOUT :
			       dynamic_cast<GameCrashedError const*>(&e) ? EXIT_GAME_ERROR : EXIT_SCRIPT_ERROR;
		}
	}

	if (!o.runScript.empty()) return RunScript(o.runScript);
	return RunServer(o.serve, o.sessionFile);
}

}
