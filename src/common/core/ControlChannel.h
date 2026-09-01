#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace sidecar {

// How the manager talks to a running overlay.
//
// Two processes, and deliberately the two cheapest mechanisms Windows has for
// the job: a message-only window carries commands one way, and a small block of
// shared memory carries status the other. No pipes, no sockets, no shared
// files -- I10 forbids networking outright, and anything with a connection
// lifecycle would need supervising for no benefit at this size.
//
// The asymmetry is on purpose. Commands are rare, must arrive in order, and
// have to run on the thread that owns the windows, which is exactly what a
// message queue provides. Status is a fast-changing snapshot where the only
// interesting value is the most recent one, which is exactly what a shared
// struct provides.

enum class SidecarCommand : uint32_t {
  None = 0,
  Stop = 1,
  ShowOverlay = 2,
  HideOverlay = 3,
  ShowHud = 4,
  HideHud = 5,
};

// What the manager displays while the overlay runs. Plain old data with fixed
// buffers so it can live in shared memory without any pointer crossing the
// process boundary.
struct SidecarStatus {
  uint32_t sequence = 0;      // odd while a write is in progress; see Read()
  uint32_t processId = 0;
  uint32_t overlayVisible = 0;
  uint32_t hudVisible = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  double p50Ms = 0.0;
  double p99Ms = 0.0;
  uint64_t frames = 0;
  uint64_t drops = 0;

  // Frames actually presented per second, measured across the last reporting
  // window rather than derived from p50. The two are not the same number and
  // the difference is the point: a pipeline that is idle waiting for the game
  // has a fast p50 and a slow frame rate.
  double fps = 0.0;
  // Frames per second Windows Graphics Capture delivered. When this is the
  // lower of the two, the game's own present rate -- or the compositor -- is
  // the ceiling, and no pipeline work can raise it.
  double captureFps = 0.0;

  // Where one frame's wall time goes, averaged over that window. Four numbers
  // with four different causes: starved by the game, our own CPU cost, the
  // compositor pacing us, and the GPU being behind.
  double idleMs = 0.0;
  double recordMs = 0.0;
  double presentWaitMs = 0.0;
  double gpuWaitMs = 0.0;

  // Video memory, in megabytes. When used exceeds budget the driver is evicting
  // to system memory and every frame is waiting on PCIe -- which produces huge
  // frame times with the GPU nearly idle, and is indistinguishable from a slow
  // neural pass unless you are told.
  uint32_t vramUsedMb = 0;
  uint32_t vramBudgetMb = 0;
  // Video memory pushed out to system RAM. Non-zero means the card is full and
  // frames are waiting on PCIe. This is the alarm worth watching; the budget
  // above stays generous until contention actually bites.
  uint32_t vramSpilledMb = 0;
  char passName[64] = {};
  char runtimeVariant[64] = {};
  char lastError[256] = {};
};

// Runtime side. Owns the message-only window and the shared block, and must be
// created on the thread that owns the overlay window -- commands are dispatched
// from that thread's message loop, and showing a window from anywhere else is
// how the device-loss bug happened.
class ControlServer {
 public:
  using Handler = std::function<void(SidecarCommand)>;

  // Null when another overlay already owns the channel, which is the check that
  // stops two runtimes fighting over one screen.
  static std::unique_ptr<ControlServer> Create(Handler handler);
  ~ControlServer();

  void Publish(const SidecarStatus& status);

 private:
  ControlServer() = default;

  HWND hwnd_ = nullptr;
  HANDLE mapping_ = nullptr;
  SidecarStatus* shared_ = nullptr;
  Handler handler_;

  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
};

// Manager side. Every call is a stateless lookup, so the manager can poll it
// each frame without owning anything that would need cleaning up when the
// overlay exits on its own.
namespace control {

bool IsRunning();

// False when there is no overlay listening, or when it did not answer in time.
// Sent rather than posted, with a timeout: the manager needs to know whether a
// Stop was actually received before it redraws the button.
bool Send(SidecarCommand command);

std::optional<SidecarStatus> Read();

}  // namespace control

}  // namespace sidecar
