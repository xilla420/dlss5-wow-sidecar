#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>

#include "present/WindowTracker.h"

using namespace sidecar;

// The class list has grown twice from observation, and each addition is a
// chance to capture the wrong application's window. These pin the rule.

TEST_CASE("distinctive WoW classes match on their own", "[unit]") {
  CHECK(WowWindowMatches(L"waApplication Window", L"World of Warcraft"));
  CHECK(WowWindowMatches(L"GxWindowClass", L"World of Warcraft"));
  // A localised client may not be titled in English, so a distinctive class is
  // not made conditional on the title.
  CHECK(WowWindowMatches(L"waApplication Window", L"Monde des Ténèbres"));
  CHECK(WowWindowMatches(L"GxWindowClass", L""));
}

// The 2026-08 retail client registers plain "w", which any application could
// use, so on its own it must not be enough.
TEST_CASE("the generic class needs the title to agree", "[unit]") {
  CHECK(WowWindowMatches(L"w", L"World of Warcraft"));
  CHECK_FALSE(WowWindowMatches(L"w", L"Notepad"));
  CHECK_FALSE(WowWindowMatches(L"w", L""));
  CHECK_FALSE(WowWindowMatches(L"w", nullptr));
}

TEST_CASE("unknown classes never match", "[unit]") {
  CHECK_FALSE(WowWindowMatches(L"Chrome_WidgetWin_1", L"World of Warcraft"));
  CHECK_FALSE(WowWindowMatches(L"", L"World of Warcraft"));
  CHECK_FALSE(WowWindowMatches(nullptr, L"World of Warcraft"));
}

TEST_CASE("class specificity is judged by length", "[unit]") {
  CHECK(ClassNameIsSpecificEnough(L"GxWindowClass"));
  CHECK(ClassNameIsSpecificEnough(L"waApplication Window"));
  CHECK_FALSE(ClassNameIsSpecificEnough(L"w"));
  CHECK_FALSE(ClassNameIsSpecificEnough(L""));
  CHECK_FALSE(ClassNameIsSpecificEnough(nullptr));
}
using namespace std::chrono_literals;

namespace {

HWND MakeWindow(DWORD style) {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SidecarTrackerTarget";
    RegisterClassExW(&wc);
    registered = true;
  }
  return CreateWindowExW(0, L"SidecarTrackerTarget", L"target",
                         style | WS_VISIBLE, 120, 120, 500, 400,
                         nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

void PumpFor(std::chrono::milliseconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  MSG msg{};
  while (std::chrono::steady_clock::now() < deadline) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    std::this_thread::sleep_for(5ms);
  }
}

}  // namespace

TEST_CASE("a WS_POPUP window reads as borderless", "[unit]") {
  HWND hwnd = MakeWindow(WS_POPUP);
  REQUIRE(hwnd != nullptr);
  REQUIRE(IsBorderless(hwnd));
  DestroyWindow(hwnd);
}

TEST_CASE("a titlebar window does not read as borderless", "[unit]") {
  HWND hwnd = MakeWindow(WS_OVERLAPPEDWINDOW);
  REQUIRE(hwnd != nullptr);
  REQUIRE(IsBorderless(hwnd) == false);
  DestroyWindow(hwnd);
}

TEST_CASE("client rect is reported in screen coordinates", "[unit]") {
  HWND hwnd = MakeWindow(WS_POPUP);
  auto rect = ClientRectInScreen(hwnd);
  REQUIRE(rect.has_value());
  REQUIRE(rect->left == 120);
  REQUIRE(rect->top == 120);
  REQUIRE(rect->right - rect->left == 500);
  REQUIRE(rect->bottom - rect->top == 400);
  DestroyWindow(hwnd);
}

TEST_CASE("tracker reports a move", "[unit]") {
  HWND hwnd = MakeWindow(WS_POPUP);
  std::atomic<int> moves{0};
  RECT latest{};

  auto tracker = WindowTracker::Create(hwnd, [&](const RECT& r) {
    latest = r;
    ++moves;
  });
  REQUIRE(tracker != nullptr);

  PumpFor(200ms);
  SetWindowPos(hwnd, nullptr, 300, 260, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  PumpFor(500ms);

  REQUIRE(moves.load() >= 1);
  REQUIRE(latest.left == 300);
  REQUIRE(latest.top == 260);
  DestroyWindow(hwnd);
}

TEST_CASE("FindWowWindow returns nothing when WoW is not running", "[unit]") {
  // The suite does not launch WoW. This asserts the negative path is clean
  // rather than throwing or returning a stale handle.
  bool anyPresent = false;
  for (const wchar_t* className : kWowWindowClasses) {
    if (FindWindowW(className, nullptr) != nullptr) anyPresent = true;
  }
  if (!anyPresent) {
    REQUIRE(FindWowWindow().has_value() == false);
  }
}

TEST_CASE("both the retail and Classic window classes are recognised", "[unit]") {
  // Retail registers "waApplication Window"; missing it is why the runtime
  // failed to see a running game.
  bool retail = false, classic = false;
  for (const wchar_t* className : kWowWindowClasses) {
    if (std::wstring_view(className) == L"waApplication Window") retail = true;
    if (std::wstring_view(className) == L"GxWindowClass") classic = true;
  }
  REQUIRE(retail);
  REQUIRE(classic);
}
