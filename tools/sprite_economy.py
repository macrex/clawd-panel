#!/usr/bin/env python3
"""Gera variantes CLW economicas, previsiveis e sem antialiasing.

O conversor trabalha sobre um CLW ja compilado para poder otimizar tanto
fontes ImageGen quanto SVG. Todos os perfis usam canvas nativo fixo, escala
inteira, 8 a 16 quadros, paleta compartilhada e um teto de bytes por pixel por
quadro. O redimensionamento e sempre nearest-neighbor.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from clw_format import (
    TRANSPARENT_KEY,
    decode_frame,
    encode_clw,
    read_clw,
    rgb_to_565,
)


MIN_FRAMES = 8
MAX_FRAMES = 16
DEFAULT_COLORS = 12
DEFAULT_MAX_BPPF = 0.30
CADENCES = {"environment": 167, "attention": 125}


@dataclass(frozen=True)
class EconomyProfile:
    name: str
    width: int
    height: int
    scale: int
    source_divisor: int | None = None


PROFILES = {
    "eco4": EconomyProfile("eco4", 110, 50, 4),
    "eco3": EconomyProfile("eco3", 146, 66, 3),
    # Fontes de rodape devem ser desenhadas com 84 ou 126 px de altura. A
    # divisao indicada cai exatamente nos 42 px nativos usados pelo firmware.
    "footer2": EconomyProfile("footer2", 110, 42, 1, source_divisor=2),
    "footer3": EconomyProfile("footer3", 146, 42, 1, source_divisor=3),
}


@dataclass(frozen=True)
class EconomyResult:
    profile: str
    frames: int
    width: int
    height: int
    scale: int
    frame_ms: int
    colors: int
    content_scale: float
    byte_size: int
    bytes_per_pixel_frame: float


def rgb_from_565(value: int) -> tuple[int, int, int]:
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def choose_frame_indices(source_frames: int, target_frames: int) -> list[int]:
    if source_frames <= 0:
        raise ValueError("sprite sem quadros")
    if not MIN_FRAMES <= target_frames <= MAX_FRAMES:
        raise ValueError(f"quadros precisam ficar entre {MIN_FRAMES} e {MAX_FRAMES}")
    # O centro de cada intervalo evita favorecer o primeiro quadro ao reduzir
    # animacoes longas e distribui repeticoes ao ampliar animacoes curtas.
    return [min(source_frames - 1, ((2 * i + 1) * source_frames) // (2 * target_frames))
            for i in range(target_frames)]


def decode_rgba(sprite, frame: int) -> Image.Image:
    rgba = []
    for value in decode_frame(sprite, frame):
        if value == sprite.key:
            rgba.append((0, 0, 0, 0))
        else:
            rgba.append((*rgb_from_565(value), 255))
    image = Image.new("RGBA", (sprite.width, sprite.height))
    image.putdata(rgba)
    return image


def union_box(images: list[Image.Image]) -> tuple[int, int, int, int]:
    boxes = [image.getchannel("A").getbbox() for image in images]
    boxes = [box for box in boxes if box]
    if not boxes:
        return (0, 0, 1, 1)
    return (
        min(box[0] for box in boxes),
        min(box[1] for box in boxes),
        max(box[2] for box in boxes),
        max(box[3] for box in boxes),
    )


def fit_to_profile(
    images: list[Image.Image],
    profile: EconomyProfile,
    *,
    padding: int = 1,
    content_scale: float = 1.0,
) -> list[Image.Image]:
    box = union_box(images)
    crop_w, crop_h = box[2] - box[0], box[3] - box[1]
    if not 0 < content_scale <= 1:
        raise ValueError("content_scale precisa ficar entre 0 e 1")
    available_w = max(1, round((profile.width - padding * 2) * content_scale))
    available_h = max(1, round((profile.height - padding * 2) * content_scale))
    ratio = min(available_w / crop_w, available_h / crop_h)
    target_w = max(1, min(available_w, round(crop_w * ratio)))
    target_h = max(1, min(available_h, round(crop_h * ratio)))
    x = (profile.width - target_w) // 2
    y = (profile.height - target_h) // 2

    fitted = []
    for image in images:
        cropped = image.crop(box)
        resized = cropped.resize((target_w, target_h), Image.Resampling.NEAREST)
        canvas = Image.new("RGBA", (profile.width, profile.height), (0, 0, 0, 0))
        canvas.alpha_composite(resized, (x, y))
        fitted.append(canvas)
    return fitted


def shared_palette(images: list[Image.Image], colors: int) -> list[int]:
    histogram: Counter[tuple[int, int, int]] = Counter()
    for image in images:
        histogram.update((r, g, b) for r, g, b, alpha in image.getdata() if alpha >= 128)
    if not histogram:
        return [0xFFFF]

    # Mantem a frequencia das cores, mas limita a amostra para a quantizacao
    # nao crescer com sprites futuros de resolucao maior.
    total = sum(histogram.values())
    budget = min(total, 250_000)
    pixels: list[tuple[int, int, int]] = []
    for color, count in histogram.items():
        copies = max(1, round(count * budget / total))
        pixels.extend([color] * copies)
    sample = Image.new("RGB", (len(pixels), 1))
    sample.putdata(pixels)
    quantized = sample.quantize(
        colors=max(1, colors),
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE,
    )
    raw = quantized.getpalette() or []
    used = sorted(index for _count, index in (quantized.getcolors() or []))
    palette565: list[int] = []
    for index in used:
        value = rgb_to_565(*raw[index * 3 : index * 3 + 3])
        if value == TRANSPARENT_KEY:
            value ^= 0x0001
        if value not in palette565:
            palette565.append(value)
    return palette565 or [0xFFFF]


def palettize(images: list[Image.Image], colors: int) -> tuple[list[list[int]], int]:
    palette = shared_palette(images, colors)
    palette_rgb = [(value, rgb_from_565(value)) for value in palette]
    cache: dict[tuple[int, int, int], int] = {}
    encoded = []
    for image in images:
        frame = []
        for r, g, b, alpha in image.getdata():
            if alpha < 128:
                frame.append(TRANSPARENT_KEY)
                continue
            source = (r, g, b)
            value = cache.get(source)
            if value is None:
                value = min(
                    palette_rgb,
                    key=lambda item: (
                        (source[0] - item[1][0]) ** 2
                        + (source[1] - item[1][1]) ** 2
                        + (source[2] - item[1][2]) ** 2
                    ),
                )[0]
                cache[source] = value
            frame.append(value)
        encoded.append(frame)
    return encoded, len(palette)


def optimize_clw(
    source: Path,
    output: Path,
    *,
    profile_name: str,
    motion: str,
    frames: int = MIN_FRAMES,
    colors: int = DEFAULT_COLORS,
    max_bppf: float = DEFAULT_MAX_BPPF,
) -> EconomyResult:
    if profile_name not in PROFILES:
        raise ValueError(f"perfil desconhecido: {profile_name}")
    if motion not in CADENCES:
        raise ValueError(f"cadencia desconhecida: {motion}")
    if colors < 2:
        raise ValueError("a paleta precisa de ao menos duas cores opacas")
    if max_bppf <= 0:
        raise ValueError("o limite B/px/qd precisa ser positivo")

    profile = PROFILES[profile_name]
    if profile.width > 220 or profile.height > 100:
        raise ValueError("perfil ultrapassa o teto nativo de 220x100")
    sprite = read_clw(source)
    indices = choose_frame_indices(sprite.frames, frames)
    images = [decode_rgba(sprite, index) for index in indices]
    frame_ms = CADENCES[motion]

    for content_scale in (1.0, 0.9, 0.8, 0.7, 0.6):
        fitted = fit_to_profile(images, profile, content_scale=content_scale)
        attempts = []
        for candidate in (colors, 10, 8, 6, 4, 3, 2):
            if candidate > colors or candidate in attempts:
                continue
            attempts.append(candidate)
            encoded, actual_colors = palettize(fitted, candidate)
            blob = encode_clw(
                encoded,
                profile.width,
                profile.height,
                key=TRANSPARENT_KEY,
                frame_ms=frame_ms,
                scale=profile.scale,
            )
            bppf = len(blob) / (frames * profile.width * profile.height)
            if bppf <= max_bppf:
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(blob)
                return EconomyResult(
                    profile=profile.name,
                    frames=frames,
                    width=profile.width,
                    height=profile.height,
                    scale=profile.scale,
                    frame_ms=frame_ms,
                    colors=actual_colors,
                    content_scale=content_scale,
                    byte_size=len(blob),
                    bytes_per_pixel_frame=bppf,
                )
    raise ValueError(
        f"nao foi possivel atingir {max_bppf:.2f} B/px/qd nem com duas cores"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--profile", choices=tuple(PROFILES), default="eco4")
    parser.add_argument("--motion", choices=tuple(CADENCES), default="environment")
    parser.add_argument("--frames", type=int, default=MIN_FRAMES)
    parser.add_argument("--colors", type=int, default=DEFAULT_COLORS)
    parser.add_argument("--max-bppf", type=float, default=DEFAULT_MAX_BPPF)
    args = parser.parse_args()
    if not args.source.is_file():
        parser.error(f"arquivo nao encontrado: {args.source}")
    try:
        result = optimize_clw(
            args.source,
            args.output,
            profile_name=args.profile,
            motion=args.motion,
            frames=args.frames,
            colors=args.colors,
            max_bppf=args.max_bppf,
        )
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(
        f"Gerado: {args.output} | {result.frames} frames | "
        f"{result.width}x{result.height} @{result.scale}x | "
        f"{result.frame_ms} ms | {result.colors} cores | "
        f"ocupacao {result.content_scale:.0%} | "
        f"{result.bytes_per_pixel_frame:.3f} B/px/qd | "
        f"{result.byte_size / 1024:.1f} KB"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
