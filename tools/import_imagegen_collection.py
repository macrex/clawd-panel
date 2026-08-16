#!/usr/bin/env python3
"""Recompila toda a colecao criada por ImageGen com estabilizacao visual."""

from __future__ import annotations

from pathlib import Path

from clw_format import parse_clw
from import_imagegen_sheet import compile_sheet
from import_marks_actions import (
    ACTIONS,
    ACTION_DOWNSAMPLE,
    ACTION_FRAME_MS,
    ACTION_LAYOUT,
    ACTION_MAX_COMPRESSION,
)


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "sprites" / "sources" / "imagegen"
OUTPUT_ROOT = ROOT / "sdcard" / "clawd"

BASE_SPRITES = (
    ("clawd_marks", 2.0, "dark-cap"),
    ("arachnid", 2.0, "warm-core"),
    ("ruby_visor", 2.0, "warm-core"),
    ("stormcaller", 2.3, "warm-core"),
    ("solar_mind", 2.3, "warm-core"),
    ("magnetic_regent", 2.3, "warm-core"),
    ("feral_energy", 2.0, "warm-core"),
)


def compile_one(
    source: Path,
    output: Path,
    downsample: float,
    stabilize: str,
    *,
    columns: int = 4,
    rows: int = 2,
    frame_ms: int = 100,
    max_core_compression: float = 0.25,
) -> None:
    compile_sheet(
        source,
        output,
        columns=columns,
        rows=rows,
        downsample=downsample,
        frame_ms=frame_ms,
        stabilize=stabilize,
        max_core_compression=max_core_compression,
    )
    sprite = parse_clw(output.read_bytes())
    print(
        f"{output.name}: {sprite.frames} frames, {sprite.width}x{sprite.height}, "
        f"{output.stat().st_size / 1024:.1f} KB, ancora={stabilize}"
    )


def main() -> int:
    for name, downsample, stabilize in BASE_SPRITES:
        compile_one(
            SOURCE_ROOT / f"{name}.png",
            OUTPUT_ROOT / f"{name}.clw",
            downsample,
            stabilize,
        )
    for name in ACTIONS:
        columns, rows = ACTION_LAYOUT.get(name, (4, 2))
        compile_one(
            SOURCE_ROOT / "marks_actions" / f"{name}.png",
            OUTPUT_ROOT / f"marks_{name}.clw",
            ACTION_DOWNSAMPLE.get(name, 2.0),
            "dark-cap",
            columns=columns,
            rows=rows,
            frame_ms=ACTION_FRAME_MS.get(name, 100),
            max_core_compression=ACTION_MAX_COMPRESSION.get(name, 0.25),
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
