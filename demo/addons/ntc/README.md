# NTC On Load for Godot

[![Godot 4.7](https://img.shields.io/badge/Godot-4.7.2-478cbf.svg)](https://godotengine.org)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x86__64-0078d4.svg)](#requirements)

A [Godot](https://godotengine.org) 4.7 **GDExtension** that wraps [NVIDIA RTX Neural Texture Compression](https://github.com/NVIDIA-RTX/RTXNTC) for **inference on load**.

Assign albedo, normal, roughness, and metallic maps to `NTCSMaterial3D`, compress them once to a `.ntc` file, and ship that file instead of imported BCn textures. At load time the extension decodes the set on a dedicated Vulkan device and writes `ImageTexture`s back onto the usual `StandardMaterial3D` slots.

This reduces **download and pack size**. After decode, VRAM is still BCn-sized. The plugin does **not** change the Godot renderer and does **not** implement inference on sample.

This project is **not** an official NVIDIA product.

![PBR sets used by the compare demo](media/preview-materials.png)

## What works in 0.1.0

| Feature | Status |
| --- | --- |
| Editor CUDA compress to `.ntc` (default 5 bpp) | Yes |
| Reopen a scene without recompressing | Yes |
| Runtime / export decode into `StandardMaterial3D` slots | Yes |
| Export feature `ntc_strip_sources` drops source maps | Yes |
| Inference on sample, `Texture2DRD`, macOS, Linux, Android | No |

Measured on Windows, Godot 4.7.2, Vulkan Forward+, LibNTC 0.10.0, RTX 3060, sixteen 4K PBR sets:

| Pack | Size |
| --- | ---: |
| Source JPEG/PNG | 707.90 MB |
| Standard Godot pack (VRAM BCn) | 835.39 MB |
| NTCS pack (`.ntc` once) | 153.53 MB |

MetalPlates013 albedo, 64×64 crop after decode: PSNR ≈ 23.2 dB at 5 bpp. That is a disk-size trade, not a lossless archive.

## Requirements

- Godot **4.7.2** (Vulkan Forward+). Compatibility / Mobile renderers are not tested.
- Windows 10/11 **x86_64**.
- NVIDIA GPU. CUDA is required **only** to compress in the editor.
- [NVIDIA RTX NTC SDK](https://github.com/NVIDIA-RTX/RTXNTC) **v0.10.0** (LibNTC). Accept NVIDIA's license before you use or redistribute `libntc.dll`.
- Visual Studio 2022, CMake 3.17+, and Git (to fetch godot-cpp and Vulkan-Headers).

## Quick start (editor)

1. Build the extension (see below) so these files exist:
   - `demo/bin/windows/libntc_godot.dll`
   - `demo/bin/windows/libntc.dll`
2. Open `demo/` with Godot 4.7.2 and enable the **NTC On Load** plugin.
3. Create an `NTCSMaterial3D`, set albedo / normal / roughness / metallic, then set **Ntc Mode** to **On Load**.
4. Wait until the inspector status is a finished `.ntc` (not a second training run). Opening the scene again must print `Using existing .ntc`, not `BeginCompression`.
5. For a shipped build, add the custom feature `ntc_strip_sources` on the Windows export preset so source JPEG/PNG/BCn are left out of the PCK.

GDScript:

```gdscript
var mat := NTCSMaterial3D.new()
mat.albedo_texture = preload("res://albedo.png")
mat.normal_texture = preload("res://normal.png")
mat.roughness_texture = preload("res://roughness.png")
mat.metallic_texture = preload("res://metallic.png")
mat.ntc_mode = NTCSMaterial3D.NTC_MODE_ON_LOAD
# Editor: CUDA compress → sidecar .ntc. Source textures stay for authoring.
# Runtime / export: decode .ntc into the StandardMaterial3D slots.
```

## Install into another project

Copy both of these trees next to your `project.godot`:

```text
addons/ntc/          # editor plugin (plugin.cfg + export hook)
bin/ntc.gdextension
bin/windows/libntc_godot.dll
bin/windows/libntc.dll
```

GitHub Releases ship a ZIP already laid out that way. Building from source writes the DLLs into `demo/bin/windows/`.

Redistributing `libntc.dll` is allowed only as part of an application, under the NVIDIA RTX SDKs license. See [NOTICE.md](NOTICE.md) and [LICENSES/NVIDIA-RTX-SDKs.txt](LICENSES/NVIDIA-RTX-SDKs.txt).

## Build

Place this repository next to a full RTX NTC SDK checkout, **or** pass `-DNTC_ROOT=` to CMake. The SDK root must contain `libraries/RTXNTC-Library` and `bin/windows-x64/libntc.dll`.

```bat
git clone https://github.com/cn2005/godot-ntc.git
cd godot-ntc
git clone --branch 10.0.0-stable --depth 1 https://github.com/godotengine/godot-cpp.git thirdparty/godot-cpp

godot --path demo --dump-extension-api --headless
move demo\extension_api.json extension_api.json

mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 -DNTC_ROOT=C:\path\to\RTXNTC ..
cmake --build . --config RelWithDebInfo
```

Outputs:

- `demo/bin/windows/libntc_godot.dll`
- `demo/bin/windows/libntc.dll` (copied from the SDK)

On Windows the editor locks those DLLs. Restart Godot after every native rebuild.

## Tests

From a machine that already has Godot 4.7.2 and a built extension:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_release_tests.ps1
```

The suite checks extension load, decode, reuse-without-recompress, export PCK contents, pack-size ratio, exported exe smoke, and a clean project that only has the extension plus one `.ntc`.

## API surface (L1)

- `NtcRuntime` — singleton. Call `initialize()` before decode.
- `NTCTextureSet` — `.ntc` bytes, metadata, decoded `Texture2D`s by semantic name.
- `NTCSMaterial3D` — `ntc_mode` (`NONE` / `ON_LOAD`), `ntc_bits_per_pixel`, `ntc_training_steps`, `rebuild_ntc()`, `apply_decoded()`.

Do not invent a third mode on this class. On-sample would be a different material and a different shader owner. See [docs/NTC-Godot-Architecture.md](docs/NTC-Godot-Architecture.md).

## License

- This repository's original source and docs: [MIT](LICENSE), Copyright (c) 2026 cn2005.
- LibNTC / NTC format: [NVIDIA RTX SDKs license](LICENSES/NVIDIA-RTX-SDKs.txt).
- Demo MetalPlates013 maps: ambientCG, CC0 1.0.

## 中文

这是 Godot 4.7 的 GDExtension，把 NVIDIA RTX NTC 接到 **加载时推理** 上：`NTCSMaterial3D` 在编辑器里用 CUDA 压出 `.ntc`，运行/导出时解码回 `StandardMaterial3D` 槽位。省的是 **包体和硬盘**，解码后显存仍按 BCn 计。

当前只做 L1 / A1。没有 on-sample、没有改引擎、没有 macOS / Android。需要 Windows x86_64、Vulkan Forward+、NVIDIA GPU；压缩还要 CUDA。仓库代码 MIT；`libntc.dll` 仍受 NVIDIA RTX SDK 许可约束，不能当成独立 SDK 再分发。

16 套 4K PBR、Godot 官方模板导出：NTCS PCK 153.53 MB，对照 Standard PCK 835.39 MB。
