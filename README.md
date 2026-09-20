# Godot NTC — Inference on Load (形态 A1)

独立 Vulkan 设备上跑 LibNTC 的加载时推理：解码 `.ntc` → GPU BCn 转码 → 回读成 Godot `Image` / `ImageTexture`，再挂到 `StandardMaterial3D`。

不需要改 Godot 引擎。目标编辑器：工作区里的 `Godot_v4.7.2-stable_win64.exe`。

## 构建

需要 Visual Studio 2022/2026、CMake、能访问 GitHub（拉取 Vulkan-Headers）。

```bat
cd godot-ntc
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config RelWithDebInfo
```

产物：

- `demo/bin/windows/libntc_godot.dll`
- `demo/bin/windows/libntc.dll`（从 SDK `bin/windows-x64` 复制）

## 运行

用 Godot 4.7.2 打开 `godot-ntc/demo`，运行主场景。应看到 MetalPlates013 贴在球上。

GDScript：

```gdscript
var mat := NTCSMaterial3D.new()
mat.albedo_texture = preload("res://albedo.png")
mat.ntc_mode = NTCSMaterial3D.NTC_MODE_ON_LOAD
# Editor: async CUDA compress → same-name .ntc, source textures stay.
# Runtime / export: decode .ntc into the Standard slots. Export strips sources.
```
