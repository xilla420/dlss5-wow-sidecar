# How the DLSS 5 Sidecar works

**English** | [Русский](HOW-IT-WORKS.ru.md) · [Back to the README](../README.md)

The long version: the safety design and how it is enforced, measured frame-rate behaviour, the pipeline, the limits, and how to build it yourself.

## Why this exists, and why it is shaped like this

The obvious way to get DLSS 5 into a game that does not support it is to inject
a DLL beside the executable. For World of Warcraft that is not a trade-off, it
is a mistake: Blizzard bans accounts for ReShade next to `Wow.exe`, and no
amount of care on the tool's part changes that.

So this project does the harder thing. It never touches the game process at all.
Everything happens out-of-process, on a frame that Windows has already finished
compositing.

That constraint is not a comment in a design document — it is enforced against
the built binaries on every build and in CI:

| Invariant | What it forbids | How it is checked |
|---|---|---|
| I1 | Reading or writing another process's memory | Import table scan |
| I4 | Window hooks of any kind (`SetWindowsHookEx`) | Import table scan |
| I6 | Synthesising input (`SendInput`, `keybd_event`, …) | Import table scan |
| I10 | Networking, of any kind, in either binary | Import table scan |
| I12 | Requesting elevation | Manifest scan |
| I7/I8/I9 | Installing next to `Wow.exe`, or running with an injector present | Unit-tested predicates |

`ci/check_imports.py` reads the import directory — static *and* delayed — of
every executable produced and fails the build if any forbidden symbol appears.
The claim is checked, not asserted.

**This is not a guarantee you will never be banned.** No third-party tool can
offer that. What it is: this program does not do the things people get banned
for.


## About frame rate

This is the part most people will care about, so here are the measurements
rather than a claim. All on an RTX 4080, 2560×1440, against retail WoW.

**There is a hard ceiling, and it is not the neural pass.** Windows Graphics
Capture delivers frames at the desktop compositor's rate. On the development
machine that is **60 per second**, on a 239 Hz monitor, with the game itself
running far faster. Nothing in this tool can present a frame that was never
captured, so 60 is the ceiling — and the Status page shows **CAPTURED FPS**
beside **OVERLAY FPS** so you can see immediately which one is limiting you.

If they are equal, you are capture-bound and no setting will help. It is worth
checking whether a multi-monitor setup with mismatched refresh rates is dragging
the compositor down to the slowest display.

### Cap the game's frame rate

If the overlay is presenting fewer frames than are being captured, the game is
taking the GPU and starving the neural pass. Measured on the development
machine, same build and same scene, differing only in whether the game had
focus:

| World of Warcraft | overlay | GPU wait |
|---|---|---|
| focused, uncapped | 11.7 fps | 83 ms |
| in the background (self-throttling) | **35.2 fps** | **27 ms** |

That is the whole of the "it speeds up when I alt-tab" effect: an unfocused game
throttles itself and hands the card back.

**So cap the game, in the game's own options.** It costs nothing, because the
overlay can never present faster than the capture rate — every frame WoW renders
beyond that is thrown away before it reaches the sidecar. Setting **Max
Foreground FPS** to roughly the captured figure on the Status page converts
wasted frames into GPU time for the neural pass. The app says this in place when
it detects the condition.

### Video memory is the thing that will actually ruin it

Watch the **GPU MEMORY** bar on the Status page before you blame the neural
pass. This is the failure that does not look like itself.

Past the per-process budget the driver evicts resources to system memory and
every frame waits on the PCIe bus. Frame times explode while the GPU sits
*nearly idle* — on the development machine, measured within a single session:

| GPU memory state | overlay | GPU wait |
|---|---|---|
| comfortable | 40 fps | 24 ms |
| card ~91% full, 745 MB spilled to system RAM | **6.5 fps** | **153 ms** |

Same build, same settings, same scene. Nothing about the pipeline changed. A
process-level GPU utilisation counter showed the sidecar at *8.8%* while it was
taking 153 ms a frame, which is the tell: that time is not compute.

The card's memory is shared with everything else on the desktop, and the
sidecar is rarely the biggest consumer — on the development machine the desktop
compositor alone held 9.1 GB of 16. Close browsers, streaming and capture tools,
and anything compositing a second monitor; lower the game's texture quality.
**Nothing in this tool can make room**, which is exactly why it tells you.

**Underneath that ceiling, two things were worth fixing:**

| | before | after |
|---|---|---|
| Swapchain (2 buffers / latency 1 → 3 / 2) — p50 | 14.16 ms | **11.61 ms** |
| the same, p99 | 16.86 ms | **12.55 ms** |
| Default settings → Recommended preset, presented | 52.0 fps | **59.9 fps** |
| the same, GPU time | 18.1 ms | **13.1 ms** |

The first was a self-inflicted stall: with two buffers and a maximum frame
latency of one, presents blocked on the compositor *inside* our own fence wait,
where it looked exactly like GPU work.

**And one thing that turned out not to be a lever at all.** Asking DLSS for a
smaller render size does nothing here — measured at a 0.667 render scale, GPU
time moved from 10.7–11.1 ms to 10.4–10.9 ms, which is noise. The add-on
substitutes its neural output at *output* resolution, so the render size never
reaches the work that costs. **There is no DLSS upscaling on this route, and
therefore no performance win**: what you get is the neural rendering filter at a
fixed cost set by your capture resolution. It makes the picture different. It
does not make the game faster.


## How it works

```
WoW window ──> Windows Graphics Capture ──> D3D11→D3D12 shared texture
                                                      │
                       ┌──────────────────────────────┤
                       │                              │
              BGRA8 → R8 luminance            BGRA8 → RGBA16F
                       │                              │
                  NVIDIA NVOFA                        │
                (optical flow)                        │
                       │                              │
              flow grid → RG16F motion vectors        │
                       │                              │
                       └──────────> DLSS/DLAA evaluate <─── synthetic R32F depth
                                            │
                              (RenoDX add-on detours this call
                               and substitutes neural output)
                                            │
                                    UI mask blend
                                            │
                         DirectComposition overlay ──> screen
```

The counter-intuitive part is the neural pass. **It is a DLSS client, not a
neural-rendering client.** Asking NGX for a neural-rendering feature directly
does not work — the runtime refuses a session set up by anyone but the NGX core,
which two spikes established. So the sidecar creates and evaluates an ordinary
DLSS/DLAA feature, and the RenoDX add-on — loaded by ReShade into *our* process,
never the game's — detours our own NGX calls and substitutes neural-rendered
output.

Design and spike write-ups live in [`docs/`](.).


## Limitations, stated plainly

- **There is no depth buffer, and there cannot be one.** This captures DWM's
  composited output. There is no depth, no albedo, no normals, no camera
  matrices — one colour image and a motion field estimated from two of them. A
  constant depth plane is bound because the contract requires the binding. It
  costs temporal stability under motion rather than preventing NR from running.
- **Motion vectors are estimated, not rendered.** NVOFA infers them from
  luminance. They are good, not authoritative, and the CNN presets exist in the
  UI specifically to contain the cases where they are confidently wrong.
- **The contract is strictly weaker than any in-process tool's.** That is the
  price of not touching the game.
- **SDR only** today. The HDR knobs are carried through to the add-on but the
  capture path is SDR.
- **This costs frames; it does not gain them.** See *About frame rate* above:
  there is no upscaling lever on this route, and the capture rate caps the
  overlay well below what the game itself achieves.
- **Latency is real.** ~11.6 ms p50 on a 4080 at 1440p. Fine for questing and
  raiding; you will feel it in high-end PvP. The manager's Status tab breaks
  each frame into GPU, idle, CPU and compositor time so you can see where it
  goes rather than guess.
- **UI masking exists but has no calibration UI** — rectangles are hand-written
  into `sidecar.toml` for now.
- **The add-on's upscaling path is unfinished** and usually reports falling back
  to native. Off is the tested path.


## Building

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Two optional SDKs, neither vendored (I11), both manual downloads:

| | |
|---|---|
| `-DDLSS_SDK_DIR=` | [NVIDIA/DLSS](https://github.com/NVIDIA/DLSS). Without it `NgxSession` compiles to a stub that reports why it is unavailable, and the neural pass falls back to passthrough. |
| `-DNVOF_SDK_DIR=` | NVIDIA Optical Flow SDK, headers only. Without it the pipeline runs on a zero motion field. |

Tests: `build\tests\Release\sidecar_tests.exe "[unit]"`. The `[device]` tests
need a real NVIDIA GPU and are excluded from CI, because a skipped GPU test must
not read as a pass.

Translation table: `python ci/check_translations.py`. Translations are keyed
by their English source text, so editing an English string orphans its
translation silently — nothing fails to build and nothing looks wrong until
somebody switches language. This check is what catches that, and it runs in CI.


## Interface language

The app speaks English by default and Russian by choice. The switch is in the
manager's header, left of the primary button, and is reachable before the
first-run notice is accepted — somebody who cannot read the notice has to be
able to change the language before agreeing to it.

The choice is kept in `sidecar.toml` under `language` (`"en"` or `"ru"`). The
Windows locale is deliberately not consulted: every screenshot in this document
is English, and a first run that does not match them is a worse introduction
than one in a second language.

Log lines stay English in every interface language. The log is what travels
back to the maintainer in a bug report, and a translated one makes that report
harder to act on rather than easier.
