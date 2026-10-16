# CamelCrusherBridge

Runs **CamelCrusher** (Camel Audio, 2011) natively on Apple Silicon.

The original plugin is x86_64-only, so on an Apple Silicon Mac it forces the
whole host into Rosetta — or simply doesn't load. This project builds an
**arm64 VST2 and Audio Unit** that the host loads natively, and hosts the
original Intel binary in a separate x86_64 helper process, passing audio
between the two over POSIX shared memory.

The result is bit-identical output to the original, in a native session.

## 1.0.4: Audio Unit editor crash fix

Fixes the crash when opening the AU editor in Ableton Live. The shared editor
now uses the bridge's program APIs instead of calling an unset VST dispatcher
inside the AU. AU views are detached and their polling timers are stopped when
the unit is disposed or its editor is replaced. The AU and VST use distinct
Objective-C view class names when loaded into the same host.

**Use 1.0.4 instead of 1.0.3.** Audio-only validation did not expose the editor
crash. The new editor regression test reproduces the crash in 1.0.3 and passes
25 open/change-preset/redraw/replace/dispose cycles in 1.0.4 with VST co-loaded.

## 1.0.3: factory presets and a graphical installer

Both formats now load the supplied 20-program CamelCrusher factory bank at
startup. The preset menu shows names such as Annihilate, British Clean and
Subtle Master instead of twenty Init slots. Session state restored by your
DAW is applied after the factory bank, preserving saved settings. VST2 also
reports the selected program's name correctly.

The release includes **Install CamelCrusher Native.app** and a standalone
**.pkg**. Both use the macOS Installer wizard to install VST2 and AU together.
The previous system bundles are backed up before replacement.

The separate Tianzii CamelCrusher Modern `.fst` files are FL Studio Patcher
layouts and are not included: their supplied license prohibits reuploading.

## 1.0.2: lower CPU use

The bridge and Intel helper now sleep while waiting for each other instead of
continuously spinning. Sessions with several CamelCrusher instances no longer
keep a CPU core busy per helper just waiting for audio work. Restart your DAW
after updating so both halves of the bridge reload.

On macOS 14.4 and later, shared-address notifications wake each process when
work or a result is ready. Earlier systems use short sleeps. The helper remains
compatible with an older bridge already loaded in a running host, and its
startup handshake no longer risks missing the first request.

In a controlled 48 kHz test at a 512-sample buffer, helper CPU fell from 75.49%
to 2.13% of one core. Six comparisons covering factory presets and parameter
automation produced bit-identical audio to the previous build in both release
formats. Real-time checks at 128, 256, 512, and 1024 samples returned finite
audio; a release check at 128 samples on a busy Mac had two late blocks.
CPU use and scheduling latency depend on hardware, buffer size, and system load.

---

## Install

Requires an **Apple Silicon Mac running macOS 11 or later**, **Rosetta 2**,
and an administrator account. The release contains both plugins and the bank;
Xcode and an existing CamelCrusher installation are not needed.

1. **[Download the latest release][latest]**.
2. Extract `CamelCrusherNative-1.0.4-Installer.zip` and open
   **Install CamelCrusher Native.app**, or open the standalone `.pkg`.
3. Save your work and quit your DAW. Choose **Continue to Installer**, then
   follow the macOS Installer wizard. It asks for an administrator password.
4. Restart your DAW and rescan plugins.

[latest]: https://github.com/slushiimusic/CamelCrusherBridge/releases/latest

This build is ad-hoc signed, not Developer ID signed or notarized. macOS may
require approval in **System Settings → Privacy & Security → Open Anyway**.
The installer does not disable Gatekeeper or change system security settings.
If Rosetta is missing, install it through macOS before running the installer.

### Installed locations and backups

| Format | Location |
|---|---|
| VST2 | `/Library/Audio/Plug-Ins/VST/CamelCrusher.vst` |
| Audio Unit | `/Library/Audio/Plug-Ins/Components/CamelCrusher.component` |

The bank is embedded in each bundle at
`Contents/Resources/CamelCrusherPresetsA.fxb`. No separate data-folder copy is
needed. Existing system bundles are copied to a timestamped directory under
`/Library/Application Support/CamelCrusherBridge/Backups` before replacement.
If you previously installed a user-level copy in `~/Library/Audio/Plug-Ins`,
move that older copy out of the scan folder to avoid duplicate entries.

The legacy `install.command` remains available for source builds and users
who need its per-user fallback. The graphical package installs system-wide.

### Making your DAW find it

**Ableton Live** — Preferences → **Plug-Ins**. Turn on **Use Audio Units** and
**Use VST2 Plug-In System Folders**, then click **Rescan**. This is a VST2, not
a VST3, so the VST3 toggle won't surface it. It appears in the browser under
Plug-Ins → Camel Audio.

**Logic Pro** — Audio Units only. Logic validates new components at launch, so
just restart it; CamelCrusher shows under Audio Units → Camel Audio.

Both formats present the original plugin's identity, so **projects that already
used CamelCrusher re-link to this build on their own** — no missing-plugin
placeholder, and nothing to re-dial by hand.

### Uninstalling

Delete whichever of these exist:

```bash
rm -rf ~/Library/Audio/Plug-Ins/VST/CamelCrusher.vst ~/Library/Audio/Plug-Ins/Components/CamelCrusher.component
```

The system folders are `root:wheel`, so removing a bundle there needs `sudo`:

```bash
sudo rm -rf /Library/Audio/Plug-Ins/VST/CamelCrusher.vst /Library/Audio/Plug-Ins/Components/CamelCrusher.component
```

> **On what's bundled.** This repository is original code only — building from
> it requires your own copy of CamelCrusher. The release disk image additionally
> carries Camel Audio's original plugin and skin art so it installs on a machine
> that never had it. CamelCrusher was released as freeware and has had no
> official download since Camel Audio was acquired in 2015. All rights in
> CamelCrusher itself remain with its owners; this project is unaffiliated with
> Camel Audio and with Apple.

---

## Why this exists

CamelCrusher was in my chain for years. It's on a lot of my old sessions, and
it isn't the kind of plugin you swap out. Then it stopped working — the GUI
went blank on newer macOS, Camel Audio had been gone since 2015, and once I
moved to Apple Silicon the Intel-only binary was the end of it.

I went five years without it. Every project built around it opened broken:
a missing plugin, or one I could load but couldn't see or trust. I spent a lot
of that time looking for a replacement. Output's Thermal, a Max for Live device
called Live Crusher — which I loved — and Ableton's own Amp all earned their
place in the meantime. But a substitute you like is still a substitute: none of
them were the thing those old sessions were written around, and none of them
could make those sessions play back the way they were made.

So the alternative turned out to be the original itself, running again. This
was a labor of love more than an engineering plan.

**It works now.** It loads natively on Apple Silicon under current macOS, and
the interface — blank since macOS 10.14 — renders again, drawn from the
original artwork. Old projects re-link and play back correctly. Nothing to
re-dial from memory.

---

## How it works

```
   ┌──────────────────────────┐         ┌──────────────────────────┐
   │  host (arm64, native)    │         │  helper (x86_64)         │
   │                          │         │                          │
   │  plugin.c  VST2 ────┐    │  shared │   helper.c               │
   │  au.c      AU  ─────┼──► │  memory │   └─► CamelCrusher.vst   │
   │  gui.m     Cocoa UI │    │ ◄─────► │       (original, Intel,  │
   │  bridge.c  transport┘    │         │        run headless)     │
   └──────────────────────────┘         └──────────────────────────┘
```

`bridge_common.h` defines the shared-memory protocol — a single `BridgeSHM`
struct whose layout is byte-identical under arm64 and x86_64 (both LP64
little-endian). The plugin bumps `reqSeq` to submit work, the helper bumps
`ackSeq` when it's done. Parameter writes are lock-free: `setParameter()` only
sets a bit in `paramDirty`, and the helper flushes dirty parameters into the
hosted plugin just before the next render.

### The GUI is redrawn, not forwarded

CamelCrusher's own editor is blank on macOS 10.14 and later — it links VSTGUI
3.x against Carbon, and layer-backed `NSView`s broke its draw path. Forwarding
the original UI would forward a blank window.

So the Intel plugin runs **headless**, and `gui.m` redraws the interface
natively in Cocoa from the original skin bitmaps in
`/Library/Application Support/Camel Audio/CamelCrusherData/Skins/default`.
The shipped `SkinParameters.txt` supplies exact control coordinates and the
bitmap-font glyph widths, so the native UI matches the 2011 plugin rather than
approximating it. Built bundles copy the skin into `Contents/Resources/Skins`
so they're self-contained.

The practical upshot: the editor that had been blank for years is usable again
on current macOS, on Apple Silicon, in a native (non-Rosetta) host.

### It impersonates the original's identity

Both formats deliberately present the original's plugin identity, so existing
projects re-link to this build instead of reporting a missing plugin:

| | value |
|---|---|
| VST2 unique ID | `0x43614372` (`'CaCr'`) |
| VST2 flags | `0x39` — canReplacing, programChunks, hasEditor, canMono |
| AU type/subtype/manufacturer | `aumf` / `CaCr` / `CamA` |

The AU identity was recovered from the `thng` resource in the original's
`.rsrc`, since Camel Audio's AU predates `AudioComponents` plists.

---

## Requirements for building from source

These apply to building from a checkout of this repo. **Installing from the
release disk image needs none of them** — see [Install](#install) above.

- Apple Silicon Mac, macOS 11+
- Xcode command line tools (`clang`) with the macOS 14.4 SDK or newer
- **Your own copy of CamelCrusher**, with:
  - the original Intel VST at
    `/Library/Application Support/Camel Audio/CamelCrusherOriginal.vst`
  - its data at `/Library/Application Support/Camel Audio/CamelCrusherData`

Keep a pristine archived copy of the original at the `CamelCrusherOriginal.vst`
path above and point the build scripts at that — never at an install slot,
which after a build holds the arm64 bridge instead.

## Build from source

```bash
./install.command   # find the original, build both formats, install them
```

Run from a checkout, `install.command` switches to build-from-source mode: it
locates an original CamelCrusher already on the machine, stages a private copy
of it (so overwriting an install slot can't destroy the last genuine copy),
builds, and installs. The same script installs the ready-built bundles when it
runs from the release disk image instead.

To drive the builds directly:

```bash
./build.sh      # VST2 → /Library/Audio/Plug-Ins/VST/CamelCrusher.vst
./build_au.sh   # AU   → ~/Library/Audio/Plug-Ins/Components/CamelCrusher.component
```

To package a release, build both bundles under `build/Plugins`, supplying
`CC_ORIG`, `CC_SKIN`, and `CC_BANK` with your original plugin, skin directory,
and `CamelCrusherPresetsA.fxb` respectively. Then run `python3 build_installer.py`.
It produces the installer app ZIP and standalone PKG in `dist`. Proprietary
plugin, skin and preset assets are supplied at build time and are not committed.

Each takes an optional argument to override the output path, and reads
`$CC_ORIG` / `$CC_SKIN` / `$CC_BANK` to override where the original plugin and skin art come
from. Both compile the x86_64 helper and the arm64 front-end, assemble the
bundle, embed the original plugin and skin, and ad-hoc codesign the result.
Builds target macOS 11 by default; `MACOSX_DEPLOYMENT_TARGET` can override this.
The new wait APIs are guarded at runtime. The fallback for systems older than
14.4 has not been exercised on an older OS in the 1.0.2 release checks.

Note: `/Library/Audio/Plug-Ins/VST` is `root:wheel`, so a bundle directory
there may not be removable even when its contents are yours. The scripts clear
the bundle in place rather than deleting and recreating it.

## Verifying

The repo includes the tooling used to prove the bridge is transparent:

```bash
clang -arch x86_64 -O2 -o dump_orig dumper.c    # render through the original
clang -arch arm64  -O2 -o dump_brdg dumper.c    # render through the bridge
./dump_orig <plugin.vst> orig.raw
./dump_brdg <plugin.vst> brdg.raw
./nulltest.py orig.raw brdg.raw
```

`dumper.c` renders a deterministic noise-plus-tone signal (44.1 kHz, 512-frame
blocks, 200 blocks) through a VST2 plugin and dumps raw float32;
`nulltest.py` compares two dumps sample by sample. `audump.c` does the same
through the Audio Unit so the AU can be checked against the original VST.

Recorded results from the original build-out: **204800/204800 samples
identical** — a perfect null — across presets and live parameter automation,
and `auval` passes clean.

## Files

| file | role |
|---|---|
| `plugin.c` | arm64 VST2 front-end |
| `au.c` | arm64 Audio Unit front-end |
| `auview.m` | Cocoa view factory for the AU (`kAudioUnitProperty_CocoaUI`) |
| `gui.m` | native Cocoa redraw of the original skin |
| `bridge.c` / `bridge.h` | shared-memory transport, plugin side |
| `helper.c` | x86_64 helper that hosts the original headlessly |
| `bridge_common.h` | the shared-memory protocol struct |
| `vst2.h` | minimal VST2 interface declarations |
| `build.sh` / `build_au.sh` | bundle builds |
| `probe.c` | dumps a VST2 plugin's reported capabilities |
| `dumper.c` / `audump.c` / `nulltest.py` | null-test harness |

## Status

Complete and in use — five years of broken sessions open correctly again.
Verified in Ableton Live 12 on Apple Silicon.

---

## License

All rights reserved. This repository is published for reference and is **not**
open source — see [LICENSE](LICENSE). The bundled CamelCrusher is not covered
by that notice and remains the property of its owners.

## Preset release checks

Build the test tools into `build`:

```bash
clang -arch arm64 -O2 preset_check.c -o build/preset_check
clang -arch x86_64 -O2 preset_check.c -o build/preset_check_orig
clang -arch arm64 -O2 dumper.c -o build/dump_brdg
clang -arch x86_64 -O2 dumper.c -o build/dump_orig
clang -arch arm64 -O2 audump.c -framework AudioUnit -framework AudioToolbox -framework CoreFoundation -o build/audump
python3 verify_release.py
```

`preset_check` takes a plugin executable path and optionally an FXB path for
the original plugin. It checks all preset names, prints all parameter values,
and checks a modified bank survives a save/restore cycle. Compare its output
between the original and bridge. `verify_release.py` compares rendered audio
for every factory preset in the staged VST2 and AU against the original with
the same bank. The AU is registered only within the test process.

To exercise the AU editor, including concurrent loading of the VST format:

```bash
clang -arch arm64 -O1 -g -fobjc-arc editor_check.m -framework Cocoa -framework AudioUnit -framework AudioToolbox -o build/editor_check
build/editor_check "$PWD/build/Plugins/CamelCrusher.component" "$PWD/build/Plugins/CamelCrusher.vst/Contents/MacOS/CamelCrusher" "$PWD/build/au-editor.png"
```

Run this in a graphical macOS session. It opens the Cocoa factory view in an
isolated host, selects every preset and checks wraparound, redraws, replaces
views, and retains a view across AU disposal to check safe detachment. It does
not open or modify an Ableton project.
