# Changelog

## 1.0.0

First public release.

- Hides RaceMenu overlay shapes that hold the blank default texture, so empty overlay slots stop costing draw calls; shows them again the moment a real texture is written.
- One DLL for Skyrim SE 1.5.97 and AE 1.6.x / 1.7.x (Address Library). Tested only on AE 1.6.1170.
- Settings in `Data/SKSE/Plugins/OverlayFpsHelper.toml`: `enabled`, `intervalMs`, `rescanMs`, `radius`, `includePlayer`, and the experimental `cullTransparent` (off by default).
- No menu, no save data, no ESP.
