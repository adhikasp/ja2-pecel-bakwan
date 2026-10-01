#include "Clock.h"
#include "Logger.h"
#include <SDL3/SDL_timer.h>
#include <thread>

namespace sgp
{

namespace
{
	bool                     g_virtual = false;
	std::chrono::nanoseconds g_quantum{ std::chrono::milliseconds{16} };
	// Start at one second so that code treating 0 as "unset" keeps working.
	std::chrono::nanoseconds g_now{ std::chrono::seconds{1} };
	uint64_t                 g_frames = 0;
	bool                     g_inFrame = false;
	unsigned                 g_presentsThisFrame = 0;
	Clock::AdvanceListener   g_listener = nullptr;

	// Busy-wait guard: a loop polling the clock without presenting a frame
	// would spin forever under virtual time. After this many reads without
	// time moving, nudge the clock forward by a millisecond.
	constexpr unsigned SPIN_LIMIT = 200'000;
	unsigned           g_readsSinceAdvance = 0;
	bool               g_spinWarned = false;

	void CountRead()
	{
		if (++g_readsSinceAdvance < SPIN_LIMIT) return;
		if (!g_spinWarned)
		{
			SLOGW("Virtual clock: code is busy-waiting on the clock; advancing time to break the loop");
			g_spinWarned = true;
		}
		Clock::Advance(std::chrono::milliseconds{1});
	}
}

GameClock::time_point GameClock::now() noexcept
{
	if (!g_virtual)
	{
		return time_point{ std::chrono::duration_cast<duration>(
			std::chrono::steady_clock::now().time_since_epoch()) };
	}
	CountRead();
	return time_point{ g_now };
}

namespace Clock
{

void EnableVirtual(std::chrono::nanoseconds const quantum)
{
	g_virtual = true;
	g_quantum = quantum;
	SLOGI("Virtual clock enabled, frame quantum {} us",
		std::chrono::duration_cast<std::chrono::microseconds>(quantum).count());
}

bool IsVirtual() { return g_virtual; }

void DisableVirtual()
{
	g_virtual = false;
	SLOGI("Virtual clock disabled: the wall clock from now on");
}

std::chrono::nanoseconds Quantum() { return g_quantum; }

uint32_t TicksMs()
{
	if (!g_virtual) return static_cast<uint32_t>(SDL_GetTicks());
	CountRead();
	return static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(g_now).count());
}

void Sleep(std::chrono::milliseconds const d)
{
	if (g_virtual) Advance(d);
	else std::this_thread::sleep_for(d);
}

void Advance(std::chrono::nanoseconds const d)
{
	if (!g_virtual || d.count() <= 0) return;
	g_now += d;
	g_readsSinceAdvance = 0;
	if (g_listener) g_listener(d);
}

void BeginFrame()
{
	g_inFrame = true;
	g_presentsThisFrame = 0;
}

void EndFrame()
{
	g_inFrame = false;
	++g_frames;
	Advance(g_quantum);
}

void OnPresent()
{
	if (!g_virtual) return;
	// The first present of a frame is the regular one; time for it is
	// accounted for in EndFrame(). Any further presents come from a modal
	// loop that animates by polling the clock.
	if (!g_inFrame || g_presentsThisFrame++ > 0) Advance(g_quantum);
}

uint64_t ElapsedMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		g_now - std::chrono::seconds{1}).count();
}

uint64_t FrameCount() { return g_frames; }

void SetAdvanceListener(AdvanceListener const l) { g_listener = l; }

}

}
