#!/usr/bin/env python3
"""Servidor localhost do Sprite Studio.

Não requer dependências externas. A interface decodifica os próprios `.clw`
no navegador, portanto o preview valida o mesmo artefato que vai para o SD.
"""

from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import mimetypes
from pathlib import Path
import re
import sys
from threading import Lock
import webbrowser
from urllib.parse import quote, unquote, urlparse


HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
ROOT = HERE.parents[1]
WEB = HERE / "web"
DEFAULT_ASSETS = ROOT / "sdcard" / "clawd"
DEFAULT_MANIFEST = ROOT / "sprites" / "manifest.json"
DEFAULT_SOURCES = ROOT / "sprites" / "sources"
sys.path.insert(0, str(TOOLS))

from clw_format import ClwError, body_anchor, read_clw  # noqa: E402


SAFE_NAME = re.compile(r"^[a-zA-Z0-9_-]+$")
SOURCE_SUFFIXES = {".png", ".webp", ".jpg", ".jpeg", ".gif", ".svg"}
ECONOMY_VARIANTS = {
    "_eco4": ("eco4", "Eco 4×"),
    "_eco3": ("eco3", "Eco 3×"),
    "_footer2": ("footer2", "Rodapé ÷2"),
    "_footer3": ("footer3", "Rodapé ÷3"),
}
ECONOMY_SPECS = {
    "eco4": (110, 50, 4),
    "eco3": (146, 66, 3),
    "footer2": (110, 42, 1),
    "footer3": (146, 42, 1),
}


def load_manifest(path: Path) -> tuple[dict, dict]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {}, {}
    entries = dict(data.get("sprites", {}))
    manifest_root = path.parent.resolve()
    for relative in data.get("includes", []):
        included = (manifest_root / relative).resolve()
        try:
            included.relative_to(manifest_root)
        except ValueError:
            continue
        try:
            payload = json.loads(included.read_text(encoding="utf-8"))
        except (FileNotFoundError, json.JSONDecodeError, OSError):
            continue
        entries.update(payload.get("sprites", {}))
    return data.get("defaults", {}), entries


def safe_source_path(source_dir: Path, relative: str) -> Path | None:
    if not relative or not isinstance(relative, str):
        return None
    source_root = source_dir.resolve()
    target = (source_root / Path(relative)).resolve()
    try:
        target.relative_to(source_root)
    except ValueError:
        return None
    if target.suffix.lower() not in SOURCE_SUFFIXES or not target.is_file():
        return None
    return target


def build_catalog(
    asset_dir: Path, manifest_path: Path, source_dir: Path | None = None
) -> list[dict]:
    defaults, entries = load_manifest(manifest_path)
    source_dir = (source_dir or manifest_path.parent / "sources").resolve()
    catalog = []
    for path in sorted(asset_dir.glob("*.clw")):
        try:
            sprite = read_clw(path)
            anchor_x, anchor_base = body_anchor(sprite)
        except (ClwError, OSError) as exc:
            catalog.append(
                {"name": path.stem, "valid": False, "error": str(exc), "file_bytes": path.stat().st_size}
            )
            continue

        variant_of = None
        economy_profile = None
        variant_label = None
        for suffix, (profile, label) in ECONOMY_VARIANTS.items():
            if path.stem.endswith(suffix):
                variant_of = path.stem.removesuffix(suffix)
                economy_profile = profile
                variant_label = label
                break
        base_behavior = entries.get(variant_of or path.stem, {})
        behavior = {**defaults, **base_behavior, **entries.get(path.stem, {})}
        if economy_profile:
            roles = list(dict.fromkeys([*behavior.get("roles", []), "economico"]))
            behavior.update(
                {
                    "label": f'{behavior.get("label", variant_of)} · {variant_label}',
                    "description": (
                        f"Versão econômica {variant_label} de "
                        f'{behavior.get("label", variant_of)}.'
                    ),
                    "roles": roles,
                    "used": False,
                }
            )
        source_file = behavior.get("source_file")
        source_path = safe_source_path(source_dir, source_file) if source_file else None
        imagegen_sheet = bool(
            source_path
            and source_path.suffix.lower() == ".png"
            and Path(source_file).parts
            and Path(source_file).parts[0] == "imagegen"
        )
        screen_pixels = sprite.screen_width * sprite.screen_height
        file_bytes = path.stat().st_size
        bytes_per_pixel_frame = file_bytes / (
            sprite.frames * sprite.width * sprite.height
        )
        economy_compliant = bool(
            economy_profile
            and (sprite.width, sprite.height, sprite.scale)
            == ECONOMY_SPECS[economy_profile]
            and 8 <= sprite.frames <= 16
            and sprite.width <= 220
            and sprite.height <= 100
            and sprite.frame_ms in {125, 167}
            and bytes_per_pixel_frame <= 0.30
        )
        catalog.append(
            {
                "name": path.stem,
                "valid": True,
                "label": behavior.get("label", path.stem.replace("_", " ").title()),
                "description": behavior.get("description", "Sprite disponível no cartão SD."),
                "roles": behavior.get("roles", ["biblioteca"]),
                "used": bool(behavior.get("used", False)),
                "playback": behavior.get("playback", "loop"),
                "source_playback": behavior.get("source_playback"),
                "frames": sprite.frames,
                "width": sprite.width,
                "height": sprite.height,
                "scale": sprite.scale,
                "screen_width": sprite.screen_width,
                "screen_height": sprite.screen_height,
                "frame_ms": sprite.frame_ms,
                "fps": round(sprite.fps, 2),
                "duration_ms": sprite.duration_ms,
                "file_bytes": file_bytes,
                "raw_bytes": sprite.raw_bytes,
                "compression": round(sprite.compression_ratio, 2),
                "bytes_per_pixel_frame": round(bytes_per_pixel_frame, 3),
                "variant_of": variant_of,
                "economy_profile": economy_profile,
                "economy_compliant": economy_compliant,
                "buffer_bytes": screen_pixels * 2,
                "page_cpu_pct": round(min(100.0, 64_000 / sprite.frame_ms / 10), 1),
                "anchor_x": anchor_x,
                "anchor_base": anchor_base,
                "asset_url": f"/clawd/{path.name}",
                "source_file": source_file if source_path else None,
                "source_url": (
                    "/sources/" + quote(Path(source_file).as_posix(), safe="/")
                    if source_path
                    else None
                ),
                "source_columns": behavior.get(
                    "source_columns", 4 if imagegen_sheet else None
                ),
                "source_rows": behavior.get("source_rows", 2 if imagegen_sheet else None),
                "level": behavior.get("level"),
                "tier": behavior.get("tier"),
                "level_min": behavior.get("level_min"),
                "level_max": behavior.get("level_max"),
            }
        )
    return catalog


class StudioHandler(BaseHTTPRequestHandler):
    server_version = "ClawdSpriteStudio/1.0"

    @property
    def studio(self) -> "StudioServer":
        return self.server  # type: ignore[return-value]

    def _send_bytes(self, body: bytes, content_type: str, status: int = 200) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, value: object, status: int = 200) -> None:
        body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self._send_bytes(body, "application/json; charset=utf-8", status)

    def _send_file(self, path: Path) -> None:
        if not path.is_file():
            self._send_json({"error": "nao encontrado"}, 404)
            return
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        if path.suffix == ".js":
            content_type = "text/javascript; charset=utf-8"
        elif path.suffix in {".html", ".css"}:
            content_type += "; charset=utf-8"
        self._send_bytes(path.read_bytes(), content_type)

    def do_GET(self) -> None:  # noqa: N802
        route = unquote(urlparse(self.path).path)
        if route == "/api/sprites":
            self._send_json(
                {
                    "sprites": self.studio.catalog(),
                    "asset_dir": str(self.studio.asset_dir),
                    "source_dir": str(self.studio.source_dir),
                }
            )
            return
        if route == "/api/health":
            self._send_json({"ok": True})
            return
        if route.startswith("/clawd/"):
            name = Path(route).name
            stem = Path(name).stem
            if not SAFE_NAME.fullmatch(stem) or Path(name).suffix != ".clw":
                self._send_json({"error": "nome de asset invalido"}, 400)
                return
            self._send_file(self.studio.asset_dir / name)
            return
        if route.startswith("/sources/"):
            relative = route.removeprefix("/sources/")
            source = safe_source_path(self.studio.source_dir, relative)
            if source is None:
                self._send_json({"error": "fonte invalida ou nao encontrada"}, 404)
                return
            self._send_file(source)
            return
        if route == "/":
            self._send_file(WEB / "index.html")
            return
        if route.startswith("/static/"):
            name = Path(route).name
            if name not in {"app.js", "styles.css"}:
                self._send_json({"error": "nao encontrado"}, 404)
                return
            self._send_file(WEB / name)
            return
        self._send_json({"error": "nao encontrado"}, 404)

    def log_message(self, fmt: str, *args: object) -> None:
        if self.studio.verbose:
            super().log_message(fmt, *args)


class StudioServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(
        self,
        address: tuple[str, int],
        *,
        asset_dir: Path,
        manifest_path: Path,
        source_dir: Path = DEFAULT_SOURCES,
        verbose: bool = False,
    ) -> None:
        self.asset_dir = asset_dir.resolve()
        self.manifest_path = manifest_path.resolve()
        self.source_dir = source_dir.resolve()
        self.verbose = verbose
        self._catalog_cache: list[dict] | None = None
        self._catalog_signature: tuple[tuple[str, int, int], ...] | None = None
        self._catalog_lock = Lock()
        super().__init__(address, StudioHandler)

    def catalog_signature(self) -> tuple[tuple[str, int, int], ...]:
        paths = [*self.asset_dir.glob("*.clw"), self.manifest_path]
        paths.extend(self.manifest_path.parent.glob("*_manifest.json"))
        return tuple(
            sorted(
                (str(path), path.stat().st_mtime_ns, path.stat().st_size)
                for path in paths
                if path.is_file()
            )
        )

    def catalog(self) -> list[dict]:
        signature = self.catalog_signature()
        if self._catalog_cache is not None and signature == self._catalog_signature:
            return self._catalog_cache
        with self._catalog_lock:
            signature = self.catalog_signature()
            if self._catalog_cache is None or signature != self._catalog_signature:
                self._catalog_cache = build_catalog(
                    self.asset_dir,
                    self.manifest_path,
                    self.source_dir,
                )
                self._catalog_signature = signature
            return self._catalog_cache


def main() -> int:
    parser = argparse.ArgumentParser(description="Galeria localhost de sprites .clw")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--assets", type=Path, default=DEFAULT_ASSETS)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--sources", type=Path, default=DEFAULT_SOURCES)
    parser.add_argument("--open", action="store_true", help="abre o navegador padrao")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    if not args.assets.is_dir():
        parser.error(f"pasta de sprites nao encontrada: {args.assets}")
    server = StudioServer(
        (args.host, args.port),
        asset_dir=args.assets,
        manifest_path=args.manifest,
        source_dir=args.sources,
        verbose=args.verbose,
    )
    url = f"http://{args.host}:{server.server_port}"
    print(f"Sprite Studio em {url}")
    print(f"Assets: {server.asset_dir}")
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
