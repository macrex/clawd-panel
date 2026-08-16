#!/usr/bin/env python3
"""Leitura, validação e escrita do formato de sprite CLWD v1.

O módulo é deliberadamente stdlib-only para ser compartilhado pelo compilador,
pelos testes e pelo Sprite Studio. Pixels são RGB565 e cada frame é armazenado
como corridas ``(valor, repeticoes)``.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
from typing import Iterable, Sequence


MAGIC = b"CLWD"
VERSION = 1
HEADER_SIZE = 20
TRANSPARENT_KEY = 0x18C5


class ClwError(ValueError):
    """Arquivo CLW inválido ou dados incompatíveis com o formato."""


@dataclass(frozen=True)
class ClwSprite:
    frames: int
    width: int
    height: int
    key: int
    frame_ms: int
    scale: int
    offsets: tuple[int, ...]
    runs: tuple[tuple[int, int], ...]
    byte_size: int = 0

    @property
    def screen_width(self) -> int:
        return self.width * self.scale

    @property
    def screen_height(self) -> int:
        return self.height * self.scale

    @property
    def duration_ms(self) -> int:
        return self.frames * self.frame_ms

    @property
    def fps(self) -> float:
        return 1000.0 / self.frame_ms if self.frame_ms else 0.0

    @property
    def raw_bytes(self) -> int:
        return self.frames * self.width * self.height * 2

    @property
    def compression_ratio(self) -> float:
        return self.raw_bytes / self.byte_size if self.byte_size else 0.0


def _validate(sprite: ClwSprite) -> None:
    if min(sprite.frames, sprite.width, sprite.height, sprite.frame_ms, sprite.scale) <= 0:
        raise ClwError("cabecalho contem zero em campo obrigatorio")
    if len(sprite.offsets) != sprite.frames + 1:
        raise ClwError("tabela de offsets nao tem frames+1 entradas")
    if sprite.offsets[0] != 0:
        raise ClwError("primeiro offset precisa ser zero")
    if sprite.offsets[-1] != len(sprite.runs):
        raise ClwError("ultimo offset nao coincide com a quantidade de corridas")

    expected = sprite.width * sprite.height
    previous = 0
    for frame in range(sprite.frames):
        first, last = sprite.offsets[frame : frame + 2]
        if first < previous or last < first or last > len(sprite.runs):
            raise ClwError(f"offset invalido no frame {frame}")
        total = 0
        for value, count in sprite.runs[first:last]:
            if not 0 <= value <= 0xFFFF or not 1 <= count <= 0xFFFF:
                raise ClwError(f"corrida invalida no frame {frame}")
            total += count
        if total != expected:
            raise ClwError(
                f"frame {frame} cobre {total} pixels; esperado {expected}"
            )
        previous = last


def parse_clw(data: bytes) -> ClwSprite:
    if len(data) < HEADER_SIZE:
        raise ClwError("arquivo menor que o cabecalho")
    if data[:4] != MAGIC:
        raise ClwError("assinatura CLWD ausente")

    version, frames, width, height, key, frame_ms, scale, _reserved = (
        struct.unpack_from("<8H", data, 4)
    )
    if version != VERSION:
        raise ClwError(f"versao {version} nao suportada")

    table_size = (frames + 1) * 4
    if len(data) < HEADER_SIZE + table_size:
        raise ClwError("arquivo truncado na tabela de offsets")
    offsets = struct.unpack_from(f"<{frames + 1}I", data, HEADER_SIZE)
    run_count = offsets[-1] if offsets else 0
    expected_size = HEADER_SIZE + table_size + run_count * 4
    if len(data) != expected_size:
        raise ClwError(
            f"tamanho {len(data)} bytes; cabecalho anuncia {expected_size}"
        )

    flat = struct.unpack_from(f"<{run_count * 2}H", data, HEADER_SIZE + table_size)
    runs = tuple(zip(flat[0::2], flat[1::2]))
    sprite = ClwSprite(
        frames=frames,
        width=width,
        height=height,
        key=key,
        frame_ms=frame_ms,
        scale=scale,
        offsets=tuple(offsets),
        runs=runs,
        byte_size=len(data),
    )
    _validate(sprite)
    return sprite


def read_clw(path: str | Path) -> ClwSprite:
    return parse_clw(Path(path).read_bytes())


def decode_frame(sprite: ClwSprite, frame: int) -> list[int]:
    if frame < 0 or frame >= sprite.frames:
        raise ClwError(f"frame {frame} fora da faixa")
    pixels: list[int] = []
    first, last = sprite.offsets[frame : frame + 2]
    for value, count in sprite.runs[first:last]:
        pixels.extend([value] * count)
    return pixels


def body_anchor(sprite: ClwSprite) -> tuple[int, int]:
    """Replica a âncora do firmware: pixels opacos em >=60% dos frames."""
    counts = bytearray(sprite.width * sprite.height)
    for frame in range(sprite.frames):
        for index, value in enumerate(decode_frame(sprite, frame)):
            if value != sprite.key and counts[index] < 255:
                counts[index] += 1

    threshold = (sprite.frames * 3 + 4) // 5
    x0, x1, y1 = sprite.width, -1, -1
    for index, count in enumerate(counts):
        if count < threshold:
            continue
        x, y = index % sprite.width, index // sprite.width
        x0 = min(x0, x)
        x1 = max(x1, x)
        y1 = max(y1, y)
    if x1 < 0:
        return sprite.width // 2, sprite.height
    return (x0 + x1 + 1) // 2, y1 + 1


def rle_encode(pixels: Sequence[int]) -> list[tuple[int, int]]:
    if not pixels:
        return []
    runs: list[tuple[int, int]] = []
    value, count = pixels[0], 1
    for pixel in pixels[1:]:
        if pixel == value and count < 0xFFFF:
            count += 1
        else:
            runs.append((value, count))
            value, count = pixel, 1
    runs.append((value, count))
    return runs


def encode_clw(
    frames: Sequence[Sequence[int]],
    width: int,
    height: int,
    *,
    key: int = TRANSPARENT_KEY,
    frame_ms: int = 125,
    scale: int = 1,
) -> bytes:
    if not frames:
        raise ClwError("ao menos um frame e obrigatorio")
    expected = width * height
    if expected <= 0:
        raise ClwError("dimensoes invalidas")

    offsets = [0]
    all_runs: list[tuple[int, int]] = []
    for index, pixels in enumerate(frames):
        if len(pixels) != expected:
            raise ClwError(
                f"frame {index} tem {len(pixels)} pixels; esperado {expected}"
            )
        all_runs.extend(rle_encode(pixels))
        offsets.append(len(all_runs))

    blob = bytearray(MAGIC)
    blob += struct.pack(
        "<8H", VERSION, len(frames), width, height, key, frame_ms, scale, 0
    )
    blob += struct.pack(f"<{len(offsets)}I", *offsets)
    for value, count in all_runs:
        blob += struct.pack("<HH", value, count)
    return bytes(blob)


def write_clw(
    path: str | Path,
    frames: Sequence[Sequence[int]],
    width: int,
    height: int,
    **kwargs: int,
) -> int:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    data = encode_clw(frames, width, height, **kwargs)
    output.write_bytes(data)
    return len(data)


def rgb_to_565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

