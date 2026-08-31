#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "gpu/MaskMath.h"

namespace sidecar {

// Keeps the game's interface out of the neural pass.
//
// Two halves, deliberately separated by cost:
//
//   Rasterise() turns the operator's rectangle list into an R8 mask. It uploads
//   a whole texture, so it runs when the configuration changes -- not per frame.
//
//   Record() blends neural output against the original frame through that mask,
//   and runs every frame.
//
// The mask has a second consumer beyond the blend: DLSS takes a
// bias-current-colour mask marking pixels whose motion vectors should be
// distrusted, and optical-flow vectors estimated over interface elements are
// exactly those pixels. Mask() exposes it for that.
class UiMask {
 public:
  static std::unique_ptr<UiMask> Create(ID3D12Device* device,
                                        uint32_t width, uint32_t height);

  // Rectangles are given in the resolution they were calibrated at; they are
  // scaled and clamped to this mask's size. Feather is the ramp width in pixels
  // just inside each edge.
  //
  // Records an upload into cl. The caller must keep this object alive until that
  // command list has retired, because the staging buffer lives here.
  void Rasterise(ID3D12GraphicsCommandList* cl,
                 const std::vector<MaskRect>& rects,
                 uint32_t sourceWidth, uint32_t sourceHeight,
                 int32_t feather);

  // True once a mask has been rasterised. Before that there is nothing to blend
  // with and the caller should skip the pass entirely rather than blend against
  // an undefined texture.
  bool HasMask() const { return hasMask_; }

  // Empty until Rasterise runs.
  ID3D12Resource* Mask() const { return mask_.Get(); }

  void Record(ID3D12GraphicsCommandList* cl,
              ID3D12Resource* original,
              ID3D12Resource* neural,
              ID3D12Resource* dst);

 private:
  UiMask() = default;

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
  Microsoft::WRL::ComPtr<ID3D12Resource> mask_;
  Microsoft::WRL::ComPtr<ID3D12Resource> upload_;
  std::vector<uint8_t> staging_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t rowPitch_ = 0;
  UINT descriptorSize_ = 0;
  bool hasMask_ = false;
};

}  // namespace sidecar
