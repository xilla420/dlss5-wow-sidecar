#include <catch2/catch_test_macros.hpp>
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/GpuProfile.h"
#include "gpu/DeviceBridge.h"
#include "gpu/FormatNormalize.h"
#include "../src/testpattern/Pattern.h"

using Microsoft::WRL::ComPtr;
using namespace sidecar;
using sidecar::testpattern::Bgra;
using sidecar::testpattern::PixelAt;

namespace {

// 256 square, not 64: the pattern's motion bar is 64 rows tall and centred, so
// at 64 square it would cover every row and blacken the quadrant samples.
constexpr uint32_t kW = 256, kH = 256;

float HalfToFloat(uint16_t h) {
  const uint32_t sign = (h & 0x8000u) << 16;
  uint32_t exponent = (h >> 10) & 0x1Fu;
  uint32_t mantissa = h & 0x3FFu;
  uint32_t bits;
  if (exponent == 0) {
    bits = mantissa ? 0 : sign;   // subnormals flush to zero; adequate here
  } else if (exponent == 31) {
    bits = sign | 0x7F800000u | (mantissa << 13);
  } else {
    bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
  }
  float out;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

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

// Publishes the pattern through the bridge, runs the normalise pass over the
// resulting shared texture, and reads the RGBA16F result back as floats.
std::vector<float> RunNormalizeAndReadBack(DeviceBridge& bridge,
                                           FormatNormalize& normalize) {
  auto* dev = bridge.D3d12();

  auto src = MakePatternTexture(bridge.D3d11(), bridge.D3d11Context(), 0);
  REQUIRE(bridge.Publish(src.Get()) == false);
  auto frame = bridge.AcquireLatest();
  REQUIRE(frame.has_value());

  auto dst = FormatNormalize::CreateRgba16fTarget(dev, kW, kH);
  REQUIRE(dst != nullptr);

  constexpr UINT kBytesPerPixel = 8;   // four 16-bit channels
  const UINT rowPitch = (kW * kBytesPerPixel + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
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

  normalize.Record(cl.Get(), frame->texture, dst.Get());

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
  copyDst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
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

  // Honour the bridge contract before reading the captured texture.
  REQUIRE(SUCCEEDED(bridge.Queue()->Wait(bridge.SharedFence(), frame->fenceValue)));
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
  std::vector<float> out(static_cast<size_t>(kW) * kH * 4);
  for (uint32_t y = 0; y < kH; ++y) {
    const auto* row =
        reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped) + y * rowPitch);
    for (uint32_t i = 0; i < kW * 4; ++i) {
      out[static_cast<size_t>(y) * kW * 4 + i] = HalfToFloat(row[i]);
    }
  }
  D3D12_RANGE none{0, 0};
  readback->Unmap(0, &none);
  return out;
}

}  // namespace

TEST_CASE("saturated BGRA8 values arrive as exact 0.0 and 1.0 in RGBA16F", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);

  auto normalize = FormatNormalize::Create(bridge->D3d12(), kW, kH);
  REQUIRE(normalize != nullptr);

  // The four quadrant colours are fully saturated, so every channel must land
  // on exactly 0.0 or 1.0 -- any deviation means a gamma or swizzle bug.
  const auto pixels = RunNormalizeAndReadBack(*bridge, *normalize);
  REQUIRE(pixels.size() == static_cast<size_t>(kW) * kH * 4);

  auto at = [&](uint32_t x, uint32_t y, int channel) {
    return pixels[(static_cast<size_t>(y) * kW + x) * 4 + channel];
  };

  // Top-left quadrant is red: R=1, G=0, B=0, A=1.
  REQUIRE(at(4, 4, 0) == 1.0f);
  REQUIRE(at(4, 4, 1) == 0.0f);
  REQUIRE(at(4, 4, 2) == 0.0f);
  REQUIRE(at(4, 4, 3) == 1.0f);

  // Top-right quadrant is green.
  REQUIRE(at(kW - 4, 4, 0) == 0.0f);
  REQUIRE(at(kW - 4, 4, 1) == 1.0f);
  REQUIRE(at(kW - 4, 4, 2) == 0.0f);

  // Bottom-left quadrant is blue: proves the BGRA to RGBA swizzle is right.
  REQUIRE(at(4, kH - 4, 0) == 0.0f);
  REQUIRE(at(4, kH - 4, 1) == 0.0f);
  REQUIRE(at(4, kH - 4, 2) == 1.0f);
}

TEST_CASE("the normalise target is a typed float format, never typeless", "[device]") {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, 64, 64);
  REQUIRE(bridge != nullptr);
  auto target = FormatNormalize::CreateRgba16fTarget(bridge->D3d12(), 64, 64);
  REQUIRE(target != nullptr);
  const auto desc = target->GetDesc();
  REQUIRE(desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
  REQUIRE((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0);
}
