#!/usr/bin/env python3
"""Deriva o sprite de CLAUDE OFFLINE do sprite do Token, trocando a camisa.

A tela de "servidor fora" e a irma da tela do Token: mesmo enquadramento, mesmo
gesto para dispensar, mesmo bicho. Se fosse outro personagem, o painel diria
duas coisas diferentes com a mesma cara — e o que muda entre elas nao e o
desenho, e o recado na camisa.

Por que DERIVAR em vez de compilar de uma folha nova: a arte e de terceiros e
mora so no cartao (ver .gitignore). Nao existe PNG original dela no repositorio,
e nao vai existir. O que existe e o proprio `sp_token.clw`, e ele ja esta no
tamanho da tela, ja tem as quatro poses e ja tem a paleta reduzida — refazer
esse caminho a partir de um render novo daria um bicho PARECIDO, e parecido e
justamente o que estraga o efeito de reconhecer a mesma tela.

O trabalho e cirurgico e acontece dentro dos pixels RGB565 do arquivo:

  1. apaga o retangulo do texto da camisa, pintando a cor lisa dela;
  2. escreve as duas linhas novas por cima, na mesma caixa;
  3. regrava, quadro a quadro, com o mesmo cabecalho do original.

Fora do retangulo NENHUM pixel e tocado — nem reconvertido. O RLE e refeito, e
so.

Uso:

    python tools/build_token_offline.py \
        sdcard/clawd/sp_token.clw sdcard/clawd/sp_offline.clw
"""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))

from clw_format import decode_frame, read_clw, rgb_to_565, write_clw


# A caixa do texto da camisa, medida nos quatro quadros do `sp_token.clw`: o
# bloco das tres linhas cabe em x[75..145] y[244..291], e o corpo se desloca no
# maximo 2 px entre as poses. A margem daqui e folgada de proposito — ela come o
# deslocamento e ainda sobra camisa lisa nas bordas, entao o mesmo retangulo
# serve para os quatro sem deixar resto de letra em nenhum.
CAIXA = (68, 238, 155, 298)     # x0, y0, x1, y1 (exclusivo em x1/y1)

# O eixo do texto. E o centro do bloco ORIGINAL, e nao o centro da caixa acima:
# a camisa nao e simetrica na tela, e centrar pela caixa jogaria as palavras
# alguns pixels para a direita de onde o olho espera.
CENTRO_X = 111

# As duas linhas. A de baixo vai dentro da tarja branca, como o "LIFE" do
# original — e o mesmo desenho de cartaz, com a palavra que importa em negativo.
LINHA_CIMA = "CLAUDE"
LINHA_BAIXO = "OFFLINE"

# Onde cada uma se apoia, em Y. As duas ocupam o mesmo bloco vertical que as
# TRES do original ocupavam: sobrou respiro, e usa-lo faria o texto crescer e
# perder o parentesco com a camisa que ele imita.
Y_CIMA, Y_BAIXO = 257, 279

# Larguras-alvo, em pixels. Batem com as do original ("TOKEN'S" tem 66,
# "MATTERS" tem 70) e e delas que sai o corpo da fonte — pedir um tamanho em
# pontos daria letra maior ou menor conforme a fonte que a maquina tiver.
LARGURA_CIMA, LARGURA_BAIXO = 64, 60

# A sombra sob as letras claras. O original tem uma, de um pixel, e sem ela o
# branco fica chapado sobre a camisa em vez de assentado nela.
SOMBRA = (16, 20, 16)

# Onde procurar uma sans-serif pesada. A camisa do original e Helvetica bold; a
# Arial e a metrica dela, e a DejaVu entra so para o script rodar fora do
# Windows.
FONTES = (
    r"C:\Windows\Fonts\arialbd.ttf",
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
)


def achar_fonte() -> str:
    for caminho in FONTES:
        if Path(caminho).exists():
            return caminho
    raise SystemExit(
        "nenhuma fonte negrito encontrada; edite FONTES em "
        + Path(__file__).name
    )


def _tinta(fonte: ImageFont.FreeTypeFont, texto: str):
    """Caixa da TINTA, e nao a metrica da fonte.

    `getbbox` devolve a caixa tipografica, que inclui o vao acima das
    maiusculas e varia com a fonte. Centrar por ela deixaria o texto alguns
    pixels acima do centro — pouco numa pagina, muito numa camisa de 47 px.
    """
    img = Image.new("L", (400, 200), 0)
    ImageDraw.Draw(img).text((20, 40), texto, font=fonte, fill=255)
    caixa = img.getbbox()
    if caixa is None:
        raise SystemExit(f"a fonte nao desenhou nada para {texto!r}")
    return caixa


def fonte_para_largura(caminho: str, texto: str, alvo: int):
    """O corpo cuja TINTA chega mais perto de `alvo` pixels de largura."""
    melhor = None
    for corpo in range(8, 40):
        fonte = ImageFont.truetype(caminho, corpo)
        caixa = _tinta(fonte, texto)
        erro = abs((caixa[2] - caixa[0]) - alvo)
        if melhor is None or erro < melhor[0]:
            melhor = (erro, fonte, corpo, caixa)
    return melhor[1], melhor[2], melhor[3]


def escrever(d: ImageDraw.ImageDraw, fonte, texto, centro_y, cor, sombra):
    caixa = _tinta(fonte, texto)
    larg, alt = caixa[2] - caixa[0], caixa[3] - caixa[1]
    x = CENTRO_X - larg // 2 - (caixa[0] - 20)
    y = centro_y - alt // 2 - (caixa[1] - 40)
    if sombra:
        d.text((x + 1, y + 1), texto, font=fonte, fill=SOMBRA)
    d.text((x, y), texto, font=fonte, fill=cor)
    return larg, alt


def cor_da_camisa(pixels: list[int], largura: int) -> int:
    """O 565 que mais aparece na caixa. E a camisa: o texto nunca a supera."""
    x0, y0, x1, y1 = CAIXA
    conta = Counter(
        pixels[y * largura + x]
        for y in range(y0, y1)
        for x in range(x0, x1)
    )
    return conta.most_common(1)[0][0]


def de_565(v: int) -> tuple[int, int, int]:
    r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
    return (r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31


def remendo(camisa565: int, fonte_cima, fonte_baixo) -> Image.Image:
    """A caixa inteira, ja com o texto novo, pronta para virar 565.

    O fundo e a cor da camisa expandida de 565 para 888. A volta e exata: o
    arredondamento de `de_565` cai sempre no mesmo degrau que `rgb_to_565`
    corta, entao o pixel que nao recebe tinta regrava o valor identico ao que
    estava no arquivo.
    """
    x0, y0, x1, y1 = CAIXA
    # A tela inteira, e nao so a caixa: CENTRO_X, Y_CIMA e Y_BAIXO sao
    # coordenadas do sprite, e desenhar aqui evita subtrair a origem em cada
    # uma delas. O recorte no fim devolve so o pedaco que vai ser gravado.
    tela = Image.new("RGB", (x1, y1), de_565(camisa565))
    dg = ImageDraw.Draw(tela)
    escrever(dg, fonte_cima, LINHA_CIMA, Y_CIMA, (255, 255, 255), True)

    caixa = _tinta(fonte_baixo, LINHA_BAIXO)
    larg, alt = caixa[2] - caixa[0], caixa[3] - caixa[1]
    dg.rectangle(
        [CENTRO_X - larg // 2 - 4, Y_BAIXO - alt // 2 - 3,
         CENTRO_X + larg // 2 + 4, Y_BAIXO + alt // 2 + 3],
        fill=(255, 255, 255),
    )
    escrever(dg, fonte_baixo, LINHA_BAIXO, Y_BAIXO, (0, 0, 0), False)
    return tela.crop(CAIXA)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("origem", type=Path, nargs="?",
                   default=Path("sdcard/clawd/sp_token.clw"))
    p.add_argument("saida", type=Path, nargs="?",
                   default=Path("sdcard/clawd/sp_offline.clw"))
    p.add_argument("--png", type=Path,
                   help="tambem exporta os quadros em PNG, para conferir")
    args = p.parse_args()

    if not args.origem.exists():
        raise SystemExit(
            f"{args.origem} nao existe. A arte do Token mora no CARTAO, nunca "
            "no repositorio: copie o arquivo do microSD antes de rodar isto."
        )

    fonte = achar_fonte()
    f_cima, corpo_cima, _ = fonte_para_largura(fonte, LINHA_CIMA, LARGURA_CIMA)
    f_baixo, corpo_baixo, _ = fonte_para_largura(
        fonte, LINHA_BAIXO, LARGURA_BAIXO)
    print(f"fonte: {Path(fonte).name} corpo {corpo_cima}/{corpo_baixo}")

    origem = read_clw(args.origem)
    x0, y0, x1, y1 = CAIXA
    if x1 > origem.width or y1 > origem.height:
        raise SystemExit(
            f"a caixa da camisa {CAIXA} nao cabe em "
            f"{origem.width}x{origem.height}: este script so serve para o "
            "sp_token.clw de 224x336"
        )

    quadros = []
    for i in range(origem.frames):
        pixels = decode_frame(origem, i)
        camisa = cor_da_camisa(pixels, origem.width)
        patch = remendo(camisa, f_cima, f_baixo)
        for y in range(y1 - y0):
            base = (y0 + y) * origem.width
            for x in range(x1 - x0):
                r, g, b = patch.getpixel((x, y))
                pixels[base + x0 + x] = rgb_to_565(r, g, b)
        quadros.append(pixels)
        print(f"  quadro {i}: camisa 0x{camisa:04X}")

    bytes_gravados = write_clw(
        args.saida, quadros, origem.width, origem.height,
        key=origem.key, frame_ms=origem.frame_ms, scale=origem.scale,
    )
    print(f"{args.saida}: {bytes_gravados} bytes "
          f"({origem.frames} quadros {origem.width}x{origem.height}, "
          f"{origem.frame_ms} ms) — origem tinha {origem.byte_size}")

    if args.png:
        args.png.mkdir(parents=True, exist_ok=True)
        for i, pixels in enumerate(quadros):
            img = Image.new("RGBA", (origem.width, origem.height))
            img.putdata([
                (0, 0, 0, 0) if v == origem.key else (*de_565(v), 255)
                for v in pixels
            ])
            img.save(args.png / f"frame_{i:03d}.png")
        print(f"PNGs em {args.png}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
