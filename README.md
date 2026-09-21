# NTC On Load for Godot

[![Godot 4.7](https://img.shields.io/badge/Godot-4.7.2-478cbf.svg)](https://godotengine.org)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x86__64-0078d4.svg)](#requirements)
[![LibNTC 0.10.0](https://img.shields.io/badge/LibNTC-0.10.0-76b900.svg)](https://github.com/NVIDIA-RTX/RTXNTC)

A [Godot](https://godotengine.org) 4.7 **GDExtension** that wraps [NVIDIA RTX Neural Texture Compression](https://github.com/NVIDIA-RTX/RTXNTC) (LibNTC) for **inference on load**.

You assign albedo, normal, roughness, metallic, AO, and emission maps to an `NTCSMaterial3D` in the editor. The plugin trains one neural texture set on CUDA and writes a single `.ntc` file next to the material. Your game ships that `.ntc` instead of the imported BCn textures. When the material loads at runtime, the extension decodes the set on its own Vulkan device, re-encodes the result to BC7/BC4 on the GPU, and writes the textures back into the normal `StandardMaterial3D` slots.

**What you gain:** a much smaller download and install. Sixteen 4K PBR sets went from an 835 MB pack to a 153 MB pack (see [Measured results](#measured-results)).

**What you do not gain:** VRAM. After decode, textures are ordinary BCn images and cost exactly what BCn costs. This is a distribution-size feature, not a memory feature.

This project is **not** an official NVIDIA product and is not endorsed by NVIDIA.

![PBR sets used by the compare demo](https://raw.githubusercontent.com/cn2005/godot-ntc/master/media/preview-materials.png)

## Contents

- [How it works](#how-it-works)
- [Scope and limits](#scope-and-limits)
- [Measured results](#measured-results)
- [Requirements](#requirements)
- [Install](#install)
- [Editor workflow](#editor-workflow)
- [Exporting a game](#exporting-a-game)
- [Compression details](#compression-details)
- [API reference](#api-reference)
- [Build from source](#build-from-source)
- [Tests](#tests)
- [Troubleshooting](#troubleshooting)
- [Roadmap](#roadmap)
- [License](#license)
- [中文说明](#中文说明)

## How it works

```
EDITOR (needs CUDA)                          RUNTIME / EXPORTED GAME (needs Vulkan)
──────────────────────                       ──────────────────────────────────────
albedo.jpg  normal.jpg                       Material.tres loads
roughness.jpg  metallic.jpg                        │
      │  slots on NTCSMaterial3D                   │ NTCTextureSet.file_path
      ▼                                            ▼
 pack channels → train on CUDA               read .ntc from the PCK
 (LibNTC, default 5 bpp)                           │
      │                                            ▼
      ▼                                     LibNTC decompression compute pass
 Material.ntc  (one file per material)        on a private VkDevice
      │                                            │
      └── referenced by NTCTextureSet              ▼
          stored inside the .tres               BC7 / BC4 encode pass on GPU
                                                   │
                                                   ▼
                                             readback → ImageTexture →
                                             StandardMaterial3D slots
```

Two separate halves, and they have different dependencies:

- **Compression happens once, in the editor, on CUDA.** Your players never run it, and it never runs at load time. The result is a plain `.ntc` file on disk.
- **Decoding happens at load, on Vulkan.** The extension creates its own Vulkan instance and device (it does not touch Godot's rendering device), runs LibNTC's decompression pass, block-compresses the output to BC7/BC4 in the same pass chain, reads it back to the CPU, and hands Godot `ImageTexture`s.

The Godot renderer is untouched. No shaders are modified, no engine build is required, and a material that has finished decoding behaves exactly like a `StandardMaterial3D` because it *is* one.

## Scope and limits

Read this section before adopting the plugin.

| | Status |
| --- | --- |
| Editor CUDA compression to `.ntc` | Yes |
| Reopening a scene reuses the existing `.ntc` instead of retraining | Yes |
| Runtime and exported-build decode into `StandardMaterial3D` slots | Yes |
| Export feature `ntc_strip_sources` drops source maps from the PCK | Yes |
| Smaller VRAM footprint | **No** — BCn after decode |
| Inference on sample (sampling latents in the shader) | **No** |
| Same-device decode / `Texture2DRD` (no CPU readback) | **No** |
| macOS, Linux, Android, web, consoles | **No** |
| Non-NVIDIA GPUs | **No** |
| `ORMMaterial3D` equivalent | **No** |
| Texture streaming, mip streaming, async load | **No** |

Costs you are accepting:

- **Load time.** Decode is a GPU dispatch plus a GPU→CPU readback plus a texture upload, and it runs on the thread that loads the material. A 4K set is not free. Load materials off the main thread if you have many of them.
- **A second Vulkan device.** `NtcRuntime.initialize()` creates its own instance, device, and allocations, separate from Godot's renderer.
- **Lossy compression.** NTC is lossy, and the default is 5 bits per pixel across the whole set. Check your own assets at your own bit rate before shipping; do not assume 5 bpp is right for text, decals, or masks.
- **One `.ntc` per material**, trained over the full mip chain of every assigned slot together.

Inference on sample is not merely unimplemented, it is not reachable from a GDExtension: Godot's shading language has no SSBOs, no `dot4add`, and no custom descriptor sets, so an MLP cannot be evaluated inside a Godot shader. That would require an engine module. See [docs/NTC-Godot-Architecture.md](docs/NTC-Godot-Architecture.md) for the layering that keeps this option open without bloating the current material.

## Measured results

Windows 11, Godot 4.7.2, Vulkan Forward+, LibNTC 0.10.0, RTX 3060. Sixteen 4K PBR sets from ambientCG (71 maps: albedo, normal, roughness, metallic on all of them, AO on seven), trained at 5 bpp and 2,000 steps. Both builds are official Godot Windows release exports of the same scene, not editor runs.

| Pack | Size | Ratio |
| --- | ---: | ---: |
| Source JPEG/PNG on disk | 707.90 MB | — |
| Standard Godot pack (VRAM/BCn import) | 835.39 MB | 5.44× |
| **NTC On Load pack** | **153.53 MB** | **1×** |

Texture payload only, inside the PCK: 826.67 MB of `.s3tc.ctex` versus 144.83 MB of `.ntc`, a 5.71× difference. Both packs still carry the 8.62 MB of GDExtension DLLs.

`.ntc` size is driven by resolution × channel count × bits per pixel, not by source entropy, so noisy photographic sets shrink far more than flat metal panels. Per-material numbers ranged from 2.27× (Onyx015) to 8.40× (Asphalt033) against the source JPEGs.

On quality: the release suite includes a coarse pixel-difference guard on a decoded albedo map to catch a broken decode path. It is a regression tripwire, not a quality benchmark. If image quality matters to your project, compare your own `.ntc` output against reference images with NVIDIA's `ntc-cli` and pick the bit rate from that.

## Requirements

**To use a released build:**

- Godot **4.7.2** (the `.gdextension` declares `compatibility_minimum = 4.7`), Vulkan **Forward+**. Mobile and Compatibility renderers are untested.
- Windows 10/11 **x86_64**.
- An NVIDIA GPU with a Vulkan 1.2+ driver. Decode uses DP4a (`GenericInt8`) weights.
- CUDA, **only** to compress in the editor. Playing a game that ships `.ntc` files does not need CUDA.

**To build from source, additionally:**

- Visual Studio 2022, CMake 3.17+, Git.
- The [NVIDIA RTX NTC SDK](https://github.com/NVIDIA-RTX/RTXNTC) **v0.10.0** (LibNTC). You must accept NVIDIA's license before using or redistributing `libntc.dll`.

## Install

Download `NTC-OnLoad-<version>-windows.zip` from [Releases](https://github.com/cn2005/godot-ntc/releases) and unzip it so these two folders sit next to `project.godot`:

```text
your-project/
├── project.godot
├── addons/
│   └── ntc/                     # editor plugin: inspector hook + export hook
│       ├── plugin.cfg
│       ├── ntc_plugin.gd
│       └── ntc_export_plugin.gd
└── bin/
    ├── ntc.gdextension          # entry point Godot reads
    └── windows/
        ├── libntc_godot.dll     # this project
        └── libntc.dll           # NVIDIA LibNTC
```

Then open **Project → Project Settings → Plugins** and enable **NTC On Load**.

Do not install from the GitHub "Source code" archive: it has no DLLs, because `libntc.dll` comes from NVIDIA's SDK and the extension DLL has to be built.

`bin/` is what a shipped game needs. `addons/ntc/` is editor-only — it drives the inspector and the export hook, and is never loaded by an exported build.

## Editor workflow

1. Create an `NTCSMaterial3D` resource and **save it as a `.tres` file**. Materials embedded in a scene work, but their `.ntc` lands in `res://.ntc_cache/<instance_id>.ntc`, which is not a stable path; a saved `.tres` gets a sidecar named after itself.
2. Assign your source textures to the usual slots: albedo, normal, roughness, metallic, ambient occlusion, emission. Assign at least one.
3. Set **NTC → Ntc Mode** to **On Load**.
4. Compression starts after a short debounce and runs on a worker thread. Watch **Ntc Status** in the inspector (or the Output panel, every status line is printed with an `NTC:` prefix): `Creating CUDA context…` → `Training (n/N, …)` → `Wrote res://…/Material.ntc (sources kept)`.
5. Save the material. Reopening the scene must print `Using existing .ntc` — if you see `BeginCompression` again, the reuse check failed (see [Troubleshooting](#troubleshooting)).

Source textures stay assigned in the editor, so you can keep authoring, change bit rate, or switch back to `NTC_MODE_NONE` at any time. They are only dropped from the exported PCK.

Tuning knobs, both of which retrigger training when changed:

| Property | Default | Range | Notes |
| --- | ---: | --- | --- |
| `ntc_bits_per_pixel` | 5.0 | 1–20 | LibNTC snaps this to the nearest supported latent shape. |
| `ntc_training_steps` | 20000 | 1000–100000 | Linear in time. The published comparison used 2,000. |

From script:

```gdscript
var mat := NTCSMaterial3D.new()
mat.albedo_texture = preload("res://mat/albedo.png")
mat.normal_texture = preload("res://mat/normal.png")
mat.roughness_texture = preload("res://mat/roughness.png")
mat.metallic_texture = preload("res://mat/metallic.png")
mat.ntc_bits_per_pixel = 5.0
mat.ntc_training_steps = 20000
mat.ntc_mode = NTCSMaterial3D.NTC_MODE_ON_LOAD
# Editor: trains on CUDA, writes the sidecar .ntc, keeps the source textures.
# Runtime: decodes the .ntc into the StandardMaterial3D slots.

mat.ntc_file_written.connect(func(path: String) -> void:
	print("wrote ", path))
```

Decoding an arbitrary `.ntc` without a material:

```gdscript
var ts := NTCTextureSet.new()
if ts.load_from_file("res://materials/Wood095.ntc") == OK:
	print(ts.get_width(), "x", ts.get_height(), " mips=", ts.get_mip_count())
	print(ts.get_texture_names())
	ts.apply_to_standard_material($MeshInstance3D.get_active_material(0))
else:
	push_error(ts.get_last_error())
```

## Exporting a game

Two things in the export preset decide whether you actually save anything.

**1. Include the `.ntc` files.** They are plain files, not imported Godot resources, so the exporter ignores them unless you ask for them:

```
include_filter = *.ntc
```

**2. Add the custom feature `ntc_strip_sources`** (Export dialog → **Features** → Custom). Without it, the sources ship alongside the `.ntc` and the pack gets *bigger*. With it, the export plugin:

- replaces every `ON_LOAD` material with a copy that has empty texture slots,
- slims embedded `NTCTextureSet` resources down to a path plus metadata,
- and skips source files during packing.

> **Caveat worth knowing before you flip it on:** the strip filter is coarse. It skips *every* `.jpg`, `.jpeg`, `.png`, and `.ctex` in the project, plus everything under `.godot/imported/`. In a project where NTC materials are the only textures — the demo — that is what you want. In a real game it will also throw out your UI sprites and any texture that is not part of an NTC set. Until the filter is narrowed, either keep NTC materials in a dedicated preset, or add the assets you must keep back through `include_filter`.

The demo's preset in `demo/export_presets.cfg` is a working reference for both settings.

Exported builds need `bin/ntc.gdextension` and both DLLs in the pack (`include_filter = *.gdextension,*.dll` covers them), and do not need `addons/ntc`.

## Compression details

**Channel packing.** Every assigned slot becomes one texture inside a single NTC texture set, packed into a shared channel budget of 16:

| Slot | Semantic | Channels | Color space | Decoded format |
| --- | --- | ---: | --- | --- |
| Albedo | `albedo` | 3, or 4 when the image has real alpha | sRGB | BC7 (`FORMAT_BPTC_RGBA`) |
| Normal | `normal` | 3 | linear | BC7 |
| Roughness | `roughness` | 1 | linear | BC4 (`FORMAT_RGTC_R`) |
| Metallic | `metallic` | 1 | linear | BC4 |
| Ambient occlusion | `ao` | 1 | linear | BC4 |
| Emission | `emission` | 3 | sRGB | BC7 |

All six at once is 12 channels (13 with albedo alpha), so the budget is never the binding constraint for a standard PBR set.

**Geometry.** The first assigned slot sets the resolution for the whole set. Every other map is converted to RGBA8 and resized to match, and both dimensions are rounded up to a multiple of 4. A full mip chain is generated (capped at 16 levels), with normal maps renormalized during downsampling. Minimum size after alignment is 4×4.

**Sources must be readable on the CPU.** `Texture2D.get_image()` has to return pixels, so disk-backed textures work and viewport textures do not.

**Output path.** A material saved at `res://mat/Steel.tres` writes `res://mat/Steel.ntc`. An embedded material writes to `res://.ntc_cache/<instance_id>.ntc`.

**Reuse.** The material stores a signature of `bits_per_pixel | training_steps | every slot's resource path and size`. On load, and on every `rebuild_ntc()`, the material recomputes it: if the signature matches and the `.ntc` exists, nothing is retrained. Changing a bit rate, a step count, or swapping a texture changes the signature and queues a new training run (0.8 s debounce, so dragging several textures in triggers one run, not four). A rebuild requested while one is running cancels the current run and restarts.

**Threading.** Training runs on a worker thread in 100-step iterations; the main thread polls it every 0.25 s for progress. Short iterations exist so Windows' GPU watchdog (TDR) does not kill the editor during a long 4K run.

## API reference

### `NtcRuntime` (Object, registered as an engine singleton)

Owns the private Vulkan device and the LibNTC context. Created automatically; `initialize()` is called on the first decode, so most projects never touch it.

| Member | Returns | Notes |
| --- | --- | --- |
| `initialize()` | `bool` | Loads `libntc.dll`, creates the Vulkan device and NTC context. Idempotent. |
| `is_available()` | `bool` | True after a successful `initialize()`. |
| `shutdown()` | `void` | Destroys the context and device. |
| `get_last_error()` | `String` | Reason the last call failed. |
| `get_library_version()` | `Dictionary` | `major`, `minor`, `point`, `branch`, `commit`. |
| `get_gpu_name()` | `String` | Device chosen for decode (prefers a discrete NVIDIA GPU). |
| `get_coop_vec_enabled()` | `bool` | Always false in this release; decode uses DP4a int8 weights. |
| `get_capability_tier()` | `int` | 1 when decode is available, 0 otherwise. |
| `ensure_library_loaded()` | `bool` | Loads `libntc.dll` without creating a device. |

### `NTCTextureSet` (Resource)

One `.ntc` file, plus the decoded textures.

| Member | Returns | Notes |
| --- | --- | --- |
| `file_path` | `String` | `res://` path to the `.ntc`. Setting it clears the decoded cache. |
| `embedded_bytes` | `PackedByteArray` | Optional inline payload instead of a file. |
| `semantic_map` | `Dictionary` | Semantic name → texture name inside the set. |
| `bits_per_pixel` | `float` | Filled in from the file after decode. |
| `load_from_file(path)` / `load_from_bytes(bytes)` | `Error` | Set the payload and decode immediately. |
| `ensure_decoded()` | `Error` | Decode once; later calls are no-ops. |
| `embed_from_file()` | `Error` | Copy the file's bytes into `embedded_bytes`. |
| `has_payload()` | `bool` | True if bytes or an existing file are present. |
| `get_width()` / `get_height()` / `get_mip_count()` / `get_channel_count()` | `int` | Valid after decode. |
| `get_weight_type()` | `String` | Inference weight type actually used, e.g. `GenericInt8`. |
| `get_texture_names()` / `get_texture(name)` / `get_all_textures()` | | Decoded `Texture2D`s. |
| `apply_to_standard_material(material)` | `Error` | Fills slots by semantic and enables normal mapping, AO, and emission as needed. |
| `get_last_error()` | `String` | |

### `NTCSMaterial3D` (extends `StandardMaterial3D`)

| Member | Type | Notes |
| --- | --- | --- |
| `ntc_mode` | `NTC_MODE_NONE` \| `NTC_MODE_ON_LOAD` | `NONE` behaves exactly like `StandardMaterial3D`. |
| `ntc_bits_per_pixel` | `float`, default 5.0 | Editor only. |
| `ntc_training_steps` | `int`, default 20000 | Editor only. |
| `ntc_texture_set` | `NTCTextureSet` | Usually written by the plugin, hidden in the inspector. |
| `ntc_status` | `String`, read-only | Live progress and error text. |
| `ntc_source_signature` | `String` | Reuse key; hidden in the inspector, saved in the `.tres`. |
| `rebuild_ntc()` | | Retrains unless the signature still matches. |
| `apply_decoded()` → `bool` | | Decode and fill slots now. |
| `notify_sources_changed()` | | Start the debounce after an external edit. |
| `get_pack_excluded_paths()` → `PackedStringArray` | | Source paths the exporter may drop. |
| `create_export_copy()` → `NTCSMaterial3D` | | Slot-stripped copy used during export. |
| `ntc_file_written(path)` | signal | Emitted after a successful training run. |

There are deliberately only two modes. An on-sample material would own its own shader and would be a separate class, not a third value here; see the architecture note.

## Build from source

The build needs a full RTX NTC SDK checkout containing `libraries/RTXNTC-Library` and `bin/windows-x64/libntc.dll`. Either place this repository next to that checkout, or pass `-DNTC_ROOT=`.

```bat
git clone https://github.com/cn2005/godot-ntc.git
cd godot-ntc
git clone --branch 10.0.0-stable --depth 1 https://github.com/godotengine/godot-cpp.git thirdparty/godot-cpp

:: godot-cpp binds against the API of the exact editor you will run
godot --path demo --dump-extension-api --headless
move demo\extension_api.json extension_api.json

mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 -DNTC_ROOT=C:\path\to\RTXNTC ..
cmake --build . --config RelWithDebInfo
```

CMake fetches Vulkan-Headers itself and writes both DLLs into `demo/bin/windows/`:

- `libntc_godot.dll` — this extension
- `libntc.dll` — copied from the SDK by a post-build step

`libntc.dll` is delay-loaded, so the extension can report a clean error instead of failing to load when it is missing. On Windows the running editor holds those DLLs open; restart Godot after every native rebuild.

Package a release ZIP with `powershell -File tools/pack_assetlib_zip.ps1 -Version 0.1.1`. It stages only `addons/ntc` and `bin`, and writes to `.cache/assetlib/`.

## Tests

On a machine with Godot 4.7.2 and a built extension:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_release_tests.ps1
```

The suite covers extension load, runtime init and library version, decoding both a stored `.ntc` and a compare-set `.ntc`, applying a set to a `StandardMaterial3D`, reuse without recompression, failure paths for a missing file and for garbage bytes, an albedo difference guard, PCK contents after export, pack-size ratio, an exported-executable smoke run, and a clean project that contains nothing but the extension and one `.ntc`. Results are written to `.cache/release_tests/RESULTS.md`.

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `Can't open dynamic library` on editor start | `libntc.dll` is missing next to `libntc_godot.dll`. Both DLLs must sit in `bin/windows/`. |
| `CUDA CreateContext failed` when compressing | No CUDA-capable NVIDIA GPU, or no driver. Decoding still works; only compression needs CUDA. |
| Status stays `ON_LOAD needs source textures or an existing .ntc` | No slot has a texture and no `.ntc` is attached. |
| `Cannot read pixels from albedo (need a disk texture, not a viewport)` | The slot holds a texture whose `get_image()` returns nothing, such as a `ViewportTexture`. |
| `Channel count must be 1..16` | Too many slots for the budget. Drop a map. |
| Training restarts every time the scene opens | The reuse signature changed: a texture was reimported or moved, or bit rate or step count differs from what the `.tres` stored. Save the material after the run finishes. |
| The editor freezes or the driver resets during a long run | Windows TDR. Lower `ntc_training_steps` or the resolution; iterations are already chunked at 100 steps to reduce this. |
| Rebuilding the DLL fails with a file lock | Godot has the extension loaded. Close the editor and rebuild. |
| The exported pack is *larger* than the standard one | The `ntc_strip_sources` feature is missing from the preset, so both the sources and the `.ntc` shipped. |
| The exported game shows an untextured material | `.ntc` files were not packed. Add `*.ntc` to `include_filter`. |
| Textures are missing from the export that have nothing to do with NTC | `ntc_strip_sources` drops every `.jpg` / `.png` / `.ctex`. See the caveat under [Exporting a game](#exporting-a-game). |

## Roadmap

The layering that this release implements, and what it intentionally leaves open:

- **L0 `NTCTextureSet` + decode backend** — done, using a private Vulkan device and CPU readback.
- **L1 `NTCSMaterial3D`** — done. Engine shaders only, slots filled with ordinary textures, zero engine changes.
- **L2 same-device decode** — planned. Open the device features NTC needs on Godot's own Vulkan device and deliver `Texture2DRD` instead of reading back through the CPU. Purely an internal change; `NTCSMaterial3D` would not change.
- **L3/L4 inference on sample** — requires an engine module (`modules/ntc`) with its own shader factory and a separate material class. Not part of this plugin, and not a third mode on this one.

Details and the reasoning are in [docs/NTC-Godot-Architecture.md](docs/NTC-Godot-Architecture.md) and [docs/NTC-Godot-Integration-Plan.md](docs/NTC-Godot-Integration-Plan.md).

## License

- This repository's own C++, GDScript, and documentation: [MIT](LICENSE), Copyright (c) 2026 cn2005.
- **LibNTC and the `.ntc` format: [NVIDIA RTX SDKs license](LICENSES/NVIDIA-RTX-SDKs.txt)**, not MIT. You may redistribute `libntc.dll` only as part of an application, never as a stand-alone SDK. Obtain the SDK yourself and accept NVIDIA's terms.
- [godot-cpp](https://github.com/godotengine/godot-cpp): MIT. Not vendored here.
- Demo textures: [ambientCG](https://ambientcg.com), CC0 1.0.

Full third-party notices are in [NOTICE.md](NOTICE.md).

## 中文说明

这是一个 Godot 4.7 的 **GDExtension**，把 [NVIDIA RTX 神经纹理压缩](https://github.com/NVIDIA-RTX/RTXNTC)（LibNTC 0.10.0）接到 **加载时推理** 上。

在编辑器里给 `NTCSMaterial3D` 挂上 albedo / normal / roughness / metallic / AO / emission，插件用 CUDA 训练一次，在材质旁边写出一个 `.ntc`；运行时扩展在自己的 Vulkan 设备上解码，GPU 上重新编码成 BC7/BC4，再回填到普通的 `StandardMaterial3D` 槽位。引擎一行都不用改。

**省的是包体，不是显存。** 解码完就是普通 BCn 贴图，显存占用和 BCn 一模一样。16 套 4K PBR 实测：官方模板导出的 Standard 包 835.39 MB，NTC 包 153.53 MB。

**当前范围**：只有 Windows x86_64、Vulkan Forward+、NVIDIA 显卡；编辑器压缩另需 CUDA。没有 on-sample（Godot 着色语言没有 SSBO 和 `dot4add`，纯扩展做不到，只能做引擎模块）、没有同设备零拷贝解码、没有 ORM 材质、没有流式加载。解码要走一次 GPU→CPU→GPU 回读，材质多的时候请放到线程里加载。

**安装**：从 [Releases](https://github.com/cn2005/godot-ntc/releases) 下载 ZIP，解压后让 `addons/ntc/` 和 `bin/` 与 `project.godot` 同级，再在插件面板启用 **NTC On Load**。GitHub 自动生成的源码包里没有 DLL，不能直接用。

**导出必看两件事**：导出预设里 `include_filter` 要加 `*.ntc`（`.ntc` 不是 Godot 导入资源，不写就不会打进包）；自定义特性要加 `ntc_strip_sources`，否则源图和 `.ntc` 一起进包，包反而更大。注意这个剥离规则目前很粗暴——工程里所有 `.jpg`/`.png`/`.ctex` 都会被跳过，UI 贴图也会被丢掉，真实项目请单独建预设或用 `include_filter` 把要留的资源加回来。

其余细节（通道打包表、mip 与对齐规则、复用签名、完整 API、排错表）见上面的英文章节。仓库代码 MIT；`libntc.dll` 受 NVIDIA RTX SDK 许可约束，只能作为应用的一部分分发，不能当独立 SDK 再发布。
