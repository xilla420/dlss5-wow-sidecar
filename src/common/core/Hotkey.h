#pragma once
#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace sidecar {

// A global key combination, as RegisterHotKey wants it.
//
// Like the panic switch, every hotkey here is RegisterHotKey and nothing else:
// the message lands in this process's own queue, no hook is installed and
// nothing is loaded into the game, so I3 and I4 hold. The one side effect worth
// knowing is that Windows consumes the combination -- WoW never sees it -- which
// is why every default is a Ctrl+Alt chord the game does not bind.
struct Hotkey {
  UINT modifiers = 0;   // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
  UINT vk = 0;

  bool operator==(const Hotkey&) const = default;
};

// "Ctrl+Alt+D", "Shift+F10", "ctrl + alt + home". Case and spaces are ignored.
// A combination needs at least one of Ctrl/Alt/Win: a bare letter would be
// stolen from chat, and Shift alone is how capitals are typed. nullopt for
// anything else, including an empty string -- the caller decides whether empty
// means "disabled".
std::optional<Hotkey> ParseHotkey(std::string_view text);

// The canonical spelling ParseHotkey reads back, e.g. "Ctrl+Alt+D".
std::string FormatHotkey(const Hotkey& hotkey);

// Registers `hotkey` on the calling thread's queue under `id`. WM_HOTKEY then
// arrives through that thread's GetMessage/PeekMessage with a null hwnd.
// False when another application already owns the combination; not fatal.
bool RegisterThreadHotkey(int id, const Hotkey& hotkey);

}  // namespace sidecar
