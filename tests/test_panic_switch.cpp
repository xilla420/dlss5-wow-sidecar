#include <catch2/catch_test_macros.hpp>
#include <dxgi.h>

#include "core/PanicSwitch.h"

using namespace sidecar;

TEST_CASE("device-loss codes are recognised", "[unit]") {
  REQUIRE(IsDeviceLost(DXGI_ERROR_DEVICE_REMOVED));
  REQUIRE(IsDeviceLost(DXGI_ERROR_DEVICE_RESET));
  REQUIRE(IsDeviceLost(DXGI_ERROR_DEVICE_HUNG));
}

TEST_CASE("ordinary failures are not mistaken for device loss", "[unit]") {
  REQUIRE(IsDeviceLost(S_OK) == false);
  REQUIRE(IsDeviceLost(E_INVALIDARG) == false);
  REQUIRE(IsDeviceLost(E_OUTOFMEMORY) == false);
  REQUIRE(IsDeviceLost(DXGI_ERROR_INVALID_CALL) == false);
}

TEST_CASE("the switch starts untriggered", "[unit]") {
  auto panic = PanicSwitch::Create([] {});
  REQUIRE(panic != nullptr);
  REQUIRE(panic->Triggered() == false);
}

TEST_CASE("a posted hotkey message fires the callback exactly once", "[unit]") {
  int fired = 0;
  auto panic = PanicSwitch::Create([&] { ++fired; });
  REQUIRE(panic != nullptr);

  // PostThreadMessage fails against a thread that has never had a message
  // queue, and this test may run first. Peeking forces one into existence.
  MSG probe{};
  PeekMessageW(&probe, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

  // Simulate the hotkey by posting the same message the OS would send.
  REQUIRE(PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY,
                             PanicSwitch::kHotkeyId, 0));
  panic->Pump();
  REQUIRE(fired == 1);
  REQUIRE(panic->Triggered());

  panic->Pump();          // no further message
  REQUIRE(fired == 1);
}
