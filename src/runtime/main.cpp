#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW; WIN32_LEAN_AND_MEAN drops it
#include <cstdio>

#include <filesystem>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/ControlChannel.h"
#include "core/GpuProfile.h"
#include "core/Log.h"
#include "neural/AddonSettings.h"
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

// Config problems are warnings by design: they must not interrupt startup with
// a dialog, but they do have to end up somewhere the operator can read.
void ReportWarnings(const std::vector<std::string>& warnings) {
  for (const auto& warning : warnings) {
    GlobalLog().Warn("config: " + warning);
  }
}

// The overlay is opaque and covers the game completely, so from the moment it
// comes up the player is looking at our window and clicking *through* it at
// whatever sits underneath. That has to be the game -- and by default it is
// not. The runtime is launched from the manager, so the foreground, and with it
// the keyboard, belongs to the manager's window, which the overlay has just
// made invisible. Clicks land on a window the player cannot see, and a
// WS_EX_NOACTIVATE overlay never hands focus back on its own.
//
// Setting another process's window foreground is permitted here because the
// process that started us owned the foreground when it did, which is what gives
// a freshly launched process foreground rights.
void GiveTargetTheForeground(HWND target) {
  if (SetForegroundWindow(target)) {
    GlobalLog().Info("foreground handed to the capture target");
    return;
  }
  // Not fatal, and not silent: without it the player is typing into something
  // they cannot see, and "alt-tab to the game once" is the whole workaround.
  GlobalLog().Warn(
      "could not give the game the foreground; alt-tab to World of Warcraft "
      "once if the mouse and keyboard do not reach it");
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

  // Truncated each run: the interesting log is always the most recent session,
  // and an ever-growing file next to the executable would be rude.
  GlobalLog().OpenFile(ExecutableDirectory() / "sidecar.log");
  GlobalLog().Info("sidecar starting");

  auto gpu = DetectPrimaryGpu();
  if (!gpu) {
    GlobalLog().Error("no NVIDIA adapter found");
    MessageBoxW(nullptr, L"No NVIDIA adapter found.", L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }
  GlobalLog().Info(std::string("adapter: ") + ToString(gpu->arch));

  if (gpu->arch != GpuArch::Ada && gpu->arch != GpuArch::Blackwell) {
    GlobalLog().Error(std::string(ToString(gpu->arch)) +
                      " is not supported; RTX 40 or RTX 50 required");
    wchar_t msg[256];
    swprintf_s(msg, L"%hs is not supported. RTX 40 or RTX 50 required.",
               ToString(gpu->arch));
    MessageBoxW(nullptr, msg, L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }

  const Target target = ResolveTarget(argc, argv);
  if (!target.hwnd) {
    GlobalLog().Error("no capture target: see the dialog for what to do");
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

  // The add-on has no API: it reads [RenoDX.DLSS5] out of ReShade.ini once, when
  // ReShade loads it, which happens at our first device creation inside
  // Pipeline::Create below. So this is the last moment at which a slider the
  // operator moved can still reach it, and it has to happen every launch --
  // sidecar.toml is the source of truth, and ReShade.ini is a projection of it.
  if (config.neuralPass != "passthrough") {
    if (!WriteNeuralSettings(ExecutableDirectory() / "ReShade.ini", config.neural)) {
      GlobalLog().Warn("could not write ReShade.ini; the neural settings in "
                       "sidecar.toml will not reach the add-on this run");
    }
  }

  PipelineConfig cfg;
  cfg.target = target.hwnd;
  cfg.showOverlay = config.showOverlay;
  cfg.showHud = config.showHud;
  cfg.flowGridSize = config.flowGridSize;
  // Calibrated at the desktop resolution the operator was looking at, which is
  // what UiMask scales from. Leaving the source size zero means "same as the
  // capture", which is right until the calibration UI records it explicitly.
  cfg.uiMaskRects = config.uiMaskRects;

  // The pass is named here and built by the pipeline, because a device-backed
  // pass cannot exist before the device does -- and has to be rebuilt with it
  // after device loss.
  cfg.neuralPass = config.neuralPass;
  cfg.dlssPreset = config.dlssPreset;
  cfg.syntheticDepth = config.syntheticDepth;
  cfg.uiMaskFeather = static_cast<int32_t>(config.uiMaskFeather);
  cfg.runtimeDir = ExecutableDirectory();
  ReportWarnings(warnings);

  auto pipeline = Pipeline::Create(*gpu, cfg, nullptr);
  if (!pipeline) {
    GlobalLog().Error("could not create the pipeline");
    MessageBoxW(nullptr, L"Failed to create the pipeline.", L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }

  // The manager's end of the channel. Created before Start() so the first status
  // the render loop publishes has somewhere to land, and refused when another
  // overlay already owns it -- two of these would each cover the screen with a
  // stale copy of the other's output.
  Pipeline* raw = pipeline.get();
  auto control = ControlServer::Create([raw](SidecarCommand command) {
    // Runs on this thread, from the message loop below, which is the thread that
    // owns the windows. That is the whole reason commands travel as messages.
    switch (command) {
      case SidecarCommand::Stop:        PostQuitMessage(0); break;
      case SidecarCommand::ShowOverlay: raw->SetOverlayVisible(true); break;
      case SidecarCommand::HideOverlay: raw->SetOverlayVisible(false); break;
      case SidecarCommand::ShowHud:     raw->SetHudVisible(true); break;
      case SidecarCommand::HideHud:     raw->SetHudVisible(false); break;
      default: break;
    }
  });
  if (!control) {
    GlobalLog().Error("another overlay is already running");
    MessageBoxW(nullptr,
                L"An overlay is already running.\n"
                L"Stop it from the manager before starting another.",
                L"DLSS 5 Sidecar", MB_ICONERROR);
    return 1;
  }
  ControlServer* channel = control.get();
  pipeline->SetStatusSink([channel](const SidecarStatus& status) {
    channel->Publish(status);
  });

  pipeline->Start();
  GlobalLog().Info("overlay running");
  GiveTargetTheForeground(target.hwnd);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);

    // The game closed, or was restarted. Nothing to recover -- a new launch is
    // a new window and a new capture item -- so exit rather than linger, which
    // would leave the manager reporting a running overlay over a dead render
    // loop. Quitting puts the manager back to "stopped", where Start works.
    if (pipeline->TargetLost()) {
      GlobalLog().Info("the game closed; shutting down");
      break;
    }

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
