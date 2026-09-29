#pragma once

#define SOL_CHECK_ARGUMENTS 1
#define SOL_PRINT_ERRORS 0
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <string>
#include <vector>

/** @file
 * The `ja2` Lua API used by automation scripts and by the TCP server.
 * It lives in its own Lua state, separate from the modding scripts.
 */
namespace Automation
{
	/** What went wrong in the most recent failed API call. */
	enum class FailureKind { None, Script, Expectation, Timeout, Crash, Exited };

	sol::state& Lua();

	/** Create the Lua state and the `ja2` table; require() also searches @a paths. */
	void InitLua(std::vector<std::string> const& paths);

	/** Kind of the error raised by the last API call that failed. */
	FailureKind LastFailure();

	/** Soft failures recorded with ja2.check(). */
	int CheckFailures();

	/** Translate a legacy line-based e2e script (.txt) to Lua. */
	std::string TranslateLegacyScript(std::string const& source, std::string& error);

	/** Serve JSON requests on a TCP port; returns the process exit code. */
	int RunServer(std::string const& port, std::string const& sessionFile);
}
