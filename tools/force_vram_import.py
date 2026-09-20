from pathlib import Path

root = Path(__file__).resolve().parents[1] / "demo" / "assets" / "pbr4k"
count = 0
for path in root.rglob("*.import"):
    is_normal = path.name.startswith("normal.")
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("compress/mode="):
            line = "compress/mode=2"
        elif line.startswith("mipmaps/generate="):
            line = "mipmaps/generate=true"
        elif line.startswith("compress/normal_map="):
            line = "compress/normal_map=1" if is_normal else "compress/normal_map=0"
        elif line.startswith("detect_3d/compress_to="):
            line = "detect_3d/compress_to=0"
        elif '"vram_texture": false' in line:
            line = line.replace("false", "true")
        out.append(line)
    path.write_text("\n".join(out) + "\n", encoding="utf-8")
    count += 1
print(f"patched {count} import files")
