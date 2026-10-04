#include <algorithm>
#include <stdint.h>

#include "Debug.h"
#include "Easings.h"

float EaseInCubic(uint32_t uiStartTime, uint32_t uiEndTime, uint32_t uiCurrentTime) {
	Assert(uiEndTime >= uiStartTime);
	Assert(uiCurrentTime <= uiEndTime);

	float fProgress = (float)(uiCurrentTime - uiStartTime) / (float)(uiEndTime - uiStartTime);

	if (fProgress < 0.5) {
		return 4.0f * fProgress * fProgress * fProgress;
	} else {
		return (fProgress - 1.0f) * (2.0f * fProgress - 2.0f) * (2.0f * fProgress - 2.0f) + 1.0f;
	}
}

double EaseInOutGravity(double progress) {
	// The legacy transition formula (percent domain, "+0.5"/"+0.05" constants and all) is a
	// quadratic ease-in over the first half and a quadratic ease-out over the second, with a small
	// jump at the midpoint where the two pieces meet. This is the curve it approximates.
	double const t = std::clamp(progress, 0.0, 1.0);
	return t < 0.5 ? 2.0 * t * t : 1.0 - 2.0 * (1.0 - t) * (1.0 - t);
}
