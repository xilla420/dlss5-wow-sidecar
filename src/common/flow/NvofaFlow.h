#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>

namespace sidecar {

// One flow vector per gridSize-by-gridSize block, two int16_t per vector in
// S10.5 fixed point (divide by 32 for pixels).
struct FlowOutput {
  ID3D12Resource* grid = nullptr;
  uint32_t gridWidth = 0;
  uint32_t gridHeight = 0;
  uint32_t gridSize = 0;
};

// Wraps the NVIDIA Optical Flow Accelerator, present on Turing and later and
// used here on Ada and Blackwell. WoW exports no motion vectors, so this is
// where the pipeline's motion estimate comes from.
class NvofaFlow {
 public:
  static std::unique_ptr<NvofaFlow> Create(ID3D12Device* device,
                                           ID3D12CommandQueue* queue,
                                           uint32_t width, uint32_t height,
                                           uint32_t gridSize);
  ~NvofaFlow();

  bool Available() const { return session_ != nullptr; }

  // current and previous must be R8_UNORM luminance textures of the configured
  // size. Returns false if the accelerator rejected the pair, in which case the
  // caller falls back to a zero motion field rather than failing the frame.
  bool Execute(ID3D12Resource* current, ID3D12Resource* previous, FlowOutput& out);

 private:
  NvofaFlow() = default;

  void* session_ = nullptr;   // NvOFHandle; opaque here to keep the SDK out of the header
  Microsoft::WRL::ComPtr<ID3D12Resource> flowBuffer_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  uint64_t fenceValue_ = 0;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t gridSize_ = 4;
  uint32_t gridWidth_ = 0;
  uint32_t gridHeight_ = 0;
};

}  // namespace sidecar
