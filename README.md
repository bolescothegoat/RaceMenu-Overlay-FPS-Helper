# RaceMenu Overlay FPS Helper

An SKSE plugin for Skyrim SE / AE that stops **empty RaceMenu overlay slots from costing you frames**.

RaceMenu (skee) gives every actor one drawable shape for each overlay slot (`Body [Ovl0..N]`, `Hands`, `Feet`, `Face`, plus the spell overlays `[SOvl#]`), whether or not anything is painted in it. An empty slot holds RaceMenu's blank default texture, but the renderer still submits it: one draw call per slot, per rendering pass, for every actor on screen. With the slot counts many overlay mods ask you to set, a single NPC can carry well over 80 of these shapes, and a busy town can spend more of its frame on invisible overlays than on the characters.

This plugin hides every overlay shape that holds the blank texture, and shows it again the moment a real texture is written to it. Your slot counts, your overlay mods and anything that paints overlays at run time keep working unchanged. There is no menu and no save data.

- Source: https://github.com/bolescothegoat/RaceMenu-Overlay-FPS-Helper
- License: GNU GPL v3 or later (see `LICENSE`); third-party licences in `THIRD_PARTY_NOTICES.txt`
- Build instructions: `BUILDING.md`

## What it did on one test install

Measured on a single machine (Ryzen 7 5800X3D, RTX 2080, 120 Hz display) with RaceMenu's slot counts raised to Body 34 / Hands 9 / Feet 9 / Face 30 (the defaults are 6 / 3 / 3 / 3), standing in a busy town market with about 30 NPCs around:

| | Without | With |
|---|---|---|
| Visible shapes (about 30 NPCs) | about 2,000 | about 600 |
| Shapes per NPC | about 63 | about 20 |
| Frame rate in the busy market | about 92 | 120-122 (the display's limit) |
| Overlay shapes on a typical female NPC | 86, nearly all empty | only the ones in use |

A female NPC in that test had 86 overlay shapes (nearly all of them empty) against about 28 real parts (body, head, hair, armour); male NPCs carried fewer (45 in one measurement). The frame rate was limited by the display, so the real gain is at least that much. The plugin itself costs about 0.12-0.15 ms per scan, ten scans a second.

Your results will depend on your slot counts, your CPU and how many actors are around. With RaceMenu's default slot counts there is much less to hide. This is a CPU-side saving (fewer draw calls for the game's main thread), so it helps most when that thread is the bottleneck.

## Requirements

- Skyrim SE 1.5.97 or AE (1.6.x / 1.7.x): one DLL, the engine addresses come from Address Library. **Tested only on AE 1.6.1170**; other versions are expected to work but are untested. VR is not supported.
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) for your game version
- [RaceMenu](https://www.nexusmods.com/skyrimspecialedition/mods/19080). Without RaceMenu there are no overlay shapes and the plugin does nothing.

## Install

Install it with your mod manager like any SKSE plugin (it needs no ESP). It loads at game start. To remove it, uninstall it: it stores nothing in your save.

## Settings

`Data/SKSE/Plugins/OverlayFpsHelper.toml` (read at game start). The defaults suit almost everyone.

| Setting | Default | What it does |
|---|---|---|
| `enabled` | `true` | Switch it off without uninstalling |
| `intervalMs` | `100` | How often the known overlay shapes are re-checked (20-2000 ms). A freshly painted overlay appears within this time. Higher is cheaper. |
| `rescanMs` | `1000` | How often each actor's scene graph is fully re-walked to find newly created overlay shapes (100-10000 ms). Staggered per actor. |
| `radius` | `0` | `0` scans every loaded actor; otherwise only actors within this many game units of you |
| `includePlayer` | `true` | Also hide your own empty slots |
| `cullTransparent` | `false` | **Experimental, leave off.** Also hides overlays that hold a texture but have alpha 0. On at least one setup this stopped another mod's overlay alpha from ever being applied (all body hair vanished), and it saves only a handful of shapes. |

The plugin writes `OverlayFpsHelper.log` (in your SKSE log folder). Every 30 seconds it records how many shapes it is hiding and what a scan costs.

## How it works

Each actor's overlay shapes are found once and cached; the regular scan only re-checks those cached shapes (the tail of the diffuse texture path, no allocation). An overlay shape whose diffuse texture is RaceMenu's blank `overlays\default.dds` gets the engine's "culled" flag, so the renderer skips it in every pass. When RaceMenu or an overlay mod writes a real texture into it, the next scan clears the flag. A full re-walk of an actor happens only when its 3D object changes or every `rescanMs`. The cache is dropped before a save loads, so no shape is kept alive across a load.

## Limits and honest notes

- A new overlay shows up to `intervalMs` (default 0.1 s) after it is written; an empty slot RaceMenu has just created can be drawn for up to `rescanMs` before it is found.
- Only actors in the engine's high-process list (the ones around you) are scanned.
- Anything that reads the hidden flag of an overlay shape would see it set on empty slots. None was found in testing, but I cannot test every mod.
- Tested on one install and one game version. Reports from other setups are welcome.

## Credits

[RaceMenu](https://www.nexusmods.com/skyrimspecialedition/mods/19080) (expired6978 and contributors) created the overlay system this works with. Built on [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) (alandtse's fork of Ryan-rsm-McKenzie's library).

Built with AI assistance (Claude, by Anthropic) from the author's designs and in-game testing.
