#!/usr/bin/env python3
"""Gera fundos animados 240x160 @2x a partir do atlas ImageGen.

O cenário-base nunca se move. Cada animação altera apenas pequenos detalhes
ambientais, portanto horizonte, câmera e linha do chão permanecem idênticos em
todos os quadros e o Clawd pode ser composto por cima sem pular.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw

from clw_format import TRANSPARENT_KEY, parse_clw, rgb_to_565, write_clw


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "sprites" / "sources" / "imagegen" / "backgrounds"
ATLAS = SOURCE_ROOT / "background_atlas.png"
OUTPUT_ROOT = ROOT / "sdcard" / "clawd"
FRAME_SIZE = (240, 160)
GRID = (2, 3)
FRAMES = 8
FRAME_MS = 250


@dataclass(frozen=True)
class Background:
    name: str
    column: int
    row: int
    effect: str


BACKGROUNDS = (
    Background("background_cyber_rain", 0, 0, "rain"),
    Background("background_sunset_beach", 1, 0, "water"),
    Background("background_hacker_workshop", 0, 1, "workshop"),
    Background("background_aurora_snow", 1, 1, "snow"),
    Background("background_volcanic_forge", 0, 2, "forge"),
    Background("background_lunar_outpost", 1, 2, "space"),
)


def crop_panel(atlas: Image.Image, spec: Background) -> Image.Image:
    cell_w = atlas.width // GRID[0]
    cell_h = atlas.height // GRID[1]
    box = (
        spec.column * cell_w,
        spec.row * cell_h,
        (spec.column + 1) * cell_w,
        (spec.row + 1) * cell_h,
    )
    panel = atlas.crop(box).convert("RGB")
    # Primeiro reduz para a grade lógica 120x80 e depois amplia 2x. Isso
    # remove o pseudo-antialiasing do modelo e produz pixel-art realmente
    # discreta, além de melhorar bastante o RLE no ESP32.
    logical = panel.resize((120, 80), Image.Resampling.LANCZOS)
    logical = logical.quantize(colors=64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert("RGB")
    return logical.resize(FRAME_SIZE, Image.Resampling.NEAREST)


def block(draw: ImageDraw.ImageDraw, x: int, y: int, color: tuple[int, int, int], w: int = 2, h: int = 2) -> None:
    draw.rectangle((x, y, x + w - 1, y + h - 1), fill=color)


def animate(base: Image.Image, effect: str, frame: int) -> Image.Image:
    image = base.copy()
    draw = ImageDraw.Draw(image)

    if effect == "rain":
        # Chuva em trajetórias periódicas; evita a faixa central baixa onde o
        # personagem fica, para sua silhueta continuar legível.
        for seed in range(18):
            x = (seed * 37 + frame * 4) % 240
            y = (seed * 23 + frame * 8) % 126
            if 72 < x < 168 and y > 54:
                continue
            draw.line((x, y, x - 2, y + 6), fill=(92, 155, 225), width=2)
        for x in (22, 204):
            color = (55, 235, 244) if (frame + x) % 4 < 2 else (25, 105, 150)
            block(draw, x, 65, color, 4, 2)

    elif effect == "water":
        colors = ((96, 229, 221), (230, 237, 190), (35, 167, 194))
        for band, y in enumerate((91, 97, 103, 109)):
            shift = (frame * 6 + band * 13) % 48
            for x in range(-24 + shift, 264, 48):
                draw.rectangle((x, y, x + 12, y + 1), fill=colors[(band + frame // 2) % len(colors)])

    elif effect == "workshop":
        # Conteúdo de monitor e LEDs mudam em posições FIXAS. Linhas móveis
        # pareciam duas barras subindo e descendo nas laterais do cenário.
        monitor_pixels = (
            (18, 92), (24, 92), (30, 92), (38, 92),
            (198, 92), (204, 92), (212, 92), (218, 92),
        )
        for index, (x, y) in enumerate(monitor_pixels):
            active = (frame + index * 2) % 8 < 3
            color = (52, 231, 190) if active else (20, 86, 75)
            block(draw, x, y + (index % 2) * 4, color, 4, 2)
        for index, (x, y) in enumerate(((21, 119), (29, 119), (211, 117), (219, 117))):
            color = (240, 180, 64) if (frame + index) % 3 == 0 else (36, 94, 78)
            block(draw, x, y, color, 2, 2)

    elif effect == "snow":
        for seed in range(20):
            x = (seed * 43 + frame * (2 + seed % 2)) % 240
            y = (seed * 19 + frame * 4) % 132
            if 76 < x < 164 and y > 62:
                continue
            color = (225, 245, 255) if seed % 3 else (124, 190, 230)
            block(draw, x, y, color, 2 + (seed % 2) * 2, 2)

    elif effect == "forge":
        for seed in range(13):
            side_x = 22 + (seed * 11) % 42 if seed % 2 == 0 else 177 + (seed * 7) % 40
            y = 113 - ((seed * 17 + frame * 8) % 62)
            color = (255, 220, 75) if (seed + frame) % 3 == 0 else (255, 96, 24)
            block(draw, side_x, y, color, 2, 2 + (seed % 2) * 2)
        # Nada de barras horizontais pulsando. O brilho muda apenas dentro de
        # pequenos pontos das fornalhas laterais, enquanto o chão fica intacto.
        furnace = (255, 205, 64) if frame in (1, 2, 5) else (207, 68, 20)
        block(draw, 16, 105, furnace, 4, 4)
        block(draw, 220, 103, furnace, 4, 4)

    elif effect == "space":
        stars = ((17, 27), (40, 18), (72, 38), (111, 20), (151, 42), (190, 25), (222, 47))
        for index, (x, y) in enumerate(stars):
            phase = (frame + index * 2) % 8
            color = (245, 250, 255) if phase < 2 else (87, 142, 190)
            size = 4 if phase == 0 else 2
            block(draw, x, y, color, size, size)
        beacon = (62, 224, 255) if frame % 4 < 2 else (24, 75, 117)
        block(draw, 218, 87, beacon, 4, 2)

    return image


def to_rgb565(image: Image.Image) -> list[int]:
    pixels = []
    for r, g, b in image.convert("RGB").getdata():
        value = rgb_to_565(r, g, b)
        # Fundo é totalmente opaco. A chave reservada jamais pode aparecer no
        # conteúdo ou abriria um furo transparente durante a composição.
        if value == TRANSPARENT_KEY:
            value ^= 0x0001
        pixels.append(value)
    return pixels


def save_sheet(frames: list[Image.Image], path: Path) -> None:
    sheet = Image.new("RGB", (FRAME_SIZE[0] * 4, FRAME_SIZE[1] * 2))
    for index, frame in enumerate(frames):
        sheet.paste(frame, ((index % 4) * FRAME_SIZE[0], (index // 4) * FRAME_SIZE[1]))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path, optimize=True)


def validate_no_moving_bars(frames: list[Image.Image], name: str, max_run: int = 16) -> None:
    """Rejeita faixas horizontais longas que se deslocam entre quadros."""
    reference = list(frames[0].convert("RGB").getdata())
    width, height = frames[0].size
    for frame_index, image in enumerate(frames[1:], start=1):
        pixels = list(image.convert("RGB").getdata())
        for y in range(height):
            longest = current = 0
            row = y * width
            for x in range(width):
                if pixels[row + x] != reference[row + x]:
                    current += 1
                    longest = max(longest, current)
                else:
                    current = 0
            if longest > max_run:
                raise ValueError(
                    f"{name}: quadro {frame_index + 1} criou barra horizontal "
                    f"de {longest}px na linha {y} (limite {max_run}px)"
                )


def main() -> int:
    if not ATLAS.is_file():
        raise SystemExit(f"atlas ausente: {ATLAS}")
    with Image.open(ATLAS) as opened:
        atlas = opened.convert("RGB")
    if atlas.width % GRID[0] or atlas.height % GRID[1]:
        raise SystemExit(f"atlas {atlas.size} nao divide exatamente em {GRID[0]}x{GRID[1]}")

    for spec in BACKGROUNDS:
        base = crop_panel(atlas, spec)
        frames = [animate(base, spec.effect, frame) for frame in range(FRAMES)]
        # Reflexos do mar são naturalmente mais largos, mas ainda ficam bem
        # abaixo das antigas barras laterais de 34–46 px.
        validate_no_moving_bars(frames, spec.name, max_run=28 if spec.effect == "water" else 16)
        source = SOURCE_ROOT / f"{spec.name}.png"
        output = OUTPUT_ROOT / f"{spec.name}.clw"
        save_sheet(frames, source)
        write_clw(
            output,
            [to_rgb565(frame) for frame in frames],
            *FRAME_SIZE,
            frame_ms=FRAME_MS,
            scale=2,
        )
        sprite = parse_clw(output.read_bytes())
        print(
            f"{output.name:38} {sprite.frames}f "
            f"{sprite.width}x{sprite.height} @{sprite.scale}x "
            f"{sprite.frame_ms}ms {output.stat().st_size / 1024:.1f}KB"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
