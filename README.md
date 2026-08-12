# CamelCrusherBridge

Runs **CamelCrusher** (Camel Audio, 2011) natively on Apple Silicon.

The original plugin is x86_64-only, so on an Apple Silicon Mac it forces the
whole host into Rosetta — or simply doesn't load. This project builds an
**arm64 VST2 and Audio Unit** that the host loads natively, and hosts the
original Intel binary in a separate x86_64 helper process, passing audio
between the two over POSIX shared memory.

The result is bit-identical output to the original, in a native session.

> This repository contains original code only. It does **not** redistribute
> CamelCrusher, its skin artwork, or any other Camel Audio property. You need
> your own copy of the plugin to build anything here.

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

## Requirements

- Apple Silicon Mac, macOS 11+
- Xcode command line tools (`clang`)
- **Your own copy of CamelCrusher**, with:
  - the original Intel VST at
    `/Library/Application Support/Camel Audio/CamelCrusherOriginal.vst`
  - its data at `/Library/Application Support/Camel Audio/CamelCrusherData`

Keep a pristine archived copy of the original at the `CamelCrusherOriginal.vst`
path above and point the build scripts at that — never at an install slot,
which after a build holds the arm64 bridge instead.

## Build

```bash
./build.sh      # VST2 → /Library/Audio/Plug-Ins/VST/CamelCrusher.vst
./build_au.sh   # AU   → ~/Library/Audio/Plug-Ins/Components/CamelCrusher.component
```

Each script takes an optional argument to override the output path. Both
compile the x86_64 helper and the arm64 front-end, assemble the bundle, embed
the original plugin and skin, and ad-hoc codesign the result.

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
