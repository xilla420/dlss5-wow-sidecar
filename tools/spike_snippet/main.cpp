// Throwaway spike for the M3 plan. NOT shipped, not linked into
// sidecar_common, and archived once its question is answered.
//
// Task 3's spike concluded that DLSS 5 Neural Rendering is unreachable, on the
// evidence that the NGX *core* (_nvngx.dll) refuses every feature id at 18 and
// above with OutOfDate, and that no SDK version above 0x15 gets past Init. All
// of that still holds. What it missed is that the core is not the only way in.
//
// nvngx_dlssnr.dll is an NGX *snippet*, and it exports the D3D12 entry points
// itself: Init_Ext, CreateFeature, EvaluateFeature, ReleaseFeature, Shutdown1,
// PopulateParameters_Impl. Normally the core loads a snippet on the caller's
// behalf once it recognises the feature id -- which is exactly the step that
// fails for id 18. Resolving those exports out of the snippet directly skips
// the core's registry entirely.
//
// That is also, judging by its strings, what renodx-dlss5.addon64 does: it
// resolves the same six names out of nvngx_dlssnr.dll and calls them, and it
// labels feature 18 "DLSSNR/reserved-18".
//
// So the question here is narrow and answerable:
//
//   Does nvngx_dlssnr.dll accept a direct Init_Ext + CreateFeature for feature
//   18 on our own D3D12 device, with no ReShade and no add-on in the process?
//
// If yes, route A (DirectNgxPass, M6) is alive after all and the third-party
// dependency can be dropped. If no, the failure code says which door closed.
//
// Usage:
//   spike_snippet.exe <path-to-nvngx_dlssnr.dll> [sdk-version-hex]
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr unsigned long long kApplicationId = 0x0;

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

using PFN_InitExt = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*,
                                                  ID3D12Device*, NVSDK_NGX_Version,
                                                  const NVSDK_NGX_Parameter*);
using PFN_Init = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*,
                                               ID3D12Device*,
                                               const NVSDK_NGX_FeatureCommonInfo*,
                                               NVSDK_NGX_Version);
using PFN_CreateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*,
                                                        NVSDK_NGX_Feature,
                                                        NVSDK_NGX_Parameter*,
                                                        NVSDK_NGX_Handle**);
using PFN_ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using PFN_Shutdown1 = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
using PFN_PopulateParams = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
using PFN_GetU32 = unsigned int(NVSDK_CONV*)();

// Calling into a snippet the core never dispatched to is precisely the sort of
// thing that faults rather than returning a code. A spike that dies on an
// access violation reports nothing at all, so every raw call goes through one
// of these. They hold only PODs, which is what lets __try coexist with /EHsc.
NVSDK_NGX_Result GuardedInitExt(PFN_InitExt fn, ID3D12Device* device,
                                NVSDK_NGX_Version version,
                                const NVSDK_NGX_Parameter* params, DWORD* outSeh) {
  *outSeh = 0;
  __try {
    return fn(kApplicationId, L".", device, version, params);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *outSeh = GetExceptionCode();
    return NVSDK_NGX_Result_Fail;
  }
}

NVSDK_NGX_Result GuardedInitPlain(PFN_Init fn, ID3D12Device* device,
                                  const NVSDK_NGX_FeatureCommonInfo* common,
                                  NVSDK_NGX_Version version, DWORD* outSeh) {
  *outSeh = 0;
  __try {
    return fn(kApplicationId, L".", device, common, version);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *outSeh = GetExceptionCode();
    return NVSDK_NGX_Result_Fail;
  }
}

NVSDK_NGX_Result GuardedCreateFeature(PFN_CreateFeature fn, ID3D12GraphicsCommandList* cmd,
                                      int id, NVSDK_NGX_Parameter* params,
                                      NVSDK_NGX_Handle** outHandle, DWORD* outSeh) {
  *outSeh = 0;
  __try {
    return fn(cmd, static_cast<NVSDK_NGX_Feature>(id), params, outHandle);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *outSeh = GetExceptionCode();
    return NVSDK_NGX_Result_Fail;
  }
}

NVSDK_NGX_Result GuardedPopulate(PFN_PopulateParams fn, NVSDK_NGX_Parameter* params,
                                 DWORD* outSeh) {
  *outSeh = 0;
  __try {
    return fn(params);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *outSeh = GetExceptionCode();
    return NVSDK_NGX_Result_Fail;
  }
}

unsigned int GuardedGetU32(PFN_GetU32 fn, DWORD* outSeh) {
  *outSeh = 0;
  __try {
    return fn();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    *outSeh = GetExceptionCode();
    return 0;
  }
}

void ReportCall(const wchar_t* what, NVSDK_NGX_Result r, DWORD seh) {
  if (seh != 0) {
    wprintf(L"  %-46s -> SEH 0x%08X (crashed, not a return code)\n", what, seh);
  } else {
    wprintf(L"  %-46s -> %hs\n", what, ResultName(r));
  }
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

// The DLSSNR.* names below are lifted verbatim from renodx-dlss5.addon64's
// string table. They are the only published account of this feature's contract,
// and the point of setting them is to find out whether the snippet recognises
// its own vocabulary when the call arrives from us rather than from the add-on.
void SetNeuralRenderingParams(NVSDK_NGX_Parameter* p, unsigned int width,
                              unsigned int height) {
  p->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
  p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);

  p->Set("DLSSNR.Enabled", 1);
  p->Set("DLSSNR.Width", width);
  p->Set("DLSSNR.Height", height);
  p->Set("DLSSNR.InputWidth", width);
  p->Set("DLSSNR.InputHeight", height);
  p->Set("DLSSNR.OutputWidth", width);
  p->Set("DLSSNR.OutputHeight", height);
  p->Set("DLSSNR.Output.Width", width);
  p->Set("DLSSNR.Output.Height", height);
  p->Set("DLSSNR.Upscaling", 0);
  p->Set("DLSSNR.Scale", 1.0f);
  p->Set("DLSSNR.ScalingRatio", 1.0f);
  p->Set("DLSSNR.DepthInverted", 0);
  p->Set("DLSSNR.Reset", 1);
  p->Set("DLSSNR.MVecScaleX", static_cast<float>(width));
  p->Set("DLSSNR.MVecScaleY", static_cast<float>(height));
  p->Set("DLSSNR.Intensity", 1.0f);
  p->Set("DLSSNR.UseAutoMask", 1);
  p->Set("DLSSNR.Style", 0);

  // The generic DLSS creation keys as well: a snippet that shares plumbing with
  // DLSS may look for these instead of, or as well as, its own.
  p->Set(NVSDK_NGX_Parameter_Width, width);
  p->Set(NVSDK_NGX_Parameter_Height, height);
  p->Set(NVSDK_NGX_Parameter_OutWidth, width);
  p->Set(NVSDK_NGX_Parameter_OutHeight, height);
  p->Set(NVSDK_NGX_Parameter_PerfQualityValue,
         static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));
  p->Set("DLSS.Feature.Create.Flags", 0);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) {
    wprintf(L"usage: spike_snippet.exe <path-to-nvngx_dlssnr.dll> [sdk-version-hex]\n");
    return 1;
  }
  const std::wstring snippetPath = argv[1];
  NVSDK_NGX_Version sdkVersion = NVSDK_NGX_Version_API;
  if (argc > 2) sdkVersion = static_cast<NVSDK_NGX_Version>(wcstoul(argv[2], nullptr, 0));

  wprintf(L"=== NGX snippet spike: direct CreateFeature on nvngx_dlssnr.dll ===\n");
  wprintf(L"snippet: %s\n", snippetPath.c_str());
  wprintf(L"reported SDK API version: 0x%08X (header default 0x%08X)\n\n",
          static_cast<unsigned>(sdkVersion),
          static_cast<unsigned>(NVSDK_NGX_Version_API));

  auto adapter = FirstNvidiaAdapter();
  if (!adapter) {
    wprintf(L"no NVIDIA adapter\n");
    return 1;
  }
  DXGI_ADAPTER_DESC1 adesc{};
  adapter->GetDesc1(&adesc);
  wprintf(L"adapter: %s (device id 0x%04X)\n\n", adesc.Description, adesc.DeviceId);

  ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
    wprintf(L"D3D12CreateDevice failed\n");
    return 1;
  }

  // --- 1. Load the snippet and resolve its own entry points -----------------
  HMODULE snippet = LoadLibraryExW(snippetPath.c_str(), nullptr,
                                   LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!snippet) {
    wprintf(L"LoadLibraryExW failed: %lu\n", GetLastError());
    return 2;
  }
  wprintf(L"--- snippet loaded at %p ---\n", static_cast<void*>(snippet));

  struct Resolved { const char* name; FARPROC proc; };
  Resolved wanted[] = {
      {"NVSDK_NGX_D3D12_Init", nullptr},
      {"NVSDK_NGX_D3D12_Init_Ext", nullptr},
      {"NVSDK_NGX_D3D12_CreateFeature", nullptr},
      {"NVSDK_NGX_D3D12_EvaluateFeature", nullptr},
      {"NVSDK_NGX_D3D12_ReleaseFeature", nullptr},
      {"NVSDK_NGX_D3D12_Shutdown1", nullptr},
      {"NVSDK_NGX_D3D12_PopulateParameters_Impl", nullptr},
      {"NVSDK_NGX_GetAPIVersion", nullptr},
      {"NVSDK_NGX_GetSnippetVersion", nullptr},
      {"NVSDK_NGX_GetGPUArchitecture", nullptr},
  };
  for (auto& w : wanted) {
    w.proc = GetProcAddress(snippet, w.name);
    wprintf(L"  %-44hs %s\n", w.name, w.proc ? L"resolved" : L"MISSING");
  }
  wprintf(L"\n");

  auto find = [&](const char* n) -> FARPROC {
    for (auto& w : wanted) {
      if (std::strcmp(w.name, n) == 0) return w.proc;
    }
    return nullptr;
  };

  // --- 2. What does the snippet say about itself, before any init? ----------
  wprintf(L"--- snippet self-report ---\n");
  DWORD seh = 0;
  if (auto f = reinterpret_cast<PFN_GetU32>(find("NVSDK_NGX_GetAPIVersion"))) {
    const unsigned int v = GuardedGetU32(f, &seh);
    if (seh) wprintf(L"  GetAPIVersion      -> SEH 0x%08X\n", seh);
    else     wprintf(L"  GetAPIVersion      -> 0x%08X\n", v);
  }
  if (auto f = reinterpret_cast<PFN_GetU32>(find("NVSDK_NGX_GetSnippetVersion"))) {
    const unsigned int v = GuardedGetU32(f, &seh);
    if (seh) wprintf(L"  GetSnippetVersion  -> SEH 0x%08X\n", seh);
    // major is the top 16 bits: 0x0136 is 310, which lines up with the add-on
    // calling this build "DLSSNR v310.8.0".
    else     wprintf(L"  GetSnippetVersion  -> 0x%08X (%u.%u.%u)\n", v,
                     (v >> 16) & 0xFFFFu, (v >> 8) & 0xFFu, v & 0xFFu);
  }
  wprintf(L"\n");

  // --- 3. A parameter block. Only the core exports AllocateParameters, so we
  //        take a core session purely to obtain one; every feature call below
  //        goes to the snippet.
  const NVSDK_NGX_Result coreInit =
      NVSDK_NGX_D3D12_Init(kApplicationId, L".", device.Get(), nullptr, sdkVersion);
  wprintf(L"--- core NVSDK_NGX_D3D12_Init (for AllocateParameters only) -> %hs ---\n",
          ResultName(coreInit));
  NVSDK_NGX_Parameter* params = nullptr;
  if (coreInit == NVSDK_NGX_Result_Success) {
    const NVSDK_NGX_Result ar = NVSDK_NGX_D3D12_AllocateParameters(&params);
    wprintf(L"  AllocateParameters -> %hs\n", ResultName(ar));
  }
  if (!params) {
    wprintf(L"\nno parameter block, so nothing below could be attempted.\n");
    return 3;
  }
  wprintf(L"\n");

  // Let the snippet fill in its own capability values if it will.
  if (auto f = reinterpret_cast<PFN_PopulateParams>(
          find("NVSDK_NGX_D3D12_PopulateParameters_Impl"))) {
    const NVSDK_NGX_Result r = GuardedPopulate(f, params, &seh);
    ReportCall(L"snippet PopulateParameters_Impl", r, seh);
    wprintf(L"\n");
  }

  // --- 4. Initialise the snippet directly ----------------------------------
  //
  // The snippet's own GetAPIVersion reports 0x13 while the public SDK header is
  // 0x15, so the first thing worth ruling out is a version mismatch. Both init
  // entry points are tried at each version: Init_Ext takes a parameter block,
  // plain Init takes a FeatureCommonInfo, and there is no reason to assume the
  // snippet honours the same one the core does.
  wprintf(L"--- snippet init sweep ---\n");
  auto initExt = reinterpret_cast<PFN_InitExt>(find("NVSDK_NGX_D3D12_Init_Ext"));
  auto initPlain = reinterpret_cast<PFN_Init>(find("NVSDK_NGX_D3D12_Init"));

  std::wstring snippetDir = snippetPath;
  const size_t slash = snippetDir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) snippetDir.resize(slash);
  const wchar_t* searchPaths[1] = {snippetDir.c_str()};
  NVSDK_NGX_FeatureCommonInfo common{};
  common.PathListInfo.Path = searchPaths;
  common.PathListInfo.Length = 1;

  NVSDK_NGX_Result snippetInit = NVSDK_NGX_Result_Fail;
  NVSDK_NGX_Version acceptedVersion = sdkVersion;
  const unsigned int versions[] = {0x13, 0x14, 0x15, 0x12, 0x11, 0x16};
  for (unsigned int v : versions) {
    const auto ver = static_cast<NVSDK_NGX_Version>(v);
    wchar_t label[64];

    if (initExt) {
      const NVSDK_NGX_Result r = GuardedInitExt(initExt, device.Get(), ver, params, &seh);
      swprintf(label, 64, L"Init_Ext(0x%02X)", v);
      ReportCall(label, r, seh);
      if (r == NVSDK_NGX_Result_Success) { snippetInit = r; acceptedVersion = ver; break; }
    }
    if (initPlain) {
      const NVSDK_NGX_Result r =
          GuardedInitPlain(initPlain, device.Get(), &common, ver, &seh);
      swprintf(label, 64, L"Init(0x%02X, path list)", v);
      ReportCall(label, r, seh);
      if (r == NVSDK_NGX_Result_Success) { snippetInit = r; acceptedVersion = ver; break; }
    }
  }
  if (snippetInit == NVSDK_NGX_Result_Success) {
    wprintf(L"  accepted at SDK version 0x%02X\n", static_cast<unsigned>(acceptedVersion));
  }
  wprintf(L"\n");

  // --- 5. The question ------------------------------------------------------
  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&alloc))) ||
      FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(),
                                       nullptr, IID_PPV_ARGS(&cmdList)))) {
    wprintf(L"could not create a command list\n");
    return 4;
  }

  auto createFeature = reinterpret_cast<PFN_CreateFeature>(
      find("NVSDK_NGX_D3D12_CreateFeature"));
  auto releaseFeature = reinterpret_cast<PFN_ReleaseFeature>(
      find("NVSDK_NGX_D3D12_ReleaseFeature"));

  wprintf(L"--- snippet CreateFeature sweep (the core refused every one of these) ---\n");
  if (createFeature) {
    const int ids[] = {1, 11, 13, 14, 15, 16, 17, 18, 19, 20};
    for (int id : ids) {
      SetNeuralRenderingParams(params, 1920, 1080);
      NVSDK_NGX_Handle* handle = nullptr;
      const NVSDK_NGX_Result r =
          GuardedCreateFeature(createFeature, cmdList.Get(), id, params, &handle, &seh);
      wchar_t label[64];
      swprintf(label, 64, L"CreateFeature(%d)%s", id,
               id == 18 ? L"  <== DLSSNR candidate" : L"");
      ReportCall(label, r, seh);
      if (handle && releaseFeature) releaseFeature(handle);
    }
  } else {
    wprintf(L"  snippet exports no CreateFeature; nothing to ask.\n");
  }
  wprintf(L"\n");

  cmdList->Close();
  if (auto f = reinterpret_cast<PFN_Shutdown1>(find("NVSDK_NGX_D3D12_Shutdown1"))) {
    if (snippetInit == NVSDK_NGX_Result_Success) f(device.Get());
  }
  NVSDK_NGX_D3D12_DestroyParameters(params);
  NVSDK_NGX_D3D12_Shutdown1(device.Get());
  wprintf(L"done.\n");
  return 0;
}
