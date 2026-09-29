#ifndef MAINMENUSCREEN_H
#define MAINMENUSCREEN_H

#include "ScreenIDs.h"


void InitMainMenu(void);
void ClearMainMenu(void);

ScreenID MainMenuScreenHandle(void);

// True once the splash has faded and the menu is drawn and accepting input.
bool MainMenuIsReady();

#endif
