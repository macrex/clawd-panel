#!/usr/bin/env python3
"""Compila as versões EM PÉ dos dez fundos de nível (320x480 na tela).

A arte-base é a mesma dos deitados (as keys de 1536x1024): daqui sai um recorte
central na proporção 2:3, o único jeito de ficar em pé sem esticar nem deitar a
cena. O pipeline lógico é o mesmo — 80x120 quantizado, NEAREST para 160x240,
scale 2 no arquivo — e os efeitos animados são os mesmos quatro, com as
coordenadas remapeadas para a moldura vertical: o que deitado vivia nas
laterais continua nas laterais, só que agora elas são estreitas e altas.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw

from build_animated_backgrounds import FRAME_MS, FRAMES, block, to_rgb565
from build_level_backgrounds import BACKGROUNDS, LevelBackground
from clw_format import parse_clw, write_clw

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "sprites" / "sources" / "imagegen" / "backgrounds" / "level_up"
OUTPUT_ROOT = ROOT / "sdcard" / "clawd"

FRAME_V = (160, 240)


def prepare_base_retrato(path: Path) -> Image.Image:
    with Image.open(path) as opened:
        source = opened.convert("RGB")
    # Recorte central 2:3. Nas keys de 1536x1024 isso da 682x1024.
    corte_w = source.height * 2 // 3
    x0 = (source.width - corte_w) // 2
    source = source.crop((x0, 0, x0 + corte_w, source.height))
    logical = source.resize((80, 120), Image.Resampling.LANCZOS)
    logical = logical.quantize(
        colors=64,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE,
    ).convert("RGB")
    return logical.resize(FRAME_V, Image.Resampling.NEAREST)


def fixed_glints_v(draw: ImageDraw.ImageDraw, frame: int,
                   bright: tuple[int, int, int], dim: tuple[int, int, int]) -> None:
    # Seis brilhos como no deitado, redistribuidos pela moldura alta: tres no
    # terco de cima, tres no de baixo — o centro e do bicho.
    points = ((16, 24), (76, 44), (136, 20), (20, 196), (82, 214), (138, 190))
    for index, (x, y) in enumerate(points):
        active = (frame + index * 2) % 8 < 2
        block(draw, x, y, bright if active else dim, 4 if active else 2, 2)


def animate_v(base: Image.Image, spec: LevelBackground, frame: int) -> Image.Image:
    image = base.copy()
    draw = ImageDraw.Draw(image)

    if spec.effect == "dust":
        for seed in range(10):
            left = seed % 2 == 0
            x0 = 6 + (seed * 13) % 28 if left else 126 + (seed * 11) % 26
            x = x0 + ((frame + seed) % 3) * 2
            y = 30 + ((seed * 17 + frame * 3) % 180)
            block(draw, x, y, spec.bright if seed % 3 == 0 else spec.dim, 2, 2)
    elif spec.effect == "elemental":
        for seed in range(10):
            left = seed % 2 == 0
            x = 8 + (seed * 9) % 30 if left else 122 + (seed * 7) % 30
            y = 220 - ((seed * 13 + frame * 5) % 190)
            block(draw, x, y, spec.bright if left else spec.dim, 2, 2 + seed % 2 * 2)
    elif spec.effect == "embers":
        for seed in range(10):
            x = 8 + (seed * 7) % 28 if seed % 2 == 0 else 124 + (seed * 9) % 28
            y = 222 - ((seed * 19 + frame * 6) % 180)
            block(draw, x, y, spec.bright if (seed + frame) % 3 == 0 else spec.dim, 2, 2)
    elif spec.effect == "eclipse":
        fixed_glints_v(draw, frame, spec.bright, spec.dim)
        fragments = ((22, 150), (36, 100), (120, 104), (134, 152))
        for index, (x, y) in enumerate(fragments):
            color = spec.bright if (frame + index) % 4 == 0 else spec.dim
            block(draw, x, y, color, 2, 4)
    else:
        fixed_glints_v(draw, frame, spec.bright, spec.dim)
    return image


def main() -> int:
    for spec in BACKGROUNDS:
        key_art = SOURCE_ROOT / f"level_{spec.suffix}_key.png"
        if not key_art.is_file():
            raise SystemExit(f"arte-base ausente: {key_art}")
        base = prepare_base_retrato(key_art)
        frames = [animate_v(base, spec, frame) for frame in range(FRAMES)]

        output = OUTPUT_ROOT / f"{spec.name}_v.clw"
        write_clw(
            output,
            [to_rgb565(frame) for frame in frames],
            *FRAME_V,
            frame_ms=FRAME_MS,
            scale=2,
        )
        sprite = parse_clw(output.read_bytes())
        print(
            f"{output.name:41} {sprite.frames}f "
            f"{sprite.width}x{sprite.height} @{sprite.scale}x "
            f"{sprite.frame_ms}ms {output.stat().st_size / 1024:.1f}KB"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
