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

/* ---- The native save/load screen (src/game/NativeUI/SaveLoadScreen.cc) uses the same logic ---- */
class SaveGameInfo;
struct SAVED_GAME_HEADER;
extern BOOLEAN gfLoadGameUponEntry;
void SaveLoadNativeEnter();
/** @a handingOver: the legacy screen runs next (to load with its fades) and finishes the job. */
void SaveLoadNativeExit(bool handingOver);
/** Where Cancel/Esc goes (LeaveSaveLoadScreen). */
ScreenID SaveLoadLeaveTarget();
/** Loadable saves, newest first; for saving without the quick and auto saves. */
std::vector<SaveGameInfo> SaveLoadListSaves(bool forSaving);
double SaveLoadModifiedTime(const ST::string& saveName);
/** Bit 1: another game or save version; bit 2: other mods. */
int SaveLoadCompatibility(const SaveGameInfo&);
/** A new save's file name from the time and its description (SaveNewSave). */
ST::string SaveLoadNewFileName(const ST::string& description);
/** Saves (or, for a Dead is Dead new game, only names the save); @a exitTo is where to go then. */
bool SaveLoadNativeSave(const ST::string& saveName, const ST::string& description, ScreenID& exitTo);
/** Deletes a save and its thumbnail. */
bool SaveLoadNativeDelete(const ST::string& saveName);
/** The legacy "load upon entry" path loads @a saveName the next time the save/load screen runs. */
void SaveLoadArmLoadUponEntry(const ST::string& saveName);
bool SaveLoadLoadUponEntryArmed();
/** The sector a save was made in, as the save list shows it. */
ST::string SaveLoadSectorText(const SAVED_GAME_HEADER&);

#endif
