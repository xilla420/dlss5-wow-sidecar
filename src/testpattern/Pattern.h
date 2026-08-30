#pragma once
#include <cstdint>

namespace sidecar::testpattern {

struct Bgra {
  uint8_t b, g, r, a;
};

// Fully deterministic. Four saturated quadrants give the capture path an
// unambiguous colour and orientation check; a black bar translating exactly
// four pixels per frame gives the optical-flow path a known ground truth.
constexpr uint32_t kBarWidth = 32;
constexpr uint32_t kBarSpeedPxPerFrame = 4;
constexpr uint32_t kBarHeight = 64;

Bgra PixelAt(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t frame);

}  // namespace sidecar::testpattern
