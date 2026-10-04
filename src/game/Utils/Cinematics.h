#ifndef CINEMATICS_H
#define CINEMATICS_H

#include "Types.h"


struct SMKFLIC;

void     SmkInitialize(void);
void     SmkShutdown(void);
SMKFLIC* SmkPlayFlic(const char* filename, UINT32 left, UINT32 top, BOOLEAN auto_close);
BOOLEAN  SmkPollFlics(void);
void     SmkCloseFlic(SMKFLIC*);
/** How far the flic has played, 0..1 (frames played over frames in the file), or -1 when its length is unknown.
 * The native intro screen draws its scene progress from this (docs/ui/intro.md). */
double   SmkProgress(SMKFLIC*);

#endif
