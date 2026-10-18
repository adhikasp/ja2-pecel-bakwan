#include "JA2_Splash.h"
#include "VSurface.h"
#include "Timer_Control.h"
#include "MainMenuScreen.h"
#include "Video.h"
#include "GameRes.h"
#include "Logger.h"

UINT32 guiSplashFrameFade = 10;
UINT32 guiSplashStartTime = 0;


//Simply create videosurface, load image, and draw it to the screen.
void InitJA2SplashScreen(bool const showSplash)
{
	InitializeJA2Clock();

	// The splash is per-language art. A run that asked for no intro must not load
	// it: on an install without that language's directory the file is absent and
	// the load aborts the process before the main menu (#411).
	if (showSplash)
	{
		if (isEnglishVersion() || isChineseVersion())
		{
			ClearMainMenu();
		}
		else
		{
			const char* const ImageFile = GetMLGFilename(MLG_SPLASH);
			SLOGI("Splash screen: {}", ImageFile);
			BltVideoSurfaceOnceWithStretch(FRAME_BUFFER, ImageFile);
		}
	}

	InvalidateScreen();
	RefreshScreen();

	guiSplashStartTime = GetJA2Clock();
}
