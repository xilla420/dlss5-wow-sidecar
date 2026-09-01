#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace sidecar {

class DeviceBridge;

// A transparent, click-through, non-activating overlay presented through
// DirectComposition.
//
// The window is created WS_EX_NOREDIRECTIONBITMAP and composed by
// DirectComposition rather than through a redirection surface (spec C2), and
// WS_EX_NOACTIVATE keeps the game focused so it never drops to its background
// frame limit.
//
// It is also WS_EX_LAYERED, which is what makes it click-through for the game.
// Answering HTTRANSPARENT is not enough: that only falls through to windows on
// the same thread, so it satisfies a same-thread test while every real click
// stops dead at the overlay. WS_EX_LAYERED | WS_EX_TRANSPARENT is the
// combination the system honours across processes. See DCompOverlay.cpp, and
// tools/spike_clickthrough for the measurement.
class DCompOverlay {
 public:
  static std::unique_ptr<DCompOverlay> Create(DeviceBridge& bridge,
                                              uint32_t width, uint32_t height);
  ~DCompOverlay();

  HWND Hwnd() const { return hwnd_; }

  void SetBounds(const RECT& screenRect);
  void Show();

  // The failure path required by the spec's failure rule. Safe from any state,
  // safe to call repeatedly, and must never throw.
  void Hide() noexcept;
  bool IsVisible() const { return visible_; }

  // Copies frame into the back buffer and presents. waitFenceValue is the
  // DeviceBridge shared-fence value the queue must wait on first.
  void Present(ID3D12Resource* frame, uint64_t waitFenceValue);

  // Where the last Present's wall time went. Three numbers, because they have
  // three different causes and three different fixes: waiting on the
  // compositor is pacing, recording is our own CPU cost, and waiting on the
  // fence is the GPU being behind. Guessing which one dominates is how a
  // throughput problem gets misdiagnosed as a quality one.
  struct PresentTiming {
    double latencyWaitMs = 0.0;
    double recordMs = 0.0;
    double gpuWaitMs = 0.0;
  };
  PresentTiming LastTiming() const { return timing_; }

 private:
  DCompOverlay() = default;

  DeviceBridge* bridge_ = nullptr;
  HWND hwnd_ = nullptr;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  bool visible_ = false;

  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
  Microsoft::WRL::ComPtr<IDCompositionDevice> dcompDevice_;
  Microsoft::WRL::ComPtr<IDCompositionTarget> dcompTarget_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> dcompVisual_;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList_;
  Microsoft::WRL::ComPtr<ID3D12Fence> presentFence_;
  uint64_t presentFenceValue_ = 0;
  HANDLE frameLatencyWaitable_ = nullptr;
  PresentTiming timing_;
};

}  // namespace sidecar
