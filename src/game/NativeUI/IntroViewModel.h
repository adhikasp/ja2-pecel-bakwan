#pragma once
// The intro/ending cinematic's view model (docs/ui/intro.md): the scene chain, the fading hint bar's state and
// where the presentation hands over to. The screen (IntroNative.cc) plays the chain; this is what it reports.

#include "IntroModel.h"
#include "ViewModel.h"

#include <functional>
#include <string>
#include <vector>

namespace NativeUI
{

	struct IntroSceneRow
	{
		int         index = 0;
		std::string id;
		bool        active = false; // the scene playing now
		static void Describe(RowFields<IntroSceneRow>& f)
		{
			f("index", &IntroSceneRow::index)("id", &IntroSceneRow::id)("active", &IntroSceneRow::active);
		}
	};

	class IntroViewModel : public ViewModel
	{
	public:
		IntroViewModel();
		void Describe(Fields&) override;

		/** The chain of @a kind as the rows the screen plays through. */
		void Load(IntroModel::Kind kind, bool miguelDead, bool skyriderDead);
		/** Points at scene @a index and refreshes the position line. */
		void Select(int index);

		std::string kind;  // "splash" | "beginning" | "ending"
		std::vector<IntroSceneRow> scenes;
		int    index = 0;      // the scene playing (0-based)
		double progress = 0;   // 0..1 of the scene; -1 when the flic's length is unknown (no track shown)
		bool   card = false;   // the scene stands as a still card: its video is missing, or cards were asked for
		bool   chrome = true;  // the hint bar is visible (it fades out when the player is idle)
		bool   finished = false;
		std::string position;  // "Scene 2 of 4"
		std::string cardNote;  // under the card ("video not available"), empty when cards were asked for
		std::string hint;      // the skip hint
		std::string exit;      // "INIT_SCREEN" | "EPILOGUE_SCREEN"

		/** Set by the screen: skip the scene playing, or leave for the exit screen at once. */
		std::function<void()> onNext, onSkipAll;

		/** Next scene (Space, click); the screen does the playing, this only moves the pointer. */
		void Next();
	};

}
