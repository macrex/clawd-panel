#!/usr/bin/env python3
"""Compila a folha de poses do Kenny na animacao da tela de RESET.

A folha e uma grade (3x2 por padrao) de poses SOLTAS, e nao quadros de uma
animacao pronta: cada celula tem o personagem em lugar e altura diferentes, e a
ordem da grade nao e a ordem da danca. O trabalho daqui e virar isso em ciclo:

  1. tirar o fundo (a folha vem com o xadrez DESENHADO, nao com alpha);
  2. alinhar as poses pelo chao — a base da sombra — e pelo eixo do corpo;
  3. remontar na ordem da danca, com ida e volta no pulo;
  4. reduzir para a caixa da tela e gravar o .clw.

A sombra fica. Ela e quase preta sobre um fundo quase preto, entao some na
tela — mas e ela que define o chao, e sem chao o pulo vira flutuacao.

Uso:

    python tools/build_reset_kenny.py "folha.png" sdcard/clawd/sp_kenny_reset.clw
"""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path
import sys

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_sprite import crop_frames, rgba_to_565, symmetric_crop_box
from clw_format import TRANSPARENT_KEY, parse_clw, write_clw


# O fundo da tela do painel (ui.cpp, `BG`). E ele, e nao o 0x1A1A2E que o
# conversor generico usa, que precisa entrar na mistura das bordas: o
# anti-aliasing e dissolvido contra ESTA cor, e qualquer outra deixa um halo
# claro em volta do bicho quando o quadro cai na tela.
FUNDO_TELA = (0x0C, 0x0E, 0x12)

# A caixa de destino. `R_RESET_Y` (54) e o topo, e o recado da tela comeca em
# PANEL_H-74 = 406: 300 px deixam 52 de respiro. O Cartman usa 240 porque e um
# busto; este e de corpo inteiro E pula, e o mesmo numero o deixaria miudo.
ALTURA_ALVO = 300
LARGURA_MAX = 300

# A ordem da danca. Os indices sao a leitura da grade da esquerda para a
# direita, de cima para baixo. O pulo entra e SAI pelo mesmo caminho (2-4-5-4-2)
# para o ciclo fechar sem teleporte, e a pose parada abre e fecha a volta.
CICLO = (0, 4, 3, 1, 2, 1, 3, 5, 3, 4)

# 10 quadros a 165 ms fecham 1,65 s — a mesma volta que a danca do Cartman
# (24 x 70). South Park e animacao de recorte: 6 quadros por segundo e o ritmo
# do original, e nao economia.
FRAME_MS = 165

# Cores da paleta final. A arte vem com grao (~3 mil cores por quadro), e grao e
# veneno para RLE: cada pixel diferente do vizinho e uma corrida nova. Uma
# paleta unica para todos os quadros tambem impede a cor de tremer entre eles.
CORES = 24

# Quantos pixels da franja do anti-aliasing sao comidos junto com o fundo. Dois
# na resolucao da folha somem no downscale, e sem isso sobra um fio branco.
EROSAO = 2


def _quase_branco(pixel) -> bool:
    r, g, b = pixel[:3]
    return min(r, g, b) >= 228 and max(r, g, b) - min(r, g, b) <= 14


def tirar_fundo(celula: Image.Image) -> Image.Image:
    """Zera o alpha do fundo LIGADO A BORDA, e come a franja clara."""
    largura, altura = celula.size
    pixels = celula.load()
    fundo = bytearray(largura * altura)
    fila: deque[tuple[int, int]] = deque()

    def semear(x: int, y: int) -> None:
        if not fundo[y * largura + x] and _quase_branco(pixels[x, y]):
            fundo[y * largura + x] = 1
            fila.append((x, y))

    for x in range(largura):
        semear(x, 0)
        semear(x, altura - 1)
    for y in range(altura):
        semear(0, y)
        semear(largura - 1, y)

    # Fica no fundo so o que se ALCANCA pela borda: o branco do olho do
    # personagem e cercado de preto e nunca e atingido.
    while fila:
        x, y = fila.popleft()
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < largura and 0 <= ny < altura:
                semear(nx, ny)

    for _ in range(EROSAO):
        novo = bytearray(fundo)
        for y in range(altura):
            base = y * largura
            for x in range(largura):
                if fundo[base + x]:
                    continue
                if ((x and fundo[base + x - 1])
                        or (x + 1 < largura and fundo[base + x + 1])
                        or (y and fundo[base - largura + x])
                        or (y + 1 < altura and fundo[base + largura + x])):
                    novo[base + x] = 1
        fundo = novo

    rgba = celula.convert("RGBA")
    dados = rgba.load()
    for y in range(altura):
        for x in range(largura):
            if fundo[y * largura + x]:
                dados[x, y] = (0, 0, 0, 0)
    return rgba


def chao_da_pose(pose: Image.Image) -> tuple[float, int]:
    """Eixo horizontal e linha do chao, medidos na sombra.

    A sombra e o unico elemento que NAO muda de lugar com a pose: ela marca
    onde o personagem pisa mesmo quando ele esta no ar. Alinhar por ela e o que
    faz o pulo subir em vez de a tela inteira balancar.
    """
    largura, altura = pose.size
    pixels = pose.load()

    def escuro(x: int, y: int) -> bool:
        r, g, b, a = pixels[x, y]
        return a > 128 and max(r, g, b) < 70

    base = None
    for y in range(altura - 1, -1, -1):
        if any(escuro(x, y) for x in range(largura)):
            base = y
            break
    if base is None:
        raise ValueError("pose sem sombra: nao da para achar o chao")

    # O eixo sai da linha MAIS LARGA da sombra, e nao da ultima: a elipse afina
    # nas pontas, e a ultima linha e curta demais para dar um centro estavel.
    melhor = (0, 0.0)
    for y in range(base, max(-1, base - 24), -1):
        xs = [x for x in range(largura) if escuro(x, y)]
        if len(xs) > melhor[0]:
            melhor = (len(xs), (min(xs) + max(xs)) / 2)
    return melhor[1], base


def alinhar(poses: list[Image.Image]) -> list[Image.Image]:
    """Poe todas as poses num canvas comum, com o mesmo chao e o mesmo eixo."""
    medidas = [chao_da_pose(pose) for pose in poses]
    largura, altura = poses[0].size
    alvo_x, alvo_y = largura / 2, altura - 1

    alinhadas: list[Image.Image] = []
    for pose, (eixo, base) in zip(poses, medidas):
        canvas = Image.new("RGBA", (largura, altura), (0, 0, 0, 0))
        canvas.alpha_composite(pose, (round(alvo_x - eixo), round(alvo_y - base)))
        alinhadas.append(canvas)
    return alinhadas


def caixa_da_uniao(quadros: list[Image.Image], margem: int) -> tuple[int, int, int, int]:
    """Recorte que cabe TODOS os quadros, simetrico em torno do eixo do corpo."""
    largura, altura = quadros[0].size
    uniao = None
    for quadro in quadros:
        caixa = quadro.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
        if caixa is None:
            continue
        uniao = caixa if uniao is None else (
            min(uniao[0], caixa[0]), min(uniao[1], caixa[1]),
            max(uniao[2], caixa[2]), max(uniao[3], caixa[3]),
        )
    if uniao is None:
        raise ValueError("todos os quadros sao transparentes")

    # O eixo e o centro do canvas (foi para la que `alinhar` levou o corpo). A
    # caixa cresce igual dos dois lados: quem desenha centraliza a CAIXA, e uma
    # caixa torta poe o bicho torto na tela.
    eixo = largura / 2
    meia = max(eixo - uniao[0], uniao[2] - eixo) + margem
    x0 = max(0, int(eixo - meia))
    x1 = min(largura, int(eixo + meia))
    y0 = max(0, uniao[1] - margem)
    y1 = min(altura, uniao[3] + margem)
    return x0, y0, x1, y1


def reduzir(quadro: Image.Image, tamanho: tuple[int, int]) -> Image.Image:
    """Downscale com alpha pre-multiplicado.

    Sem pre-multiplicar, a media de um pixel opaco com um transparente carrega a
    COR do transparente (preto, no nosso caso) para dentro da borda — o mesmo
    halo do fundo, so que escuro.
    """
    premultiplicado = Image.new("RGBA", quadro.size)
    origem, destino = quadro.load(), premultiplicado.load()
    for y in range(quadro.height):
        for x in range(quadro.width):
            r, g, b, a = origem[x, y]
            destino[x, y] = (r * a // 255, g * a // 255, b * a // 255, a)

    menor = premultiplicado.resize(tamanho, Image.Resampling.LANCZOS)
    dados = menor.load()
    for y in range(menor.height):
        for x in range(menor.width):
            r, g, b, a = dados[x, y]
            if a == 0:
                dados[x, y] = (0, 0, 0, 0)
            else:
                dados[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a),
                               min(255, b * 255 // a), a)
    return menor


def paletizar(quadros: list[Image.Image], cores: int) -> list[Image.Image]:
    """Uma paleta so para o ciclo inteiro, com o alpha preservado."""
    largura, altura = quadros[0].size
    tira = Image.new("RGB", (largura, altura * len(quadros)))
    for indice, quadro in enumerate(quadros):
        tira.paste(quadro.convert("RGB"), (0, indice * altura))
    paleta = tira.quantize(colors=cores, method=Image.Quantize.MEDIANCUT,
                           dither=Image.Dither.NONE)

    saida: list[Image.Image] = []
    for quadro in quadros:
        plano = quadro.convert("RGB").quantize(
            palette=paleta, dither=Image.Dither.NONE).convert("RGB")
        plano.putalpha(quadro.getchannel("A"))
        saida.append(plano)
    return saida


def compilar(origem: Path, saida: Path, *, colunas: int, linhas: int,
             altura_alvo: int, largura_max: int, frame_ms: int,
             cores: int, margem: int, preview: Path | None) -> None:
    with Image.open(origem) as aberta:
        folha = aberta.convert("RGB")
    if folha.width % colunas or folha.height % linhas:
        raise ValueError(
            f"folha {folha.width}x{folha.height} nao divide por {colunas}x{linhas}")

    celula_w, celula_h = folha.width // colunas, folha.height // linhas
    poses = [
        tirar_fundo(folha.crop((c * celula_w, r * celula_h,
                                (c + 1) * celula_w, (r + 1) * celula_h)))
        for r in range(linhas) for c in range(colunas)
    ]
    for indice in CICLO:
        if indice >= len(poses):
            raise ValueError(f"o ciclo pede a pose {indice}; a folha tem {len(poses)}")

    poses = alinhar(poses)
    quadros = [poses[indice] for indice in CICLO]

    x0, y0, x1, y1 = caixa_da_uniao(quadros, margem)
    quadros = [quadro.crop((x0, y0, x1, y1)) for quadro in quadros]

    fator = altura_alvo / quadros[0].height
    destino = (round(quadros[0].width * fator), altura_alvo)
    if destino[0] > largura_max:
        fator = largura_max / quadros[0].width
        destino = (largura_max, round(quadros[0].height * fator))
    quadros = [reduzir(quadro, destino) for quadro in quadros]
    quadros = paletizar(quadros, cores)

    if preview:
        montar_preview(quadros, preview)

    pixels = [
        [rgba_to_565(p, key=TRANSPARENT_KEY, background=FUNDO_TELA)
         for p in quadro.getdata()]
        for quadro in quadros
    ]
    largura, altura = destino
    caixa = symmetric_crop_box(pixels, largura, altura, TRANSPARENT_KEY)
    pixels = crop_frames(pixels, largura, caixa)
    _x, _y, largura, altura = caixa

    saida.parent.mkdir(parents=True, exist_ok=True)
    bytes_gravados = write_clw(saida, pixels, largura, altura,
                               key=TRANSPARENT_KEY, frame_ms=frame_ms, scale=1)
    sprite = parse_clw(saida.read_bytes())
    print(f"Gerado: {saida}")
    print(f"  quadros: {sprite.frames} @ {frame_ms} ms (ciclo {sprite.duration_ms} ms)")
    print(f"  caixa:   {largura}x{altura}")
    print(f"  arquivo: {bytes_gravados / 1024:.1f} KB, "
          f"compressao {sprite.compression_ratio:.1f}x")


def montar_preview(quadros: list[Image.Image], destino: Path) -> None:
    colunas = 5
    linhas = (len(quadros) + colunas - 1) // colunas
    largura, altura = quadros[0].size
    folha = Image.new("RGB", (largura * colunas, altura * linhas), FUNDO_TELA)
    for indice, quadro in enumerate(quadros):
        folha.paste(quadro, ((indice % colunas) * largura,
                             (indice // colunas) * altura), quadro)
    destino.parent.mkdir(parents=True, exist_ok=True)
    folha.save(destino)
    print(f"  preview: {destino}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("origem", type=Path, help="folha PNG com as poses")
    parser.add_argument("saida", type=Path, help="arquivo .clw de saida")
    parser.add_argument("--colunas", type=int, default=3)
    parser.add_argument("--linhas", type=int, default=2)
    parser.add_argument("--altura", type=int, default=ALTURA_ALVO)
    parser.add_argument("--largura-max", type=int, default=LARGURA_MAX)
    parser.add_argument("--frame-ms", type=int, default=FRAME_MS)
    parser.add_argument("--cores", type=int, default=CORES)
    parser.add_argument("--margem", type=int, default=4,
                        help="respiro em px da folha ao redor da uniao")
    parser.add_argument("--preview", type=Path, default=None,
                        help="grava um contato dos quadros ja reduzidos")
    args = parser.parse_args()

    if not args.origem.is_file():
        parser.error(f"arquivo nao encontrado: {args.origem}")
    try:
        compilar(args.origem, args.saida, colunas=args.colunas, linhas=args.linhas,
                 altura_alvo=args.altura, largura_max=args.largura_max,
                 frame_ms=args.frame_ms, cores=args.cores, margem=args.margem,
                 preview=args.preview)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
