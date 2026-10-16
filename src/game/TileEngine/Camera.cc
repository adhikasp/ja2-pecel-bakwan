#include "Camera.h"

#include <cmath>

namespace Camera
{
	namespace
	{
		double Snap(double const v, double const size)
		{
			return std::floor(v / size) * size + size / 2.0;
		}
	}

	Centre SnapToTileCentre(Centre const c)
	{
		return { Snap(c.x, CELL_X_SIZE), Snap(c.y, CELL_Y_SIZE) };
	}

	bool OnTileCentre(Centre const c)
	{
		return c.x == Snap(c.x, CELL_X_SIZE) && c.y == Snap(c.y, CELL_Y_SIZE);
	}
}
