#include "RmlCommon.h"
#include "UiSpike.h"

#include <vector>

namespace spike {

void InitRmlOnce()
{
	static bool done = false;
	if (done) return;
	done = true;
	nui::InitRml();
	// Font data must outlive Rml::Shutdown, which we never call: static storage.
	static std::vector<unsigned char> regular = LoadAssetBytes("LatoLatin-Regular.ttf");
	static std::vector<unsigned char> bold    = LoadAssetBytes("LatoLatin-Bold.ttf");
	Rml::LoadFontFace({ regular.data(), regular.size() }, "Lato", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true);
	Rml::LoadFontFace({ bold.data(), bold.size() }, "Lato", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold);
}

} // namespace spike
