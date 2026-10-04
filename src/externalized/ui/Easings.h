#pragma once

#include <stdint.h>

float EaseInCubic(uint32_t uiStartTime, uint32_t uiEndTime, uint32_t uiCurrentTime);

/** The "gravity falling acceleration" easing of the JA2 screen transitions (the sector-load zoom,
 * the pre-battle and laptop power up/down ones): starts slow, falls through the middle, settles at
 * the end. Progress and result in [0, 1]. */
double EaseInOutGravity(double progress);
