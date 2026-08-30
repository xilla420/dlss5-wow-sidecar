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

const char* ToString(GpuArch arch);

// Enumerates adapters and returns the first hardware NVIDIA adapter.
std::optional<GpuInfo> DetectPrimaryGpu();

}  // namespace sidecar
