#include "flow/NvofaFlow.h"

#include <windows.h>

#if SIDECAR_HAVE_NVOF
#include <nvOpticalFlowCommon.h>
#include <nvOpticalFlowD3D12.h>
#endif

using Microsoft::WRL::ComPtr;

namespace sidecar {
namespace {

uint32_t CellsAcross(uint32_t extent, uint32_t gridSize) {
  return (extent + gridSize - 1) / gridSize;
}

// Four bytes per cell: two int16_t in S10.5.
constexpr uint32_t kBytesPerCell = 4;

ComPtr<ID3D12Resource> CreateFlowBuffer(ID3D12Device* device, uint32_t cells) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = static_cast<UINT64>(cells) * kBytesPerCell;
  rd.Height = 1;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_UNKNOWN;
  rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  ComPtr<ID3D12Resource> buffer;
  if (FAILED(device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON,
          nullptr, IID_PPV_ARGS(&buffer)))) {
    return nullptr;
  }
  return buffer;
}

}  // namespace

std::unique_ptr<NvofaFlow> NvofaFlow::Create(ID3D12Device* device,
                                             ID3D12CommandQueue* queue,
                                             uint32_t width, uint32_t height,
                                             uint32_t gridSize) {
  if (!device || !queue || width == 0 || height == 0) return nullptr;
  if (gridSize != 1 && gridSize != 2 && gridSize != 4) return nullptr;

  std::unique_ptr<NvofaFlow> f(new NvofaFlow());
  f->width_ = width;
  f->height_ = height;
  f->gridSize_ = gridSize;
  f->gridWidth_ = CellsAcross(width, gridSize);
  f->gridHeight_ = CellsAcross(height, gridSize);

  f->flowBuffer_ = CreateFlowBuffer(device, f->gridWidth_ * f->gridHeight_);
  if (!f->flowBuffer_) return nullptr;
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f->fence_)))) {
    return nullptr;
  }

#if SIDECAR_HAVE_NVOF
  // nvofapi64.dll ships with the NVIDIA display driver. Its absence, or a
  // failure to create a session, is not fatal: Available() reports false and
  // the pipeline runs with a zero motion field (spec section 11).
  NV_OF_D3D12_API_FUNCTION_LIST functions{};
  if (NvOFAPICreateInstanceD3D12(NV_OF_API_VERSION, &functions) != NV_OF_SUCCESS) {
    return f;   // session_ stays null
  }

  NvOFHandle session = nullptr;
  if (functions.nvCreateOpticalFlowD3D12(device, queue, &session) != NV_OF_SUCCESS) {
    return f;
  }

  NV_OF_INIT_PARAMS init{};
  init.width = width;
  init.height = height;
  init.mode = NV_OF_MODE_OPTICALFLOW;
  init.inputBufferFormat = NV_OF_BUFFER_FORMAT_GRAYSCALE8;

  // Perf level FAST, not SLOW: this runs every frame inside a latency budget
  // measured in single-digit milliseconds. Quality beyond FAST buys nothing
  // once the vectors are only feeding a DLAA contract.
  init.perfLevel = NV_OF_PERF_LEVEL_FAST;

  // Hint vectors and cost buffers are explicitly disabled. We have no previous
  // flow to seed from across a capture drop, and the cost buffer is unused.
  init.enableExternalHints = NV_OF_FALSE;
  init.enableOutputCost = NV_OF_FALSE;
  init.enableRoi = NV_OF_FALSE;

  // The grid size must match what FlowToMotionVec expects; a mismatch produces
  // vectors that are silently the wrong scale.
  init.outGridSize = static_cast<NV_OF_OUTPUT_VECTOR_GRID_SIZE>(gridSize);
  init.hintGridSize = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;

  if (functions.nvOFInit(session, &init) != NV_OF_SUCCESS) {
    functions.nvOFDestroy(session);
    return f;
  }
  f->session_ = session;
#else
  (void)queue;
#endif
  return f;
}

NvofaFlow::~NvofaFlow() {
#if SIDECAR_HAVE_NVOF
  if (session_) {
    NV_OF_D3D12_API_FUNCTION_LIST functions{};
    if (NvOFAPICreateInstanceD3D12(NV_OF_API_VERSION, &functions) == NV_OF_SUCCESS) {
      functions.nvOFDestroy(static_cast<NvOFHandle>(session_));
    }
    session_ = nullptr;
  }
#endif
}

bool NvofaFlow::Execute(ID3D12Resource* current, ID3D12Resource* previous,
                        FlowOutput& out) {
  if (!current || !previous || !session_) return false;

#if SIDECAR_HAVE_NVOF
  NV_OF_D3D12_API_FUNCTION_LIST functions{};
  if (NvOFAPICreateInstanceD3D12(NV_OF_API_VERSION, &functions) != NV_OF_SUCCESS) {
    return false;
  }

  const uint64_t signalValue = ++fenceValue_;

  NV_OF_EXECUTE_INPUT_PARAMS_D3D12 input{};
  input.inputFrame.pResource = current;
  input.referenceFrame.pResource = previous;

  NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 output{};
  output.outputBuffer.pResource = flowBuffer_.Get();
  output.outputBuffer.outputFencePoint.fence = fence_.Get();
  output.outputBuffer.outputFencePoint.value = signalValue;

  if (functions.nvOFExecuteD3D12(static_cast<NvOFHandle>(session_), &input, &output) !=
      NV_OF_SUCCESS) {
    return false;
  }

  out.grid = flowBuffer_.Get();
  out.gridWidth = gridWidth_;
  out.gridHeight = gridHeight_;
  out.gridSize = gridSize_;
  return true;
#else
  (void)out;
  return false;
#endif
}

}  // namespace sidecar
