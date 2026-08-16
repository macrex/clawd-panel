#!/usr/bin/env python3
"""Normaliza e compila os cinco estados climaticos do Clawd Original."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from clw_format import parse_clw
from import_imagegen_sheet import compile_sheet
from normalize_sprite_sheet import normalize_sheet


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "sprites" / "sources" / "imagegen" / "climate"
OUTPUT_ROOT = ROOT / "sdcard" / "clawd"


@dataclass(frozen=True)
class ClimateState:
    name: str
    frame_ms: int
    downsample: float
    stabilize: str = "warm-core"


STATES = (
    ClimateState("hot", 167, 2.0),
    ClimateState("cold", 167, 2.0),
    ClimateState("very_cold", 167, 2.0),
    ClimateState("very_hot", 125, 2.0),
    ClimateState("swimwear", 167, 2.0),
    ClimateState("swim_briefs", 167, 1.5, "warm-body"),
)


def main() -> int:
    for state in STATES:
        source = SOURCE_ROOT / f"{state.name}.png"
        output = OUTPUT_ROOT / f"clawd_{state.name}.clw"
        if not source.is_file():
            raise SystemExit(f"fonte ausente: {source}")
        normalize_sheet(source, columns=4, rows=2)
        compile_sheet(
            source,
            output,
            columns=4,
            rows=2,
            downsample=state.downsample,
            frame_ms=state.frame_ms,
            stabilize=state.stabilize,
            max_core_compression=0.12,
            validate_core_geometry=True,
        )
        sprite = parse_clw(output.read_bytes())
        print(
            f"{output.name:26} {sprite.frames}f "
            f"{sprite.width}x{sprite.height} @{sprite.scale}x "
            f"{sprite.frame_ms}ms {output.stat().st_size / 1024:.1f}KB"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
