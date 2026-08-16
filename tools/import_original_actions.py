#!/usr/bin/env python3
"""Compila as acoes financeiras do Clawd Original, sem variantes ECO."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from clw_format import parse_clw
from import_imagegen_sheet import compile_sheet


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "sprites" / "sources" / "imagegen" / "original_actions"
OUTPUT_ROOT = ROOT / "sdcard" / "clawd"


@dataclass(frozen=True)
class Action:
    name: str
    frame_ms: int
    downsample: float


ACTIONS = (
    Action("counting_money", 167, 2.0),
    Action("throwing_money", 125, 1.5),
    Action("eating_tokens", 167, 2.0),
)


def main() -> int:
    for action in ACTIONS:
        source = SOURCE_ROOT / f"{action.name}.png"
        output = OUTPUT_ROOT / f"clawd_{action.name}.clw"
        if not source.is_file():
            raise SystemExit(f"fonte ausente: {source}")
        compile_sheet(
            source,
            output,
            columns=4,
            rows=2,
            downsample=action.downsample,
            frame_ms=action.frame_ms,
            stabilize="warm-core",
        )
        sprite = parse_clw(output.read_bytes())
        print(
            f"{output.name:30} {sprite.frames}f "
            f"{sprite.width}x{sprite.height} @{sprite.scale}x "
            f"{sprite.frame_ms}ms {output.stat().st_size / 1024:.1f}KB"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
