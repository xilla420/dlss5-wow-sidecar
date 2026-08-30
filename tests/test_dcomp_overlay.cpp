#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <chrono>
#include <thread>

#include "core/GpuProfile.h"
#include "gpu/DeviceBridge.h"
#include "present/DCompOverlay.h"

using namespace sidecar;
using namespace std::chrono_literals;

TEST_CASE("overlay window carries exactly the required extended styles", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, 640, 480);
  REQUIRE(bridge != nullptr);

  auto overlay = DCompOverlay::Create(*bridge, 640, 480);
  REQUIRE(overlay != nullptr);

  const LONG ex = GetWindowLongW(overlay->Hwnd(), GWL_EXSTYLE);
  REQUIRE((ex & WS_EX_NOREDIRECTIONBITMAP) != 0);
  REQUIRE((ex & WS_EX_TRANSPARENT) != 0);
  REQUIRE((ex & WS_EX_NOACTIVATE) != 0);
  REQUIRE((ex & WS_EX_TOPMOST) != 0);
  REQUIRE((ex & WS_EX_TOOLWINDOW) != 0);

  // Spec C2: a layered window cannot host a flip swapchain.
  REQUIRE((ex & WS_EX_LAYERED) == 0);

  const LONG style = GetWindowLongW(overlay->Hwnd(), GWL_STYLE);
  REQUIRE((style & WS_POPUP) != 0);
}

TEST_CASE("clicks pass through the overlay to the window beneath", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, 400, 300);
  REQUIRE(bridge != nullptr);

  // A plain window standing in for the game.
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"SidecarClickTarget";
  RegisterClassExW(&wc);
  HWND beneath = CreateWindowExW(0, wc.lpszClassName, L"beneath",
                                 WS_POPUP | WS_VISIBLE, 200, 200, 400, 300,
                                 nullptr, nullptr, wc.hInstance, nullptr);
  REQUIRE(beneath != nullptr);

  auto overlay = DCompOverlay::Create(*bridge, 400, 300);
  REQUIRE(overlay != nullptr);
  RECT bounds{200, 200, 600, 500};
  overlay->SetBounds(bounds);
  overlay->Show();
  std::this_thread::sleep_for(150ms);

  // A point squarely inside both windows must resolve to the one beneath.
  const POINT p{400, 350};
  const HWND hit = WindowFromPoint(p);
  REQUIRE(hit != overlay->Hwnd());
  REQUIRE(hit == beneath);

  // WindowFromPoint only honours hit-testing within the calling thread; asked
  // from another process it reports the overlay regardless. What actually
  // routes a click past us is the overlay answering HTTRANSPARENT, so assert
  // on that directly -- it is the property that must never regress.
  REQUIRE(SendMessageW(overlay->Hwnd(), WM_NCHITTEST, 0,
                       MAKELPARAM(p.x, p.y)) == HTTRANSPARENT);

  DestroyWindow(beneath);
}

TEST_CASE("hide is idempotent and safe from any state", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, 320, 240);
  REQUIRE(bridge != nullptr);
  auto overlay = DCompOverlay::Create(*bridge, 320, 240);
  REQUIRE(overlay != nullptr);

  REQUIRE(overlay->IsVisible() == false);   // starts hidden
  overlay->Hide();                          // before ever showing
  REQUIRE(overlay->IsVisible() == false);
  overlay->Show();
  REQUIRE(overlay->IsVisible() == true);
  overlay->Hide();
  overlay->Hide();                          // twice
  REQUIRE(overlay->IsVisible() == false);
}
