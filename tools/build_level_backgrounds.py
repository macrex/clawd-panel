#!/usr/bin/env python3
"""Compila os dez fundos animados da progressão de nível 1–99."""

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
SOURCE_ROOT = ROOT / "sprites" / "sources" / "imagegen" / "backgrounds" / "level_up"
OUTPUT_ROOT = ROOT / "sdcard" / "clawd"


@dataclass(frozen=True)
class LevelBackground:
    minimum: int
    maximum: int
    effect: str
    bright: tuple[int, int, int]
    dim: tuple[int, int, int]

    @property
    def suffix(self) -> str:
        return f"{self.minimum:03d}_{self.maximum:03d}"

    @property
    def name(self) -> str:
        return f"background_level_{self.suffix}"


BACKGROUNDS = (
    LevelBackground(1, 9, "dust", (229, 158, 76), (101, 67, 47)),
    LevelBackground(10, 19, "fixed", (255, 190, 77), (111, 71, 31)),
    LevelBackground(20, 29, "fixed", (96, 174, 235), (42, 79, 111)),
    LevelBackground(30, 39, "fixed", (45, 225, 246), (18, 83, 115)),
    LevelBackground(40, 49, "elemental", (255, 157, 48), (64, 184, 235)),
    LevelBackground(50, 59, "fixed", (202, 102, 255), (83, 43, 123)),
    LevelBackground(60, 69, "fixed", (91, 228, 255), (30, 91, 193)),
    LevelBackground(70, 79, "embers", (255, 208, 83), (160, 60, 31)),
    LevelBackground(80, 89, "fixed", (246, 244, 205), (70, 202, 255)),
    LevelBackground(90, 99, "eclipse", (255, 215, 86), (59, 218, 245)),
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


def fixed_glints(
    draw: ImageDraw.ImageDraw,
    frame: int,
    bright: tuple[int, int, int],
    dim: tuple[int, int, int],
) -> None:
    points = ((20, 28), (42, 56), (62, 20), (178, 28), (201, 58), (222, 22))
    for index, (x, y) in enumerate(points):
        active = (frame + index * 2) % 8 < 2
        block(draw, x, y, bright if active else dim, 4 if active else 2, 2)


def animate(base: Image.Image, spec: LevelBackground, frame: int) -> Image.Image:
    image = base.copy()
    draw = ImageDraw.Draw(image)

    if spec.effect == "dust":
        for seed in range(10):
            left = seed % 2 == 0
            x0 = 10 + (seed * 13) % 42 if left else 188 + (seed * 11) % 40
            x = x0 + ((frame + seed) % 3) * 2
            y = 38 + ((seed * 17 + frame * 3) % 76)
            block(draw, x, y, spec.bright if seed % 3 == 0 else spec.dim, 2, 2)
    elif spec.effect == "elemental":
        for seed in range(10):
            left = seed % 2 == 0
            x = 12 + (seed * 9) % 45 if left else 184 + (seed * 7) % 44
            y = 111 - ((seed * 13 + frame * 5) % 62)
            block(draw, x, y, spec.bright if left else spec.dim, 2, 2 + seed % 2 * 2)
    elif spec.effect == "embers":
        for seed in range(10):
            x = 14 + (seed * 7) % 40 if seed % 2 == 0 else 188 + (seed * 9) % 38
            y = 112 - ((seed * 19 + frame * 6) % 58)
            block(draw, x, y, spec.bright if (seed + frame) % 3 == 0 else spec.dim, 2, 2)
    elif spec.effect == "eclipse":
        fixed_glints(draw, frame, spec.bright, spec.dim)
        fragments = ((34, 76), (50, 48), (188, 50), (207, 78))
        for index, (x, y) in enumerate(fragments):
            color = spec.bright if (frame + index) % 4 == 0 else spec.dim
            block(draw, x, y, color, 2, 4)
    else:
        fixed_glints(draw, frame, spec.bright, spec.dim)
    return image


def main() -> int:
    for spec in BACKGROUNDS:
        key_art = SOURCE_ROOT / f"level_{spec.suffix}_key.png"
        if not key_art.is_file():
            raise SystemExit(f"arte-base ausente: {key_art}")
        base = prepare_base(key_art)
        frames = [animate(base, spec, frame) for frame in range(FRAMES)]
        validate_no_moving_bars(frames, spec.name)

        sheet = SOURCE_ROOT / f"{spec.name}.png"
        output = OUTPUT_ROOT / f"{spec.name}.clw"
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
            f"{output.name:39} {sprite.frames}f "
            f"{sprite.width}x{sprite.height} @{sprite.scale}x "
            f"{sprite.frame_ms}ms {output.stat().st_size / 1024:.1f}KB"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
