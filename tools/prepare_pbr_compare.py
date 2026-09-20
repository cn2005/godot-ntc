#!/usr/bin/env python3
"""Download 16 ambientCG 4K PBR sets. Compression happens inside Godot NTCSMaterial3D."""

from __future__ import annotations

import json
import struct
import sys
import zipfile
import zlib
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[2]
DEMO = ROOT / "godot-ntc" / "demo"
OUT = DEMO / "assets" / "pbr4k"
CACHE = ROOT / "godot-ntc" / ".cache" / "ambientcg"
UA = "NTC-Godot-Compare/1.0"

MATERIALS = [
    "Metal063",
    "CorrugatedSteel009",
    "Metal055A",
    "Metal046B",
    "Wood095",
    "WoodFloor051",
    "Rock064",
    "Bricks104",
    "Concrete034",
    "Tiles141",
    "Marble012",
    "Asphalt033",
    "PavingStones151",
    "Leather037",
    "Fabric061",
    "Onyx015",
]

# semantic, filename tokens, required in zip (metallic can be synthesized)
MAPS = [
    ("albedo", ["Color", "Diffuse", "BaseColor"], True),
    ("normal", ["NormalGL", "NormalDX", "Normal"], True),
    ("roughness", ["Roughness"], True),
    ("metallic", ["Metalness", "Metallic"], False),
    ("ao", ["AmbientOcclusion", "AO", "Occlusion"], False),
]


def log(msg: str) -> None:
    print(msg, flush=True)


def download(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and dest.stat().st_size > 1024:
        log(f"  cached {dest.name} ({dest.stat().st_size / 1e6:.1f} MB)")
        return
    tmp = dest.with_suffix(dest.suffix + ".part")
    log(f"  GET {url}")
    req = Request(url, headers={"User-Agent": UA})
    with urlopen(req, timeout=180) as resp, tmp.open("wb") as f:
        while True:
            chunk = resp.read(1024 * 1024)
            if not chunk:
                break
            f.write(chunk)
    tmp.replace(dest)
    log(f"  saved {dest.name} ({dest.stat().st_size / 1e6:.1f} MB)")


def token_key(name: str) -> str:
    stem = Path(name).stem
    return stem.split("_")[-1] if "_" in stem else stem


def pick_map(names: list[str], tokens: list[str]) -> str | None:
    keys = {n: token_key(n).lower() for n in names}
    for token in tokens:
        t = token.lower()
        for n, key in keys.items():
            if key == t:
                return n
    return None


def write_gray_png(path: Path, width: int, height: int, value: int = 0) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes([value]) * width for _ in range(height))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    )


def extract_material(asset_id: str, zip_path: Path, dest: Path) -> dict:
    dest.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as zf:
        names = [i.filename for i in zf.infolist() if not i.is_dir()]
        picked = {}
        for semantic, tokens, required in MAPS:
            match = pick_map(names, tokens)
            if match is None:
                if required:
                    raise FileNotFoundError(f"{asset_id}: missing {semantic} in {names}")
                continue
            suffix = Path(match).suffix.lower()
            target_name = f"{semantic}{suffix}"
            target = dest / target_name
            if not target.exists() or target.stat().st_size < 16:
                with zf.open(match) as src, target.open("wb") as out:
                    out.write(src.read())
            picked[semantic] = target_name
    if "metallic" not in picked:
        write_gray_png(dest / "metallic.png", 4096, 4096, 0)
        picked["metallic"] = "metallic.png"
        log(f"  synthesized 4K black metallic for {asset_id}")
    return picked


def fetch_one(asset_id: str) -> tuple[str, Path, dict]:
    url = f"https://ambientcg.com/get?file={asset_id}_4K-JPG.zip"
    zip_path = CACHE / f"{asset_id}_4K-JPG.zip"
    download(url, zip_path)
    dest = OUT / asset_id
    picked = extract_material(asset_id, zip_path, dest)
    log(f"ready {asset_id}: {', '.join(picked)}")
    return asset_id, dest, picked


def main() -> int:
    CACHE.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    log(f"Downloading {len(MATERIALS)} 4K materials into {OUT}")
    errors = []
    ready = []
    with ThreadPoolExecutor(max_workers=4) as pool:
        futs = {pool.submit(fetch_one, mid): mid for mid in MATERIALS}
        for fut in as_completed(futs):
            mid = futs[fut]
            try:
                ready.append(fut.result())
            except Exception as exc:
                errors.append(f"{mid}: {exc}")
                log(f"FAIL download {mid}: {exc}")
    if len(ready) < 16:
        log(f"Need 16 materials, got {len(ready)}. {errors}")
        return 1
    catalog = []
    for asset_id, dest, picked in sorted(ready, key=lambda x: MATERIALS.index(x[0])):
        src = sum(p.stat().st_size for p in dest.iterdir() if p.suffix.lower() in {".jpg", ".jpeg", ".png"})
        catalog.append({"id": asset_id, "maps": picked, "source_bytes": src})
    (OUT / "catalog.json").write_text(json.dumps({"materials": catalog}, indent=2), encoding="utf-8")
    log(f"Downloaded {len(ready)} materials. Compress with Godot NTCSMaterial3D, not ntc-cli.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
