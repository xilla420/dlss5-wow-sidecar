#include "runtime/Pipeline.h"

#include <chrono>

#include "capture/WgcSource.h"
#include "gpu/DeviceBridge.h"
#include "present/DCompOverlay.h"

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

namespace sidecar {
namespace {

// A D3D12 texture the neural pass can write into, matching the capture format.
// It rests in COPY_SOURCE between frames because the overlay's present is the
// last thing to touch it; the render loop brackets the pass with barriers.
ComPtr<ID3D12Resource> CreateWorkTarget(ID3D12Device* dev, uint32_t w, uint32_t h) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = w;
  rd.Height = h;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  ComPtr<ID3D12Resource> tex;
  dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                               D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
                               IID_PPV_ARGS(&tex));
  return tex;
}

}  // namespace

std::unique_ptr<Pipeline> Pipeline::Create(const GpuInfo& gpu,
                                           const PipelineConfig& config,
                                           std::unique_ptr<INeuralPass> pass) {
  if (!config.target || !IsWindow(config.target)) return nullptr;

  RECT client{};
  if (!GetClientRect(config.target, &client)) return nullptr;
  const uint32_t w = static_cast<uint32_t>(client.right - client.left);
  const uint32_t h = static_cast<uint32_t>(client.bottom - client.top);
  if (w == 0 || h == 0) return nullptr;

  std::unique_ptr<Pipeline> p(new Pipeline());
  p->config_ = config;
  p->gpu_ = gpu;
  p->pass_ = std::move(pass);

  p->bridge_ = DeviceBridge::Create(gpu.luid, w, h);
  if (!p->bridge_) return nullptr;

  p->overlay_ = DCompOverlay::Create(*p->bridge_, w, h);
  if (!p->overlay_) return nullptr;

  POINT origin{client.left, client.top};
  ClientToScreen(config.target, &origin);
  RECT bounds{origin.x, origin.y, origin.x + static_cast<LONG>(w),
              origin.y + static_cast<LONG>(h)};
  p->overlay_->SetBounds(bounds);

  p->hud_ = Hud::Create();          // a missing HUD is not fatal
  // Narrow the adapter description to ASCII for the HUD's GDI text path.
  p->gpuName_.reserve(gpu.name.size());
  for (const wchar_t c : gpu.name) {
    p->gpuName_.push_back(c < 128 ? static_cast<char>(c) : '?');
  }

  auto* dev = p->bridge_->D3d12();
  p->workTarget_ = CreateWorkTarget(dev, w, h);
  if (!p->workTarget_) return nullptr;
  if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&p->alloc_)))) return nullptr;
  if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, p->alloc_.Get(),
                                    nullptr, IID_PPV_ARGS(&p->cmdList_)))) return nullptr;
  p->cmdList_->Close();

  // Optical flow and its consumers. Every one of these is optional: a failure
  // here costs motion vectors, not the frame, so the pipeline still runs with
  // a static-scene assumption (spec section 11).
  p->normalize_ = FormatNormalize::Create(dev, w, h);
  p->normalized_ = FormatNormalize::CreateRgba16fTarget(dev, w, h);
  p->luminance_ = Luminance::Create(dev, w, h);
  p->previousLuma_ = Luminance::CreateR8Target(dev, w, h);
  p->currentLuma_ = Luminance::CreateR8Target(dev, w, h);
  p->flow_ = NvofaFlow::Create(dev, p->bridge_->Queue(), w, h, 4);
  p->flowToMv_ = FlowToMotionVec::Create(dev, w, h);
  p->motionTarget_ = FlowToMotionVec::CreateMotionTarget(dev, w, h);

  Pipeline* raw = p.get();
  p->source_ = WgcSource::CreateForWindow(config.target, *p->bridge_,
                                          [raw] { raw->stats_.RecordDrop(); });
  if (!p->source_) return nullptr;

  DCompOverlay* overlay = p->overlay_.get();
  p->tracker_ = WindowTracker::Create(config.target, [overlay](const RECT& r) {
    overlay->SetBounds(r);
  });
  // A missing tracker is not fatal: the overlay simply will not follow moves.

  return p;
}

Pipeline::~Pipeline() { Stop(); }

HWND Pipeline::OverlayHwnd() const { return overlay_ ? overlay_->Hwnd() : nullptr; }

std::string Pipeline::LastError() const {
  std::lock_guard<std::mutex> lock(errorMutex_);
  return lastError_;
}

void Pipeline::FailAndHide(const char* reason) {
  // Spec failure rule: hide first, then record. Never leave an opaque overlay
  // over a live game.
  if (overlay_) overlay_->Hide();
  std::lock_guard<std::mutex> lock(errorMutex_);
  lastError_ = reason;
}

void Pipeline::Panic() noexcept {
  // Overlay first, always. The player must be able to see the game again
  // immediately, before any slower teardown happens.
  if (overlay_) overlay_->Hide();
  if (hud_) hud_->Hide();
  stopRequested_.store(true, std::memory_order_release);
}

bool Pipeline::Rebuild() {
  // Device loss invalidates both devices, the shared ring, the swapchain and
  // every pipeline state, so the only correct response is to build all of it
  // again from the adapter up.
  if (overlay_) overlay_->Hide();
  source_.reset();
  tracker_.reset();
  overlay_.reset();
  normalize_.reset();
  flow_.reset();
  flowToMv_.reset();
  normalized_.Reset();
  motionTarget_.Reset();
  previousLuma_.Reset();
  currentLuma_.Reset();
  workTarget_.Reset();
  cmdList_.Reset();
  alloc_.Reset();
  bridge_.reset();

  auto rebuilt = Pipeline::Create(gpu_, config_, std::move(pass_));
  if (!rebuilt) return false;

  // Everything device-derived comes across. Stats and the panic switch stay,
  // so a reset does not erase the latency history the operator is reading.
  // The HUD moves too: Hud::Create points a file-scope pointer at the newest
  // instance, so leaving the old one in place would stop it painting.
  bridge_ = std::move(rebuilt->bridge_);
  overlay_ = std::move(rebuilt->overlay_);
  source_ = std::move(rebuilt->source_);
  tracker_ = std::move(rebuilt->tracker_);
  hud_ = std::move(rebuilt->hud_);
  pass_ = std::move(rebuilt->pass_);
  normalize_ = std::move(rebuilt->normalize_);
  flow_ = std::move(rebuilt->flow_);
  flowToMv_ = std::move(rebuilt->flowToMv_);
  normalized_ = rebuilt->normalized_;
  motionTarget_ = rebuilt->motionTarget_;
  workTarget_ = rebuilt->workTarget_;
  alloc_ = rebuilt->alloc_;
  cmdList_ = rebuilt->cmdList_;
  gpuName_ = rebuilt->gpuName_;
  havePreviousFrame_ = false;

  source_->Start();
  if (config_.showOverlay) overlay_->Show();
  if (hud_) hud_->Show();
  return true;
}

void Pipeline::Start() {
  // Gate on the thread, not on running_: the render loop clears running_ by
  // itself when a frame fails, and a second Start() must not leave the first
  // thread unjoined.
  if (renderThread_.joinable()) return;
  stopRequested_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  source_->Start();
  if (config_.showOverlay) overlay_->Show();
  if (hud_) hud_->Show();
  renderThread_ = std::thread([this] { RenderLoop(); });
}

void Pipeline::Stop() {
  // Always join if there is a thread. The loop may already have exited on a
  // failure and cleared running_, and an unjoined std::thread terminates the
  // process when it is destroyed.
  stopRequested_.store(true, std::memory_order_release);
  if (renderThread_.joinable()) renderThread_.join();
  if (source_) source_->Stop();
  if (overlay_) overlay_->Hide();
  if (hud_) hud_->Hide();
  running_.store(false, std::memory_order_release);
}

void Pipeline::RenderLoop() {
  // RegisterHotKey binds the hotkey to the registering thread's message queue
  // and Pump drains that same queue, so both have to happen here rather than
  // on whichever thread called Start().
  panic_ = PanicSwitch::Create([this] { Panic(); });

  uint64_t sinceHudUpdate = 0;
  while (!stopRequested_.load(std::memory_order_acquire)) {
    if (panic_) panic_->Pump();
    if (panic_ && panic_->Triggered()) break;

    const HRESULT reason = bridge_->D3d12()->GetDeviceRemovedReason();
    if (IsDeviceLost(reason)) {
      FailAndHide("graphics device was reset; rebuilding");
      if (!Rebuild()) {
        FailAndHide("graphics device was reset and could not be rebuilt");
        break;
      }
      continue;
    }

    if (source_->IsClosed()) {
      FailAndHide("capture item closed: the target window went away");
      break;
    }

    auto frame = bridge_->AcquireLatest();
    if (!frame) {
      Sleep(1);
      continue;
    }

    const auto begin = Clock::now();

    alloc_->Reset();
    cmdList_->Reset(alloc_.Get(), nullptr);

    // Widen the captured frame into typed RGBA16F. Nothing consumes it until
    // the neural pass lands in M3, but running it now keeps the format seam
    // exercised rather than discovered later.
    if (normalize_ && normalized_) {
      normalize_->Record(cmdList_.Get(), frame->texture, normalized_.Get());
    }

    // NVOFA consumes GRAYSCALE8, so reduce the captured frame to luminance
    // before it goes anywhere near the flow accelerator.
    const bool haveLuma = luminance_ && currentLuma_ && previousLuma_;
    if (haveLuma) {
      luminance_->Record(cmdList_.Get(), frame->texture, currentLuma_.Get());
    }

    // A missing motion field is not a frame failure: the pass receives null and
    // treats the scene as static for this frame.
    ID3D12Resource* motion = nullptr;
    if (flow_ && flow_->Available() && haveLuma && havePreviousFrame_) {
      FlowOutput out{};
      if (flow_->Execute(currentLuma_.Get(), previousLuma_.Get(), out)) {
        flowToMv_->Record(cmdList_.Get(), out, motionTarget_.Get());
        motion = motionTarget_.Get();
      }
    }

    // The pass writes workTarget_ and the overlay's present reads it straight
    // back, so bracket the pass: COPY_SOURCE at rest, COPY_DEST while written.
    D3D12_RESOURCE_BARRIER toWrite{};
    toWrite.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toWrite.Transition.pResource = workTarget_.Get();
    toWrite.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toWrite.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toWrite.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    cmdList_->ResourceBarrier(1, &toWrite);

    const bool ok = pass_->Evaluate(cmdList_.Get(), frame->texture,
                                    motion, nullptr, workTarget_.Get());

    D3D12_RESOURCE_BARRIER toRead = toWrite;
    toRead.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toRead.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cmdList_->ResourceBarrier(1, &toRead);

    cmdList_->Close();
    if (!ok) {
      FailAndHide("neural pass rejected the frame");
      break;
    }

    bridge_->Queue()->Wait(bridge_->SharedFence(), frame->fenceValue);
    ID3D12CommandList* lists[] = {cmdList_.Get()};
    bridge_->Queue()->ExecuteCommandLists(1, lists);

    overlay_->Present(workTarget_.Get(), frame->fenceValue);

    // This frame's luminance becomes next frame's reference. Swapping rather
    // than copying keeps both textures alive and costs nothing.
    if (haveLuma) {
      currentLuma_.Swap(previousLuma_);
      havePreviousFrame_ = true;
    }

    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - begin);
    stats_.Record(elapsed.count());

    // Refresh roughly twice a second rather than every frame.
    if (hud_ && ++sinceHudUpdate >= 30) {
      sinceHudUpdate = 0;
      HudModel model;
      model.p50Ms = stats_.P50();
      model.p99Ms = stats_.P99();
      model.frames = stats_.Count();
      model.drops = stats_.Dropped();
      model.passName = pass_->Name();
      model.gpuName = gpuName_.c_str();
      hud_->Update(model);
    }
  }
  // UnregisterHotKey must run on the thread that registered it.
  panic_.reset();
  running_.store(false, std::memory_order_release);
}

}  // namespace sidecar
