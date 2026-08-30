#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace sidecar {

// Widens captured BGRA8 frames into explicitly typed RGBA16F.
//
// The dlss5-d3d12-fix project exists because DLSS 5 add-ons are sensitive to
// typeless-versus-typed resource views, so this pipeline uses typed views on
// both ends from the start (spec section 8).
class FormatNormalize {
 public:
  static std::unique_ptr<FormatNormalize> Create(ID3D12Device* device,
                                                 uint32_t width, uint32_t height);

  static Microsoft::WRL::ComPtr<ID3D12Resource> CreateRgba16fTarget(
      ID3D12Device* device, uint32_t width, uint32_t height);

  void Record(ID3D12GraphicsCommandList* cl,
              ID3D12Resource* srcBgra8,
              ID3D12Resource* dstRgba16f);

 private:
  FormatNormalize() = default;

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  UINT descriptorSize_ = 0;
};

}  // namespace sidecar
