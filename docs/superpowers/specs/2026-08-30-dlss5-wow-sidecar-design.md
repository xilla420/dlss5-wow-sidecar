# DLSS 5 Neural Rendering Sidecar for World of Warcraft — Design

**Date:** 2026-08-30
**Status:** Approved design, pre-implementation
**Target:** Windows 11 x64, NVIDIA RTX 40 (Ada) and RTX 50 (Blackwell)

## 1. Summary

An out-of-process application that applies NVIDIA DLSS 5 Neural Rendering to
World of Warcraft Retail without loading any code into `Wow.exe`.

The sidecar captures WoW's window through the Windows Graphics Capture API,
synthesises the inputs DLSS requires (motion vectors from the NVIDIA Optical
Flow Accelerator, a constant depth plane), runs an NGX DLAA evaluation inside
its own process where the DLSS 5 neural pass intercepts it, and presents the
result in a click-through DirectComposition overlay positioned exactly over the
game window.

The game process is never opened, never read, never written, and never hooked.
This is the entire premise of the project and every other decision is
subordinate to it.

## 2. Context and prior art

DLSS 5 is neural rendering, not upscaling. The runtime is `nvngx_dlssnr.dll`,
identified as DLSSNR 310.8.0.0. It re-lights and re-materialises surfaces,
predominantly character models. NVIDIA has not shipped it publicly; the current
target is autumn 2026 and RTX 50 exclusivity at launch. Driver 610.47 already
carries `Enable DLSS NR Override` and `Override DLSS NR Presets` profile keys
with no public runtime behind them.

The runtime leaked on 26 August 2026 in an NBA 2K27 early-access build. Within
hours, community modders had it running in Control, GTA 5 and Skyrim. A
community build patches the incompatible CUDA instructions so it runs on Ada.

Every published tool for enabling it works the same way: a ReShade add-on
detours `NVSDK_NGX_D3D12_CreateFeature` and `NVSDK_NGX_D3D12_EvaluateFeature`
and inserts the neural pass. The relevant projects are `rakanki911/DLSS5-Swapper`
(D3D12 games with native DLSS), `NIGos/dlss5-dx11-bridge` (D3D11 games with
DLSS, via a private D3D12 NGX session), `jlrouzies-fr/DLSS5-Feeder` (games with
no DLSS at all, synthesising a DLAA contract from ReShade depth and estimated
motion vectors), and `NIGos/dlss5-d3d12-fix` (texture format corrections).

None of them can be used on WoW.

World of Warcraft has no native DLSS of any kind — only NVIDIA Reflex and FSR 1.0
render scaling. There is therefore no NGX call inside `Wow.exe` to detour. More
decisively, Blizzard's Terms of Use prohibit third-party client modification and
Warden enforces it: patch 11.1.7 (14 July 2025) explicitly rejected ReShade's
`dxgi.dll`, and Blizzard has issued ban waves and blue posts naming ReShade, with
penalties up to permanent account closure.

DLSS5-Feeder is architecturally the closest fit and is unusable, because it is a
ReShade add-on and ReShade cannot go near `Wow.exe`.

This design takes DLSS5-Feeder's insight — synthesise the DLAA contract, run NGX
on a private device — and relocates the entire thing outside the game process.

## 3. Non-goals

- Upscaling. We run DLAA only: render size equals output size, zero jitter.
  Without a real depth buffer or a jitter sequence, any upscale ratio produces
  garbage.
- Frame generation.
- Ray reconstruction.
- Support for RTX 20/30, AMD, or Intel.
- Any modification, however cosmetic, to WoW's install directory.
- Competitive-latency play. See §9.

## 4. Hard constraints

**C1. WoW must run borderless windowed.** WGC window capture reads DWM's
redirection surface. Exclusive fullscreen has no redirection surface and yields
black frames.

**C2. The overlay cannot be a layered window.** `WS_EX_LAYERED` cannot host a
DXGI flip swapchain. The correct construction is `WS_EX_NOREDIRECTIONBITMAP`
plus a DirectComposition visual tree, which is also the lowest-latency
transparent-overlay path Windows offers.

**C3. WoW's UI is composited into the captured frame** and no API exposes a mask
for it. Addressed in §12.

**C4. Refresh rate dominates playability.** The pipeline floor is two DWM
vblanks. At 60 Hz that is 33 ms before any work; at 144 Hz it is 13.9 ms.

**C5. The DLSS 5 runtime is not redistributable.** It is a user-supplied drop-in,
verified by hash. Nothing unredistributable enters the repository.

## 5. Safety model

The safety claim is mechanical, not a promise. These invariants are enforced by
a CI job that dumps each binary's import table and fails the build on any
forbidden symbol.

**I1.** No `ReadProcessMemory`, `WriteProcessMemory`, `VirtualAllocEx`,
`VirtualProtectEx`, or `CreateRemoteThread` in the import table of either binary.

**I2.** `OpenProcess` is never requested above `PROCESS_QUERY_LIMITED_INFORMATION`.

**I3.** `SetWinEventHook` is used only with `WINEVENT_OUTOFCONTEXT`. The
in-context form maps a DLL into the target process and would destroy the premise.

**I4.** `SetWindowsHookEx` is never used in any form.

**I5.** Never write to WoW's install directory; never read WoW's game data.
WoW's display mode is determined from window styles via `GetWindowLong`,
out-of-process, rather than by parsing `Config.wtf`.

**I6.** Never synthesise input to WoW's window.

**I7.** The installer hard-refuses any target path containing a WoW install
signature (`Wow.exe`, `_retail_`, `_classic_`, `_classic_era_`).

**I8.** On every launch, the manager enumerates *filenames only* in WoW's install
directory looking for known injector loaders — `dxgi.dll`, `d3d12.dll`,
`d3d11.dll`, `dinput8.dll`, `winmm.dll`, `version.dll`, `opengl32.dll` — and for
ReShade configuration files. If any are present, the sidecar refuses to launch
and tells the user to remove them first.

This is the one invariant that reads WoW's directory, and it is deliberate: it is
a filename enumeration with no file contents read, and its entire purpose is to
stop the user from being banned for something they did before installing this
tool. I5 prohibits reading WoW's *game data*; a safety scan for foreign
injectors is the opposite of that.

**I9.** The sidecar's own directory is re-verified at every launch to be outside
WoW's install tree. I7 covers installation; I9 covers a user who moved things
afterwards.

**I10.** Neither binary makes any network connection, ever. No telemetry, no
update check, no download. The hash manifest is compiled in, not fetched. The CI
job asserts no `ws2_32`, `winhttp`, `wininet` or `urlmon` imports in either
binary. A tool that downloads DLLs onto a gamer's machine is a supply-chain
liability and looks exactly like malware; this one cannot.

**I11.** The project distributes no third-party binaries. Not the neural runtime,
not the Ada patch, not ReShade. The user supplies them; we verify and refuse
unknown hashes.

**I12.** Never behave the way a cheat behaves. No packing, no obfuscation, no
anti-debug, no process-name randomisation, no privilege elevation beyond a normal
user token, no attempt to hide from process enumeration. Warden can see
`wowsidecar.exe` in a process list and that is fine — what gets software flagged
is looking like it is hiding. Legitimate software behaves legibly, and this is
legitimate software.

**I13.** A panic hotkey hides the overlay and stops the pipeline instantly,
without needing focus to leave the game.

ReShade, when used for route B (§11), is loaded into the *sidecar's* process and
placed in the *sidecar's* directory. It never goes near the game.

### Ban-risk analysis

Blizzard enforces against client modification and automation. Every vector they
are known to act on is addressed by construction rather than by policy:

| Enforcement vector | Applies here? | Why |
|---|---|---|
| Code injected into `Wow.exe` | No | I1–I4. Nothing of ours is ever loaded into the game process. |
| Client memory read or written | No | I1, I2. We never open a handle capable of it. |
| Modified or added client files | No | I5, I7, I9. We never write to the install tree, and refuse to install into it. |
| Input automation, botting | No | I6. We never synthesise input. The sidecar cannot act. |
| Reading game state for advantage | No | We see only the pixels already on the player's screen, after the game has drawn them. No information the player did not already have. |
| Known-bad process signature | No | I12. We do nothing to hide, and we are not a cheat. |
| Screen capture of the game window | Not enforced | WGC is the same OS API behind OBS, Discord and Xbox Game Bar. Enforcing against it would ban most of the streaming population. |

**Residual risk, stated honestly.** Two things remain, and neither can be
engineered away:

1. **Blizzard could change what they enforce.** This design removes every
   currently-known detection vector, which is the strongest position available —
   but it is not a guarantee, and nobody can offer one. If Blizzard's policy
   moves, the honest response is to stop shipping, not to evade.
2. **The user can still ban themselves.** The most plausible path to an account
   closure for someone holding this tool is putting ReShade next to `Wow.exe` on
   their own initiative. I7 makes that awkward at install time and I8 makes it
   blocking at launch time, but a determined user can still do it. The manager
   says so in plain words on first run.

The project claims what it can support: *this sidecar does not do any of the
things Blizzard bans people for.* It does not claim that installing it is
risk-free, because that claim would not be true of any third-party tool.

### Failure behaviour

**On any pipeline failure, hide the overlay immediately.** The overlay is an
opaque window covering a live game. A broken pipeline that keeps presenting
leaves the player blind mid-encounter. Fail to a visible game, never to black.
This rule outranks every other error-handling consideration.

## 6. Toolchain and repository layout

C++20, CMake (3.28+), MSVC v143, x64 only. Every SDK in this stack — D3D12,
NGX, the NVIDIA Optical Flow SDK, WGC/WinRT via C++/WinRT, DirectComposition —
is C or C++ first; Rust or C# would be pure FFI overhead for no gain.

Third-party code is vendored through CMake `FetchContent`: Dear ImGui for the
manager, `toml++` for config, and Catch2 for tests. The NGX headers come from
the public NVIDIA DLSS SDK. The DLSS 5 neural runtime is never vendored (C5).

```
CMakeLists.txt
src/runtime/        wowsidecar.exe
src/manager/        wowsidecar-manager.exe
src/common/         shared code (core/, gpu/, safety/)
src/testpattern/    testpattern.exe
tests/              Catch2 unit tests
ci/                 import-table invariant checker
docs/superpowers/specs/
third_party/        vendored via FetchContent, not checked in
```

## 7. Architecture

One runtime executable, `wowsidecar.exe`, plus one manager executable,
`wowsidecar-manager.exe` (§13). Neither loads anything into `Wow.exe`.

Three threads in the runtime:

- **Capture** — WinRT `GraphicsCaptureItem` created from WoW's HWND,
  `Direct3D11CaptureFramePool::CreateFreeThreaded`, frame-arrived callback.
  Owns a D3D11 device.
- **Render** — owns the D3D12 device. Optical flow, neural pass, present.
- **Control** — hotkeys, config hot-reload, window tracking, HUD.

### The D3D11 to D3D12 seam

WGC produces D3D11 textures. NGX and NVOFA consume D3D12 resources. Two devices
are created on the same adapter, matched by LUID, and bridged by a three-deep
ring of textures created on the D3D11 side with
`D3D11_RESOURCE_MISC_SHARED_NTHANDLE` and opened on the D3D12 side with
`ID3D12Device::OpenSharedHandle`.

Synchronisation uses a shared fence pair (`ID3D11Device5::CreateFence` with
`D3D11_FENCE_FLAG_SHARED`, opened as an `ID3D12Fence`). A keyed mutex is
explicitly rejected: it serialises the two queues and costs latency the budget
cannot absorb.

### Presentation

`WS_POPUP` window with extended styles
`WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW`.
A DirectComposition device and visual tree target the window;
`IDXGIFactory2::CreateSwapChainForComposition` supplies the swapchain with
`DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, `SetMaximumFrameLatency(1)` and
waitable-object pacing.

`WS_EX_TRANSPARENT` provides click-through. `WS_EX_NOACTIVATE` ensures WoW keeps
focus, so it never drops to its background frame-rate limit.

Window tracking uses `SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, …,
WINEVENT_OUTOFCONTEXT)` to follow WoW's client rect through moves and resizes.

## 8. Modules

```
capture/    WgcSource          HWND to shared D3D12 texture ring
gpu/        DeviceBridge       D3D11 + D3D12 devices, shared fence, ring allocation
            FormatNormalize    BGRA8 to RGBA16F
flow/       NvofaFlow          NV_OF_D3D12, S10.5 grid flow
            FlowToMotionVec    unpack, upscale, sign and scale, RG16F
depth/      ConstantDepth      full-res R32F constant, Depth_Inverted flag
neural/     INeuralPass        Evaluate(color, mv, depth) -> color
            PassthroughPass    milestone 0, no NGX
            ReshadeHostedPass  route B, primary
            DirectNgxPass      route A, clean-room upgrade
mask/       UiMask             user rectangles plus temporal-stability heuristic
present/    DCompOverlay       NOREDIRECTIONBITMAP window, DComp, flip swapchain
            WindowTracker      out-of-context WinEvent hook
core/       FrameClock         pacing, latency accounting, HUD counters
            Config             TOML, hot-reload
            GpuProfile         adapter detection, per-architecture selection
safety/     Invariants         runtime guards mirroring the CI import check
```

Only `neural/` touches anything unredistributable.

### Two modules that carry disproportionate risk

**`FlowToMotionVec`.** NVOFA emits S10.5 fixed-point flow on a 1x, 2x or 4x grid,
and its direction (current-to-previous versus previous-to-current) is
configuration-dependent. NGX expects pixels or NDC governed by
`NVSDK_NGX_Parameter_MV_Scale_X` and `_Y`. A wrong sign or scale produces output
that smears under motion — a failure that presents as a quality problem rather
than a bug, and can therefore survive a long time undiagnosed. It gets a unit
test against synthetic image pairs with known translation before it ever sees WoW.

**`FormatNormalize`.** The existence of `NIGos/dlss5-d3d12-fix` — which exists
solely to correct typeless-to-typed output mismatches — indicates the DLSS 5
add-on is sensitive to resource formats. WGC delivers `B8G8R8A8UIntNormalized`;
we normalise to `R16G16B16A16_FLOAT` with explicitly typed views on both ends.

## 9. Frame pipeline and pacing

```
WoW presents frame N -> DWM composite -> WGC frame-arrived (about 1 vblank)
    -> copy to shared ring
    -> FormatNormalize
    -> NvofaFlow(N, N-1) -> FlowToMotionVec
    -> ConstantDepth
    -> INeuralPass::Evaluate
    -> UiMask blend
    -> DComp present -> DWM composite (about 1 vblank)
```

Floor is two vblanks plus GPU work. Expected added end-to-end latency at 144 Hz
with a 1440p neural pass is 25–35 ms. At 60 Hz it is 50–70 ms and the experience
is materially worse.

**Pacing rule.** If the render thread is busy when a new capture arrives, the
pending slot is *overwritten*, never queued. Queuing converts a transient hitch
into permanent latency growth that never recovers. Latest frame wins; drops are
counted and surfaced in the HUD.

**Honest scope.** This is suitable for questing, exploration, roleplay and
screenshots. It is not suitable for Mythic+, raid progression, or arena. The
manager states this plainly rather than burying it.

## 10. GPU matrix

`GpuProfile` reads `DXGI_ADAPTER_DESC3`, maps device ID to architecture, and
selects behaviour:

| | RTX 50 (Blackwell) | RTX 40 (Ada) | RTX 20/30 |
|---|---|---|---|
| DLSS 5 NR binary | stock `nvngx_dlssnr.dll` 310.8.0.0 | community CUDA-patched Ada build | unsupported |
| Optical flow | Blackwell OFA, `NV_OF_D3D12` | Ada OFA, `NV_OF_D3D12` | present, unvalidated |
| Default internal resolution | 4K | 1440p | n/a |
| Behaviour | full pipeline | full pipeline | clear refusal, passthrough only |

The neural runtime is verified by SHA-256 against a manifest covering both
variants. An unknown hash is refused with a readable message rather than
producing an access violation at feature creation.

## 11. Neural pass routes

`INeuralPass` abstracts how the neural pass is invoked. Three implementations.

**`PassthroughPass`** — copies input to output. No NGX. Exists so the entire
real-time pipeline can be validated before any unredistributable binary is
involved. Also the permanent fallback when the neural runtime is missing or
fails to initialise.

**`ReshadeHostedPass` (route B, primary).** The sidecar creates its own D3D12
device and NGX session, loads ReShade into its own process, and allows
`renodx-dlss5.addon64` to detour *our* `NVSDK_NGX_D3D12_EvaluateFeature`. This is
exactly the mechanism DLSS5-Feeder uses, relocated from the game process to ours.
It is the route proven to work today and is therefore what ships first.

Cost: we depend on a closed-source community binary and inherit ReShade's
lifecycle. Both are acceptable because neither touches WoW.

**`DirectNgxPass` (route A, later).** We own the NGX session, so in principle we
can load `nvngx_dlssnr.dll` and create the neural-rendering feature directly,
with no detour, no ReShade and no third-party binary in the tree. The driver's
`Enable DLSS NR Override` profile keys imply NR is a first-class NGX feature
rather than a detour-only trick.

Risk: the feature identifier and parameter contract are undocumented and require
export-table reverse engineering. It may not be callable standalone at all. If it
cracks, this becomes the first DLSS-NR implementation with no ReShade dependency
anywhere, and route B is dropped. If it does not, route B already shipped and
nothing was wasted.

## 12. UI masking

WoW's interface is composited into the captured frame and will otherwise be
neural-processed alongside the scene. This is the largest quality risk in the
project.

A pure temporal heuristic was considered and rejected as the primary mechanism.
The chosen approach is a one-time calibration in which the user drags rectangles
over their action bars, unit frames, chat and minimap; the rectangles are stored
in config. WoW interfaces are static per user, so this is close to exact for the
large majority of screen-space UI at zero runtime cost.

A temporal-stability and edge-energy heuristic then handles only the moving
remainder — nameplates and floating combat text. Original pixels are blended back
wherever the combined mask indicates interface.

v1 ships the config plumbing and blend path with an empty rectangle list.

## 13. Manager application

`wowsidecar-manager.exe`. Dear ImGui on D3D11, custom-themed, borderless window
with a custom title bar. Same toolchain as the runtime, no additional runtime
dependency for the user, single executable.

WinUI 3 was considered and rejected: a more native feel is not worth a second
toolchain and a .NET runtime prerequisite for the audience.

### Dependency board

Each row reports green, amber or red with a one-line remedy.

| Probe | Method |
|---|---|
| GPU and architecture | `DXGI_ADAPTER_DESC3`, mapped to Blackwell / Ada / unsupported |
| Driver version | at least 610.47, the first build carrying DLSS-NR profile keys |
| Windows build | free-threaded WGC frame pool and occluded window capture |
| Display refresh rate | flags anything below 120 Hz, per C4 |
| `nvngx_dlssnr.dll` | present, SHA-256 matched to the detected architecture's variant |
| ReShade and `renodx-dlss5.addon64` | present in the sidecar directory, for route B |
| WoW running | window located, and display mode confirmed borderless via window styles |

### Actions

Install (place user-supplied DLLs into the sidecar directory, write config),
Remove (delete everything created, verifiable clean), Re-check, Launch and Stop
with a live HUD showing capture-to-present p50 and p99 plus dropped frames, and
Calibrate to paint UI mask rectangles.

### Safety surface

Per I7, the installer hard-refuses any target path containing a WoW install
signature. The most plausible route to a ban for a user holding this tool is
placing ReShade next to `Wow.exe` themselves, so the installer makes that
physically awkward.

A first-run notice, acknowledged once, states the boundary plainly: this sidecar
never loads code into `Wow.exe`, which is what makes it safe; ReShade pointed at
`Wow.exe` is a bannable offence that Blizzard actively enforces; and the tool
will not assist with that.

## 14. Error handling

| Condition | Response |
|---|---|
| Any pipeline failure | Hide overlay immediately, then handle. Never present black over a live game. |
| WGC item closed (WoW exited) | Tear down capture, idle, wait for the window to reappear. |
| `DXGI_ERROR_DEVICE_REMOVED` | Rebuild both devices, the shared ring and the NGX session. |
| Capture returns black (exclusive fullscreen) | Hide overlay, tell the user borderless is required, per C1. |
| Neural runtime missing or init failure | Fall back to `PassthroughPass`, name the exact missing file. |
| Unknown DLL hash | Refuse to load, report expected variant for the detected architecture. |
| WoW window moved or resized | `WindowTracker` repositions the overlay; swapchain resized on client-rect change. |

## 15. Testing

**`testpattern.exe`** — a trivial D3D window rendering known geometry, shipped
with the project. It allows the entire capture, bridge, overlay and pacing path
to be verified pixel-exact with neither WoW nor NGX involved. This is the single
highest-value test in the plan.

**Unit tests** — `FlowToMotionVec` against synthetic image pairs with known
translation (§8); `FormatNormalize` round-trip; config parsing; hash manifest
matching.

**CI invariant job** — dumps the import table of both binaries and fails the
build on any symbol forbidden by I1 through I4, and on any networking import
forbidden by I10. This is what converts the safety claim from an assertion into
a check. A second job asserts I12 by confirming the binaries are unpacked,
unobfuscated and request no elevated manifest.

**Latency instrumentation** — HUD reporting capture-to-present p50 and p99 and
dropped-frame count, used as the evidence for the M1 gate.

## 16. Milestones

- **M0** — `testpattern` passthrough. Capture, bridge, DComp overlay,
  click-through, pacing. No NGX.
- **M1** — the same against live WoW. Measure real latency.
  **Decision gate: is live overlay playable on this hardware?** If
  capture-to-present exceeds roughly 80 ms, the honest conclusion is that live
  overlay is not the product and photo mode is.
- **M2** — NVOFA and motion vectors, synthetic-validated, then live.
- **M3** — `ReshadeHostedPass`. DLSS 5 Neural Rendering running on WoW Retail.
- **M4** — GPU matrix: Ada patched build and Blackwell stock, hash manifest,
  automatic selection.
- **M5** — UI mask calibration.
- **M6** — `DirectNgxPass` clean-room. Drop ReShade entirely if the contract
  cracks.

The manager application is developed alongside M0 through M4, since its
dependency board is the most convenient harness for the probes those milestones
need anyway.

## 17. Open risks

- **Route A may be impossible.** Mitigated by shipping route B first.
- **Occluded-window capture behaviour** under an always-on-top overlay is
  documented to work through DWM but is unverified for this specific
  configuration. M0 tests exactly this and is deliberately the first milestone.
- **The neural pass may be too expensive on Ada** at useful resolutions. The
  reported cost on Blackwell is severe (RTX 5090 dropping 91 to 50 fps at 4K).
  M1 and M3 measure it; 1440p is the Ada default for this reason.
- **The community Ada patch is an unofficial binary** of unknown provenance.
  Hash-pinned, user-supplied, never redistributed by this project.
- **Blizzard could begin detecting WGC capture of its window.** Extremely
  unlikely — it would break OBS, Discord, and Xbox Game Bar simultaneously — but
  it is the residual risk, and it is not zero.

## 18. References

- NVIDIA NGX Programming Guide — https://docs.nvidia.com/ngx/latest/programming-guide/index.html
- NVIDIA DLSS SDK — https://github.com/NVIDIA/DLSS
- jlrouzies-fr/DLSS5-Feeder — https://github.com/jlrouzies-fr/DLSS5-Feeder
- NIGos/dlss5-dx11-bridge — https://github.com/NIGos/dlss5-dx11-bridge
- NIGos/dlss5-d3d12-fix — https://github.com/NIGos/dlss5-d3d12-fix
- rakanki911/DLSS5-Swapper — https://github.com/rakanki911/DLSS5-Swapper
- TechPowerUp, DLSS 5 at SIGGRAPH 2026 — https://www.techpowerup.com/350916/nvidia-shows-dlss-5-progress-and-technical-details-at-siggraph-2026
- VideoCardz, DLL found in NBA 2K27 — https://videocardz.com/newz/nvidia-dlss-5-neural-rendering-dll-found-in-nba-2k27-early-access-build-file-is-3x-larger-than-dlss-4
- Tom's Hardware, RTX 40 port — https://www.tomshardware.com/pc-components/gpus/exclusive-dlss-5-has-already-been-ported-to-work-on-rtx-4000-series-graphics-cards-incompatible-cuda-instructions-get-patched-to-work-on-previous-gen-hardware
- Guru3D, driver 610.47 NR profiles — https://www.guru3d.com/story/nvidia-geforce-61047-driver-quietly-adds-first-dlss-5-neural-rendering-profiles/
- ReShade forum, WoW blocking dxgi.dll — https://reshade.me/forum/troubleshooting/10065-world-of-warcraft-blocking-reshade-dxgi-dll-after-7-14-2025-11-1-7-61965-patch
- Wowhead, Blizzard third-party software reminder — https://www.wowhead.com/news/blizzard-reminds-players-that-third-party-software-is-prohibited-378029
