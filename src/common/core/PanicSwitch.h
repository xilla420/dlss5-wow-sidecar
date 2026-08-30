#pragma once
#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>

namespace sidecar {

// True for the HRESULTs that mean the D3D device is gone and every resource
// derived from it is invalid.
bool IsDeviceLost(HRESULT hr);

// I13: a global hotkey that hides the overlay and stops the pipeline without
// the game ever losing focus.
//
// RegisterHotKey delivers WM_HOTKEY to this thread's own message queue. It
// installs no hook and loads nothing into any other process, so it satisfies
// I3 and I4.
//
// The hotkey belongs to the thread that registers it, and Pump drains that
// same thread's queue, so one thread must do both.
class PanicSwitch {
 public:
  static constexpr int kHotkeyId = 0xB00C;

  static std::unique_ptr<PanicSwitch> Create(std::function<void()> onPanic);
  ~PanicSwitch();

  // Drains WM_HOTKEY from this thread's queue.
  void Pump();
  bool Triggered() const { return triggered_.load(std::memory_order_acquire); }

 private:
  PanicSwitch() = default;

  std::function<void()> onPanic_;
  std::atomic<bool> triggered_{false};
  bool registered_ = false;
};

}  // namespace sidecar
