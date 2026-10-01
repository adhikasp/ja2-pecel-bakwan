#pragma once

#include <chrono>
#include <cstdint>

/** @file
 * The single source of time for the engine.
 *
 * In normal play every function here forwards to the wall clock. When the
 * automation layer enables virtual time, the clock only moves when the driver
 * steps frames (or when a modal loop presents a frame), which makes runs
 * reproducible, lets them go as fast as the CPU allows, and keeps the world
 * frozen while an external controller is thinking.
 */
namespace sgp
{

/** A std::chrono clock that is either the steady wall clock or virtual time. */
struct GameClock
{
	using duration   = std::chrono::nanoseconds;
	using rep        = duration::rep;
	using period     = duration::period;
	using time_point = std::chrono::time_point<GameClock>;
	static constexpr bool is_steady = true;

	static time_point now() noexcept;
};

namespace Clock
{
	/** Switch to virtual time. Each frame advances time by @a quantum. */
	void EnableVirtual(std::chrono::nanoseconds quantum);
	bool IsVirtual();
	/** Back to the wall clock (frame-rate measurements in a driven window). Time jumps forward. */
	void DisableVirtual();
	std::chrono::nanoseconds Quantum();

	/** Milliseconds since start; what GetClock()/SDL_GetTicks() used to return. */
	uint32_t TicksMs();

	/** Virtual: advance time and return immediately. Real: sleep. */
	void Sleep(std::chrono::milliseconds);

	/** Advance virtual time (no-op in real mode). */
	void Advance(std::chrono::nanoseconds);

	/** Bracket one iteration of the main loop. EndFrame advances one quantum. */
	void BeginFrame();
	void EndFrame();

	/** Called whenever a frame is presented. Presenting more than once per
	 * frame means a modal loop is animating on its own, so virtual time must
	 * move or that loop would never finish. */
	void OnPresent();

	/** Total virtual time elapsed and frames stepped (virtual mode only). */
	uint64_t ElapsedMs();
	uint64_t FrameCount();

	/** Called with the elapsed time on every virtual advance (audio pump). */
	using AdvanceListener = void (*)(std::chrono::nanoseconds);
	void SetAdvanceListener(AdvanceListener);
}

}
