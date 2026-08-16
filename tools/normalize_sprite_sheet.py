#!/usr/bin/env python3
"""Completa uma sprite sheet com transparencia ate a grade ficar exata."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def normalize_sheet(path: Path, *, columns: int, rows: int) -> bool:
    """Adiciona somente pixels transparentes a direita/embaixo, se preciso."""
    with Image.open(path) as opened:
        source = opened.convert("RGBA")

    width = ((source.width + columns - 1) // columns) * columns
    height = ((source.height + rows - 1) // rows) * rows
    if (width, height) == source.size:
        return False

    normalized = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    normalized.alpha_composite(source, (0, 0))
    normalized.save(path, optimize=True)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--rows", type=int, default=2)
    args = parser.parse_args()
    if min(args.columns, args.rows) <= 0:
        parser.error("columns e rows precisam ser positivos")

    for path in args.paths:
        if not path.is_file():
            parser.error(f"arquivo nao encontrado: {path}")
        changed = normalize_sheet(path, columns=args.columns, rows=args.rows)
        with Image.open(path) as image:
            state = "normalizada" if changed else "ja exata"
            print(f"{path}: {image.width}x{image.height} ({state})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
