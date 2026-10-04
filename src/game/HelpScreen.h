#ifndef _HELP_SCREEN__H_
#define _HELP_SCREEN__H_

#include "Types.h"

#include <string>
#include <vector>

//enum used for the different help screens that can come up
enum HelpScreenID
{
	HELP_SCREEN_NONE = -1,
	HELP_SCREEN_LAPTOP,
	HELP_SCREEN_MAPSCREEN,
	HELP_SCREEN_MAPSCREEN_NO_ONE_HIRED,
	HELP_SCREEN_MAPSCREEN_NOT_IN_ARULCO,
	HELP_SCREEN_MAPSCREEN_SECTOR_INVENTORY,
	HELP_SCREEN_TACTICAL,
	HELP_SCREEN_OPTIONS,
	HELP_SCREEN_LOAD_GAME,

	HELP_SCREEN_NUMBER_OF_HELP_SCREENS,
};


struct HELP_SCREEN_STRUCT
{
	HelpScreenID bCurrentHelpScreen;
	UINT32	uiFlags;

	UINT16	usHasPlayerSeenHelpScreenInCurrentScreen;

	UINT8		ubHelpScreenDirty;

	UINT16	usScreenLocX;
	UINT16	usScreenLocY;
	UINT16	usScreenWidth;
	UINT16	usScreenHeight;

	INT32		iLastMouseClickY;			//last position the mouse was clicked ( if != -1 )

	INT8		bCurrentHelpScreenActiveSubPage;  //used to keep track of the current page being displayed

	INT8		bNumberOfButtons;

	//used so if the user checked the box to show the help, it doesnt automatically come up every frame
	BOOLEAN	fHaveAlreadyBeenInHelpScreenSinceEnteringCurrenScreen;

	INT8		bDelayEnteringHelpScreenBy1FrameCount;
	UINT16	usLeftMarginPosX;

	UINT16	usCursor;

	BOOLEAN	fWasTheGamePausedPriorToEnteringHelpScreen;



	//scroll variables
	UINT16	usTotalNumberOfPixelsInBuffer;
	UINT32	uiLineAtTopOfTextBuffer;
	UINT16	usTotalNumberOfLinesInBuffer;
	BOOLEAN	fForceHelpScreenToComeUp;
};

extern HELP_SCREEN_STRUCT gHelpScreen;


BOOLEAN ShouldTheHelpScreenComeUp(HelpScreenID, BOOLEAN fForceHelpScreenToComeUp);
void HelpScreenHandler(void);
void InitHelpScreenSystem(void);
void NewScreenSoResetHelpScreen(void);
HelpScreenID HelpScreenDetermineWhichMapScreenHelpToShow();

// ---- what the native help overlay draws (NativeUI/MapScreenNative.cc, docs/ui/mapscreen.md) -----------
// A read-only snapshot plus the commands the native buttons send, so the native and the legacy screen run
// exactly the same game code. The legacy help screen keeps running underneath (input, pausing, exit).
struct HelpPageInfo
{
	std::string              button;      // the page button's label (empty when the screen has no buttons)
	std::vector<std::string> paragraphs;  // the page's paragraphs, in order
};

struct HelpScreenView
{
	bool                     active = false;
	int                      page = 0, page_count = 0;
	bool                     force = false;    // fForceHelpScreenToComeUp: no "don't show again"
	std::string              title, subtitle, footer;
	std::vector<HelpPageInfo> pages;
	bool                     dont_show = false; // the "don't show help again" toggle is checked
};

/** The help screen now, or active == false when none is up. */
HelpScreenView GetHelpScreenView();

/** The page button (ChangeToHelpScreenSubPage), clamped like the legacy callbacks. */
void HelpScreenSelectPage(int page);
/** Closes the help screen (the legacy Esc / exit button): acted on by HelpScreenHandler next frame. */
void HelpScreenClose();
/** Toggles the "don't show help again" check box (no-op when the toggle is hidden). */
void HelpScreenToggleDontShow();

#endif
