#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW; WIN32_LEAN_AND_MEAN drops it
#include <cstdio>

#include "core/GpuProfile.h"
#include "neural/PassthroughPass.h"
#include "runtime/Pipeline.h"

using namespace sidecar;

namespace {

// M0 accepts a window class name so the runtime can be driven against
// testpattern.exe before WoW discovery lands in Task 10.
HWND FindTargetWindow(int argc, wchar_t** argv) {
  const wchar_t* className = (argc > 1) ? argv[1] : L"SidecarTestPattern";
  return FindWindowW(className, nullptr);
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

  HWND target = FindTargetWindow(argc, argv);
  if (!target) {
    MessageBoxW(nullptr, L"Target window not found.", L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }

  PipelineConfig cfg;
  cfg.target = target;
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
