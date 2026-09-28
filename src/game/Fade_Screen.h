#ifndef FADE_SCREEN_H
#define FADE_SCREEN_H

#include "ScreenIDs.h"
#include "Types.h"

typedef void (*FADE_HOOK)( void );

extern FADE_HOOK gFadeInDoneCallback;
extern FADE_HOOK gFadeOutDoneCallback;


extern BOOLEAN       gfFadeInitialized;
extern BOOLEAN       gfFadeIn;
extern BOOLEAN       gfFadeOut;

BOOLEAN HandleBeginFadeIn(ScreenID uiScreenExit);
BOOLEAN HandleBeginFadeOut(ScreenID uiScreenExit);

BOOLEAN HandleFadeOutCallback(void);
BOOLEAN HandleFadeInCallback(void);

void FadeInNextFrame(void);
void FadeOutNextFrame(void);

ScreenID FadeScreenHandle(void);

#endif
