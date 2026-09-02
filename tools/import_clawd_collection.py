#!/usr/bin/env python3
"""Importa a colecao SVG complementar do clawd-tank para arquivos CLW."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
from shutil import copy2
import tempfile

from build_sprite import (
    DEFAULT_BG,
    crop_frames,
    fit_scale,
    load_png_frames,
    symmetric_crop_box,
)
from clw_format import TRANSPARENT_KEY, parse_clw, write_clw
from render_svg_frames import render_svg


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REFERENCE = ROOT.parent / "clawd-tank"
DEFAULT_SOURCE_COPY = ROOT / "sprites" / "sources" / "vector"


@dataclass(frozen=True)
class ImportSpec:
    name: str
    source: str
    duration: float
    frame_ms: int


# Duracoes seguem os ciclos mestres declarados nos SVGs originais. Tempos de
# frame foram escolhidos por movimento: 80 ms para acao ritmica, 100 ms para
# narrativa e 125 ms para respiracao lenta.
COLLECTION = (
    ImportSpec("eureka", "clawd-eureka.svg", 6.0, 100),
    ImportSpec("grooving", "clawd-grooving.svg", 1.6, 80),
    # 3.0 s e o menor ciclo que fecha limpo: o pulo dura 1 s e as faiscas
    # 1,5 — em 3 s cabem tres pulos e duas faiscas, sem engasgo no loop.
    ImportSpec("happy", "clawd-happy.svg", 3.0, 100),
    ImportSpec("hat_mishap", "clawd-hat-mishap.svg", 7.0, 100),
    ImportSpec("low_battery", "clawd-idle-low-battery.svg", 8.0, 125),
    ImportSpec("wake", "clawd-wake.svg", 1.5, 100),
    ImportSpec("builder", "clawd-working-builder.svg", 6.0, 100),
    ImportSpec("carrying", "clawd-working-carrying.svg", 2.0, 80),
    ImportSpec("overheated", "clawd-working-overheated.svg", 3.0, 100),
    ImportSpec("pushing", "clawd-working-pushing.svg", 2.0, 80),
)


def compile_frames(frames_dir: Path, output: Path, frame_ms: int) -> None:
    frames, width, height, _files = load_png_frames(
        frames_dir, key=TRANSPARENT_KEY, background=DEFAULT_BG
    )
    box = symmetric_crop_box(frames, width, height, TRANSPARENT_KEY)
    frames = crop_frames(frames, width, box)
    _x, _y, width, height = box
    scale = fit_scale(width, height)
    write_clw(
        output,
        frames,
        width,
        height,
        key=TRANSPARENT_KEY,
        frame_ms=frame_ms,
        scale=scale,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--reference",
        type=Path,
        default=DEFAULT_REFERENCE,
        help="raiz do repositorio clawd-tank",
    )
    parser.add_argument(
        "--output", type=Path, default=ROOT / "sdcard" / "clawd"
    )
    parser.add_argument(
        "--source-copy",
        type=Path,
        default=DEFAULT_SOURCE_COPY,
        help="pasta local que recebe uma copia revisavel dos SVGs",
    )
    parser.add_argument(
        "--only",
        nargs="*",
        help="nomes especificos; sem esta opcao importa toda a colecao",
    )
    parser.add_argument("--render-scale", type=float, default=4.0)
    args = parser.parse_args()

    source_dir = args.reference / "assets" / "svg-animations"
    selected = [
        item for item in COLLECTION if not args.only or item.name in args.only
    ]
    unknown = set(args.only or ()) - {item.name for item in COLLECTION}
    if unknown:
        parser.error(f"sprites desconhecidos: {', '.join(sorted(unknown))}")
    missing = [item.source for item in selected if not (source_dir / item.source).is_file()]
    if missing:
        parser.error(f"fontes ausentes em {source_dir}: {', '.join(missing)}")
    args.output.mkdir(parents=True, exist_ok=True)
    args.source_copy.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="clawd-collection-") as temporary:
        temporary_root = Path(temporary)
        for position, item in enumerate(selected, 1):
            print(f"[{position}/{len(selected)}] {item.name}: renderizando...")
            frames_dir = temporary_root / item.name
            render_svg(
                source_dir / item.source,
                frames_dir,
                frame_ms=item.frame_ms,
                duration=item.duration,
                scale=args.render_scale,
            )
            output = args.output / f"{item.name}.clw"
            compile_frames(frames_dir, output, item.frame_ms)
            copy2(source_dir / item.source, args.source_copy / item.source)
            sprite = parse_clw(output.read_bytes())
            print(
                f"    {sprite.frames} frames, {sprite.width}x{sprite.height} @ "
                f"{sprite.scale}x, {output.stat().st_size / 1024:.1f} KB"
            )

    print(f"Colecao concluida: {len(selected)} sprites em {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
