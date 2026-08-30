#pragma once
#include <windows.h>

#include <functional>
#include <memory>
#include <optional>

namespace sidecar {

struct TargetWindow {
  HWND hwnd = nullptr;
  RECT clientScreen{};
  bool borderless = false;
};

// WoW's top-level window class. Matching by class rather than by process is
// deliberate: it needs no process handle at all (I2).
inline constexpr wchar_t kWowWindowClass[] = L"GxWindowClass";

std::optional<TargetWindow> FindWowWindow();

// Display mode from window styles only. The spec forbids reading WoW's game
// data, so Config.wtf is never parsed (I5).
bool IsBorderless(HWND hwnd);

std::optional<RECT> ClientRectInScreen(HWND hwnd);

// Follows a window through moves and resizes.
//
// The hook is installed WINEVENT_OUTOFCONTEXT. The in-context form maps a DLL
// into the observed process and would destroy the premise of this project (I3).
class WindowTracker {
 public:
  using MovedCallback = std::function<void(const RECT& clientScreen)>;

  static std::unique_ptr<WindowTracker> Create(HWND target, MovedCallback onMoved);
  ~WindowTracker();

 private:
  WindowTracker() = default;

  static void CALLBACK EventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                 LONG objectId, LONG childId,
                                 DWORD threadId, DWORD timestamp);

  HWINEVENTHOOK hook_ = nullptr;
  HWND target_ = nullptr;
  MovedCallback onMoved_;
};

}  // namespace sidecar
