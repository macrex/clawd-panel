#!/usr/bin/env python3
"""Renderiza SVGs animados em PNGs pixel-perfect usando o Edge/Chromium.

O renderizador controla o tempo das animacoes CSS e SMIL diretamente. Depois
da captura, cores opacas sao encaixadas na paleta declarada pelo proprio SVG,
removendo tons de anti-alias sem destruir brilhos semitransparentes.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path
import re
import sys


EDGE_CANDIDATES = (
    Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
    Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"),
)


def source_palette(svg_text: str) -> list[tuple[int, int, int]]:
    """Extrai as cores hexadecimais autorais, preservando a identidade visual."""
    colors: list[tuple[int, int, int]] = []
    for value in re.findall(r"#([0-9a-fA-F]{6})(?![0-9a-fA-F])", svg_text):
        color = tuple(int(value[i : i + 2], 16) for i in (0, 2, 4))
        if color not in colors:
            colors.append(color)  # type: ignore[arg-type]
    return colors


def parse_viewbox(svg_text: str) -> tuple[float, float]:
    match = re.search(r'viewBox\s*=\s*["\']([^"\']+)["\']', svg_text)
    if not match:
        raise ValueError("SVG sem viewBox")
    parts = match.group(1).replace(",", " ").split()
    if len(parts) != 4:
        raise ValueError("viewBox invalido")
    return float(parts[2]), float(parts[3])


def build_html(svg_text: str, scale: float) -> tuple[str, int, int]:
    view_w, view_h = parse_viewbox(svg_text)
    width = math.ceil(view_w * scale)
    height = math.ceil(view_h * scale)
    svg_text = re.sub(
        r'(<svg[^>]*?)\s+(?:width|height)\s*=\s*["\'][^"\']*["\']',
        r"\1",
        svg_text,
        count=2,
    )
    html = f"""<!doctype html>
<html><head><meta charset="utf-8"><style>
* {{ box-sizing: border-box; margin: 0; padding: 0; }}
html, body, #stage {{ width: {width}px; height: {height}px; overflow: hidden; background: transparent; }}
#stage svg {{ width: {width}px; height: {height}px; display: block; shape-rendering: crispEdges; }}
#stage svg * {{ shape-rendering: crispEdges; }}
</style></head><body><div id="stage">{svg_text}</div><script>
const root = document.querySelector('svg');
function pauseEverything() {{
  document.getAnimations().forEach(animation => animation.pause());
  if (root && root.pauseAnimations) root.pauseAnimations();
}}
window.seekSprite = (milliseconds) => {{
  document.getAnimations().forEach(animation => {{
    animation.pause();
    animation.currentTime = milliseconds;
  }});
  if (root && root.setCurrentTime) root.setCurrentTime(milliseconds / 1000);
}};
requestAnimationFrame(() => requestAnimationFrame(pauseEverything));
</script></body></html>"""
    return html, width, height


def snap_to_source_palette(
    frame_path: Path, palette: list[tuple[int, int, int]]
) -> None:
    """Elimina cores espurias do rasterizador mantendo alpha intencional."""
    from PIL import Image
    import numpy as np

    image = Image.open(frame_path).convert("RGBA")
    data = np.asarray(image).copy()
    alpha = data[:, :, 3]
    noise = alpha <= 16
    solid = alpha > 200
    alpha[noise] = 0
    alpha[solid] = 255

    if palette and np.any(solid):
        pixels = data[:, :, :3][solid].astype(np.int16)
        colors = np.asarray(palette, dtype=np.int16)
        # Processar em blocos evita uma matriz temporaria grande em SVGs futuros.
        snapped = np.empty_like(pixels, dtype=np.uint8)
        for start in range(0, len(pixels), 65536):
            block = pixels[start : start + 65536]
            distances = np.sum(
                (block[:, None, :] - colors[None, :, :]) ** 2,
                axis=2,
                dtype=np.int32,
            )
            snapped[start : start + len(block)] = colors[
                np.argmin(distances, axis=1)
            ]
        data[:, :, :3][solid] = snapped

    data[:, :, 3] = alpha
    Image.fromarray(data, "RGBA").save(frame_path, optimize=True)


def find_edge() -> Path | None:
    return next((path for path in EDGE_CANDIDATES if path.is_file()), None)


def render_svg(
    source: Path,
    destination: Path,
    *,
    frame_ms: int,
    duration: float,
    scale: float = 4.0,
) -> list[Path]:
    try:
        from playwright.sync_api import sync_playwright
    except ImportError as exc:
        raise RuntimeError(
            "Playwright ausente; instale com: python -m pip install playwright"
        ) from exc

    svg_text = source.read_text(encoding="utf-8")
    palette = source_palette(svg_text)
    html, width, height = build_html(svg_text, scale)
    frame_count = max(1, round(duration * 1000 / frame_ms))
    destination.mkdir(parents=True, exist_ok=True)
    wrapper = destination / "_render.html"
    wrapper.write_text(html, encoding="utf-8")

    edge = find_edge()
    launch = {"headless": True}
    if edge:
        launch["executable_path"] = str(edge)

    outputs: list[Path] = []
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(**launch)
            context = browser.new_context(
                viewport={"width": width, "height": height},
                device_scale_factor=1,
            )
            page = context.new_page()
            page.goto(wrapper.resolve().as_uri())
            page.wait_for_function("typeof window.seekSprite === 'function'")
            page.evaluate(
                "() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))"
            )
            stage = page.locator("#stage")
            for index in range(frame_count):
                page.evaluate("ms => window.seekSprite(ms)", index * frame_ms)
                page.evaluate(
                    "() => new Promise(resolve => requestAnimationFrame(resolve))"
                )
                output = destination / f"frame_{index:03d}.png"
                stage.screenshot(path=str(output), omit_background=True)
                snap_to_source_palette(output, palette)
                outputs.append(output)
            context.close()
            browser.close()
    finally:
        wrapper.unlink(missing_ok=True)

    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--frame-ms", type=int, default=100)
    parser.add_argument("--duration", type=float, required=True)
    parser.add_argument("--scale", type=float, default=4.0)
    args = parser.parse_args()
    if not args.source.is_file():
        parser.error(f"arquivo nao encontrado: {args.source}")
    if args.frame_ms <= 0 or args.duration <= 0 or args.scale <= 0:
        parser.error("frame-ms, duration e scale precisam ser positivos")
    try:
        outputs = render_svg(
            args.source,
            args.destination,
            frame_ms=args.frame_ms,
            duration=args.duration,
            scale=args.scale,
        )
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"erro: {exc}", file=sys.stderr)
        return 1
    print(
        f"Renderizado: {args.source.name} -> {len(outputs)} frames em {args.destination}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
