# Godot Forum announcement (ready to post)

Title: `[Plugin] NTC On Load — Neural Texture Compression for Godot 4.7 (Windows)`

Category: Projects / Plugins (or Showcase)

Body:

---

**NTC On Load** packages [NVIDIA RTX Neural Texture Compression](https://github.com/NVIDIA-RTX/RTXNTC) as a Godot 4.7 GDExtension. It implements **inference on load only**: compress a PBR set to `.ntc` in the editor, decode it when the game loads, and bind the result to `StandardMaterial3D` slots.

It does **not** implement inference on sample, does **not** patch the engine, and is **not** an official NVIDIA product.

**Repo:** https://github.com/cn2005/godot-ntc  
**Release:** https://github.com/cn2005/godot-ntc/releases/tag/v0.1.0

### Why it exists

NTC stores correlated PBR channels (albedo, normal, roughness, metallic, optional AO) at about 5 bits/texel. Godot's usual imported BCn still wins for VRAM. NTC wins for **disk and download**.

On the included 16×4K compare (Godot official Windows template, RTX 3060):

- Standard pack: **835 MB**
- NTCS pack (one `.ntc` per material, sources stripped): **154 MB**

### What you need

- Godot 4.7.2, Vulkan Forward+
- Windows x86_64
- NVIDIA GPU (CUDA only for editor compress)

### What you do

1. Download the Windows ZIP from the release.
2. Copy `addons/ntc` and `bin/` next to `project.godot`.
3. Enable **NTC On Load**.
4. Use `NTCSMaterial3D`, set maps, switch **Ntc Mode** to **On Load**.
5. Add export feature `ntc_strip_sources` so source maps stay out of the PCK.

Opening a scene that already has a matching `.ntc` must **not** start CUDA again.

Issues and source: https://github.com/cn2005/godot-ntc/issues
