#include <catch2/catch_test_macros.hpp>
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>

#include "core/GpuProfile.h"
#include "gpu/DeviceBridge.h"
#include "../src/testpattern/Pattern.h"

using Microsoft::WRL::ComPtr;
using namespace sidecar;
using sidecar::testpattern::Bgra;
using sidecar::testpattern::PixelAt;

namespace {

constexpr uint32_t kW = 256, kH = 128;

// Fills a CPU-writable D3D11 texture with the deterministic test pattern.
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

// Reads a D3D12 texture back to system memory through a readback buffer. Takes
// the whole BridgeFrame because honouring the bridge's contract -- wait on the
// shared fence before reading the texture -- needs its fence value.
std::vector<Bgra> ReadBack(DeviceBridge& bridge, const BridgeFrame& frame);

}  // namespace

TEST_CASE("bridge transfers a frame from D3D11 to D3D12 pixel-exact", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());

  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);
  REQUIRE(bridge->Width() == kW);
  REQUIRE(bridge->Height() == kH);

  auto src = MakePatternTexture(bridge->D3d11(), bridge->D3d11Context(), 0);
  REQUIRE(bridge->Publish(src.Get()) == false);  // nothing was overwritten

  auto frame = bridge->AcquireLatest();
  REQUIRE(frame.has_value());

  const auto pixels = ReadBack(*bridge, *frame);
  REQUIRE(pixels.size() == static_cast<size_t>(kW) * kH);

  for (uint32_t y = 0; y < kH; y += 7) {
    for (uint32_t x = 0; x < kW; x += 7) {
      const auto expected = PixelAt(x, y, kW, kH, 0);
      const auto actual = pixels[static_cast<size_t>(y) * kW + x];
      INFO("at " << x << "," << y);
      REQUIRE(actual.b == expected.b);
      REQUIRE(actual.g == expected.g);
      REQUIRE(actual.r == expected.r);
    }
  }
}

TEST_CASE("acquire returns nothing when no new frame was published", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);
  REQUIRE(bridge->AcquireLatest().has_value() == false);
}

TEST_CASE("publishing over an unconsumed frame reports a drop and wins", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);

  auto first = MakePatternTexture(bridge->D3d11(), bridge->D3d11Context(), 0);
  auto second = MakePatternTexture(bridge->D3d11(), bridge->D3d11Context(), 5);

  REQUIRE(bridge->Publish(first.Get()) == false);
  REQUIRE(bridge->Publish(second.Get()) == true);  // overwrote an unconsumed frame

  auto frame = bridge->AcquireLatest();
  REQUIRE(frame.has_value());

  // Latest wins: the bar must be at frame 5's position, not frame 0's.
  const auto pixels = ReadBack(*bridge, *frame);
  const uint32_t barY = kH / 2;
  auto barLeft = [&](auto sample) {
    for (uint32_t x = 0; x < kW; ++x) {
      const auto p = sample(x);
      if (p.r == 0 && p.g == 0 && p.b == 0) return static_cast<int>(x);
    }
    return -1;
  };
  const int actual = barLeft([&](uint32_t x) { return pixels[static_cast<size_t>(barY) * kW + x]; });
  const int expected = barLeft([&](uint32_t x) { return PixelAt(x, barY, kW, kH, 5); });
  REQUIRE(actual == expected);
}

namespace {

std::vector<Bgra> ReadBack(DeviceBridge& bridge, const BridgeFrame& frame) {
  ID3D12Resource* src = frame.texture;
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
      &heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, IID_PPV_ARGS(&readback))));

  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cl;
  REQUIRE(SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))));
  REQUIRE(SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(),
                                           nullptr, IID_PPV_ARGS(&cl))));

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = readback.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  dst.PlacedFootprint.Footprint.Width = kW;
  dst.PlacedFootprint.Footprint.Height = kH;
  dst.PlacedFootprint.Footprint.Depth = 1;
  dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = src;
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  source.SubresourceIndex = 0;

  cl->CopyTextureRegion(&dst, 0, 0, 0, &source, nullptr);
  REQUIRE(SUCCEEDED(cl->Close()));

  // The bridge's contract: wait on the shared fence before touching the
  // texture, or this copy can race the D3D11-side copy that filled it.
  REQUIRE(SUCCEEDED(bridge.Queue()->Wait(bridge.SharedFence(), frame.fenceValue)));

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
  std::vector<Bgra> out(static_cast<size_t>(kW) * kH);
  for (uint32_t y = 0; y < kH; ++y) {
    const auto* row = reinterpret_cast<const Bgra*>(static_cast<const uint8_t*>(mapped) + y * rowPitch);
    std::copy_n(row, kW, out.begin() + static_cast<size_t>(y) * kW);
  }
  D3D12_RANGE none{0, 0};
  readback->Unmap(0, &none);
  return out;
}

}  // namespace
