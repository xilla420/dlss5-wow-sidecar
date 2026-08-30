// Throwaway spike for the M3 plan, Task 3. NOT shipped, not linked into
// sidecar_common, and deleted or archived once its questions are answered.
//
// It answers, on real hardware rather than by assumption:
//
//   1. Which NGX feature identifiers does this driver actually report as
//      supported? The public SDK's enum stops at RayReconstruction = 13 and
//      then has Reserved14..18. DLSS 5 Neural Rendering is not named anywhere
//      in it, so if NR is reachable at all it is one of those reserved slots.
//
//   2. Does NGX see nvngx_dlssnr.dll when it is pointed at a directory
//      containing it, and what does it say if it cannot?
//
// Usage:
//   spike_ngx.exe [path-to-directory-containing-nvngx-dlls]
//
// The path is optional. Without it, NGX searches only the application folder,
// which is the honest "nothing supplied" baseline.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>

using Microsoft::WRL::ComPtr;

namespace {

// The application id NVIDIA hands out per title. 0x0 is the documented value
// for "no registered id", which is what a spike has.
constexpr unsigned long long kApplicationId = 0x0;

const char* ResultName(NVSDK_NGX_Result r) {
  switch (r) {
    case NVSDK_NGX_Result_Success:                       return "Success";
    case NVSDK_NGX_Result_Fail:                          return "Fail";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported:      return "FeatureNotSupported";
    case NVSDK_NGX_Result_FAIL_PlatformError:            return "PlatformError";
    case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists:     return "FeatureAlreadyExists";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound:          return "FeatureNotFound";
    case NVSDK_NGX_Result_FAIL_InvalidParameter:         return "InvalidParameter";
    case NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall:    return "ScratchBufferTooSmall";
    case NVSDK_NGX_Result_FAIL_NotInitialized:           return "NotInitialized";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat:   return "UnsupportedInputFormat";
    case NVSDK_NGX_Result_FAIL_RWFlagMissing:            return "RWFlagMissing";
    case NVSDK_NGX_Result_FAIL_MissingInput:             return "MissingInput";
    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature:return "UnableToInitializeFeature";
    case NVSDK_NGX_Result_FAIL_OutOfDate:                return "OutOfDate";
    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory:           return "OutOfGPUMemory";
    case NVSDK_NGX_Result_FAIL_UnsupportedFormat:        return "UnsupportedFormat";
    case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath: return "UnableToWriteToAppDataPath";
    case NVSDK_NGX_Result_FAIL_UnsupportedParameter:     return "UnsupportedParameter";
    case NVSDK_NGX_Result_FAIL_Denied:                   return "Denied";
    case NVSDK_NGX_Result_FAIL_NotImplemented:           return "NotImplemented";
    default:                                             return "<unknown>";
  }
}

std::string SupportBits(NVSDK_NGX_Feature_Support_Result r) {
  if (r == NVSDK_NGX_FeatureSupportResult_Supported) return "Supported";
  std::string out;
  const unsigned int bits = static_cast<unsigned int>(r);
  auto add = [&out](const char* name) {
    if (!out.empty()) out += "|";
    out += name;
  };
  if (bits & NVSDK_NGX_FeatureSupportResult_CheckNotPresent) add("CheckNotPresent");
  if (bits & NVSDK_NGX_FeatureSupportResult_DriverVersionUnsupported) add("DriverVersionUnsupported");
  if (bits & NVSDK_NGX_FeatureSupportResult_AdapterUnsupported) add("AdapterUnsupported");
  if (bits & NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported) add("OSVersionBelowMinimum");
  if (bits & NVSDK_NGX_FeatureSupportResult_NotImplemented) add("NotImplemented");
  if (out.empty()) out = "<no bits set>";
  return out;
}

const char* FeatureName(int id) {
  switch (id) {
    case 1:  return "SuperSampling (nvngx_dlss)";
    case 2:  return "InPainting";
    case 3:  return "ImageSuperResolution";
    case 4:  return "SlowMotion";
    case 5:  return "VideoSuperResolution";
    case 9:  return "ImageSignalProcessing";
    case 10: return "DeepResolve";
    case 11: return "FrameGeneration (nvngx_dlssg)";
    case 12: return "DeepDVC";
    case 13: return "RayReconstruction (nvngx_dlssd)";
    case 14: return "Reserved14  <-- NR candidate";
    case 15: return "Reserved15  <-- NR candidate";
    case 16: return "Reserved16  <-- NR candidate";
    case 17: return "Reserved17  <-- NR candidate";
    case 18: return "Reserved18  <-- NR candidate";
    default: return "Reserved/unnamed";
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

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::wstring searchPath;
  if (argc > 1) searchPath = argv[1];

  // Optional: report a different SDK API version than the public header's
  // 0x15. Everything at feature id 18 and above comes back OutOfDate, which
  // reads like a version gate rather than a per-feature verdict; this is how
  // that gets tested rather than assumed.
  NVSDK_NGX_Version sdkVersion = NVSDK_NGX_Version_API;
  if (argc > 2) {
    sdkVersion = static_cast<NVSDK_NGX_Version>(wcstoul(argv[2], nullptr, 0));
  }

  wprintf(L"=== NGX spike (M3 Task 3) ===\n");
  wprintf(L"feature dll search path: %s\n",
          searchPath.empty() ? L"<application folder only>" : searchPath.c_str());
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

  // Report which feature dlls are actually present where we are pointing NGX,
  // so the availability results below can be read against reality.
  if (!searchPath.empty()) {
    wprintf(L"--- nvngx_* dlls in that directory ---\n");
    WIN32_FIND_DATAW found{};
    const std::wstring pattern = searchPath + L"\\nvngx_*.dll";
    HANDLE h = FindFirstFileW(pattern.c_str(), &found);
    if (h == INVALID_HANDLE_VALUE) {
      wprintf(L"  (none)\n");
    } else {
      do {
        wprintf(L"  %s\n", found.cFileName);
      } while (FindNextFileW(h, &found));
      FindClose(h);
    }
    wprintf(L"\n");
  }

  // GetFeatureRequirements needs no device and no prior Init, which makes it
  // the cleanest way to ask "is this feature id reachable at all".
  const wchar_t* paths[1] = {searchPath.c_str()};
  NVSDK_NGX_FeatureCommonInfo common{};
  if (!searchPath.empty()) {
    common.PathListInfo.Path = paths;
    common.PathListInfo.Length = 1;
  }

  wprintf(L"--- per-feature availability (NVSDK_NGX_D3D12_GetFeatureRequirements) ---\n");
  for (int id = 1; id <= 18; ++id) {
    NVSDK_NGX_FeatureDiscoveryInfo info{};
    info.SDKVersion = sdkVersion;
    info.FeatureID = static_cast<NVSDK_NGX_Feature>(id);
    info.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Application_Id;
    info.Identifier.v.ApplicationId = kApplicationId;
    info.ApplicationDataPath = L".";
    info.FeatureInfo = searchPath.empty() ? nullptr : &common;

    NVSDK_NGX_FeatureRequirement req{};
    const NVSDK_NGX_Result r =
        NVSDK_NGX_D3D12_GetFeatureRequirements(adapter.Get(), &info, &req);

    // Only report the support bits when the call actually succeeded. The
    // struct is zero-initialised and zero means "Supported", so printing it
    // after a failure claims support that was never established.
    if (r == NVSDK_NGX_Result_Success) {
      wprintf(L"  [%2d] %-34hs result=%-22hs support=%hs\n", id, FeatureName(id),
              ResultName(r), SupportBits(req.FeatureSupported).c_str());
    } else {
      wprintf(L"  [%2d] %-34hs result=%-22hs support=<not reported>\n", id,
              FeatureName(id), ResultName(r));
    }
  }
  wprintf(L"\n");

  // Now bring up a real device and session, which is what actually loads the
  // feature dlls and is where a missing or mismatched runtime shows up.
  ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
    wprintf(L"D3D12CreateDevice failed\n");
    return 1;
  }

  const NVSDK_NGX_Result init = NVSDK_NGX_D3D12_Init(
      kApplicationId, L".", device.Get(),
      searchPath.empty() ? nullptr : &common, sdkVersion);
  wprintf(L"--- NVSDK_NGX_D3D12_Init -> %hs ---\n", ResultName(init));
  if (init != NVSDK_NGX_Result_Success) {
    wprintf(L"\nNGX would not initialise, so nothing below could be trusted.\n");
    return 2;
  }

  NVSDK_NGX_Parameter* caps = nullptr;
  const NVSDK_NGX_Result capResult = NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
  wprintf(L"NVSDK_NGX_D3D12_GetCapabilityParameters -> %hs\n\n", ResultName(capResult));

  if (capResult == NVSDK_NGX_Result_Success && caps) {
    // The documented availability keys. Anything DLSS 5 NR uses is not among
    // them, which is exactly the gap this spike is measuring.
    const char* keys[] = {
        NVSDK_NGX_Parameter_SuperSampling_Available,
        NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,
        NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
    };
    wprintf(L"--- capability parameters ---\n");
    for (const char* key : keys) {
      int value = -1;
      const NVSDK_NGX_Result r = caps->Get(key, &value);
      // FeatureInitResult carries an NVSDK_NGX_Result, so decode it rather
      // than printing a large negative number nobody can read.
      const unsigned int raw = static_cast<unsigned int>(value);
      const bool looksLikeResult = (raw & 0xFFF00000u) == 0xBAD00000u;
      wprintf(L"  %-52hs get=%-14hs value=%d", key, ResultName(r), value);
      if (looksLikeResult) {
        wprintf(L"  (0x%08X = %hs)", raw,
                ResultName(static_cast<NVSDK_NGX_Result>(raw)));
      }
      wprintf(L"\n");
    }
    wprintf(L"\n");
  }

  // The authoritative probe.
  //
  // GetFeatureRequirements above reports NotImplemented even for
  // RayReconstruction, which is a real shipping feature, so "no discovery
  // check" tells us nothing about whether a feature exists. CreateFeature does
  // tell us, through *which* way it fails:
  //
  //   FeatureNotFound      -> no such feature, or its dll was not located
  //   InvalidParameter,
  //   MissingInput,
  //   UnsupportedParameter -> the feature EXISTS and rejected our parameters,
  //                           which is what identifies it
  //
  // The parameters below are the generic DLSS-shaped set. They are deliberately
  // not right for anything in particular: the point is to get far enough into
  // a real feature to be told what it actually wants.
  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_PPV_ARGS(&alloc))) ||
      FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(),
                                       nullptr, IID_PPV_ARGS(&cmdList)))) {
    wprintf(L"could not create a command list for CreateFeature probing\n");
    NVSDK_NGX_D3D12_Shutdown1(device.Get());
    return 3;
  }

  wprintf(L"--- CreateFeature probe (how each id fails is the signal) ---\n");
  for (int id = 1; id <= 32; ++id) {
    NVSDK_NGX_Parameter* params = nullptr;
    if (NVSDK_NGX_D3D12_AllocateParameters(&params) != NVSDK_NGX_Result_Success || !params) {
      wprintf(L"  [%2d] could not allocate parameters\n", id);
      continue;
    }
    params->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
    params->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
    params->Set(NVSDK_NGX_Parameter_Width, 1920u);
    params->Set(NVSDK_NGX_Parameter_Height, 1080u);
    params->Set(NVSDK_NGX_Parameter_OutWidth, 1920u);
    params->Set(NVSDK_NGX_Parameter_OutHeight, 1080u);
    params->Set(NVSDK_NGX_Parameter_PerfQualityValue,
                static_cast<int>(NVSDK_NGX_PerfQuality_Value_MaxQuality));

    NVSDK_NGX_Handle* handle = nullptr;
    const NVSDK_NGX_Result r = NVSDK_NGX_D3D12_CreateFeature(
        cmdList.Get(), static_cast<NVSDK_NGX_Feature>(id), params, &handle);

    const bool interesting = (r != NVSDK_NGX_Result_FAIL_FeatureNotFound &&
                              r != NVSDK_NGX_Result_FAIL_FeatureNotSupported);
    wprintf(L"  [%2d] %-34hs -> %-26hs%hs\n", id, FeatureName(id), ResultName(r),
            interesting ? "  <== reached a real feature" : "");

    if (handle) NVSDK_NGX_D3D12_ReleaseFeature(handle);
    NVSDK_NGX_D3D12_DestroyParameters(params);
  }
  cmdList->Close();
  wprintf(L"\n");

  NVSDK_NGX_D3D12_Shutdown1(device.Get());
  wprintf(L"done.\n");
  return 0;
}
