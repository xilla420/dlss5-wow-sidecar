#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <chrono>
#include <future>
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

  // WS_EX_LAYERED is what makes the overlay click-through for the game's
  // process. It was once asserted absent here, on the belief that a layered
  // window cannot host a flip swapchain -- true of CreateSwapChainForHwnd, and
  // irrelevant to a composition swapchain bound to a DirectComposition visual.
  // Removing it makes the game unclickable.
  REQUIRE((ex & WS_EX_LAYERED) != 0);

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
  // Topmost, so a fullscreen topmost application that happens to be running --
  // the game itself, for instance -- cannot sit between the overlay and this
  // window and steal the hit test.
  HWND beneath = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"beneath",
                                 WS_POPUP | WS_VISIBLE, 200, 200, 400, 300,
                                 nullptr, nullptr, wc.hInstance, nullptr);
  REQUIRE(beneath != nullptr);
  SetWindowPos(beneath, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

  // Asked from a foreign thread, always.
  //
  // On the test's own thread HTTRANSPARENT grants a fall-through that no other
  // process ever gets, which is how this test passed for months while every
  // real click stopped dead at the overlay and the game could be neither
  // clicked nor typed into. A foreign thread removes that courtesy and leaves
  // only what the system honours across processes: WS_EX_LAYERED |
  // WS_EX_TRANSPARENT.
  const POINT p{400, 350};
  const auto hitTest = [p] {
    return std::async(std::launch::async, [p] { return WindowFromPoint(p); }).get();
  };

  std::this_thread::sleep_for(150ms);
  const HWND before = hitTest();

  auto overlay = DCompOverlay::Create(*bridge, 400, 300);
  REQUIRE(overlay != nullptr);
  RECT bounds{200, 200, 600, 500};
  overlay->SetBounds(bounds);
  overlay->Show();
  // The overlay must end up above the window beneath, or the test proves
  // nothing about hit-testing through it.
  SetWindowPos(overlay->Hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  std::this_thread::sleep_for(150ms);
  const HWND after = hitTest();

  // The assertion is relative, not absolute, and deliberately so.
  //
  // Requiring `after == beneath` says "this exact window got the click", which
  // is only true on a desktop with nothing else topmost over that point. Any
  // always-on-top application -- Task Manager, a chat overlay -- owns the point
  // instead and fails the test for a reason that has nothing to do with the
  // code. Asking instead that the overlay did not *change* who receives the
  // click tests the real property, and stays true whoever is underneath.
  REQUIRE(after != overlay->Hwnd());
  REQUIRE(after == before);

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
