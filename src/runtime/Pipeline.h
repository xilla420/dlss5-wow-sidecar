#pragma once
#include <windows.h>

#include <wrl/client.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/Config.h"
#include "core/GpuProfile.h"
#include "core/LatencyStats.h"
#include "core/PanicSwitch.h"
#include "flow/FlowToMotionVec.h"
#include "flow/NvofaFlow.h"
#include "gpu/FormatNormalize.h"
#include "gpu/Luminance.h"
#include "gpu/UiMask.h"
#include "neural/INeuralPass.h"
#include "present/Hud.h"
#include "present/WindowTracker.h"

namespace sidecar {

class DeviceBridge;
class WgcSource;
class DCompOverlay;

struct PipelineConfig {
  HWND target = nullptr;
  bool showOverlay = true;
  bool showHud = true;
  // Pixels per flow vector. Validated to 1, 2 or 4 by Config before it gets
  // here; NvofaFlow refuses anything else.
  uint32_t flowGridSize = 4;
  // Screen-space rectangles the neural pass must leave alone, in the resolution
  // the operator calibrated at. Empty means no masking, and the mask pass is
  // then not created at all.
  std::vector<UiRect> uiMaskRects;
  uint32_t uiMaskWidth = 0;
  uint32_t uiMaskHeight = 0;
  int32_t uiMaskFeather = 4;

  // Which pass to build. Only consulted when Create() is handed a null pass,
  // which is how the runtime asks for one -- a device-backed pass cannot be
  // constructed before the device exists, and must be rebuilt with it after
  // device loss.
  std::string neuralPass = "passthrough";
  // Where nvngx_*.dll live. Defaults to the executable's own directory.
  std::filesystem::path runtimeDir;
};

// Owns the render thread and the per-frame orchestration: acquire the newest
// captured frame, run the neural pass, present through the overlay.
//
// Pacing is latest-wins. DeviceBridge already overwrites unconsumed slots; the
// pipeline records each overwrite as a drop and never builds a queue.
class Pipeline {
 public:
  static std::unique_ptr<Pipeline> Create(const GpuInfo& gpu,
                                          const PipelineConfig& config,
                                          std::unique_ptr<INeuralPass> pass);
  ~Pipeline();

  void Start();
  void Stop();
  bool Running() const { return running_.load(std::memory_order_acquire); }

  const LatencyStats& Stats() const { return stats_; }
  HWND OverlayHwnd() const;
  const Hud* GetHud() const { return dev_.hud.get(); }
  std::string LastError() const;

  // I13. Hides the overlay before anything else and asks the render loop to
  // stop. Safe to call from the hotkey callback.
  void Panic() noexcept;

  // True once the render loop has seen the device go away. The render thread
  // cannot rebuild by itself: rebuilding creates the overlay window, and a
  // window belongs to the thread that creates it, so it has to be the thread
  // that pumps. Poll this from the owner's message loop.
  bool NeedsRebuild() const { return rebuildRequested_.load(std::memory_order_acquire); }

  // Rebuilds everything and restarts capture. Call only from the thread that
  // called Start(), which is the thread that owns the windows.
  bool RebuildAndRestart();

  ID3D12Device* DeviceForTest() const;

 private:
  Pipeline() = default;
  void RenderLoop();
  void FailAndHide(const char* reason);

  // Recreates every device-derived object. Does not start anything; Start()
  // does that, so the two paths cannot drift apart.
  bool Rebuild();

  // Blocks until the GPU has finished with everything this pipeline owns.
  void DrainGpu();

  // Everything derived from the D3D device lives in one movable struct.
  //
  // Rebuild() after device loss has to replace all of it, and the earlier
  // version transferred members by hand -- which silently missed the luminance
  // pass, both luma targets and the second command list when those were added
  // later, leaving the rebuilt pipeline holding objects from a dead device.
  // Moving one struct cannot miss a member, and a new member added for M3
  // is carried across for free.
  //
  // Declaration order is load-bearing, because members are destroyed in
  // reverse: the tracker's callback points at the overlay, the source points
  // at the bridge, and the NVIDIA optical flow driver is fussy about the order
  // its registered textures come apart relative to its session. This order is
  // the one that was verified working on hardware -- do not rearrange it
  // without re-running the [device] suite.
  struct DeviceState {
    std::unique_ptr<DeviceBridge> bridge;
    std::unique_ptr<WgcSource> source;
    std::unique_ptr<DCompOverlay> overlay;
    std::unique_ptr<WindowTracker> tracker;
    std::unique_ptr<Hud> hud;
    std::unique_ptr<INeuralPass> pass;
    std::string gpuName;

    Microsoft::WRL::ComPtr<ID3D12Resource> workTarget;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList;
    // A second list, because optical flow sits between the two: its inputs
    // must be submitted and fenced before it starts, and its output consumed
    // after.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc2;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList2;
    uint64_t inputFenceValue = 0;

    std::unique_ptr<FormatNormalize> normalize;
    std::unique_ptr<Luminance> luminance;
    std::unique_ptr<NvofaFlow> flow;
    std::unique_ptr<FlowToMotionVec> flowToMv;
    Microsoft::WRL::ComPtr<ID3D12Resource> normalized;
    Microsoft::WRL::ComPtr<ID3D12Resource> motionTarget;
    Microsoft::WRL::ComPtr<ID3D12Resource> previousLuma;
    Microsoft::WRL::ComPtr<ID3D12Resource> currentLuma;
    bool havePreviousFrame = false;

    // Only created when the operator configured mask rectangles. When absent the
    // render loop takes exactly the path it took before this existed, so an
    // unmasked run pays nothing and cannot regress.
    //
    // Appended at the end deliberately: members are destroyed in reverse, so
    // these come apart before the bridge they were created from.
    std::unique_ptr<UiMask> uiMask;
    Microsoft::WRL::ComPtr<ID3D12Resource> neuralTarget;
    bool maskUploaded = false;
  };
  DeviceState dev_;

  PipelineConfig config_;
  GpuInfo gpu_;
  std::unique_ptr<PanicSwitch> panic_;
  std::thread renderThread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> rebuildRequested_{false};
  // True when dev_.pass was built from config_ rather than handed in, and so
  // must be rebuilt against a new device rather than carried across one.
  bool passFromConfig_ = false;
  // The thread that called Start(), and therefore owns the windows.
  DWORD ownerThreadId_ = 0;
  LatencyStats stats_;

  mutable std::mutex errorMutex_;
  std::string lastError_;
};

}  // namespace sidecar
