#include "neural/ReshadeHostedPass.h"

#include <cstring>
#include <string>
#include <vector>

#include "core/Log.h"

using Microsoft::WRL::ComPtr;

namespace sidecar {
namespace {

// The add-on arms its NGX detours asynchronously after the runtime comes up, so
// a feature created immediately is simply never intercepted and NR never runs.
// DLSS5-Feeder holds off about this long for the same reason.
constexpr uint64_t kCreateDelayFrames = 60;

// The add-on latches into a standby state on the first feature it sees and
// clears it when the feature is re-created. Builds from v45 rescan every present
// and do not need this; ours reports v4.1.5, so it does.
constexpr uint64_t kWarmupRebuildFrames = 180;

constexpr uint32_t kUploadAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

// One transition, kept short because this file records a lot of them.
void Transition(ID3D12GraphicsCommandList* cl, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  if (!resource || before == after) return;
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = resource;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = before;
  b.Transition.StateAfter = after;
  cl->ResourceBarrier(1, &b);
}

}  // namespace

std::unique_ptr<ReshadeHostedPass> ReshadeHostedPass::Create(
    ID3D12Device* device, const Options& options, std::string& reason) {
  reason.clear();
  if (!device || options.width == 0 || options.height == 0) {
    reason = "invalid device or dimensions";
    return nullptr;
  }

  auto session = NgxSession::Create(device, options.runtimeDir);
  if (!session) {
    reason = "NGX session could not be created";
    return nullptr;
  }
  if (!session->Available() || !session->DlssSupported()) {
    reason = session->UnavailableReason();
    if (reason.empty()) reason = "DLSS is unavailable on this system";
    return nullptr;
  }

  std::unique_ptr<ReshadeHostedPass> p(new ReshadeHostedPass());
  p->session_ = std::move(session);
  p->width_ = options.width;
  p->height_ = options.height;

  if (!p->PrepareDepth(device, options.syntheticDepth)) {
    reason = "the synthetic depth plane could not be allocated";
    return nullptr;
  }
  p->preset_ = options.preset;
  return p;
}

bool ReshadeHostedPass::PrepareDepth(ID3D12Device* device, float value) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = width_;
  rd.Height = height_;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_R32_FLOAT;   // typed, as DLSS expects for depth
  rd.SampleDesc.Count = 1;
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_COMMON, nullptr,
                                             IID_PPV_ARGS(&depth_)))) {
    return false;
  }

  const uint32_t rowPitch = AlignUp(width_ * sizeof(float), kUploadAlignment);

  D3D12_HEAP_PROPERTIES uploadHeap{};
  uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC ud{};
  ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  ud.Width = static_cast<uint64_t>(rowPitch) * height_;
  ud.Height = 1;
  ud.DepthOrArraySize = 1;
  ud.MipLevels = 1;
  ud.Format = DXGI_FORMAT_UNKNOWN;
  ud.SampleDesc.Count = 1;
  ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ud,
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&depthUpload_)))) {
    return false;
  }

  void* mapped = nullptr;
  D3D12_RANGE noRead{0, 0};
  if (FAILED(depthUpload_->Map(0, &noRead, &mapped)) || !mapped) return false;
  for (uint32_t y = 0; y < height_; ++y) {
    auto* row = reinterpret_cast<float*>(static_cast<uint8_t*>(mapped) +
                                         static_cast<size_t>(y) * rowPitch);
    for (uint32_t x = 0; x < width_; ++x) row[x] = value;
  }
  depthUpload_->Unmap(0, nullptr);
  depthRowPitch_ = rowPitch;
  return true;
}

bool ReshadeHostedPass::Evaluate(ID3D12GraphicsCommandList* cl,
                                 ID3D12Resource* color,
                                 ID3D12Resource* motion,
                                 ID3D12Resource* /*depth*/,
                                 ID3D12Resource* out) {
  if (!cl || !color || !out) return false;

  // The depth plane is constant, so it is uploaded once and then left in a
  // shader-readable state forever.
  if (!depthUploaded_) {
    Transition(cl, depth_.Get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = depth_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = depthUpload_.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
    src.PlacedFootprint.Footprint.Width = width_;
    src.PlacedFootprint.Footprint.Height = height_;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = depthRowPitch_;
    cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    Transition(cl, depth_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    depthUploaded_ = true;
  }

  ++frames_;

  // Two scheduled creates, both worked around rather than discovered: the first
  // once the add-on's detours are armed, the second to clear the standby latch
  // the add-on sets on the feature it first sees.
  const bool timeToCreate =
      !featureFailed_ &&
      (frames_ == kCreateDelayFrames ||
       (!warmedUp_ && frames_ == kCreateDelayFrames + kWarmupRebuildFrames));

  if (timeToCreate) {
    DlssFeatureDesc desc{};
    // DLAA: render size equals output size and there is no jitter. This sidecar
    // is not upscaling -- it is asking for the neural pass that rides along with
    // a DLSS evaluate, and the prior art feeds exactly this contract.
    desc.renderWidth = width_;
    desc.renderHeight = height_;
    desc.outputWidth = width_;
    desc.outputHeight = height_;
    desc.hdr = false;
    desc.preset = preset_;

    if (session_->CreateDlssFeature(cl, desc)) {
      if (frames_ != kCreateDelayFrames) warmedUp_ = true;
      GlobalLog().Info(std::string("DLSS/DLAA feature created at frame ") +
                       std::to_string(frames_) + "; neural rendering is armed.");
    } else if (frames_ == kCreateDelayFrames) {
      // A failed first create is not fatal; the warm-up attempt still comes.
      GlobalLog().Warn("first DLSS feature creation failed; retrying at warm-up.");
    } else {
      featureFailed_ = true;
      GlobalLog().Error("DLSS feature creation failed twice; neural rendering "
                        "is unavailable this session.");
    }
  }

  // Until the feature exists -- and permanently if it never does -- pass the
  // frame through untouched. Both textures are the pass's own RGBA16F format, so
  // this is a straight copy rather than a conversion.
  if (!session_->HasFeature()) {
    Transition(cl, out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(cl, color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    cl->CopyResource(out, color);
    Transition(cl, color, D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cl, out, D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    return true;
  }

  // NGX reads its inputs as shader resources; the pipeline rests them in
  // UNORDERED_ACCESS because that is how they are written.
  Transition(cl, color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  Transition(cl, motion, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  DlssEvalDesc eval{};
  eval.color = color;
  eval.motion = motion;
  eval.depth = depth_.Get();
  eval.output = out;
  eval.jitterX = 0.0f;
  eval.jitterY = 0.0f;
  // FlowToMotionVec emits pixels divided by the full extent, so multiplying by
  // the extent returns them to the pixel space NGX wants. MotionVectorMath.h
  // holds the same arithmetic for the CPU side and is unit-tested.
  eval.motionScaleX = static_cast<float>(width_);
  eval.motionScaleY = static_cast<float>(height_);
  eval.reset = justCreated_;
  justCreated_ = false;

  const bool ok = motion != nullptr && session_->Evaluate(cl, eval);

  Transition(cl, motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  Transition(cl, color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  if (ok) return true;

  // Evaluate failed, or there was no motion field this frame. Either way the
  // frame still has to be presentable, so fall back to the copy rather than
  // leaving the output undefined. NgxSession has already dropped the feature if
  // the failure was a fault.
  Transition(cl, out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_DEST);
  Transition(cl, color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  cl->CopyResource(out, color);
  Transition(cl, color, D3D12_RESOURCE_STATE_COPY_SOURCE,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  Transition(cl, out, D3D12_RESOURCE_STATE_COPY_DEST,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  return true;
}

}  // namespace sidecar
