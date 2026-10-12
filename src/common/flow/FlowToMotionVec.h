#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

#include "flow/NvofaFlow.h"

namespace sidecar {

// Expands the NVOFA flow grid to full resolution and rewrites it into the
// motion-vector convention NGX expects. The arithmetic mirrors
// MotionVectorMath.h, which is unit-tested; this class only moves data.
//
// The same dispatch grades each vector and writes a distrust mask: 1 where the
// previous frame, fetched along the vector, does not match, where it would be
// read from off-screen, or where the motion field jumps. The shader carries the
// reasoning. Consumers are DLSS's bias-current-colour mask and the final blend.
class FlowToMotionVec {
 public:
  // invertMotion flips the vectors for diagnosis (sidecar.toml
  // `invert_motion`). It is never right in normal use.
  static std::unique_ptr<FlowToMotionVec> Create(ID3D12Device* device,
                                                 uint32_t fullWidth,
                                                 uint32_t fullHeight,
                                                 bool invertMotion = false);

  static Microsoft::WRL::ComPtr<ID3D12Resource> CreateMotionTarget(
      ID3D12Device* device, uint32_t width, uint32_t height);

  // R8_UNORM with unordered access, created in UNORDERED_ACCESS.
  static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDistrustTarget(
      ID3D12Device* device, uint32_t width, uint32_t height);

  // Both luminance textures must be readable as shader resources and both
  // outputs in UNORDERED_ACCESS. previousLuma is the frame NVOFA used as its
  // reference, so the photometric check compares exactly what the flow did.
  void Record(ID3D12GraphicsCommandList* cl,
              const FlowOutput& flow,
              ID3D12Resource* currentLuma,
              ID3D12Resource* previousLuma,
              ID3D12Resource* dstRg16f,
              ID3D12Resource* distrustR8);

 private:
  FlowToMotionVec() = default;

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
  uint32_t fullWidth_ = 0;
  uint32_t fullHeight_ = 0;
  float sign_ = 1.0f;
  UINT descriptorSize_ = 0;
};

}  // namespace sidecar
