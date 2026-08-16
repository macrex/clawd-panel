#!/usr/bin/env python3
"""Compila a colecao Clawd Fawkes e suas variantes economicas."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from clw_format import parse_clw
from import_imagegen_sheet import compile_sheet
from sprite_economy import optimize_clw


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "sprites" / "sources" / "imagegen" / "fawkes"
OUTPUT_ROOT = ROOT / "sdcard" / "clawd"


@dataclass(frozen=True)
class Action:
    name: str
    frame_ms: int
    motion: str


ACTIONS = (
    Action("idle", 167, "environment"),
    Action("hat_tip", 125, "attention"),
    Action("cloak_flourish", 125, "attention"),
    Action("lantern_signal", 125, "attention"),
    Action("scroll_reading", 167, "environment"),
    Action("shadow_smoke", 125, "attention"),
)


def main() -> int:
    for action in ACTIONS:
        source = SOURCE_ROOT / f"{action.name}.png"
        output = OUTPUT_ROOT / f"fawkes_{action.name}.clw"
        if not source.is_file():
            raise SystemExit(f"fonte ausente: {source}")
        compile_sheet(
            source,
            output,
            columns=4,
            rows=2,
            downsample=2.0,
            frame_ms=action.frame_ms,
            stabilize="dark-cap",
            max_core_compression=0.15,
        )
        original = parse_clw(output.read_bytes())
        print(
            f"{output.name:32} {original.frames}f "
            f"{original.width}x{original.height} @{original.scale}x "
            f"{original.frame_ms}ms {output.stat().st_size / 1024:.1f}KB"
        )
        for profile in ("eco4", "eco3"):
            economy_output = OUTPUT_ROOT / f"fawkes_{action.name}_{profile}.clw"
            result = optimize_clw(
                output,
                economy_output,
                profile_name=profile,
                motion=action.motion,
                frames=8,
            )
            print(
                f"  {profile}: {result.width}x{result.height} @{result.scale}x, "
                f"{result.colors} cores, {result.bytes_per_pixel_frame:.3f} "
                f"B/px/qd, {result.byte_size / 1024:.1f}KB"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
