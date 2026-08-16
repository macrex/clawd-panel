#!/usr/bin/env python3
"""Gera os 99 niveis animados do Clawd a partir de marcos visuais."""

from __future__ import annotations

import argparse
from collections import Counter, deque
from dataclasses import dataclass
import json
import math
from pathlib import Path
from statistics import median

from PIL import Image, ImageDraw

from clw_format import parse_clw
from import_imagegen_sheet import compile_sheet


ROOT = Path(__file__).resolve().parents[1]
BASE_REFERENCE = ROOT / "sprites" / "references" / "clawd_base" / "frame_000.png"
SOURCE_ROOT = ROOT / "sprites" / "sources" / "imagegen" / "level_progression"
CHECKPOINT_ROOT = SOURCE_ROOT / "checkpoints"
LEVEL_ROOT = SOURCE_ROOT / "levels"
OUTPUT_ROOT = ROOT / "sdcard" / "clawd"
MANIFEST_PATH = ROOT / "sprites" / "level_progression_manifest.json"
CELL_SIZE = (240, 192)
TARGET_EYE_DISTANCE = 48
TARGET_BASELINE = 174


@dataclass(frozen=True)
class Tier:
    start: int
    end: int
    name: str
    description: str
    accent: tuple[int, int, int, int]
    secondary: tuple[int, int, int, int]


TIERS = (
    Tier(1, 9, "Sobrevivente", "equipamento inicial de cobre e couro", (190, 105, 55, 255), (105, 55, 35, 255)),
    Tier(10, 19, "Guardiao de bronze", "armadura de bronze em aperfeicoamento", (224, 139, 58, 255), (104, 62, 30, 255)),
    Tier(20, 29, "Cavaleiro de aco", "aco, reforcos e defesa de energia", (206, 222, 232, 255), (45, 104, 176, 255)),
    Tier(30, 39, "Sentinela runico", "runas ciano e controle de aura", (45, 229, 248, 255), (35, 110, 145, 255)),
    Tier(40, 49, "Vanguarda elemental", "fogo e gelo em equilibrio", (255, 112, 26, 255), (58, 220, 246, 255)),
    Tier(50, 59, "Guardiao arcano", "gemas violetas, halo e orbes", (185, 89, 242, 255), (105, 45, 171, 255)),
    Tier(60, 69, "Bastiao cristalino", "cristais azuis e campo prismatico", (58, 204, 255, 255), (31, 92, 210, 255)),
    Tier(70, 79, "Regente dourado", "ouro, manto e autoridade real", (255, 205, 68, 255), (174, 37, 37, 255)),
    Tier(80, 89, "Protetor celestial", "placas claras, estrelas e asas de energia", (247, 244, 214, 255), (63, 201, 255, 255)),
    Tier(90, 98, "Eclipse lendario", "obsidiana, ouro e runas orbitais", (255, 203, 64, 255), (239, 47, 78, 255)),
    Tier(99, 99, "Clawd ascendente", "forma final com nucleo estelar completo", (255, 240, 164, 255), (67, 216, 255, 255)),
)


def tier_for(level: int) -> Tier:
    return next(tier for tier in TIERS if tier.start <= level <= tier.end)


def make_level_one_checkpoint() -> None:
    """Remove apenas o fundo preto conectado a borda, preservando os olhos."""
    output = CHECKPOINT_ROOT / "level_001.png"
    with Image.open(BASE_REFERENCE) as opened:
        source = opened.convert("RGBA")
    pixels = source.load()
    width, height = source.size
    candidates = {
        (x, y)
        for y in range(height)
        for x in range(width)
        if max(pixels[x, y][:3]) <= 20
    }
    queue: deque[tuple[int, int]] = deque(
        point
        for point in candidates
        if point[0] in {0, width - 1} or point[1] in {0, height - 1}
    )
    background = set(queue)
    while queue:
        x, y = queue.popleft()
        for point in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if point in candidates and point not in background:
                background.add(point)
                queue.append(point)
    for x, y in background:
        r, g, b, _alpha = pixels[x, y]
        pixels[x, y] = (r, g, b, 0)
    output.parent.mkdir(parents=True, exist_ok=True)
    source.save(output, optimize=True)


def clean_residual_chroma(image: Image.Image) -> Image.Image:
    """Remove o raro contorno magenta que sobra em marcos violeta."""
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    for y in range(rgba.height):
        for x in range(rgba.width):
            r, g, b, alpha = pixels[x, y]
            if (
                alpha
                and r > 170
                and b > 170
                and g < 80
                and abs(r - b) < 55
            ):
                pixels[x, y] = (r, g, b, 0)
    return rgba


def dark_components(image: Image.Image) -> list[tuple[int, int, int, int, int]]:
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    remaining = {
        (x, y)
        for y in range(rgba.height)
        for x in range(rgba.width)
        if pixels[x, y][3] > 200 and max(pixels[x, y][:3]) < 38
    }
    components = []
    while remaining:
        start = remaining.pop()
        stack = [start]
        min_x = max_x = start[0]
        min_y = max_y = start[1]
        area = 0
        while stack:
            x, y = stack.pop()
            area += 1
            min_x, max_x = min(min_x, x), max(max_x, x)
            min_y, max_y = min(min_y, y), max(max_y, y)
            for point in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if point in remaining:
                    remaining.remove(point)
                    stack.append(point)
        components.append((area, min_x, min_y, max_x, max_y))
    return components


def find_eyes(image: Image.Image) -> tuple[tuple[int, int, int, int], tuple[int, int, int, int]]:
    alpha_box = image.getchannel("A").getbbox()
    if not alpha_box:
        raise ValueError("checkpoint vazio")
    left, top, right, bottom = alpha_box
    width, height = right - left, bottom - top
    candidates = []
    for area, min_x, min_y, max_x, max_y in dark_components(image):
        box_w, box_h = max_x - min_x + 1, max_y - min_y + 1
        center_x, center_y = (min_x + max_x) / 2, (min_y + max_y) / 2
        fill = area / (box_w * box_h)
        if (
            area >= max(20, width * height * 0.0004)
            and fill > 0.72
            and 0.28 <= box_w / box_h <= 1.35
            and left + width * 0.22 < center_x < left + width * 0.78
            and top + height * 0.18 < center_y < top + height * 0.68
            and box_w < width * 0.16
            and box_h < height * 0.24
        ):
            candidates.append((area, (min_x, min_y, max_x + 1, max_y + 1)))
    pairs = []
    center = (left + right) / 2
    for _area_a, a in candidates:
        for _area_b, b in candidates:
            ax, ay = (a[0] + a[2]) / 2, (a[1] + a[3]) / 2
            bx, by = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
            if ax >= center or bx <= center:
                continue
            distance = bx - ax
            if distance < width * 0.16:
                continue
            score = abs(ay - by) * 6 + abs((a[3] - a[1]) - (b[3] - b[1])) * 3 - distance
            pairs.append((score, a, b))
    if not pairs:
        raise ValueError(f"nao foi possivel detectar os dois olhos em {image.size}")
    _score, left_eye, right_eye = min(pairs, key=lambda item: item[0])
    return left_eye, right_eye


def is_coral(pixel: tuple[int, int, int, int]) -> bool:
    r, g, b, alpha = pixel
    if alpha < 150 or r < 125:
        return False
    return 0.40 <= g / r <= 0.79 and 0.24 <= b / r <= 0.70 and r > g > b


def coral_baseline(image: Image.Image) -> int:
    pixels = image.load()
    rows = [
        y
        for y in range(image.height)
        if any(is_coral(pixels[x, y]) for x in range(round(image.width * 0.18), round(image.width * 0.82)))
    ]
    if not rows:
        raise ValueError("nao foi possivel detectar a linha coral dos pes")
    return max(rows)


def face_color(image: Image.Image) -> tuple[int, int, int, int]:
    colors = [pixel for pixel in image.getdata() if is_coral(pixel)]
    if not colors:
        return (228, 132, 98, 255)
    return tuple(round(median(pixel[index] for pixel in colors)) for index in range(3)) + (255,)


def normalize_checkpoint(path: Path) -> tuple[Image.Image, tuple[tuple[int, int, int, int], tuple[int, int, int, int]]]:
    with Image.open(path) as opened:
        source = clean_residual_chroma(opened)
    eyes = find_eyes(source)
    eye_centers = [((box[0] + box[2]) / 2, (box[1] + box[3]) / 2) for box in eyes]
    distance = eye_centers[1][0] - eye_centers[0][0]
    scale = TARGET_EYE_DISTANCE / distance
    resized = source.resize(
        (round(source.width * scale), round(source.height * scale)),
        Image.Resampling.NEAREST,
    )
    scaled_eyes = tuple(
        tuple(round(value * scale) for value in box) for box in eyes
    )
    eye_mid_x = sum((box[0] + box[2]) / 2 for box in scaled_eyes) / 2
    baseline = coral_baseline(resized)
    offset_x = round(CELL_SIZE[0] / 2 - eye_mid_x)
    offset_y = TARGET_BASELINE - baseline
    canvas = Image.new("RGBA", CELL_SIZE, (0, 0, 0, 0))
    canvas.alpha_composite(resized, (offset_x, offset_y))
    placed_eyes = tuple(
        (box[0] + offset_x, box[1] + offset_y, box[2] + offset_x, box[3] + offset_y)
        for box in scaled_eyes
    )
    if canvas.getchannel("A").getbbox() is None:
        raise ValueError(f"checkpoint ficou fora da celula: {path}")
    return canvas, placed_eyes  # type: ignore[return-value]


def diamond(draw: ImageDraw.ImageDraw, x: int, y: int, radius: int, color: tuple[int, int, int, int]) -> None:
    draw.polygon(((x, y - radius), (x + radius, y), (x, y + radius), (x - radius, y)), fill=color)


def add_level_upgrades(frame: Image.Image, tier: Tier, level: int) -> None:
    progress = level - tier.start
    if progress <= 0:
        return
    draw = ImageDraw.Draw(frame)
    if tier.start == 1:
        features = (
            lambda: draw.rectangle((76, 147, 164, 151), fill=tier.secondary),
            lambda: draw.rectangle((116, 145, 124, 153), fill=tier.accent, outline=tier.secondary),
            lambda: draw.rectangle((43, 127, 57, 132), fill=tier.secondary),
            lambda: draw.rectangle((183, 127, 197, 132), fill=tier.secondary),
            lambda: draw.rectangle((69, 113, 82, 126), fill=tier.accent, outline=tier.secondary),
            lambda: draw.rectangle((158, 113, 171, 126), fill=tier.accent, outline=tier.secondary),
            lambda: draw.rectangle((83, 103, 157, 107), fill=tier.secondary),
            lambda: diamond(draw, 120, 140, 7, tier.accent),
        )
    else:
        features = (
            lambda: draw.rectangle((39, 130, 52, 134), fill=tier.accent),
            lambda: draw.rectangle((188, 130, 201, 134), fill=tier.accent),
            lambda: diamond(draw, 120, 149, 4, tier.secondary),
            lambda: draw.rectangle((72, 111, 77, 118), fill=tier.accent),
            lambda: draw.rectangle((163, 111, 168, 118), fill=tier.accent),
            lambda: diamond(draw, 91, 128, 3, tier.secondary),
            lambda: diamond(draw, 149, 128, 3, tier.secondary),
            lambda: draw.rectangle((117, 78, 123, 85), fill=tier.accent),
            lambda: (diamond(draw, 25, 103, 3, tier.accent), diamond(draw, 215, 103, 3, tier.secondary)),
        )
    for feature in features[:progress]:
        feature()


def add_particles(frame: Image.Image, tier: Tier, level: int, frame_index: int) -> None:
    count = min(6, max(0, level // 18))
    if level - tier.start >= 8:
        count = min(6, count + 1)
    if not count:
        return
    draw = ImageDraw.Draw(frame)
    for index in range(count):
        angle = 2 * math.pi * (index / count + frame_index / 16)
        x = round(120 + math.cos(angle) * (94 - index % 2 * 8))
        y = round(116 + math.sin(angle) * (64 - index % 2 * 6))
        color = tier.accent if (index + frame_index) % 2 == 0 else tier.secondary
        radius = 2 if (level + index) % 3 else 3
        diamond(draw, x, y, radius, color)


def blink(frame: Image.Image, eyes: tuple[tuple[int, int, int, int], tuple[int, int, int, int]], frame_index: int) -> None:
    if frame_index not in {5, 6}:
        return
    draw = ImageDraw.Draw(frame)
    fill = face_color(frame)
    for left, top, right, bottom in eyes:
        if frame_index == 5:
            draw.rectangle((left, (top + bottom) // 2, right - 1, bottom - 1), fill=fill)
        else:
            draw.rectangle((left, top, right - 1, bottom - 1), fill=fill)
            y = (top + bottom) // 2
            draw.rectangle((left, y, right - 1, min(bottom - 1, y + 1)), fill=(0, 0, 0, 255))


def coral_core_box(image: Image.Image) -> tuple[int, int, int, int]:
    """Retorna o maior componente coral, ignorando particulas separadas."""
    pixels = image.load()
    remaining = {
        (x, y)
        for y in range(image.height)
        for x in range(image.width)
        if is_coral(pixels[x, y])
    }
    components: list[tuple[int, int, int, int, int]] = []
    while remaining:
        start = remaining.pop()
        stack = [start]
        min_x = max_x = start[0]
        min_y = max_y = start[1]
        area = 0
        while stack:
            x, y = stack.pop()
            area += 1
            min_x, max_x = min(min_x, x), max(max_x, x)
            min_y, max_y = min(min_y, y), max(max_y, y)
            for point in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if point in remaining:
                    remaining.remove(point)
                    stack.append(point)
        components.append((area, min_x, min_y, max_x + 1, max_y + 1))
    if not components:
        raise ValueError("nenhum nucleo coral foi encontrado")
    _area, left, top, right, bottom = max(components)
    return left, top, right, bottom


def validate_body_lock(frames: list[Image.Image]) -> None:
    """Rejeita salto, squash, stretch ou escala antes de salvar a folha."""
    boxes = [coral_core_box(frame) for frame in frames]
    if any(box != boxes[0] for box in boxes[1:]):
        raise ValueError(f"nucleo corporal instavel entre os quadros: {boxes}")


def make_sheet(level: int) -> Image.Image:
    tier = tier_for(level)
    checkpoint = CHECKPOINT_ROOT / f"level_{tier.start:03d}.png"
    base, eyes = normalize_checkpoint(checkpoint)
    frames = []
    for frame_index in range(8):
        frame = base.copy()
        add_level_upgrades(frame, tier, level)
        add_particles(frame, tier, level, frame_index)
        blink(frame, eyes, frame_index)
        frames.append(frame)
    validate_body_lock(frames)
    sheet = Image.new("RGBA", (CELL_SIZE[0] * 4, CELL_SIZE[1] * 2), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        sheet.alpha_composite(frame, ((index % 4) * CELL_SIZE[0], (index // 4) * CELL_SIZE[1]))
    return sheet


def manifest_entry(level: int) -> dict:
    tier = tier_for(level)
    return {
        "label": f"Clawd Evolução: Nível {level:02d}",
        "description": f"Nivel {level}: {tier.description}; patamar {tier.name}.",
        "roles": ["original", "progressao", "nivel"],
        "source_file": f"imagegen/level_progression/levels/level_{level:03d}.png",
        "source_columns": 4,
        "source_rows": 2,
        "economy_enabled": False,
        "level": level,
        "tier": tier.name,
    }


def write_manifest() -> None:
    payload = {
        "version": 1,
        "sprites": {
            f"clawd_level_{level:03d}": manifest_entry(level)
            for level in range(1, 100)
        },
    }
    MANIFEST_PATH.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--start", type=int, default=1)
    parser.add_argument("--end", type=int, default=99)
    parser.add_argument("--sources-only", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.start <= args.end <= 99:
        parser.error("o intervalo precisa estar entre 1 e 99")

    make_level_one_checkpoint()
    missing = [
        level
        for level in (1, 10, 20, 30, 40, 50, 60, 70, 80, 90, 99)
        if not (CHECKPOINT_ROOT / f"level_{level:03d}.png").is_file()
    ]
    if missing:
        raise SystemExit(f"checkpoints ausentes: {missing}")
    LEVEL_ROOT.mkdir(parents=True, exist_ok=True)
    OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)

    for level in range(args.start, args.end + 1):
        source = LEVEL_ROOT / f"level_{level:03d}.png"
        make_sheet(level).save(source, optimize=True)
        if args.sources_only:
            print(f"nivel {level:02d}: fonte {source.name}")
            continue
        output = OUTPUT_ROOT / f"clawd_level_{level:03d}.clw"
        compile_sheet(
            source,
            output,
            columns=4,
            rows=2,
            downsample=1.0,
            frame_ms=167,
            scale=1,
            stabilize="warm-core",
            max_core_compression=0.08,
            # A trava acima compara o componente corporal, sem confundir
            # particulas quentes com pernas ou alteracoes de altura.
            validate_core_geometry=False,
        )
        sprite = parse_clw(output.read_bytes())
        print(
            f"nivel {level:02d}: {sprite.frames}f {sprite.width}x{sprite.height} "
            f"@{sprite.scale}x {output.stat().st_size / 1024:.1f}KB"
        )
    write_manifest()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
