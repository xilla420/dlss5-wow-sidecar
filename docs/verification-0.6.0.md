# 0.6.0 verification

Verified on Windows 11 (build 26200) on 2026-10-20, on an NVIDIA RTX PRO 6000
Blackwell.

- Release build: Visual Studio 2022, both NVIDIA SDKs present,
  `SIDECAR_REQUIRE_SDKS=ON`; no compiler warnings.
- `sidecar_tests.exe "[unit]"`: 235 cases, 7,506 assertions passed.
- `sidecar_tests.exe "[device]"`: 34 cases, 2,626 assertions passed.
- `sidecar_tests.exe "[deviceloss]"`: 1 case, 10 assertions passed, in its own process.
- `python ci/check_translations.py`: all nine translated tables valid, no stale keys.
- Import checks: manager, runtime and test pattern have no forbidden imports.

## DLSS 5 runtimes

`nvngx_dlssnr.dll` 310.9.16.0 carries a valid NVIDIA Authenticode signature
(SHA-256 `2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f`).
The Ada-patched 310.8.0 (`e67dee20…c989a`) is the build 0.5.0 shipped.

Against `testpattern` at 1280x720, overlay running, same card, each build run
twice:

| Build | GPU wait | Latency p50 / p99 | Sidecar video memory |
|---|---|---|---|
| 310.8.0, Ada-patched | 2.84-2.87 ms | 4.24 / 4.70 ms | ~709 MB |
| 310.9.16, stock | 1.69-1.78 ms | 3.00 / 3.33 ms | ~647 MB |

The test pattern caps both at about 32 fps, so neither run is GPU-bound and the
frame rate is the same; the difference is GPU time per frame.

Each `neural_runtime` mode was run with both slots filled: `compatible` staged
310.8.0, `auto` and `latest` staged 310.9.16, and each logged the build it ran.
Re-running a mode logged "already in place" and did not copy again.

Staging fails, and says so, when another process has the loose runtime mapped.
That happens when `testpattern.exe` runs from the sidecar folder, because ReShade
loads into it from there; the game never runs from that folder.

## Interface

Setup, Checks and Tuning captured with `tools/capture_manager.ps1` (PrintWindow on
the manager only) in English and Japanese. Setup lists both runtime slots with
their versions; Tuning shows the version on each runtime card and in the "In use"
line.

## Not verified

- 310.9.16's image quality in live WoW, against 310.8.0.
- The runtime picker on an RTX 40 card; on Ada the Latest card is disabled by
  design, and Automatic resolves to Compatible in unit tests only.
- The ghosting fix in live WoW on this card; it was measured with the camera-pan
  test in `tests/test_temporal_stability.cpp`.
- New translations have not had native-speaker review.
