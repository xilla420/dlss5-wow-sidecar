#include <catch2/catch_test_macros.hpp>

#include "core/Config.h"
#include "core/Hotkey.h"

using namespace sidecar;

TEST_CASE("hotkeys parse the way a player writes them", "[unit]") {
  const auto d = ParseHotkey("Ctrl+Alt+D");
  REQUIRE(d);
  REQUIRE(d->modifiers == (MOD_CONTROL | MOD_ALT));
  REQUIRE(d->vk == 'D');

  // Case and spacing do not matter.
  REQUIRE(ParseHotkey(" ctrl + alt + d ") == d);
  REQUIRE(ParseHotkey("Alt+Ctrl+D") == d);

  const auto f = ParseHotkey("Shift+Alt+F9");
  REQUIRE(f);
  REQUIRE(f->vk == VK_F9);
  REQUIRE(f->modifiers == (MOD_SHIFT | MOD_ALT));

  REQUIRE(ParseHotkey("Ctrl+Alt+Home")->vk == VK_HOME);
  REQUIRE(ParseHotkey("Win+7")->vk == '7');
  REQUIRE(ParseHotkey("Ctrl+Alt+Backspace")->vk == VK_BACK);
}

TEST_CASE("combinations that would steal typing are refused", "[unit]") {
  REQUIRE_FALSE(ParseHotkey(""));
  REQUIRE_FALSE(ParseHotkey("D"));            // a bare letter eats chat
  REQUIRE_FALSE(ParseHotkey("Shift+D"));      // that is a capital D
  REQUIRE_FALSE(ParseHotkey("Ctrl+Alt"));     // no key
  REQUIRE_FALSE(ParseHotkey("Ctrl+D+E"));     // two keys
  REQUIRE_FALSE(ParseHotkey("Ctrl++D"));
  REQUIRE_FALSE(ParseHotkey("Ctrl+Alt+"));
  REQUIRE_FALSE(ParseHotkey("Ctrl+Alt+F25"));
  REQUIRE_FALSE(ParseHotkey("Hyper+D"));
}

TEST_CASE("a formatted hotkey parses back to itself", "[unit]") {
  for (const char* text : {"Ctrl+Alt+D", "Ctrl+Alt+H", "Alt+Shift+F12", "Ctrl+Win+PageUp",
                           "Ctrl+Alt+Backspace", "Alt+0"}) {
    const auto parsed = ParseHotkey(text);
    REQUIRE(parsed);
    REQUIRE(FormatHotkey(*parsed) == text);
    REQUIRE(ParseHotkey(FormatHotkey(*parsed)) == parsed);
  }
  REQUIRE(FormatHotkey(*ParseHotkey("alt+ctrl+del")) == "Ctrl+Alt+Delete");
}

TEST_CASE("hotkeys default on, round-trip, and survive a typo", "[unit]") {
  std::vector<std::string> warnings;
  const Config defaults = ParseConfig("", warnings);
  REQUIRE(defaults.hotkeys.toggleOverlay == "Ctrl+Alt+D");
  REQUIRE(defaults.hotkeys.toggleHud == "Ctrl+Alt+H");
  REQUIRE(defaults.hotkeys.startStop == "Ctrl+Alt+S");

  const Config custom = ParseConfig(
      "[hotkeys]\n"
      "toggle_overlay = \"shift + alt + f9\"\n"
      "toggle_hud = \"\"\n"
      "start_stop = \"not a key\"\n",
      warnings);
  REQUIRE(custom.hotkeys.toggleOverlay == "Alt+Shift+F9");
  REQUIRE(custom.hotkeys.toggleHud.empty());           // deliberately off
  REQUIRE(custom.hotkeys.startStop == "Ctrl+Alt+S");   // typo keeps the default
  REQUIRE(warnings.size() == 1);

  warnings.clear();
  const Config back = ParseConfig(SerializeConfig(custom), warnings);
  REQUIRE(warnings.empty());
  REQUIRE(back.hotkeys.toggleOverlay == custom.hotkeys.toggleOverlay);
  REQUIRE(back.hotkeys.toggleHud.empty());
  REQUIRE(back.hotkeys.startStop == custom.hotkeys.startStop);
}
