#pragma once
#include <windows.h>
#include <cstdint>
#include <optional>
#include <string>

namespace sidecar {

enum class GpuArch { Unsupported, Turing, Ampere, Ada, Blackwell };

struct GpuInfo {
  GpuArch arch = GpuArch::Unsupported;
  std::wstring name;
  LUID luid{};
  uint32_t vendorId = 0;
  uint32_t deviceId = 0;
};

// Pure. Device-id ranges are published per architecture; anything outside a
// known range is reported Unsupported rather than guessed, because a wrong
// guess selects the wrong neural runtime variant.
GpuArch ArchitectureFromDeviceId(uint32_t vendorId, uint32_t deviceId);

// Spec GPU matrix: 4K on Blackwell, 1440p on Ada, nothing else supported.
uint32_t DefaultInternalHeight(GpuArch arch);

struct RenderSize {
  uint32_t width = 0;
  uint32_t height = 0;
};

// The resolution the neural pass should work at, given what was captured.
//
// Above the architecture's ceiling the pass renders lower and DLSS upscales,
// which is the whole reason the ceiling differs per architecture -- neural
// rendering costs roughly 8 ms at 1440p on Ada, and running it at 4K there
// would put the frame budget somewhere it cannot come back from.
//
// At or below the ceiling the capture size is used unchanged, so the pass runs
// DLAA rather than upscaling something that was never downscaled. Aspect ratio
// is preserved and both axes are kept even, because odd dimensions upset
// chroma-subsampled and block-based stages elsewhere in the pipeline.
RenderSize InternalRenderSize(uint32_t captureWidth, uint32_t captureHeight,
                              GpuArch arch);

const char* ToString(GpuArch arch);

// Enumerates adapters and returns the first hardware NVIDIA adapter.
std::optional<GpuInfo> DetectPrimaryGpu();

}  // namespace sidecar
