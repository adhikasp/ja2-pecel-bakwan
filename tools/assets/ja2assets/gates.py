"""Quality gates for recreated art (docs/plan/native-modern-game.md, "Asset methodology").

Each gate compares a candidate (HD) image with the original frame and returns a GateResult. Thresholds are
starting points for Phase 9 to tune per category; they are arguments, not constants baked into callers.
All images are ja2assets.png.Image (8-bit RGBA).
"""
from __future__ import annotations

from dataclasses import dataclass

from .png import Image


@dataclass
class GateResult:
    gate: str
    passed: bool
    value: float
    limit: float
    detail: str = ""

    def as_dict(self) -> dict:
        return {"gate": self.gate, "passed": self.passed, "value": round(self.value, 4), "limit": self.limit,
                "detail": self.detail}


def downscale(img: Image, factor: int) -> Image:
    """Box filter by an integer factor (alpha-weighted colour)."""
    w, h = img.width // factor, img.height // factor
    out = Image.blank(w, h)
    src = img.rgba
    for y in range(h):
        for x in range(w):
            r = g = b = a = 0
            for dy in range(factor):
                row = ((y * factor + dy) * img.width + x * factor) * 4
                for dx in range(factor):
                    i = row + dx * 4
                    pa = src[i + 3]
                    r += src[i] * pa
                    g += src[i + 1] * pa
                    b += src[i + 2] * pa
                    a += pa
            n = factor * factor
            o = (y * w + x) * 4
            if a:
                out.rgba[o:o + 4] = bytes((r // a, g // a, b // a, a // n))
    return out


def scale_multiple(original: Image, candidate: Image, allowed=(2, 3, 4, 6, 8)) -> GateResult:
    """The candidate is an exact integer multiple of the original in both axes (offsets and anchors stay exact)."""
    fx = candidate.width / original.width if original.width else 0
    fy = candidate.height / original.height if original.height else 0
    ok = fx == fy and fx in allowed and candidate.width % original.width == 0
    return GateResult("scale-multiple", ok, fx, 0, f"{original.width}x{original.height} -> {candidate.width}x{candidate.height}")


def _factor(original: Image, candidate: Image) -> int:
    return max(1, candidate.width // max(1, original.width))


def silhouette_match(original: Image, candidate: Image, min_iou: float = 0.97) -> GateResult:
    """Coverage (alpha > 50%) of the downscaled candidate against the original: intersection over union."""
    small = downscale(candidate, _factor(original, candidate))
    inter = union = 0
    for i in range(3, min(len(small.rgba), len(original.rgba)), 4):
        a = original.rgba[i] >= 128
        b = small.rgba[i] >= 128
        inter += a and b
        union += a or b
    iou = inter / union if union else 1.0
    return GateResult("silhouette", iou >= min_iou, iou, min_iou, "alpha IoU at the original size")


def colour_drift(original: Image, candidate: Image, max_mean: float = 12.0) -> GateResult:
    """Mean per-channel difference (0..255) of opaque pixels after downscaling the candidate."""
    small = downscale(candidate, _factor(original, candidate))
    total = n = 0
    o, s = original.rgba, small.rgba
    for i in range(0, min(len(o), len(s)), 4):
        if o[i + 3] >= 128 and s[i + 3] >= 128:
            total += abs(o[i] - s[i]) + abs(o[i + 1] - s[i + 1]) + abs(o[i + 2] - s[i + 2])
            n += 3
    mean = total / n if n else 0.0
    return GateResult("colour-drift", mean <= max_mean, mean, max_mean, "mean channel delta, opaque pixels")


def _centroid(img: Image) -> tuple[float, float]:
    sx = sy = n = 0
    for y in range(img.height):
        for x in range(img.width):
            if img.rgba[(y * img.width + x) * 4 + 3] >= 128:
                sx += x
                sy += y
                n += 1
    return (sx / n, sy / n) if n else (0.0, 0.0)


def frame_stability(originals: list[Image], candidates: list[Image], max_px: float = 0.75) -> GateResult:
    """Animations: the frame-to-frame motion of the silhouette centroid matches the original's (in original
    pixels), so upscaled frames do not wobble."""
    worst = 0.0
    for k in range(1, min(len(originals), len(candidates))):
        f = _factor(originals[k], candidates[k])
        (ox0, oy0), (ox1, oy1) = _centroid(originals[k - 1]), _centroid(originals[k])
        (cx0, cy0), (cx1, cy1) = _centroid(candidates[k - 1]), _centroid(candidates[k])
        d = abs((cx1 - cx0) / f - (ox1 - ox0)) + abs((cy1 - cy0) / f - (oy1 - oy0))
        worst = max(worst, d)
    return GateResult("frame-stability", worst <= max_px, worst, max_px, "worst centroid motion error")


def tile_seams(candidate: Image, neighbour: Image, edge: str = "right", max_mean: float = 10.0) -> GateResult:
    """Two tiles meant to join: the colour step across their shared edge (mean over the opaque edge pixels)."""
    a, b = candidate, neighbour
    total = n = 0
    if edge == "right":
        for y in range(min(a.height, b.height)):
            p, q = a.pixel(a.width - 1, y), b.pixel(0, y)
            if p[3] >= 128 and q[3] >= 128:
                total += sum(abs(p[c] - q[c]) for c in range(3))
                n += 3
    else:
        for x in range(min(a.width, b.width)):
            p, q = a.pixel(x, a.height - 1), b.pixel(x, 0)
            if p[3] >= 128 and q[3] >= 128:
                total += sum(abs(p[c] - q[c]) for c in range(3))
                n += 3
    mean = total / n if n else 0.0
    return GateResult("tile-seam", mean <= max_mean, mean, max_mean, f"{edge} edge")


def size_budget(nbytes: int, budget: int) -> GateResult:
    return GateResult("size-budget", nbytes <= budget, nbytes, budget, "bytes on disk")


def run_all(original: Image, candidate: Image, png_bytes: int | None = None, budget: int | None = None) -> list[GateResult]:
    results = [scale_multiple(original, candidate)]
    if results[0].passed:
        results += [silhouette_match(original, candidate), colour_drift(original, candidate)]
    if png_bytes is not None and budget is not None:
        results.append(size_budget(png_bytes, budget))
    return results
