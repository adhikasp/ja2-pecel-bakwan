#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>

/** @file
 * The single source of time for the engine.
 *
 * In normal play every function here forwards to the wall clock. When the
 * automation layer enables virtual time, the clock only moves when the driver
 * steps frames (or when a modal loop presents a frame), which makes runs
 * reproducible, lets them go as fast as the CPU allows, and keeps the world
 * frozen while an external controller is thinking.
 *
 * Virtual time covers time the engine *measures*. WallSeconds() covers the time
 * it *shows*: the clock a save's filename is stamped with, the "Today 19:47"
 * next to a save. Those are absolute instants, so virtual time cannot make them
 * reproducible; FreezeWall() pins them instead.
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

	/** Wall-clock seconds since the Unix epoch: the time of day, which is what a
	 * save's filename and a "Today 19:47" label are made of. Unaffected by virtual
	 * time, because those are absolute instants rather than elapsed durations. */
	std::time_t WallSeconds();

	/** Pin WallSeconds() to @a seconds since the Unix epoch, so a driven run shows
	 * the same clock no matter when or where it was started. Pass 0 to go back to
	 * the real wall clock. See IsWallFrozen(). */
	void FreezeWall(std::time_t at);

	/** Whether WallSeconds() is pinned. Anything that formats an instant for the
	 * player — a date, a time of day, a filename — must format a frozen instant in
	 * UTC, because the same instant read in the build machine's timezone is a
	 * different string in Berlin than in Singapore. */
	bool IsWallFrozen();

	/** Seconds since the Unix epoch for a UTC civil date and time: the portable counterpart of
	 * timegm(), which Windows does not have and whose local-timezone relatives are exactly what
	 * makes a frozen instant unreproducible. @a month is 1-12 and @a day is 1-31.
	 * Empty when the date does not exist (month 13, 31 February). */
	std::optional<std::time_t> WallSecondsFromUtc(int year, int month, int day,
		int hour = 0, int minute = 0, int second = 0);

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

	/** How many presents happened since start-up (OnPresent calls, ticking or not). Lets a
	 * modal animation tell whether its frame draw presented at all. */
	uint64_t PresentCount();

	/** Total virtual time elapsed and frames stepped (virtual mode only). */
	uint64_t ElapsedMs();
	uint64_t FrameCount();

	/** Called with the elapsed time on every virtual advance (audio pump). */
	using AdvanceListener = void (*)(std::chrono::nanoseconds);
	void SetAdvanceListener(AdvanceListener);
}

}
