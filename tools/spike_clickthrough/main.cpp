// Spike: which window styles actually make a DirectComposition overlay
// click-through for *another thread's* hit test -- while still being visible?
//
// The shipping overlay answers HTTRANSPARENT to WM_NCHITTEST and assumes that
// is enough. It is not. HTTRANSPARENT fall-through is documented to continue
// "to underlying windows in the same thread" -- so a same-thread test passes
// while the game, in another process entirely, has its clicks stop dead at the
// overlay. The cross-process mechanism is WS_EX_LAYERED | WS_EX_TRANSPARENT,
// which the shipping overlay deliberately omits on the belief that a layered
// window cannot host a flip swapchain.
//
// That belief is worth checking rather than inheriting: this swapchain is
// created with CreateSwapChainForComposition and bound to a DirectComposition
// visual, not to the HWND. The window is only the composition target.
//
// Both halves are measured, because either one alone is a trap. A window that
// is click-through but invisible is not an overlay, and a window that is
// visible but opaque to clicks is the bug being fixed. So each variant is
// filled with a known colour and then read back off the screen.
#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <thread>

using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

namespace {

struct Variant {
  const char* name;
  DWORD exStyle;
  bool setLayeredAttributes;
};

const Variant kVariants[] = {
    {"current: NOREDIRECTION | TRANSPARENT",
     WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
         WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
     false},
    {"NOREDIRECTION | TRANSPARENT | LAYERED",
     WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
         WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
     true},
    {"TRANSPARENT | LAYERED (no NOREDIRECTION)",
     WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
         WS_EX_LAYERED,
     true},
};

// Distinctive enough that nothing on a desktop is plausibly this colour.
constexpr COLORREF kFill = RGB(255, 0, 255);

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCHITTEST) return HTTRANSPARENT;
  return DefWindowProcW(hwnd, msg, wp, lp);
}

const char* YesNo(bool value) { return value ? "yes" : "NO "; }

void PumpFor(std::chrono::milliseconds duration) {
  const auto until = std::chrono::steady_clock::now() + duration;
  while (std::chrono::steady_clock::now() < until) {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    std::this_thread::sleep_for(5ms);
  }
}

}  // namespace

int main() {
  WNDCLASSEXW overlayClass{sizeof(overlayClass)};
  overlayClass.lpfnWndProc = OverlayProc;
  overlayClass.hInstance = GetModuleHandleW(nullptr);
  overlayClass.lpszClassName = L"SpikeOverlay";
  RegisterClassExW(&overlayClass);

  WNDCLASSEXW targetClass{sizeof(targetClass)};
  targetClass.lpfnWndProc = DefWindowProcW;
  targetClass.hInstance = GetModuleHandleW(nullptr);
  targetClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  targetClass.lpszClassName = L"SpikeTarget";
  RegisterClassExW(&targetClass);

  // Stands in for the game: topmost, so nothing else can slide between them.
  HWND beneath = CreateWindowExW(WS_EX_TOPMOST, targetClass.lpszClassName, L"beneath",
                                 WS_POPUP | WS_VISIBLE, 200, 200, 400, 300, nullptr,
                                 nullptr, targetClass.hInstance, nullptr);
  if (!beneath) {
    std::printf("could not create the target window\n");
    return 1;
  }

  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &device, nullptr, &context))) {
    std::printf("no D3D11 device\n");
    return 1;
  }

  std::printf("%-42s %-7s %-9s %-9s %s\n", "variant", "dcomp", "visible", "through",
              "notes");
  std::printf("%s\n", std::string(96, '-').c_str());

  for (const auto& variant : kVariants) {
    HWND overlay = CreateWindowExW(variant.exStyle, overlayClass.lpszClassName, L"",
                                   WS_POPUP, 200, 200, 400, 300, nullptr, nullptr,
                                   overlayClass.hInstance, nullptr);
    if (!overlay) {
      std::printf("%-42s window creation failed\n", variant.name);
      continue;
    }
    if (variant.setLayeredAttributes) {
      // Fully opaque. With DirectComposition doing the compositing this should
      // change no pixels; the flag is here for the hit test, not the picture --
      // and that is exactly the assumption being measured below.
      SetLayeredWindowAttributes(overlay, 0, 255, LWA_ALPHA);
    }

    ComPtr<IDXGIFactory2> factory;
    CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = 400;
    scd.Height = 300;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain;
    const bool swapOk = SUCCEEDED(
        factory->CreateSwapChainForComposition(device.Get(), &scd, nullptr, &swapChain));

    ComPtr<IDCompositionDevice> dcomp;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    bool dcompOk = false;
    if (SUCCEEDED(DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&dcomp)))) {
      dcompOk = SUCCEEDED(dcomp->CreateTargetForHwnd(overlay, TRUE, &target)) &&
                SUCCEEDED(dcomp->CreateVisual(&visual)) && swapOk &&
                SUCCEEDED(visual->SetContent(swapChain.Get())) &&
                SUCCEEDED(target->SetRoot(visual.Get())) && SUCCEEDED(dcomp->Commit());
    }

    ShowWindow(overlay, SW_SHOWNOACTIVATE);
    SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // Paint it a colour nothing else on the desktop will be, and present a few
    // times so the compositor has certainly caught up.
    if (swapOk) {
      ComPtr<ID3D11Texture2D> back;
      ComPtr<ID3D11RenderTargetView> rtv;
      if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&back))) &&
          SUCCEEDED(device->CreateRenderTargetView(back.Get(), nullptr, &rtv))) {
        const float magenta[4] = {1.0f, 0.0f, 1.0f, 1.0f};
        for (int i = 0; i < 3; ++i) {
          context->ClearRenderTargetView(rtv.Get(), magenta);
          context->Flush();
          swapChain->Present(0, 0);
        }
      }
    }
    PumpFor(400ms);

    // Is it actually on screen? Read the composed desktop, not our own surface.
    const POINT probe{400, 350};
    HDC screen = GetDC(nullptr);
    const COLORREF pixel = GetPixel(screen, probe.x, probe.y);
    ReleaseDC(nullptr, screen);
    const bool visible = pixel == kFill;

    // The whole point. WindowFromPoint on a *foreign* thread gets no
    // HTTRANSPARENT fall-through -- it only skips windows the system itself
    // excludes from hit-testing, which is what WS_EX_LAYERED | WS_EX_TRANSPARENT
    // buys. This is the closest thing to asking as the game would.
    auto asked = std::async(std::launch::async, [probe] { return WindowFromPoint(probe); });
    const HWND hit = asked.get();
    const bool passesThrough = hit != overlay;

    std::printf("%-42s %-7s %-9s %-9s %s\n", variant.name, YesNo(dcompOk), YesNo(visible),
                YesNo(passesThrough),
                hit == beneath ? "click reached the window beneath"
                : passesThrough ? "click went somewhere else"
                                : "click stopped at the overlay");

    DestroyWindow(overlay);
    PumpFor(150ms);
  }

  DestroyWindow(beneath);
  return 0;
}
