#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace sidecar {

class DeviceBridge;

// Windows Graphics Capture window source. Uses the OS capture API, which reads
// DWM's redirection surface and injects nothing into the target process.
//
// The target must be borderless windowed: exclusive fullscreen has no
// redirection surface and yields black frames (spec C1).
class WgcSource {
 public:
  using DropCallback = std::function<void()>;

  static std::unique_ptr<WgcSource> CreateForWindow(HWND target,
                                                    DeviceBridge& bridge,
                                                    DropCallback onDrop);
  ~WgcSource();

  void Start();
  void Stop();

  // True once the target window is gone. The runtime treats this as "WoW
  // exited". WGC's own Closed event is the fast path, but it does not fire
  // when the owning process is terminated outright, so the window itself is
  // also checked. IsWindow is a plain window-state query in the same category
  // as GetWindowLong (spec I5): no process handle, no hook, no memory access.
  bool IsClosed() const;
  uint64_t FramesDelivered() const { return delivered_.load(std::memory_order_relaxed); }

 private:
  WgcSource() = default;

  struct Impl;
  std::unique_ptr<Impl> impl_;
  HWND target_ = nullptr;
  std::atomic<bool> closed_{false};
  std::atomic<uint64_t> delivered_{0};
};

}  // namespace sidecar
