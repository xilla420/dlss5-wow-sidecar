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
  return ((extent + gridSize - 1) & ~(gridSize - 1)) / gridSize;
}

}  // namespace

#if SIDECAR_HAVE_NVOF

namespace {

NV_OF_D3D12_API_FUNCTION_LIST* Api(void* p) {
  return static_cast<NV_OF_D3D12_API_FUNCTION_LIST*>(p);
}

}  // namespace

std::unique_ptr<NvofaFlow> NvofaFlow::Create(ID3D12Device* device,
                                             ID3D12CommandQueue* queue,
                                             uint32_t width, uint32_t height,
                                             uint32_t gridSize) {
  (void)queue;   // NVOFA owns its own queue internally.
  if (!device || width == 0 || height == 0) return nullptr;
  if (gridSize != 1 && gridSize != 2 && gridSize != 4) return nullptr;

  std::unique_ptr<NvofaFlow> f(new NvofaFlow());
  f->width_ = width;
  f->height_ = height;
  f->gridSize_ = gridSize;
  f->gridWidth_ = CellsAcross(width, gridSize);
  f->gridHeight_ = CellsAcross(height, gridSize);

  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f->inputFence_))) ||
      FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f->outputFence_)))) {
    return nullptr;
  }

  // The flow grid is a texture, not a buffer: one R16G16_SINT texel per cell.
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = f->gridWidth_;
  rd.Height = f->gridHeight_;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_R16G16_SINT;   // NV_OF_BUFFER_FORMAT_SHORT2
  rd.SampleDesc.Count = 1;
  if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                             D3D12_RESOURCE_STATE_COMMON, nullptr,
                                             IID_PPV_ARGS(&f->flowGrid_)))) {
    return nullptr;
  }

  // nvofapi64.dll ships with the driver. Resolving it at runtime rather than
  // linking it keeps this binary loadable on a machine without it, and keeps
  // the import table free of a dependency the invariant check would see.
  HMODULE library = LoadLibraryW(L"nvofapi64.dll");
  if (!library) return f;   // session_ stays null; Available() is false
  f->library_ = library;

  using CreateInstanceFn = NV_OF_STATUS(NVOFAPI*)(uint32_t, NV_OF_D3D12_API_FUNCTION_LIST*);
  auto createInstance = reinterpret_cast<CreateInstanceFn>(
      reinterpret_cast<void*>(GetProcAddress(library, "NvOFAPICreateInstanceD3D12")));
  if (!createInstance) return f;

  auto* api = new NV_OF_D3D12_API_FUNCTION_LIST{};
  if (createInstance(NV_OF_API_VERSION, api) != NV_OF_SUCCESS) {
    delete api;
    return f;
  }
  f->api_ = api;

  NvOFHandle session = nullptr;
  if (api->nvCreateOpticalFlowD3D12(device, &session) != NV_OF_SUCCESS || !session) {
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
  init.enableGlobalFlow = NV_OF_FALSE;
  init.predDirection = NV_OF_PRED_DIRECTION_FORWARD;
  init.disparityRange = NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;
  init.hintGridSize = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;

  // The grid size must match what FlowToMotionVec expects; a mismatch produces
  // vectors that are silently the wrong scale.
  init.outGridSize = static_cast<NV_OF_OUTPUT_VECTOR_GRID_SIZE>(gridSize);

  if (api->nvOFInit(session, &init) != NV_OF_SUCCESS) {
    api->nvOFDestroy(session);
    return f;
  }
  f->session_ = session;
  return f;
}

void* NvofaFlow::HandleFor(ID3D12Resource* resource) {
  if (!resource || !session_) return nullptr;
  const auto found = registered_.find(resource);
  if (found != registered_.end()) return found->second;

  NvOFGPUBufferHandle handle = nullptr;
  NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 params{};
  params.resource = resource;
  params.hOFGpuBuffer = &handle;
  // Registration itself needs no cross-queue ordering; per-frame ordering is
  // carried by the fence points passed to Execute.
  params.inputFencePoint.fence = inputFence_.Get();
  params.inputFencePoint.value = 0;
  params.outputFencePoint.fence = outputFence_.Get();
  params.outputFencePoint.value = 0;

  if (Api(api_)->nvOFRegisterResourceD3D12(static_cast<NvOFHandle>(session_), &params) !=
          NV_OF_SUCCESS ||
      !handle) {
    return nullptr;
  }
  registered_.emplace(resource, handle);
  return handle;
}

bool NvofaFlow::Execute(ID3D12Resource* current, ID3D12Resource* previous,
                        uint64_t inputReadyValue, FlowOutput& out) {
  if (!current || !previous || !session_) return false;

  void* currentHandle = HandleFor(current);
  void* previousHandle = HandleFor(previous);
  void* gridHandle = HandleFor(flowGrid_.Get());
  if (!currentHandle || !previousHandle || !gridHandle) return false;

  NV_OF_FENCE_POINT wait{};
  wait.fence = inputFence_.Get();
  wait.value = inputReadyValue;

  NV_OF_FENCE_POINT signal{};
  signal.fence = outputFence_.Get();
  signal.value = ++outputFenceValue_;

  NV_OF_EXECUTE_INPUT_PARAMS_D3D12 input{};
  input.inputFrame = static_cast<NvOFGPUBufferHandle>(currentHandle);
  input.referenceFrame = static_cast<NvOFGPUBufferHandle>(previousHandle);
  // Successive frames of one continuous scene, so temporal hints help.
  input.disableTemporalHints = NV_OF_FALSE;
  input.numFencePoints = 1;
  input.fencePoint = &wait;

  NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 output{};
  output.outputBuffer = static_cast<NvOFGPUBufferHandle>(gridHandle);
  output.fencePoint = &signal;

  if (Api(api_)->nvOFExecuteD3D12(static_cast<NvOFHandle>(session_), &input, &output) !=
      NV_OF_SUCCESS) {
    return false;
  }

  out.grid = flowGrid_.Get();
  out.gridWidth = gridWidth_;
  out.gridHeight = gridHeight_;
  out.gridSize = gridSize_;
  out.readyFenceValue = signal.value;
  return true;
}

NvofaFlow::~NvofaFlow() {
  if (api_ && session_) {
    for (auto& [resource, handle] : registered_) {
      (void)resource;
      NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 params{};
      params.hOFGpuBuffer = static_cast<NvOFGPUBufferHandle>(handle);
      Api(api_)->nvOFUnregisterResourceD3D12(&params);
    }
    Api(api_)->nvOFDestroy(static_cast<NvOFHandle>(session_));
  }
  registered_.clear();
  session_ = nullptr;
  delete Api(api_);
  api_ = nullptr;
  if (library_) FreeLibrary(static_cast<HMODULE>(library_));
  library_ = nullptr;
}

#else   // !SIDECAR_HAVE_NVOF

std::unique_ptr<NvofaFlow> NvofaFlow::Create(ID3D12Device* device,
                                             ID3D12CommandQueue* queue,
                                             uint32_t width, uint32_t height,
                                             uint32_t gridSize) {
  (void)queue;
  if (!device || width == 0 || height == 0) return nullptr;
  if (gridSize != 1 && gridSize != 2 && gridSize != 4) return nullptr;

  // Built without the SDK: a valid object whose Available() is false, so the
  // pipeline runs on a zero motion field rather than refusing to start.
  std::unique_ptr<NvofaFlow> f(new NvofaFlow());
  f->width_ = width;
  f->height_ = height;
  f->gridSize_ = gridSize;
  f->gridWidth_ = CellsAcross(width, gridSize);
  f->gridHeight_ = CellsAcross(height, gridSize);
  return f;
}

void* NvofaFlow::HandleFor(ID3D12Resource*) { return nullptr; }

bool NvofaFlow::Execute(ID3D12Resource*, ID3D12Resource*, uint64_t, FlowOutput&) {
  return false;
}

NvofaFlow::~NvofaFlow() = default;

#endif  // SIDECAR_HAVE_NVOF

}  // namespace sidecar
