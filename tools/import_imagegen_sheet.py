#!/usr/bin/env python3
"""Compila uma sprite sheet RGBA em um arquivo CLW animado.

A folha deve ter celulas regulares (4x2 por padrao) e fundo transparente.
Cada celula vira um frame, percorrendo linhas da esquerda para a direita.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from statistics import median
import tempfile

from PIL import Image

from build_sprite import (
    DEFAULT_BG,
    crop_frames,
    fit_scale,
    load_png_frames,
    symmetric_crop_box,
)
from clw_format import TRANSPARENT_KEY, parse_clw, write_clw
from sprite_economy import (
    CADENCES,
    DEFAULT_COLORS,
    DEFAULT_MAX_BPPF,
    MIN_FRAMES,
    PROFILES,
    optimize_clw,
)


# Tolerância conservadora para folhas genéricas; ações passivas podem pedir
# uma regra mais rígida no importador da coleção. Objetos diante do corpo, como
# o livro, reduzem a medida aparente sem representar compressão real.
MAX_CORE_COMPRESSION = 0.25


def dark_cap_anchor(image: Image.Image) -> tuple[int, int]:
    """Localiza o boné pelo maior componente escuro e largo no alto do frame."""
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    width, height = rgba.size
    candidates: set[tuple[int, int]] = set()
    for y in range(round(height * 0.78)):
        for x in range(width):
            r, g, b, alpha = pixels[x, y]
            if alpha > 150 and max(r, g, b) < 105:
                candidates.add((x, y))

    components: list[tuple[int, int, int, int, int]] = []
    while candidates:
        start = candidates.pop()
        stack = [start]
        min_x = max_x = start[0]
        min_y = max_y = start[1]
        area = 0
        while stack:
            x, y = stack.pop()
            area += 1
            min_x, max_x = min(min_x, x), max(max_x, x)
            min_y, max_y = min(min_y, y), max(max_y, y)
            for neighbor in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if neighbor in candidates:
                    candidates.remove(neighbor)
                    stack.append(neighbor)
        components.append((area, min_x, min_y, max_x, max_y))

    min_cap_width = max(12, round(width * 0.25))
    cap_components = [
        component
        for component in components
        if component[3] - component[1] + 1 >= min_cap_width
        and component[0] >= min_cap_width
    ]
    if not cap_components:
        raise ValueError("nao foi possivel detectar o bone para estabilizacao")
    # O boné é o componente largo mais alto; a área desempata pequenos ruídos.
    _area, min_x, min_y, max_x, _max_y = min(
        cap_components, key=lambda component: (component[2], -component[0])
    )
    return round((min_x + max_x) / 2), min_y


def warm_core_anchor(image: Image.Image) -> tuple[int, int]:
    """Localiza o maior nucleo coral/vermelho perto do centro do personagem."""
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    width, height = rgba.size
    candidates: set[tuple[int, int]] = set()
    for y in range(round(height * 0.82)):
        for x in range(round(width * 0.12), round(width * 0.88)):
            r, g, b, alpha = pixels[x, y]
            if alpha > 150 and r > 115 and r > g * 1.10 and r > b * 1.06:
                candidates.add((x, y))

    components: list[tuple[int, int, int, int, int]] = []
    while candidates:
        start = candidates.pop()
        stack = [start]
        min_x = max_x = start[0]
        min_y = max_y = start[1]
        area = 0
        while stack:
            x, y = stack.pop()
            area += 1
            min_x, max_x = min(min_x, x), max(max_x, x)
            min_y, max_y = min(min_y, y), max(max_y, y)
            for neighbor in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if neighbor in candidates:
                    candidates.remove(neighbor)
                    stack.append(neighbor)
        component_width = max_x - min_x + 1
        component_height = max_y - min_y + 1
        if (
            component_width >= max(8, round(width * 0.12))
            and component_height >= max(8, round(height * 0.06))
            and area >= 100
        ):
            components.append((area, min_x, min_y, max_x, max_y))

    if not components:
        raise ValueError("nao foi possivel detectar o nucleo coral para estabilizacao")
    center_x = width / 2
    _area, min_x, min_y, max_x, _max_y = min(
        components,
        key=lambda component: (
            0
            if component[1] <= center_x <= component[3]
            else abs((component[1] + component[3]) / 2 - center_x),
            -component[0],
        ),
    )
    return round((min_x + max_x) / 2), min_y


def warm_body_anchor(image: Image.Image) -> tuple[int, int]:
    """Ancora o torso central sem deixar uma pinça erguida puxar o quadro."""
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    width, height = rgba.size
    candidates: set[tuple[int, int]] = set()
    for y in range(round(height * 0.84)):
        for x in range(round(width * 0.22), round(width * 0.78)):
            r, g, b, alpha = pixels[x, y]
            if alpha > 150 and r > 115 and r > g * 1.10 and r > b * 1.06:
                candidates.add((x, y))

    components: list[tuple[int, int, int, int, int]] = []
    while candidates:
        start = candidates.pop()
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
                if point in candidates:
                    candidates.remove(point)
                    stack.append(point)
        components.append((area, min_x, min_y, max_x, max_y))
    if not components:
        raise ValueError("nao foi possivel detectar o torso coral central")
    _area, min_x, min_y, max_x, _max_y = max(components)
    return round((min_x + max_x) / 2), min_y


def resolve_stabilization(images: list[Image.Image], method: str) -> str:
    if method != "auto":
        return method
    for candidate, finder in (
        ("dark-cap", dark_cap_anchor),
        ("warm-core", warm_core_anchor),
        ("warm-body", warm_body_anchor),
    ):
        try:
            for image in images:
                finder(image)
        except ValueError:
            continue
        return candidate
    raise ValueError("nao foi possivel encontrar uma ancora visual automatica")


def central_warm_span(image: Image.Image, anchor_x: int, anchor_y: int) -> int:
    """Mede a altura corporal perto do eixo, ignorando ferramentas laterais."""
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    width, height = rgba.size
    half_band = round(width * 0.16)
    warm_rows: list[int] = []
    for y in range(max(0, anchor_y), height):
        for x in range(max(0, anchor_x - half_band), min(width, anchor_x + half_band + 1)):
            r, g, b, alpha = pixels[x, y]
            if alpha > 150 and r > 115 and r > g * 1.10 and r > b * 1.06:
                warm_rows.append(y)
                break
    if not warm_rows:
        raise ValueError("nao foi possivel medir o corpo coral")
    return max(warm_rows) - anchor_y + 1


def central_warm_baseline(image: Image.Image) -> int:
    """Localiza a linha inferior coral, usada para impedir saltos dos pes."""
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    width, height = rgba.size
    warm_rows: list[int] = []
    for y in range(height):
        for x in range(round(width * 0.12), round(width * 0.88)):
            r, g, b, alpha = pixels[x, y]
            if alpha > 150 and r > 115 and r > g * 1.10 and r > b * 1.06:
                warm_rows.append(y)
                break
    if not warm_rows:
        raise ValueError("nao foi possivel localizar a linha dos pes")
    return max(warm_rows)


def validate_stabilized_images(
    images: list[Image.Image],
    method: str,
    *,
    max_core_compression: float = MAX_CORE_COMPRESSION,
    validate_core_geometry: bool = False,
) -> None:
    """Rejeita drift vertical e compressao corporal antes de gravar o CLW."""
    if method == "none":
        return
    finders = {
        "dark-cap": dark_cap_anchor,
        "warm-core": warm_core_anchor,
        "warm-body": warm_body_anchor,
    }
    finder = finders[method]
    anchors = [finder(image) for image in images]
    anchor_y_values = [anchor[1] for anchor in anchors]
    if max(anchor_y_values) - min(anchor_y_values) > 1:
        raise ValueError(
            "estabilizacao vertical falhou nos quadros: "
            + ", ".join(str(index + 1) for index, value in enumerate(anchor_y_values) if value != round(median(anchor_y_values)))
        )

    # O boné oferece uma referência rígida por padrao. Colecoes de identidade
    # estrita podem ativar a mesma medicao para o nucleo coral, impedindo tanto
    # compressao quanto salto mesmo quando nao existe um acessorio escuro.
    if method != "dark-cap" and not validate_core_geometry:
        return
    spans = [
        central_warm_span(image, anchor_x, anchor_y)
        for image, (anchor_x, anchor_y) in zip(images, anchors)
    ]
    # O quadro mais alto funciona como referência conservadora: mesmo que a
    # maioria venha achatada, basta um quadro íntegro para denunciar os demais.
    reference = max(spans)
    compressed = [
        index + 1
        for index, span in enumerate(spans)
        if span < reference * (1 - max_core_compression)
    ]
    if compressed:
        details = ", ".join(
            f"{index} ({spans[index - 1]}px)" for index in compressed
        )
        raise ValueError(
            f"corpo comprimido nos quadros {details}; maior referencia "
            f"{reference:.0f}px. Corrija a folha ou use --allow-deformation "
            "somente se a deformacao for intencional"
        )

    if method == "warm-core" and validate_core_geometry:
        baselines = [central_warm_baseline(image) for image in images]
        if max(baselines) - min(baselines) > 2:
            reference_baseline = round(median(baselines))
            drifting = [
                index + 1
                for index, baseline in enumerate(baselines)
                if abs(baseline - reference_baseline) > 2
            ]
            raise ValueError(
                "linha dos pes instavel nos quadros "
                + ", ".join(str(index) for index in drifting)
                + f"; valores detectados: {baselines}"
            )


def stabilize_images(
    images: list[Image.Image],
    method: str,
    *,
    validate: bool = True,
    max_core_compression: float = MAX_CORE_COMPRESSION,
    validate_core_geometry: bool = False,
) -> list[Image.Image]:
    if method == "none":
        return images
    method = resolve_stabilization(images, method)
    anchor_finders = {
        "dark-cap": dark_cap_anchor,
        "warm-core": warm_core_anchor,
        "warm-body": warm_body_anchor,
    }
    if method not in anchor_finders:
        raise ValueError(f"metodo de estabilizacao desconhecido: {method}")

    anchors = [anchor_finders[method](image) for image in images]
    target_x = round(median(anchor[0] for anchor in anchors))
    target_y = round(median(anchor[1] for anchor in anchors))
    stabilized: list[Image.Image] = []
    for image, (anchor_x, anchor_y) in zip(images, anchors):
        canvas = Image.new("RGBA", image.size, (0, 0, 0, 0))
        canvas.alpha_composite(image, (target_x - anchor_x, target_y - anchor_y))
        stabilized.append(canvas)
    if validate:
        validate_stabilized_images(
            stabilized,
            method,
            max_core_compression=max_core_compression,
            validate_core_geometry=validate_core_geometry,
        )
    return stabilized


def split_sheet(
    source: Path,
    destination: Path,
    *,
    columns: int,
    rows: int,
    downsample: float,
    stabilize: str = "auto",
    validate: bool = True,
    max_core_compression: float = MAX_CORE_COMPRESSION,
    validate_core_geometry: bool = False,
) -> list[Path]:
    with Image.open(source) as opened:
        sheet = opened.convert("RGBA")
    if sheet.width % columns or sheet.height % rows:
        raise ValueError(
            f"folha {sheet.width}x{sheet.height} nao e divisivel por {columns}x{rows}"
        )
    cell_w, cell_h = sheet.width // columns, sheet.height // rows
    target_w = round(cell_w / downsample)
    target_h = round(cell_h / downsample)
    if min(target_w, target_h) <= 0:
        raise ValueError("downsample maior que a celula")

    destination.mkdir(parents=True, exist_ok=True)
    images: list[Image.Image] = []
    for row in range(rows):
        for column in range(columns):
            cell = sheet.crop(
                (
                    column * cell_w,
                    row * cell_h,
                    (column + 1) * cell_w,
                    (row + 1) * cell_h,
                )
            )
            if downsample > 1:
                cell = cell.resize((target_w, target_h), Image.Resampling.NEAREST)
            images.append(cell)

    images = stabilize_images(
        images,
        stabilize,
        validate=validate,
        max_core_compression=max_core_compression,
        validate_core_geometry=validate_core_geometry,
    )
    frames: list[Path] = []
    for index, cell in enumerate(images):
        output = destination / f"frame_{index:03d}.png"
        cell.save(output, optimize=True)
        frames.append(output)
    return frames


def compile_sheet(
    source: Path,
    output: Path,
    *,
    columns: int = 4,
    rows: int = 2,
    downsample: float = 2.0,
    frame_ms: int = 100,
    scale: int | None = None,
    stabilize: str = "auto",
    validate: bool = True,
    max_core_compression: float = MAX_CORE_COMPRESSION,
    validate_core_geometry: bool = False,
    economy_profile: str | None = None,
    economy_motion: str = "environment",
    economy_frames: int = MIN_FRAMES,
    economy_colors: int = DEFAULT_COLORS,
    max_bppf: float = DEFAULT_MAX_BPPF,
) -> None:
    with tempfile.TemporaryDirectory(prefix="clawd-sheet-") as temporary:
        frames_dir = Path(temporary)
        split_sheet(
            source,
            frames_dir,
            columns=columns,
            rows=rows,
            downsample=downsample,
            stabilize=stabilize,
            validate=validate,
            max_core_compression=max_core_compression,
            validate_core_geometry=validate_core_geometry,
        )
        frames, width, height, _files = load_png_frames(
            frames_dir, key=TRANSPARENT_KEY, background=DEFAULT_BG
        )
        box = symmetric_crop_box(frames, width, height, TRANSPARENT_KEY)
        frames = crop_frames(frames, width, box)
        _x, _y, width, height = box
        output.parent.mkdir(parents=True, exist_ok=True)
        write_clw(
            output,
            frames,
            width,
            height,
            key=TRANSPARENT_KEY,
            frame_ms=frame_ms,
            scale=scale if scale is not None else fit_scale(width, height),
        )
        if economy_profile:
            optimize_clw(
                output,
                output,
                profile_name=economy_profile,
                motion=economy_motion,
                frames=economy_frames,
                colors=economy_colors,
                max_bppf=max_bppf,
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="sprite sheet PNG com alpha")
    parser.add_argument("output", type=Path, help="arquivo CLW de saida")
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--rows", type=int, default=2)
    parser.add_argument(
        "--downsample",
        type=float,
        default=2.0,
        help="divisor de tamanho da celula; aceita decimais (padrao: 2.0)",
    )
    parser.add_argument("--frame-ms", type=int, default=100)
    parser.add_argument(
        "--scale",
        type=int,
        default=0,
        help="escala de tela fixa; 0 escolhe automaticamente",
    )
    parser.add_argument(
        "--economy-profile",
        choices=("none", *PROFILES),
        default="none",
        help="gera diretamente nos perfis eco4, eco3, footer2 ou footer3",
    )
    parser.add_argument(
        "--economy-motion",
        choices=tuple(CADENCES),
        default="environment",
        help="environment=167 ms; attention=125 ms",
    )
    parser.add_argument("--economy-frames", type=int, default=MIN_FRAMES)
    parser.add_argument("--economy-colors", type=int, default=DEFAULT_COLORS)
    parser.add_argument("--max-bppf", type=float, default=DEFAULT_MAX_BPPF)
    parser.add_argument(
        "--max-core-compression",
        type=float,
        default=MAX_CORE_COMPRESSION,
        help="compressao corporal maxima entre 0 e 1 (padrao: 0.25)",
    )
    parser.add_argument(
        "--stabilize",
        choices=("auto", "none", "dark-cap", "warm-core", "warm-body"),
        default="auto",
        help="alinha os frames por uma ancora visual (padrao: auto)",
    )
    parser.add_argument(
        "--allow-deformation",
        action="store_true",
        help="nao rejeita compressao corporal; use apenas quando intencional",
    )
    parser.add_argument(
        "--validate-core-geometry",
        action="store_true",
        help="rejeita tambem compressao e salto em sprites sem bone",
    )
    args = parser.parse_args()
    if not args.source.is_file():
        parser.error(f"arquivo nao encontrado: {args.source}")
    if min(args.columns, args.rows, args.downsample, args.frame_ms) <= 0:
        parser.error("todos os parametros numericos precisam ser positivos")
    if args.scale < 0:
        parser.error("--scale nao pode ser negativa")
    if not 0 <= args.max_core_compression < 1:
        parser.error("--max-core-compression precisa estar entre 0 e 1")

    try:
        compile_sheet(
            args.source,
            args.output,
            columns=args.columns,
            rows=args.rows,
            downsample=args.downsample,
            frame_ms=args.frame_ms,
            scale=args.scale or None,
            stabilize=args.stabilize,
            validate=not args.allow_deformation,
            max_core_compression=args.max_core_compression,
            validate_core_geometry=args.validate_core_geometry,
            economy_profile=(
                None if args.economy_profile == "none" else args.economy_profile
            ),
            economy_motion=args.economy_motion,
            economy_frames=args.economy_frames,
            economy_colors=args.economy_colors,
            max_bppf=args.max_bppf,
        )
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    sprite = parse_clw(args.output.read_bytes())
    print(
        f"Gerado: {args.output} | {sprite.frames} frames | "
        f"{sprite.width}x{sprite.height} @ {sprite.scale}x | "
        f"{args.output.stat().st_size / 1024:.1f} KB"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
