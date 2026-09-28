#include "Headless.h"

namespace sgp
{
	static bool g_headless = false;

	void SetHeadless(bool const headless) { g_headless = headless; }
	bool IsHeadless() { return g_headless; }
}
