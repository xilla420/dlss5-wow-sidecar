# Spike findings — is DLSS 5 Neural Rendering reachable through public NGX?

**M3 plan, Task 3.** Harness: [`tools/spike_ngx/main.cpp`](../../../tools/spike_ngx/main.cpp). Not shipped, not linked into `sidecar_common`, built only when `-DDLSS_SDK_DIR=` points at a checkout of github.com/NVIDIA/DLSS.

**Machine:** RTX 4080 (Ada, device id `0x2704`), driver 610.62, Windows 11 build 26200.
**SDK:** public NVIDIA DLSS SDK, `NVSDK_NGX_VERSION_API_MACRO = 0x15` (1.5.0).
**Runtimes visible to the probe:** `nvngx_dlss.dll`, `nvngx_dlssg.dll`, `nvngx_dlssnr.dll`.

## Answer

**No. DLSS 5 Neural Rendering cannot be reached through the public NGX SDK on this driver.** Route A (`DirectNgxPass`, milestone M6) is blocked, and not by a missing parameter contract — by a version horizon that cannot be raised.

## What was measured

NGX itself works. `NVSDK_NGX_D3D12_Init` returns **Success** against our own D3D12 device, and `GetCapabilityParameters` succeeds. The sidecar can own an NGX session; that was never the problem.

**1. Feature discovery is unreliable and proves nothing on its own.**
`NVSDK_NGX_D3D12_GetFeatureRequirements` returns `NotImplemented` for almost every id — including `RayReconstruction = 13`, which is a real shipping feature. Absence of a discovery check is not absence of a feature.

It does work where a feature dll is present: pointing the path list at the directory containing `nvngx_dlssg.dll` flips `FrameGeneration = 11` from `NotImplemented` to **`Success` / `Supported`**. That control confirms the mechanism functions, which is what makes the negative results below meaningful.

**2. `CreateFeature` splits the id space into two bands.**

| ids | result | reading |
|---|---|---|
| 1–17 | `UnableToInitializeFeature` | id is known to this NGX core; our generic parameters are wrong for it |
| 11 | `MissingInput` | got furthest — the dll was found, loaded, and told us what we omitted |
| 18–32 | `OutOfDate` | uniform; the id is beyond what a 0x15 client may ask for |

Id 11 behaving differently from every neighbour, and only when `nvngx_dlssg.dll` is on the path, is the signature of actually reaching a feature. **Nothing in 1–17 ever reached `nvngx_dlssnr.dll`,** despite it sitting in the same directory.

**3. The `OutOfDate` band is not a header-version problem.**
The obvious hypothesis was that ids ≥ 18 need a newer SDK than 0x15, so the probe reported 0x16, 0x17, 0x18 and 0x20 instead. Every one of them made **`NVSDK_NGX_D3D12_Init` itself return `OutOfDate`**, and every feature — including FrameGeneration, which had worked at 0x15 — went `OutOfDate` with it.

So 0x15 is the newest version this NGX core accepts, and there is no version a client can claim that opens the higher ids. The horizon is fixed from our side.

## Why this does not block M3

It blocks **route A**, not route B.

Route B never asks NGX for a neural-rendering feature id. Every published tool detours `NVSDK_NGX_D3D12_EvaluateFeature` and drives `nvngx_dlssnr.dll` itself, which sidesteps the core's feature registry entirely — and that registry is precisely what refuses us above id 17. The finding explains *why* every existing tool is built as a detour rather than as a clean NGX client: on a shipping driver there is no other way in.

## Consequences for the plan

- **M6 / Task 10 (`DirectNgxPass`) is closed unless the situation changes.** The spec anticipated this ("it may not be callable standalone at all") and shipped route B first for exactly this reason. It reopens only if NVIDIA ships an SDK whose version this NGX core accepts *and* which names the feature.
- **Task 5 (`ReshadeHostedPass`) keeps its premise**, but with one assumption corrected: it is not "we create an NR feature through our own NGX session". It is "the add-on drives the NR runtime inside our process". Task 4's spike — does the detour fire in our process — is now the load-bearing question for all of M3.
- **The depth question (Task 3, Question 1) is not yet answered.** It cannot be answered through NGX feature creation, because we never get far enough to be told what inputs NR wants. It has to be answered inside route B, once the detour is firing.

## Reproducing

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DDLSS_SDK_DIR=<path to NVIDIA/DLSS>
cmake --build build --config Debug --target spike_ngx
build\Debug\spike_ngx.exe "<directory containing nvngx_*.dll>" [sdk-version-hex]
```

Run with no arguments for the baseline: application folder only, header's own SDK version.

## Note on where the runtimes were read from

The only copies of `nvngx_dlssnr.dll` on this machine sit in `D:\World of Warcraft\_retail_` alongside a ReShade install (`dxgi.dll`, `ReShade.ini`, `renodx-dlss5.addon64`). The probe was pointed at that directory read-only — NGX loaded dlls from it into the spike's own process. Nothing there was written, moved or deleted.

That arrangement is what invariant I8 exists to refuse: `ProbeInjectorScan` returns `Fail` for that folder and the manager will not launch the overlay while it stands. Resolving it is the operator's decision, and it is separate from this spike's findings.
