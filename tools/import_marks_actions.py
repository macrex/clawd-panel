#!/usr/bin/env python3
"""Recompila todas as acoes do Clawd Marks para a pasta do SD."""

from __future__ import annotations

import argparse
from pathlib import Path

from import_imagegen_sheet import compile_sheet
from clw_format import parse_clw


ROOT = Path(__file__).resolve().parents[1]
ACTIONS = (
    "salute",
    "march",
    "hammering",
    "reading",
    "command",
    "celebrate",
    "rest",
    "alert",
    "speech",
    "victory",
)
ACTION_DOWNSAMPLE = {"victory": 2.1}
ACTION_LAYOUT = {"rest": (4, 3)}
ACTION_FRAME_MS = {"rest": 220}
ACTION_MAX_COMPRESSION = {"rest": 0.10}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--sources",
        type=Path,
        default=ROOT / "sprites" / "sources" / "imagegen" / "marks_actions",
    )
    parser.add_argument(
        "--output", type=Path, default=ROOT / "sdcard" / "clawd"
    )
    parser.add_argument("--frame-ms", type=int, default=100)
    args = parser.parse_args()

    missing = [name for name in ACTIONS if not (args.sources / f"{name}.png").is_file()]
    if missing:
        parser.error(f"folhas ausentes: {', '.join(missing)}")

    for name in ACTIONS:
        output = args.output / f"marks_{name}.clw"
        columns, rows = ACTION_LAYOUT.get(name, (4, 2))
        compile_sheet(
            args.sources / f"{name}.png",
            output,
            columns=columns,
            rows=rows,
            downsample=ACTION_DOWNSAMPLE.get(name, 2.0),
            frame_ms=ACTION_FRAME_MS.get(name, args.frame_ms),
            stabilize="dark-cap",
            max_core_compression=ACTION_MAX_COMPRESSION.get(name, 0.25),
        )
        sprite = parse_clw(output.read_bytes())
        print(
            f"{name:10} {sprite.frames} frames | "
            f"{sprite.screen_width}x{sprite.screen_height} | "
            f"{output.stat().st_size / 1024:.1f} KB"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
