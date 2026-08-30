#include "Pattern.h"

namespace sidecar::testpattern {

Bgra PixelAt(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t frame) {
  const uint32_t barY0 = (h / 2) - (kBarHeight / 2);
  const uint32_t barY1 = barY0 + kBarHeight;
  if (y >= barY0 && y < barY1) {
    const uint32_t barX = (frame * kBarSpeedPxPerFrame) % w;
    // Wrap the bar horizontally so it is always present in the frame.
    const uint32_t dx = (x + w - barX) % w;
    if (dx < kBarWidth) return Bgra{0, 0, 0, 255};
  }

  const bool right = x >= w / 2;
  const bool bottom = y >= h / 2;
  if (!right && !bottom) return Bgra{0, 0, 255, 255};      // red
  if (right && !bottom) return Bgra{0, 255, 0, 255};       // green
  if (!right && bottom) return Bgra{255, 0, 0, 255};       // blue
  return Bgra{255, 255, 255, 255};                          // white
}

}  // namespace sidecar::testpattern
