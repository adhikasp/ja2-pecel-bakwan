#include "Local.h"
#include "SysUtil.h"
#include "VSurface.h"
#include "UILayout.h"
#include "Video.h"


SGPVSurface* guiSAVEBUFFER;
SGPVSurface* guiWORLDSAVEBUFFER;
SGPVSurface* guiEXTRABUFFER;


void InitializeGameVideoObjects()
{
	guiSAVEBUFFER  = AddVideoSurface(SCREEN_WIDTH, SCREEN_HEIGHT, PIXEL_DEPTH);
	guiEXTRABUFFER = AddVideoSurface(SCREEN_WIDTH, SCREEN_HEIGHT, PIXEL_DEPTH);
	guiWORLDSAVEBUFFER = VideoIsLayered() ?
		AddVideoSurface(WORLD_SCREEN_WIDTH, WORLD_SCREEN_HEIGHT, PIXEL_DEPTH) : guiSAVEBUFFER;
}
