# RaceMenu Overlay FPS Helper - source code and build instructions (version 1.0.0)

Free software under the GNU GPL v3 or later (see `LICENSE`). Copyright (C) 2026 Bolescothegoat.
Third-party licences: `THIRD_PARTY_NOTICES.txt`.

An SKSE plugin (C++23) for Skyrim SE 1.5.97 and AE 1.6.x / 1.7.x (one DLL; the engine addresses come from Address Library). It hides RaceMenu overlay shapes that hold the blank default texture.

Built with AI assistance (Claude, by Anthropic) from the author's designs and in-game testing.

## Layout

| File | What it is |
|---|---|
| `src/main.cpp` | SKSE plugin entry (`SKSEPlugin_Version` for AE, `SKSEPlugin_Query` for SE 1.5.97), logging, load-event hooks |
| `src/Cull.h`, `src/Cull.cpp` | Everything else: config, the per-actor shape cache, the scan, hiding / showing shapes |
| `dist/SKSE/Plugins/OverlayFpsHelper.toml` | The default settings file shipped with the mod |

## Building

Prerequisites: Visual Studio 2022+ (MSVC, C++23), CMake 3.21+, Ninja, [vcpkg](https://github.com/microsoft/vcpkg), and:

1. **CommonLibSSE-NG 15.5.0** - a checkout of <https://github.com/alandtse/CommonLibSSE-NG> at tag `v15.5.0` (commit `8eadded`). Not the older
   CharmedBaryon repository: it cannot read the AE 1.7 Address Library files. Set the environment variable `COMMONLIB_SSE_FOLDER` to the checkout folder.
2. vcpkg packages (`spdlog`, `fmt`, `tomlplusplus`, and the ones CommonLibSSE-NG needs) are installed automatically from `vcpkg.json`; set `VCPKG_ROOT` to your vcpkg folder.

```
cmake --preset release
cmake --build --preset release
```

The DLL is written to `dist/SKSE/Plugins/OverlayFpsHelper.dll`. At run time the plugin needs SKSE and the Address Library for your game version (and RaceMenu, or there is nothing for it to do).

The CMake file also adds `/d1trimfile` for the source, library and package folders, so the build machine's folder names do not end up inside the DLL (the log macros embed `__FILE__`).

## Notes for hackers

* **What it touches:** nothing in the engine is hooked or patched. It walks each actor's scene graph with `RE::BSVisit::TraverseScenegraphGeometries`, and for every geometry whose name ends in `[Ovl#]` or `[SOvl#]` (any case) reads the lighting shader property's diffuse texture path (`BSLightingShaderMaterialBase::textureSet`, slot `kDiffuse`) and calls `SetAppCulled`. All of it runs on the main thread, inside a task queued with `SKSE::GetTaskInterface()->AddTask` by a timer thread, one task at a time.
* **Blank texture:** RaceMenu's default overlay texture ends in `overlays\default.dds` (or with `/`); an empty path counts as blank too. The test is a case-insensitive tail compare, no allocation.
* **Cache:** per actor FormID: the root `NiAVObject*` (identity only, never dereferenced), a vector of overlay shapes (`NiPointer<BSGeometry>`, and an owned `NiPointer<BSShaderProperty>` so its address cannot be reused while being compared against). The cache is dropped on `kPreLoadGame` / `kNewGame`.
* **Alpha-0 overlays:** `cullTransparent` also hides shapes with `BSShaderProperty::alpha <= 0.001`. It is off by default because hiding a shape while its alpha is momentarily 0 can stop the real alpha from ever being applied by another mod.
* **Not done:** VR; actors outside the high-process list; reading RaceMenu's own slot counts (it does not need to).
