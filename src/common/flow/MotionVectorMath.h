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
// The negation is the whole point of this function. NVOFA answers "where did
// this pixel come from", so its vector points backwards in time. NGX motion
// vectors answer "where is this pixel going". Getting this backwards produces
// output that smears under motion and looks like a quality problem rather than
// a bug, which is exactly why it is tested here rather than discovered later.
constexpr MotionVec FlowToMotionPixels(int16_t rawX, int16_t rawY) {
  return MotionVec{-static_cast<float>(rawX) / kS10_5Scale,
                   -static_cast<float>(rawY) / kS10_5Scale};
}

// NGX reads MV_Scale_X/Y to decide whether vectors are pixels or NDC. This
// pipeline sends NDC, so the scale is the reciprocal of the full extent.
constexpr MotionVec MotionPixelsToNdc(MotionVec pixels, uint32_t width, uint32_t height) {
  return MotionVec{pixels.x / static_cast<float>(width),
                   pixels.y / static_cast<float>(height)};
}

}  // namespace sidecar
