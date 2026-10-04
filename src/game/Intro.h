#ifndef _INTRO__C_
#define _INTRO__C_

#include "IntroModel.h"
#include "Types.h"
#include "ScreenIDs.h"


ScreenID IntroScreenHandle(void);


//enums used for when the intro screen can come up, used with 'gbIntroScreenMode'
enum
{
	INTRO_BEGINING,			//set when viewing the intro at the begining of the game
	INTRO_ENDING,				//set when viewing the end game video.

	INTRO_SPLASH,
};


extern	UINT32	guiSmackerSurface;


void SetIntroType( INT8 bIntroType );

// The presentation the screen was asked for (SetIntroType). Somebody who asked for none gets the ending's rules,
// which is what the legacy screen has always done with its unset mode.
NativeUI::IntroModel::Kind GetIntroKind( void );

// Ends the presentation the way both UIs do it: the exit screen per mode, the Sir-Tech logo and its done flag on
// the splash. Returns the screen to go to (docs/ui/intro.md).
ScreenID PrepareToExitIntroScreen( void );

// Skip the splash and new-game intro videos (the end-game one still plays).
void SetSkipIntroVideos(bool skip);
bool SkipIntroVideos(void);

// Play the chain as still cards instead of the videos: the fallback for a data set without them, and what
// ja2.debug("intro", kind, "still") drives deterministic screenshots with.
void SetIntroStillCards(bool cards);
bool IntroStillCards(void);

#endif
