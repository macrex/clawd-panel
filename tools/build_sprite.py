#!/usr/bin/env python3
"""Compila uma sequência de PNGs diretamente para o formato ``.clw``.

Exemplo:

    python tools/build_sprite.py frames/eureka sdcard/clawd/eureka.clw \
        --frame-ms 125 --scale auto

Os PNGs são ordenados pelo nome. O crop usa a união de todos os frames,
simétrico no eixo horizontal, para preservar o centro do personagem.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from clw_format import TRANSPARENT_KEY, ClwError, parse_clw, rgb_to_565, write_clw


DEFAULT_BG = (0x1A, 0x1A, 0x2E)
BOX_W, BOX_H = 440, 200


def parse_hex_color(value: str) -> tuple[int, int, int]:
    raw = value.strip().lstrip("#")
    if len(raw) != 6:
        raise argparse.ArgumentTypeError("use uma cor no formato #RRGGBB")
    try:
        return tuple(int(raw[i : i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("cor hexadecimal invalida") from exc


def rgba_to_565(
    rgba: tuple[int, int, int, int],
    *,
    key: int,
    background: tuple[int, int, int],
) -> int:
    r, g, b, alpha = rgba
    if alpha == 0 or (alpha == 255 and (r, g, b) == background):
        return key
    if alpha < 255:
        mix = alpha / 255.0
        r = int(r * mix + background[0] * (1.0 - mix))
        g = int(g * mix + background[1] * (1.0 - mix))
        b = int(b * mix + background[2] * (1.0 - mix))
    value = rgb_to_565(r, g, b)
    return value + 1 if value == key else value


def symmetric_crop_box(
    frames: list[list[int]], width: int, height: int, key: int
) -> tuple[int, int, int, int]:
    min_x, min_y, max_x, max_y = width, height, -1, -1
    for pixels in frames:
        for index, value in enumerate(pixels):
            if value == key:
                continue
            x, y = index % width, index // width
            min_x, max_x = min(min_x, x), max(max_x, x)
            min_y, max_y = min(min_y, y), max(max_y, y)
    if max_x < 0:
        raise ClwError("todos os frames sao transparentes")

    center = width / 2.0
    half = max(center - min_x, max_x - center + 1)
    crop_x = max(0, int(center - half))
    crop_w = min(width - crop_x, int(half * 2))
    if crop_w % 2:
        if crop_x + crop_w < width:
            crop_w += 1
        elif crop_x:
            crop_x -= 1
            crop_w += 1
    return crop_x, min_y, crop_w, max_y - min_y + 1


def crop_frames(
    frames: list[list[int]],
    original_width: int,
    box: tuple[int, int, int, int],
) -> list[list[int]]:
    x, y, width, height = box
    cropped: list[list[int]] = []
    for pixels in frames:
        frame: list[int] = []
        for row in range(y, y + height):
            start = row * original_width + x
            frame.extend(pixels[start : start + width])
        cropped.append(frame)
    return cropped


def fit_scale(width: int, height: int) -> int:
    return max(1, min(4, BOX_W // width, BOX_H // height))


def load_png_frames(
    source: Path,
    *,
    key: int,
    background: tuple[int, int, int],
) -> tuple[list[list[int]], int, int, list[Path]]:
    try:
        from PIL import Image
    except ImportError as exc:
        raise ClwError("Pillow e necessario: python -m pip install Pillow") from exc

    files = sorted(source.glob("*.png"))
    if not files:
        raise ClwError(f"nenhum PNG encontrado em {source}")

    width = height = 0
    frames: list[list[int]] = []
    for path in files:
        with Image.open(path) as image:
            rgba = image.convert("RGBA")
            if not width:
                width, height = rgba.size
            elif rgba.size != (width, height):
                raise ClwError(
                    f"{path.name} mede {rgba.width}x{rgba.height}; "
                    f"esperado {width}x{height}"
                )
            frames.append(
                [
                    rgba_to_565(pixel, key=key, background=background)
                    for pixel in rgba.getdata()
                ]
            )
    return frames, width, height, files


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compila frames PNG diretamente para um sprite .clw"
    )
    parser.add_argument("frames", type=Path, help="pasta com os PNGs")
    parser.add_argument("output", type=Path, help="arquivo .clw de saida")
    parser.add_argument("--frame-ms", type=int, default=125)
    parser.add_argument(
        "--scale",
        default="auto",
        help="escala inteira de 1 a 4, ou auto (padrao)",
    )
    parser.add_argument("--no-crop", action="store_true")
    parser.add_argument("--background", type=parse_hex_color, default=DEFAULT_BG)
    parser.add_argument("--key", type=lambda v: int(v, 0), default=TRANSPARENT_KEY)
    args = parser.parse_args()

    if not args.frames.is_dir():
        parser.error(f"pasta nao encontrada: {args.frames}")
    if args.frame_ms <= 0:
        parser.error("--frame-ms precisa ser positivo")
    if not 0 <= args.key <= 0xFFFF:
        parser.error("--key precisa caber em uint16")

    try:
        frames, width, height, files = load_png_frames(
            args.frames, key=args.key, background=args.background
        )
        original = (width, height)
        if not args.no_crop:
            box = symmetric_crop_box(frames, width, height, args.key)
            frames = crop_frames(frames, width, box)
            _x, _y, width, height = box

        if args.scale == "auto":
            scale = fit_scale(width, height)
        else:
            scale = int(args.scale)
            if not 1 <= scale <= 4:
                raise ClwError("a escala precisa estar entre 1 e 4")

        size = write_clw(
            args.output,
            frames,
            width,
            height,
            key=args.key,
            frame_ms=args.frame_ms,
            scale=scale,
        )
        sprite = parse_clw(args.output.read_bytes())
    except (ClwError, OSError, ValueError) as exc:
        print(f"erro: {exc}", file=sys.stderr)
        return 1

    print(f"Gerado: {args.output}")
    print(f"  frames: {len(files)} @ {args.frame_ms} ms ({sprite.fps:.1f} fps)")
    print(f"  origem: {original[0]}x{original[1]}")
    print(f"  nativo: {width}x{height}")
    print(f"  tela:   {sprite.screen_width}x{sprite.screen_height} ({scale}x)")
    print(f"  arquivo: {size / 1024:.1f} KB, compressao {sprite.compression_ratio:.1f}x")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
