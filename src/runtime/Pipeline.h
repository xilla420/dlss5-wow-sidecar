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
#include "neural/INeuralPass.h"
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
  std::string LastError() const;

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
  std::unique_ptr<INeuralPass> pass_;
  Microsoft::WRL::ComPtr<ID3D12Resource> workTarget_;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc_;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList_;

  PipelineConfig config_;
  std::thread renderThread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopRequested_{false};
  LatencyStats stats_;

  mutable std::mutex errorMutex_;
  std::string lastError_;
};

}  // namespace sidecar
