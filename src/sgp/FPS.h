#ifndef SGP_FPS_H
#define SGP_FPS_H

struct SDL_Renderer;
struct SDL_GPUTexture;
class SGPVObject;

namespace FPS
{
	using GameLoopFunc_t  = void (*)();
	using RenderPresent_t = bool (*)(SDL_Renderer *);

	extern GameLoopFunc_t  GameLoopPtr;
	extern RenderPresent_t RenderPresentPtr;

	void Init(GameLoopFunc_t gameLoop, SGPVObject * FPSDisplayFont);
	void ToggleOnOff();

	/** Whether the counter is on (a Ctrl+F toggle). */
	bool Enabled();
	/** Frames the counter has actually been drawn on, on whichever presentation path is active. A headless run
	 * draws none (no renderer and no compositor), so this stays where it was. */
	unsigned long long Drawn();

	/** The SDL_GPU compositor path: counts the frame, refreshes the counter's surface once a second and records
	 * its quad, mapping its canvas position through @a originX,@a originY and @a scale. Returns whether it
	 * recorded one. Called from the compositor before the render pass. */
	bool RecordGpuQuad(float originX, float originY, float scale);
}

#endif
