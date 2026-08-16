#!/usr/bin/env python3
"""Gera variantes economicas dos sprites ImageGen da colecao."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from clw_format import read_clw
from sprite_economy import (
    DEFAULT_COLORS,
    DEFAULT_MAX_BPPF,
    MAX_FRAMES,
    MIN_FRAMES,
    PROFILES,
    optimize_clw,
)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ASSETS = ROOT / "sdcard" / "clawd"
DEFAULT_MANIFEST = ROOT / "sprites" / "manifest.json"
ATTENTION_ROLES = {"acao", "alerta", "celebracao", "trabalho", "transicao"}


def motion_for(roles: list[str]) -> str:
    return "attention" if ATTENTION_ROLES.intersection(roles) else "environment"


def imagegen_names(manifest: dict) -> list[str]:
    return sorted(
        name
        for name, behavior in manifest.get("sprites", {}).items()
        if str(behavior.get("source_file", "")).startswith("imagegen/")
        and behavior.get("economy_enabled", True)
        and not name.endswith(("_eco4", "_eco3"))
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", type=Path, default=DEFAULT_ASSETS)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument(
        "--profiles",
        nargs="+",
        choices=tuple(PROFILES),
        default=["eco4", "eco3"],
    )
    parser.add_argument("--colors", type=int, default=DEFAULT_COLORS)
    parser.add_argument("--max-bppf", type=float, default=DEFAULT_MAX_BPPF)
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    entries = manifest.get("sprites", {})
    generated = []
    for name in imagegen_names(manifest):
        source = args.assets / f"{name}.clw"
        if not source.is_file():
            parser.error(f"sprite base ausente: {source}")
        original = read_clw(source)
        target_frames = min(MAX_FRAMES, max(MIN_FRAMES, original.frames))
        motion = motion_for(entries[name].get("roles", []))
        for profile in args.profiles:
            output = args.assets / f"{name}_{profile}.clw"
            result = optimize_clw(
                source,
                output,
                profile_name=profile,
                motion=motion,
                frames=target_frames,
                colors=args.colors,
                max_bppf=args.max_bppf,
            )
            generated.append(result)
            print(
                f"{output.name:31} {result.frames:2}f "
                f"{result.width:3}x{result.height:<2} @{result.scale}x "
                f"{result.frame_ms:3}ms {result.colors:2}c "
                f"{result.content_scale:3.0%} "
                f"{result.bytes_per_pixel_frame:.3f} B/px/qd "
                f"{result.byte_size / 1024:5.1f}KB"
            )
    print(
        f"\n{len(generated)} variantes geradas; pior caso "
        f"{max(item.bytes_per_pixel_frame for item in generated):.3f} B/px/qd"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
