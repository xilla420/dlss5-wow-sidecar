#pragma once
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

namespace sidecar {

struct HudModel {
  double p50Ms = 0.0;
  double p99Ms = 0.0;
  uint64_t frames = 0;
  uint64_t drops = 0;
  const char* passName = "";
  const char* gpuName = "";
  // Which neural runtime build is live. Two builds share a version string and a
  // byte count and differ only by digest, and one of them cannot run on Ada at
  // all, so naming it on screen is the difference between "it works" and "it
  // works and I know why".
  const char* runtimeVariant = "";
};

// Spec M1: above roughly 80 ms capture-to-present, live overlay is not the
// product. Below 40 ms it is comfortable; between the two it is a judgement
// call the operator makes with the game in front of them.
enum class GateVerdict { Playable, Marginal, Failed };
GateVerdict JudgeGate(double p99Ms);

// Pure, so the numbers can be asserted without a window.
std::string FormatHud(const HudModel& model);

// A small layered tool window. Deliberately separate from DCompOverlay: that
// window hosts a flip swapchain and must stay simple, and the HUD has to
// survive a failure that hides the overlay.
class Hud {
 public:
  static std::unique_ptr<Hud> Create();
  ~Hud();

  void Update(const HudModel& model);
  void Show();
  void Hide() noexcept;
  HWND Hwnd() const { return hwnd_; }

  const std::string& TextForPaint() const { return text_; }

 private:
  Hud() = default;

  HWND hwnd_ = nullptr;
  std::string text_;
};

}  // namespace sidecar
