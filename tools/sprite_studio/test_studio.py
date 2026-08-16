from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from urllib.request import urlopen
from urllib.error import HTTPError


HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(HERE))

from clw_format import (  # noqa: E402
    TRANSPARENT_KEY,
    body_anchor,
    decode_frame,
    encode_clw,
    parse_clw,
)
from import_imagegen_sheet import (  # noqa: E402
    dark_cap_anchor,
    split_sheet,
    stabilize_images,
    warm_body_anchor,
    warm_core_anchor,
)
from build_level_progression import coral_core_box  # noqa: E402
from sprite_economy import PROFILES, optimize_clw  # noqa: E402
from server import DEFAULT_ASSETS, DEFAULT_MANIFEST, StudioServer, build_catalog  # noqa: E402


class ClwFormatTests(unittest.TestCase):
    def test_round_trip_preserva_frames_e_metadados(self):
        key = TRANSPARENT_KEY
        frames = [
            [key, 0xF800, 0xF800, key, key, 0x07E0],
            [key, 0x001F, 0x001F, key, key, 0x07E0],
        ]
        sprite = parse_clw(encode_clw(frames, 3, 2, frame_ms=100, scale=3))
        self.assertEqual(sprite.frames, 2)
        self.assertEqual((sprite.width, sprite.height), (3, 2))
        self.assertEqual(sprite.frame_ms, 100)
        self.assertEqual(sprite.scale, 3)
        self.assertEqual(decode_frame(sprite, 0), frames[0])
        self.assertEqual(decode_frame(sprite, 1), frames[1])

    def test_ancora_ignora_pixel_transitorio(self):
        key = TRANSPARENT_KEY
        base = [key] * 16
        base[13:15] = [0xF800, 0xF800]
        frames = [base[:] for _ in range(5)]
        frames[-1][0] = 0xFFFF
        sprite = parse_clw(encode_clw(frames, 4, 4))
        self.assertEqual(body_anchor(sprite), (2, 4))

    def test_acervo_real_inteiro_e_valido(self):
        files = sorted(DEFAULT_ASSETS.glob("*.clw"))
        self.assertGreaterEqual(len(files), 45)
        for path in files:
            sprite = parse_clw(path.read_bytes())
            self.assertGreater(sprite.frames, 0, path.name)
            self.assertEqual(len(decode_frame(sprite, 0)), sprite.width * sprite.height)

    def test_folhas_imagegen_ficam_sem_salto_vertical(self):
        from PIL import Image

        source_root = ROOT / "sprites" / "sources" / "imagegen"
        cases = (
            (source_root / "clawd_marks.png", 4, 2, 2.0, "dark-cap", dark_cap_anchor),
            (source_root / "solar_mind.png", 4, 2, 2.3, "warm-core", warm_core_anchor),
            (
                source_root / "marks_actions" / "victory.png",
                4,
                2,
                2.1,
                "dark-cap",
                dark_cap_anchor,
            ),
            (
                source_root / "marks_actions" / "rest.png",
                4,
                3,
                2.0,
                "dark-cap",
                dark_cap_anchor,
            ),
        )
        for source, columns, rows, downsample, method, anchor_finder in cases:
            with self.subTest(source=source.name), tempfile.TemporaryDirectory() as temp:
                paths = split_sheet(
                    source,
                    Path(temp),
                    columns=columns,
                    rows=rows,
                    downsample=downsample,
                    stabilize=method,
                    max_core_compression=0.10 if source.name == "rest.png" else 0.25,
                )
                anchors = []
                for path in paths:
                    with Image.open(path) as frame:
                        anchors.append(anchor_finder(frame))
                self.assertEqual(len({anchor[1] for anchor in anchors}), 1)
                self.assertLessEqual(
                    max(anchor[0] for anchor in anchors)
                    - min(anchor[0] for anchor in anchors),
                    4,
                )

    def test_compilador_rejeita_corpo_espremido(self):
        from PIL import Image, ImageDraw

        frames = []
        for index in range(8):
            frame = Image.new("RGBA", (100, 100), (0, 0, 0, 0))
            draw = ImageDraw.Draw(frame)
            draw.rectangle((25, 10, 74, 24), fill=(35, 38, 42, 255))
            bottom = 50 if index == 6 else 85
            draw.rectangle((30, 30, 70, bottom), fill=(225, 100, 75, 255))
            frames.append(frame)
        with self.assertRaisesRegex(ValueError, "corpo comprimido.*7"):
            stabilize_images(frames, "auto")

    def test_perfis_economicos_respeitam_orcamento(self):
        assets = ROOT / "sdcard" / "clawd"
        variants = sorted(assets.glob("*_eco[34].clw"))
        self.assertGreater(len(variants), 0)
        variant_names = {path.stem for path in variants}
        for path in variants:
            base = path.stem.removesuffix("_eco3").removesuffix("_eco4")
            self.assertIn(f"{base}_eco3", variant_names)
            self.assertIn(f"{base}_eco4", variant_names)
        for path in variants:
            with self.subTest(sprite=path.name):
                sprite = parse_clw(path.read_bytes())
                profile_name = "eco4" if path.stem.endswith("_eco4") else "eco3"
                profile = PROFILES[profile_name]
                self.assertEqual(
                    (sprite.width, sprite.height), (profile.width, profile.height)
                )
                self.assertEqual(sprite.scale, profile.scale)
                self.assertGreaterEqual(sprite.frames, 8)
                self.assertLessEqual(sprite.frames, 16)
                self.assertIn(sprite.frame_ms, (125, 167))
                bppf = path.stat().st_size / (
                    sprite.frames * sprite.width * sprite.height
                )
                self.assertLessEqual(bppf, 0.30)
                colors = {
                    value
                    for frame in range(sprite.frames)
                    for value in decode_frame(sprite, frame)
                    if value != sprite.key
                }
                self.assertLessEqual(len(colors), 12)

        with tempfile.TemporaryDirectory() as temp:
            result = optimize_clw(
                assets / "clawd_marks.clw",
                Path(temp) / "footer.clw",
                profile_name="footer2",
                motion="environment",
            )
            self.assertEqual((result.width, result.height), (110, 42))
            self.assertEqual(result.scale, 1)
            self.assertLessEqual(result.bytes_per_pixel_frame, 0.30)

    def test_colecao_fawkes_tem_seis_animacoes_estaveis(self):
        from PIL import Image

        source_root = ROOT / "sprites" / "sources" / "imagegen" / "fawkes"
        sources = sorted(source_root.glob("*.png"))
        sources = [path for path in sources if ".chroma." not in path.name]
        self.assertEqual(len(sources), 6)
        for source in sources:
            with self.subTest(source=source.name), tempfile.TemporaryDirectory() as temp:
                paths = split_sheet(
                    source,
                    Path(temp),
                    columns=4,
                    rows=2,
                    downsample=2.0,
                    stabilize="dark-cap",
                    max_core_compression=0.15,
                )
                anchors = []
                for path in paths:
                    with Image.open(path) as frame:
                        anchors.append(dark_cap_anchor(frame))
                self.assertEqual(len(paths), 8)
                self.assertEqual(len({anchor[1] for anchor in anchors}), 1)

    def test_acoes_originais_tem_tres_animacoes_sem_eco(self):
        from PIL import Image

        source_root = ROOT / "sprites" / "sources" / "imagegen" / "original_actions"
        sources = sorted(source_root.glob("*.png"))
        self.assertEqual(len(sources), 3)
        assets = ROOT / "sdcard" / "clawd"
        for source in sources:
            with self.subTest(source=source.name), tempfile.TemporaryDirectory() as temp:
                paths = split_sheet(
                    source,
                    Path(temp),
                    columns=4,
                    rows=2,
                    downsample=2.0,
                    stabilize="warm-core",
                )
                anchors = []
                for path in paths:
                    with Image.open(path) as frame:
                        anchors.append(warm_core_anchor(frame))
                self.assertEqual(len(paths), 8)
                self.assertEqual(len({anchor[1] for anchor in anchors}), 1)
            stem = f"clawd_{source.stem}"
            self.assertTrue((assets / f"{stem}.clw").is_file())
            self.assertFalse((assets / f"{stem}_eco3.clw").exists())
            self.assertFalse((assets / f"{stem}_eco4.clw").exists())

    def test_estados_climaticos_tem_seis_animacoes_originais(self):
        from PIL import Image

        source_root = ROOT / "sprites" / "sources" / "imagegen" / "climate"
        assets = ROOT / "sdcard" / "clawd"
        sources = sorted(source_root.glob("*.png"))
        self.assertEqual(len(sources), 6)
        for source in sources:
            with self.subTest(source=source.name):
                sprite = parse_clw((assets / f"clawd_{source.stem}.clw").read_bytes())
                self.assertEqual(sprite.frames, 8)
                self.assertEqual(sprite.scale, 1)
                self.assertLessEqual(sprite.screen_width, 240)
                self.assertLessEqual(sprite.screen_height, 320)
                self.assertFalse((assets / f"clawd_{source.stem}_eco3.clw").exists())
                self.assertFalse((assets / f"clawd_{source.stem}_eco4.clw").exists())
        with tempfile.TemporaryDirectory() as temp:
            frames = split_sheet(
                source_root / "swim_briefs.png",
                Path(temp),
                columns=4,
                rows=2,
                downsample=1.5,
                stabilize="warm-body",
                max_core_compression=0.12,
                validate_core_geometry=True,
            )
            anchors = []
            for frame in frames:
                with Image.open(frame) as opened:
                    anchors.append(warm_body_anchor(opened))
            self.assertEqual(len({anchor[1] for anchor in anchors}), 1)

    def test_progressao_tem_99_niveis_sem_salto_ou_compressao(self):
        from PIL import Image

        source_root = ROOT / "sprites" / "sources" / "imagegen" / "level_progression" / "levels"
        assets = ROOT / "sdcard" / "clawd"
        sources = sorted(source_root.glob("level_*.png"))
        binaries = sorted(assets.glob("clawd_level_*.clw"))
        self.assertEqual(len(sources), 99)
        self.assertEqual(len(binaries), 99)
        samples = (1, 9, 10, 19, 20, 29, 30, 39, 40, 49, 50, 59, 60, 69, 70, 79, 80, 89, 90, 98, 99)
        for level in samples:
            with self.subTest(level=level):
                source = source_root / f"level_{level:03d}.png"
                with Image.open(source) as opened:
                    sheet = opened.convert("RGBA")
                self.assertEqual(sheet.size, (960, 384))
                boxes = []
                for index in range(8):
                    left = (index % 4) * 240
                    top = (index // 4) * 192
                    boxes.append(coral_core_box(sheet.crop((left, top, left + 240, top + 192))))
                self.assertEqual(len(set(boxes)), 1)
                sprite = parse_clw((assets / f"clawd_level_{level:03d}.clw").read_bytes())
                self.assertEqual(sprite.frames, 8)
                self.assertEqual(sprite.frame_ms, 167)
                self.assertEqual(sprite.scale, 1)
                self.assertLessEqual(sprite.screen_width, 240)
                self.assertLessEqual(sprite.screen_height, 192)
                self.assertFalse((assets / f"clawd_level_{level:03d}_eco3.clw").exists())
                self.assertFalse((assets / f"clawd_level_{level:03d}_eco4.clw").exists())

    def test_fundos_animados_preenchem_a_tela_e_sao_opacos(self):
        source_root = ROOT / "sprites" / "sources" / "imagegen" / "backgrounds"
        assets = ROOT / "sdcard" / "clawd"
        names = (
            "background_cyber_rain",
            "background_sunset_beach",
            "background_hacker_workshop",
            "background_aurora_snow",
            "background_volcanic_forge",
            "background_lunar_outpost",
        )
        for name in names:
            with self.subTest(background=name):
                sprite = parse_clw((assets / f"{name}.clw").read_bytes())
                self.assertEqual(sprite.frames, 8)
                self.assertEqual(sprite.frame_ms, 250)
                self.assertEqual((sprite.width, sprite.height, sprite.scale), (240, 160, 2))
                decoded = [decode_frame(sprite, frame) for frame in range(sprite.frames)]
                self.assertNotIn(sprite.key, decoded[0])
                self.assertGreater(len({tuple(frame) for frame in decoded}), 1)
                # O palco sob o personagem não anima: evita a impressão de
                # salto mesmo quando partículas e luzes se movem ao redor.
                safe_floor = [
                    frame[y * sprite.width + 72 : y * sprite.width + 168]
                    for frame in decoded
                    for y in range(132, 160)
                ]
                rows_per_frame = 160 - 132
                reference = safe_floor[:rows_per_frame]
                for frame in range(1, sprite.frames):
                    start = frame * rows_per_frame
                    self.assertEqual(reference, safe_floor[start : start + rows_per_frame])
                self.assertTrue((source_root / f"{name}.png").is_file())

    def test_fundos_de_level_cobrem_1_a_99_sem_lacunas(self):
        source_root = ROOT / "sprites" / "sources" / "imagegen" / "backgrounds" / "level_up"
        assets = ROOT / "sdcard" / "clawd"
        ranges = ((1, 9),) + tuple((start, start + 9) for start in range(10, 100, 10))
        covered = []
        for minimum, maximum in ranges:
            name = f"background_level_{minimum:03d}_{maximum:03d}"
            with self.subTest(background=name):
                sprite = parse_clw((assets / f"{name}.clw").read_bytes())
                self.assertEqual((sprite.frames, sprite.frame_ms), (8, 250))
                self.assertEqual((sprite.width, sprite.height, sprite.scale), (240, 160, 2))
                decoded = [decode_frame(sprite, frame) for frame in range(sprite.frames)]
                self.assertNotIn(sprite.key, decoded[0])
                self.assertGreater(len({tuple(frame) for frame in decoded}), 1)
                for frame in decoded[1:]:
                    for y in range(132, 160):
                        self.assertEqual(
                            decoded[0][y * 240 + 72 : y * 240 + 168],
                            frame[y * 240 + 72 : y * 240 + 168],
                        )
                self.assertTrue((source_root / f"{name}.png").is_file())
                self.assertTrue((source_root / f"level_{minimum:03d}_{maximum:03d}_key.png").is_file())
                covered.extend(range(minimum, maximum + 1))
        self.assertEqual(covered, list(range(1, 100)))

    def test_fundos_tematicos_sao_animados_sem_mover_o_chao(self):
        source_root = ROOT / "sprites" / "sources" / "imagegen" / "backgrounds" / "themed"
        assets = ROOT / "sdcard" / "clawd"
        names = (
            "legendary_kingdom",
            "energy_martial_arena",
            "portal_dragon_cavern",
            "chinese_mountain_temple",
            "sakura_plain",
        )
        for theme in names:
            name = f"background_theme_{theme}"
            with self.subTest(background=name):
                sprite = parse_clw((assets / f"{name}.clw").read_bytes())
                self.assertEqual((sprite.frames, sprite.frame_ms), (8, 250))
                self.assertEqual((sprite.width, sprite.height, sprite.scale), (240, 160, 2))
                decoded = [decode_frame(sprite, frame) for frame in range(sprite.frames)]
                self.assertNotIn(sprite.key, decoded[0])
                self.assertGreater(len({tuple(frame) for frame in decoded}), 1)
                for frame in decoded[1:]:
                    for y in range(132, 160):
                        self.assertEqual(
                            decoded[0][y * 240 + 72 : y * 240 + 168],
                            frame[y * 240 + 72 : y * 240 + 168],
                        )
                self.assertTrue((source_root / f"{name}.png").is_file())
                self.assertTrue((source_root / f"{theme}_key.png").is_file())


class StudioTests(unittest.TestCase):
    def test_catalogo_combina_manifesto_e_binario(self):
        catalog = build_catalog(DEFAULT_ASSETS, DEFAULT_MANIFEST)
        self.assertEqual(len(catalog), len(list(DEFAULT_ASSETS.glob("*.clw"))))
        by_name = {entry["name"]: entry for entry in catalog}
        self.assertTrue(all(entry.get("source_url") for entry in catalog))
        self.assertTrue(by_name["alert"]["used"])
        self.assertEqual(by_name["alert"]["frames"], 40)
        self.assertEqual(by_name["typing"]["screen_width"], 172)
        self.assertGreater(by_name["beacon"]["compression"], 1)
        self.assertEqual(by_name["grooving"]["frames"], 20)
        self.assertEqual(by_name["wake"]["source_playback"], "oneshot")
        self.assertEqual(by_name["arachnid"]["frames"], 8)
        self.assertLessEqual(by_name["solar_mind"]["screen_height"], 200)
        self.assertEqual(by_name["marks_salute"]["frames"], 8)
        self.assertLessEqual(by_name["marks_victory"]["screen_height"], 200)
        self.assertEqual(by_name["marks_rest"]["source_columns"], 4)
        self.assertEqual(by_name["marks_rest"]["source_rows"], 3)
        self.assertEqual(by_name["marks_rest"]["frames"], 12)
        self.assertEqual(by_name["background_level_001_009"]["level_min"], 1)
        self.assertEqual(by_name["background_level_090_099"]["level_max"], 99)
        self.assertEqual(by_name["marks_rest"]["frame_ms"], 220)
        self.assertEqual(by_name["marks_rest"]["duration_ms"], 2640)
        eco = by_name["marks_rest_eco4"]
        self.assertEqual(eco["variant_of"], "marks_rest")
        self.assertEqual(eco["economy_profile"], "eco4")
        self.assertTrue(eco["economy_compliant"])
        self.assertIn("economico", eco["roles"])
        self.assertLessEqual(eco["bytes_per_pixel_frame"], 0.30)
        self.assertEqual(eco["source_url"], by_name["marks_rest"]["source_url"])
        fawkes = by_name["fawkes_idle"]
        self.assertEqual(fawkes["frames"], 8)
        self.assertEqual(fawkes["frame_ms"], 167)
        self.assertEqual(fawkes["source_rows"], 2)
        self.assertEqual(
            fawkes["source_url"], "/sources/imagegen/fawkes/idle.png"
        )
        self.assertTrue(by_name["fawkes_idle_eco4"]["economy_compliant"])
        original = by_name["clawd_counting_money"]
        self.assertEqual(original["frames"], 8)
        self.assertEqual(original["frame_ms"], 167)
        self.assertIn("original", original["roles"])
        self.assertEqual(
            original["source_url"],
            "/sources/imagegen/original_actions/counting_money.png",
        )
        throwing = by_name["clawd_throwing_money"]
        self.assertEqual(throwing["screen_height"], 200)
        self.assertEqual(throwing["economy_profile"], None)
        self.assertEqual(
            by_name["marks_rest"]["source_url"],
            "/sources/imagegen/marks_actions/rest.png",
        )
        climate = by_name["clawd_very_cold"]
        self.assertIn("clima", climate["roles"])
        self.assertEqual(climate["frames"], 8)
        level_one = by_name["clawd_level_001"]
        level_99 = by_name["clawd_level_099"]
        self.assertEqual(level_one["level"], 1)
        self.assertEqual(level_99["level"], 99)
        self.assertEqual(level_99["tier"], "Clawd ascendente")
        self.assertIn("progressao", level_99["roles"])
        self.assertEqual(level_99["source_columns"], 4)
        self.assertEqual(level_99["source_rows"], 2)

    def test_servidor_entrega_api_pagina_e_asset(self):
        server = StudioServer(
            ("127.0.0.1", 0),
            asset_dir=DEFAULT_ASSETS,
            manifest_path=DEFAULT_MANIFEST,
        )
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{server.server_port}"
        try:
            with urlopen(base + "/api/health") as response:
                self.assertEqual(json.load(response), {"ok": True})
            with urlopen(base + "/api/sprites") as response:
                self.assertEqual(
                    len(json.load(response)["sprites"]),
                    len(list(DEFAULT_ASSETS.glob("*.clw"))),
                )
            with urlopen(base + "/") as response:
                self.assertIn(b"Clawd Sprite Studio", response.read())
            with urlopen(base + "/clawd/idle.clw") as response:
                self.assertEqual(response.read(4), b"CLWD")
            with urlopen(base + "/sources/imagegen/marks_actions/rest.png") as response:
                self.assertEqual(response.read(8), b"\x89PNG\r\n\x1a\n")
            with urlopen(base + "/sources/vector/clawd-working-typing.svg") as response:
                self.assertIn(b"<svg", response.read(200))
            with self.assertRaises(HTTPError) as blocked:
                urlopen(base + "/sources/../manifest.json")
            self.assertEqual(blocked.exception.code, 404)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
