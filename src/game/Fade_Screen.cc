#include "HImage.h"
#include "Timer_Control.h"
#include "Sys_Globals.h"
#include "Fade_Screen.h"
#include "SysUtil.h"
#include "Cursor_Control.h"
#include "Music_Control.h"
#include "Render_Dirty.h"
#include "GameLoop.h"
#include "VObject.h"
#include "Video.h"
#include "VSurface.h"
#include "UILayout.h"


enum FadeType : INT8 {
	FADE_OUT_REALFADE,
	FADE_IN_REALFADE
};

static ScreenID guiExitScreen;
BOOLEAN gfFadeInitialized = FALSE;
INT16   gsFadeLimit;
UINT32  guiTime;
UINT32  guiFadeDelay;
BOOLEAN gfFirstTimeInFade = FALSE;
INT16   gsFadeCount;
static FadeType gbFadeType;
INT16   gsFadeRealCount;

using FADE_FUNCTION = void (*)();
static FADE_FUNCTION gFadeFunction = NULL;

FADE_HOOK gFadeInDoneCallback  = NULL;
FADE_HOOK gFadeOutDoneCallback = NULL;


BOOLEAN gfFadeIn      = FALSE;
BOOLEAN gfFadeOut     = FALSE;
BOOLEAN gfFadeOutDone = FALSE;
BOOLEAN gfFadeInDone  = FALSE;


void FadeInNextFrame( )
{
	gfFadeIn = TRUE;
	gfFadeInDone = FALSE;
}

void FadeOutNextFrame( )
{
	gfFadeOut = TRUE;
	gfFadeOutDone = FALSE;
}


static void BeginFade(ScreenID uiExitScreen, FadeType bType, UINT32 uiDelay);


BOOLEAN HandleBeginFadeIn(ScreenID const uiScreenExit)
{
	if ( gfFadeIn )
	{
		BeginFade(uiScreenExit, FADE_IN_REALFADE, 5);

		gfFadeIn = FALSE;

		gfFadeInDone = TRUE;

		return( TRUE );
	}

	return( FALSE );
}

BOOLEAN HandleBeginFadeOut(ScreenID const uiScreenExit)
{
	if ( gfFadeOut )
	{
		BeginFade(uiScreenExit, FADE_OUT_REALFADE, 5);

		gfFadeOut = FALSE;

		gfFadeOutDone = TRUE;

		return( TRUE );
	}

	return( FALSE );
}


BOOLEAN HandleFadeOutCallback( )
{
	if ( gfFadeOutDone )
	{
		gfFadeOutDone = FALSE;

		if ( gFadeOutDoneCallback != NULL )
		{
			gFadeOutDoneCallback( );

			gFadeOutDoneCallback = NULL;

			return( TRUE );
		}
	}

	return( FALSE );
}


BOOLEAN HandleFadeInCallback( )
{
	if ( gfFadeInDone )
	{
		gfFadeInDone = FALSE;

		if ( gFadeInDoneCallback != NULL )
		{
			gFadeInDoneCallback( );
		}

		gFadeInDoneCallback = NULL;

		return( TRUE );
	}

	return( FALSE );
}


static void FadeFrameBufferRealFade(void);
static void FadeInFrameBufferRealFade(void);


static void BeginFade(ScreenID const uiExitScreen, FadeType const bType, UINT32 const uiDelay)
{
	//Init some paramters
	guiExitScreen	= uiExitScreen;
	guiFadeDelay			= uiDelay;
	gfFadeIn = FALSE;

	// Calculate step;
	switch (bType)
	{
		case FADE_IN_REALFADE:
			gsFadeRealCount = -1;
			gsFadeLimit			= 8;
			gFadeFunction = FadeInFrameBufferRealFade;

			BltVideoSurface(guiSAVEBUFFER, FRAME_BUFFER, 0, 0, NULL);
			FRAME_BUFFER->Fill(Get16BPPColor(FROMRGB(0, 0, 0)));
			break;

		case FADE_OUT_REALFADE:
			gsFadeRealCount = -1;
			gsFadeLimit			= 10;
			gFadeFunction = FadeFrameBufferRealFade;
			break;
	}

	gfFadeInitialized = TRUE;
	gfFirstTimeInFade = TRUE;
	gsFadeCount				= 0;
	gbFadeType						= bType;

	SetPendingNewScreen(FADE_SCREEN);
}


ScreenID FadeScreenHandle()
{
	UINT32 uiTime;

	if ( !gfFadeInitialized )
	{
		SET_ERROR( "Fade Screen called but not intialized " );
		return( ERROR_SCREEN );
	}

	// ATE: Remove cursor
	SetCurrentCursorFromDatabase( VIDEO_NO_CURSOR );


	if ( gfFirstTimeInFade )
	{
		gfFirstTimeInFade = FALSE;

		// Calcuate delay
		guiTime = GetJA2Clock( );
	}

	// Get time
	uiTime = GetJA2Clock( );

	MusicPoll();

	if ( ( uiTime - guiTime ) > guiFadeDelay )
	{
		InvalidateScreen();
		gFadeFunction();

		gsFadeCount++;

		if ( gsFadeCount > gsFadeLimit )
		{
			switch( gbFadeType )
			{
				case FADE_OUT_REALFADE:
					FRAME_BUFFER->Fill(Get16BPPColor(FROMRGB(0, 0, 0)));
					break;
				case FADE_IN_REALFADE:
					break;
			}

			//End!
			gfFadeInitialized = FALSE;
			gfFadeIn = FALSE;

			return( guiExitScreen );
		}
	}

	return( FADE_SCREEN );
}


static void FadeFrameBufferRealFade(void)
{
	if ( gsFadeRealCount != gsFadeCount )
	{
		FRAME_BUFFER->ShadowRectUsingLowPercentTable(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

		gsFadeRealCount = gsFadeCount;
	}

}


static void FadeInFrameBufferRealFade(void)
{
	INT32 cnt;

	if ( gsFadeRealCount != gsFadeCount )
	{

		for ( cnt = 0; cnt < ( gsFadeLimit - gsFadeCount ); cnt++ )
		{
			FRAME_BUFFER->ShadowRectUsingLowPercentTable(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
		}

		VideoPresentFrame();

		// Copy save buffer back
		RestoreExternBackgroundRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

		gsFadeRealCount = gsFadeCount;
	}

}
