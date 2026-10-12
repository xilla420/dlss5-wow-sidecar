# 0.5.0 verification

Verified on Windows 11 (build 26200) on 2026-10-12, on an NVIDIA RTX PRO 6000
Blackwell at 2560x1440, 240 Hz.

- Release build: Visual Studio 2022, both NVIDIA SDKs present,
  `SIDECAR_REQUIRE_SDKS=ON`; no compiler warnings.
- `sidecar_tests.exe "[unit]"`: 221 cases, 7,096 assertions passed.
- `sidecar_tests.exe "[device]"`: 31 cases, 2,486 assertions passed.
- `sidecar_tests.exe "[deviceloss]"`: 1 case, 10 assertions passed, in its own process.
- `python ci/check_translations.py`: all nine translated tables valid, no stale keys.
- Import checks: manager, runtime and test pattern have no forbidden imports.
- `python -m pytest ci` was not run locally (pytest is not installed on the
  verification machine); CI runs it on every push to main.

## WoW 3.3.5a, live

Client: 3.3.5a build 12340 (a private-server distribution), Direct3D 9, on its
login screen. Signing in was not part of the test.

- The window registers as `GxWindowClassD3d`, titled "World of Warcraft". With
  **Windowed Mode** and **Maximized** ticked it is `WS_POPUP` with no caption, which
  the borderless check accepts.
- With 3.3.5a selected, all ten checks were green, including the new WoW folder
  row (`Wow.exe` and `Data/common.MPQ` found) and the window row ("2560x1440
  windowed, maximized"). The Ready to play list ticked steps 1-3 by itself.
- The overlay found the window, armed DLSS 5 neural rendering, and presented
  58-60 fps of 60 captured; the game held 59.8 fps under it. No errors or
  warnings in `sidecar.log`.
- Captures of the overlay and of the game window were non-black and visibly
  different (smoother, slightly less saturated with the overlay on).

Frame-rate investigation, game frames delivered with the overlay running and the
game in the foreground:

| 3.3.5a setting | game frame rate |
|---|---|
| `maxFPS 60` | 35 fps |
| `maxFPS 120` | 60 fps |
| uncapped | 60 fps |
| vsync on | 60 fps |

One earlier vsync run dropped to 30 fps for a stretch and did not reproduce.
With the GPU saturated by a synthetic load, a high-priority D3D12 queue gave the
same 52-53 fps as a normal one, so the priority was left as it was.

## Interface

Captured with `tools/capture_manager.ps1` (PrintWindow on the manager only) in
all three themes, and in English, Russian, Japanese, Arabic, Chinese and Korean.
Arabic text beside the welcome art was found wrapping one letter per line and
fixed; Japanese and Russian labels were found clipping in the rail and fixed.

## Not verified

- 3.3.5a in the world (only the login screen), with DXVK (`d3d9.dll`), or with
  the OpenGL renderer.
- Lower-end RTX 40 cards; every figure above is from one workstation card.
- New translations have not had native-speaker review.
