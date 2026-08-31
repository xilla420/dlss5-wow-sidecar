#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace sidecar {

// Pure rectangle arithmetic for the UI mask.
//
// The spec calls UI masking the largest quality risk in the project, so as with
// the motion-vector conversion the arithmetic lives here, testable without a
// GPU, and UiMaskBlend.hlsl is written to match it rather than the other way
// round.
//
// Two consumers, not one:
//   - the blend pass, which writes original pixels back over the interface;
//   - DLSS's bias-current-colour mask, since optical-flow vectors estimated over
//     interface elements are exactly the vectors worth distrusting.

// Half-open, matching the Win32 RECT convention the calibration UI produces:
// left and top are inside, right and bottom are not.
struct MaskRect {
  int32_t left = 0;
  int32_t top = 0;
  int32_t right = 0;
  int32_t bottom = 0;
};

constexpr bool IsEmpty(const MaskRect& r) {
  return r.right <= r.left || r.bottom <= r.top;
}

constexpr bool Covers(const MaskRect& r, int32_t x, int32_t y) {
  return !IsEmpty(r) && x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

inline bool CoveredByAny(const std::vector<MaskRect>& rects, int32_t x, int32_t y) {
  for (const auto& r : rects) {
    if (Covers(r, x, y)) return true;
  }
  return false;
}

constexpr MaskRect ClampToFrame(const MaskRect& r, int32_t width, int32_t height) {
  MaskRect out;
  out.left = std::clamp(r.left, 0, width);
  out.top = std::clamp(r.top, 0, height);
  out.right = std::clamp(r.right, 0, width);
  out.bottom = std::clamp(r.bottom, 0, height);
  return out;
}

// The operator calibrates at their desktop resolution; the pass may run at a
// different internal one. A mask that did not scale would sit off the interface
// entirely.
//
// Rounds outward, and never collapses a non-empty rectangle to nothing: masking
// one pixel too many is invisible, whereas a thin element that stops being
// masked at all is not.
constexpr MaskRect ScaleRect(const MaskRect& r, int32_t fromWidth, int32_t fromHeight,
                             int32_t toWidth, int32_t toHeight) {
  if (fromWidth <= 0 || fromHeight <= 0) return r;
  if (fromWidth == toWidth && fromHeight == toHeight) return r;

  const int64_t left = static_cast<int64_t>(r.left) * toWidth / fromWidth;
  const int64_t top = static_cast<int64_t>(r.top) * toHeight / fromHeight;
  // Ceiling on the far edges, so a rectangle never shrinks below its source.
  const int64_t right =
      (static_cast<int64_t>(r.right) * toWidth + fromWidth - 1) / fromWidth;
  const int64_t bottom =
      (static_cast<int64_t>(r.bottom) * toHeight + fromHeight - 1) / fromHeight;

  MaskRect out;
  out.left = static_cast<int32_t>(left);
  out.top = static_cast<int32_t>(top);
  out.right = static_cast<int32_t>(right);
  out.bottom = static_cast<int32_t>(bottom);

  if (!IsEmpty(r)) {
    if (out.right <= out.left) out.right = out.left + 1;
    if (out.bottom <= out.top) out.bottom = out.top + 1;
  }
  return out;
}

// Mask strength at a pixel, in [0, 1]. 1 means "this is interface, keep the
// original pixel"; 0 means "this is world, keep the neural output".
//
// feather is the width in pixels of the ramp just inside each edge. It exists
// because a hard boundary between a re-lit image and an untouched one is
// conspicuous, especially where it cuts across a gradient.
inline float CoverageAt(const MaskRect& r, int32_t x, int32_t y, int32_t feather) {
  if (!Covers(r, x, y)) return 0.0f;
  if (feather <= 0) return 1.0f;

  // Distance to the nearest edge, in pixels, with 0 meaning "on the edge pixel".
  const int32_t dx = std::min(x - r.left, r.right - 1 - x);
  const int32_t dy = std::min(y - r.top, r.bottom - 1 - y);
  const int32_t d = std::min(dx, dy);

  // A rectangle thinner than twice the feather has no full-strength interior;
  // the ramp simply never reaches 1, which is correct and stays in range.
  const float t = static_cast<float>(d + 1) / static_cast<float>(feather + 1);
  return t >= 1.0f ? 1.0f : t;
}

// Union by strongest, not by sum: a pixel under two overlapping panels is
// interface once. Summing would over-weight the overlap in the blend.
inline float CoverageAtAny(const std::vector<MaskRect>& rects, int32_t x, int32_t y,
                           int32_t feather) {
  float best = 0.0f;
  for (const auto& r : rects) {
    best = std::max(best, CoverageAt(r, x, y, feather));
    if (best >= 1.0f) return 1.0f;
  }
  return best;
}

}  // namespace sidecar
