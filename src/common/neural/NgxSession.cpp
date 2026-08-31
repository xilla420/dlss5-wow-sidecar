#include "neural/NgxSession.h"

#include <windows.h>

#include "core/Log.h"

#if SIDECAR_HAVE_NGX
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_helpers.h>
#endif

namespace sidecar {

#if SIDECAR_HAVE_NGX

namespace {

// This project is not a registered NVIDIA title, so it identifies itself by
// project id rather than application id. NGX rejects DLSS outright for
// application id 0 on some drivers; the project-id path is what NVIDIA points
// unregistered callers at.
constexpr const char* kProjectId = "a0f57b54-1daf-4934-90ae-c4035c19df04";

NVSDK_NGX_Parameter* Params(void* p) {
  return static_cast<NVSDK_NGX_Parameter*>(p);
}

NVSDK_NGX_Handle* Handle(void* p) {
  return static_cast<NVSDK_NGX_Handle*>(p);
}

const char* ResultName(NVSDK_NGX_Result r) {
  switch (r) {
    case NVSDK_NGX_Result_Success:                        return "Success";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported:       return "FeatureNotSupported";
    case NVSDK_NGX_Result_FAIL_PlatformError:             return "PlatformError";
    case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists:      return "FeatureAlreadyExists";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound:           return "FeatureNotFound";
    case NVSDK_NGX_Result_FAIL_InvalidParameter:          return "InvalidParameter";
    case NVSDK_NGX_Result_FAIL_NotInitialized:            return "NotInitialized";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat:    return "UnsupportedInputFormat";
    case NVSDK_NGX_Result_FAIL_RWFlagMissing:             return "RWFlagMissing";
    case NVSDK_NGX_Result_FAIL_MissingInput:              return "MissingInput";
    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature: return "UnableToInitializeFeature";
    case NVSDK_NGX_Result_FAIL_OutOfDate:                 return "OutOfDate";
    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory:            return "OutOfGPUMemory";
    case NVSDK_NGX_Result_FAIL_UnsupportedFormat:         return "UnsupportedFormat";
    case NVSDK_NGX_Result_FAIL_UnsupportedParameter:      return "UnsupportedParameter";
    case NVSDK_NGX_Result_FAIL_Denied:                    return "Denied";
    case NVSDK_NGX_Result_FAIL_NotImplemented:            return "NotImplemented";
    default:                                              return "Fail";
  }
}

}  // namespace

std::unique_ptr<NgxSession> NgxSession::Create(ID3D12Device* device,
                                               const std::filesystem::path& runtimeDir) {
  if (!device) return nullptr;

  std::unique_ptr<NgxSession> s(new NgxSession());
  s->device_ = device;

  // Absolute, always. See the header: a relative path finds nothing and the
  // symptom is indistinguishable from an unsupported driver.
  std::error_code ec;
  std::filesystem::path absoluteDir = std::filesystem::absolute(runtimeDir, ec);
  if (ec) absoluteDir = runtimeDir;
  const std::wstring dir = absoluteDir.wstring();

  const wchar_t* paths[1] = {dir.c_str()};
  NVSDK_NGX_FeatureCommonInfo common{};
  common.PathListInfo.Path = paths;
  common.PathListInfo.Length = 1;

  const NVSDK_NGX_Result init = NVSDK_NGX_D3D12_Init_with_ProjectID(
      kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", dir.c_str(), device,
      &common, NVSDK_NGX_Version_API);
  if (init != NVSDK_NGX_Result_Success) {
    s->unavailableReason_ =
        std::string("NGX init failed: ") + ResultName(init) +
        ". Runtimes were looked for in " + absoluteDir.string() + ".";
    GlobalLog().Warn(s->unavailableReason_);
    return s;  // Available() == false, but the reason survives.
  }
  s->initialised_ = true;

  NVSDK_NGX_Parameter* params = nullptr;
  const NVSDK_NGX_Result alloc = NVSDK_NGX_D3D12_AllocateParameters(&params);
  if (alloc != NVSDK_NGX_Result_Success || !params) {
    s->unavailableReason_ =
        std::string("NGX parameter allocation failed: ") + ResultName(alloc);
    GlobalLog().Warn(s->unavailableReason_);
    return s;
  }
  s->parameters_ = params;

  // Ask the driver whether DLSS is usable before trying to create anything.
  // The capability block reports a reason; a failed CreateFeature does not.
  NVSDK_NGX_Parameter* caps = nullptr;
  if (NVSDK_NGX_D3D12_GetCapabilityParameters(&caps) == NVSDK_NGX_Result_Success &&
      caps) {
    int available = 0;
    caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    s->dlssSupported_ = available != 0;
    if (!s->dlssSupported_) {
      int initResult = 0;
      int needsDriver = 0;
      caps->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initResult);
      caps->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
      s->unavailableReason_ =
          std::string("DLSS reports unavailable: ") +
          ResultName(static_cast<NVSDK_NGX_Result>(initResult)) +
          (needsDriver ? ". The driver is too old." :
                         ". Check nvngx_dlss.dll is beside the sidecar.");
      GlobalLog().Warn(s->unavailableReason_);
    }
  } else {
    s->unavailableReason_ = "NGX capability parameters unavailable.";
    GlobalLog().Warn(s->unavailableReason_);
  }

  return s;
}

NgxSession::~NgxSession() {
  ReleaseFeature();
  if (parameters_) {
    NVSDK_NGX_D3D12_DestroyParameters(Params(parameters_));
    parameters_ = nullptr;
  }
  if (initialised_ && device_) {
    NVSDK_NGX_D3D12_Shutdown1(device_);
    initialised_ = false;
  }
}

bool NgxSession::Available() const { return initialised_ && parameters_ != nullptr; }

void NgxSession::ReleaseFeature() {
  if (handle_) {
    NVSDK_NGX_D3D12_ReleaseFeature(Handle(handle_));
    handle_ = nullptr;
  }
}

bool NgxSession::CreateDlssFeature(ID3D12GraphicsCommandList* cl,
                                   const DlssFeatureDesc& desc) {
  if (!cl || !Available() || !dlssSupported_) return false;
  if (desc.renderWidth == 0 || desc.renderHeight == 0 ||
      desc.outputWidth == 0 || desc.outputHeight == 0) {
    return false;
  }

  // One feature at a time. Creating a second over the top leaks the first, and
  // NGX answers FeatureAlreadyExists rather than replacing it.
  ReleaseFeature();

  NVSDK_NGX_DLSS_Create_Params create{};
  create.Feature.InWidth = desc.renderWidth;
  create.Feature.InHeight = desc.renderHeight;
  create.Feature.InTargetWidth = desc.outputWidth;
  create.Feature.InTargetHeight = desc.outputHeight;
  // A 1:1 ratio is DLAA and wants its own quality value; asking for MaxQuality
  // at 1:1 is refused. The Task 4 spike measured both.
  const bool upscaling = desc.renderWidth != desc.outputWidth ||
                         desc.renderHeight != desc.outputHeight;
  create.Feature.InPerfQualityValue = upscaling
                                          ? NVSDK_NGX_PerfQuality_Value_MaxQuality
                                          : NVSDK_NGX_PerfQuality_Value_DLAA;
  int flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
  if (desc.hdr) flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
  create.InFeatureCreateFlags = flags;

  NVSDK_NGX_Handle* handle = nullptr;
  const NVSDK_NGX_Result r =
      NGX_D3D12_CREATE_DLSS_EXT(cl, 1, 1, &handle, Params(parameters_), &create);
  if (r != NVSDK_NGX_Result_Success || !handle) {
    GlobalLog().Error(std::string("DLSS feature creation failed: ") + ResultName(r));
    return false;
  }
  handle_ = handle;
  return true;
}

bool NgxSession::Evaluate(ID3D12GraphicsCommandList* cl, const DlssEvalDesc& desc) {
  if (!cl || !handle_ || !Available()) return false;
  // Every one of these is required. NGX would fault rather than refuse if handed
  // a null, so the check belongs here and not in a comment.
  if (!desc.color || !desc.motion || !desc.depth || !desc.output) return false;

  NVSDK_NGX_D3D12_DLSS_Eval_Params eval{};
  eval.Feature.pInColor = desc.color;
  eval.Feature.pInOutput = desc.output;
  eval.Feature.InSharpness = 0.0f;
  eval.pInDepth = desc.depth;
  eval.pInMotionVectors = desc.motion;
  eval.InJitterOffsetX = desc.jitterX;
  eval.InJitterOffsetY = desc.jitterY;
  eval.InMVScaleX = desc.motionScaleX;
  eval.InMVScaleY = desc.motionScaleY;
  eval.InReset = desc.reset ? 1 : 0;

  const NVSDK_NGX_Result r =
      NGX_D3D12_EVALUATE_DLSS_EXT(cl, Handle(handle_), Params(parameters_), &eval);
  if (r != NVSDK_NGX_Result_Success) {
    GlobalLog().Error(std::string("DLSS evaluate failed: ") + ResultName(r));
    return false;
  }
  return true;
}

#else  // !SIDECAR_HAVE_NGX

std::unique_ptr<NgxSession> NgxSession::Create(ID3D12Device* device,
                                               const std::filesystem::path& runtimeDir) {
  (void)runtimeDir;
  // A null device is a caller bug in either build, so it must fail the same way
  // in both. Everything else returns a session rather than null, so callers can
  // report the reason instead of a bare "unavailable".
  if (!device) return nullptr;

  std::unique_ptr<NgxSession> s(new NgxSession());
  s->unavailableReason_ =
      "Built without the NVIDIA DLSS SDK, so no NGX session is possible. "
      "Configure with -DDLSS_SDK_DIR= to enable it.";
  return s;
}

NgxSession::~NgxSession() = default;
bool NgxSession::Available() const { return false; }
void NgxSession::ReleaseFeature() {}

bool NgxSession::CreateDlssFeature(ID3D12GraphicsCommandList* cl,
                                   const DlssFeatureDesc& desc) {
  (void)cl;
  (void)desc;
  return false;
}

bool NgxSession::Evaluate(ID3D12GraphicsCommandList* cl, const DlssEvalDesc& desc) {
  (void)cl;
  (void)desc;
  return false;
}

#endif  // SIDECAR_HAVE_NGX

}  // namespace sidecar
