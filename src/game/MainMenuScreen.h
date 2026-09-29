#ifndef MAINMENUSCREEN_H
#define MAINMENUSCREEN_H

#include "ScreenIDs.h"


void InitMainMenu(void);
void ClearMainMenu(void);

ScreenID MainMenuScreenHandle(void);

// True once the splash has faded and the menu is drawn and accepting input.
bool MainMenuIsReady();

/** The screen has another size: tear the menu down so that it is built again, in the new layout, in the next frame. */
void RelayoutMainMenu();

#endif
