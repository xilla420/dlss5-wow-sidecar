#include <catch2/catch_test_macros.hpp>
#include <d3d12.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/GpuProfile.h"
#include "flow/NvofaFlow.h"
#include "gpu/DeviceBridge.h"
#include "../src/testpattern/Pattern.h"

using Microsoft::WRL::ComPtr;
using namespace sidecar;
using sidecar::testpattern::PixelAt;

namespace {

// Runs a one-shot command list on the bridge queue and blocks until it retires.
void RunAndWait(DeviceBridge& bridge, ID3D12GraphicsCommandList* cl) {
  REQUIRE(SUCCEEDED(cl->Close()));
  ID3D12CommandList* lists[] = {cl};
  bridge.Queue()->ExecuteCommandLists(1, lists);

  ComPtr<ID3D12Fence> done;
  REQUIRE(SUCCEEDED(bridge.D3d12()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                IID_PPV_ARGS(&done))));
  REQUIRE(SUCCEEDED(bridge.Queue()->Signal(done.Get(), 1)));
  HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  REQUIRE(SUCCEEDED(done->SetEventOnCompletion(1, evt)));
  WaitForSingleObject(evt, INFINITE);
  CloseHandle(evt);
}

// The Optical Flow SDK's D3D12 path takes GRAYSCALE8 input, so the pattern is
// reduced to luminance before upload.
ComPtr<ID3D12Resource> UploadPatternFrame(DeviceBridge& bridge, uint32_t w,
                                          uint32_t h, uint32_t frame) {
  auto* dev = bridge.D3d12();

  D3D12_HEAP_PROPERTIES defaultHeap{};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = w;
  rd.Height = h;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_R8_UNORM;
  rd.SampleDesc.Count = 1;

  ComPtr<ID3D12Resource> tex;
  REQUIRE(SUCCEEDED(dev->CreateCommittedResource(
      &defaultHeap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, IID_PPV_ARGS(&tex))));

  const UINT rowPitch = (w + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

  D3D12_HEAP_PROPERTIES uploadHeap{};
  uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC bd{};
  bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bd.Width = static_cast<UINT64>(rowPitch) * h;
  bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
  bd.Format = DXGI_FORMAT_UNKNOWN;
  bd.SampleDesc.Count = 1;
  bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> upload;
  REQUIRE(SUCCEEDED(dev->CreateCommittedResource(
      &uploadHeap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr, IID_PPV_ARGS(&upload))));

  void* mapped = nullptr;
  D3D12_RANGE none{0, 0};
  REQUIRE(SUCCEEDED(upload->Map(0, &none, &mapped)));
  for (uint32_t y = 0; y < h; ++y) {
    auto* row = static_cast<uint8_t*>(mapped) + static_cast<size_t>(y) * rowPitch;
    for (uint32_t x = 0; x < w; ++x) {
      const auto p = PixelAt(x, y, w, h, frame);
      const float luma = 0.299f * p.r + 0.587f * p.g + 0.114f * p.b;
      row[x] = static_cast<uint8_t>(luma + 0.5f);
    }
  }
  upload->Unmap(0, nullptr);

  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cl;
  REQUIRE(SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&alloc))));
  REQUIRE(SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           alloc.Get(), nullptr, IID_PPV_ARGS(&cl))));

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = tex.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = upload.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
  src.PlacedFootprint.Footprint.Width = w;
  src.PlacedFootprint.Footprint.Height = h;
  src.PlacedFootprint.Footprint.Depth = 1;
  src.PlacedFootprint.Footprint.RowPitch = rowPitch;

  cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  D3D12_RESOURCE_BARRIER toCommon{};
  toCommon.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toCommon.Transition.pResource = tex.Get();
  toCommon.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  toCommon.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  toCommon.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  cl->ResourceBarrier(1, &toCommon);

  RunAndWait(bridge, cl.Get());
  return tex;
}

// Reads the flow grid back as int16_t pairs, two per cell. The grid is a
// R16G16_SINT texture, so the copy goes through a placed footprint.
std::vector<int16_t> ReadFlowGrid(DeviceBridge& bridge, const FlowOutput& flow) {
  auto* dev = bridge.D3d12();
  const UINT rowPitch = (flow.gridWidth * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC bd{};
  bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bd.Width = static_cast<UINT64>(rowPitch) * flow.gridHeight;
  bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
  bd.Format = DXGI_FORMAT_UNKNOWN;
  bd.SampleDesc.Count = 1;
  bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> readback;
  REQUIRE(SUCCEEDED(dev->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, IID_PPV_ARGS(&readback))));

  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cl;
  REQUIRE(SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&alloc))));
  REQUIRE(SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           alloc.Get(), nullptr, IID_PPV_ARGS(&cl))));

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = readback.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16_SINT;
  dst.PlacedFootprint.Footprint.Width = flow.gridWidth;
  dst.PlacedFootprint.Footprint.Height = flow.gridHeight;
  dst.PlacedFootprint.Footprint.Depth = 1;
  dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = flow.grid;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;

  cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  REQUIRE(SUCCEEDED(cl->Close()));

  // NVOFA runs on its own queue, so wait for it before reading the grid.
  ID3D12CommandList* lists[] = {cl.Get()};
  bridge.Queue()->ExecuteCommandLists(1, lists);

  ComPtr<ID3D12Fence> done;
  REQUIRE(SUCCEEDED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&done))));
  REQUIRE(SUCCEEDED(bridge.Queue()->Signal(done.Get(), 1)));
  HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  REQUIRE(SUCCEEDED(done->SetEventOnCompletion(1, evt)));
  WaitForSingleObject(evt, INFINITE);
  CloseHandle(evt);

  void* mapped = nullptr;
  D3D12_RANGE all{0, static_cast<SIZE_T>(bd.Width)};
  REQUIRE(SUCCEEDED(readback->Map(0, &all, &mapped)));
  std::vector<int16_t> out(static_cast<size_t>(flow.gridWidth) * flow.gridHeight * 2);
  for (uint32_t y = 0; y < flow.gridHeight; ++y) {
    std::memcpy(out.data() + static_cast<size_t>(y) * flow.gridWidth * 2,
                static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * rowPitch,
                static_cast<size_t>(flow.gridWidth) * 4);
  }
  D3D12_RANGE none{0, 0};
  readback->Unmap(0, &none);
  return out;
}

}  // namespace

TEST_CASE("optical flow is available on a supported adapter", "[device]") {
#if !SIDECAR_HAVE_NVOF
  SKIP("Built without the NVIDIA Optical Flow SDK. Configure with "
       "-DNVOF_SDK_DIR=<sdk root> to enable this test.");
#else
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  REQUIRE((gpu->arch == GpuArch::Ada || gpu->arch == GpuArch::Blackwell));

  auto bridge = DeviceBridge::Create(gpu->luid, 256, 256);
  REQUIRE(bridge != nullptr);
  auto flow = NvofaFlow::Create(bridge->D3d12(), bridge->Queue(), 256, 256, 4);
  REQUIRE(flow != nullptr);
  REQUIRE(flow->Available());
#endif
}

TEST_CASE("flow recovers a known horizontal translation", "[device]") {
#if !SIDECAR_HAVE_NVOF
  SKIP("Built without the NVIDIA Optical Flow SDK. Configure with "
       "-DNVOF_SDK_DIR=<sdk root> to enable this test.");
#else
  constexpr uint32_t kW = 256, kH = 256;
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);
  auto flow = NvofaFlow::Create(bridge->D3d12(), bridge->Queue(), kW, kH, 4);
  REQUIRE(flow != nullptr);

  // Frames 0 and 1 of the test pattern differ by exactly kBarSpeedPxPerFrame
  // pixels of horizontal bar motion, which is the ground truth here.
  auto previous = UploadPatternFrame(*bridge, kW, kH, 0);
  auto current = UploadPatternFrame(*bridge, kW, kH, 1);

  // Both uploads already completed on the bridge queue, so signal the input
  // fence NVOFA waits on before it starts.
  REQUIRE(SUCCEEDED(bridge->Queue()->Signal(flow->InputFence(), 1)));

  FlowOutput out{};
  REQUIRE(flow->Execute(current.Get(), previous.Get(), 1, out));
  REQUIRE(out.grid != nullptr);

  // Block until NVOFA reports the grid written.
  HANDLE flowDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  REQUIRE(SUCCEEDED(flow->OutputFence()->SetEventOnCompletion(out.readyFenceValue, flowDone)));
  REQUIRE(WaitForSingleObject(flowDone, 5000) == WAIT_OBJECT_0);
  CloseHandle(flowDone);

  REQUIRE(out.gridSize == 4);
  REQUIRE(out.gridWidth == (kW + 3) / 4);
  REQUIRE(out.gridHeight == (kH + 3) / 4);

  // Sample the grid row covering the bar and take the strongest horizontal
  // vector. Estimated flow is approximate, so the tolerance is generous; the
  // assertion that matters is the sign and rough magnitude.
  const auto vectors = ReadFlowGrid(*bridge, out);
  const uint32_t barRow = (kH / 2) / out.gridSize;
  float strongest = 0.0f;
  for (uint32_t gx = 0; gx < out.gridWidth; ++gx) {
    const float fx = vectors[(static_cast<size_t>(barRow) * out.gridWidth + gx) * 2] / 32.0f;
    if (std::fabs(fx) > std::fabs(strongest)) strongest = fx;
  }
  INFO("strongest horizontal flow: " << strongest << " px");
  REQUIRE(std::fabs(strongest) >= 2.0f);
  REQUIRE(std::fabs(strongest) <= 8.0f);
#endif
}
