#include "present/DCompOverlay.h"

#include <chrono>

#include "gpu/DeviceBridge.h"

using Microsoft::WRL::ComPtr;

namespace sidecar {
namespace {

constexpr wchar_t kClassName[] = L"SidecarOverlay";

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  // Belt to the styles' braces, and not the part that does the work.
  //
  // HTTRANSPARENT only falls through "to underlying windows in the same
  // thread". The game is another process, so this answer alone routes nothing:
  // what actually excludes the overlay from the game's hit test is
  // WS_EX_LAYERED | WS_EX_TRANSPARENT, set at creation. This stays because it
  // costs nothing and is right for anything asking from our own thread.
  if (msg == WM_NCHITTEST) return HTTRANSPARENT;
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void EnsureClassRegistered() {
  static bool registered = false;
  if (registered) return;
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = OverlayProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);
  registered = true;
}

}  // namespace

std::unique_ptr<DCompOverlay> DCompOverlay::Create(DeviceBridge& bridge,
                                                   uint32_t width, uint32_t height) {
  EnsureClassRegistered();

  std::unique_ptr<DCompOverlay> o(new DCompOverlay());
  o->bridge_ = &bridge;
  o->width_ = width;
  o->height_ = height;

  // WS_EX_LAYERED is load-bearing and was missing until it was measured.
  //
  // It is the only thing that excludes this window from *another process's*
  // hit test, which is the only hit test that matters: the player clicking on
  // World of Warcraft underneath. Without it every click stopped dead at the
  // overlay, and because WS_EX_NOACTIVATE means the overlay does not even take
  // focus in exchange, the click went nowhere at all -- and with the game never
  // focused, neither did the keyboard.
  //
  // It was left out on the belief that a layered window cannot host a flip
  // swapchain. That is true of CreateSwapChainForHwnd and irrelevant here: this
  // swapchain comes from CreateSwapChainForComposition and is bound to a
  // DirectComposition visual, never to the window. tools/spike_clickthrough
  // measures all three combinations and shows this one is both visible and
  // click-through.
  o->hwnd_ = CreateWindowExW(
      WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT |
          WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
      kClassName, L"", WS_POPUP,
      0, 0, static_cast<int>(width), static_cast<int>(height),
      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!o->hwnd_) return nullptr;

  // Fully opaque. DirectComposition does the compositing, so this changes no
  // pixels -- but a layered window with no attributes set is never composed at
  // all, so it has to be here.
  SetLayeredWindowAttributes(o->hwnd_, 0, 255, LWA_ALPHA);

  ComPtr<IDXGIFactory2> factory;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return nullptr;

  DXGI_SWAP_CHAIN_DESC1 scd{};
  scd.Width = width;
  scd.Height = height;
  scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  scd.SampleDesc.Count = 1;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  // Three buffers and a latency of two, not two and one.
  //
  // With BufferCount 2 and MaximumFrameLatency 1 the queue has nowhere to put
  // the next frame until the compositor has released the only spare buffer, so
  // a present blocks on the compositor and that block lands inside our own
  // fence wait -- where it is indistinguishable from GPU work and was
  // originally mistaken for it. One more buffer gives the queue somewhere to
  // go while the compositor is busy.
  scd.BufferCount = 3;
  scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;  // we cover the game completely
  scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

  ComPtr<IDXGISwapChain1> sc1;
  if (FAILED(factory->CreateSwapChainForComposition(bridge.Queue(), &scd, nullptr, &sc1))) {
    return nullptr;
  }
  if (FAILED(sc1.As(&o->swapChain_))) return nullptr;
  o->swapChain_->SetMaximumFrameLatency(2);
  o->frameLatencyWaitable_ = o->swapChain_->GetFrameLatencyWaitableObject();

  if (FAILED(DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&o->dcompDevice_)))) return nullptr;
  if (FAILED(o->dcompDevice_->CreateTargetForHwnd(o->hwnd_, TRUE, &o->dcompTarget_))) return nullptr;
  if (FAILED(o->dcompDevice_->CreateVisual(&o->dcompVisual_))) return nullptr;
  if (FAILED(o->dcompVisual_->SetContent(o->swapChain_.Get()))) return nullptr;
  if (FAILED(o->dcompTarget_->SetRoot(o->dcompVisual_.Get()))) return nullptr;
  if (FAILED(o->dcompDevice_->Commit())) return nullptr;

  auto* dev = bridge.D3d12();
  if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&o->alloc_)))) {
    return nullptr;
  }
  if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, o->alloc_.Get(),
                                    nullptr, IID_PPV_ARGS(&o->cmdList_)))) {
    return nullptr;
  }
  o->cmdList_->Close();
  if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&o->presentFence_)))) {
    return nullptr;
  }
  return o;
}

DCompOverlay::~DCompOverlay() {
  Hide();
  if (frameLatencyWaitable_) CloseHandle(frameLatencyWaitable_);
  if (hwnd_) DestroyWindow(hwnd_);
}

void DCompOverlay::SetBounds(const RECT& screenRect) {
  SetWindowPos(hwnd_, HWND_TOPMOST, screenRect.left, screenRect.top,
               screenRect.right - screenRect.left,
               screenRect.bottom - screenRect.top,
               SWP_NOACTIVATE | SWP_NOREDRAW);
}

void DCompOverlay::Show() {
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  visible_ = true;
}

void DCompOverlay::Hide() noexcept {
  // Called from the render thread on the failure path, while the window is
  // owned by the thread that created the overlay. ShowWindow would block here
  // until that thread pumped, holding the render thread hostage exactly when
  // the spec demands the overlay come down immediately. SWP_ASYNCWINDOWPOS
  // posts the request instead of waiting for it.
  if (hwnd_) {
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_ASYNCWINDOWPOS);
  }
  visible_ = false;
}

void DCompOverlay::Present(ID3D12Resource* frame, uint64_t waitFenceValue) {
  using Clock = std::chrono::steady_clock;
  const auto beginWait = Clock::now();
  if (frameLatencyWaitable_) WaitForSingleObjectEx(frameLatencyWaitable_, 1000, TRUE);
  const auto afterWait = Clock::now();

  // Wait for the capture-side copy to complete before reading the frame.
  bridge_->Queue()->Wait(bridge_->SharedFence(), waitFenceValue);

  const UINT backIndex = swapChain_->GetCurrentBackBufferIndex();
  ComPtr<ID3D12Resource> back;
  if (FAILED(swapChain_->GetBuffer(backIndex, IID_PPV_ARGS(&back)))) return;

  alloc_->Reset();
  cmdList_->Reset(alloc_.Get(), nullptr);

  D3D12_RESOURCE_BARRIER toCopy{};
  toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toCopy.Transition.pResource = back.Get();
  toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  cmdList_->ResourceBarrier(1, &toCopy);

  cmdList_->CopyResource(back.Get(), frame);

  D3D12_RESOURCE_BARRIER toPresent = toCopy;
  toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  cmdList_->ResourceBarrier(1, &toPresent);

  cmdList_->Close();
  ID3D12CommandList* lists[] = {cmdList_.Get()};
  bridge_->Queue()->ExecuteCommandLists(1, lists);

  swapChain_->Present(0, 0);   // pacing comes from the waitable object, not vsync
  const auto afterRecord = Clock::now();

  bridge_->Queue()->Signal(presentFence_.Get(), ++presentFenceValue_);
  if (presentFence_->GetCompletedValue() < presentFenceValue_) {
    HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    presentFence_->SetEventOnCompletion(presentFenceValue_, evt);
    WaitForSingleObject(evt, 1000);
    CloseHandle(evt);
  }
  const auto afterGpu = Clock::now();

  using Ms = std::chrono::duration<double, std::milli>;
  timing_.latencyWaitMs = Ms(afterWait - beginWait).count();
  timing_.recordMs = Ms(afterRecord - afterWait).count();
  timing_.gpuWaitMs = Ms(afterGpu - afterRecord).count();
}

}  // namespace sidecar
