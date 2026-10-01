// Static design mocks (M2 wireframes, docs/plan/native-modern-game.md): an RML document from assets/ui/mocks/ shown
// full screen through the real native runtime, so it renders at the output's own resolution and UI scale exactly like
// a finished screen would. No view model: the data in the mock is literal. Opened with ja2.debug("mock", "<path>");
// Esc goes back to the screen underneath.
#include "NativeImages.h"
#include "NativeUIRuntime.h"

#include "ContentManager.h"
#include "Cursor_Control.h"
#include "Video.h"
#include "Directories.h"
#include "GameInstance.h"
#include "Input.h"
#include "LoadingScreenModel.h"
#include "Logger.h"
#include "SaveLoadGame.h"
#include "VSurface.h"
#include "UiCore.h"

#include <cstdlib>
#include <memory>

namespace NativeUI
{

SDL_Surface* LoadWholeImage(std::string const& file)
{
	try
	{
		std::unique_ptr<SGPVSurface> const vs(AddVideoSurfaceFromFile(file.c_str()));
		return SDL_ConvertSurface(const_cast<SDL_Surface*>(&vs->GetSDLSurface()), SDL_PIXELFORMAT_RGBA32);
	}
	catch (std::exception const& e)
	{
		SLOGW("native UI: no image {}: {}", file, e.what());
		return nullptr;
	}
}

void RegisterFrontEndImages()
{
	{
		// "loadscreen-<n>": loading screen n (loading-screens.json order) from the player's game data
		RegisterImageSource("loadscreen-", [](std::string const& name) -> SDL_Surface* {
			int const id = std::atoi(name.c_str() + 11);
			LoadingScreen const* ls = GCM->getLoadingScreen(uint8_t(id));
			return ls ? LoadWholeImage((ST::string(LOADSCREENSDIR) + ls->filename).to_std_string()) : nullptr;
		});
		// the main menu art, uplifted 4x (Scale2x twice) so that it stays sharp when it covers a big screen
		RegisterImageSource("mainmenu-art", [](std::string const&) {
			return UpliftArt(LoadStiFrame(LOADSCREENSDIR "/mainmenubackground.sti", 0), 2);
		});
		RegisterImageSource("mainmenu-logo", [](std::string const&) {
			return LoadStiFrame(LOADSCREENSDIR "/ja2logo.sti", 0);
		});
		// "save-thumb-<save name>@<modified time>": the thumbnail next to a save (the time makes a new picture a new
		// texture after the save was overwritten)
		RegisterImageSource("save-thumb-", [](std::string const& name) -> SDL_Surface* {
			std::string save = name.substr(11);
			save = save.substr(0, save.rfind('@'));
			return LoadThumbnail(GCM->saveGameFiles()->absolutePath(GetSaveThumbnailPath(save)).to_std_string());
		});
	}
}

namespace
{

	class MockScreen final : public Screen
	{
	public:
		MockScreen(std::string path, ScreenID self) : m_path(std::move(path)), m_self(self) {}

		void Enter() override
		{
			nui::SetImageProvider(ProvideGameImage);
			RegisterFrontEndImages();
			RegisterLaptopImages();
			m_doc = LoadDocument(m_path);
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
		}

		ScreenID Handle() override
		{
			InputAtom e;
			while (DequeueEvent(&e))
			{
				ProcessKey(e);
				if (e.usEvent == KEY_UP && e.usParam == SDLK_ESCAPE) m_done = true;
			}
			SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR);
			return m_self;
		}

		void Exit() override
		{
			CloseDocument(m_doc);
			m_doc = nullptr;
		}

		bool Finished() const override { return m_done; }

	private:
		std::string m_path;
		ScreenID m_self;
		Rml::ElementDocument* m_doc = nullptr;
		bool m_done = false;
	};
}

std::unique_ptr<Screen> CreateMockScreen(std::string const& path, ScreenID self)
{
	return std::make_unique<MockScreen>(path, self);
}

}
