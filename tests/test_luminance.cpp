#include <catch2/catch_test_macros.hpp>
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/GpuProfile.h"
#include "gpu/DeviceBridge.h"
#include "gpu/Luminance.h"
#include "../src/testpattern/Pattern.h"

using Microsoft::WRL::ComPtr;
using namespace sidecar;
using sidecar::testpattern::Bgra;
using sidecar::testpattern::PixelAt;

namespace {

// Clear of the pattern's 64-row motion bar, so quadrant samples are flat.
constexpr uint32_t kW = 256, kH = 256;

ComPtr<ID3D11Texture2D> MakePatternTexture(ID3D11Device* dev,
                                           ID3D11DeviceContext* ctx,
                                           uint32_t frame) {
  D3D11_TEXTURE2D_DESC td{};
  td.Width = kW; td.Height = kH; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DYNAMIC;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  ComPtr<ID3D11Texture2D> tex;
  REQUIRE(SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &tex)));

  D3D11_MAPPED_SUBRESOURCE m{};
  REQUIRE(SUCCEEDED(ctx->Map(tex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)));
  for (uint32_t y = 0; y < kH; ++y) {
    auto* row = reinterpret_cast<Bgra*>(static_cast<uint8_t*>(m.pData) + y * m.RowPitch);
    for (uint32_t x = 0; x < kW; ++x) row[x] = PixelAt(x, y, kW, kH, frame);
  }
  ctx->Unmap(tex.Get(), 0);
  return tex;
}

std::vector<uint8_t> RunLuminanceAndReadBack(DeviceBridge& bridge, Luminance& luma,
                                             uint32_t frame) {
  auto* dev = bridge.D3d12();

  auto src = MakePatternTexture(bridge.D3d11(), bridge.D3d11Context(), frame);
  REQUIRE(bridge.Publish(src.Get()) == false);
  auto captured = bridge.AcquireLatest();
  REQUIRE(captured.has_value());

  auto dst = Luminance::CreateR8Target(dev, kW, kH);
  REQUIRE(dst != nullptr);

  const UINT rowPitch = (kW + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
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
      &heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, IID_PPV_ARGS(&readback))));

  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cl;
  REQUIRE(SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&alloc))));
  REQUIRE(SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(),
                                           nullptr, IID_PPV_ARGS(&cl))));

  luma.Record(cl.Get(), captured->texture, dst.Get());

  D3D12_RESOURCE_BARRIER toCopy{};
  toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toCopy.Transition.pResource = dst.Get();
  toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  cl->ResourceBarrier(1, &toCopy);

  D3D12_TEXTURE_COPY_LOCATION copyDst{};
  copyDst.pResource = readback.Get();
  copyDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  copyDst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
  copyDst.PlacedFootprint.Footprint.Width = kW;
  copyDst.PlacedFootprint.Footprint.Height = kH;
  copyDst.PlacedFootprint.Footprint.Depth = 1;
  copyDst.PlacedFootprint.Footprint.RowPitch = rowPitch;

  D3D12_TEXTURE_COPY_LOCATION copySrc{};
  copySrc.pResource = dst.Get();
  copySrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  copySrc.SubresourceIndex = 0;

  cl->CopyTextureRegion(&copyDst, 0, 0, 0, &copySrc, nullptr);
  REQUIRE(SUCCEEDED(cl->Close()));

  REQUIRE(SUCCEEDED(bridge.Queue()->Wait(bridge.SharedFence(), captured->fenceValue)));
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
  std::vector<uint8_t> out(static_cast<size_t>(kW) * kH);
  for (uint32_t y = 0; y < kH; ++y) {
    std::memcpy(out.data() + static_cast<size_t>(y) * kW,
                static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * rowPitch, kW);
  }
  D3D12_RANGE none{0, 0};
  readback->Unmap(0, &none);
  return out;
}

// Rec.601 luma of a pattern pixel, as the shader computes it.
int ExpectedLuma(uint32_t x, uint32_t y, uint32_t frame) {
  const auto p = PixelAt(x, y, kW, kH, frame);
  const float luma = 0.299f * (p.r / 255.0f) + 0.587f * (p.g / 255.0f) +
                     0.114f * (p.b / 255.0f);
  return static_cast<int>(std::lround(luma * 255.0f));
}

}  // namespace

TEST_CASE("luminance reduces the pattern to Rec.601 grey", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);

  auto luma = Luminance::Create(bridge->D3d12(), kW, kH);
  REQUIRE(luma != nullptr);

  const auto pixels = RunLuminanceAndReadBack(*bridge, *luma, 0);
  REQUIRE(pixels.size() == static_cast<size_t>(kW) * kH);

  auto at = [&](uint32_t x, uint32_t y) { return static_cast<int>(pixels[static_cast<size_t>(y) * kW + x]); };

  // One unit of slack for the float-to-unorm rounding the hardware does.
  for (const auto [x, y] : {std::pair<uint32_t, uint32_t>{4, 4},
                            {kW - 4, 4}, {4, kH - 4}, {kW - 4, kH - 4}}) {
    INFO("at " << x << "," << y);
    REQUIRE(std::abs(at(x, y) - ExpectedLuma(x, y, 0)) <= 1);
  }

  // The quadrants must stay distinguishable after the reduction, or optical
  // flow has nothing to track.
  REQUIRE(at(4, 4) != at(kW - 4, 4));
  REQUIRE(at(4, 4) != at(4, kH - 4));
  REQUIRE(at(kW - 4, kH - 4) == 255);   // white stays white
}

TEST_CASE("the motion bar survives the luminance reduction", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);
  auto luma = Luminance::Create(bridge->D3d12(), kW, kH);
  REQUIRE(luma != nullptr);

  // The bar is what optical flow is meant to lock onto, so it has to be a hard
  // black edge in the grey image, and it has to move by four pixels.
  const auto frame0 = RunLuminanceAndReadBack(*bridge, *luma, 0);
  const auto frame1 = RunLuminanceAndReadBack(*bridge, *luma, 1);

  const uint32_t barY = kH / 2;
  auto barLeft = [&](const std::vector<uint8_t>& image) {
    for (uint32_t x = 0; x < kW; ++x) {
      if (image[static_cast<size_t>(barY) * kW + x] == 0) return static_cast<int>(x);
    }
    return -1;
  };

  const int a = barLeft(frame0);
  const int b = barLeft(frame1);
  REQUIRE(a >= 0);
  REQUIRE(b >= 0);
  REQUIRE(b - a == 4);
}
