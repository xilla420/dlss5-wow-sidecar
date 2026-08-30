#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace sidecar {

// Reduces a captured BGRA8 frame to an R8_UNORM luminance texture.
//
// This exists because NVOFA consumes GRAYSCALE8 while the capture path holds
// BGRA8. Without it there is nothing to hand the optical flow accelerator.
class Luminance {
 public:
  static std::unique_ptr<Luminance> Create(ID3D12Device* device,
                                           uint32_t width, uint32_t height);

  static Microsoft::WRL::ComPtr<ID3D12Resource> CreateR8Target(
      ID3D12Device* device, uint32_t width, uint32_t height);

  void Record(ID3D12GraphicsCommandList* cl,
              ID3D12Resource* srcBgra8,
              ID3D12Resource* dstR8);

 private:
  Luminance() = default;

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  UINT descriptorSize_ = 0;
};

}  // namespace sidecar
