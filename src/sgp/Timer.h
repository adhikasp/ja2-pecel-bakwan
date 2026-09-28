#ifndef TIMER_H
#define TIMER_H

#include "Types.h"
#include "Clock.h"

static inline UINT32 GetClock(void)
{
	return sgp::Clock::TicksMs();
}

#endif
