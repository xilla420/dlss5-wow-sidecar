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

## A note on the NVIDIA runtimes

`nvngx_dlssnr.dll` shipped in the release is a **third-party build patched to run
on Ada**. NVIDIA's own runtime is Blackwell-only. Redistributing a modified
proprietary binary is not covered by NVIDIA's licence terms, and it is bundled
here for convenience with that understood.

If NVIDIA objects, the fix is to pull the two `nvngx_*.dll` files from the
release assets. Nothing in the application depends on them being bundled: the
**Setup** tab was built for the supply-your-own arrangement, names each missing
file, says what it is for, points at where it comes from, and copies in whatever
you browse to. Removing them from the release costs users one download and
costs this project nothing.

This is a practical note, not legal advice.
