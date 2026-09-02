#!/usr/bin/env python3
"""Compila os SVGs de marca em `sprites/sources/marcas/` nas mascaras de alfa
que `src/provedores.cpp` carrega.

    python tools\build_icone_provedor.py            # imprime os tres arrays
    python tools\build_icone_provedor.py codex      # so um

POR QUE MASCARA DE ALFA E NAO 1 BIT
Ate esta versao os icones eram bitmaps de 1 bit, e o knot da OpenAI reprovava:
seis alcas finas entrelacadas, reduzidas a preto-ou-branco, viram ruido em 9, 11,
13, 16 e 20 px — so em 24 px a forma voltava, e 24 px nao cabem na faixa de
cabecalho. O que salva o traco fino nao e tamanho, e meio-tom. Guardando o alfa
de 8 bits e misturando com o fundo na hora de desenhar, o mesmo knot se le em
12 px. Ver o comentario de `misturaNaFaixa` em src/ui.cpp.

Depende do Playwright, que ja e dependencia de tools/render_svg_frames.py.
"""

from __future__ import annotations

import pathlib
import sys

from PIL import Image, ImageChops, ImageDraw
from playwright.sync_api import sync_playwright

RAIZ  = pathlib.Path(__file__).resolve().parent.parent
FONTE = RAIZ / "sprites" / "sources" / "marcas"
PX    = 512      # a rasterizacao intermediaria; a reducao final sai daqui

# Cada marca traz a receita que ela EXIGE, e as tres sao diferentes de proposito.
#
# `modo`  como separar o desenho do fundo. "alfa" quando o SVG ja vem recortado;
#         "escuro" para o Codex, que e um knot preto dentro de um quadrado
#         branco — ali quem separa e a luminancia, nao a transparencia.
# `grade` quando o logotipo NASCE em grade de pixels, o par (colunas, linhas)
#         dela. A reducao vira amostragem exata: sem meio-tom, sem franja.
#         `escala` diz quantas linhas cada celula ocupa (o Claude Code e
#         16x5 celulas de 32x64 px — celula duas vezes mais alta que larga, e
#         guardar 16x5 achatava o icone pela metade).
# `alt`   a altura final, para quem nao tem grade. 12 e o teto: a faixa de
#         cabecalho tem 16 px (H_CAB em src/ui.cpp) e precisa de folga dos dois
#         lados.
# `filtro`/`contraste` so o Codex precisa, e o comentario dele em
#         src/provedores.cpp explica por que.
MARCAS = {
    "claude": dict(arquivo="claude-code.svg", nome="CLAUDE",
                   modo="alfa", grade=(16, 5), escala=2),
    "agy":    dict(arquivo="antigravity.svg", nome="AGY",
                   modo="alfa", alt=12, filtro=Image.LANCZOS),
    "codex":  dict(arquivo="codex.svg", nome="CODEX",
                   modo="escuro", alt=12, filtro=Image.BOX, contraste=1.6),
    # O reserva: vale para TODA CLI que nao seja uma das tres de cima (ver o
    # fallback de `iconeDe`). Unico com `modo="silhueta"` — a lhama e desenhada
    # a traco, e traco de um terco de pixel nao sobrevive a reducao; o porque
    # esta em `preencher`. A reducao e por MEDIA DE AREA pela mesma razao do
    # Codex: LANCZOS cava halo, e aqui ele apagaria os olhos vazados.
    "ollama": dict(arquivo="ollama.svg", nome="OLLAMA",
                   modo="silhueta", alt=12, filtro=Image.BOX),
}


def rasterizar(svg: pathlib.Path, saida: pathlib.Path) -> None:
    """O SVG num PNG de PX x PX, com fundo transparente.

    Passa por um HTML e nao por `goto` direto no .svg: navegar ate um SVG faz o
    documento ser XML, e `set_content` recusa. O <img> tambem garante que so o
    desenho conte — nada de folha de estilo do visualizador de SVG do Chromium.
    """
    html = saida.with_suffix(".html")
    html.write_text(
        "<style>html,body{margin:0;background:transparent}"
        f"img{{width:{PX}px;height:{PX}px;display:block}}</style>"
        f'<img src="{svg.name}">', encoding="utf-8")
    with sync_playwright() as p:
        b  = p.chromium.launch(headless=True)
        pg = b.new_page(viewport={"width": PX, "height": PX})
        pg.goto(html.as_uri())
        pg.wait_for_timeout(600)
        pg.screenshot(path=str(saida), omit_background=True)
        b.close()
    html.unlink(missing_ok=True)


def mascara(png: pathlib.Path, modo: str) -> Image.Image:
    """A cobertura, ja recortada na caixa do desenho."""
    img = img_rgba = Image.open(png).convert("RGBA")
    m  = Image.new("L", img.size, 0)
    mp, px = m.load(), img_rgba.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if modo == "alfa":
                mp[x, y] = a
            else:
                lum = (r * 299 + g * 587 + b * 114) // 1000
                mp[x, y] = a if lum < 128 else 0
    caixa = m.getbbox()
    m = m.crop(caixa) if caixa else m
    return preencher(m) if modo == "silhueta" else m


def preencher(m: Image.Image) -> Image.Image:
    """A area FECHADA por um desenho de contorno, com as ilhas de dentro vazadas.

    Existe pela lhama do Ollama, que e line art: o traco tem 14 px em 512, ou
    seja um TERCO de pixel na altura de 12 px em que o icone e desenhado. O
    contorno some na reducao e sobra ruido — e nao adianta meio-tom, porque nao
    ha traco para preservar. O que sobrevive nessa altura e a SILHUETA (as duas
    orelhas dizem que e um bicho), e os olhos e o focinho vazados, que sao a
    unica coisa que separa a silhueta de um retangulo com duas pontas.
    """
    b = m.point(lambda v: 255 if v >= 128 else 0)

    # Folga so nos tres lados FECHADOS. A lhama e cortada em baixo pelo viewBox,
    # entao o interior encosta na ultima linha: uma folga embaixo ligaria o
    # fundo externo ao interior e o preenchimento vazaria (medido — foi o
    # primeiro resultado, uma silhueta que voltou vazia).
    def com_folga():
        p = Image.new("L", (b.width + 4, b.height + 2), 0)
        p.paste(b, (2, 2))
        return p

    fora = com_folga()
    ImageDraw.floodfill(fora, (0, 0), 128)     # tudo que o fundo alcanca
    cheia = fora.point(lambda v: 0 if v == 128 else 255)

    # As ILHAS sao o desenho que nao faz parte do contorno externo: os olhos, o
    # oval do focinho, a narina. O contorno e o que a linha do meio encontra
    # primeiro vindo da esquerda.
    ilhas = com_folga()
    p = ilhas.load()
    y = ilhas.height // 2
    x = next((x for x in range(ilhas.width) if p[x, y] == 255), None)
    if x is not None:
        ImageDraw.floodfill(ilhas, (x, y), 128)
    ilhas = ilhas.point(lambda v: 255 if v == 255 else 0)

    return ImageChops.subtract(cheia, ilhas).crop(
        (2, 2, cheia.width - 2, cheia.height))


def por_grade(m: Image.Image, cols: int, linhas: int, escala: int):
    """Amostra o centro de cada celula. Sem interpolacao: 0 ou 255."""
    w, h = m.size
    p = m.load()
    saida = []
    for r in range(linhas):
        y = min(h - 1, int((r + 0.5) * h / linhas))
        linha = [255 if p[min(w - 1, int((c + 0.5) * w / cols)), y] >= 128 else 0
                 for c in range(cols)]
        saida.extend([linha] * escala)
    return saida


def por_altura(m: Image.Image, alt: int, filtro, contraste: float = 1.0):
    r = m.resize((max(1, round(m.width * alt / m.height)), alt), filtro)
    p = r.load()
    def curva(v: int) -> int:
        if contraste == 1.0:
            return v
        return round(255 * min(1.0, max(0.0, (v / 255 - 0.5) * contraste + 0.5)))
    return [[curva(p[x, y]) for x in range(r.width)] for y in range(r.height)]


def compilar(chave: str, receita: dict, tmp: pathlib.Path) -> None:
    svg = FONTE / receita["arquivo"]
    if not svg.exists():
        sys.exit(f"falta {svg}")
    # O PNG intermediario fica ao lado do SVG por causa do <img src> relativo.
    png = FONTE / f"_{chave}.png"
    try:
        rasterizar(svg, png)
        m = mascara(png, receita["modo"])
    finally:
        png.unlink(missing_ok=True)

    if "grade" in receita:
        bits = por_grade(m, *receita["grade"], receita["escala"])
    else:
        bits = por_altura(m, receita["alt"], receita["filtro"],
                          receita.get("contraste", 1.0))

    h, w = len(bits), len(bits[0])
    print(f"const uint8_t {receita['nome']}[] = {{")
    for linha in bits:
        print("    " + " ".join(f"{v:3d}," for v in linha))
    print("};")
    print(f"// {receita['nome']}: {w}x{h} — em iconeDe(), {{{receita['nome']}, "
          f"{w}, {h}}}\n")


def main() -> int:
    alvos = sys.argv[1:] or list(MARCAS)
    for chave in alvos:
        if chave not in MARCAS:
            sys.exit(f"marca desconhecida: {chave} (tenho {', '.join(MARCAS)})")
        compilar(chave, MARCAS[chave], FONTE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
