# 0.7.0 verification

Verified on Windows 11 (build 26200) on 2026-11-11, on an NVIDIA RTX PRO 6000
Blackwell Max-Q, driver 610.88.

0.7.0 adds WoW: Forever. The manager's folder detection and install-path check
learn the `_forever_` folder. The overlay needed no code change, because Forever
registers the same window class as Retail.

- Release build: Visual Studio 2022, MSVC 14.44.35207, both NVIDIA SDKs present,
  `SIDECAR_REQUIRE_SDKS=ON`, dxc 1.8.2502.11; no compiler warnings.
- `sidecar_tests.exe "[unit]"`: 236 cases, 7,507 assertions passed.
- `sidecar_tests.exe "[device]"`: 34 cases, 2,626 assertions passed.
- `sidecar_tests.exe "[deviceloss]"`: 1 case, 10 assertions passed, in its own process.
- `python ci/check_translations.py`: passes.
- Import checks: manager, runtime and test pattern have no forbidden imports.
- `wowsidecar.exe` differs from 0.6.1's only by the 6 bytes of the link timestamp.
- The five third-party binaries are the same files as in 0.6.1, with the same hashes.

## Live run

The overlay ran over a live WoW: Forever client (1.60.1.70862) at 2560×1440.
`sidecar.log` shows DLSS 5 310.9.16 armed, about 58 fps presented, and
capture-to-present latency of 7.9 to 8.7 ms p50. The picture was checked by eye.

The top level of the Forever folder holds no injector names. Its own DLLs
(`wow_loader.dll`, `libxell.dll`, `GFSDK_Aftermath_Lib.x64.dll`) are not flagged.

## Packaging

`tools/package_release.ps1` was run on the clean release commit.
`sha256sum -c --ignore-missing` passes for the zip and for every unpacked file.

## Not verified

- Forever on an RTX 40 card.
- **Detect** on a real install with `_forever_` beside the other game folders. The
  machine used here has Forever in a separate folder that Windows has no record
  of, so that path is covered by unit tests only.
- Retail, Classic and 3.3.5a in live play for this release. The overlay's code is
  unchanged from 0.6.1.
