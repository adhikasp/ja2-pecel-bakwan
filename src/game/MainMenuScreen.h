#ifndef MAINMENUSCREEN_H
#define MAINMENUSCREEN_H

#include "ScreenIDs.h"


void InitMainMenu(void);
void ClearMainMenu(void);

ScreenID MainMenuScreenHandle(void);

// True once the splash has faded and the menu is drawn and accepting input.
bool MainMenuIsReady();

/** The splash and its fade (the legacy and the native menu both show it first); true while it runs. */
bool HandleMainMenuSplash();
/** The native main menu is showing (NativeUI). */
extern bool gfNativeMainMenuActive;
/** Removes the legacy menu's buttons (built at start-up) when the native menu shows instead. */
void RemoveLegacyMainMenu();

/** The screen has another size: tear the menu down so that it is built again, in the new layout, in the next frame. */
void RelayoutMainMenu();

#endif
