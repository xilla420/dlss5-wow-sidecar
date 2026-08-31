#include <catch2/catch_test_macros.hpp>
#include <d3d12.h>
#include <wrl/client.h>

#include <cstring>
#include <vector>

#include "core/GpuProfile.h"
#include "gpu/DeviceBridge.h"
#include "gpu/UiMask.h"

using Microsoft::WRL::ComPtr;
using namespace sidecar;

namespace {

constexpr uint32_t kW = 256, kH = 256;

struct Rgba8 {
  uint8_t r, g, b, a;
  bool operator==(const Rgba8& o) const {
    return r == o.r && g == o.g && b == o.b && a == o.a;
  }
};

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

// A flat-coloured RGBA8 texture, uploaded so the shader has real input. RGBA8
// rather than the pipeline's RGBA16F because the assertion this test exists for
// is bit-exactness, and byte comparison states that plainly.
ComPtr<ID3D12Resource> MakeFilled(DeviceBridge& bridge,
                                  ID3D12GraphicsCommandList* cl,
                                  ComPtr<ID3D12Resource>& uploadKeepAlive,
                                  Rgba8 colour) {
  auto* dev = bridge.D3d12();

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = kW; rd.Height = kH;
  rd.DepthOrArraySize = 1; rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  ComPtr<ID3D12Resource> tex;
  REQUIRE(SUCCEEDED(dev->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(&tex))));

  const UINT rowPitch = (kW * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

  D3D12_HEAP_PROPERTIES up{};
  up.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC bd{};
  bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bd.Width = static_cast<UINT64>(rowPitch) * kH;
  bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
  bd.Format = DXGI_FORMAT_UNKNOWN;
  bd.SampleDesc.Count = 1;
  bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  REQUIRE(SUCCEEDED(dev->CreateCommittedResource(
      &up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&uploadKeepAlive))));

  void* mapped = nullptr;
  D3D12_RANGE noRead{0, 0};
  REQUIRE(SUCCEEDED(uploadKeepAlive->Map(0, &noRead, &mapped)));
  for (uint32_t y = 0; y < kH; ++y) {
    auto* row = reinterpret_cast<Rgba8*>(static_cast<uint8_t*>(mapped) + y * rowPitch);
    for (uint32_t x = 0; x < kW; ++x) row[x] = colour;
  }
  uploadKeepAlive->Unmap(0, nullptr);

  D3D12_RESOURCE_BARRIER toCopy{};
  toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toCopy.Transition.pResource = tex.Get();
  toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cl->ResourceBarrier(1, &toCopy);

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = tex.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = uploadKeepAlive.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  src.PlacedFootprint.Footprint.Width = kW;
  src.PlacedFootprint.Footprint.Height = kH;
  src.PlacedFootprint.Footprint.Depth = 1;
  src.PlacedFootprint.Footprint.RowPitch = rowPitch;
  cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  D3D12_RESOURCE_BARRIER back = toCopy;
  back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  back.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  cl->ResourceBarrier(1, &back);
  return tex;
}

std::vector<Rgba8> ReadBack(DeviceBridge& bridge, ID3D12GraphicsCommandList* cl,
                            ID3D12Resource* tex) {
  auto* dev = bridge.D3d12();
  const UINT rowPitch = (kW * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC bd{};
  bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bd.Width = static_cast<UINT64>(rowPitch) * kH;
  bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
  bd.Format = DXGI_FORMAT_UNKNOWN;
  bd.SampleDesc.Count = 1;
  bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> readback;
  REQUIRE(SUCCEEDED(dev->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
      IID_PPV_ARGS(&readback))));

  D3D12_RESOURCE_BARRIER toCopy{};
  toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toCopy.Transition.pResource = tex;
  toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cl->ResourceBarrier(1, &toCopy);

  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = tex;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = readback.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  dst.PlacedFootprint.Footprint.Width = kW;
  dst.PlacedFootprint.Footprint.Height = kH;
  dst.PlacedFootprint.Footprint.Depth = 1;
  dst.PlacedFootprint.Footprint.RowPitch = rowPitch;
  cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  RunAndWait(bridge, cl);

  void* mapped = nullptr;
  D3D12_RANGE all{0, static_cast<SIZE_T>(bd.Width)};
  REQUIRE(SUCCEEDED(readback->Map(0, &all, &mapped)));
  std::vector<Rgba8> out(static_cast<size_t>(kW) * kH);
  for (uint32_t y = 0; y < kH; ++y) {
    std::memcpy(out.data() + static_cast<size_t>(y) * kW,
                static_cast<uint8_t*>(mapped) + static_cast<size_t>(y) * rowPitch,
                static_cast<size_t>(kW) * 4);
  }
  readback->Unmap(0, nullptr);
  return out;
}

}  // namespace

// The plan's Step 3 assertion: with a mask over one quadrant, that quadrant is
// bit-identical to the original and the rest is not.
//
// Bit-identical is the demanding half. It only holds if full coverage quantises
// to exactly 255 and the shader's lerp contributes no neural component there --
// which is why Rasterise rounds rather than truncates.
TEST_CASE("UiMask keeps the masked region bit-identical and blends elsewhere",
          "[device]") {
  const auto gpu = DetectPrimaryGpu();
  if (!gpu) { SUCCEED("no NVIDIA adapter"); return; }
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);

  auto mask = UiMask::Create(bridge->D3d12(), kW, kH);
  REQUIRE(mask != nullptr);
  CHECK(mask->HasMask() == false);   // nothing rasterised yet

  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cl;
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))));
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cl))));

  const Rgba8 kOriginal{200, 40, 40, 255};
  const Rgba8 kNeural{40, 200, 40, 255};

  ComPtr<ID3D12Resource> upA, upB;
  auto original = MakeFilled(*bridge, cl.Get(), upA, kOriginal);
  auto neural = MakeFilled(*bridge, cl.Get(), upB, kNeural);

  // Top-left quadrant, no feather, so the boundary is exact and the assertion
  // is unambiguous.
  const std::vector<MaskRect> rects{{0, 0, static_cast<int32_t>(kW / 2),
                                     static_cast<int32_t>(kH / 2)}};
  mask->Rasterise(cl.Get(), rects, kW, kH, 0);
  CHECK(mask->HasMask());

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = kW; rd.Height = kH;
  rd.DepthOrArraySize = 1; rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> out;
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      nullptr, IID_PPV_ARGS(&out))));

  mask->Record(cl.Get(), original.Get(), neural.Get(), out.Get());
  const auto pixels = ReadBack(*bridge, cl.Get(), out.Get());

  const auto at = [&](uint32_t x, uint32_t y) { return pixels[y * kW + x]; };

  // Masked quadrant: exactly the original, byte for byte.
  CHECK(at(0, 0) == kOriginal);
  CHECK(at(kW / 2 - 1, kH / 2 - 1) == kOriginal);
  CHECK(at(10, 10) == kOriginal);

  // Everywhere else: exactly the neural output.
  CHECK(at(kW / 2, kH / 2) == kNeural);
  CHECK(at(kW - 1, kH - 1) == kNeural);
  CHECK(at(kW - 1, 0) == kNeural);
  CHECK(at(0, kH - 1) == kNeural);
}

TEST_CASE("UiMask with no rectangles leaves the neural output untouched",
          "[device]") {
  const auto gpu = DetectPrimaryGpu();
  if (!gpu) { SUCCEED("no NVIDIA adapter"); return; }
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);

  auto mask = UiMask::Create(bridge->D3d12(), kW, kH);
  REQUIRE(mask != nullptr);

  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cl;
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))));
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cl))));

  const Rgba8 kOriginal{200, 40, 40, 255};
  const Rgba8 kNeural{40, 200, 40, 255};
  ComPtr<ID3D12Resource> upA, upB;
  auto original = MakeFilled(*bridge, cl.Get(), upA, kOriginal);
  auto neural = MakeFilled(*bridge, cl.Get(), upB, kNeural);

  mask->Rasterise(cl.Get(), {}, kW, kH, 4);

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = kW; rd.Height = kH;
  rd.DepthOrArraySize = 1; rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> out;
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      nullptr, IID_PPV_ARGS(&out))));

  mask->Record(cl.Get(), original.Get(), neural.Get(), out.Get());
  const auto pixels = ReadBack(*bridge, cl.Get(), out.Get());

  for (uint32_t i = 0; i < 8; ++i) {
    INFO("sample " << i);
    CHECK(pixels[i * (kW * kH / 8)] == kNeural);
  }
}

// Feathering is the reason the mask is R8 rather than a stencil. Sampled across
// an edge, coverage must rise monotonically rather than jump.
TEST_CASE("UiMask feathers across the rectangle edge", "[device]") {
  const auto gpu = DetectPrimaryGpu();
  if (!gpu) { SUCCEED("no NVIDIA adapter"); return; }
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);

  auto mask = UiMask::Create(bridge->D3d12(), kW, kH);
  REQUIRE(mask != nullptr);

  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cl;
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))));
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cl))));

  const Rgba8 kOriginal{255, 0, 0, 255};
  const Rgba8 kNeural{0, 0, 0, 255};
  ComPtr<ID3D12Resource> upA, upB;
  auto original = MakeFilled(*bridge, cl.Get(), upA, kOriginal);
  auto neural = MakeFilled(*bridge, cl.Get(), upB, kNeural);

  constexpr int32_t kFeather = 8;
  const std::vector<MaskRect> rects{{64, 64, 192, 192}};
  mask->Rasterise(cl.Get(), rects, kW, kH, kFeather);

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = kW; rd.Height = kH;
  rd.DepthOrArraySize = 1; rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> out;
  REQUIRE(SUCCEEDED(bridge->D3d12()->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      nullptr, IID_PPV_ARGS(&out))));

  mask->Record(cl.Get(), original.Get(), neural.Get(), out.Get());
  const auto pixels = ReadBack(*bridge, cl.Get(), out.Get());

  // Red channel carries the blend: 0 outside, 255 in the interior, rising
  // across the feather band.
  const auto redAt = [&](uint32_t x, uint32_t y) { return pixels[y * kW + x].r; };
  CHECK(redAt(60, 128) == 0);          // outside
  CHECK(redAt(128, 128) == 255);       // deep inside

  uint8_t previous = 0;
  for (int32_t x = 64; x <= 64 + kFeather; ++x) {
    const uint8_t here = redAt(static_cast<uint32_t>(x), 128);
    INFO("x=" << x << " red=" << static_cast<int>(here));
    CHECK(here >= previous);
    previous = here;
  }
  CHECK(previous > 0);
}
