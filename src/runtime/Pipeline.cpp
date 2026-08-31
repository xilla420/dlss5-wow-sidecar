#include "runtime/Pipeline.h"

#include <chrono>
#include <cstdio>
#include <string>

#include "capture/WgcSource.h"
#include "core/Log.h"
#include "gpu/DeviceBridge.h"
#include "neural/NeuralPassFactory.h"
#include "neural/PassthroughPass.h"
#include "neural/ReshadeHostedPass.h"
#include "present/DCompOverlay.h"

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

namespace sidecar {
namespace {

// Two decimals is the useful precision for a millisecond figure; std::to_string
// would print six and bury it.
std::string FormatMs(double ms) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", ms);
  return buffer;
}

// Same shape as the work target but in a caller-chosen format, for a pass that
// writes something other than the presentable one. It rests in
// UNORDERED_ACCESS because that is how NGX writes it.
ComPtr<ID3D12Resource> CreateTypedTarget(ID3D12Device* dev, uint32_t w, uint32_t h,
                                         DXGI_FORMAT format) {
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = w;
  rd.Height = h;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.Format = format;
  rd.SampleDesc.Count = 1;
  rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  ComPtr<ID3D12Resource> tex;
  dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                               IID_PPV_ARGS(&tex));
  return tex;
}

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
  // A caller-supplied pass is device-free by construction -- that is the only
  // kind that can exist before this function runs. A pass named in config may
  // hold device resources, so it is built below, once the device does, and
  // rebuilt with it after device loss.
  p->passFromConfig_ = (pass == nullptr);
  p->dev_.pass = std::move(pass);

  p->dev_.bridge = DeviceBridge::Create(gpu.luid, w, h);
  if (!p->dev_.bridge) return nullptr;

  p->dev_.overlay = DCompOverlay::Create(*p->dev_.bridge, w, h);
  if (!p->dev_.overlay) return nullptr;

  POINT origin{client.left, client.top};
  ClientToScreen(config.target, &origin);
  RECT bounds{origin.x, origin.y, origin.x + static_cast<LONG>(w),
              origin.y + static_cast<LONG>(h)};
  p->dev_.overlay->SetBounds(bounds);

  p->dev_.hud = Hud::Create();          // a missing HUD is not fatal
  // Narrow the adapter description to ASCII for the HUD's GDI text path.
  p->dev_.gpuName.reserve(gpu.name.size());
  for (const wchar_t c : gpu.name) {
    p->dev_.gpuName.push_back(c < 128 ? static_cast<char>(c) : '?');
  }

  auto* dev = p->dev_.bridge->D3d12();
  p->dev_.workTarget = CreateWorkTarget(dev, w, h);
  if (!p->dev_.workTarget) return nullptr;
  if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&p->dev_.alloc)))) return nullptr;
  if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, p->dev_.alloc.Get(),
                                    nullptr, IID_PPV_ARGS(&p->dev_.cmdList)))) return nullptr;
  p->dev_.cmdList->Close();
  if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&p->dev_.alloc2)))) return nullptr;
  if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, p->dev_.alloc2.Get(),
                                    nullptr, IID_PPV_ARGS(&p->dev_.cmdList2)))) return nullptr;
  p->dev_.cmdList2->Close();

  // Optical flow and its consumers. Every one of these is optional: a failure
  // here costs motion vectors, not the frame, so the pipeline still runs with
  // a static-scene assumption (spec section 11).
  p->dev_.normalize = FormatNormalize::Create(dev, w, h);
  p->dev_.normalized = FormatNormalize::CreateRgba16fTarget(dev, w, h);
  p->dev_.luminance = Luminance::Create(dev, w, h);
  p->dev_.previousLuma = Luminance::CreateR8Target(dev, w, h, D3D12_RESOURCE_STATE_COMMON);
  p->dev_.currentLuma = Luminance::CreateR8Target(dev, w, h, D3D12_RESOURCE_STATE_COMMON);
  p->dev_.flow = NvofaFlow::Create(dev, p->dev_.bridge->Queue(), w, h, config.flowGridSize);
  p->dev_.flowToMv = FlowToMotionVec::Create(dev, w, h);
  p->dev_.motionTarget = FlowToMotionVec::CreateMotionTarget(dev, w, h);

  // Build the configured pass now that there is a device for it to hold. Any
  // failure inside MakeNeuralPass degrades to passthrough and explains itself,
  // so this cannot fail the pipeline (spec section 11).
  if (p->passFromConfig_) {
    NeuralPassContext ctx;
    ctx.device = dev;
    ctx.runtimeDir = config.runtimeDir;
    ctx.width = w;
    ctx.height = h;
    ctx.arch = gpu.arch;

    std::vector<std::string> warnings;
    p->dev_.pass = MakeNeuralPass(config.neuralPass, ctx, warnings);
    for (const auto& warning : warnings) GlobalLog().Warn(warning);
    if (!p->dev_.pass) return nullptr;
    GlobalLog().Info(std::string("neural pass: ") + p->dev_.pass->Name());
    // Only a route-B pass has a runtime behind it; passthrough leaves this
    // empty and the HUD omits the field entirely.
    if (auto* hosted = dynamic_cast<ReshadeHostedPass*>(p->dev_.pass.get())) {
      p->dev_.runtimeVariant = ToString(hosted->Variant());
    }
  }

  // A pass that names its own output format wants an intermediate target in it,
  // which the mask blend then resolves down to the presentable BGRA8. That is
  // also the only stage that can convert between the two, so a pass asking for
  // a format implies the blend runs whether or not any rectangles are masked.
  const DXGI_FORMAT passFormat = p->dev_.pass->OutputFormat();
  const bool needsResolve = passFormat != DXGI_FORMAT_UNKNOWN;

  // The UI mask, only when the operator actually configured rectangles. Without
  // it the render loop takes the path it took before this existed, so an
  // unmasked run pays nothing for the feature and cannot regress because of it.
  if (!config.uiMaskRects.empty() || needsResolve) {
    p->dev_.uiMask = UiMask::Create(dev, w, h);
    if (p->dev_.uiMask) {
      p->dev_.neuralTarget = needsResolve
                                 ? CreateTypedTarget(dev, w, h, passFormat)
                                 : CreateWorkTarget(dev, w, h);
    }
    if (!p->dev_.uiMask || !p->dev_.neuralTarget) {
      p->dev_.uiMask.reset();
      p->dev_.neuralTarget.Reset();
      if (needsResolve) {
        // Without the resolve stage this pass's output cannot be presented at
        // all, so fall back to the one pass that needs no resolve.
        GlobalLog().Error("the neural pass needs a resolve stage that could not "
                          "be created; falling back to passthrough.");
        p->dev_.pass = PassthroughPass::Create();
      } else {
        // Degrade rather than fail: an unmasked overlay is worth more than none,
        // and the failure rule reserves hiding for things that break the frame.
        GlobalLog().Warn("UI mask could not be created; the interface will be "
                         "processed along with the rest of the frame.");
      }
    } else if (!config.uiMaskRects.empty()) {
      GlobalLog().Info("UI mask active with " +
                       std::to_string(config.uiMaskRects.size()) + " rectangle(s).");
    }
  }

  // Say which way this went. "No motion vectors" is the difference between a
  // neural pass that tracks the scene and one that assumes it is static, and
  // silently guessing wrong is exactly the kind of thing that gets diagnosed
  // as a quality problem months later.
  if (p->dev_.flow && p->dev_.flow->Available()) {
    GlobalLog().Info("optical flow available, grid size " +
                     std::to_string(config.flowGridSize));
  } else {
    GlobalLog().Warn("optical flow unavailable; running on a zero motion field");
  }

  Pipeline* raw = p.get();
  p->dev_.source = WgcSource::CreateForWindow(config.target, *p->dev_.bridge,
                                              [raw] { raw->stats_.RecordDrop(); });
  if (!p->dev_.source) return nullptr;

  DCompOverlay* overlay = p->dev_.overlay.get();
  p->dev_.tracker = WindowTracker::Create(config.target, [overlay](const RECT& r) {
    overlay->SetBounds(r);
  });
  // A missing tracker is not fatal: the overlay simply will not follow moves.

  return p;
}

Pipeline::~Pipeline() { Stop(); }

HWND Pipeline::OverlayHwnd() const { return dev_.overlay ? dev_.overlay->Hwnd() : nullptr; }

std::string Pipeline::LastError() const {
  std::lock_guard<std::mutex> lock(errorMutex_);
  return lastError_;
}

void Pipeline::FailAndHide(const char* reason) {
  // Spec failure rule: hide first, then record. Never leave an opaque overlay
  // over a live game.
  if (dev_.overlay) dev_.overlay->Hide();
  GlobalLog().Error(reason);
  std::lock_guard<std::mutex> lock(errorMutex_);
  lastError_ = reason;
}

void Pipeline::Panic() noexcept {
  // Overlay first, always. The player must be able to see the game again
  // immediately, before any slower teardown happens.
  if (dev_.overlay) dev_.overlay->Hide();
  if (dev_.hud) dev_.hud->Hide();
  stopRequested_.store(true, std::memory_order_release);
}

bool Pipeline::Rebuild() {
  // Device loss invalidates both devices, the shared ring, the swapchain and
  // every pipeline state, so the only correct response is to build all of it
  // again from the adapter up.
  if (dev_.overlay) dev_.overlay->Hide();

  // The GPU must be finished with everything before any of it is released.
  // The accelerator session is deliberately NOT torn down early here: it comes
  // apart inside ~NvofaFlow, after the textures it registered are gone, which
  // is the only ordering the driver survives.
  DrainGpu();

  // A caller-supplied pass holds no device resources and is carried across. One
  // built from config may hold plenty -- an NGX session, a depth plane -- all of
  // it belonging to the device that just went away, so it is dropped here and
  // rebuilt against the new device by Create().
  auto pass = passFromConfig_ ? nullptr : std::move(dev_.pass);

  // Move out and let the temporary die, rather than assigning an empty state
  // over the top. Move-assignment releases the old members in *declaration*
  // order, which would free the bridge first, while the capture source and
  // overlay still point into it. Destruction runs in reverse, which is the
  // order the dependencies actually require.
  {
    DeviceState dying = std::move(dev_);
  }

  auto rebuilt = Pipeline::Create(gpu_, config_, std::move(pass));
  if (!rebuilt) return false;

  // One move, so a member added later cannot be forgotten here. Stats and the
  // panic switch deliberately stay behind: a reset must not erase the latency
  // history the operator is reading.
  //
  // The HUD comes across with everything else, which matters because
  // Hud::Create points a file-scope pointer at the newest instance; leaving
  // the old one in place would stop it painting.
  dev_ = std::move(rebuilt->dev_);
  return true;
}

bool Pipeline::RebuildAndRestart() {
  // Must run on the thread that owns the windows: Rebuild creates a new
  // overlay, and a window belongs to its creating thread.
  Stop();

  // Retry, because a real driver reset takes time to finish and
  // D3D12CreateDevice can refuse the adapter until it has. This is defensive
  // and deliberately unverified: the only way to simulate removal in-process
  // is ID3D12Device5::RemoveDevice, which poisons the adapter permanently, so
  // no amount of retrying recovers from it and it cannot exercise this path.
  //
  // Blocking the owner's message loop for up to a second is acceptable here:
  // the overlay is already hidden, and the game is a separate process that
  // carries on regardless.
  constexpr int kAttempts = 5;
  constexpr auto kDelay = std::chrono::milliseconds(200);
  bool rebuilt = false;
  for (int attempt = 0; attempt < kAttempts && !rebuilt; ++attempt) {
    if (attempt > 0) std::this_thread::sleep_for(kDelay);
    rebuilt = Rebuild();
  }
  if (!rebuilt) {
    FailAndHide("graphics device was reset and could not be rebuilt");
    return false;
  }
  rebuildRequested_.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_.clear();
  }
  Start();
  return true;
}

ID3D12Device* Pipeline::DeviceForTest() const {
  return dev_.bridge ? dev_.bridge->D3d12() : nullptr;
}

void Pipeline::Start() {
  // Gate on the thread, not on running_: the render loop clears running_ by
  // itself when a frame fails, and a second Start() must not leave the first
  // thread unjoined.
  if (renderThread_.joinable()) return;
  // Recorded here because Start() is called from the thread that created the
  // windows; the render loop needs it to wake that thread on device loss.
  ownerThreadId_ = GetCurrentThreadId();
  stopRequested_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  dev_.source->Start();
  if (config_.showOverlay) dev_.overlay->Show();
  if (dev_.hud && config_.showHud) dev_.hud->Show();
  renderThread_ = std::thread([this] { RenderLoop(); });
}

void Pipeline::Stop() {
  // Always join if there is a thread. The loop may already have exited on a
  // failure and cleared running_, and an unjoined std::thread terminates the
  // process when it is destroyed.
  stopRequested_.store(true, std::memory_order_release);
  if (renderThread_.joinable()) renderThread_.join();
  if (dev_.source) dev_.source->Stop();
  DrainGpu();
  if (dev_.overlay) dev_.overlay->Hide();
  if (dev_.hud) dev_.hud->Hide();
  running_.store(false, std::memory_order_release);
}

void Pipeline::DrainGpu() {
  // Destroying a resource the GPU is still reading is undefined, and with
  // optical flow in the mix it reliably crashed: NVOFA runs on its own queue,
  // so work can still be in flight after the render thread has stopped.
  if (dev_.flow) dev_.flow->WaitForIdle();
  if (!dev_.bridge) return;

  auto* device = dev_.bridge->D3d12();
  if (device->GetDeviceRemovedReason() != S_OK) return;   // never signals again

  ComPtr<ID3D12Fence> drain;
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&drain)))) return;
  if (FAILED(dev_.bridge->Queue()->Signal(drain.Get(), 1))) return;
  if (drain->GetCompletedValue() >= 1) return;

  HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!evt) return;
  if (SUCCEEDED(drain->SetEventOnCompletion(1, evt))) {
    WaitForSingleObject(evt, 2000);
  }
  CloseHandle(evt);
}

void Pipeline::RenderLoop() {
  // RegisterHotKey binds the hotkey to the registering thread's message queue
  // and Pump drains that same queue, so both have to happen here rather than
  // on whichever thread called Start().
  panic_ = PanicSwitch::Create([this] { Panic(); });

  uint64_t sinceHudUpdate = 0;
  uint64_t hudUpdates = 0;
  while (!stopRequested_.load(std::memory_order_acquire)) {
    if (panic_) panic_->Pump();
    if (panic_ && panic_->Triggered()) break;

    const HRESULT reason = dev_.bridge->D3d12()->GetDeviceRemovedReason();
    if (IsDeviceLost(reason)) {
      // Hide first, then hand the rebuild to the owner thread. Rebuilding here
      // would create the overlay window on this thread, which never pumps, so
      // the new window could never be shown or moved.
      FailAndHide("graphics device was reset; waiting for a rebuild");
      rebuildRequested_.store(true, std::memory_order_release);
      // Wake the owner's GetMessage so it notices without polling on a timer.
      if (ownerThreadId_ != 0) PostThreadMessageW(ownerThreadId_, WM_NULL, 0, 0);
      break;
    }

    if (dev_.source->IsClosed()) {
      FailAndHide("capture item closed: the target window went away");
      break;
    }

    auto frame = dev_.bridge->AcquireLatest();
    if (!frame) {
      Sleep(1);
      continue;
    }

    const auto begin = Clock::now();

    // Phase one: everything NVOFA depends on. It runs on its own queue, so its
    // inputs have to be submitted and fenced before it is asked to start.
    dev_.alloc->Reset();
    dev_.cmdList->Reset(dev_.alloc.Get(), nullptr);

    // Widen the captured frame into typed RGBA16F. Nothing consumes it until
    // the neural pass lands in M3, but running it now keeps the format seam
    // exercised rather than discovered later.
    if (dev_.normalize && dev_.normalized) {
      dev_.normalize->Record(dev_.cmdList.Get(), frame->texture, dev_.normalized.Get());
    }

    // NVOFA consumes GRAYSCALE8, so reduce the captured frame to luminance
    // before it goes anywhere near the flow accelerator. The texture rests in
    // COMMON because another queue reads it, and D3D12 requires COMMON for
    // cross-queue access.
    const bool haveLuma = dev_.luminance && dev_.currentLuma && dev_.previousLuma;
    if (haveLuma) {
      D3D12_RESOURCE_BARRIER toWriteLuma{};
      toWriteLuma.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      toWriteLuma.Transition.pResource = dev_.currentLuma.Get();
      toWriteLuma.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      toWriteLuma.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
      toWriteLuma.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      dev_.cmdList->ResourceBarrier(1, &toWriteLuma);

      dev_.luminance->Record(dev_.cmdList.Get(), frame->texture, dev_.currentLuma.Get());

      D3D12_RESOURCE_BARRIER toCommon = toWriteLuma;
      toCommon.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      toCommon.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
      dev_.cmdList->ResourceBarrier(1, &toCommon);
    }

    dev_.cmdList->Close();
    dev_.bridge->Queue()->Wait(dev_.bridge->SharedFence(), frame->fenceValue);
    ID3D12CommandList* first[] = {dev_.cmdList.Get()};
    dev_.bridge->Queue()->ExecuteCommandLists(1, first);

    // A missing motion field is not a frame failure: the pass receives null and
    // treats the scene as static for this frame.
    ID3D12Resource* motion = nullptr;
    FlowOutput flowOut{};
    bool haveFlow = false;
    if (dev_.flow && dev_.flow->Available() && haveLuma && dev_.havePreviousFrame) {
      // Tell NVOFA which fence value means "the luminance is written".
      dev_.bridge->Queue()->Signal(dev_.flow->InputFence(), ++dev_.inputFenceValue);
      haveFlow = dev_.flow->Execute(dev_.currentLuma.Get(), dev_.previousLuma.Get(),
                                dev_.inputFenceValue, flowOut);
    }

    // Phase two: consume the flow grid and present. The GPU waits on NVOFA's
    // output fence, so the render thread never blocks.
    dev_.alloc2->Reset();
    dev_.cmdList2->Reset(dev_.alloc2.Get(), nullptr);

    if (haveFlow && dev_.flowToMv && dev_.motionTarget) {
      dev_.bridge->Queue()->Wait(dev_.flow->OutputFence(), flowOut.readyFenceValue);
      dev_.flowToMv->Record(dev_.cmdList2.Get(), flowOut, dev_.motionTarget.Get());
      motion = dev_.motionTarget.Get();
    }

    // With no mask configured this is exactly the path it always was: the pass
    // writes dev_.workTarget and the overlay presents it. The masked path adds
    // one indirection -- the pass writes a scratch target, and the blend
    // composes that against the original frame into workTarget.
    const bool masked = dev_.uiMask && dev_.neuralTarget;

    // The mask texture is uploaded once, not per frame: it only changes when the
    // configuration does. cmdList2 is open here, which is the cheapest place to
    // record the copy.
    if (masked && !dev_.maskUploaded) {
      std::vector<MaskRect> rects;
      rects.reserve(config_.uiMaskRects.size());
      for (const auto& r : config_.uiMaskRects) {
        rects.push_back(MaskRect{r.left, r.top, r.right, r.bottom});
      }
      const uint32_t sourceWidth =
          config_.uiMaskWidth ? config_.uiMaskWidth : dev_.bridge->Width();
      const uint32_t sourceHeight =
          config_.uiMaskHeight ? config_.uiMaskHeight : dev_.bridge->Height();
      dev_.uiMask->Rasterise(dev_.cmdList2.Get(), rects, sourceWidth, sourceHeight,
                             config_.uiMaskFeather);
      dev_.maskUploaded = true;
    }

    // A pass that names an output format writes the intermediate target and
    // reads the widened frame; one that does not writes the work target and
    // reads the captured frame directly, exactly as before.
    const bool resolving = dev_.pass->OutputFormat() != DXGI_FORMAT_UNKNOWN;
    ID3D12Resource* passTarget =
        (masked || resolving) ? dev_.neuralTarget.Get() : dev_.workTarget.Get();
    ID3D12Resource* passColor =
        resolving ? dev_.normalized.Get() : frame->texture;

    // Each pass declares the state it needs its output in: PassthroughPass
    // copies and wants COPY_DEST, anything driving NGX writes a UAV. The
    // intermediate target rests in UNORDERED_ACCESS, the work target in
    // COPY_SOURCE, because that is how each is consumed afterwards.
    const D3D12_RESOURCE_STATES restState =
        (masked || resolving) ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                              : D3D12_RESOURCE_STATE_COPY_SOURCE;
    const D3D12_RESOURCE_STATES writeState = dev_.pass->OutputState();

    D3D12_RESOURCE_BARRIER toWrite{};
    toWrite.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toWrite.Transition.pResource = passTarget;
    toWrite.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toWrite.Transition.StateBefore = restState;
    toWrite.Transition.StateAfter = writeState;
    if (restState != writeState) dev_.cmdList2->ResourceBarrier(1, &toWrite);

    const bool ok = dev_.pass->Evaluate(dev_.cmdList2.Get(), passColor,
                                    motion, nullptr, passTarget);

    // Back to rest, then on to whatever reads it: the blend samples the
    // intermediate target, the overlay copies from the work target.
    if (restState != writeState) {
      D3D12_RESOURCE_BARRIER back = toWrite;
      back.Transition.StateBefore = writeState;
      back.Transition.StateAfter = restState;
      dev_.cmdList2->ResourceBarrier(1, &back);
    }

    D3D12_RESOURCE_BARRIER toRead = toWrite;
    toRead.Transition.StateBefore = restState;
    toRead.Transition.StateAfter =
        (masked || resolving) ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                              : D3D12_RESOURCE_STATE_COPY_SOURCE;
    if (toRead.Transition.StateBefore != toRead.Transition.StateAfter) {
      dev_.cmdList2->ResourceBarrier(1, &toRead);
    }

    if (masked) {
      // workTarget becomes the blend's UAV output, then goes back to
      // COPY_SOURCE for the present. neuralTarget returns to COPY_SOURCE so the
      // next frame starts from the state this one assumed.
      D3D12_RESOURCE_BARRIER toBlend{};
      toBlend.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      toBlend.Transition.pResource = dev_.workTarget.Get();
      toBlend.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      toBlend.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
      toBlend.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      dev_.cmdList2->ResourceBarrier(1, &toBlend);

      dev_.uiMask->Record(dev_.cmdList2.Get(), frame->texture,
                          dev_.neuralTarget.Get(), dev_.workTarget.Get());

      D3D12_RESOURCE_BARRIER after[2]{};
      after[0] = toBlend;
      after[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      after[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      after[1] = toRead;
      after[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      // Back to whatever this target rests in, which depends on how the pass
      // writes it -- not unconditionally COPY_SOURCE.
      after[1].Transition.StateAfter = restState;
      dev_.cmdList2->ResourceBarrier(2, after);
    }

    dev_.cmdList2->Close();
    if (!ok) {
      // Spec section 11: an unusable neural pass degrades to passthrough and
      // says so. It does not hide the overlay -- that is reserved for failures
      // that break the frame itself.
      //
      // The list is deliberately not executed: after a failed pass its recorded
      // barriers no longer describe reality. Nothing was submitted, so the GPU's
      // actual states still match what the next frame will assume. Rebuilding
      // reuses the device-loss path, which already drains and reconstructs on
      // the thread that owns the windows.
      GlobalLog().Error("the neural pass rejected a frame; rebuilding on passthrough.");
      {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_ = "neural pass failed; fell back to passthrough";
      }
      config_.neuralPass = "passthrough";
      passFromConfig_ = true;
      rebuildRequested_.store(true, std::memory_order_release);
      if (ownerThreadId_ != 0) PostThreadMessageW(ownerThreadId_, WM_NULL, 0, 0);
      break;
    }

    ID3D12CommandList* lists[] = {dev_.cmdList2.Get()};
    dev_.bridge->Queue()->ExecuteCommandLists(1, lists);

    dev_.overlay->Present(dev_.workTarget.Get(), frame->fenceValue);

    // This frame's luminance becomes next frame's reference. Swapping rather
    // than copying keeps both textures alive and costs nothing.
    if (haveLuma) {
      dev_.currentLuma.Swap(dev_.previousLuma);
      dev_.havePreviousFrame = true;
    }

    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - begin);
    stats_.Record(elapsed.count());

    // Refresh roughly twice a second rather than every frame.
    if (dev_.hud && ++sinceHudUpdate >= 30) {
      sinceHudUpdate = 0;
      HudModel model;
      model.p50Ms = stats_.P50();
      model.p99Ms = stats_.P99();
      model.frames = stats_.Count();
      model.drops = stats_.Dropped();
      // Periodically to the log as well, roughly every ten seconds. The HUD is
      // the operator's view, but a bug report needs numbers that survive being
      // pasted into a text box -- and it is the only way to read latency from a
      // headless run.
      if (++hudUpdates % 20 == 0) {
        GlobalLog().Info("latency p50 " + FormatMs(stats_.P50()) + " ms, p99 " +
                         FormatMs(stats_.P99()) + " ms over " +
                         std::to_string(stats_.Count()) + " frames, " +
                         std::to_string(stats_.Dropped()) + " dropped, pass " +
                         dev_.pass->Name());
      }
      model.passName = dev_.pass->Name();
      model.runtimeVariant = dev_.runtimeVariant;
      model.gpuName = dev_.gpuName.c_str();
      dev_.hud->Update(model);
    }
  }
  // UnregisterHotKey must run on the thread that registered it.
  panic_.reset();
  running_.store(false, std::memory_order_release);
}

}  // namespace sidecar
