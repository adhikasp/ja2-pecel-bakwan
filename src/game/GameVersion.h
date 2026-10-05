#ifndef _GAME_VERSION_H_
#define _GAME_VERSION_H_

#include "Types.h"

#include <string>

//
//	Keeps track of the game version
//

extern const char g_version_label[];
extern const char g_version_number[16];

//
//	What the UI shows as the version.
//
//	CMakeLists.txt appends `git rev-parse --short HEAD` to the version, so this
//	label carries the build's commit — good for a bug report, and useless in a
//	golden screenshot: every commit would invalidate every screen that shows it.
//	Driven runs replace it (Automation's -version-label) for exactly that reason,
//	so prefer VersionLabel() over g_version_label at every call site.
//
std::string const& VersionLabel();

/** Automation only: replace the version the UI shows. An empty @a label restores
 *  the built-in one. */
void SetVersionLabelOverride(std::string label);


//
//		Keeps track of the saved game version.  Increment the saved game version whenever
//	you will invalidate the saved game file
//

constexpr UINT32 SAVE_GAME_VERSION = 106;

#endif
