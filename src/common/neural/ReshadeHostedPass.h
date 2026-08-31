#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "neural/INeuralPass.h"
#include "neural/NgxSession.h"

namespace sidecar {

// Route B: DLSS 5 Neural Rendering, driven by hosting ReShade and
// renodx-dlss5.addon64 inside the sidecar.
//
// The shape of this is counter-intuitive and worth stating plainly, because it
// is not what the original plan assumed. **This pass is a DLSS client, not an
// NR client.** It creates and evaluates an ordinary DLSS/DLAA feature; the
// add-on detours our NGX calls, harvests the colour/depth/motion contract out of
// our parameter block, and substitutes neural-rendered output. Asking NGX for a
// neural-rendering feature directly does not work -- the M3 Task 3 and Task 4
// spikes established that the runtime refuses a session set up by anyone but the
// NGX core.
//
// Depth is synthesised. This sidecar captures DWM's composited output and has no
// depth buffer, and cannot acquire one. Depth drives disocclusion detection in
// the reprojection, so a constant plane costs temporal stability under motion
// rather than preventing NR from running -- measured in the Task 4 spike, where
// NR evaluated happily against an uninitialised depth texture. It is bound
// because the contract requires the binding, not because the contents are
// meaningful.
class ReshadeHostedPass : public INeuralPass {
 public:
  struct Options {
    // Where nvngx_*.dll live. Resolved to an absolute path by NgxSession,
    // because a relative one silently finds nothing.
    std::filesystem::path runtimeDir;
    uint32_t width = 0;
    uint32_t height = 0;
    // Estimated vectors do better under the CNN presets, which clamp temporal
    // history harder than the default transformer preset. See the Task 4
    // findings.
    DlssPreset preset = DlssPreset::CnnF;
    // Constant written into the synthetic depth plane.
    float syntheticDepth = 0.5f;
  };

  // Returns null when NGX is unavailable, so MakeNeuralPass can fall back to
  // passthrough and say why. `reason` always explains a null result.
  static std::unique_ptr<ReshadeHostedPass> Create(ID3D12Device* device,
                                                   const Options& options,
                                                   std::string& reason);

  bool Evaluate(ID3D12GraphicsCommandList* cl,
                ID3D12Resource* color,
                ID3D12Resource* motion,
                ID3D12Resource* depth,
                ID3D12Resource* out) override;

  // NGX writes through an unordered-access view, not a copy.
  D3D12_RESOURCE_STATES OutputState() const override {
    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  }

  // Explicitly typed float on both ends. The pipeline gives this pass its own
  // intermediate target in this format and resolves it down for presentation.
  DXGI_FORMAT OutputFormat() const override {
    return DXGI_FORMAT_R16G16B16A16_FLOAT;
  }

  const char* Name() const override { return "reshade-hosted DLSS 5 NR"; }

 private:
  ReshadeHostedPass() = default;

  // Uploads the constant depth plane once. The pass owns it because nothing else
  // in the pipeline has any use for a depth buffer.
  bool PrepareDepth(ID3D12Device* device, float value);

  std::unique_ptr<NgxSession> session_;
  Microsoft::WRL::ComPtr<ID3D12Resource> depth_;
  Microsoft::WRL::ComPtr<ID3D12Resource> depthUpload_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t depthRowPitch_ = 0;
  DlssPreset preset_ = DlssPreset::CnnF;
  bool depthUploaded_ = false;
  bool featureFailed_ = false;
  bool warmedUp_ = false;
  bool justCreated_ = true;
  uint64_t frames_ = 0;
};

}  // namespace sidecar
