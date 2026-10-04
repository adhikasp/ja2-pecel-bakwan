#include "Button_System.h"
#include "FPS.h"
#include "GameLoop.h"
#include "GameSettings.h"
#include "Input.h"
#include "Intro.h"
#include "JA2_Splash.h"
#include "Random.h"
#include "SGP.h"
#include "NativeUI.h"
#include "SoundMan.h"
#include "VObject.h"
#include "Video.h"
#include "SDL3/SDL.h"
#include "Visualizer.h"
#include "UILayout.h"
#include "GameRes.h"
#include "GameMode.h"

#include "DefaultContentManager.h"
#include "GameInstance.h"
#include "Automation.h"
#include "Clock.h"
#include "Headless.h"
#include "VideoOptionsScreen.h"
#include "WorldRender.h"
#include "ModPackContentManager.h"
#include "policy/GamePolicy.h"
#include "RustInterface.h"
#include "EnumCodeGen.h"

#include "Logger.h"
#include <SDL3/SDL_events.h>
#include <iostream>

#ifdef WITH_UNITTESTS
#include "gtest/gtest.h"
#endif

#if defined _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <process.h>
#	include <typeinfo>
#	include "Local.h"
#else
#	include <unistd.h>
#endif

#ifdef __ANDROID__
#include "jni.h"
#include "SDL3/SDL_main.h"
#endif

#include <string_theory/format>

#include <chrono>
#include <cerrno>
#include <cstring>
#include <exception>
#include <locale>
#include <new>
#include <thread>
#include <utility>
using namespace std::chrono_literals;

extern BOOLEAN gfPauseDueToPlayerGamePause;

////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////

static BOOLEAN gfGameInitialized = FALSE;

/** Deinitialize the game an exit. */
static void shutdownGame()
{
	if (gfGameInitialized)
	{
		SLOGD("Shutting Down Game Manager");
		ShutdownGame();
	}

	SLOGD("Shutting Down Content Manager");
	delete GCM;
	GCM = NULL;

	SLOGD("Shutting Down Button System");
	ShutdownButtonSystem();
	MSYS_Shutdown();

	SLOGD("Shutting Down Sound Manager");
	ShutdownSoundManager();

	SLOGD("Shutting Down Video Surface Manager");
	ShutdownVideoSurfaceManager();
	SLOGD("Shutting Down Video Object Manager");
	ShutdownVideoObjectManager();
	SLOGD("Shutting Down Video Manager");
	ShutdownVideoManager();

	SLOGD("Shutting Down SDL");
	SDL_Quit();
}

/** Deinitialize the game an exit. */
static void deinitGameAndExit(int const exitCode = EXIT_SUCCESS)
{
	SLOGD("Deinitializing Game");
	// If we are in Dead is Dead mode, save before exit
	// Does this code also fire on crash? Let's hope not!
	DoDeadIsDeadSaveIfNecessary();

	shutdownGame();

	exit(exitCode);
}


/** Request game exit.
 * Call this function if you want to exit the game. */
void requestGameExit()
{
	SDL_Event event;
	event.type = SDL_EVENT_QUIT;
	SDL_PushEvent(&event);
}


/** Replaces this process with a fresh run of the same executable. The setup screen calls for it after it writes a
 *  configuration that only takes effect on a restart (game directory, resource version, mods). Only returns when the
 *  new process could not be started. */
static void RelaunchProcess(int const argc, char* argv[])
{
	if (argc <= 0 || argv == nullptr || argv[0] == nullptr) return;
#if defined(_WIN32)
	_execv(argv[0], argv);
#else
	execv(argv[0], argv);
#endif
	SLOGE("could not relaunch {}: {}", argv[0], std::strerror(errno));
}


void UpdateJA2Clock();

namespace sgp
{

void DispatchInputEvent(SDL_Event const& event)
{
	switch (event.type)
	{
		case SDL_EVENT_KEY_DOWN:   KeyDown(&event.key);   break;
		case SDL_EVENT_KEY_UP:     KeyUp(  &event.key);   break;
		case SDL_EVENT_TEXT_INPUT: TextInput(&event.text); break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN: MouseButtonDown(&event.button); break;
		case SDL_EVENT_MOUSE_BUTTON_UP:   MouseButtonUp(&event.button);   break;
		case SDL_EVENT_MOUSE_MOTION:      MouseMove(&event.motion);       break;
		case SDL_EVENT_MOUSE_WHEEL:       MouseWheelScroll(&event.wheel); break;

		case SDL_EVENT_FINGER_MOTION: FingerMove(&event.tfinger); break;
		case SDL_EVENT_FINGER_UP:     FingerUp(&event.tfinger);   break;
		case SDL_EVENT_FINGER_DOWN:   FingerDown(&event.tfinger); break;

		default: break;
	}
}


static bool g_quitRequested = false;

bool QuitRequested() { return g_quitRequested; }

bool StepFrame()
{
	sgp::Clock::BeginFrame();

	// Only system events are taken from SDL: a driven session gets its input
	// exclusively from the driver, so a stray real mouse or keyboard (when a
	// window is shown) cannot make a run non-reproducible.
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_EVENT_QUIT) g_quitRequested = true;
		else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ) VideoNotifyWindowChanged();
	}

	if (!g_quitRequested)
	{
		::UpdateJA2Clock();
		FPS::GameLoopPtr();
	}

	sgp::Clock::EndFrame();
	return !g_quitRequested;
}

}


static void MainLoop()
{
	bool s_doGameCycles{true};

	while (true)
	{
		// cycle until SDL_Quit is received
		extern void UpdateJA2Clock();
		UpdateJA2Clock();

		SDL_Event event;
		if (SDL_PollEvent(&event))
		{
			// Convert mouse and finger event coordinates from window
			// coordinates to render logical coordinates (accounts for
			// letterboxing, e.g. on Android's portrait orientation)
			switch (event.type) {
				case SDL_EVENT_MOUSE_MOTION:
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
				case SDL_EVENT_MOUSE_BUTTON_UP:
				case SDL_EVENT_FINGER_MOTION:
				case SDL_EVENT_FINGER_DOWN:
				case SDL_EVENT_FINGER_UP:
					VideoConvertEventToCanvas(event);
					break;
				default:
					break;
			}

			switch (event.type)
			{
				case SDL_EVENT_WILL_ENTER_BACKGROUND:
					s_doGameCycles = false;
					break;

				case SDL_EVENT_WILL_ENTER_FOREGROUND:
					s_doGameCycles = true;
					break;

				case SDL_EVENT_KEY_DOWN:
					if (event.key.key == SDLK_F &&
					    SDL_GetModState() & SDL_KMOD_CTRL)
					{
						FPS::ToggleOnOff();
					}
					else if (event.key.key == SDLK_C && SDL_GetModState() & SDL_KMOD_CTRL)
					{
						Visualizer::ToggleOnOff();
					}
					else
					{
						KeyDown(&event.key);
					}
					break;

				case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
				case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
				case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
					// A resized window or another display: with an automatic UI scale the canvas follows
					VideoNotifyWindowChanged();
					sgp::DispatchInputEvent(event);
					break;

				case SDL_EVENT_QUIT: deinitGameAndExit(); break;

				default: sgp::DispatchInputEvent(event); break;
			}
		}
		else
		{
			if (s_doGameCycles)
			{
				// Aim to execute the game loop at a rate of 144Hz,
				// once every ~6944 microseconds.
				constexpr auto targetResolution = 1'000'000us / 144;
				auto const beforeGameLoop = std::chrono::steady_clock::now();
				FPS::GameLoopPtr();

				// If the game loop took longer than 6944ms, this call does nothing.
				std::this_thread::sleep_until(beforeGameLoop + targetResolution);
			}
			else
			{
				SDL_WaitEvent(NULL);
			}
		}
	}
}

////////////////////////////////////////////////////////////

ContentManager *GCM = NULL;

////////////////////////////////////////////////////////////

/// Sets the C/C++ locale.
/// @return true if successful, false otherwise
[[maybe_unused]] static bool SetGlobalLocale(const char* name)
{
	try
	{
		std::locale::global(std::locale(name));
		return true;
	}
	catch(...)
	{
		return false;
	}
}

/// Tries to set the C/C++ locale to something that supports unicode.
///
/// There is no way to query available locales so you can only try them.
///
/// @return List of problems.
std::vector<ST::string> InitGlobalLocale()
{
	std::vector<ST::string> problems;

#ifdef _WIN32
	// In windows the console is a special device that accepts CP_UTF8, but needs a true type font to display it.
	if (!SetConsoleOutputCP(CP_UTF8))
	{
		problems.emplace_back(std::move(ST::format("SetConsoleOutputCP(CP_UTF8) failed, using output code page {}", GetConsoleOutputCP())));
	}
	if (!SetConsoleCP(CP_UTF8))
	{
		problems.emplace_back(std::move(ST::format("SetConsoleCP(CP_UTF8) failed, using input code page {}", GetConsoleCP())));
	}

	// Ensure quick-edit mode is off, or else it will block execution
	HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
	SetConsoleMode(hInput, ENABLE_EXTENDED_FLAGS);
#endif

#ifdef WITH_CUSTOM_LOCALE
	if (!SetGlobalLocale(WITH_CUSTOM_LOCALE))
	{
		problems.emplace_back(std::move(ST::format("failed to set custom locale '{}'", WITH_CUSTOM_LOCALE)));
	}
	else
	{
		return problems; // the custom locale is set, assume unicode locale (no way to test)
	}
#endif

#ifndef _WIN32
	// Windows does not support the utf8 locale (".65001") or the LC_* environment variables.
	// CP_UTF8 (65001) is a pseudo code page that does not have a nls file.
	// With VS2003 setlocale would accept it but the CRT APIs would fail, now it fails directly.
	//
	// According to https://www.python.org/dev/peps/pep-0538/
	// *nix have one of "C.UTF-8", "C.utf8" or "UTF-8".
	// Mac OS X and other *BSD systems have a partial UTF-8 locale that only defines the LC_CTYPE category.

	// set locale from the process environment
	if (!SetGlobalLocale(""))
	{
		problems.emplace_back("failed to set locale from the process environment");
	}

	if (!setlocale(LC_CTYPE, "C.UTF-8") && !setlocale(LC_CTYPE, "C.utf8") && !setlocale(LC_CTYPE, "UTF-8"))
	{
		problems.emplace_back(ST::format("failed to set unicode ctype for locale '{}', using ctype '{}'", setlocale(LC_ALL, nullptr), setlocale(LC_CTYPE, nullptr)));
	}
#endif

	return problems;
}

int main(int argc, char* argv[])
{
    try {
		#ifdef __ANDROID__
		JNIEnv* jniEnv = (JNIEnv*)SDL_GetAndroidJNIEnv();

		if (setGlobalJniEnv(jniEnv) == FALSE) {
			auto rustError = getRustError();
			if (rustError != NULL) {
				SLOGE("Failed to set global JNI env for Android: {}", rustError);
			}
			return EXIT_FAILURE;
		}
		#endif

		// Automation flags (-run, -serve, -headless, ...) are not known to the
		// Rust CLI parser, so take them out of argv first. They also decide
		// where the log goes and which home directory is used.
		Automation::Options automation;
		{
			std::string error;
			if (!Automation::ParseCommandLine(argc, argv, automation, error))
			{
				std::cerr << "ja2: " << error << "\n\n" << Automation::Usage();
				return Automation::EXIT_SCRIPT_ERROR;
			}
			for (int i = 1; i < argc; ++i)
			{
				if (std::strcmp(argv[i], "-help") == 0 || std::strcmp(argv[i], "--help") == 0)
				{
					std::cout << Automation::Usage() << "\n";
				}
			}
			Automation::PreInit(automation);
		}

		// init locale and logging
		{
			std::vector<ST::string> problems = InitGlobalLocale();
			Logger_initialize(automation.logFile.empty() ? "ja2.log" : automation.logFile.c_str());
			for (const ST::string& msg : problems)
			{
				SLOGW("{}", msg);
			}
		}

		RustPointer<char> configFolderPath(EngineOptions_getPecelBakwanHome());
		if (configFolderPath.get() == NULL) {
			auto rustError = getRustError();
			if (rustError != NULL) {
				SLOGE("Failed to find home directory: {}", rustError);
			}
			return EXIT_FAILURE;
		}

		RustPointer<EngineOptions> params(EngineOptions_create(configFolderPath.get(), argv, argc));
		if (params == NULL) {
			return EXIT_FAILURE;
		}

		if (EngineOptions_shouldShowHelp(params.get())) {
			return EXIT_SUCCESS;
		}

		if (EngineOptions_shouldRunEnumGen(params.get())) {
			PrintAllJA2Enums(std::cout);
			return EXIT_SUCCESS;
		}

		if (EngineOptions_shouldStartWithoutSound(params.get())) {
			SoundEnableSound(FALSE);
		}

		if (EngineOptions_shouldStartInDebugMode(params.get())) {
			Logger_setLevel(LogLevel::Debug);
			GameMode::getInstance()->setDebugging(true);
		}

		if (EngineOptions_shouldRunEditor(params.get())) {
			GameMode::getInstance()->setEditorMode(false);
		}

		{
			// ja2.json "world_renderer", JA2_WORLD_RENDERER wins (docs/plan/native-modern-game.md, Phase 8)
			RustPointer<char> wr(EngineOptions_getWorldRenderer(params.get()));
			std::string setting = wr.get() ? wr.get() : "";
			if (char const* env = std::getenv("JA2_WORLD_RENDERER")) setting = env;
			WorldRendererKind const kind = setting.empty() ? WorldRendererDefault(sgp::IsHeadless() || automation.Active()) : ParseWorldRenderer(setting);
			bool const editor = GameMode::getInstance()->isEditorMode();
			WorldRendererConfigure(editor ? WorldRendererKind::Software : kind);
			// The presentation is SDL_GPU in every window (Phase 2 follow-up): the UI, the legacy frame and the
			// world share one compositor and one present path. Headless sessions keep the software path.
			VideoRequestGpuDevice(!sgp::IsHeadless());
			if (!editor && kind != WorldRendererKind::Software)
			{
				VideoSetForceLayered(true);
			}
		}

		// Resolution 0x0 is "auto": the desktop size, only known once SDL's video
		// subsystem is up. Until then (and for headless sessions, which have no
		// display and ignore the UI scale) a classic canvas is used.
		VideoDisplaySettings displaySettings{
			EngineOptions_getResolutionX(params.get()),
			EngineOptions_getResolutionY(params.get()),
			EngineOptions_getUiScale(params.get()),
			EngineOptions_getWindowMode(params.get()),
			EngineOptions_getWorldZoom(params.get()) };
		// The map editor draws its taskbar over the world at one scale: no layers.
		if (GameMode::getInstance()->isEditorMode()) displaySettings.worldZoom = VideoLayout::WORLD_ZOOM_MATCH_UI;
		bool const autoResolution = displaySettings.resX == 0 || displaySettings.resY == 0;
		uint16_t width = autoResolution ? 640 : displaySettings.resX;
		uint16_t height = autoResolution ? 480 : displaySettings.resY;
		g_ui.setScreenSize(width, height);
		if (sgp::IsHeadless() && (displaySettings.worldZoom != VideoLayout::WORLD_ZOOM_MATCH_UI || VideoForceLayered()) && !autoResolution)
		{
			// A headless session normally renders a single layer at scale 1 (its screenshots are the reference
			// images). Asking for a world zoom explicitly makes it run the layers, so they can be tested:
			// -res is the window, the UI scale defaults to 1.
			int const uiScale = displaySettings.uiScale == VideoLayout::UI_SCALE_AUTO ? 1 : displaySettings.uiScale;
			auto const display = VideoLayout::ComputeDisplayLayout({ displaySettings.resX, displaySettings.resY }, uiScale);
			auto const layers = VideoLayout::ComputeLayerLayout(display, displaySettings.worldZoom);
			g_ui.setLayers(VideoForceLayered() ? VideoLayout::ForceLayered(layers) : layers);
		}

	if (EngineOptions_shouldRunUnittests(params.get())) {
	#ifdef WITH_UNITTESTS
			Logger_setLevel(LogLevel::Error);
			testing::InitGoogleTest(&argc, argv);
			return RUN_ALL_TESTS();
	#else
			SLOGW("This executable does not include unit tests.");
	#endif
		}

		{
			// ja2.json "ui_mode" and "native_ui_scale" (docs/plan/native-modern-game.md, Phase 2)
			RustPointer<char> modes(EngineOptions_getUiModes(params.get()));
			NativeUI::Configure(modes.get() ? modes.get() : "", EngineOptions_getNativeUiScale(params.get()));
			NativeUI::SetReducedMotion(EngineOptions_getReducedMotion(params.get()));
		}

		GameVersion version = EngineOptions_getResourceVersion(params.get());
		setGameVersion(version);

		VideoScaleQuality scalingQuality = EngineOptions_getScalingQuality(params.get());

		FLOAT brightness = EngineOptions_getBrightness(params.get());

		////////////////////////////////////////////////////////////

		if (automation.Active())
		{
			// A driven session must never steal focus from the user.
			SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
		}

		// Headless sessions need SDL only for its event queue (quit requests).
		SDL_Init(sgp::IsHeadless() ? SDL_INIT_EVENTS : SDL_INIT_VIDEO);

		if (!sgp::IsHeadless())
		{
			// logical canvas = window / UI scale, see VideoLayout.h
			auto const layout = VideoComputeLayout(displaySettings);
			auto layers = VideoLayout::ComputeLayerLayout(layout, displaySettings.worldZoom);
			if (VideoForceLayered()) layers = VideoLayout::ForceLayered(layers);
			g_ui.setLayers(layers);
			SLOGI("Layers: UI {}x{} at {}x, world {}x{} at {}x{}", layers.ui.w, layers.ui.h, layers.uiScale,
				layers.world.w, layers.world.h, layers.worldZoom, layers.layered ? " (separate layer)" : " (same layer)");
		}

		// A missing or unusable game directory opens the native setup screen instead of failing to load the game
		// data (the launcher's replacement). Headless and driven sessions keep the old failing path.
		if (NativeUI::Built() && !sgp::IsHeadless() && !automation.Active() &&
		    NativeUI::ConfiguredMode("setup") == NativeUI::UiMode::Native &&
		    !EngineOptions_shouldRunEditor(params.get()))
		{
			RustPointer<char> configuredDir(EngineOptions_getVanillaGameDir(params.get()));
			std::string const dir = configuredDir && configuredDir.get() ? configuredDir.get() : "";
			bool const usable = !dir.empty() && checkIfRelativePathExists(dir.c_str(), "Data", true);
			// JA2_SETUP forces the screen even with a usable game directory (reach the logs; golden screenshots)
			char const* const force = std::getenv("JA2_SETUP");
			bool const forced = force && *force && std::strcmp(force, "0") != 0;
			if (forced || !usable)
			{
				SLOGI("opening the native setup screen (game directory '{}')", dir);
				InitializeVideoManager(scalingQuality, 60, displaySettings);
				bool const restart = NativeUI::RunSetup(params.get());
				ShutdownVideoManager();
				SDL_Quit();
				if (restart) RelaunchProcess(argc, argv);
				return EXIT_SUCCESS;
			}
		}

		// restore output to the console (on windows when built with MINGW)
		// Not for automation: its output is usually piped to the controller.
	#ifdef __MINGW32__
		if (!automation.Active())
		{
			freopen("CON", "w", stdout);
			freopen("CON", "w", stderr);
		}
	#endif

		SLOGD("Initializing Game Resources");

		DefaultContentManager *cm;

		uint32_t n = EngineOptions_getModsLength(params.get());
		if(n > 0)
		{
			cm = new ModPackContentManager(std::move(params));
		}
		else
		{
			cm = new DefaultContentManager(std::move(params));
		}

		cm->logConfiguration();

		if (!cm->loadGameData())
		{
			throw std::runtime_error("Failed to load the game data.");
		}

		GCM = cm;

		g_ui.recalculatePositions();

		SLOGD("Initializing Video Manager");
		InitializeVideoManager(scalingQuality, GCM->getGamePolicy()->target_fps, displaySettings);
		VideoSetBrightness(brightness);

		#ifdef __ANDROID__
			// On Android, always run fullscreen to hide system bars
			VideoSetFullScreen(TRUE);
		#endif

		SLOGD("Initializing Video Surface Manager");
		InitializeVideoSurfaceManager();

		InitJA2SplashScreen();

		SLOGD("Initializing Sound Manager");
		InitializeSoundManager();

		SLOGD("Initializing Random");
		// Initialize random number generator (seeded by -seed in automation)
		if (automation.Active()) SetRandomSeed(automation.Seed());
		else InitializeRandom(); // no Shutdown

		SLOGD("Initializing Game Manager");
		// Initialize the Game
		InitializeGame();

		gfGameInitialized = TRUE;

		if (automation.noIntro)
		{
			extern BOOLEAN gfDoneWithSplashScreen;
			gfDoneWithSplashScreen = TRUE;
			SetSkipIntroVideos(true);
		}
		else if(isEnglishVersion() || isChineseVersion())
		{
			SetIntroType(INTRO_SPLASH);
		}

		SLOGD("Running Game");

		if (automation.Active())
		{
			// The driver owns the loop: it steps frames as the script or the
			// remote controller asks.
			int const exitCode = Automation::Run();
			deinitGameAndExit(exitCode);
		}

		/* At this point the SGP is set up, which means all I/O, Memory, tools, etc.
		* are available. All we need to do is attend to the gaming mechanics
		* themselves */
		MainLoop();

		delete cm;
		GCM = NULL;

		return EXIT_SUCCESS;
	} catch (...) {
		try {
			TerminationHandler();
		} catch (...) {
			// If you ever see return code 27, try to set a breakpoint here
			return 27;
		}
		return EXIT_FAILURE;
	}
}

void TerminationHandler()
{
	auto ex = std::current_exception();
	auto errorMessage = ST::string("Game has been terminated due to an unknown error");
	#ifdef __ANDROID__
	// Pull out some methods from JNI to set error on NativeExceptionContainer
	auto jniEnv = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jclass exceptionContainer = jniEnv->FindClass("io/github/adhikasp/pecelbakwan/NativeExceptionContainer");
    jfieldID singletonFieldId = jniEnv->GetStaticFieldID(exceptionContainer, "INSTANCE", "Lio/github/adhikasp/pecelbakwan/NativeExceptionContainer;");
    jobject exceptionContainerSingleton = jniEnv->GetStaticObjectField(exceptionContainer, singletonFieldId);
    jmethodID setAndroidExceptionMethodId = jniEnv->GetMethodID(exceptionContainer, "setException","(Ljava/lang/String;)V");
	#endif

	if (ex)
	{
		try
		{
			std::rethrow_exception(ex);
		}
		catch (const std::exception& e)
		{
			errorMessage = ST::format("Game has been terminated due to an unrecoverable error: {} ({})", e.what(), typeid(e).name());
		}
		catch (...)
		{
		}
	}
	SLOGE(errorMessage.c_str());
	#ifdef __ANDROID__
	jniEnv->CallVoidMethod(exceptionContainerSingleton, setAndroidExceptionMethodId,
                                   jniEnv->NewStringUTF(errorMessage.c_str()));
	#endif
	shutdownGame();
	#ifndef __ANDROID__
	std::abort();
	#endif
}

/*static void convertDialogQuotesToJson(const DefaultContentManager *cm,
					STRING_ENC_TYPE encType,
					const char *dialogFile, const char *outputFile)
{
	std::vector<ST::string*> quotes;
	std::vector<ST::string> quotes_str;
	cm->loadAllDialogQuotes(encType, dialogFile, quotes);
	for(int i = 0; i < quotes.size(); i++)
	{
		quotes_str.push_back(quotes[i]->to_std_string());
		delete quotes[i];
		quotes[i] = nullptr;
	}
	JsonUtility::writeToFile(outputFile, quotes_str);
}*/

#ifdef WITH_UNITTESTS
#include "gtest/gtest.h"

struct TestStruct {
	int a;
	int b;
	int c[2];
};

struct NonTrivialTestStruct {
	NonTrivialTestStruct() : a(0) {}
	NonTrivialTestStruct(int a) : a(a) {}
	int a;
	int b = 0;
};

TEST(cpp_language, list_initialization)
{
	// since C++11: https://en.cppreference.com/w/cpp/language/list_initialization
	{
		TestStruct tmp{};
		EXPECT_EQ(tmp.a, 0);
		EXPECT_EQ(tmp.b, 0);
		EXPECT_EQ(tmp.c[0], 0);
		EXPECT_EQ(tmp.c[1], 0);
	}
	{
		TestStruct tmp{1, 2, {3, 4}};
		EXPECT_EQ(tmp.a, 1);
		EXPECT_EQ(tmp.b, 2);
		EXPECT_EQ(tmp.c[0], 3);
		EXPECT_EQ(tmp.c[1], 4);
	}
	{
		TestStruct tmp = TestStruct{};
		EXPECT_EQ(tmp.a, 0);
		EXPECT_EQ(tmp.b, 0);
		EXPECT_EQ(tmp.c[0], 0);
		EXPECT_EQ(tmp.c[1], 0);
	}
	{
		TestStruct tmp = TestStruct{1, 2, {3, 4}};
		EXPECT_EQ(tmp.a, 1);
		EXPECT_EQ(tmp.b, 2);
		EXPECT_EQ(tmp.c[0], 3);
		EXPECT_EQ(tmp.c[1], 4);
	}
	{
		TestStruct tmp = {};
		EXPECT_EQ(tmp.a, 0);
		EXPECT_EQ(tmp.b, 0);
		EXPECT_EQ(tmp.c[0], 0);
		EXPECT_EQ(tmp.c[1], 0);
	}
	{
		TestStruct tmp = {1, 2, {3, 4}};
		EXPECT_EQ(tmp.a, 1);
		EXPECT_EQ(tmp.b, 2);
		EXPECT_EQ(tmp.c[0], 3);
		EXPECT_EQ(tmp.c[1], 4);
	}
}

// get initialized memory from new
TEST(cpp_language, new_initialization)
{
	{
		int* tmp = new int();
		EXPECT_EQ(*tmp, 0);
		delete tmp;
	}
	{
		int* tmp = new int(123);
		EXPECT_EQ(*tmp, 123);
		delete tmp;
	}
	{
		int* tmp = new int{123};
		EXPECT_EQ(*tmp, 123);
		delete tmp;
	}
	{
		// avoid this pattern, it's uninitialized memory for trivial structs (PODs)
		NonTrivialTestStruct* tmp = new NonTrivialTestStruct;
		EXPECT_EQ(tmp->a, 0);
		EXPECT_EQ(tmp->b, 0);
		delete tmp;
	}
	{
		TestStruct* tmp = new TestStruct();
		EXPECT_EQ(tmp->a, 0);
		EXPECT_EQ(tmp->b, 0);
		EXPECT_EQ(tmp->c[0], 0);
		EXPECT_EQ(tmp->c[1], 0);
		delete tmp;
	}
	{
		NonTrivialTestStruct* tmp = new NonTrivialTestStruct();
		EXPECT_EQ(tmp->a, 0);
		EXPECT_EQ(tmp->b, 0);
		delete tmp;
	}
	{
		NonTrivialTestStruct* tmp = new NonTrivialTestStruct(123);
		EXPECT_EQ(tmp->a, 123);
		EXPECT_EQ(tmp->b, 0);
		delete tmp;
	}
	{
		TestStruct* tmp = new TestStruct{};
		EXPECT_EQ(tmp->a, 0);
		EXPECT_EQ(tmp->b, 0);
		EXPECT_EQ(tmp->c[0], 0);
		EXPECT_EQ(tmp->c[1], 0);
		delete tmp;
	}
	{
		TestStruct* tmp = new TestStruct{1, 2, {3, 4}};
		EXPECT_EQ(tmp->a, 1);
		EXPECT_EQ(tmp->b, 2);
		EXPECT_EQ(tmp->c[0], 3);
		EXPECT_EQ(tmp->c[1], 4);
		delete tmp;
	}
	{
		NonTrivialTestStruct* tmp = new NonTrivialTestStruct{};
		EXPECT_EQ(tmp->a, 0);
		EXPECT_EQ(tmp->b, 0);
		delete tmp;
	}
	{
		NonTrivialTestStruct* tmp = new NonTrivialTestStruct{123};
		EXPECT_EQ(tmp->a, 123);
		EXPECT_EQ(tmp->b, 0);
		delete tmp;
	}
}

// get initialized memory from new[]
TEST(cpp_language, new_array_initialization)
{
	{
		int* tmp = new int[2]();
		EXPECT_EQ(tmp[0], 0);
		EXPECT_EQ(tmp[1], 0);
		delete[] tmp;
	}
	{
		int* tmp = new int[2]{};
		EXPECT_EQ(tmp[0], 0);
		EXPECT_EQ(tmp[1], 0);
		delete[] tmp;
	}
	{
		int* tmp = new int[2]{123};
		EXPECT_EQ(tmp[0], 123);
		EXPECT_EQ(tmp[1], 0);
		delete[] tmp;
	}
	{
		int* tmp = new int[2]{123, 456};
		EXPECT_EQ(tmp[0], 123);
		EXPECT_EQ(tmp[1], 456);
		delete[] tmp;
	}
	{
		// avoid this pattern, it's uninitialized memory for trivial structs (PODs)
		NonTrivialTestStruct* tmp = new NonTrivialTestStruct[2];
		EXPECT_EQ(tmp[0].a, 0);
		EXPECT_EQ(tmp[0].b, 0);
		EXPECT_EQ(tmp[1].a, 0);
		EXPECT_EQ(tmp[1].b, 0);
		delete[] tmp;
	}
	{
		TestStruct* tmp = new TestStruct[2]();
		EXPECT_EQ(tmp[0].a, 0);
		EXPECT_EQ(tmp[0].b, 0);
		EXPECT_EQ(tmp[0].c[0], 0);
		EXPECT_EQ(tmp[0].c[1], 0);
		EXPECT_EQ(tmp[1].a, 0);
		EXPECT_EQ(tmp[1].b, 0);
		EXPECT_EQ(tmp[1].c[0], 0);
		EXPECT_EQ(tmp[1].c[1], 0);
		delete[] tmp;
	}
	{
		NonTrivialTestStruct* tmp = new NonTrivialTestStruct[2]();
		EXPECT_EQ(tmp[0].a, 0);
		EXPECT_EQ(tmp[0].b, 0);
		EXPECT_EQ(tmp[1].a, 0);
		EXPECT_EQ(tmp[1].b, 0);
		delete[] tmp;
	}
	{
		TestStruct* tmp = new TestStruct[2]{};
		EXPECT_EQ(tmp[0].a, 0);
		EXPECT_EQ(tmp[0].b, 0);
		EXPECT_EQ(tmp[0].c[0], 0);
		EXPECT_EQ(tmp[0].c[1], 0);
		EXPECT_EQ(tmp[1].a, 0);
		EXPECT_EQ(tmp[1].b, 0);
		EXPECT_EQ(tmp[1].c[0], 0);
		EXPECT_EQ(tmp[1].c[1], 0);
		delete[] tmp;
	}
	{
		NonTrivialTestStruct* tmp = new NonTrivialTestStruct[2]{};
		EXPECT_EQ(tmp[0].a, 0);
		EXPECT_EQ(tmp[0].b, 0);
		EXPECT_EQ(tmp[1].a, 0);
		EXPECT_EQ(tmp[1].b, 0);
		delete[] tmp;
	}
	{
		// avoid this pattern, it's uninitialized memory for trivial structs (PODs) in VS2015
		NonTrivialTestStruct* tmp = new NonTrivialTestStruct[2]{
			{123}
		};
		EXPECT_EQ(tmp[0].a, 123);
		EXPECT_EQ(tmp[0].b, 0);
		EXPECT_EQ(tmp[1].a, 0);
		EXPECT_EQ(tmp[1].b, 0);
		delete[] tmp;
	}
	{
		// avoid this pattern, it's uninitialized memory for trivial structs (PODs) in VS2015
		NonTrivialTestStruct* tmp = new NonTrivialTestStruct[2]{
			{123},
			{456}
		};
		EXPECT_EQ(tmp[0].a, 123);
		EXPECT_EQ(tmp[0].b, 0);
		EXPECT_EQ(tmp[1].a, 456);
		EXPECT_EQ(tmp[1].b, 0);
		delete[] tmp;
	}
}

TEST(cpp_language, sizeof_type)
{
	EXPECT_EQ(sizeof(char), 1);
	EXPECT_EQ(sizeof(char16_t), 2);
	EXPECT_EQ(sizeof(char32_t), 4);
}

#endif // WITH_UNITTESTS
