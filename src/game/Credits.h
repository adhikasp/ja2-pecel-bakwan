#pragma once

#include "Types.h"

/** Where a credited person's face is in the credits background (INTERFACEDIR/credits.sti, 640x480), where the eye
 * frames of "credit faces.sti" go, and how often the eyes blink (ms). The native credits screen uses it too. */
struct CreditFaceArea
{
	INT16  x, y, w, h;
	INT16  eyeX, eyeY;
	UINT16 blinkMs;
};
CreditFaceArea GetCreditFaceArea(int person);
