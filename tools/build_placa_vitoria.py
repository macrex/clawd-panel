#!/usr/bin/env python3
"""Poe a placa do mascote do Claude Code nas quatro poses de VITORIA.

POR QUE UMA PLACA. A 30-42 px o olho de longe le cor e silhueta, nao gesto: a
pose de vitoria de cada um (dancar, rir, gingar) tinha a mesma mancha da pose de
descanso, e o aviso de turno concluido passava batido. A placa muda a silhueta
dos quatro de uma vez — e o mesmo objeto, no mesmo lugar, em todo mundo.

A placa entra em TODOS os quadros na mesma posicao. Ela nao acompanha a mao
porque nao ha mao: Stan e Kenny nao mostram as maos nesta escala. As duas
manchas de pele nas bordas sao desenhadas aqui, e e so isso que sustenta a
leitura de "segurando".

O MASCOTE E DESENHADO, NAO REDUZIDO. Reamostrar o PNG de 600 px para 12 come as
pernas e borra os olhos `>` `<`, que sao a identidade da coisa. As proporcoes
saem medidas do original (corpo 527x415, orelhas no meio da altura, quatro
pernas recortadas na base) e viram retangulo em `mascote()`.

O TETO E O SELO_H. `clawd.cpp` reduz o icone da fileira por divisor INTEIRO
contra 42 px: um sprite de 43 cai no divisor 2 e sai pela METADE na tela. A
placa cresce a moldura para cima (`CRESCER`) e o teto e conferido antes de
gravar. Com 38 px de origem sobra pouco — nao aumente PLACA_H sem refazer a
conta.

A ORIGEM E SEMPRE A MATERIA-PRIMA, nunca o arquivo ja gravado no cartao: rodar
duas vezes sobre o proprio destino empilharia placa sobre placa. As poses cruas
ficam em `sprites/sources/south_park/`, fora do git como o resto da arte de
terceiros.

Uso:

    python tools/build_placa_vitoria.py
    python tools/build_placa_vitoria.py --origem outra/pasta --destino E:/clawd
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_sprite import rgba_to_565
from clw_format import decode_frame, read_clw, write_clw


RAIZ = Path(__file__).resolve().parent.parent
ORIGEM = RAIZ / "sprites" / "sources" / "south_park"
DESTINO = RAIZ / "sdcard" / "clawd"
NOMES = ("cartman", "stan", "kenny", "kyle")

# O fundo da tela do painel (ui.cpp, `BG`). Ver build_reset_kenny.
FUNDO_TELA = (0x0C, 0x0E, 0x12)

# Teto de `SELO_H` em clawd.cpp. Passar disto dobra o divisor.
SELO_H = 42

# ---- A placa ----
# 16 px de altura para o mascote caber em 12 com 2 px de folga em cima e em
# baixo. Colado na moldura ele lia como um bloco laranja; a folga e o que faz a
# placa continuar sendo uma placa.
PLACA_H = 16
# Quanto a moldura cresce para cima. A placa fica presa ao PE do sprite, entao
# sem isto os 16 px subiriam por cima do queixo. Com 38 de origem: 41 < 42.
CRESCER = 3
# Sobra horizontal: 3 px de cada lado, para o corpo do personagem aparecer.
MARGEM = 6
# Distancia da base da placa ate o pe do sprite.
BAIXO = 2
# PLACA_H menos a altura do mascote. Metade em cima, metade embaixo.
FOLGA = 4

PAPEL = (244, 241, 232)
TINTA = (26, 26, 30)
PELE = (255, 220, 177)
PELE_BORDA = (120, 90, 60)
LARANJA = (217, 119, 87)          # o laranja do mascote, medido no PNG
PRETO = (16, 16, 20)


def mascote(altura: int) -> Image.Image:
    """O mascote do Claude Code em pixel, na altura pedida."""
    largura = round(altura * 1.27)          # 527x415 do original
    im = Image.new("RGBA", (largura, altura), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    # O corpo deixa 1 px de cada lado livre para as orelhas saltarem.
    bx0, bx1 = 1, largura - 2
    d.rectangle([bx0, 0, bx1, altura - 1], fill=LARANJA)
    d.rectangle([0, round(altura * 0.28), 0, round(altura * 0.52)], fill=LARANJA)
    d.rectangle([largura - 1, round(altura * 0.28),
                 largura - 1, round(altura * 0.52)], fill=LARANJA)

    # Os olhos `>` e `<`: chevron de tres linhas. O traco engrossa so quando ha
    # altura para isso — a 11 px o segundo pixel fecha o V e vira um triangulo.
    corpo_w = bx1 - bx0 + 1
    ey = round(altura * 0.30)
    ex = bx0 + max(1, round(corpo_w * 0.17))
    dx = bx1 - max(1, round(corpo_w * 0.17))
    tracos = [(0, 0), (1, 1), (0, 2)]
    if altura >= 12:
        tracos += [(1, 0), (2, 1), (1, 2)]
    for a, b in tracos:
        d.point((ex + a, ey + b), fill=PRETO)
        d.point((dx - a, ey + b), fill=PRETO)

    # As pernas sao VAOS recortados na base, e nao retangulos desenhados: as
    # fracoes sao as do original (26 colunas, vaos em 5-6, 9-16 e 19-20).
    prof = 2 if altura < 13 else 3
    for f0, f1 in ((0.19, 0.27), (0.42, 0.58), (0.73, 0.81)):
        gx0 = bx0 + round(corpo_w * f0)
        gx1 = max(gx0, bx0 + round(corpo_w * f1) - 1)
        d.rectangle([gx0, altura - prof, gx1, altura - 1], fill=(0, 0, 0, 0))
    return im


def com_placa(quadro: Image.Image) -> Image.Image:
    """Devolve o quadro com a placa, a moldura ja crescida."""
    w, h = quadro.size
    alto = h + CRESCER
    saida = Image.new("RGBA", (w, alto), (0, 0, 0, 0))
    saida.paste(quadro, (0, CRESCER))
    d = ImageDraw.Draw(saida)

    pw = w - MARGEM
    px = (w - pw) // 2
    py = alto - PLACA_H - BAIXO

    # As maos entram ANTES: a placa fica por cima delas, como se estivesse a
    # frente. Ao contrario, a mao pousaria sobre o papel.
    for hx in (px - 3, px + pw):
        d.rectangle([hx, py + PLACA_H - 6, hx + 2, py + PLACA_H - 3],
                    fill=PELE, outline=PELE_BORDA)

    d.rectangle([px, py, px + pw - 1, py + PLACA_H - 1],
                fill=PAPEL, outline=TINTA)
    m = mascote(PLACA_H - FOLGA)
    saida.paste(m, (px + (pw - m.width) // 2, py + (PLACA_H - m.height) // 2), m)
    return saida


def quadro_rgba(sprite, indice: int) -> Image.Image:
    im = Image.new("RGBA", (sprite.width, sprite.height), (0, 0, 0, 0))
    im.putdata([
        (0, 0, 0, 0) if v == sprite.key else (
            ((v >> 11) & 0x1F) * 255 // 31,
            ((v >> 5) & 0x3F) * 255 // 63,
            (v & 0x1F) * 255 // 31,
            255,
        )
        for v in decode_frame(sprite, indice)
    ])
    return im


def compilar(nome: str, origem: Path, destino: Path) -> None:
    cru = read_clw(origem / f"sp_{nome}_vitoria.clw")
    quadros = []
    for i in range(cru.frames):
        marcado = com_placa(quadro_rgba(cru, i))
        quadros.append([
            rgba_to_565(p, key=cru.key, background=FUNDO_TELA)
            for p in marcado.convert("RGBA").get_flattened_data()
        ])
    w, h = marcado.size
    if h > SELO_H:
        raise SystemExit(
            f"{nome}: {h} px passa de SELO_H={SELO_H} — o firmware reduziria "
            f"por divisor 2 e o bicho sairia pela metade"
        )
    alvo = destino / f"sp_{nome}_vitoria.clw"
    tam = write_clw(alvo, quadros, w, h, key=cru.key, frame_ms=cru.frame_ms,
                    scale=cru.scale)
    pronto = read_clw(alvo)          # o parse valida o que acabou de ser escrito
    print(f"{nome:8s} {cru.width}x{cru.height} -> {pronto.width}x{pronto.height}"
          f"  {pronto.frames}q  {tam // 1024} KB")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--origem", type=Path, default=ORIGEM,
                   help="pasta com as poses cruas (padrao: %(default)s)")
    p.add_argument("--destino", type=Path, default=DESTINO,
                   help="onde gravar os .clw (padrao: %(default)s)")
    args = p.parse_args()

    if args.origem.resolve() == args.destino.resolve():
        raise SystemExit("origem igual ao destino: a segunda rodada empilharia "
                         "placa sobre placa")
    for nome in NOMES:
        compilar(nome, args.origem, args.destino)


if __name__ == "__main__":
    main()
