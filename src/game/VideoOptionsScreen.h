#ifndef VIDEO_OPTIONS_SCREEN_H
#define VIDEO_OPTIONS_SCREEN_H

// Video settings that can be changed while the game runs: the "Video" screen reachable from the
// options screen, the tactical world zoom hotkeys, the automation hook and window resizes.

#include "ScreenIDs.h"
#include "Video.h"

#include <string_theory/string>

/** The Video sub-screen of the options screen. */
ScreenID VideoOptionsScreenHandle(void);

/** Applies video settings without restarting: window mode and size, UI scale, world zoom, scaling filter.
 * Rebuilds the surfaces and textures, recomputes the layout and re-enters the current screen.
 * Must be called between frames, never from a mouse or button callback (use RequestVideoSettings there).
 * With `persist` the settings are also written to ja2.json. Returns false and fills `error` when the
 * current screen does not support a change (nothing is changed then). */
bool ChangeVideoSettings(VideoDisplaySettings const& want, VideoScaleQuality quality, bool persist,
			ST::string* error = nullptr, bool resizeWindow = true);

/** Like ChangeVideoSettings, at the start of the next frame (safe from anywhere). */
void RequestVideoSettings(VideoDisplaySettings const& want, VideoScaleQuality quality, bool persist);

/** Tactical hotkeys: one step up or down in the world zoom (the world's scale, independent of the UI's). */
void StepWorldZoom(int delta);

/** The window was resized or moved to another display (or the desktop changed). */
void VideoNotifyWindowChanged(void);

/** Once per frame, first thing: applies what was requested and follows window changes when the UI scale is auto. */
void HandlePendingVideoChanges(void);

/** A change was requested and has not been applied yet. */
bool VideoChangePending(void);

#endif
