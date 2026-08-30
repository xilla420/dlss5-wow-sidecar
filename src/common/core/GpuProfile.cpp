#include "core/GpuProfile.h"

#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace sidecar {
namespace {

constexpr uint32_t kNvidiaVendorId = 0x10DE;

struct Range {
  uint32_t lo, hi;
  GpuArch arch;
};

// Consumer desktop ranges. Deliberately narrow: an unknown id must fall
// through to Unsupported so the manager reports it honestly.
constexpr Range kRanges[] = {
    {0x1E00, 0x1FFF, GpuArch::Turing},
    {0x2180, 0x21FF, GpuArch::Turing},
    {0x2200, 0x22FF, GpuArch::Ampere},
    {0x2400, 0x24FF, GpuArch::Ampere},
    {0x2500, 0x25FF, GpuArch::Ampere},
    {0x2600, 0x26FF, GpuArch::Ada},
    {0x2700, 0x27FF, GpuArch::Ada},
    {0x2780, 0x28FF, GpuArch::Ada},
    {0x2B00, 0x2BFF, GpuArch::Blackwell},
    {0x2C00, 0x2DFF, GpuArch::Blackwell},
};

}  // namespace

GpuArch ArchitectureFromDeviceId(uint32_t vendorId, uint32_t deviceId) {
  if (vendorId != kNvidiaVendorId) return GpuArch::Unsupported;
  for (const auto& r : kRanges) {
    if (deviceId >= r.lo && deviceId <= r.hi) return r.arch;
  }
  return GpuArch::Unsupported;
}

uint32_t DefaultInternalHeight(GpuArch arch) {
  switch (arch) {
    case GpuArch::Blackwell: return 2160;
    case GpuArch::Ada:       return 1440;
    default:                 return 0;
  }
}

const char* ToString(GpuArch arch) {
  switch (arch) {
    case GpuArch::Blackwell:   return "Blackwell (RTX 50)";
    case GpuArch::Ada:         return "Ada (RTX 40)";
    case GpuArch::Ampere:      return "Ampere (RTX 30)";
    case GpuArch::Turing:      return "Turing (RTX 20)";
    default:                   return "Unsupported";
  }
}

std::optional<GpuInfo> DetectPrimaryGpu() {
  ComPtr<IDXGIFactory6> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return std::nullopt;

  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0;
       factory->EnumAdapterByGpuPreference(
           i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
           IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
       ++i) {
    DXGI_ADAPTER_DESC3 desc{};
    ComPtr<IDXGIAdapter4> adapter4;
    if (FAILED(adapter.As(&adapter4)) || FAILED(adapter4->GetDesc3(&desc))) continue;
    if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) continue;
    if (desc.VendorId != kNvidiaVendorId) continue;

    GpuInfo info;
    info.vendorId = desc.VendorId;
    info.deviceId = desc.DeviceId;
    info.arch = ArchitectureFromDeviceId(desc.VendorId, desc.DeviceId);
    info.name = desc.Description;
    info.luid = desc.AdapterLuid;
    return info;
  }
  return std::nullopt;
}

}  // namespace sidecar
