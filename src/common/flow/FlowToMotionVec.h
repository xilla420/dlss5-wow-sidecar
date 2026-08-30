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
class FlowToMotionVec {
 public:
  static std::unique_ptr<FlowToMotionVec> Create(ID3D12Device* device,
                                                 uint32_t fullWidth,
                                                 uint32_t fullHeight);

  static Microsoft::WRL::ComPtr<ID3D12Resource> CreateMotionTarget(
      ID3D12Device* device, uint32_t width, uint32_t height);

  void Record(ID3D12GraphicsCommandList* cl,
              const FlowOutput& flow,
              ID3D12Resource* dstRg16f);

 private:
  FlowToMotionVec() = default;

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
  uint32_t fullWidth_ = 0;
  uint32_t fullHeight_ = 0;
  UINT descriptorSize_ = 0;
};

}  // namespace sidecar
