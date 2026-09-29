#ifndef _SAVE_LOAD_SCREEN__H_
#define _SAVE_LOAD_SCREEN__H_

#include "JA2Types.h"
#include "MessageBoxScreen.h"
#include "ScreenIDs.h"

#include <string_theory/string>
#include <vector>


//This flag is used to diferentiate between loading a game and saveing a game.
// gfSaveGame=TRUE	For saving a game
// gfSaveGame=FALSE	For loading a game
extern BOOLEAN gfSaveGame;

extern	BOOLEAN gfCameDirectlyFromGame;

ScreenID SaveLoadScreenHandle(void);

void DoSaveLoadMessageBox(const ST::string& str, ScreenID uiExitScreen, MessageBoxFlags, MSGBOX_CALLBACK ReturnCallback);

void DoQuickSave(void);
void DoAutoSave(void);
void DoDeadIsDeadSave(void);
void DoQuickLoad(void);

/* Load the save called @a saveName (file name without extension). From the
 * map screen or tactical this starts loading right away, like a quick load.
 * Elsewhere it arms the "load upon entry" path, which runs the next time the
 * save/load screen is entered (e.g. via ALT+C in the main menu).
 * Returns false if no such save exists. */
bool DoLoadSavedGameByName(const ST::string& saveName);

/* All loadable saves, newest first. */
std::vector<ST::string> GetLoadableSaveNames();

bool AreThereAnySavedGameFiles();

void DeleteSaveGameNumber(UINT8 save_slot_id);

#endif
