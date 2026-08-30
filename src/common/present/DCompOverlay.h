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
// WS_EX_LAYERED cannot host a DXGI flip swapchain, so the window is created
// WS_EX_NOREDIRECTIONBITMAP and composed by DirectComposition instead
// (spec C2). WS_EX_NOACTIVATE keeps the game focused so it never drops to its
// background frame limit.
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
};

}  // namespace sidecar
