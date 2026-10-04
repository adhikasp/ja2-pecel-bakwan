#ifndef _EPILOGUE_H_
#define _EPILOGUE_H_

#include "ScreenIDs.h"

// The victory epilogue (docs/ui/epilogue.md): the campaign's last page, after the ending cinematic and before the
// credits. Where the native UI can run it is NativeUI/EpilogueNative.cc; this is its legacy handler, which has
// nothing to show — the screen is new content — and so does what the chain always did: restart the game and go on.

ScreenID EpilogueScreenHandle(void);

// Leaves the epilogue for @p next: restarts the campaign (what Intro.cc used to do when the ending cinematic
// finished; the epilogue reads the campaign first, so it happens here) and returns the next screen.
ScreenID LeaveEpilogue(ScreenID next);

#endif
