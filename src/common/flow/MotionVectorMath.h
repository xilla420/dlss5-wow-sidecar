#pragma once
#include <cstdint>

namespace sidecar {

struct MotionVec {
  float x = 0.0f;
  float y = 0.0f;
};

// NVOFA emits S10.5 fixed point: 32 raw units per pixel.
inline constexpr float kS10_5Scale = 32.0f;

// Decodes a raw flow vector to pixels in the convention NGX expects.
//
// No negation, and that is the point of this function. NVOFA is run with the
// current frame as input and the previous one as reference, so its vector at a
// pixel points to where that pixel was a frame ago: prev = curr + flow. NGX
// motion vectors use exactly the same convention -- DLSS, FSR and the
// DLSS5-Feeder prior art all read history at `uv + mv`.
//
// This used to negate, on the reading that NGX wants "where the pixel is
// going". The hardware measurement the negation was justified by (NVOFA
// returning -4.5 px for a bar moving +4 px a frame) was correct; the
// conclusion drawn from it was not. A negated field makes DLSS fetch history
// from twice the real motion away, which shows up as translucent trails behind
// anything that moves and as smearing under camera rotation -- reported in
// 0.4.0 and 0.5.0 as "temporal ghosting".
//
// `invert` exists for diagnosis only (sidecar.toml `invert_motion`), so the
// direction can be checked on hardware without a rebuild.
constexpr MotionVec FlowToMotionPixels(int16_t rawX, int16_t rawY, bool invert = false) {
  const float sign = invert ? -1.0f : 1.0f;
  return MotionVec{sign * static_cast<float>(rawX) / kS10_5Scale,
                   sign * static_cast<float>(rawY) / kS10_5Scale};
}

// NGX reads MV_Scale_X/Y to decide whether vectors are pixels or NDC. This
// pipeline sends NDC, so the scale is the reciprocal of the full extent.
constexpr MotionVec MotionPixelsToNdc(MotionVec pixels, uint32_t width, uint32_t height) {
  return MotionVec{pixels.x / static_cast<float>(width),
                   pixels.y / static_cast<float>(height)};
}

// Where history for a pixel is read from, in pixels. The relation the whole
// convention exists to get right, stated once so tests can pin it.
constexpr MotionVec HistoryPosition(MotionVec current, MotionVec motionPixels) {
  return MotionVec{current.x + motionPixels.x, current.y + motionPixels.y};
}

}  // namespace sidecar
