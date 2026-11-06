# 0.6.1 verification

Verified on Windows 11 (build 26200) on 2026-11-06, on an NVIDIA RTX PRO 6000
Blackwell Max-Q, driver 610.88.

0.6.1 changes no program code. It adds [VERIFYING.md](VERIFYING.md), corrects
where THIRD-PARTY-NOTICES.md says the add-on comes from, and makes packaging
record the toolchain and every file's checksum.

- Release build: Visual Studio 2022, MSVC 14.44.35207, both NVIDIA SDKs present,
  `SIDECAR_REQUIRE_SDKS=ON`, dxc 1.8.2502.11; no compiler warnings.
- `sidecar_tests.exe "[unit]"`: 235 cases, 7,504 assertions passed.
- `sidecar_tests.exe "[device]"`: 34 cases, 2,626 assertions passed.
- `sidecar_tests.exe "[deviceloss]"`: 1 case, 10 assertions passed, in its own process.
- `python ci/check_translations.py`: passes.
- Import checks: manager, runtime and test pattern have no forbidden imports.
- `wowsidecar.exe` is byte-identical to 0.6.0's (SHA-256 `b5a5962a…2e3e`). Only
  the manager has the version compiled in, and it reports 0.6.1.
- The five third-party binaries are the same files as in 0.6.0, with the same hashes.
- A clean rebuild of `v0.6.0` matched both 0.6.0 executables apart from the 6
  timestamp bytes. See [VERIFYING.md](VERIFYING.md#2-rebuild-the-executables).

## Packaging

`tools/package_release.ps1` was run on the clean release commit.
`BUILD-MANIFEST.json` carries the toolchain block, and
`sha256sum -c --ignore-missing` passes for the zip and for every unpacked file.

## Not verified

- The overlay in live WoW for this release. Its executable is unchanged from
  0.6.0, which was verified then.
