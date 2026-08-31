#include "manager/Probes.h"

#include "core/Sha256.h"
#include "neural/RuntimeManifest.h"

#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <system_error>

#include "core/GpuProfile.h"
#include "present/WindowTracker.h"

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace sidecar {
namespace {

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// ReShade and similar loaders masquerade as system DLLs the game already
// imports, so detection is by filename. I8 reads filenames only and never
// opens any of these files.
constexpr const char* kInjectorLoaders[] = {
    "dxgi.dll", "d3d12.dll", "d3d11.dll", "dinput8.dll",
    "winmm.dll", "version.dll", "opengl32.dll",
};
constexpr const char* kInjectorConfigs[] = {"reshade.ini", "reshade.log"};

constexpr const char* kWowSignatures[] = {
    "wow.exe", "_retail_", "_classic_", "_classic_era_", "world of warcraft",
};

// True when child sits inside parent, compared case-insensitively on the
// lexically normalised generic form so separators and case cannot fool it.
bool IsInside(const fs::path& child, const fs::path& parent) {
  if (parent.empty()) return false;
  std::string p = ToLower(parent.lexically_normal().generic_string());
  std::string c = ToLower(child.lexically_normal().generic_string());
  if (!p.empty() && p.back() == '/') p.pop_back();
  if (c.size() < p.size()) return false;
  if (c.compare(0, p.size(), p) != 0) return false;
  return c.size() == p.size() || c[p.size()] == '/';
}

}  // namespace

bool PathLooksLikeWowInstall(const fs::path& path) {
  const std::string lowered = ToLower(path.generic_string());
  for (const char* signature : kWowSignatures) {
    if (lowered.find(signature) != std::string::npos) return true;
  }
  return false;
}

std::vector<std::string> FindInjectorLoaders(const std::vector<std::string>& filenames) {
  std::vector<std::string> found;
  for (const auto& name : filenames) {
    const std::string lowered = ToLower(name);
    for (const char* loader : kInjectorLoaders) {
      if (lowered == loader) { found.push_back(name); break; }
    }
    for (const char* config : kInjectorConfigs) {
      if (lowered == config) { found.push_back(name); break; }
    }
  }
  return found;
}

ProbeResult ProbeGpu() {
  ProbeResult r;
  r.title = "Graphics adapter";

  const auto gpu = DetectPrimaryGpu();
  if (!gpu) {
    r.state = ProbeState::Fail;
    r.detail = "No NVIDIA adapter found.";
    r.remedy = "This sidecar needs an NVIDIA RTX 40 or RTX 50 card.";
    return r;
  }

  // Adapter descriptions are ASCII in practice; narrow explicitly so the
  // conversion is deliberate rather than a warning.
  for (const wchar_t c : gpu->name) r.detail.push_back(c < 128 ? static_cast<char>(c) : '?');
  r.detail += " - ";
  r.detail += ToString(gpu->arch);

  if (gpu->arch == GpuArch::Ada || gpu->arch == GpuArch::Blackwell) {
    r.state = ProbeState::Ok;
    return r;
  }
  r.state = ProbeState::Fail;
  r.remedy = "RTX 40 (Ada) or RTX 50 (Blackwell) is required. Older cards are "
             "refused rather than run badly.";
  return r;
}

ProbeResult ProbeDriver() {
  ProbeResult r;
  r.title = "Display driver";

  const auto gpu = DetectPrimaryGpu();
  if (!gpu) {
    r.state = ProbeState::Fail;
    r.detail = "No NVIDIA adapter to query.";
    r.remedy = "Install an NVIDIA RTX 40 or RTX 50 card.";
    return r;
  }

  ComPtr<IDXGIFactory4> factory;
  ComPtr<IDXGIAdapter1> adapter;
  LARGE_INTEGER umd{};
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->EnumAdapterByLuid(gpu->luid, IID_PPV_ARGS(&adapter))) &&
      SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
    // The NVIDIA user-mode driver version encodes the public driver number in
    // its low digits, which is enough to show the operator what they are on.
    const uint64_t version = static_cast<uint64_t>(umd.QuadPart);
    const unsigned product = static_cast<unsigned>((version >> 16) & 0xFFFF);
    const unsigned build = static_cast<unsigned>(version & 0xFFFF);
    r.detail = "User-mode driver " + std::to_string(product) + "." + std::to_string(build);
    r.state = ProbeState::Ok;
    return r;
  }

  r.state = ProbeState::Warn;
  r.detail = "Could not read the driver version.";
  r.remedy = "Not fatal. Update to a current NVIDIA driver if capture misbehaves.";
  return r;
}

ProbeResult ProbeWindows() {
  ProbeResult r;
  r.title = "Windows version";

  RTL_OSVERSIONINFOW info{};
  info.dwOSVersionInfoSize = sizeof(info);
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  bool queried = false;
  if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
    // GetVersionEx lies unless the binary carries a compatibility manifest;
    // RtlGetVersion reports the real build.
    auto fn = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (fn && fn(&info) == 0) queried = true;
  }

  if (!queried) {
    r.state = ProbeState::Warn;
    r.detail = "Could not read the Windows build number.";
    r.remedy = "Not fatal, but this project is only supported on Windows 11.";
    return r;
  }

  r.detail = "Build " + std::to_string(info.dwBuildNumber);
  if (info.dwBuildNumber >= 22000) {
    r.state = ProbeState::Ok;
    return r;
  }
  r.state = ProbeState::Fail;
  r.remedy = "Windows 11 is required: the overlay depends on compositor "
             "behaviour that Windows 10 does not provide.";
  return r;
}

ProbeResult ProbeRefreshRate() {
  ProbeResult r;
  r.title = "Display refresh rate";

  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
    r.state = ProbeState::Warn;
    r.detail = "Could not read the current display mode.";
    r.remedy = "Not fatal. Check your monitor settings if pacing looks wrong.";
    return r;
  }

  r.detail = std::to_string(mode.dmDisplayFrequency) + " Hz";
  if (mode.dmDisplayFrequency >= 120) {
    r.state = ProbeState::Ok;
    return r;
  }
  r.state = ProbeState::Warn;
  r.remedy = "Below 120 Hz the overlay's added latency is a larger share of the "
             "frame. It will still run.";
  return r;
}

ProbeResult ProbeNeuralRuntime(const fs::path& sidecarDir) {
  ProbeResult r;
  r.title = "Neural runtime";

  std::error_code ec;
  const fs::path dll = sidecarDir / "nvngx_dlssnr.dll";
  if (!fs::exists(dll, ec) || ec) {
    r.state = ProbeState::Warn;
    r.detail = "nvngx_dlssnr.dll not found next to the sidecar.";
    r.remedy = "Optional until the neural pass ships. Without it the pipeline "
               "runs the passthrough pass.";
    return r;
  }

  // Hash it rather than trust the filename. A wrong or truncated binary here
  // becomes an access violation inside NGX at feature creation (spec §10), and
  // a digest is the only thing that distinguishes it from a good one beforehand.
  return NeuralRuntimeVerdict(dll.filename().string(), Sha256File(dll));
}

ProbeResult NeuralRuntimeVerdict(std::string_view fileName,
                                 std::string_view sha256Hex) {
  ProbeResult r;
  r.title = "Neural runtime";

  if (sha256Hex.empty()) {
    r.state = ProbeState::Warn;
    r.detail = std::string(fileName) + " is present but could not be read.";
    r.remedy = "Check the file is not locked by another process, and that the "
               "sidecar has permission to read it.";
    return r;
  }

  r.detail = DescribeRuntime(fileName, sha256Hex);
  if (LookupRuntime(sha256Hex)) {
    r.state = ProbeState::Ok;
    return r;
  }

  // Amber, not red. A newer runtime than this manifest knows is a legitimate
  // thing for an operator to have, and refusing it outright would age badly.
  r.state = ProbeState::Warn;
  r.remedy = "The pass will still try this build. If it fails, quote the "
             "SHA-256 above when reporting it.";
  return r;
}

ProbeResult ProbeReshade(const fs::path& sidecarDir) {
  ProbeResult r;
  r.title = "ReShade host (optional)";

  // ReShade installs itself as a proxy DLL named after the API the host imports,
  // not as ReShade64.dll -- which is what this probe used to look for, so it
  // never once found a real install. The sidecar imports dxgi, so dxgi.dll is
  // the name here; the others are listed because an operator may have copied a
  // proxy built for a different API.
  static constexpr const char* kProxyNames[] = {
      "dxgi.dll", "d3d12.dll", "d3d11.dll", "ReShade64.dll"};

  std::error_code ec;
  for (const char* name : kProxyNames) {
    if (fs::exists(sidecarDir / name, ec) && !ec) {
      r.state = ProbeState::Ok;
      r.detail = std::string(name) +
                 " present next to the sidecar, where it is harmless.";
      return r;
    }
  }

  r.state = ProbeState::Warn;
  r.detail = "No ReShade host alongside the sidecar.";
  r.remedy = "Only needed for the ReShade-hosted neural pass. Install ReShade "
             "against the sidecar so it lands as dxgi.dll. Never place ReShade "
             "next to Wow.exe.";
  return r;
}

ProbeResult ProbeWowWindow() {
  ProbeResult r;
  r.title = "World of Warcraft window";

  const auto wow = FindWowWindow();
  if (!wow) {
    r.state = ProbeState::Warn;
    r.detail = "WoW is not running.";
    r.remedy = "Start the game, then run the probes again.";
    return r;
  }

  const auto width = wow->clientScreen.right - wow->clientScreen.left;
  const auto height = wow->clientScreen.bottom - wow->clientScreen.top;
  r.detail = std::to_string(width) + "x" + std::to_string(height);

  if (wow->borderless) {
    r.state = ProbeState::Ok;
    r.detail += " borderless windowed";
    return r;
  }
  r.state = ProbeState::Fail;
  r.detail += " windowed with a border, or exclusive fullscreen";
  r.remedy = "Set WoW to borderless windowed. Exclusive fullscreen has no "
             "compositor surface to capture and yields black frames.";
  return r;
}

ProbeResult ProbeInjectorScan(const fs::path& wowDir) {
  ProbeResult r;
  r.title = "Injector scan of the WoW folder";

  std::error_code ec;
  if (wowDir.empty() || !fs::is_directory(wowDir, ec) || ec) {
    r.state = ProbeState::Warn;
    r.detail = "No WoW folder set, so nothing was scanned.";
    r.remedy = "Point the manager at your WoW folder so it can check for "
               "injectors before launching.";
    return r;
  }

  // Filenames only. Nothing in WoW's directory is ever opened or read (I5, I8).
  std::vector<std::string> filenames;
  for (const auto& entry : fs::directory_iterator(wowDir, ec)) {
    if (ec) break;
    if (entry.is_regular_file(ec)) filenames.push_back(entry.path().filename().string());
  }

  const auto found = FindInjectorLoaders(filenames);
  if (found.empty()) {
    r.state = ProbeState::Ok;
    r.detail = "No injector loaders found.";
    return r;
  }

  r.state = ProbeState::Fail;
  r.detail = "Found: ";
  for (size_t i = 0; i < found.size(); ++i) {
    if (i) r.detail += ", ";
    r.detail += found[i];
  }
  r.remedy = "Remove these from your WoW folder. Blizzard bans accounts for "
             "in-process injectors, and this tool refuses to run alongside one.";
  return r;
}

ProbeResult ProbeSidecarPath(const fs::path& sidecarDir, const fs::path& wowDir) {
  ProbeResult r;
  r.title = "Sidecar install location";
  r.detail = sidecarDir.generic_string();

  if (PathLooksLikeWowInstall(sidecarDir) || IsInside(sidecarDir, wowDir)) {
    r.state = ProbeState::Fail;
    r.remedy = "Move the sidecar outside your WoW folder. Anything sitting next "
               "to Wow.exe looks like an injector, which is the one thing this "
               "design exists to avoid.";
    return r;
  }

  r.state = ProbeState::Ok;
  return r;
}

std::vector<ProbeResult> RunAllProbes(const fs::path& sidecarDir, const fs::path& wowDir) {
  return {
      ProbeGpu(),
      ProbeDriver(),
      ProbeWindows(),
      ProbeRefreshRate(),
      ProbeWowWindow(),
      ProbeSidecarPath(sidecarDir, wowDir),
      ProbeInjectorScan(wowDir),
      ProbeNeuralRuntime(sidecarDir),
      ProbeReshade(sidecarDir),
  };
}

}  // namespace sidecar
