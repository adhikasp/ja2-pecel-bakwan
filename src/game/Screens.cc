#include "Auto_Resolve.h"
#include "EditScreen.h"
#include "Epilogue.h"
#include "Fade_Screen.h"
#include "GameScreen.h"
#include "Intro.h"
#include "JAScreens.h"
#include "Laptop.h"
#include "LoadScreen.h"
#include "MainMenuScreen.h"
#include "MapScreen.h"
#include "MessageBoxScreen.h"
#include "Options_Screen.h"
#include "Quest_Debug_System.h"
#include "SaveLoadScreen.h"
#include "Screens.h"
#include "ScreenIDs.h"
#include "ShopKeeper_Interface.h"
#include "UiSpikeScreen.h"
#include "VideoOptionsScreen.h"


Screens const GameScreens[] =
{
	{ EditScreenInit,       EditScreenHandle,            EditScreenShutdown       },
	{ nullptr,                 nullptr,                        nullptr                     },
	{ nullptr,                 nullptr,                        nullptr                     },
	{ nullptr,                 ErrorScreenHandle,           nullptr                     }, // Title Screen
	{ nullptr,                 InitScreenHandle,            nullptr                     }, // Title Screen
	{ MainGameScreenInit,   MainGameScreenHandle,        MainGameScreenShutdown   },
	{ nullptr,                 nullptr,                        nullptr                     }, // was AniEditScreen
	{ nullptr,                 PalEditScreenHandle,         nullptr                     },
	{ nullptr,                 DebugScreenHandle,           nullptr                     },
	{ MapScreenInit,        MapScreenHandle,             MapScreenShutdown        },
	{ LaptopScreenInit,     LaptopScreenHandle,          LaptopScreenShutdown     },
	{ nullptr,                 LoadSaveScreenHandle,        nullptr                     },
	{ nullptr,              HandleScreen<MAPUTILITY_SCREEN>, nullptr              },
	{ nullptr,                 FadeScreenHandle,            nullptr                     },
	{ nullptr,                 MessageBoxScreenHandle,      MessageBoxScreenShutdown },
	{ nullptr,                 MainMenuScreenHandle,        nullptr                     },
	{ nullptr,                 AutoResolveScreenHandle,     nullptr                     },
	{ nullptr,                 SaveLoadScreenHandle,        nullptr                     },
	{ nullptr,                 OptionsScreenHandle,         nullptr                     },
	{ ShopKeeperScreenInit, ShopKeeperScreenHandle,      ShopKeeperScreenShutdown },
	{ nullptr,                 SexScreenHandle,             nullptr                     },
	{ nullptr,              HandleScreen<GAME_INIT_OPTIONS_SCREEN>, nullptr       },
	{ nullptr,                 nullptr,                        nullptr                     },
	{ nullptr,                 IntroScreenHandle,           nullptr                     },
	{ nullptr,              HandleScreen<CREDIT_SCREEN>, nullptr                  },
	{ QuestDebugScreenInit, QuestDebugScreenHandle,      nullptr                     },
	{ nullptr,                 VideoOptionsScreenHandle,    nullptr                     },
	{ nullptr,              UiSpikeScreenHandle,         nullptr                  },
	{ nullptr,              EpilogueScreenHandle,        nullptr                  }
};


#ifdef WITH_UNITTESTS
#include "gtest/gtest.h"

TEST(Screens, asserts)
{
	EXPECT_EQ(lengthof(GameScreens), MAX_SCREENS);
}

#endif
