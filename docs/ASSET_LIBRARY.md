# Godot Asset Library submission

Use this file to fill [Submit Assets](https://godotengine.org/asset-library/asset/submit).
The library does not accept Markdown in the description field. Paste the
plain-text block as-is.

Reviewers will reject an asset that does not run. Do **not** point Download
at the GitHub archive ZIP: that ZIP has no `libntc.dll`. Use the **Custom**
download provider and the Windows release ZIP from GitHub Releases.

You need a logged-in [Godot Asset Library](https://godotengine.org/asset-library)
account. This repository cannot submit on your behalf without that login.

## Form

| Field | Value |
| --- | --- |
| Asset Name | NTC On Load |
| Category | Addons → 3D Tools |
| Godot version | 4.7 |
| Version | 0.1.1 |
| Repository host | Custom |
| Repository URL | https://github.com/cn2005/godot-ntc |
| Issues URL | https://github.com/cn2005/godot-ntc/issues |
| Download Commit / URL | `https://github.com/cn2005/godot-ntc/releases/download/v0.1.1/NTC-OnLoad-0.1.1-windows.zip` |
| Icon URL | https://raw.githubusercontent.com/cn2005/godot-ntc/master/icon.png |
| License | MIT |
| Preview 1 | Image, https://raw.githubusercontent.com/cn2005/godot-ntc/master/media/preview-materials.png |

## Description (plain text)

```
NTC On Load is a Godot 4.7 GDExtension that wraps NVIDIA RTX Neural Texture Compression (LibNTC 0.10.0) for inference on load. Assign albedo, normal, roughness, and metallic textures to NTCSMaterial3D, compress them once to a .ntc file, and ship that file instead of imported BCn maps. At runtime the extension decodes the set on a dedicated Vulkan device and writes ImageTextures back onto StandardMaterial3D slots.

This reduces download and pack size. Video memory after decode is still BCn-sized. The Godot renderer is not modified. Inference on sample is not included.

Requirements: Windows 10/11 x86_64, Godot 4.7.2, Vulkan Forward+, and an NVIDIA GPU. CUDA is required only to compress in the editor. macOS, Linux, and Android are not supported.

This project is not an official NVIDIA product. Original plugin code is MIT. libntc.dll remains under the NVIDIA RTX SDKs license and must be redistributed only as part of an application.

Install the release ZIP so addons/ntc and bin/windows sit next to project.godot, then enable the NTC On Load plugin. See the GitHub README for build steps and measured pack sizes.
```
