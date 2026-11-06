# Verifying a release

A release zip holds two kinds of file: the two executables, which are built from
this repository, and third-party binaries, which are not. This page covers how to
check each.

Releases are built locally, not by CI. The tag-triggered workflow needs the NVIDIA
Optical Flow SDK, which cannot be redistributed, so it stops on purpose when the
`NVOF_SDK_URL` secret is missing.

## 1. Check the download

Each release has a `SHA256SUMS-vX.Y.Z.txt` asset, and GitHub shows a `sha256:`
digest for every asset. Both should match the zip:

```powershell
Get-FileHash dlss5-wow-sidecar-v0.6.0.zip -Algorithm SHA256
```

In releases after 0.6.0, the same file also lists every file in the zip, by its path once
unpacked. After you unpack the zip next to it:

```sh
sha256sum -c --ignore-missing SHA256SUMS-vX.Y.Z.txt
```

Inside the zip, `BUILD-MANIFEST.json` records the source commit and the size and
SHA-256 of every file. In releases after 0.6.0 it also records the toolchain used for the
build: CMake, the MSVC toolset, the Windows SDK, `dxc`, and both NVIDIA SDKs.

## 2. Rebuild the executables

`wowsidecar.exe` and `wowsidecar-manager.exe` can be rebuilt from the tag and
compared byte for byte. The build is deterministic except for the linker
timestamp, and neither executable embeds a build path or a PDB path, so the
folder you build in does not matter.

You need:

- Visual Studio 2022 with the C++ workload. CMake comes with it.
- The Windows SDK whose `dxc` built the release (see below). The shaders are
  compiled into `wowsidecar.exe`, so a different `dxc` gives different bytes.
- The NVIDIA DLSS SDK ([github.com/NVIDIA/DLSS](https://github.com/NVIDIA/DLSS)), checked out at the release's tag.
- The NVIDIA Optical Flow SDK, from NVIDIA's developer site (needs a free account).

```bat
git clone https://github.com/xilla420/dlss5-wow-sidecar
cd dlss5-wow-sidecar
git checkout v0.6.0
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSIDECAR_REQUIRE_SDKS=ON ^
  -DDLSS_SDK_DIR="<DLSS SDK at the release's tag>" ^
  -DNVOF_SDK_DIR="<Optical Flow SDK>" ^
  -DDXC_EXECUTABLE="C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe"
cmake --build build --config Release --target wowsidecar wowsidecar-manager
fc /b build\Release\wowsidecar.exe <unpacked release>\wowsidecar.exe
fc /b build\Release\wowsidecar-manager.exe <unpacked release>\wowsidecar-manager.exe
```

Each `fc` should report exactly 6 differing bytes, in two runs of 3. Those are
the two copies of the link timestamp: `TimeDateStamp` in the PE header, and again
in the debug directory. Anything else differing means the toolchain differs or
the binary is not what the tag builds.

If you leave out `-DDXC_EXECUTABLE`, CMake uses the newest `dxc` it can find, and
configuring prints a warning when that is not the release's version. With dxc 1.9
(Windows SDK 10.0.28000.0), `wowsidecar.exe` differs from 0.6.0 by about 750
bytes, all inside the embedded shaders. The manager has no shaders and matches
either way.

### Toolchains used

| Release | Source | MSVC toolset | Windows SDK | dxc | DLSS SDK | Optical Flow SDK |
|---|---|---|---|---|---|---|
| 0.6.0 | `v0.6.0` (`c09bea5`) | 14.44.35207 (`cl` 19.44.35228) | 10.0.28000.0 | 1.8.2502.11, from SDK 10.0.26100.0 | v310.7.0 (`a291cc7`) | 5.0.7 |
| 0.6.1 | `v0.6.1` | 14.44.35207 (`cl` 19.44.35228) | 10.0.28000.0 | 1.8.2502.11, from SDK 10.0.26100.0 | v310.7.0 (`a291cc7`) | 5.0.7 |

Only the manager has the version compiled in, so 0.6.1's `wowsidecar.exe` is
byte-identical to 0.6.0's.

All of them used CMake 3.31.6 (the copy that comes with Visual Studio), the
generator `Visual Studio 17 2022`, x64 Release, and `SIDECAR_REQUIRE_SDKS=ON`.
Catch2, toml++ and Dear ImGui are fetched at the tags pinned in `CMakeLists.txt`.
In releases after 0.6.0, `BUILD-MANIFEST.json` carries this row for its own build.

For 0.6.0 this was checked on 2026-11-06: a clean build of `v0.6.0` with the
toolchain above matched both shipped executables, apart from the 6 timestamp bytes.

## 3. The third-party binaries

None of these is built here, so they can only be checked by origin, signature
and hash. [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) has the licences.
0.6.0 and 0.6.1 ship the same files:

| File | SHA-256 | Origin | Authenticode |
|---|---|---|---|
| `runtimes\latest\nvngx_dlssnr.dll` | `2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f` | NVIDIA 310.9.16.0, unmodified | NVIDIA, valid |
| `runtimes\compatible\nvngx_dlssnr.dll` | `e67dee209320cdafe0e93e45675d7aa34323a53acc57a72b2e40a181581c989a` | NVIDIA 310.8.0, patched by a third party to run on RTX 40 | `HashMismatch`: NVIDIA's signature no longer matches the modified file |
| `nvngx_dlss.dll` | `c85f971ce023c9f3492fc7455f0b01a24ba18ea39636407a846902c4360b0b7e` | NVIDIA 310.8.0 | NVIDIA, valid |
| `dxgi.dll` | `0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7` | ReShade 6.8.0.2155, add-on build | unsigned |
| `renodx-dlss5.addon64` | `e1c28fde0922b12fc10734e58c3d24a36808e575247f4fd4f36226540d7ee023` | RenoDX development build, from [Nexus Mods mod 2224](https://www.nexusmods.com/site/mods/2224) | unsigned |

Check a signature with:

```powershell
Get-AuthenticodeSignature .\runtimes\latest\nvngx_dlssnr.dll
```

The add-on is not an official RenoDX release, and no upstream checksum exists for
it. [Where the add-on comes from](../THIRD-PARTY-NOTICES.md#where-the-add-on-comes-from)
has the details. On an RTX 50 card the default runtime setting uses the
unmodified, signed 310.9.16, so the patched runtime is not loaded. If you would
rather not run the add-on or the patched runtime, delete them, and the manager's
**Setup** tab will take copies you supply.

## Antivirus results

Neither executable is code-signed, and unsigned tools with few downloads draw
heuristic detections. Neither one can use the network: the build fails if either
imports a networking API (invariant I10, checked by `ci/check_imports.py` after every build). The add-on
hooks NGX functions inside the process, which is the kind of behaviour antivirus
heuristics flag. A rebuild that matches, as in step 2, is the strongest evidence
this page can offer that the two executables are what the source says.
