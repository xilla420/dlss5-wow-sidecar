#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <chrono>
#include <memory>
#include <thread>

#include "core/GpuProfile.h"
#include "gpu/DeviceBridge.h"
#include "neural/PassthroughPass.h"
#include "runtime/Pipeline.h"

using Microsoft::WRL::ComPtr;
using namespace sidecar;
using namespace std::chrono_literals;

namespace {

struct PatternApp {
  PROCESS_INFORMATION pi{};
  HWND hwnd = nullptr;
  bool Launch() {
    STARTUPINFOW si{sizeof(si)};
    wchar_t cmd[] = L"testpattern.exe";
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi)) return false;
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

// The overlay window belongs to this thread, so its state only advances while
// this thread pumps -- exactly as wWinMain does.
void PumpFor(std::chrono::milliseconds duration) {
  const auto until = std::chrono::steady_clock::now() + duration;
  MSG msg{};
  while (std::chrono::steady_clock::now() < until) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    std::this_thread::sleep_for(2ms);
  }
}

}  // namespace

// The bug this pins down: Rebuild() used to transfer device-derived members by
// hand and silently missed the ones added later -- the luminance pass, both
// luma targets and the second command list -- leaving the restarted pipeline
// holding objects from a dead device.
TEST_CASE("rebuilding produces a pipeline that presents again", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());

  PatternApp app;
  REQUIRE(app.Launch());

  PipelineConfig cfg;
  cfg.target = app.hwnd;
  auto pipeline = Pipeline::Create(*gpu, cfg, PassthroughPass::Create());
  REQUIRE(pipeline != nullptr);
  pipeline->Start();
  PumpFor(600ms);
  REQUIRE(pipeline->Running());
  REQUIRE(pipeline->Stats().Count() > 0);

  RECT boundsBefore{};
  REQUIRE(GetWindowRect(pipeline->OverlayHwnd(), &boundsBefore));

  REQUIRE(pipeline->RebuildAndRestart());
  PumpFor(1200ms);

  const HWND overlayAfter = pipeline->OverlayHwnd();
  REQUIRE(overlayAfter != nullptr);
  REQUIRE(IsWindowVisible(overlayAfter));

  RECT boundsAfter{};
  REQUIRE(GetWindowRect(overlayAfter, &boundsAfter));
  REQUIRE(boundsAfter.left == boundsBefore.left);
  REQUIRE(boundsAfter.top == boundsBefore.top);
  REQUIRE(boundsAfter.right == boundsBefore.right);
  REQUIRE(boundsAfter.bottom == boundsBefore.bottom);

  // Presenting again, not merely visible. This is what failed when the
  // rebuilt pipeline was left holding a dead device's objects.
  REQUIRE(pipeline->Running());
  const uint64_t before = pipeline->Stats().Count();
  PumpFor(600ms);
  REQUIRE(pipeline->Stats().Count() > before);

  // And a second rebuild works, so nothing is left in a one-shot state.
  REQUIRE(pipeline->RebuildAndRestart());
  PumpFor(1200ms);
  REQUIRE(pipeline->Running());
  REQUIRE(IsWindowVisible(pipeline->OverlayHwnd()));

  pipeline->Stop();
}

// Device loss must be reported to the owner thread, never acted on by the
// render thread: rebuilding creates the overlay window, and a window belongs
// to its creating thread, which for the render thread never pumps.
//
// Deliberately NOT tagged [device], so it never shares a process with the rest
// of the suite. ID3D12Device5::RemoveDevice poisons the adapter for the whole
// process -- D3D12CreateDevice keeps returning DXGI_ERROR_DEVICE_REMOVED
// afterwards, verified over ten retries across 2.5 s -- so every later test
// that builds a DeviceBridge would fail through no fault of its own.
//
// Run it on its own:  sidecar_tests.exe "[deviceloss]"
//
// It therefore covers detection and hand-off, which is the part that was
// wrong. Recovery is covered by the rebuild test above, which does not poison
// anything.
TEST_CASE("device loss is handed to the owner thread and hides the overlay", "[deviceloss]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());

  PatternApp app;
  REQUIRE(app.Launch());

  PipelineConfig cfg;
  cfg.target = app.hwnd;
  auto pipeline = Pipeline::Create(*gpu, cfg, PassthroughPass::Create());
  REQUIRE(pipeline != nullptr);
  pipeline->Start();
  PumpFor(600ms);
  REQUIRE(pipeline->Running());
  REQUIRE(pipeline->NeedsRebuild() == false);

  {
    ComPtr<ID3D12Device5> device5;
    REQUIRE(SUCCEEDED(pipeline->DeviceForTest()->QueryInterface(IID_PPV_ARGS(&device5))));
    device5->RemoveDevice();
  }

  bool requested = false;
  for (int i = 0; i < 200 && !requested; ++i) {
    requested = pipeline->NeedsRebuild();
    if (!requested) PumpFor(25ms);
  }
  REQUIRE(requested);
  REQUIRE(pipeline->LastError().empty() == false);

  // Failure rule: the overlay comes down before anything slower happens, so
  // the player is never left looking at a frozen frame over a live game.
  bool hidden = false;
  for (int i = 0; i < 80 && !hidden; ++i) {
    hidden = !IsWindowVisible(pipeline->OverlayHwnd());
    if (!hidden) PumpFor(25ms);
  }
  REQUIRE(hidden);

  // The render thread stopped rather than rebuilding on its own.
  REQUIRE(pipeline->Running() == false);

  // Teardown must not crash. It used to: NvofaFlow unregistered its resources
  // against the removed device, which corrupted state that blew up when the
  // registered luminance textures were released moments later.
  pipeline->Stop();
}
