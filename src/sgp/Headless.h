#pragma once

/** @file
 * Headless mode: the engine runs without a window, renderer or audio device.
 * Everything above the platform layer is unchanged; the composited frame still
 * lands in the CPU-side ScreenBuffer, so screenshots and pixel reads work.
 */
namespace sgp
{
	void SetHeadless(bool);
	bool IsHeadless();
}
