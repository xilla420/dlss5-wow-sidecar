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

RenderSize InternalRenderSize(uint32_t captureWidth, uint32_t captureHeight,
                              GpuArch arch) {
  if (captureWidth == 0 || captureHeight == 0) return RenderSize{};

  const uint32_t ceilingHeight = DefaultInternalHeight(arch);
  // No ceiling means an architecture outside the matrix. The pass will refuse
  // to run there anyway; scaling its resolution would be inventing a policy for
  // a case that does not exist.
  if (ceilingHeight == 0 || captureHeight <= ceilingHeight) {
    return RenderSize{captureWidth, captureHeight};
  }

  // Round the width to the nearest even pixel rather than truncating, so a
  // 21:9 capture does not drift a pixel off its aspect ratio.
  const uint64_t scaledWidth =
      (static_cast<uint64_t>(captureWidth) * ceilingHeight + captureHeight / 2) /
      captureHeight;

  RenderSize out;
  out.width = static_cast<uint32_t>(scaledWidth) & ~1u;
  out.height = ceilingHeight & ~1u;
  if (out.width == 0) out.width = 2;
  return out;
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

std::optional<VideoMemory> QueryVideoMemory(LUID adapter) {
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return std::nullopt;

  ComPtr<IDXGIAdapter1> adapter1;
  if (FAILED(factory->EnumAdapterByLuid(adapter, IID_PPV_ARGS(&adapter1)))) {
    return std::nullopt;
  }
  ComPtr<IDXGIAdapter3> adapter3;
  if (FAILED(adapter1.As(&adapter3))) return std::nullopt;

  // LOCAL is the memory on the card; NON_LOCAL is system memory the driver
  // falls back to when the card is full. Both are worth having: the first is
  // what we are using, the second is whether we have already been evicted.
  DXGI_QUERY_VIDEO_MEMORY_INFO local{};
  if (FAILED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local))) {
    return std::nullopt;
  }
  DXGI_QUERY_VIDEO_MEMORY_INFO nonLocal{};
  // A failure here is not fatal; it costs the eviction alarm, not the reading.
  adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal);

  VideoMemory out;
  out.budgetBytes = local.Budget;
  out.usedBytes = local.CurrentUsage;
  out.spilledBytes = nonLocal.CurrentUsage;
  return out;
}

}  // namespace sidecar
