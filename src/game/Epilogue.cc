// The victory epilogue's legacy handler (docs/ui/epilogue.md): the screen is new content — the old chain went
// straight from the ending videos to the credits — so where the native UI cannot run, the chain does exactly what
// it used to do: restart the game and go on to the credits.
#include "Epilogue.h"

#include "Game_Init.h"


ScreenID LeaveEpilogue(ScreenID const next)
{
	// What Intro.cc did when the ending cinematic finished. It happens here now because the epilogue reads the
	// campaign (days, sectors, kills, the roster) before it is reset.
	ReStartingGame();
	return next;
}


ScreenID EpilogueScreenHandle(void)
{
	return LeaveEpilogue(CREDIT_SCREEN);
}
