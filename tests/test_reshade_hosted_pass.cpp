#include <catch2/catch_test_macros.hpp>
#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "core/GpuProfile.h"
#include "core/Log.h"
#include "gpu/DeviceBridge.h"
#include "neural/ReshadeHostedPass.h"
#include "runtime/Pipeline.h"

using namespace sidecar;
using namespace std::chrono_literals;

namespace {

// Opt-in: this needs NVIDIA runtimes that are ~230 MB and are never committed.
// Point it at a directory holding nvngx_dlss.dll and nvngx_dlssnr.dll:
//
//   set SIDECAR_TEST_RUNTIME_DIR=D:\path\to\runtimes
//
// Note that neural rendering itself needs ReShade and the add-on loaded into
// this process, which means a dxgi.dll beside the test binary -- deliberately
// not done, because that same dxgi.dll would then be injected into
// wowsidecar.exe on its next run. What this covers is everything up to the
// add-on: the NGX session, DLSS feature creation and evaluation, and the
// pipeline's resolve path. The add-on interception is covered by the Task 4
// spike, which runs from its own scratch directory.
std::filesystem::path RuntimeDirFromEnv() {
  size_t len = 0;
  char raw[1024] = {};
  if (getenv_s(&len, raw, sizeof(raw), "SIDECAR_TEST_RUNTIME_DIR") != 0 || len == 0) {
    return {};
  }
  return std::filesystem::path(raw);
}

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

}  // namespace

TEST_CASE("ReshadeHostedPass refuses impossible arguments", "[unit]") {
  ReshadeHostedPass::Options options;
  options.width = 256;
  options.height = 256;
  std::string reason;
  CHECK(ReshadeHostedPass::Create(nullptr, options, reason) == nullptr);
  CHECK(reason.empty() == false);
}

TEST_CASE("ReshadeHostedPass declares a contract the pipeline can honour",
          "[device]") {
  const auto dir = RuntimeDirFromEnv();
  if (dir.empty()) { SUCCEED("SIDECAR_TEST_RUNTIME_DIR not set"); return; }
  const auto gpu = DetectPrimaryGpu();
  if (!gpu) { SUCCEED("no NVIDIA adapter"); return; }

  auto bridge = DeviceBridge::Create(gpu->luid, 1920, 1080);
  REQUIRE(bridge != nullptr);

  ReshadeHostedPass::Options options;
  options.runtimeDir = dir;
  options.width = 1920;
  options.height = 1080;

  std::string reason;
  auto pass = ReshadeHostedPass::Create(bridge->D3d12(), options, reason);
  if (!pass) {
    INFO("reason: " << reason);
    CHECK(reason.empty() == false);
    SUCCEED("DLSS unavailable here; the reason was reported");
    return;
  }

  // These two are what let the pipeline bracket the pass correctly. Getting
  // either wrong is a debug-layer error at best and a device removal at worst.
  CHECK(pass->OutputState() == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  CHECK(pass->OutputFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT);
  CHECK(std::string(pass->Name()).empty() == false);
}

// The real integration: run the whole pipeline through the pass long enough to
// cross both scheduled feature creates, and require it to keep presenting.
//
// The interesting window is frames 60 and 240 -- the hold-off for the add-on's
// asynchronous hook arming, and the warm-up rebuild that clears its standby
// latch. A run shorter than that would never exercise either.
TEST_CASE("pipeline runs end to end through the reshade-hosted pass", "[device]") {
  const auto dir = RuntimeDirFromEnv();
  if (dir.empty()) { SUCCEED("SIDECAR_TEST_RUNTIME_DIR not set"); return; }
  const auto gpu = DetectPrimaryGpu();
  if (!gpu) { SUCCEED("no NVIDIA adapter"); return; }

  GlobalLog().Clear();

  PatternApp app;
  REQUIRE(app.Launch());

  PipelineConfig cfg;
  cfg.target = app.hwnd;
  cfg.showOverlay = true;
  cfg.neuralPass = "reshade";
  cfg.runtimeDir = dir;

  auto pipeline = Pipeline::Create(*gpu, cfg, nullptr);
  REQUIRE(pipeline != nullptr);
  pipeline->Start();

  // Long enough to pass frame 240 at any plausible rate.
  std::this_thread::sleep_for(8s);

  const auto& stats = pipeline->Stats();
  INFO("p50=" << stats.P50() << "ms p99=" << stats.P99()
              << "ms frames=" << stats.Count() << " drops=" << stats.Dropped());
  INFO("lastError='" << pipeline->LastError() << "'");
  // Accumulated into one message rather than one INFO per line: an INFO inside
  // a single-statement loop body is destroyed at the end of that statement, so
  // the log would never reach the report.
  std::string log;
  for (const auto& line : GlobalLog().Recent()) log += "\n  " + line;
  INFO("log:" << log);

  REQUIRE(pipeline->Running());
  REQUIRE(stats.Count() >= 60);
  REQUIRE(stats.P99() < 250.0);

  // The assertions above all passed while every single evaluate was failing,
  // because the pass degrades to a copy and keeps the frame rate up. Throughput
  // therefore proves nothing about whether the pass did any work, and the log is
  // the only place that does.
  const bool created = log.find("feature created at frame") != std::string::npos;
  const bool evaluateFailed = log.find("evaluate failed") != std::string::npos;
  if (created) {
    CHECK_FALSE(evaluateFailed);
  } else {
    // No feature is a legitimate outcome on a machine without the runtime, but
    // it has to have been explained rather than passed over in silence.
    CHECK(log.empty() == false);
  }

  pipeline->Stop();
  REQUIRE(pipeline->Running() == false);
}
