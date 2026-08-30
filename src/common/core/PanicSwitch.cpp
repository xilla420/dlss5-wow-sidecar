#include "core/PanicSwitch.h"

#include <dxgi.h>

namespace sidecar {

bool IsDeviceLost(HRESULT hr) {
  return hr == DXGI_ERROR_DEVICE_REMOVED ||
         hr == DXGI_ERROR_DEVICE_RESET ||
         hr == DXGI_ERROR_DEVICE_HUNG;
}

std::unique_ptr<PanicSwitch> PanicSwitch::Create(std::function<void()> onPanic) {
  std::unique_ptr<PanicSwitch> p(new PanicSwitch());
  p->onPanic_ = std::move(onPanic);

  // Ctrl+Alt+Backspace. Chosen because WoW binds neither it nor anything near
  // it, and because it is awkward enough not to be hit by accident mid-pull.
  p->registered_ = RegisterHotKey(nullptr, kHotkeyId,
                                  MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                                  VK_BACK) != 0;
  // A failed registration is not fatal: another application may already own
  // the combination. The manager surfaces this rather than the runtime dying.
  return p;
}

PanicSwitch::~PanicSwitch() {
  if (registered_) UnregisterHotKey(nullptr, kHotkeyId);
}

void PanicSwitch::Pump() {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
    if (msg.wParam != kHotkeyId) continue;
    if (triggered_.exchange(true, std::memory_order_acq_rel)) continue;
    if (onPanic_) onPanic_();
  }
}

}  // namespace sidecar
