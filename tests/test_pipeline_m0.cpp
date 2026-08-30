#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <chrono>
#include <memory>
#include <thread>

#include "core/GpuProfile.h"
#include "neural/PassthroughPass.h"
#include "runtime/Pipeline.h"

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

// The overlay window is owned by this thread, so window state the render
// thread requests is only applied while this thread pumps. wWinMain runs a
// GetMessage loop for exactly this reason; the test has to do the same.
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

TEST_CASE("pipeline runs end to end over the test pattern", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());

  PatternApp app;
  REQUIRE(app.Launch());

  PipelineConfig cfg;
  cfg.target = app.hwnd;
  cfg.showOverlay = true;

  auto pipeline = Pipeline::Create(*gpu, cfg, PassthroughPass::Create());
  REQUIRE(pipeline != nullptr);
  pipeline->Start();

  // Let it settle, then require real throughput.
  std::this_thread::sleep_for(3s);
  REQUIRE(pipeline->Running());

  const auto& stats = pipeline->Stats();
  INFO("p50=" << stats.P50() << "ms p99=" << stats.P99()
              << "ms drops=" << stats.Dropped());
  REQUIRE(stats.Count() >= 30);
  REQUIRE(stats.P50() > 0.0);

  // Sanity bound: anything above this means the seam is fundamentally broken,
  // not merely slow. The real M1 judgement is made against live WoW by a human
  // reading the HUD, not by this assertion.
  REQUIRE(stats.P99() < 250.0);

  pipeline->Stop();
  REQUIRE(pipeline->Running() == false);
}

TEST_CASE("pipeline hides the overlay when the target window closes", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());

  auto app = std::make_unique<PatternApp>();
  REQUIRE(app->Launch());

  PipelineConfig cfg;
  cfg.target = app->hwnd;
  auto pipeline = Pipeline::Create(*gpu, cfg, PassthroughPass::Create());
  REQUIRE(pipeline != nullptr);
  pipeline->Start();
  PumpFor(500ms);

  const HWND overlay = pipeline->OverlayHwnd();
  REQUIRE(IsWindowVisible(overlay));

  app.reset();  // target disappears

  bool hidden = false;
  for (int i = 0; i < 120 && !hidden; ++i) {
    hidden = !IsWindowVisible(overlay);
    if (!hidden) PumpFor(25ms);
  }
  REQUIRE(hidden);   // spec failure rule: fail to a visible game, never to black

  pipeline->Stop();
}
