// The native credits screen (docs/ui/credits.md): the credits file as a scrolling reel, and the team behind the game
// as a roster with their portraits (cut from the player's own credits art at runtime).
#include "CreditsViewModel.h"
#include "NativeImages.h"
#include "NativeUIRuntime.h"

#include "Credits.h"
#include "Cursor_Control.h"
#include "Directories.h"
#include "EDT.h"
#include "Input.h"
#include "Logger.h"
#include "Random.h"
#include "Text.h"
#include "Timer.h"
#include "Video.h"
#include "UiCore.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace NativeUI
{

bool ParseCreditRecord(std::string const& record, std::vector<CreditLine>& out)
{
	bool title = false, endSection = false;
	char const* s = record.c_str();
	if (*s == '@')
	{
		// codes: "@X<number>,Y,...;text" (see Credits.cc); only the title and section flags matter here
		for (;;)
		{
			++s;
			char const code = *s ? *s++ : '\0';
			if (code == 'T') title = true;
			else if (code == '}') endSection = true;
			while (*s && *s != ',' && *s != ';') ++s;
			if (*s == ';') { ++s; break; }
			if (*s != ',') break;
		}
	}
	std::string const text = s;
	bool const hasText = !text.empty();
	if (hasText) out.push_back({ title ? "title" : "line", text });
	if (endSection) out.push_back({ "gap", "" });
	return hasText;
}

CreditsViewModel::CreditsViewModel() : ViewModel("credits", TOPIC_SETTINGS)
{
	Command("back", [this](Args const&) { if (onBack) onBack(); });
	Command("pause", [this](Args const&) { paused = !paused; pauseLabel = Str(paused ? "credits.resume" : "credits.pause"); Changed(); });
	Command("select", [this](Args const& a) { Select(a.empty() ? -1 : std::atoi(a[0].c_str())); });
}

void CreditsViewModel::Describe(Fields& f)
{
	f.Rows("lines", lines);
	f.Rows("people", people);
	f.Field("selected", selected);
	f.Field("sel_name", selName);
	f.Field("sel_title", selTitle);
	f.Field("sel_funny", selFunny);
	f.Field("paused", paused);
	f.Field("finished", finished);
	f.Field("offset", offset);
	f.Field("title", title);
	f.Field("pause_label", pauseLabel);
	f.Field("back_label", backLabel);
	f.Field("team_label", teamLabel);
	f.Field("hint", hint);
	f.Field("paused_text", pausedText);
}

void CreditsViewModel::Load()
{
	lines.clear();
	EDTFile const file(EDTFile::CREDITS);
	for (unsigned record = 1;; ++record)
	{
		ST::string text;
		try { text = file.at(record, 0); }
		catch (...) { break; }
		ParseCreditRecord(text.to_std_string(), lines);
	}
	people.clear();
	for (int i = 0; i < NUM_PEOPLE_IN_CREDITS; ++i)
	{
		CreditPerson p;
		p.index = i;
		p.name  = gzCreditNames[i].to_std_string();
		p.title = gzCreditNameTitle[i].to_std_string();
		p.funny = gzCreditNameFunny[i].to_std_string();
		p.face  = "credits-face-" + std::to_string(i);
		p.faceBlink = "credits-blink-" + std::to_string(i);
		people.push_back(p);
	}
	title      = Str("credits.title");
	pauseLabel = Str("credits.pause");
	backLabel  = Str("common.back");
	teamLabel  = Str("credits.team");
	hint       = Str("credits.hint");
	pausedText = Str("credits.paused");
	Select(-1);
}

void CreditsViewModel::Select(int const person)
{
	selected = person >= 0 && person < int(people.size()) ? person : -1;
	if (selected >= 0)
	{
		selName  = people[selected].name;
		selTitle = people[selected].title;
		selFunny = people[selected].funny;
	}
	else
	{
		selName = selTitle = selFunny = "";
	}
	Changed();
}

bool CreditsViewModel::Advance(double const seconds)
{
	if (!paused && !finished)
	{
		offset += speed * seconds;
		if (length > 0 && offset >= length) finished = true;
	}
	return finished;
}

void CreditsViewModel::Scroll(double const dp)
{
	offset = std::clamp(offset + dp, 0.0, std::max(0.0, length));
	Changed();
}


namespace
{
	/** The credits background (frame 0 of credits.sti), loaded once while the screen is open. */
	SDL_Surface* g_background = nullptr;
	SDL_Surface* g_eyes[NUM_PEOPLE_IN_CREDITS] = {};

	SDL_Surface* Background()
	{
		if (!g_background) g_background = LoadStiFrame(INTERFACEDIR "/credits.sti", 0);
		return g_background;
	}

	SDL_Surface* FaceImage(std::string const& name, bool const blink)
	{
		int const i = std::atoi(name.c_str() + name.find_last_of('-') + 1);
		if (i < 0 || i >= NUM_PEOPLE_IN_CREDITS) return nullptr;
		SDL_Surface* bg = Background();
		if (!bg) return nullptr;
		CreditFaceArea const a = GetCreditFaceArea(i);
		int const scale = 4; // enlarged with nearest neighbour; the UI filters it down to its size
		SDL_Surface* s = CropScaled(bg, a.x, a.y, a.w, a.h, scale);
		if (s && blink)
		{
			// the closed-eye frame over the eyes, where the legacy screen draws it
			if (!g_eyes[i]) g_eyes[i] = LoadStiFrame(INTERFACEDIR "/credit faces.sti", i * 3);
			if (SDL_Surface* eyes = g_eyes[i])
			{
				SDL_Surface* big = CropScaled(eyes, 0, 0, eyes->w, eyes->h, scale);
				if (big)
				{
					SDL_Rect dst{ (a.eyeX - a.x) * scale, (a.eyeY - a.y) * scale, big->w, big->h };
					SDL_SetSurfaceBlendMode(big, SDL_BLENDMODE_BLEND);
					SDL_BlitSurface(big, nullptr, s, &dst);
					SDL_DestroySurface(big);
				}
			}
		}
		return s;
	}

	class CreditsScreen final : public Screen
	{
	public:
		void Enter() override
		{
			nui::SetImageProvider(ProvideGameImage); // a spike screen may have replaced it
			RegisterImageSource("credits-face-", [](std::string const& n) { return FaceImage(n, false); });
			RegisterImageSource("credits-blink-", [](std::string const& n) { return FaceImage(n, true); });
			m_vm.Load();
			m_vm.onBack = [this] { m_done = true; };
			if (ReducedMotion()) m_vm.Invoke("pause"); // accessibility: the reel only moves by hand
			m_binding.emplace(Context(), m_vm);
			m_doc = LoadDocument("screens/credits.rml");
			m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::Document);
			m_last = GetClock();
			uint32_t const now = GetClock();
			for (int i = 0; i < NUM_PEOPLE_IN_CREDITS; ++i) m_nextBlink[i] = now + Random(GetCreditFaceArea(i).blinkMs * 2);
			Measure();
			Apply();
		}

		ScreenID Handle() override
		{
			// Input: the document first (focus navigation, buttons), then the screen's own keys
			InputAtom e;
			while (DequeueEvent(&e))
			{
				bool const used = ProcessKey(e);
				if (e.usEvent == KEY_UP && e.usParam == SDLK_ESCAPE) m_done = true; // like the legacy screen: on release
				if (used || (e.usEvent != KEY_DOWN && e.usEvent != KEY_REPEAT)) continue;
				switch (e.usParam)
				{
					case SDLK_SPACE:
					case SDLK_P:        m_vm.Invoke("pause"); break;
					case SDLK_UP:       m_vm.Scroll(-60); break;
					case SDLK_DOWN:     m_vm.Scroll(60); break;
					case SDLK_PAGEUP:   m_vm.Scroll(-400); break;
					case SDLK_PAGEDOWN: m_vm.Scroll(400); break;
					case SDLK_HOME:     m_vm.Scroll(-m_vm.offset); break;
					case SDLK_END:      m_vm.Scroll(m_vm.length); break; // to the end: the screen then closes, like a finished reel
					default: break;
				}
			}
			SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR); // the native cursor is drawn instead

			uint32_t const now = GetClock();
			double const dt = std::min(0.25, (now - m_last) / 1000.0);
			m_last = now;
			if (m_vm.Advance(dt)) m_done = true;
			Blink(now);
			Apply();
			return m_done ? MAINMENU_SCREEN : CREDIT_SCREEN;
		}

		void Exit() override
		{
			CloseDocument(m_doc);
			m_doc = nullptr;
			m_binding.reset();
			if (g_background) { SDL_DestroySurface(g_background); g_background = nullptr; }
			for (auto& s : g_eyes) if (s) { SDL_DestroySurface(s); s = nullptr; }
		}

		void Resized() override { Measure(); Apply(); }

	private:
		/** The reel scrolls from below its window until the last line has left at the top. */
		void Measure()
		{
			// narrow layouts (below 1600 dp: small windows, big UI scales) show the team in two columns
			float const widthDp = Context()->GetDimensions().x / std::max(0.01f, DpScale());
			if (Rml::Element* root = m_doc->GetElementById("credits")) root->SetClass("compact", widthDp < 1600);
			Context()->Update();
			Rml::Element* window  = m_doc->GetElementById("credits.reel");
			Rml::Element* content = m_doc->GetElementById("credits.reel-content");
			if (!window || !content) return;
			float const dp = std::max(0.01f, DpScale());
			m_windowDp = window->GetClientHeight() / dp;
			m_vm.length = m_windowDp + content->GetBox().GetSize(Rml::BoxArea::Border).y / dp;
		}

		void Apply()
		{
			Rml::Element* content = m_doc->GetElementById("credits.reel-content");
			if (!content) return;
			float const top = float(m_windowDp - m_vm.offset);
			content->SetProperty(Rml::PropertyId::Top, Rml::Property(std::round(top * DpScale()), Rml::Unit::PX));
			if (!m_vm.paused) Invalidate(2);
		}

		void Blink(uint32_t const now)
		{
			bool changed = false;
			for (int i = 0; i < NUM_PEOPLE_IN_CREDITS; ++i)
			{
				CreditPerson& p = m_vm.people[i];
				if (!p.blink && now >= m_nextBlink[i])
				{
					p.blink = true;
					m_openAt[i] = now + 150 + Random(150); // legacy: CRDT_EYES_CLOSED_TIME + random
					changed = true;
				}
				else if (p.blink && now >= m_openAt[i])
				{
					p.blink = false;
					m_nextBlink[i] = now + GetCreditFaceArea(i).blinkMs;
					changed = true;
				}
			}
			if (changed) m_vm.Changed();
		}

		CreditsViewModel m_vm;
		std::optional<Binding> m_binding;
		Rml::ElementDocument* m_doc = nullptr;
		bool     m_done = false;
		uint32_t m_last = 0;
		double   m_windowDp = 0;
		uint32_t m_nextBlink[NUM_PEOPLE_IN_CREDITS] = {};
		uint32_t m_openAt[NUM_PEOPLE_IN_CREDITS] = {};
	};
}

std::unique_ptr<Screen> CreateCreditsScreen()
{
	return std::make_unique<CreditsScreen>();
}

}
