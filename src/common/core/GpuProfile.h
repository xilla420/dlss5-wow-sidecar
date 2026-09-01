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

// How much video memory this process is allowed, and how much it is using.
//
// Worth reporting because running past the budget is the single worst thing
// that can happen to this pipeline, and it does not look like what it is: the
// driver evicts resources to system memory, every frame then waits on PCIe
// transfers, and the result is enormous frame times with the GPU almost idle.
// That reads exactly like a slow neural pass and is nothing of the sort.
//
// Measured against the sidecar's own adapter. Windows reports a per-process
// budget rather than the card's total, and the budget shrinks as other
// applications take memory -- which is the case worth catching, because
// nothing this program does causes it and no setting here fixes it.
struct VideoMemory {
  uint64_t budgetBytes = 0;
  uint64_t usedBytes = 0;
  // Video memory this process has been pushed out into system RAM. Any value
  // here at all means the card is full and the driver is paging over PCIe,
  // which is the condition that destroys frame times. It is a better alarm
  // than the budget: Windows reports a generous per-process budget right up
  // until contention actually bites, so usage rarely looks close to it even
  // when the card is nearly full.
  uint64_t spilledBytes = 0;
  bool OverBudget() const { return budgetBytes > 0 && usedBytes > budgetBytes; }
  bool Spilling() const { return spilledBytes > 0; }
};
std::optional<VideoMemory> QueryVideoMemory(LUID adapter);

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
