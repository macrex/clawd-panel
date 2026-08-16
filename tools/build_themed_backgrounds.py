#!/usr/bin/env python3
"""Compila a coleção de fundos temáticos animados."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw

from build_animated_backgrounds import (
    FRAME_MS,
    FRAME_SIZE,
    FRAMES,
    block,
    save_sheet,
    to_rgb565,
    validate_no_moving_bars,
)
from clw_format import parse_clw, write_clw


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "sprites" / "sources" / "imagegen" / "backgrounds" / "themed"
OUTPUT_ROOT = ROOT / "sdcard" / "clawd"


@dataclass(frozen=True)
class Theme:
    name: str
    effect: str
    bright: tuple[int, int, int]
    dim: tuple[int, int, int]


THEMES = (
    Theme("legendary_kingdom", "fireflies", (255, 244, 118), (72, 165, 111)),
    Theme("energy_martial_arena", "energy", (231, 119, 255), (100, 52, 151)),
    Theme("portal_dragon_cavern", "portal", (221, 104, 255), (255, 137, 42)),
    Theme("chinese_mountain_temple", "lanterns", (255, 196, 78), (115, 49, 26)),
    Theme("sakura_plain", "petals", (255, 190, 217), (220, 116, 159)),
)


def prepare_base(path: Path) -> Image.Image:
    with Image.open(path) as opened:
        source = opened.convert("RGB")
    logical = source.resize((120, 80), Image.Resampling.LANCZOS)
    logical = logical.quantize(
        colors=64,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE,
    ).convert("RGB")
    return logical.resize(FRAME_SIZE, Image.Resampling.NEAREST)


def animate(base: Image.Image, theme: Theme, frame: int) -> Image.Image:
    image = base.copy()
    draw = ImageDraw.Draw(image)

    if theme.effect == "fireflies":
        points = ((24, 44), (43, 78), (61, 34), (184, 48), (205, 79), (224, 37))
        for index, (x, y) in enumerate(points):
            active = (frame + index * 2) % 8 < 2
            block(draw, x, y, theme.bright if active else theme.dim, 4 if active else 2, 2)

    elif theme.effect == "energy":
        for seed in range(12):
            left = seed % 2 == 0
            x = 13 + (seed * 11) % 45 if left else 182 + (seed * 9) % 45
            y = 108 - ((seed * 17 + frame * 6) % 78)
            color = theme.bright if (seed + frame) % 3 == 0 else theme.dim
            block(draw, x, y, color, 2, 2 + seed % 2 * 2)

    elif theme.effect == "portal":
        # O portal pulsa em pontos, nunca como um anel/linha inteira.
        for index, (x, y) in enumerate(((112, 23), (126, 23), (116, 35), (124, 35))):
            active = (frame + index) % 4 < 2
            block(draw, x, y, theme.bright if active else (83, 37, 121), 4, 2)
        flame = theme.dim if frame % 4 < 2 else (255, 205, 79)
        block(draw, 24, 70, flame, 4, 4)
        block(draw, 212, 70, flame, 4, 4)

    elif theme.effect == "lanterns":
        glow = theme.bright if frame % 4 < 2 else theme.dim
        block(draw, 23, 58, glow, 4, 4)
        block(draw, 213, 58, glow, 4, 4)
        for index, (x, y) in enumerate(((49, 41), (190, 39), (57, 84), (181, 82))):
            color = (130, 190, 119) if (frame + index) % 5 == 0 else (43, 92, 68)
            block(draw, x, y, color, 2, 2)

    elif theme.effect == "petals":
        for seed in range(16):
            x = (seed * 31 + frame * (2 + seed % 3)) % 240
            y = (seed * 17 + frame * 5) % 118
            color = theme.bright if seed % 3 == 0 else theme.dim
            block(draw, x, y, color, 4 if seed % 4 == 0 else 2, 2)

    return image


def main() -> int:
    for theme in THEMES:
        key_art = SOURCE_ROOT / f"{theme.name}_key.png"
        if not key_art.is_file():
            raise SystemExit(f"arte-base ausente: {key_art}")
        base = prepare_base(key_art)
        frames = [animate(base, theme, frame) for frame in range(FRAMES)]
        validate_no_moving_bars(frames, f"background_theme_{theme.name}")

        name = f"background_theme_{theme.name}"
        sheet = SOURCE_ROOT / f"{name}.png"
        output = OUTPUT_ROOT / f"{name}.clw"
        save_sheet(frames, sheet)
        write_clw(
            output,
            [to_rgb565(frame) for frame in frames],
            *FRAME_SIZE,
            frame_ms=FRAME_MS,
            scale=2,
        )
        sprite = parse_clw(output.read_bytes())
        print(
            f"{output.name:48} {sprite.frames}f "
            f"{sprite.width}x{sprite.height} @{sprite.scale}x "
            f"{sprite.frame_ms}ms {output.stat().st_size / 1024:.1f}KB"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
