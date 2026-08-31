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

// WoW's top-level window classes, observed rather than documented, which is why
// this list has grown twice. Retail has registered "waApplication Window" and,
// as of the 2026-08 client, plain "w"; older and Classic clients use
// "GxWindowClass". Matching by class rather than by process is deliberate: it
// needs no process handle at all (I2).
inline constexpr const wchar_t* kWowWindowClasses[] = {
    L"waApplication Window",
    L"GxWindowClass",
    L"w",
};

// The window title WoW gives its main window, used to disambiguate the classes
// that are too generic to stand alone.
inline constexpr const wchar_t* kWowWindowTitle = L"World of Warcraft";

// Whether a class name is specific enough to identify WoW by itself.
//
// "w" plainly is not -- any application could register it -- so a window of that
// class is only accepted when its title matches as well. The longer names are
// distinctive enough that a title check would only risk rejecting a localised
// client for no gain.
//
// Pure, and unit-tested, because getting it wrong either misses the game
// entirely or captures somebody else's window.
bool ClassNameIsSpecificEnough(const wchar_t* className);

// Given what can be read from a window without opening its process, does this
// look like WoW's main window?
bool WowWindowMatches(const wchar_t* className, const wchar_t* title);

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
