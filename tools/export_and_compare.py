#!/usr/bin/env python3
"""Godot-import textures, run NTCSMaterial3D compress scene, export both packs."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEMO = ROOT / "godot-ntc" / "demo"
GODOT = ROOT / "Godot_v4.7.2-stable_win64.exe"
DIST = ROOT / "dist" / "compare"
NTCS_DIR = DIST / "ntcs"
STD_DIR = DIST / "standard"
PROJECT = DEMO / "project.godot"


def log(msg: str) -> None:
    print(msg, flush=True)


def run(cmd: list[str]) -> None:
    log(" ".join(cmd))
    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}")


def set_main_scene(scene: str) -> str:
    text = PROJECT.read_text(encoding="utf-8")
    old = ""
    lines = []
    for line in text.splitlines(True):
        if line.startswith("run/main_scene="):
            old = line
            lines.append(f'run/main_scene="{scene}"\n')
        else:
            lines.append(line)
    PROJECT.write_text("".join(lines), encoding="utf-8")
    return old


def restore_main(old: str) -> None:
    if not old:
        return
    text = PROJECT.read_text(encoding="utf-8")
    lines = []
    for line in text.splitlines(True):
        if line.startswith("run/main_scene="):
            lines.append(old if old.endswith("\n") else old + "\n")
        else:
            lines.append(line)
    PROJECT.write_text("".join(lines), encoding="utf-8")


def folder_size(path: Path) -> int:
    if not path.exists():
        return 0
    return sum(p.stat().st_size for p in path.rglob("*") if p.is_file())


def export_pack(preset: str, dest_dir: Path, exe_name: str, main_scene: str) -> Path:
    dest_dir.mkdir(parents=True, exist_ok=True)
    pck = dest_dir / f"{Path(exe_name).stem}.pck"
    old = set_main_scene(main_scene)
    try:
        run([str(GODOT), "--headless", "--path", str(DEMO), "--export-pack", preset, str(pck)])
    finally:
        restore_main(old)
    if not pck.exists():
        raise RuntimeError(f"missing {pck}")
    shutil.copy2(GODOT, dest_dir / exe_name)
    (dest_dir / "run.bat").write_text(
        f'@echo off\r\ncd /d "%~dp0"\r\nstart "" "{exe_name}"\r\n',
        encoding="utf-8",
    )
    return pck


def mb(n: int) -> str:
    return f"{n / 1e6:.2f} MB"


def main() -> int:
    if not GODOT.exists():
        raise FileNotFoundError(GODOT)
    log("Godot import 4K textures...")
    run([str(GODOT), "--headless", "--path", str(DEMO), "--import", "--quit"])
    log("Godot NTCSMaterial3D compress + scene build...")
    run([str(GODOT), "--headless", "--path", str(DEMO), "res://tools/godot_build_compare.tscn"])
    log("Godot export NTCS pack...")
    ntc_pck = export_pack("NTCS", NTCS_DIR, "NTCCompare.exe", "res://scenes/compare_ntcs.tscn")
    log("Godot export Standard pack...")
    std_pck = export_pack("Standard", STD_DIR, "StandardCompare.exe", "res://scenes/compare_standard.tscn")

    pbr = DEMO / "assets" / "pbr4k"
    src = sum(p.stat().st_size for p in pbr.rglob("*") if p.suffix.lower() in {".jpg", ".jpeg", ".png"})
    ntc_files = sum(p.stat().st_size for p in (DEMO / "materials" / "ntcs").rglob("*.ntc")) if (DEMO / "materials" / "ntcs").exists() else 0
    imported = DEMO / ".godot" / "imported"
    ctex = sum(p.stat().st_size for p in imported.rglob("*.ctex")) if imported.exists() else 0
    report = {
        "source_maps_bytes": src,
        "godot_ntc_files_bytes": ntc_files,
        "imported_ctex_bytes": ctex,
        "ntcs_pck_bytes": ntc_pck.stat().st_size,
        "standard_pck_bytes": std_pck.stat().st_size,
        "ratio_standard_over_ntcs_pck": std_pck.stat().st_size / ntc_pck.stat().st_size if ntc_pck.stat().st_size else 0,
        "ntcs_dir": str(NTCS_DIR),
        "standard_dir": str(STD_DIR),
    }
    DIST.mkdir(parents=True, exist_ok=True)
    (DIST / "size_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    log(json.dumps(report, indent=2))
    log("")
    log("Godot packed asset sizes (PCK = shipped data; exe is the same Godot runner):")
    log(f"  NTCS     PCK  {mb(report['ntcs_pck_bytes'])}")
    log(f"  Standard PCK  {mb(report['standard_pck_bytes'])}")
    log(f"  Standard / NTCS = {report['ratio_standard_over_ntcs_pck']:.2f}x")
    return 0


if __name__ == "__main__":
    sys.exit(main())
