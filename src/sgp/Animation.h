#pragma once

/** @file
 * Duration-based animations that present their frames — transitions, fades, zooms.
 *
 * The engine draws what someone can see. A windowed session presents every frame of an
 * animation to the player. A headless driven session composes its frame on demand when the
 * driver reads it (a screenshot, a pixel, the text on screen), and a read cannot happen while
 * an animation blocks: the frames in between are rendered for nobody. There the animation is
 * not drawn at all — the virtual clock ticks past it exactly as the presented frames would have
 * ticked it, and only the end state is drawn. A run is identical whether or not anyone watched.
 */

#include <chrono>
#include <cstdint>
#include <functional>

namespace sgp
{

/** Whether the frames of an animation are drawn: a window shows them (a player, or an automation
 * session run with -show). Headless, the frame is composed on demand by the next read. */
bool AnimationFramesObserved();

/**
 * Run a blocking animation of @a duration: @a draw(t) renders *and presents* exactly one frame
 * at progress @a t in [0, 1], deriving the frame from @a t alone (never from earlier frames).
 * The last call is exactly draw(1.0), the animation's end state.
 *
 * Presents advance the virtual clock one frame quantum each (Clock::OnPresent), so an animation
 * lasts exactly its duration of game time under virtual time; under the wall clock it lasts its
 * duration of real time. When no frame is observed, each frame that would have been presented
 * ticks the clock without drawing, and only draw(1.0) runs.
 */
void RunAnimation(std::chrono::milliseconds duration, std::function<void(double)> draw);

}
