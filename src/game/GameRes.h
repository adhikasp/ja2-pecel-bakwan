#pragma once

/* Game resources */

#include "Types.h"
#include "StringEncodingTypes.h"
#include <string_theory/string>


/** List of supported game versions (localizations). */
using GameVersion = VanillaVersion;

enum MultiLanguageGraphic : int8_t
{
	MLG_AIMSYMBOL,
	MLG_BOBBYNAME,
	MLG_BOBBYRAYAD21,
	MLG_BOBBYRAYLINK,
	MLG_CLOSED,
	MLG_CONFIRMORDER,
	MLG_DESKTOP,
	MLG_FUNERALAD9,
	MLG_GOLDPIECEBUTTONS,
	MLG_HISTORY,
	MLG_IMPSYMBOL,
	MLG_INSURANCEAD10,
	MLG_INSURANCELINK,
	MLG_INSURANCETITLE, //LargeTitle
	MLG_LARGEFLORISTSYMBOL, //LargeSymbol
	MLG_LOADSAVEHEADER, //LoadScreenAddOns
	MLG_MCGILLICUTTYS,
	MLG_MORTUARY,
	MLG_MORTUARYLINK,
	MLG_OPTIONHEADER, //OptionScreenAddOns
	MLG_ORDERGRID,
	MLG_PREBATTLEPANEL,
	MLG_SMALLFLORISTSYMBOL, //SmallSymbol
	MLG_SMALLTITLE,
	MLG_SPLASH,
	MLG_STATSBOX,
	MLG_STOREPLAQUE,
	MLG_TITLETEXT,
	MLG_TOALUMNI,
	MLG_TOMUGSHOTS,
	MLG_TOSTATS,
	MLG_WARNING,
	MLG_YOURAD13
};

char const* GetMLGFilename(MultiLanguageGraphic);
/** The multilingual graphic's path for one game version, without asking the
 * file system. Split out of GetMLGFilename so the choice is unit-testable. */
char const* GetMLGFilenameForVersion(MultiLanguageGraphic, GameVersion);
/** The path a game version should load. `localisedPresent` is the caller's
 * answer to "does the path GetMLGFilenameForVersion() gives for @a version
 * exist on disk?"; this function does not touch the file system itself. When
 * it does not, the English graphic is used instead, as the string loader does
 * for a missing translation (#411). English runs are unaffected. */
char const* ResolveMLGFilename(MultiLanguageGraphic, GameVersion, bool localisedPresent);
// Shortcut for AddVideoObjectFromFile(GetMLGFilename(id))
SGPVObject* AddVideoObjectFromFile(MultiLanguageGraphic);

/** Object cache helper functions. */
SGPVObject * GetVObject(MultiLanguageGraphic);
bool RemoveVObject(MultiLanguageGraphic);
void BltVideoObject(SGPVSurface * dst, MultiLanguageGraphic, UINT16 subIndex, int x, int y);

/** Choose game version. */
void setGameVersion(GameVersion ver);
/** The chosen game version. */
GameVersion getGameVersion();

/** Check if this is English version of the game. */
bool isEnglishVersion();

/** Check if this is German version of the game. */
bool isGermanVersion();

/** Check if this is Polish version of the game. */
bool isPolishVersion();

/** Check if this is Russian version of the game. */
bool isRussianVersion();

/** Check if this is Russian GOLD version of the game. */
bool isRussianGoldVersion();

/** Check if this is Chinese version of the game. */
bool isChineseVersion();

/** Get major map version. */
FLOAT getMajorMapVersion();

/*************************************************************
 *
 ************************************************************/

/** Get current string encoding type. */
STRING_ENC_TYPE getStringEncType();
