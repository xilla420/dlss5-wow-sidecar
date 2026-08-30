#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace sidecar {

// The flow grid is a Texture2D of gridWidth x gridHeight in R16G16_SINT: one
// vector per gridSize-by-gridSize block, two int16_t in S10.5 fixed point
// (divide by 32 for pixels).
struct FlowOutput {
  ID3D12Resource* grid = nullptr;
  uint32_t gridWidth = 0;
  uint32_t gridHeight = 0;
  uint32_t gridSize = 0;
  // Value the caller's queue must wait for on OutputFence() before reading the
  // grid: NVOFA runs on its own queue, not the one that recorded the inputs.
  uint64_t readyFenceValue = 0;
};

// Wraps the NVIDIA Optical Flow Accelerator, present on Turing and later and
// used here on Ada and Blackwell. WoW exports no motion vectors, so this is
// where the pipeline's motion estimate comes from.
//
// nvofapi64.dll ships with the display driver and is resolved at runtime, so a
// machine without it still loads this binary; Available() simply reports false.
class NvofaFlow {
 public:
  static std::unique_ptr<NvofaFlow> Create(ID3D12Device* device,
                                           ID3D12CommandQueue* queue,
                                           uint32_t width, uint32_t height,
                                           uint32_t gridSize);
  ~NvofaFlow();

  bool Available() const { return session_ != nullptr; }

  // current and previous must be R8_UNORM luminance textures of the configured
  // size, and the caller must have signalled InputFence() to inputReadyValue
  // after the work that produced them. Returns false if the accelerator
  // rejected the pair, in which case the caller falls back to a zero motion
  // field rather than failing the frame.
  bool Execute(ID3D12Resource* current, ID3D12Resource* previous,
               uint64_t inputReadyValue, FlowOutput& out);

  // Signalled by the caller once the luminance textures are written.
  ID3D12Fence* InputFence() const { return inputFence_.Get(); }
  // Signalled by NVOFA once the flow grid is written.
  ID3D12Fence* OutputFence() const { return outputFence_.Get(); }

  uint32_t GridWidth() const { return gridWidth_; }
  uint32_t GridHeight() const { return gridHeight_; }

 private:
  NvofaFlow() = default;

  // NVOFA works on registered resources, not raw ones. Registration is per
  // resource and persists, so handles are cached rather than churned per frame.
  void* HandleFor(ID3D12Resource* resource);

  void* session_ = nullptr;   // NvOFHandle; opaque so the SDK stays out of the header
  void* api_ = nullptr;       // NV_OF_D3D12_API_FUNCTION_LIST*
  void* library_ = nullptr;   // HMODULE for nvofapi64.dll

  std::unordered_map<ID3D12Resource*, void*> registered_;

  Microsoft::WRL::ComPtr<ID3D12Resource> flowGrid_;
  Microsoft::WRL::ComPtr<ID3D12Fence> inputFence_;
  Microsoft::WRL::ComPtr<ID3D12Fence> outputFence_;
  uint64_t outputFenceValue_ = 0;

  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint32_t gridSize_ = 4;
  uint32_t gridWidth_ = 0;
  uint32_t gridHeight_ = 0;
};

}  // namespace sidecar
