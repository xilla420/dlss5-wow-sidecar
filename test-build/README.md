# Test build: temporal ghosting fix (#16)

Two executables built from this branch, for trying the ghosting fix before it is
released. They are **not a full install**: the DLSS runtime and the other
third-party files are too large for the repository and only ship in releases.

## What changed

- **The motion vectors pointed the wrong way.** Every frame blended in history
  from twice the real motion away, which is what drew the trails behind moving
  characters and smeared the picture when the camera turned. They now point the
  right way.
- **Extra neural passes no longer multiply the smear.** 2x and 3x strength
  should now hold up under motion as well as 1x does.
- **Uncovered areas show the present frame.** Where a turning camera reveals new
  ground or a character uncovers what was behind them, there is no usable
  history, so those pixels fall back to the real frame instead of a trail.
- History is dropped after alt-tab, loading screens or a minimised game.

## How to try it

1. Install the latest release,
   [v0.5.0](https://github.com/xilla420/dlss5-wow-sidecar/releases/latest), and
   check that it starts.
2. Close the manager and the overlay.
3. Download `wowsidecar.exe` and `wowsidecar-manager.exe` from this folder and
   replace the two files of the same name in your install. Keep a copy of the
   originals if you want to go back.
4. Start the manager and the overlay as usual. Retail, Classic and 3.3.5a all
   use the same fix.

The window title still says v0.5.0. That is expected for a test build.

## Optional settings

Both go in `sidecar.toml` in the install folder, with the overlay stopped. The
manager does not show them yet.

- `disocclusion_fallback = 1.0` controls how strongly uncovered areas fall back
  to the real frame, from `0.0` (off) to `1.0` (the default). Lower it if you
  see unprocessed-looking edges around moving characters.
- `invert_motion = true` puts the old, wrong vectors back, so you can compare
  before and after in the same scene. Remove it again afterwards.

## What to report

Comment on [#16](https://github.com/xilla420/dlss5-wow-sidecar/issues/16) with
your GPU, resolution, client (retail, Classic or 3.3.5a), neural strength (1x,
2x, 3x), and `sidecar.log` from the install folder. A short clip of a camera
turn around your character, before and after, helps most.

## Checksums (SHA-256)

    35626ec4a87b10f7e23c13ebc2d33c795a5e08737b487cf8b1b165aa1756ae88 wowsidecar.exe
    02daf491d825ef25301042fb9341897c86fa599905986ef84f914f9712813825 wowsidecar-manager.exe
