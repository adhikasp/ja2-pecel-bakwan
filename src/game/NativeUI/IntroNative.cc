// The native intro/ending cinematic (docs/ui/intro.md): the chain of Smacker videos under a fading hint bar. The
// videos are content and play through the legacy decoder into the frame buffer; this screen owns the presentation
// around them — the chain and its variants, the scene indicator and skip hints, the still card when a video cannot
// be played, and where the presentation hands over to.
#include "IntroViewModel.h"
#include "NativeImages.h"
#include "NativeUIRuntime.h"

#include "Cinematics.h"
#include "ContentMusic.h"
#include "Cursor_Control.h"
#include "Cursors.h"
#include "Directories.h"
#include "Input.h"
#include "Intro.h"
#include "Music_Control.h"
#include "Soldier_Profile.h"
#include "Timer.h"
#include "UILayout.h"
#include "Video.h"
#include "VSurface.h"
#include "UiCore.h"

#include <string_theory/format>

#include <algorithm>
#include <memory>
#include <optional>

namespace NativeUI
{

IntroViewModel::IntroViewModel() : ViewModel("intro", 0)
{
	Command("next", [this](Args const&) { if (onNext) onNext(); });
	Command("skipall", [this](Args const&) { if (onSkipAll) onSkipAll(); });
}

void IntroViewModel::Describe(Fields& f)
{
	f.Field("kind", kind);
	f.Rows("scenes", scenes);
	f.Field("index", index);
	f.Field("progress", progress);
	f.Field("card", card);
	f.Field("chrome", chrome);
	f.Field("finished", finished);
	f.Field("position", position);
	f.Field("card_note", cardNote);
	f.Field("hint", hint);
	f.Field("exit", exit);
}

void IntroViewModel::Load(IntroModel::Kind const k, bool const miguelDead, bool const skyriderDead)
{
	kind = k == IntroModel::Kind::Splash ? "splash" : k == IntroModel::Kind::Beginning ? "beginning" : "ending";
	scenes.clear();
	std::vector<IntroModel::Scene> const chain = IntroModel::Chain(k, miguelDead, skyriderDead);
	for (size_t i = 0; i < chain.size(); ++i) scenes.push_back({ int(i), chain[i].id, false });
	exit = IntroModel::ExitTarget(k) == EPILOGUE_SCREEN ? "EPILOGUE_SCREEN" : "INIT_SCREEN";
	index = 0;
	Select(0);
}

void IntroViewModel::Select(int const i)
{
	index = scenes.empty() ? 0 : std::clamp(i, 0, int(scenes.size()) - 1);
	for (IntroSceneRow& s : scenes) s.active = s.index == index;
	position = ST::format(Str("intro.scene").c_str(), index + 1, scenes.size()).to_std_string();
	Changed();
}

void IntroViewModel::Next()
{
	if (onNext) onNext();
}


namespace
{
	/** One presentation: the chain of scenes, the flic playing now and the screen's own timing. */
	class IntroScreen final : public Screen
	{
	public:
		void Enter() override
		{
			FRAME_BUFFER->Fill(0); // the stage is black; the video is blitted into it by the decoder
			SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR);
			SetMusicMode(MUSIC_NONE); // the videos carry their own audio, like the legacy screen
			SmkInitialize();

			m_kind = GetIntroKind();
			bool const miguelDead   = gMercProfiles[MIGUEL].bMercStatus == MERC_IS_DEAD;
			bool const skyriderDead = gMercProfiles[SKYRIDER].bMercStatus == MERC_IS_DEAD;
			m_chain = IntroModel::Chain(m_kind, miguelDead, skyriderDead);
			m_vm.Load(m_kind, miguelDead, skyriderDead);
			m_vm.onNext = [this] { SkipScene(); };
			m_vm.onSkipAll = [this] { Finish(); };
			m_binding.emplace(Context(), m_vm);
			m_doc = LoadDocument("screens/intro.rml");
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
			m_lastInput = GetClock();

			// -no-intro skips the splash and the new-game intro; the ending still plays (Intro.cc)
			if (SkipIntroVideos() && m_kind != IntroModel::Kind::Ending) Finish();
			else StartScene();
			Apply();
		}

		ScreenID Handle() override
		{
			InputAtom e;
			while (DequeueEvent(&e))
			{
				ProcessKey(e);
				if (e.usEvent != KEY_UP) continue;
				m_lastInput = GetClock();
				if (e.usParam == SDLK_ESCAPE) Finish();
				else if (e.usParam == SDLK_SPACE) SkipScene();
			}
			SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR);

			uint32_t const now = GetClock();
			if (!m_done)
			{
				if (m_flic)
				{
					if (SmkPollFlics()) m_vm.progress = SmkProgress(m_flic);
					else { m_flic = nullptr; Advance(); } // the flic closed itself when it ran out
				}
				else if (m_card && now >= m_cardUntil)
				{
					Advance();
				}
			}

			// the hint bar fades out when the player is idle (docs/ui/intro.md §6) and comes back on activity
			if (MouseMovedSinceLastFrame()) m_lastInput = now;
			m_vm.chrome = IntroModel::ChromeVisible(now, m_lastInput);
			Apply();
			return m_done ? m_next : INTRO_SCREEN;
		}

		void Exit() override
		{
			if (m_flic) { SmkCloseFlic(m_flic); m_flic = nullptr; }
			SmkShutdown();
			CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
		}

		void Resized() override { Apply(); }

		/** The pointer is part of the hint bar: both show when the player is active and hide when they idle
		 * (docs/ui/intro.md §6). */
		bool ShowsCursor() const override { return m_vm.chrome; }

	private:
		void StartScene()
		{
			if (m_index >= int(m_chain.size())) { Finish(); return; }
			m_vm.Select(m_index);
			m_vm.progress = 0;
			m_vm.card = false;
			m_card = false;
			m_flic = nullptr;
			if (!IntroStillCards())
			{
				ST::string const path = ST::format(INTRODIR "/{}", m_chain[m_index].file.c_str());
				m_flic = SmkPlayFlic(path.c_str(), STD_SCREEN_X, STD_SCREEN_Y, TRUE);
			}
			if (m_flic) m_vm.card = false;
			else
			{
				// no video: the scene stands as a still card and the chain goes on (§S6)
				m_card = m_vm.card = true;
				m_cardUntil = GetClock() + IntroModel::CARD_MS;
				m_vm.progress = -1;
				m_vm.cardNote = IntroStillCards() ? "" : Str("intro.unavailable");
			}
			Apply();
		}

		void Advance()
		{
			if (++m_index >= int(m_chain.size())) Finish();
			else StartScene();
		}

		void SkipScene()
		{
			if (m_flic) { SmkCloseFlic(m_flic); m_flic = nullptr; }
			if (m_done) return;
			Advance();
		}

		void Finish()
		{
			if (m_done) return;
			if (m_flic) { SmkCloseFlic(m_flic); m_flic = nullptr; }
			m_vm.finished = true;
			m_next = PrepareToExitIntroScreen(); // the exit rules both UIs share (Intro.cc)
			m_done = true;
			m_vm.Changed();
		}

		bool MouseMovedSinceLastFrame()
		{
			Rml::Vector2f const p = MousePosition();
			bool const moved = p.x != m_mouseX || p.y != m_mouseY;
			m_mouseX = p.x;
			m_mouseY = p.y;
			return moved;
		}

		void Apply()
		{
			if (!m_doc) return;
			// narrow layouts (big UI scales, small windows) shorten the hint
			float const widthDp = Context()->GetDimensions().x / std::max(0.01f, DpScale());
			Rml::Element* const root = m_doc->GetElementById("intro");
			if (root)
			{
				root->SetClass("compact", widthDp < 1200);
				root->SetClass("reduced", ReducedMotion());
			}
			m_vm.hint = Str(widthDp < 1200 ? "intro.hint.short" : "intro.hint");
			if (Rml::Element* fill = m_doc->GetElementById("intro.track-fill"))
			{
				float const pct = float(std::clamp(m_vm.progress, 0.0, 1.0)) * 100.0f;
				fill->SetProperty(Rml::PropertyId::Width, Rml::Property(pct, Rml::Unit::PERCENT));
			}
			m_vm.Changed();
			Invalidate(2);
		}

		IntroViewModel m_vm;
		std::vector<IntroModel::Scene> m_chain;
		std::optional<Binding> m_binding;
		Rml::ElementDocument* m_doc = nullptr;
		IntroModel::Kind m_kind = IntroModel::Kind::Ending;
		SMKFLIC* m_flic = nullptr;
		bool     m_card = false;
		uint32_t m_cardUntil = 0;
		int      m_index = 0;
		bool     m_done = false;
		ScreenID m_next = INTRO_SCREEN;
		uint32_t m_lastInput = 0;
		float    m_mouseX = -1, m_mouseY = -1;
	};
}

std::unique_ptr<Screen> CreateIntroScreen()
{
	return std::make_unique<IntroScreen>();
}

namespace { bool const g_registered = (RegisterViewModelFactory("intro", [] {
	auto vm = std::make_unique<IntroViewModel>();
	vm->Load(GetIntroKind(),
		gMercProfiles[MIGUEL].bMercStatus == MERC_IS_DEAD,
		gMercProfiles[SKYRIDER].bMercStatus == MERC_IS_DEAD);
	return std::unique_ptr<ViewModel>(std::move(vm));
}), true); }

}
