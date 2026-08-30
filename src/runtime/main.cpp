#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW; WIN32_LEAN_AND_MEAN drops it
#include <cstdio>

#include <filesystem>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/GpuProfile.h"
#include "neural/NeuralPassFactory.h"
#include "present/WindowTracker.h"
#include "runtime/Pipeline.h"

namespace fs = std::filesystem;
using namespace sidecar;

namespace {

struct Target {
  HWND hwnd = nullptr;
  const wchar_t* problem = nullptr;
};

fs::path ExecutableDirectory() {
  wchar_t buffer[MAX_PATH]{};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return fs::path(buffer).parent_path();
}

// Config problems are warnings by design, so they must not interrupt startup
// with a dialog. There is no logging module yet, so they go to the debugger.
void ReportWarnings(const std::vector<std::string>& warnings) {
  for (const auto& warning : warnings) {
    const std::string line = "[sidecar] config: " + warning + "\n";
    OutputDebugStringA(line.c_str());
  }
}

Target ResolveTarget(int argc, wchar_t** argv) {
  // An explicit class name still works, so the runtime can be driven against
  // testpattern.exe.
  if (argc > 1) {
    HWND hwnd = FindWindowW(argv[1], nullptr);
    return hwnd ? Target{hwnd, nullptr} : Target{nullptr, L"Window class not found."};
  }
  auto wow = FindWowWindow();
  if (!wow) return {nullptr, L"World of Warcraft is not running."};
  if (!wow->borderless) {
    return {nullptr,
            L"World of Warcraft must run in borderless windowed mode.\n"
            L"Exclusive fullscreen has no compositor surface to capture."};
  }
  return {wow->hwnd, nullptr};
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);

  auto gpu = DetectPrimaryGpu();
  if (!gpu) {
    MessageBoxW(nullptr, L"No NVIDIA adapter found.", L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }
  if (gpu->arch != GpuArch::Ada && gpu->arch != GpuArch::Blackwell) {
    wchar_t msg[256];
    swprintf_s(msg, L"%hs is not supported. RTX 40 or RTX 50 required.",
               ToString(gpu->arch));
    MessageBoxW(nullptr, msg, L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }

  const Target target = ResolveTarget(argc, argv);
  if (!target.hwnd) {
    MessageBoxW(nullptr, target.problem, L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }

  // An absent config file is normal: every value has a working default, and a
  // malformed one warns rather than stopping the overlay (Task 15's contract).
  std::vector<std::string> warnings;
  Config config;
  if (auto loaded = LoadConfig(ExecutableDirectory() / "sidecar.toml", warnings)) {
    config = *loaded;
  }

  PipelineConfig cfg;
  cfg.target = target.hwnd;
  cfg.showOverlay = config.showOverlay;
  cfg.showHud = config.showHud;
  cfg.flowGridSize = config.flowGridSize;

  auto pass = MakeNeuralPass(config.neuralPass, warnings);
  ReportWarnings(warnings);

  auto pipeline = Pipeline::Create(*gpu, cfg, std::move(pass));
  if (!pipeline) {
    MessageBoxW(nullptr, L"Failed to create the pipeline.", L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }
  pipeline->Start();

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);

    // The render thread cannot rebuild after device loss: it would create the
    // overlay window on a thread that never pumps. It posts a WM_NULL to wake
    // this loop instead, and the rebuild happens here, where the windows live.
    if (pipeline->NeedsRebuild() && !pipeline->RebuildAndRestart()) {
      MessageBoxW(nullptr,
                  L"The graphics device was reset and could not be rebuilt.",
                  L"DLSS 5 Sidecar", MB_ICONERROR);
      break;
    }
  }
  pipeline->Stop();
  return 0;
}
