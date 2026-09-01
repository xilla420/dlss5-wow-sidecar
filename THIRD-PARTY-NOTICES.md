# Third-party components

The source in this repository is MIT-licensed (see [LICENSE](LICENSE)). The
release bundle additionally contains four binaries that are **not** ours, are
not covered by that licence, and are redistributed only for convenience. Each
remains the property of its author and is governed by its own terms.

None of these is built from source here, and none is downloaded by the
software — neither executable in this project can reach the network at all.

| Component | Publisher | What it does | Terms |
|---|---|---|---|
| `nvngx_dlssnr.dll` | NVIDIA | The DLSS 5 neural rendering runtime. **This is a third-party build patched to run on Ada (RTX 40); the stock runtime is Blackwell-only.** | NVIDIA proprietary. Redistribution of a patched build is not covered by NVIDIA's own terms — see the warning below. |
| `nvngx_dlss.dll` | NVIDIA | The DLSS upscaling runtime. Optional; only used if the add-on's work-in-progress upscaling path is enabled. | NVIDIA proprietary, per the NVIDIA DLSS SDK licence. |
| `dxgi.dll` | crosire | ReShade 6.8, the add-on-enabled build. Hosts the add-on inside the sidecar's own process. | ReShade is BSD 3-Clause. Redistribution is permitted with its copyright notice. |
| `renodx-dlss5.addon64` | RenoDX | The DLSS 5 Generic add-on. Detours the sidecar's own NGX calls and substitutes neural-rendered output. | RenoDX is MIT-licensed. |

## Before making this repository public

**Remove the two NVIDIA binaries from the release assets first.**

`nvngx_dlssnr.dll` in particular is a *modified* build of a proprietary NVIDIA
runtime. Redistributing it publicly is outside anything NVIDIA licenses, and is
the kind of thing that attracts a takedown. While the repository is private the
assets are private with it, which is materially different from publishing them.

The supply-your-own model the manager was built around still works and is the
safe public arrangement: the **Setup** tab names each missing file, says what it
is for, and points at where it comes from, and copies in whatever the operator
browses to. Nothing in the application depends on the files being bundled.

This is a practical note, not legal advice.
