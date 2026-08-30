#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW; WIN32_LEAN_AND_MEAN drops it
#include <cstdio>

#include "core/GpuProfile.h"
#include "neural/PassthroughPass.h"
#include "present/WindowTracker.h"
#include "runtime/Pipeline.h"

using namespace sidecar;

namespace {

struct Target {
  HWND hwnd = nullptr;
  const wchar_t* problem = nullptr;
};

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

  PipelineConfig cfg;
  cfg.target = target.hwnd;
  auto pipeline = Pipeline::Create(*gpu, cfg, PassthroughPass::Create());
  if (!pipeline) {
    MessageBoxW(nullptr, L"Failed to create the pipeline.", L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }
  pipeline->Start();

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  pipeline->Stop();
  return 0;
}
