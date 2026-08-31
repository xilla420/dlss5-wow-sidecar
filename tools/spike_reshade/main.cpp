// Throwaway spike for the M3 plan, Task 4: does the ReShade add-on's detour
// fire in *our* process? NOT shipped, not linked into sidecar_common.
//
// Task 4 is the gate for all of M3. Route B's premise is that ReShade loaded
// into the sidecar lets renodx-dlss5.addon64 detour the sidecar's own NGX
// calls. The add-on was written to sit inside a game, and this process is not
// shaped like one in two ways that plausibly matter:
//
//   1. The overlay presents through a *composition* swapchain created on a
//      D3D12 command queue with no HWND (DCompOverlay uses
//      CreateSwapChainForComposition). Every game ReShade targets presents
//      through CreateSwapChainForHwnd. This spike reproduces the composition
//      shape deliberately -- testing an HWND swapchain would answer a question
//      about a program we do not ship.
//
//   2. Nothing here draws a game. The add-on's own strings say it activates by
//      intercepting a DLSS or DLSSD feature the *host* creates and harvesting
//      that feature's colour/depth/motion contract. So the spike creates and
//      evaluates a real DLSS feature, which is the only thing the add-on is
//      documented to react to.
//
// What it reports:
//   - which of ReShade, the add-on and the NGX runtimes actually loaded;
//   - whether the NGX entry points were detoured, by byte-diffing each export's
//     prologue before and after ReShade comes up;
//   - what our own NGX calls returned.
// ReShade.log, written next to the exe, carries the add-on's side of the story.
//
// Run it from a directory holding ReShade's dxgi.dll, renodx-dlss5.addon64,
// ReShade.ini and the nvngx_* runtimes -- never from the build output
// directory, or ReShade would be injected into wowsidecar.exe on its next run.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <psapi.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_helpers.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr unsigned long long kApplicationId = 0x0;
constexpr UINT kWidth = 1920;
constexpr UINT kHeight = 1080;

const char* ResultName(NVSDK_NGX_Result r) {
  switch (r) {
    case NVSDK_NGX_Result_Success:                        return "Success";
    case NVSDK_NGX_Result_Fail:                           return "Fail";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported:       return "FeatureNotSupported";
    case NVSDK_NGX_Result_FAIL_PlatformError:             return "PlatformError";
    case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists:      return "FeatureAlreadyExists";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound:           return "FeatureNotFound";
    case NVSDK_NGX_Result_FAIL_InvalidParameter:          return "InvalidParameter";
    case NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall:     return "ScratchBufferTooSmall";
    case NVSDK_NGX_Result_FAIL_NotInitialized:            return "NotInitialized";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat:    return "UnsupportedInputFormat";
    case NVSDK_NGX_Result_FAIL_RWFlagMissing:             return "RWFlagMissing";
    case NVSDK_NGX_Result_FAIL_MissingInput:              return "MissingInput";
    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature: return "UnableToInitializeFeature";
    case NVSDK_NGX_Result_FAIL_OutOfDate:                 return "OutOfDate";
    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory:            return "OutOfGPUMemory";
    case NVSDK_NGX_Result_FAIL_UnsupportedFormat:         return "UnsupportedFormat";
    case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath:return "UnableToWriteToAppDataPath";
    case NVSDK_NGX_Result_FAIL_UnsupportedParameter:      return "UnsupportedParameter";
    case NVSDK_NGX_Result_FAIL_Denied:                    return "Denied";
    case NVSDK_NGX_Result_FAIL_NotImplemented:            return "NotImplemented";
    default:                                              return "<unknown>";
  }
}

void ListInterestingModules(const wchar_t* when) {
  wprintf(L"--- loaded modules of interest (%s) ---\n", when);
  HMODULE mods[1024];
  DWORD needed = 0;
  if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
    wprintf(L"  EnumProcessModules failed: %lu\n", GetLastError());
    return;
  }
  const size_t count = needed / sizeof(HMODULE);
  bool any = false;
  for (size_t i = 0; i < count; ++i) {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(mods[i], path, MAX_PATH)) continue;
    std::wstring lower(path);
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    if (lower.find(L"nvngx") != std::wstring::npos ||
        lower.find(L"reshade") != std::wstring::npos ||
        lower.find(L"addon") != std::wstring::npos ||
        lower.find(L"renodx") != std::wstring::npos ||
        lower.find(L"dxgi.dll") != std::wstring::npos ||
        lower.find(L"\\sl.") != std::wstring::npos) {
      wprintf(L"  %p  %s\n", static_cast<void*>(mods[i]), path);
      any = true;
    }
  }
  if (!any) wprintf(L"  (none)\n");
  wprintf(L"\n");
}

// An inline detour rewrites the first bytes of the target with a jump. Snapshot
// them before ReShade is in the process and again afterwards: any export whose
// prologue changed was hooked, and by whom is then a question for ReShade.log.
const char* kNgxEntryPoints[] = {
    "NVSDK_NGX_D3D12_Init",
    "NVSDK_NGX_D3D12_Init_Ext",
    "NVSDK_NGX_D3D12_CreateFeature",
    "NVSDK_NGX_D3D12_EvaluateFeature",
    "NVSDK_NGX_D3D12_ReleaseFeature",
    "NVSDK_NGX_D3D12_AllocateParameters",
};

struct Prologue {
  std::wstring module;
  std::string entry;
  unsigned char bytes[16];
  bool valid = false;
};

std::vector<Prologue> SnapshotNgxPrologues() {
  std::vector<Prologue> out;
  HMODULE mods[1024];
  DWORD needed = 0;
  if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) return out;
  const size_t count = needed / sizeof(HMODULE);
  for (size_t i = 0; i < count; ++i) {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(mods[i], path, MAX_PATH)) continue;
    std::wstring lower(path);
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    if (lower.find(L"nvngx") == std::wstring::npos) continue;
    for (const char* entry : kNgxEntryPoints) {
      FARPROC proc = GetProcAddress(mods[i], entry);
      if (!proc) continue;
      Prologue p;
      p.module = path;
      p.entry = entry;
      std::memcpy(p.bytes, reinterpret_cast<const void*>(proc), sizeof(p.bytes));
      p.valid = true;
      out.push_back(p);
    }
  }
  return out;
}

void ReportPrologueDiff(const std::vector<Prologue>& before,
                        const std::vector<Prologue>& after) {
  wprintf(L"--- NGX entry-point prologues, before vs after ReShade ---\n");
  int hooked = 0;
  for (const auto& a : after) {
    const Prologue* b = nullptr;
    for (const auto& x : before) {
      if (x.entry == a.entry && x.module == a.module) { b = &x; break; }
    }
    if (!b) {
      wprintf(L"  %-40hs  NEW MODULE (not present before ReShade loaded)\n",
              a.entry.c_str());
      continue;
    }
    if (std::memcmp(a.bytes, b->bytes, sizeof(a.bytes)) != 0) {
      ++hooked;
      wprintf(L"  %-40hs  CHANGED  ", a.entry.c_str());
      wprintf(L"before:");
      for (int i = 0; i < 8; ++i) wprintf(L" %02X", b->bytes[i]);
      wprintf(L"  after:");
      for (int i = 0; i < 8; ++i) wprintf(L" %02X", a.bytes[i]);
      wprintf(L"\n");
    } else {
      wprintf(L"  %-40hs  unchanged\n", a.entry.c_str());
    }
  }
  wprintf(L"\n  %d entry point(s) detoured.\n\n", hooked);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(h, m, w, l);
}

ComPtr<IDXGIAdapter1> FirstNvidiaAdapter() {
  ComPtr<IDXGIFactory6> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return nullptr;
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0;
       factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                           IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
       ++i) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.VendorId == 0x10DE) return adapter;
  }
  return nullptr;
}

ComPtr<ID3D12Resource> MakeTexture(ID3D12Device* device, DXGI_FORMAT format,
                                   UINT width, UINT height) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> res;
  device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                  D3D12_RESOURCE_STATE_COMMON, nullptr,
                                  IID_PPV_ARGS(&res));
  return res;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  // Unbuffered: the interesting failures here happen inside a detour, and a
  // buffered tail is lost exactly when it matters most.
  setvbuf(stdout, nullptr, _IONBF, 0);

  // NGX resolves the feature-dll search path as given. A relative path silently
  // finds nothing, which reads identically to "the driver refuses DLSS" -- it
  // cost a wrong conclusion once already, so make it absolute here.
  std::wstring runtimePath = L".";
  if (argc > 1) runtimePath = argv[1];
  wchar_t absolute[MAX_PATH];
  if (GetFullPathNameW(runtimePath.c_str(), MAX_PATH, absolute, nullptr) != 0) {
    runtimePath = absolute;
  }

  wprintf(L"=== ReShade detour spike (M3 Task 4) ===\n");
  wprintf(L"nvngx runtime search path: %s\n\n", runtimePath.c_str());

  ListInterestingModules(L"at startup, before we load anything");

  auto adapter = FirstNvidiaAdapter();
  if (!adapter) { wprintf(L"no NVIDIA adapter\n"); return 1; }
  DXGI_ADAPTER_DESC1 adesc{};
  adapter->GetDesc1(&adesc);
  wprintf(L"adapter: %s\n\n", adesc.Description);

  ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
    wprintf(L"D3D12CreateDevice failed\n");
    return 1;
  }
  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC qd{};
  qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
    wprintf(L"CreateCommandQueue failed\n");
    return 1;
  }

  // Bring the NGX core up first, so its exports exist to be snapshotted before
  // ReShade has had any chance to touch them.
  const wchar_t* paths[1] = {runtimePath.c_str()};
  NVSDK_NGX_FeatureCommonInfo common{};
  common.PathListInfo.Path = paths;
  common.PathListInfo.Length = 1;

  // Application id 0 means "no registered title". DLSS is the feature most
  // likely to care, so use the project-id path, which is what NVIDIA points
  // unregistered callers at, and fall back to the plain one if it refuses.
  NVSDK_NGX_Result init = NVSDK_NGX_D3D12_Init_with_ProjectID(
      "a0f57b54-1daf-4934-90ae-c4035c19df04", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0",
      L".", device.Get(), &common, NVSDK_NGX_Version_API);
  wprintf(L"--- NVSDK_NGX_D3D12_Init_with_ProjectID -> %hs ---\n", ResultName(init));
  if (init != NVSDK_NGX_Result_Success) {
    init = NVSDK_NGX_D3D12_Init(kApplicationId, L".", device.Get(), &common,
                                NVSDK_NGX_Version_API);
    wprintf(L"--- NVSDK_NGX_D3D12_Init (fallback) -> %hs ---\n", ResultName(init));
  }

  // If DLSS will not come up, the capability block says why in a way no amount
  // of guessing at create parameters would.
  NVSDK_NGX_Parameter* caps = nullptr;
  if (init == NVSDK_NGX_Result_Success &&
      NVSDK_NGX_D3D12_GetCapabilityParameters(&caps) == NVSDK_NGX_Result_Success && caps) {
    const char* keys[] = {
        NVSDK_NGX_Parameter_SuperSampling_Available,
        NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
        NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,
        NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,
        NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,
    };
    wprintf(L"--- DLSS capability block ---\n");
    for (const char* key : keys) {
      int value = -1;
      const NVSDK_NGX_Result r = caps->Get(key, &value);
      const unsigned int raw = static_cast<unsigned int>(value);
      wprintf(L"  %-56hs get=%-12hs value=%d", key, ResultName(r), value);
      if ((raw & 0xFFF00000u) == 0xBAD00000u) {
        wprintf(L"  (0x%08X = %hs)", raw, ResultName(static_cast<NVSDK_NGX_Result>(raw)));
      }
      wprintf(L"\n");
    }
  }
  wprintf(L"\n");

  auto before = SnapshotNgxPrologues();
  wprintf(L"snapshotted %zu NGX entry point(s) before ReShade\n\n", before.size());

  // --- The window and composition swapchain, mirroring DCompOverlay ---------
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"SpikeReshadeHost";
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW,
                              wc.lpszClassName, L"spike", WS_POPUP,
                              100, 100, 640, 360, nullptr, nullptr,
                              wc.hInstance, nullptr);
  if (!hwnd) { wprintf(L"CreateWindowExW failed\n"); return 1; }
  ShowWindow(hwnd, SW_SHOW);

  ComPtr<IDXGIFactory2> factory;
  CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
  DXGI_SWAP_CHAIN_DESC1 scd{};
  scd.Width = 640;
  scd.Height = 360;
  scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.BufferCount = 2;
  scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  scd.SampleDesc.Count = 1;
  ComPtr<IDXGISwapChain1> swapChain;
  const HRESULT schr = factory->CreateSwapChainForComposition(queue.Get(), &scd, nullptr,
                                                              &swapChain);
  wprintf(L"--- CreateSwapChainForComposition -> 0x%08X ---\n", schr);

  ComPtr<IDCompositionDevice> dcomp;
  ComPtr<IDCompositionTarget> target;
  ComPtr<IDCompositionVisual> visual;
  if (SUCCEEDED(schr)) {
    DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&dcomp));
    if (dcomp) {
      dcomp->CreateTargetForHwnd(hwnd, TRUE, &target);
      dcomp->CreateVisual(&visual);
      if (visual && target) {
        visual->SetContent(swapChain.Get());
        target->SetRoot(visual.Get());
        dcomp->Commit();
      }
    }
  }
  wprintf(L"\n");

  // Present for a while. ReShade installs its hooks when it first sees a
  // swapchain present, and the add-on's own text says it reattaches every
  // present, so a single frame would not be a fair test.
  wprintf(L"--- presenting 180 frames so ReShade and the add-on can attach ---\n");
  for (int i = 0; i < 180; ++i) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    swapChain->Present(1, 0);
  }
  wprintf(L"done presenting\n\n");

  ListInterestingModules(L"after presenting");
  auto after = SnapshotNgxPrologues();
  ReportPrologueDiff(before, after);

  // --- Give the add-on the one thing it reacts to: a real DLSS feature ------
  if (init != NVSDK_NGX_Result_Success) {
    wprintf(L"NGX did not initialise, so no DLSS feature can be offered.\n");
    return 2;
  }

  NVSDK_NGX_Parameter* params = nullptr;
  if (NVSDK_NGX_D3D12_AllocateParameters(&params) != NVSDK_NGX_Result_Success || !params) {
    wprintf(L"AllocateParameters failed\n");
    return 3;
  }

  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
  device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr,
                            IID_PPV_ARGS(&cmdList));

  // A 1:1 ratio at MaxQuality is not a configuration DLSS accepts -- 1:1 is
  // DLAA and wants its own quality value. Rather than guess which detail the
  // runtime objected to, try the plausible shapes and let it say.
  struct CreateConfig {
    const wchar_t* label;
    UINT inWidth, inHeight;
    NVSDK_NGX_PerfQuality_Value quality;
    int flags;
  };
  const CreateConfig configs[] = {
      {L"1280x720 -> 1920x1080, MaxQuality, MVLowRes",
       1280, 720, NVSDK_NGX_PerfQuality_Value_MaxQuality,
       NVSDK_NGX_DLSS_Feature_Flags_MVLowRes},
      {L"1280x720 -> 1920x1080, Balanced, MVLowRes|IsHDR",
       1280, 720, NVSDK_NGX_PerfQuality_Value_Balanced,
       NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_IsHDR},
      {L"1920x1080 -> 1920x1080, DLAA, no flags",
       kWidth, kHeight, NVSDK_NGX_PerfQuality_Value_DLAA, 0},
      {L"1920x1080 -> 1920x1080, MaxQuality, IsHDR (original attempt)",
       kWidth, kHeight, NVSDK_NGX_PerfQuality_Value_MaxQuality,
       NVSDK_NGX_DLSS_Feature_Flags_IsHDR},
  };

  NVSDK_NGX_Handle* handle = nullptr;
  NVSDK_NGX_Result created = NVSDK_NGX_Result_Fail;
  UINT renderWidth = kWidth, renderHeight = kHeight;
  wprintf(L"--- NGX_D3D12_CREATE_DLSS_EXT, across plausible shapes ---\n");
  for (const auto& cfg : configs) {
    NVSDK_NGX_DLSS_Create_Params createParams{};
    createParams.Feature.InWidth = cfg.inWidth;
    createParams.Feature.InHeight = cfg.inHeight;
    createParams.Feature.InTargetWidth = kWidth;
    createParams.Feature.InTargetHeight = kHeight;
    createParams.Feature.InPerfQualityValue = cfg.quality;
    createParams.InFeatureCreateFlags = cfg.flags;

    NVSDK_NGX_Handle* h = nullptr;
    const NVSDK_NGX_Result r =
        NGX_D3D12_CREATE_DLSS_EXT(cmdList.Get(), 1, 1, &h, params, &createParams);
    wprintf(L"  %-56s -> %hs\n", cfg.label, ResultName(r));
    if (r == NVSDK_NGX_Result_Success && h) {
      handle = h;
      created = r;
      renderWidth = cfg.inWidth;
      renderHeight = cfg.inHeight;
      break;
    }
    if (h) NVSDK_NGX_D3D12_ReleaseFeature(h);
  }
  wprintf(L"\n");

  if (created == NVSDK_NGX_Result_Success && handle) {
    auto color  = MakeTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
                              renderWidth, renderHeight);
    auto output = MakeTexture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, kWidth, kHeight);
    auto mvec   = MakeTexture(device.Get(), DXGI_FORMAT_R16G16_FLOAT,
                              renderWidth, renderHeight);
    auto depth  = MakeTexture(device.Get(), DXGI_FORMAT_R32_FLOAT,
                              renderWidth, renderHeight);

    NVSDK_NGX_D3D12_DLSS_Eval_Params eval{};
    eval.Feature.pInColor = color.Get();
    eval.Feature.pInOutput = output.Get();
    eval.Feature.InSharpness = 0.0f;
    eval.pInDepth = depth.Get();
    eval.pInMotionVectors = mvec.Get();
    eval.InJitterOffsetX = 0.0f;
    eval.InJitterOffsetY = 0.0f;
    eval.InRenderSubrectDimensions.Width = renderWidth;
    eval.InRenderSubrectDimensions.Height = renderHeight;
    eval.InReset = 1;
    eval.InMVScaleX = 1.0f;
    eval.InMVScaleY = 1.0f;

    // Evaluate several times: the add-on reports counting evaluations, and its
    // status text distinguishes "no create seen" from "created, not evaluated".
    for (int i = 0; i < 5; ++i) {
      const NVSDK_NGX_Result r =
          NGX_D3D12_EVALUATE_DLSS_EXT(cmdList.Get(), handle, params, &eval);
      if (i == 0) wprintf(L"--- NGX_D3D12_EVALUATE_DLSS_EXT -> %hs ---\n", ResultName(r));
      eval.InReset = 0;
    }
    cmdList->Close();
    ID3D12CommandList* lists[] = {cmdList.Get()};
    queue->ExecuteCommandLists(1, lists);

    // Let the add-on see more presents after the evaluations.
    for (int i = 0; i < 120; ++i) swapChain->Present(1, 0);

    NVSDK_NGX_D3D12_ReleaseFeature(handle);
  } else {
    cmdList->Close();
  }
  wprintf(L"\n");

  ListInterestingModules(L"at exit");
  NVSDK_NGX_D3D12_DestroyParameters(params);
  NVSDK_NGX_D3D12_Shutdown1(device.Get());
  DestroyWindow(hwnd);
  wprintf(L"done. Read ReShade.log next to this exe for the add-on's account.\n");
  return 0;
}
