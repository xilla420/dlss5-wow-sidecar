#include "present/WindowTracker.h"

#include <mutex>
#include <unordered_map>

namespace sidecar {
namespace {

std::mutex g_mutex;
std::unordered_map<HWND, WindowTracker*> g_trackers;

}  // namespace

bool IsBorderless(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) return false;
  const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
  // Borderless means no caption and no thick frame. WoW's borderless mode is
  // WS_POPUP; its windowed mode carries WS_CAPTION.
  return (style & WS_CAPTION) == 0 && (style & WS_THICKFRAME) == 0;
}

std::optional<RECT> ClientRectInScreen(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) return std::nullopt;
  RECT client{};
  if (!GetClientRect(hwnd, &client)) return std::nullopt;
  POINT topLeft{client.left, client.top};
  POINT bottomRight{client.right, client.bottom};
  if (!ClientToScreen(hwnd, &topLeft)) return std::nullopt;
  if (!ClientToScreen(hwnd, &bottomRight)) return std::nullopt;
  return RECT{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
}

std::optional<TargetWindow> FindWowWindow() {
  HWND hwnd = FindWindowW(kWowWindowClass, nullptr);
  if (!hwnd || !IsWindow(hwnd)) return std::nullopt;
  auto rect = ClientRectInScreen(hwnd);
  if (!rect) return std::nullopt;

  TargetWindow t;
  t.hwnd = hwnd;
  t.clientScreen = *rect;
  t.borderless = IsBorderless(hwnd);
  return t;
}

std::unique_ptr<WindowTracker> WindowTracker::Create(HWND target, MovedCallback onMoved) {
  if (!target || !IsWindow(target)) return nullptr;

  std::unique_ptr<WindowTracker> t(new WindowTracker());
  t->target_ = target;
  t->onMoved_ = std::move(onMoved);

  DWORD processId = 0;
  const DWORD threadId = GetWindowThreadProcessId(target, &processId);

  // WINEVENT_OUTOFCONTEXT is mandatory (I3): it delivers events to our own
  // process without loading anything into the observed one.
  //
  // The hook is already narrowed to the target's process and thread, so
  // WINEVENT_SKIPOWNPROCESS would buy nothing against WoW while making a
  // target inside this process -- which is how the tracker is tested --
  // impossible to observe.
  t->hook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                             nullptr, &WindowTracker::EventProc,
                             processId, threadId,
                             WINEVENT_OUTOFCONTEXT);
  if (!t->hook_) return nullptr;

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_trackers[target] = t.get();
  }
  return t;
}

WindowTracker::~WindowTracker() {
  if (hook_) UnhookWinEvent(hook_);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_trackers.erase(target_);
}

void CALLBACK WindowTracker::EventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                       LONG objectId, LONG, DWORD, DWORD) {
  if (event != EVENT_OBJECT_LOCATIONCHANGE || objectId != OBJID_WINDOW) return;

  WindowTracker* tracker = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_trackers.find(hwnd);
    if (it == g_trackers.end()) return;
    tracker = it->second;
  }
  if (!tracker || !tracker->onMoved_) return;
  if (auto rect = ClientRectInScreen(hwnd)) tracker->onMoved_(*rect);
}

}  // namespace sidecar
