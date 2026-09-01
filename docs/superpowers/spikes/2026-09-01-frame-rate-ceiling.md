# Spike: where the overlay's frame rate actually goes

**Date:** 2026-09-01
**Outcome:** the ceiling is the capture rate, not the neural pass. Two real wins found underneath it; one assumed lever measured and discarded.

All figures RTX 4080, 2560×1440, monitor at 239 Hz.

## Why this was needed

"Low FPS" was diagnosed three times by reasoning and got it wrong each time. So the render loop was instrumented instead: `idle`, `record`, `present wait` and `gpu wait`, accumulated per reporting window and logged, plus a presented-vs-captured frame rate. Everything below came out of that line.

## Finding 1: the swapchain was starving itself

```
before   idle 2.46   record 1.10   present wait 0.57   gpu wait 12.00   p50 14.16   p99 16.86
after    idle 4.50   record 0.78   present wait 0.00   gpu wait 11.10   p50 11.61   p99 12.55
```

`BufferCount = 2` with `SetMaximumFrameLatency(1)` left the queue nowhere to put the next frame until the compositor released the only spare buffer. That block happened *inside* our own fence wait, where it was indistinguishable from GPU work — and was originally read as GPU work. Three buffers and a latency of two: p99 nearly halved, `present wait` to zero.

## Finding 2: render size is not a lever

`NgxSession` already supports `renderWidth != outputWidth`, so this was cheap to test directly.

| | GPU wait | p50 |
|---|---|---|
| full scale | 10.7–11.1 ms | 11.61 ms |
| 0.667 render scale | 10.4–10.9 ms | 11.63 ms |

Noise. The add-on substitutes its own neural output for our DLSS evaluate and does it at **output** resolution, so the render size we ask for never reaches the work that costs. **There is no upscaling lever on this route**, and therefore no DLSS-style performance win is available. A `render_scale` setting was designed, spiked, and deliberately not built.

## Finding 3: the real ceiling is Windows Graphics Capture

The decisive number came from counting delivered frames against presented ones:

```
frame budget: 31.02 fps presented, 59.97 fps captured, ...
```

The source window was measured independently at **171.7 fps** (it reports its frame index in its title). WGC was handing us **60.0**. Against live retail WoW the same reading is 58–61 captured.

So the compositor's delivery rate is the ceiling for the entire approach. No pipeline work can present a frame that was never captured. This is now surfaced in the app as **CAPTURED FPS** next to **OVERLAY FPS**, with a plain-language note when they converge.

## Finding 4: two default settings were costing frames

Measured against live WoW, changing only the configuration:

| | presented | GPU wait |
|---|---|---|
| flow grid 1, transformer-K, add-on upscaling on | 52.0 fps | 18.1 ms |
| flow grid 4, CNN F, upscaling off (*Recommended*) | 59.9 fps | 13.1 ms |

Optical flow at grid 1 is sixteen times the vector work of grid 4, and the add-on's upscaling path is unfinished and falls back to native regardless. Both are now set correctly by the presets, and the presets are what the UI leads with.

## The lesson worth keeping

A timer on the whole loop (`p50`) hides which of four different things is slow, and they have four different answers. Splitting the frame into idle / record / present-wait / gpu-wait turned three wrong guesses into four measurements in an afternoon — and the single most valuable number, captured-versus-presented, was not in the original instrumentation at all because nobody suspected the capture rate.
