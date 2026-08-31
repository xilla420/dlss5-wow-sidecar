# Spike findings — Task 4: does the ReShade detour fire in our process?

**M3 plan, Task 4**, plus a correction to [Task 3's findings](2026-08-30-ngx-feature-probe.md).

Harnesses: [`tools/spike_reshade/main.cpp`](../../../tools/spike_reshade/main.cpp) and
[`tools/spike_snippet/main.cpp`](../../../tools/spike_snippet/main.cpp). Neither is shipped,
neither is linked into `sidecar_common`, and both build only when `-DDLSS_SDK_DIR=` points at a
checkout of github.com/NVIDIA/DLSS.

**Machine:** RTX 4080 (Ada, `0x2704`), driver 610.62, Windows 11 build 26200.
**ReShade:** 6.8.0.2155, add-on build. **Add-on:** `renodx-dlss5.addon64`, "DLSS 5 Neural
Rendering" v0.2026.828.2110, ReShade API 18. **Runtimes:** `nvngx_dlss` / `dlssg` / `dlssnr`,
all Authenticode-valid NVIDIA builds, 310.8.0.

## Answer

**Yes.** The detour fires in a process shaped like the sidecar, and it goes considerably
further than the plan asked: the add-on brings the DLSS 5 NR runtime up inside our process and
attempts the NR feature. It gets to the last step and fails there.

## What was measured

**1. ReShade attaches to a composition swapchain.** This was the specific doubt — `DCompOverlay`
presents through `CreateSwapChainForComposition` on a D3D12 command queue with no HWND, and
every game ReShade targets uses `CreateSwapChainForHwnd`. The spike reproduces the composition
shape deliberately. ReShade's log shows it redirecting `CreateSwapChainForComposition` and
creating a runtime on it. That risk is retired.

**2. The NGX entry points are really detoured.** Independently of anything ReShade reports, the
spike snapshots each NGX export's first 16 bytes before ReShade is in the process and again
afterwards. Six prologues are rewritten with `E9` (jmp rel32), across two module copies:

| module | detoured |
|---|---|
| `_nvngx.dll` (NGX core, driver store) | `CreateFeature`, `EvaluateFeature`, `ReleaseFeature` |
| `nvngx_dlss.dll` (DLSS snippet) | `CreateFeature`, `EvaluateFeature`, `ReleaseFeature` |

`Init`, `Init_Ext` and `AllocateParameters` are left alone.

**3. It intercepts our calls and reaches the NR runtime.** With the sidecar creating and
evaluating a real DLSS feature, the add-on's log reads:

```
D3D12 NGX hooks installed across 2 module copy(ies); inline DLSS contract capture armed
NGX feature create intercepted: feature=1 (DLSS/DLAA), slot=0
first NGX evaluate intercepted (slot=0)
signed DLSSNR 310.8.0 D3D12 runtime initialized
created Control-equivalent soft-clip/sRGB/UpgradeToneMap codec
created inline NR resources 1920x1080 -> 1920x1080 (native) format=10
NGX feature create intercepted: feature=18 (DLSSNR/reserved-18), slot=0
feature 18 create failed with 0xbad00001
```

Our own calls returned `Success` for both `NGX_D3D12_CREATE_DLSS_EXT` (1280×720 → 1920×1080,
MaxQuality, MVLowRes) and `NGX_D3D12_EVALUATE_DLSS_EXT`.

**4. A hash for the manifest.** The add-on reports
`E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E` for `nvngx_dlssnr.dll`
310.8.0 as a "reference match". Task 5 Step 2 needs known-good digests; that is one, from an
independent source, and `Sha256` should reproduce it.

## The remaining blocker

`0xbad00001` is plain `NVSDK_NGX_Result_Fail` — no detail. The contract was synthetic here
(uninitialised textures, and NR resources built at 1920×1080 native while our DLSS rendered
1280×720), so a rejected contract is a live possibility and is **the same depth question Task 3
left open**.

But there is a second candidate that costs nothing to state: **this is the stock NR runtime on
an Ada card.** The plan's own Task 6 already assumes a GPU matrix where Blackwell gets the stock
variant and Ada needs a patched one. A stock Blackwell-only runtime refusing feature creation on
Ada would produce exactly this generic failure at exactly this step. Distinguishing the two
requires the Ada variant, which is user-supplied — it is not something to go and fetch.

## What the published prior art settles

**Everything in this section is read, not measured.** It is kept separate from the
measurements above on purpose. It does, however, resolve the ambiguity this spike could not.

**1. The stock runtime is Blackwell-only, and that is almost certainly our `0xbad00001`.**
The leaked `nvngx_dlssnr.dll` contains CUDA binaries compiled for Blackwell. A patched build
replacing the Ada-incompatible ones exists and is reported to add RTX 40 and 50 support. So a
stock runtime on an RTX 4080 failing at feature creation — after loading and initialising
cleanly — is the expected outcome, not a rejected contract.

This is now checked before anything is attempted: `CheckRuntimeCompatibility(Ada, Stock)`
returns `WrongVariant` and the probe says why, so nobody else has to spend a spike finding out.

**2. Feature 18 does work, given a real depth buffer.** DLSS5-Feeder — the closest prior art,
and the design this project relocates out of process — reports `feature 18 created` and
`inline feature 18 evaluation succeeded`. The mechanism is sound; ours failed on the runtime,
not on the idea.

**3. Question 1 has an answer, and it is the unwelcome one.** DLSS5-Feeder feeds a DLAA
contract of colour + **R32F depth** + **RG16F motion vectors**, all at output resolution, no
jitter, render size equal to output size. Depth is **mandatory, with no synthetic fallback**.
It gets real hardware depth from ReShade because it runs inside the game.

This sidecar has no depth and cannot obtain one — that is what capturing DWM's composited
output means. So M3 cannot simply copy the prior art. What remains untested by anyone is
whether NR tolerates a *synthesised* depth (constant, or a luminance-derived proxy), and with
what quality cost. That is now the single open question for M3, and it can only be answered
once an Ada-capable runtime is present.

**4. Two details worth taking for free.** Their motion vectors are in pixels with
`prev_uv = uv + mv`, which is an independent cross-check on the sign convention M2 measured
(NVOFA returned −4.5 px against +4 px/frame, and `FlowToMotionPixels` negates). And they pass
a `R8` trust mask for vectors that failed validation as DLSS's **bias-current-colour mask** —
which means the UI mask of Tasks 7 and 8 has a second consumer, and optical-flow vectors over
interface elements are exactly the vectors worth distrusting.

**5. A performance warning.** An RTX 4090 running NR in-engine is reported dropping from ~135
to ~82 FPS, roughly −39%. The M1 gate passed at p99 6.1 ms against an ~80 ms threshold, so
there is headroom, but this is the first indication that the neural pass may dominate the
budget on Ada. Task 5 Step 9 measures it rather than assuming either way.

Sources: [Tom's Hardware](https://www.tomshardware.com/pc-components/gpus/exclusive-dlss-5-has-already-been-ported-to-work-on-rtx-4000-series-graphics-cards-incompatible-cuda-instructions-get-patched-to-work-on-previous-gen-hardware),
[DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder),
[DLSS5-Swapper releases](https://github.com/rakanki911/DLSS5-Swapper/releases).

## Correction to Task 3

Task 3 concluded "M6 / Task 10 is closed". **That conclusion was drawn too broadly** and is
narrowed here. What Task 3 measured still holds exactly as written: the NGX *core* refuses every
feature id from 18 up with `OutOfDate`, and no SDK version above `0x15` gets past `Init`.

What it missed is that the core is not the only door. `nvngx_dlssnr.dll` is an NGX *snippet* and
exports the D3D12 entry points itself — `Init`, `Init_Ext`, `CreateFeature`, `EvaluateFeature`,
`ReleaseFeature`, `Shutdown1`, `PopulateParameters_Impl`, `GetScratchBufferSize` — so the core's
feature registry can be bypassed entirely. It does not export `AllocateParameters`,
`DestroyParameters` or `GetCapabilityParameters`; those stay in the core, which is why a caller
needs both.

`spike_snippet` tested that door. It is also shut, but differently:

| call | result |
|---|---|
| `GetAPIVersion` (snippet) | `0x13` — older than the public SDK's `0x15` |
| `GetSnippetVersion` | `0x01360800` → 310.8.0 |
| snippet `Init_Ext` | `PlatformError`, at SDK versions 0x11–0x16 |
| snippet `Init` + path list | `FeatureNotSupported`, at the same six versions |
| snippet `CreateFeature`, ids 1–20 | `PlatformError` throughout |

The two init entry points fail *differently*, so both are real code paths rather than stubs, and
the result does not move with the reported version. The snippet declines to initialise for a
caller that is not the NGX core.

So route A is closed at both doors — but for a reason the original write-up did not identify,
and the distinction matters: the barrier is not "NR does not exist for us to call", it is "NR
refuses a session we set up ourselves". The add-on gets past it by initialising the NR runtime
only *after* intercepting a live DLSS contract from the host, not by asking for it cold.

## What route B actually costs

Worth recording plainly, since it is now the shipping route. The add-on works by **inline
detouring NVIDIA's signed NGX core and DLSS snippet** in the host process, and its own strings
describe making `nvngx_dlssnr.dll`'s import table writable and patching it. Consequences the
manager should be honest about:

- anti-malware heuristics flag inline detours of signed binaries; expect false positives
- a driver update changes `_nvngx.dll` and can break the hooks silently
- none of it is ours to fix, and none of it is redistributable

This is separate from the WoW-injection question. It all happens inside `wowsidecar.exe`; I5/I8
are untouched, and nothing goes near `Wow.exe`.

## Two mistakes in the harness, corrected

- **A relative NGX feature-dll search path silently finds nothing**, and the symptom is
  `SuperSampling.Available = 0` with `FeatureInitResult = UnableToInitializeFeature` — which
  reads exactly like "the driver refuses DLSS on this machine". It cost one wrong reading before
  the absolute path made DLSS available and every create succeed. `spike_reshade` now calls
  `GetFullPathNameW` on whatever it is given. Any real `NgxSession` must do the same.
- Buffered stdout loses the tail precisely when a call inside a detour is the thing failing. The
  harness is unbuffered now.

## Reproducing

```
cmake -S . -B build -DDLSS_SDK_DIR=<path to NVIDIA/DLSS>
cmake --build build --config Debug --target spike_reshade
```

Run it from a scratch directory holding ReShade's `dxgi.dll`, `renodx-dlss5.addon64`, a
`ReShade.ini` with `[ADDON] AddonPath=.`, and the `nvngx_*` runtimes, passing that directory as
the argument. **Never from the build output directory** — a ReShade `dxgi.dll` sitting next to
`wowsidecar.exe` would be injected into it on its next run, which is the one thing this project
exists to avoid.

## Note on where the binaries came from

ReShade was taken from the operator's own install at
`F:\SteamLibrary\steamapps\common\REPO\` — byte-identical (SHA-256 `0CEE63F9…`) to the copy in
the WoW folder, so the WoW copy was not needed. `renodx-dlss5.addon64` and the three `nvngx_*`
runtimes were copied read-only out of `D:\World of Warcraft\_retail_`, which holds the only
copies on this machine. Nothing in that directory was written, moved or deleted, and the
ReShade install that sits there is still what `ProbeInjectorScan` refuses under I8.
