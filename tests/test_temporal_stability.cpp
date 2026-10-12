// The temporal-ghosting regression (0.4.0/0.5.0): motion vectors were negated
// on their way from NVOFA to NGX, so DLSS fetched history twice the real motion
// away and anything that moved left a trail.
//
// These run a known camera pan through the real chain -- luminance, NVOFA,
// FlowToMotionVec, and (opt-in) a DLSS/DLAA feature -- and measure the result
// rather than assert a convention, because the convention is exactly what was
// wrong last time while every unit test agreed with it.

#include <catch2/catch_test_macros.hpp>
#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "core/GpuProfile.h"
#include "flow/FlowToMotionVec.h"
#include "flow/NvofaFlow.h"
#include "gpu/DeviceBridge.h"
#include "gpu/Luminance.h"
#include "neural/ReshadeHostedPass.h"

using Microsoft::WRL::ComPtr;
using namespace sidecar;

namespace {

constexpr uint32_t kW = 512;
constexpr uint32_t kH = 256;
// Pixels per frame. Content moves right, so a pixel at x was at x - kPan a
// frame ago, and NGX must be told to read history at x - kPan.
constexpr int kPan = 6;
constexpr int kFrames = 100;

// --- the scene ------------------------------------------------------------

// Two octaves of value noise over a strip wide enough to pan across for the
// whole run. Smooth enough for NVOFA to lock onto, detailed enough that a
// misplaced history fetch is a large error rather than a subtle one.
class Scene {
 public:
  Scene() : width_(kW + kPan * (kFrames + 2)) {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const auto octave = [&](uint32_t cell, float weight) {
      const uint32_t gw = width_ / cell + 2, gh = kH / cell + 2;
      std::vector<float> grid(static_cast<size_t>(gw) * gh);
      for (auto& v : grid) v = unit(rng);
      for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
          const float fx = static_cast<float>(x) / cell, fy = static_cast<float>(y) / cell;
          const uint32_t ix = static_cast<uint32_t>(fx), iy = static_cast<uint32_t>(fy);
          const float tx = fx - ix, ty = fy - iy;
          const auto at = [&](uint32_t a, uint32_t b) { return grid[b * gw + a]; };
          const float top = at(ix, iy) * (1 - tx) + at(ix + 1, iy) * tx;
          const float bottom = at(ix, iy + 1) * (1 - tx) + at(ix + 1, iy + 1) * tx;
          value_[static_cast<size_t>(y) * width_ + x] += weight * (top * (1 - ty) + bottom * ty);
        }
      }
    };
    value_.assign(static_cast<size_t>(width_) * kH, 0.0f);
    octave(16, 0.6f);
    octave(5, 0.4f);
  }

  // Frame t is the strip seen through a window that slides left by kPan a
  // frame, so the content appears to move right.
  float At(uint32_t x, uint32_t y, int frame) const {
    const int sx = static_cast<int>(x) - kPan * frame + kPan * (kFrames + 1);
    return value_[static_cast<size_t>(y) * width_ + static_cast<size_t>(sx)];
  }

  void Rgb(uint32_t x, uint32_t y, int frame, float rgb[3]) const {
    const float v = At(x, y, frame);
    rgb[0] = v;
    rgb[1] = 0.8f * v + 0.1f;
    rgb[2] = 1.0f - v;
  }

  std::vector<uint8_t> Luma(int frame) const {
    std::vector<uint8_t> out(static_cast<size_t>(kW) * kH);
    float rgb[3];
    for (uint32_t y = 0; y < kH; ++y) {
      for (uint32_t x = 0; x < kW; ++x) {
        Rgb(x, y, frame, rgb);
        const float luma = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
        out[static_cast<size_t>(y) * kW + x] =
            static_cast<uint8_t>(std::clamp(luma, 0.0f, 1.0f) * 255.0f + 0.5f);
      }
    }
    return out;
  }

  std::vector<uint16_t> ColorHalf(int frame) const;

 private:
  uint32_t width_;
  std::vector<float> value_;
};

uint16_t FloatToHalf(float f) {
  uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof(bits));
  const uint32_t sign = (bits >> 16) & 0x8000u;
  const int exponent = static_cast<int>((bits >> 23) & 0xFF) - 127 + 15;
  const uint32_t mantissa = bits & 0x7FFFFFu;
  if (exponent <= 0) return static_cast<uint16_t>(sign);   // flush; fine here
  if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
  return static_cast<uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
}

float HalfToFloat(uint16_t h) {
  const uint32_t sign = (h & 0x8000u) << 16;
  const uint32_t exponent = (h >> 10) & 0x1Fu;
  const uint32_t mantissa = h & 0x3FFu;
  uint32_t bits = 0;
  if (exponent == 0) {
    bits = sign;
  } else if (exponent == 31) {
    bits = sign | 0x7F800000u | (mantissa << 13);
  } else {
    bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
  }
  float f = 0.0f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

std::vector<uint16_t> Scene::ColorHalf(int frame) const {
  std::vector<uint16_t> out(static_cast<size_t>(kW) * kH * 4);
  float rgb[3];
  for (uint32_t y = 0; y < kH; ++y) {
    for (uint32_t x = 0; x < kW; ++x) {
      Rgb(x, y, frame, rgb);
      uint16_t* px = out.data() + (static_cast<size_t>(y) * kW + x) * 4;
      px[0] = FloatToHalf(rgb[0]);
      px[1] = FloatToHalf(rgb[1]);
      px[2] = FloatToHalf(rgb[2]);
      px[3] = FloatToHalf(1.0f);
    }
  }
  return out;
}

// --- D3D12 plumbing --------------------------------------------------------

struct OneShot {
  ComPtr<ID3D12CommandAllocator> alloc;
  ComPtr<ID3D12GraphicsCommandList> cl;
  explicit OneShot(ID3D12Device* dev) {
    REQUIRE(SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&alloc))));
    REQUIRE(SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             alloc.Get(), nullptr, IID_PPV_ARGS(&cl))));
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

void Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* r,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  if (before == after) return;
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = r;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = before;
  b.Transition.StateAfter = after;
  cl->ResourceBarrier(1, &b);
}

ComPtr<ID3D12Resource> MakeTexture(ID3D12Device* dev, DXGI_FORMAT format,
                                   D3D12_RESOURCE_STATES state) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = kW;
  rd.Height = kH;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.Format = format;
  rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> tex;
  REQUIRE(SUCCEEDED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                 state, nullptr, IID_PPV_ARGS(&tex))));
  return tex;
}

UINT Pitch(uint32_t bytesPerPixel) {
  return (kW * bytesPerPixel + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
         ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
}

// Copies tightly packed pixels into an existing texture, leaving it in `rest`.
void UploadInto(DeviceBridge& bridge, ID3D12Resource* tex, DXGI_FORMAT format,
                uint32_t bytesPerPixel, const void* data, D3D12_RESOURCE_STATES rest) {
  auto* dev = bridge.D3d12();
  const UINT pitch = Pitch(bytesPerPixel);

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC bd{};
  bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bd.Width = static_cast<UINT64>(pitch) * kH;
  bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
  bd.Format = DXGI_FORMAT_UNKNOWN;
  bd.SampleDesc.Count = 1;
  bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> upload;
  REQUIRE(SUCCEEDED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ,
                                                 nullptr, IID_PPV_ARGS(&upload))));
  void* mapped = nullptr;
  D3D12_RANGE none{0, 0};
  REQUIRE(SUCCEEDED(upload->Map(0, &none, &mapped)));
  for (uint32_t y = 0; y < kH; ++y) {
    std::memcpy(static_cast<uint8_t*>(mapped) + static_cast<size_t>(y) * pitch,
                static_cast<const uint8_t*>(data) + static_cast<size_t>(y) * kW * bytesPerPixel,
                static_cast<size_t>(kW) * bytesPerPixel);
  }
  upload->Unmap(0, nullptr);

  OneShot s(dev);
  Transition(s.cl.Get(), tex, rest, D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = tex;
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = upload.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint.Footprint = {format, kW, kH, 1, pitch};
  s.cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  Transition(s.cl.Get(), tex, D3D12_RESOURCE_STATE_COPY_DEST, rest);
  RunAndWait(bridge, s.cl.Get());
}

// Reads a texture back as tightly packed pixels. It is left in `rest`.
std::vector<uint8_t> ReadBack(DeviceBridge& bridge, ID3D12Resource* tex,
                              DXGI_FORMAT format, uint32_t bytesPerPixel,
                              D3D12_RESOURCE_STATES rest) {
  auto* dev = bridge.D3d12();
  const UINT pitch = Pitch(bytesPerPixel);

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC bd{};
  bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bd.Width = static_cast<UINT64>(pitch) * kH;
  bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
  bd.Format = DXGI_FORMAT_UNKNOWN;
  bd.SampleDesc.Count = 1;
  bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> readback;
  REQUIRE(SUCCEEDED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
                                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                                 nullptr, IID_PPV_ARGS(&readback))));
  OneShot s(dev);
  Transition(s.cl.Get(), tex, rest, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = readback.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint.Footprint = {format, kW, kH, 1, pitch};
  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = tex;
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  s.cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  Transition(s.cl.Get(), tex, D3D12_RESOURCE_STATE_COPY_SOURCE, rest);
  RunAndWait(bridge, s.cl.Get());

  std::vector<uint8_t> out(static_cast<size_t>(kW) * kH * bytesPerPixel);
  void* mapped = nullptr;
  D3D12_RANGE all{0, static_cast<SIZE_T>(bd.Width)};
  REQUIRE(SUCCEEDED(readback->Map(0, &all, &mapped)));
  for (uint32_t y = 0; y < kH; ++y) {
    std::memcpy(out.data() + static_cast<size_t>(y) * kW * bytesPerPixel,
                static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * pitch,
                static_cast<size_t>(kW) * bytesPerPixel);
  }
  D3D12_RANGE wrote{0, 0};
  readback->Unmap(0, &wrote);
  return out;
}

// The two luminance textures NVOFA alternates between, kept for the whole run:
// NVOFA registers resources by pointer, exactly as the pipeline's swap relies on.
struct FlowChain {
  DeviceBridge& bridge;
  std::unique_ptr<NvofaFlow> flow;
  std::unique_ptr<FlowToMotionVec> toMv;
  ComPtr<ID3D12Resource> luma[2];
  ComPtr<ID3D12Resource> motion;
  ComPtr<ID3D12Resource> distrust;
  uint64_t inputFence = 0;

  FlowChain(DeviceBridge& b, bool invert) : bridge(b) {
    auto* dev = bridge.D3d12();
    flow = NvofaFlow::Create(dev, bridge.Queue(), kW, kH, 2);
    toMv = FlowToMotionVec::Create(dev, kW, kH, invert);
    for (auto& l : luma) l = Luminance::CreateR8Target(dev, kW, kH, D3D12_RESOURCE_STATE_COMMON);
    motion = FlowToMotionVec::CreateMotionTarget(dev, kW, kH);
    distrust = FlowToMotionVec::CreateDistrustTarget(dev, kW, kH);
  }

  bool Usable() const {
    return flow && flow->Available() && toMv && luma[0] && luma[1] && motion && distrust;
  }

  ID3D12Resource* Current(int frame) const { return luma[frame & 1].Get(); }
  ID3D12Resource* Previous(int frame) const { return luma[(frame + 1) & 1].Get(); }

  void UploadLuma(const Scene& scene, int frame) {
    const auto pixels = scene.Luma(frame);
    UploadInto(bridge, Current(frame), DXGI_FORMAT_R8_UNORM, 1, pixels.data(),
               D3D12_RESOURCE_STATE_COMMON);
  }

  // Runs NVOFA for (frame, frame - 1) and records the conversion into cl, the
  // way Pipeline does. Leaves motion in UNORDERED_ACCESS and distrust in
  // NON_PIXEL_SHADER_RESOURCE.
  void Record(ID3D12GraphicsCommandList* cl, int frame) {
    REQUIRE(SUCCEEDED(bridge.Queue()->Signal(flow->InputFence(), ++inputFence)));
    FlowOutput out{};
    REQUIRE(flow->Execute(Current(frame), Previous(frame), inputFence, out));
    REQUIRE(SUCCEEDED(bridge.Queue()->Wait(flow->OutputFence(), out.readyFenceValue)));

    for (int i = 0; i < 2; ++i) {
      Transition(cl, luma[i].Get(), D3D12_RESOURCE_STATE_COMMON,
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    toMv->Record(cl, out, Current(frame), Previous(frame), motion.Get(), distrust.Get());
    for (int i = 0; i < 2; ++i) {
      Transition(cl, luma[i].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATE_COMMON);
    }
    Transition(cl, distrust.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
};

struct FieldStats {
  float medianMotionX = 0.0f;   // pixels
  float medianMotionY = 0.0f;
  float interiorDistrust = 0.0f;
  float revealedDistrust = 0.0f;   // the strip the pan uncovers at the left
};

FieldStats MeasureField(bool invert) {
  FieldStats stats;
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);
  FlowChain chain(*bridge, invert);
  REQUIRE(chain.Usable());

  const Scene scene;
  chain.UploadLuma(scene, 0);
  chain.UploadLuma(scene, 1);
  OneShot s(bridge->D3d12());
  chain.Record(s.cl.Get(), 1);
  Transition(s.cl.Get(), chain.distrust.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  RunAndWait(*bridge, s.cl.Get());

  const auto motion = ReadBack(*bridge, chain.motion.Get(), DXGI_FORMAT_R16G16_FLOAT, 4,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  const auto distrust = ReadBack(*bridge, chain.distrust.Get(), DXGI_FORMAT_R8_UNORM, 1,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  std::vector<float> xs, ys;
  double interior = 0.0, revealed = 0.0;
  size_t interiorCount = 0, revealedCount = 0;
  for (uint32_t y = 16; y < kH - 16; ++y) {
    for (uint32_t x = 0; x < kW - 16; ++x) {
      const size_t i = static_cast<size_t>(y) * kW + x;
      const float d = distrust[i] / 255.0f;
      if (x < 4) {
        revealed += d;
        ++revealedCount;
      } else if (x >= 32) {
        uint16_t h[2];
        std::memcpy(h, motion.data() + i * 4, 4);
        xs.push_back(HalfToFloat(h[0]) * kW);
        ys.push_back(HalfToFloat(h[1]) * kH);
        interior += d;
        ++interiorCount;
      }
    }
  }
  std::nth_element(xs.begin(), xs.begin() + xs.size() / 2, xs.end());
  std::nth_element(ys.begin(), ys.begin() + ys.size() / 2, ys.end());
  stats.medianMotionX = xs[xs.size() / 2];
  stats.medianMotionY = ys[ys.size() / 2];
  stats.interiorDistrust = static_cast<float>(interior / interiorCount);
  stats.revealedDistrust = static_cast<float>(revealed / revealedCount);
  return stats;
}

std::filesystem::path RuntimeDirFromEnv() {
  size_t len = 0;
  char raw[1024] = {};
  if (getenv_s(&len, raw, sizeof(raw), "SIDECAR_TEST_RUNTIME_DIR") != 0 || len == 0) {
    return {};
  }
  return std::filesystem::path(raw);
}

// Mean absolute RGB error of the DLAA output against the true frame, over the
// interior and the last ten frames of a pan. Negative when DLSS is unavailable.
double DlaaPanError(const std::filesystem::path& runtimeDir, bool invert, bool useMask) {
  auto gpu = DetectPrimaryGpu();
  REQUIRE(gpu.has_value());
  auto bridge = DeviceBridge::Create(gpu->luid, kW, kH);
  REQUIRE(bridge != nullptr);
  auto* dev = bridge->D3d12();

  ReshadeHostedPass::Options options;
  options.runtimeDir = runtimeDir;
  options.width = kW;
  options.height = kH;
  options.arch = gpu->arch;
  std::string reason;
  auto pass = ReshadeHostedPass::Create(dev, options, reason);
  if (!pass) return -1.0;

  FlowChain chain(*bridge, invert);
  REQUIRE(chain.Usable());
  auto color = MakeTexture(dev, DXGI_FORMAT_R16G16B16A16_FLOAT,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto out = MakeTexture(dev, DXGI_FORMAT_R16G16B16A16_FLOAT,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  const Scene scene;
  chain.UploadLuma(scene, 0);
  double error = 0.0;
  size_t samples = 0;
  for (int frame = 1; frame <= kFrames; ++frame) {
    chain.UploadLuma(scene, frame);
    const auto pixels = scene.ColorHalf(frame);
    UploadInto(*bridge, color.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 8, pixels.data(),
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    OneShot s(dev);
    chain.Record(s.cl.Get(), frame);
    FrameHints hints;
    hints.distrust = useMask ? chain.distrust.Get() : nullptr;
    pass->SetFrameHints(hints);
    REQUIRE(pass->Evaluate(s.cl.Get(), color.Get(), chain.motion.Get(), nullptr, out.Get()));
    Transition(s.cl.Get(), chain.distrust.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    RunAndWait(*bridge, s.cl.Get());

    if (frame <= kFrames - 10) continue;
    const auto result = ReadBack(*bridge, out.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 8,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    float truth[3];
    for (uint32_t y = 16; y < kH - 16; ++y) {
      for (uint32_t x = 32; x < kW - 16; ++x) {
        uint16_t h[4];
        std::memcpy(h, result.data() + (static_cast<size_t>(y) * kW + x) * 8, 8);
        scene.Rgb(x, y, frame, truth);
        for (int c = 0; c < 3; ++c) error += std::fabs(HalfToFloat(h[c]) - truth[c]);
        samples += 3;
      }
    }
  }
  return error / static_cast<double>(samples);
}

}  // namespace

TEST_CASE("a camera pan yields vectors that point at the previous frame", "[device]") {
#if !SIDECAR_HAVE_NVOF
  SKIP("Built without the NVIDIA Optical Flow SDK.");
#else
  const auto stats = MeasureField(false);
  INFO("median motion " << stats.medianMotionX << ", " << stats.medianMotionY
       << " px; distrust interior " << stats.interiorDistrust
       << ", revealed strip " << stats.revealedDistrust);

  // Content moved +kPan, so history lives at x - kPan.
  CHECK(std::fabs(stats.medianMotionX - static_cast<float>(-kPan)) < 1.0f);
  CHECK(std::fabs(stats.medianMotionY) < 1.0f);
  // A correct field over a clean pan is trusted almost everywhere...
  CHECK(stats.interiorDistrust < 0.1f);
  // ...except the strip the pan has just uncovered, which has no history.
  CHECK(stats.revealedDistrust > 0.9f);
#endif
}

TEST_CASE("the distrust mask rejects an inverted motion field", "[device]") {
#if !SIDECAR_HAVE_NVOF
  SKIP("Built without the NVIDIA Optical Flow SDK.");
#else
  // The photometric check is what makes the mask worth having: hand it the
  // 0.5.0 convention and it should refuse most of the frame.
  const auto right = MeasureField(false);
  const auto wrong = MeasureField(true);
  INFO("distrust correct " << right.interiorDistrust << ", inverted "
       << wrong.interiorDistrust);
  CHECK(wrong.medianMotionX > 0.0f);
  CHECK(wrong.interiorDistrust > 0.25f);
  CHECK(wrong.interiorDistrust > 5.0f * right.interiorDistrust);
#endif
}

TEST_CASE("DLAA tracks a camera pan instead of smearing it", "[device]") {
  const auto dir = RuntimeDirFromEnv();
  if (dir.empty()) { SUCCEED("SIDECAR_TEST_RUNTIME_DIR not set"); return; }
#if !SIDECAR_HAVE_NVOF
  SKIP("Built without the NVIDIA Optical Flow SDK.");
#else
  // Without the bias mask first, so the sign is measured on its own: the mask
  // hides much of a wrong field, which is its job but not what this asks.
  const double right = DlaaPanError(dir, false, false);
  if (right < 0.0) { SUCCEED("DLSS unavailable here"); return; }
  const double wrong = DlaaPanError(dir, true, false);
  const double masked = DlaaPanError(dir, false, true);
  INFO("mean abs error: correct vectors " << right << ", inverted (0.5.0) "
       << wrong << ", correct with bias mask " << masked);
  // The inverted run is the 0.5.0 behaviour. It must be clearly worse, and the
  // fixed one must stay close to the true frame.
  CHECK(right < 0.5 * wrong);
  CHECK(right < 0.05);
  // On a clean pan the mask marks only the revealed strip, so it must cost
  // essentially nothing where the vectors are good.
  CHECK(masked < 1.25 * right + 0.001);
#endif
}
