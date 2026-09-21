# NTC On Load

Godot 4.7 GDExtension that wraps NVIDIA RTX Neural Texture Compression for
inference on load. Full documentation, measured pack sizes, API reference, and
troubleshooting live at <https://github.com/cn2005/godot-ntc>.

## Install

Unzip the release so both folders sit next to `project.godot`:

```text
your-project/
├── project.godot
├── addons/ntc/          # this folder (editor only)
└── bin/
    ├── ntc.gdextension
    └── windows/
        ├── libntc_godot.dll
        └── libntc.dll
```

Enable **NTC On Load** in Project Settings → Plugins.

Requires Windows x86_64, Godot 4.7.2, Vulkan Forward+, and an NVIDIA GPU. CUDA
is required only to compress in the editor, never to play a shipped game.

## Use

1. Create an `NTCSMaterial3D` and save it as a `.tres`.
2. Assign albedo, normal, roughness, metallic, AO, or emission textures.
3. Set **Ntc Mode** to **On Load**. Training runs on CUDA and writes a `.ntc`
   next to the material; watch **Ntc Status** in the inspector.
4. Reopening the scene should report `Using existing .ntc`, not a new run.

## Export

In the export preset:

- add `*.ntc` to **include_filter**, otherwise the files are not packed;
- add the custom feature `ntc_strip_sources` to drop source maps from the PCK.

`ntc_strip_sources` currently skips every `.jpg`, `.jpeg`, `.png`, and `.ctex`
in the project. Use a dedicated preset, or add anything you need to keep back
through `include_filter`.

An exported game needs `bin/` only; this addon is editor-only.

## License

Plugin code: MIT, see `LICENSE`. LibNTC (`libntc.dll`) and the `.ntc` format
are covered by the NVIDIA RTX SDKs license in `NVIDIA-RTX-SDKs.txt` and may be
redistributed only as part of an application. See `NOTICE.md`.

This project is not an official NVIDIA product.
