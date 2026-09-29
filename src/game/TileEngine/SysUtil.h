#ifndef __SYSTEM_UTILS
#define __SYSTEM_UTILS

#include "Types.h"


extern SGPVSurface* guiSAVEBUFFER;
/** The static world (and what was drawn into it once): the "save buffer" of the world. The same surface as
 * guiSAVEBUFFER unless the world is a layer of its own. */
extern SGPVSurface* guiWORLDSAVEBUFFER;
extern SGPVSurface* guiEXTRABUFFER;

void InitializeGameVideoObjects(void);
/** The screen or the world buffer has another size: the buffers follow (their contents are lost). */
void ResizeGameVideoObjects(void);

#endif
