**English** | [Русский](CHANGELOG.ru.md)

# Changelog

What changed, and when. Entries for 0.1.0 through 0.1.2 are condensed from the
published release notes rather than written after the fact; the wording is
theirs, shortened.

## How this file works

**Versions are `MAJOR.MINOR.PATCH`, and the project is pre-1.0.** Before 1.0
there is no major number to bump, so the rule is the one semver states for
`0.x`: a release that changes behaviour people depend on raises the **minor**,
and a release that only fixes things raises the **patch**. Reaching 1.0 is a
decision about the project, not an outcome of the numbering.

**A tag is the release.** `vX.Y.Z` on `main`, matching the `VERSION` in
`CMakeLists.txt` — which is the single source of that number. It reaches the
binaries through `cmake/Version.h.in`, so the window title, the log's first
line and the tag cannot disagree.

**Entries go under `Unreleased` as the change is made**, in the pull request
that makes it, not gathered up at release time. A change nobody wrote down is a
change whose reason is gone by the time anyone needs it.

**Each entry says what it means for somebody using the program.** "Fixed a
device-id range" is a commit message. "The RTX 5070 was refused as unsupported"
is what the reader is looking for.

---

## Unreleased

## 0.2.0

Three things that were broken or missing rather than merely unpolished, and the
first release with an interface in a second language.

### Fixed

**The RTX 5070 was refused as an unsupported card.** The device-id table mapped
Blackwell over two ranges and GB205 continues neither: the 5070 is `0x2F04`,
past an unused gap, so a shipping RTX 50 card fell through to Unsupported. The
runtime exited with "RTX 40 or RTX 50 required" and the manager's Checks page
showed a failure — on exactly the class of card this exists for. The ids now
come from the display driver's own INF, and the tests name the whole published
desktop line rather than the two parts that happened to be to hand.

**The injector scan was never actually running.** It is the safety claim this
project is built around, and the folder it scans lived in a local variable in
the manager's frame loop rather than in the config, so every launch started
empty and the check reported "No WoW folder set, so nothing was scanned" —
permanently. The second half of the same guard, which refuses to run with the
sidecar installed inside the game's folder, could not fire either.

**Paths outside ASCII were corrupted in both directions.** `std::filesystem::path`'s
narrow accessors go through the process code page rather than UTF-8, and every
path here crosses the boundary between Windows and the interface at least
twice. A folder such as `D:\Игры\World of Warcraft` was drawn as mojibake and
read back as a directory that does not exist. ASCII-only paths hid it
completely.

### Added

**The WoW folder is a real setting, with a folder picker and detection.** It is
found from Battle.net's own uninstall entry, preferring the folder that holds
`Wow.exe` over its parent — an injector has to sit beside the executable to be
loaded by it, so scanning the parent proves nothing. Detection that finds
nothing leaves the field alone rather than inventing a path. The path is kept
in `sidecar.toml` and the text field no longer truncates at a fixed length.

**A Russian interface, with English still the default.** The switch is in the
header and reachable before the first-run notice is accepted: somebody who
cannot read the notice has to be able to change the language before agreeing to
it. The Windows locale is deliberately not consulted — every screenshot in the
documentation is English. Log lines stay English in both languages, because the
log is what travels back to the maintainer in a bug report.

**The interface scales itself.** The process declares per-monitor DPI
awareness and sizes its fonts and layout from the display. DPI alone is not
enough: a 49-inch panel at 100% scaling reports 96 DPI and is telling the truth,
while the interface on it is still unreadable across a desk. What DPI cannot
express is viewing distance, and physical size is the best proxy for it. The
controls that hold text now measure that text rather than trusting a constant,
so a longer translation cannot run out of its button. `ui_scale` in
`sidecar.toml` overrides the measurement; it is read once at startup,
because rebuilding the font atlas mid-frame is not something an
interface should do to itself.

**Multiple neural passes, off by default.** The public demonstrations for this
game run the filter three times; measured here, three passes change the frame
nine times more than turning the neural pass on at all -- and do it by removing
a quarter of the edge detail. `neural_passes` exists so that can be tried and
seen rather than argued about. One stays the default.

**The Log page shows the overlay's log** beside the manager's own. The manager's
half of any failure is the same two lines; the answer lives in the other
process's file, which previously meant opening a text editor in the install
folder.

**The version is on screen** — in the window title and at the foot of the
navigation rail — so a screenshot says which build it came from.

### Checked

**`ci/check_translations.py`**, alongside the existing import-invariant check.
Translations are keyed by their English source text, which makes a missing
entry harmless — the English shows — and a stale entry invisible: edit an
English string and its translation silently stops matching. The check catches
that, and has tests of its own.

**CI runs again.** Every run had failed since the `windows-latest` image became
the Visual Studio 2026 one: the workflow named "Visual Studio 17 2022", CMake
answered that it could not find any instance of Visual Studio, and the job died
at configure. The build, the tests and the invariant check had not run on any
commit for weeks.

---

## 0.1.3

Released before 0.2.0's work was merged, so the two overlap: the RTX 5070 fix
described under 0.2.0 above shipped here first.

### Fixed

**The RTX 5070 was refused as an unsupported card.** GB205 is `0x2F04`, past an
unused gap the Blackwell device-id table did not cover, so a shipping RTX 50
card fell through to Unsupported and the runtime would not start. Checking the
display driver's own INF rather than the bug report also turned up `0x2F06`, a
second RTX 5060 in the same block, which was refused for the same reason.

**The overlay was black on a display with scaling set above 100%.** Both
executables were DPI-unaware, so Windows reported a 2560×1440 game window as
2048×1152 at 125%. Windows Graphics Capture is not virtualised and delivered
frames at the real size, so the ring textures and the captured frame had
different dimensions — and `CopyResource` across a size mismatch returns void
and copies nothing. The ring slot stayed zeroed and presented as pure black,
with healthy counters, correct pacing and an empty log, which is why it looked
like a working pipeline and reproduced even in passthrough.

Both entry points now declare per-monitor-v2 awareness. `DeviceBridge` also
refuses a frame whose dimensions do not match and logs both sizes, because a
silent all-black overlay is the worst way for that class of mistake to present
itself.

## 0.1.2

### Fixed

**The Status page no longer moves under the cursor.** Advisories sat above the
buttons, so every time one appeared the page jumped and buttons slid away
mid-click. The buttons now sit above the diagnosis, the advisory lives in a
fixed-height box that says something in every state, and each condition
switches with hysteresis so a frame rate sitting on a threshold no longer
strobes the message on and off.

**GPU memory reports what actually matters.** The old bar showed usage against
a per-process budget that Windows keeps generous until contention bites — it
read *1256 of 15280 MB*, apparently plenty, while the card was full and the
driver was already paging. It now also reports memory pushed out into system
RAM, which is the real alarm: non-zero means every frame is waiting on the PCIe
bus. It caught 105 MB spilled on the first run with a reading that had looked
comfortable. The bar is also relabelled to say it is the sidecar's own share,
not the whole card.

## 0.1.1

### Fixed

**Only the DLSS 5 add-on runs.** ReShade's default pointed its effect search at
the sidecar's own directory, so any `.fx` dropped in beside it would have been
compiled and blended on top of the neural pass with nothing to say so. The
search paths are now pinned at a directory that does not exist.

**Clearing an advanced strength actually clears it.** "Unset" meant "skip the
key", and skipping a key in a merge leaves whatever was there before — so a
value from an earlier session survived every later save while the manager
reported the setting as untouched.

**The runtime exits when the game closes.** Restarting WoW used to leave a
zombie: the render loop stopped and the overlay hid itself, but the process
stayed up with its control channel open, so the manager went on reporting
"Overlay: running".

**The Status page explains a bad frame rate instead of leaving you to guess.**
Captured-versus-presented frames, a GPU memory bar, and a four-way split of
where each frame's time goes. When the overlay falls short of the capture rate
it says the game is competing for the GPU and points at the fix.

## 0.1.0

First release. Everything needed to run, in one download.

Superseded by 0.1.1, which fixed three problems in it: ReShade's effect search
pointing at the sidecar's own directory, clearing an advanced strength not
clearing it, and a restarted game leaving the runtime up with a dead render
loop.
