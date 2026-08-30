#pragma once
#include <windows.h>

#include <wrl/client.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/GpuProfile.h"
#include "core/LatencyStats.h"
#include "core/PanicSwitch.h"
#include "flow/FlowToMotionVec.h"
#include "flow/NvofaFlow.h"
#include "gpu/FormatNormalize.h"
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
  const Hud* GetHud() const { return hud_.get(); }
  std::string LastError() const;

  // I13. Hides the overlay before anything else and asks the render loop to
  // stop. Safe to call from the hotkey callback.
  void Panic() noexcept;

  // Rebuilds every device-derived object after DXGI reports the adapter gone.
  bool Rebuild();

 private:
  Pipeline() = default;
  void RenderLoop();
  void FailAndHide(const char* reason);

  std::unique_ptr<DeviceBridge> bridge_;
  std::unique_ptr<WgcSource> source_;
  std::unique_ptr<DCompOverlay> overlay_;
  // Declared after overlay_ so it is torn down first: its callback holds a raw
  // pointer to the overlay.
  std::unique_ptr<WindowTracker> tracker_;
  std::unique_ptr<Hud> hud_;
  std::unique_ptr<INeuralPass> pass_;
  std::string gpuName_;
  Microsoft::WRL::ComPtr<ID3D12Resource> workTarget_;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList_;

  std::unique_ptr<FormatNormalize> normalize_;
  std::unique_ptr<NvofaFlow> flow_;
  std::unique_ptr<FlowToMotionVec> flowToMv_;
  Microsoft::WRL::ComPtr<ID3D12Resource> normalized_;
  Microsoft::WRL::ComPtr<ID3D12Resource> motionTarget_;
  Microsoft::WRL::ComPtr<ID3D12Resource> previousLuma_;
  Microsoft::WRL::ComPtr<ID3D12Resource> currentLuma_;
  bool havePreviousFrame_ = false;

  PipelineConfig config_;
  GpuInfo gpu_;
  std::unique_ptr<PanicSwitch> panic_;
  std::thread renderThread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopRequested_{false};
  LatencyStats stats_;

  mutable std::mutex errorMutex_;
  std::string lastError_;
};

}  // namespace sidecar
