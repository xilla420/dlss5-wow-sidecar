#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <winrt/base.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "capture/WgcSource.h"
#include "core/GpuProfile.h"
#include "gpu/DeviceBridge.h"

using namespace sidecar;
using namespace std::chrono_literals;

namespace {

// WGC is a WinRT API, so the calling thread needs an apartment. The runtime
// does this in wWinMain; the test harness has to do it itself. Repeat calls
// with the same apartment type are harmless.
void EnsureWinRtApartment() {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
}

// Launches testpattern.exe next to the test binary and returns its window.
struct PatternApp {
  PROCESS_INFORMATION pi{};
  HWND hwnd = nullptr;

  bool Launch() {
    STARTUPINFOW si{sizeof(si)};
    wchar_t cmd[] = L"testpattern.exe";
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi)) {
      return false;
    }
    // Poll for the window rather than sleeping a fixed amount.
    for (int i = 0; i < 200 && !hwnd; ++i) {
      hwnd = FindWindowW(L"SidecarTestPattern", nullptr);
      if (!hwnd) std::this_thread::sleep_for(25ms);
    }
    return hwnd != nullptr;
  }

  ~PatternApp() {
    if (pi.hProcess) {
      TerminateProcess(pi.hProcess, 0);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }
  }
};

}  // namespace

TEST_CASE("capture delivers frames from a live window", "[device]") {
  EnsureWinRtApartment();
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());

  PatternApp app;
  REQUIRE(app.Launch());

  RECT client{};
  REQUIRE(GetClientRect(app.hwnd, &client));
  const uint32_t w = static_cast<uint32_t>(client.right - client.left);
  const uint32_t h = static_cast<uint32_t>(client.bottom - client.top);

  auto bridge = DeviceBridge::Create(gpu->luid, w, h);
  REQUIRE(bridge != nullptr);

  std::atomic<uint64_t> drops{0};
  auto source = WgcSource::CreateForWindow(app.hwnd, *bridge, [&] { ++drops; });
  REQUIRE(source != nullptr);
  source->Start();

  // Drain as a render thread would, so drops stay near zero.
  uint64_t consumed = 0;
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < deadline && consumed < 30) {
    if (bridge->AcquireLatest().has_value()) ++consumed;
    std::this_thread::sleep_for(5ms);
  }
  source->Stop();

  REQUIRE(consumed >= 10);
  REQUIRE(source->FramesDelivered() >= 10);
  REQUIRE(source->IsClosed() == false);
}

TEST_CASE("capture reports closed when the target window disappears", "[device]") {
  EnsureWinRtApartment();
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());

  auto app = std::make_unique<PatternApp>();
  REQUIRE(app->Launch());

  auto bridge = DeviceBridge::Create(gpu->luid, 1280, 720);
  REQUIRE(bridge != nullptr);
  auto source = WgcSource::CreateForWindow(app->hwnd, *bridge, [] {});
  REQUIRE(source != nullptr);
  source->Start();
  std::this_thread::sleep_for(300ms);

  app.reset();  // terminates the process, closing the capture item

  bool closed = false;
  for (int i = 0; i < 100 && !closed; ++i) {
    closed = source->IsClosed();
    if (!closed) std::this_thread::sleep_for(25ms);
  }
  source->Stop();
  REQUIRE(closed);
}
