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

double EaseProgress(int const frame, int const frames)
{
	if (frame <= 0 || frames <= 0) return 0.0;
	if (frame >= frames) return 1.0;
	double const t = double(frame) / double(frames);
	return 1.0 - std::pow(1.0 - t, 3.0);
}

void Ease::Start(Centre const from, Centre const to)
{
	m_from = from;
	m_target = to;
	m_frame = 0;
	m_active = from.x != to.x || from.y != to.y;
}

Centre Ease::Advance()
{
	if (!m_active) return m_target;

	++m_frame;
	// The arrival is the target itself, not a rounding of it: "it reaches" is the whole point of the ease.
	if (m_frame >= kEaseFrames)
	{
		m_active = false;
		return m_target;
	}

	double const p = EaseProgress(m_frame, kEaseFrames);
	return { m_from.x + (m_target.x - m_from.x) * p, m_from.y + (m_target.y - m_from.y) * p };
}
}
