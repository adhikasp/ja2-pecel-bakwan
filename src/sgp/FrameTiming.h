#pragma once
// A sample accumulator for a per-frame host-time instrument (the native UI layer's own cost, and anything else that
// wants a distribution rather than a mean). The samples are kept so that p95 and the worst frame are readable: a
// mean hides the stutter a frame budget is about. The ring keeps the newest samples and never grows past its
// capacity; `frames` still counts every Add, so a long run cannot silently report a shorter one.
//
// It is deliberately not thread safe and does no locking: it is fed from the frame loop and read from the same thread
// (ja2ctl/automation), which is how the other timing surfaces in the tree work too (WorldRenderTiming).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sgp
{
class FrameTiming
{
  public:
	struct Stats
	{
		std::uint64_t frames = 0; // every Add since the last reset, including the samples the ring dropped
		int samples = 0;          // samples the statistics were taken over
		double meanMs = 0, p50Ms = 0, p95Ms = 0, maxMs = 0;
	};

	// The default holds a little under an hour of frames at 60 Hz. `capacity` is there for the unit tests (and a
	// caller that wants a shorter window); it is never zero.
	explicit FrameTiming(std::size_t const capacity = 100000) : capacity_(capacity ? capacity : 1) {}

	void Add(double const ms)
	{
		++frames_;
		if (samples_.size() < capacity_)
			samples_.push_back(ms);
		else
			samples_[next_] = ms;
		next_ = (next_ + 1) % capacity_;
	}

	/** The distribution now, and with @a reset a fresh measurement starts (frame count included). */
	Stats Take(bool const reset)
	{
		Stats s;
		s.frames = frames_;
		s.samples = int(samples_.size());
		if (!samples_.empty())
		{
			std::vector<double> v = samples_;
			std::sort(v.begin(), v.end());
			double sum = 0;
			for (double const x : v) sum += x;
			s.meanMs = sum / double(v.size());
			s.p50Ms = v[v.size() / 2];
			s.p95Ms = v[std::min(v.size() - 1, v.size() * 95 / 100)];
			s.maxMs = v.back();
		}
		if (reset) Reset();
		return s;
	}

	void Reset()
	{
		samples_.clear();
		next_ = 0;
		frames_ = 0;
	}

  private:
	std::size_t capacity_;
	std::vector<double> samples_;
	std::size_t next_ = 0;
	std::uint64_t frames_ = 0;
};
}
