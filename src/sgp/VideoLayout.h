#ifndef VIDEO_LAYOUT_H
#define VIDEO_LAYOUT_H

// Pure (SDL-free) display arithmetic: how the physical window size, the UI scale
// factor and the logical canvas size (SCREEN_WIDTH x SCREEN_HEIGHT) relate.

#include <algorithm>

namespace VideoLayout
{

constexpr int MIN_LOGICAL_WIDTH  = 640;
constexpr int MIN_LOGICAL_HEIGHT = 480;

// "auto" scale keeps the logical canvas at least this big
constexpr int AUTO_MIN_LOGICAL_WIDTH  = 1280;
constexpr int AUTO_MIN_LOGICAL_HEIGHT = 720;

constexpr int MAX_UI_SCALE = 4;

// UI scale value meaning "pick for me"
constexpr int UI_SCALE_AUTO = 0;

struct Size
{
	int w;
	int h;
	constexpr bool operator==(Size const&) const = default;
};

struct DisplayLayout
{
	Size logical; // size of the canvas the game renders into
	int  scale;   // effective integer scale (>= 1); logical * scale <= window unless clamped
	constexpr bool operator==(DisplayLayout const&) const = default;
};

/** Largest scale in 1..maxScale for which floor(window / scale) >= min. At least 1. */
constexpr int LargestScaleKeeping(Size const window, Size const min, int const maxScale = MAX_UI_SCALE)
{
	int scale = 1;
	for (int s = 2; s <= maxScale; ++s)
	{
		if (window.w / s >= min.w && window.h / s >= min.h) scale = s;
	}
	return scale;
}

/**
 * Logical canvas size for a window of the given size in physical pixels.
 *
 * logical = floor(window / scale), clamped to at least 640x480.
 * uiScale is UI_SCALE_AUTO (0) or 1..MAX_UI_SCALE. Auto picks the largest scale
 * that keeps the logical size >= 1280x720 (on small windows: 1). An explicit scale
 * is reduced when it would push the logical size below 640x480.
 */
constexpr DisplayLayout ComputeDisplayLayout(Size const window, int const uiScale)
{
	int scale;
	if (uiScale == UI_SCALE_AUTO)
	{
		scale = LargestScaleKeeping(window, { AUTO_MIN_LOGICAL_WIDTH, AUTO_MIN_LOGICAL_HEIGHT });
	}
	else
	{
		int const wanted = std::clamp(uiScale, 1, MAX_UI_SCALE);
		scale = std::min(wanted, LargestScaleKeeping(window, { MIN_LOGICAL_WIDTH, MIN_LOGICAL_HEIGHT }, wanted));
	}

	Size logical{ window.w / scale, window.h / scale };
	logical.w = std::max(logical.w, MIN_LOGICAL_WIDTH);
	logical.h = std::max(logical.h, MIN_LOGICAL_HEIGHT);
	return { logical, scale };
}

/** World zoom value meaning "same layer and scale as the UI" (the classic single layer). */
constexpr int WORLD_ZOOM_MATCH_UI = 0;

/** Integer division rounding towards negative infinity (world positions can be negative). */
constexpr int FloorDiv(int const a, int const b)
{
	int const q = a / b;
	return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

/**
 * The layers of the picture: the UI canvas (logical size, drawn at the UI scale Su) and, when the
 * world has its own scale Zw, a world layer of `window / Zw` pixels underneath it. Both cover the
 * same physical area (the canvas: UI size * Su). When the world follows the UI there is just one
 * layer: world == ui, Zw == Su.
 */
struct LayerLayout
{
	Size ui;         // UI canvas in UI pixels (SCREEN_WIDTH x SCREEN_HEIGHT)
	Size world;      // world buffer in world pixels; == ui when not layered
	Size canvas;     // ui * uiScale: the physical area both layers fill
	int  uiScale;    // Su
	int  worldZoom;  // Zw; == uiScale when not layered
	bool layered;    // world and UI are separate layers
	constexpr bool operator==(LayerLayout const&) const = default;
};

/** `worldZoom` is WORLD_ZOOM_MATCH_UI or 1..MAX_UI_SCALE. A zoom equal to the UI scale is not layered. */
constexpr LayerLayout ComputeLayerLayout(DisplayLayout const ui, int const worldZoom)
{
	Size const canvas{ ui.logical.w * ui.scale, ui.logical.h * ui.scale };
	int const zw = worldZoom == WORLD_ZOOM_MATCH_UI ? ui.scale : std::clamp(worldZoom, 1, MAX_UI_SCALE);
	if (zw == ui.scale) return { ui.logical, ui.logical, canvas, ui.scale, ui.scale, false };
	// Rounded up so that the world covers the whole canvas.
	Size const world{ (canvas.w + zw - 1) / zw, (canvas.h + zw - 1) / zw };
	return { ui.logical, world, canvas, ui.scale, zw, true };
}

/** The same layers, but the world a layer of its own even at the UI scale (for renderers that draw the world
 * separately, see WorldRender.h). */
constexpr LayerLayout ForceLayered(LayerLayout l)
{
	l.layered = true;
	return l;
}

/** A position in UI pixels expressed in world pixels (and back): both cover the same area. */
constexpr int UiToWorld(int const v, int const uiScale, int const worldZoom)
{
	return FloorDiv(v * uiScale, worldZoom);
}

constexpr int WorldToUi(int const v, int const uiScale, int const worldZoom)
{
	return FloorDiv(v * worldZoom, uiScale);
}

/** How the logical canvas is put on a window of some (physical) size. */
struct Presentation
{
	// Integer factor: window fits logical * k with less than k leftover pixels per axis,
	// so nearest-neighbour scaling plus a neutral border is pixel exact.
	bool integerFit;
	// Largest integer multiple of logical that fits the window (0 if the window is smaller
	// than the canvas). The sharp-bilinear intermediate texture is logical * k.
	int  k;
};

constexpr Presentation ComputePresentation(Size const window, Size const logical)
{
	int const k = std::min(window.w / logical.w, window.h / logical.h);
	bool const fit = k >= 1
		&& window.w - logical.w * k < k
		&& window.h - logical.h * k < k;
	return { fit, k };
}

/** Window sizes to request: `auto` means the desktop size. */
constexpr Size ResolveWindowSize(int const resX, int const resY, Size const desktop)
{
	return (resX <= 0 || resY <= 0) ? desktop : Size{ resX, resY };
}


/** Size of a regular window when the resolution is `auto`: most of the desktop, not all of it. */
constexpr Size DefaultWindowedSize(Size const desktop)
{
	return { std::max(desktop.w * 85 / 100, MIN_LOGICAL_WIDTH), std::max(desktop.h * 85 / 100, MIN_LOGICAL_HEIGHT) };
}

}

#endif
