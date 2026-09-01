# Spike: what actually makes the overlay click-through

**Date:** 2026-08-31
**Harness:** [`tools/spike_clickthrough/`](../../../tools/spike_clickthrough/main.cpp)
**Outcome:** the shipping overlay was never click-through. `WS_EX_LAYERED` was missing, and adding it costs nothing.

## The symptom

With the overlay up, World of Warcraft could be neither clicked nor typed into. Both at once, which was the clue: the overlay is `WS_EX_NOACTIVATE`, so it does not take focus in exchange for the clicks it swallows. Clicks went nowhere at all, the game was never activated, and so the keyboard never reached it either.

## What was believed

`DCompOverlay` answers `HTTRANSPARENT` to `WM_NCHITTEST`, and a `[device]` test asserted exactly that. The test passed. It had passed since M0.

## Why the test passed anyway

`HTTRANSPARENT` is documented to fall through "to underlying windows **in the same thread**". The test created its stand-in for the game on the test's own thread, so `WindowFromPoint` obligingly skipped the overlay and the assertion held.

The game is in another process. It gets no such courtesy. The test was measuring a proxy — that our window procedure returns the right constant — rather than the property that matters, which is whether Windows routes a foreign process's click past us. The test file's own comment even recorded the discrepancy and drew the wrong conclusion from it.

The cross-process mechanism is `WS_EX_LAYERED | WS_EX_TRANSPARENT`. That pair makes the system exclude the window from hit-testing entirely, for everyone.

## Why `WS_EX_LAYERED` had been ruled out

From `DCompOverlay.h`, since M0:

> `WS_EX_LAYERED` cannot host a DXGI flip swapchain, so the window is created `WS_EX_NOREDIRECTIONBITMAP` and composed by DirectComposition instead (spec C2).

True of `CreateSwapChainForHwnd`, and irrelevant here. This swapchain comes from **`CreateSwapChainForComposition`** and is bound to a DirectComposition *visual*. The window is only the composition target and never sees the swapchain at all. The constraint was inherited from a different presentation path and never re-examined against the one actually in use.

## The measurement

Three style combinations, each one filled with a known colour and then read back off the composed desktop with `GetPixel` — because "click-through but invisible" is just as broken as the bug being fixed — and hit-tested from a **foreign thread**, which is the closest available proxy for the game's own hit test.

| Variant | DComp target | Visible | Click-through |
|---|---|---|---|
| `NOREDIRECTION \| TRANSPARENT` (shipping) | yes | yes | **no** |
| `NOREDIRECTION \| TRANSPARENT \| LAYERED` | yes | yes | **yes** |
| `TRANSPARENT \| LAYERED` (no `NOREDIRECTION`) | yes | yes | yes |

## What changed

Variant 2. `WS_EX_LAYERED` is added and `SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)` is called — a layered window with no attributes set is never composed at all, so the call is required even though DirectComposition is doing the actual compositing and the alpha changes no pixels.

`WS_EX_NOREDIRECTIONBITMAP` stays: variant 3 works too, but there is no reason to give up the redirection-surface saving.

The `HTTRANSPARENT` reply stays as well. It does nothing for the game, but it is correct for anything asking from our own thread, and it costs a comparison.

## What changed in the test

`tests/test_dcomp_overlay.cpp` now asks `WindowFromPoint` from a foreign thread via `std::async`. That removes the same-thread fall-through and leaves only what the system honours across processes. The old assertion — `SendMessage(WM_NCHITTEST) == HTTRANSPARENT` — is gone, because passing it never implied anything about the real behaviour.

## The lesson worth keeping

A test that asserts the return value of your own code is not a test of a system behaviour. This one passed for four milestones while the product's single most visible feature did not work. Where the property is "the operating system does X for another process", the test has to be written from outside that process, or it is measuring the wrong thing.
