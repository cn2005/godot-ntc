# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-09-21

First public release. Scope is **inference on load only** (architecture L1 / A1).

### Added

- `NTCTextureSet` resource that loads a `.ntc` file and decodes it on a dedicated Vulkan device.
- `NTCSMaterial3D`, a `StandardMaterial3D` subclass with `NONE` and `ON_LOAD` modes.
- Editor CUDA compression via `rebuild_ntc()`, default 5 bits/texel.
- Reuse of an existing `.ntc` when source textures and settings have not changed.
- Editor export plugin that strips source maps when the `ntc_strip_sources` custom feature is set.
- Demo project for Godot 4.7.2 (Vulkan Forward+) and a 16-material compare pair.

### Limits

- Windows x86_64 only.
- NVIDIA GPU required. CUDA is required to compress in the editor.
- Inference on sample, same-device `Texture2DRD`, macOS, Linux, and Android are not included.
