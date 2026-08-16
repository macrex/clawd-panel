#!/usr/bin/env python3
"""Mede a qualidade de um .clw e desenha o prototipo do que a placa vai mostrar.

    python tools/validar_sprite.py sdcard/clawd/sp_cartman_reset.clw
    python tools/validar_sprite.py novo.clw --baseline sdcard/clawd/sp_kenny_reset.clw
    python tools/validar_sprite.py novo.clw --saida pasta/   # contato, tela e GIF

POR QUE MEDIR, SE DA PARA OLHAR
Da para olhar — e foi o que se fez por tres rodadas do Cartman, cada uma
corrigindo o que o olho pegou e deixando passar o que ele nao pega. O olho ve
a silhueta; ele NAO conta que um sprite tem 684 cores onde o vizinho tem 223,
nem que 15% dos pixels discordam da vizinhanca. Esses dois numeros sao o
"detalhe estourado" antes de ele virar mancha na tela.

O QUE CADA NUMERO PEGA

  cores        Cores distintas no arquivo. Um sprite de recorte tem paleta
               curta; cor demais e mistura de borda — alpha parcial de regiao
               difusa (sombra, glow) virando cor nova a cada pixel. E o numero
               que mais separou o Kenny (223) do Cartman ruim (684).
  ruido        Pixels que discordam de 3 ou mais vizinhos opacos. Chapado tem
               ruido baixo; area difusa e dithering tem ruido alto.
  ilhas        Componentes soltos com menos de 2% da area do corpo: sujeira que
               sobrou do recorte, e que na animacao pisca.
  buracos      Transparencia cercada de desenho. Quase sempre e mascara
               furada, e na tela vira um ponto do fundo dentro do bicho.
  toque_lat    Pixels opacos encostados nas laterais do quadro. Base encostada
               e normal (o pe define o limite); LATERAL longa e caixa cortando
               o bicho, que aparece como borda reta vertical.
  tremor       Quanto o centro de massa anda entre quadros vizinhos. Alto e
               legitimo em quem pula (o Kenny), suspeito em quem so gesticula.

O veredito compara com um BASELINE — por padrao o Kenny, que e o sprite que
saiu certo de primeira porque a folha dele tinha fundo chapado. Comparar com
ele e o mais perto de "padrao da casa" que existe aqui.
"""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

from clw_format import ClwSprite, decode_frame, read_clw

# O fundo da tela do painel (ui.cpp, `BG`) e o Y em que o bicho da tela de
# RESET nasce (`R_RESET_Y`). O protótipo desenha nesses numeros, e nao em
# aproximacoes: enquadramento errado no papel vira enquadramento errado na tela.
FUNDO_TELA = (0x0C, 0x0E, 0x12)
PANEL_W, PANEL_H = 320, 480
R_RESET_Y = 54
RECADO_Y = PANEL_H - 74

# Quanto pior que o baseline ainda passa. 1.5 nao e gosto: entre o Kenny (223
# cores) e o Cartman corrigido a mao (~350) cabe folga para arte mais colorida,
# e o Cartman quebrado (684) fica de fora com margem.
TETO_RELATIVO = 1.5

# Area minima de um componente para nao ser ilha, como fracao do maior.
FRACAO_ILHA = 0.02


def _quadro_565(sprite: ClwSprite, indice: int) -> np.ndarray:
    return np.array(decode_frame(sprite, indice), dtype=np.int32).reshape(
        sprite.height, sprite.width)


def _rgb(valor: np.ndarray) -> np.ndarray:
    """RGB565 -> RGB888, do mesmo jeito que o painel expande."""
    r = ((valor >> 11) & 0x1F) * 255 // 31
    g = ((valor >> 5) & 0x3F) * 255 // 63
    b = (valor & 0x1F) * 255 // 31
    return np.dstack([r, g, b]).astype(np.uint8)


def _componentes(mascara: np.ndarray) -> list[int]:
    """Tamanho de cada componente conexo, sem scipy."""
    altura, largura = mascara.shape
    visto = np.zeros_like(mascara)
    tamanhos = []
    for y0, x0 in zip(*np.where(mascara)):
        if visto[y0, x0]:
            continue
        fila = deque([(y0, x0)])
        visto[y0, x0] = True
        conta = 0
        while fila:
            y, x = fila.popleft()
            conta += 1
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if (0 <= ny < altura and 0 <= nx < largura
                        and mascara[ny, nx] and not visto[ny, nx]):
                    visto[ny, nx] = True
                    fila.append((ny, nx))
        tamanhos.append(conta)
    return sorted(tamanhos, reverse=True)


def _buracos(opaco: np.ndarray) -> int:
    """Transparencia que nao se alcanca pela borda do quadro."""
    altura, largura = opaco.shape
    livre = ~opaco
    visto = np.zeros_like(livre)
    fila: deque[tuple[int, int]] = deque()
    for x in range(largura):
        for y in (0, altura - 1):
            if livre[y, x] and not visto[y, x]:
                visto[y, x] = True
                fila.append((y, x))
    for y in range(altura):
        for x in (0, largura - 1):
            if livre[y, x] and not visto[y, x]:
                visto[y, x] = True
                fila.append((y, x))
    while fila:
        y, x = fila.popleft()
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if (0 <= ny < altura and 0 <= nx < largura
                    and livre[ny, nx] and not visto[ny, nx]):
                visto[ny, nx] = True
                fila.append((ny, nx))
    return int((livre & ~visto).sum())


def medir(sprite: ClwSprite) -> dict:
    """Os numeros do arquivo inteiro."""
    cores: set[int] = set()
    ruido = opacos = ilhas = buracos = toque_lat = toque_base = 0
    centros = []

    for indice in range(sprite.frames):
        valor = _quadro_565(sprite, indice)
        opaco = valor != sprite.key
        opacos += int(opaco.sum())
        cores |= set(np.unique(valor[opaco]).tolist())

        # Discordancia da vizinhanca: 3 dos 4 vizinhos opacos com outra cor.
        discorda = np.zeros(valor.shape, np.int8)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            vizinho = np.roll(np.roll(valor, dy, 0), dx, 1)
            vizinho_opaco = np.roll(np.roll(opaco, dy, 0), dx, 1)
            discorda += ((vizinho != valor) & vizinho_opaco & opaco).astype(np.int8)
        ruido += int((discorda >= 3).sum())

        tamanhos = _componentes(opaco)
        if tamanhos:
            ilhas += sum(1 for t in tamanhos[1:] if t < FRACAO_ILHA * tamanhos[0])
        buracos += _buracos(opaco)

        toque_lat += int(opaco[:, 0].sum() + opaco[:, -1].sum())
        toque_base += int(opaco[0, :].sum() + opaco[-1, :].sum())

        ys, xs = np.where(opaco)
        centros.append((xs.mean(), ys.mean()))

    passos = [abs(centros[i][0] - centros[i - 1][0])
              + abs(centros[i][1] - centros[i - 1][1])
              for i in range(1, len(centros))]

    return {
        "quadros": sprite.frames,
        "caixa": f"{sprite.width}x{sprite.height}",
        "kb": round(sprite.byte_size / 1024, 1),
        "compressao": round(sprite.compression_ratio, 1),
        "cores": len(cores),
        "ruido_pct": round(100 * ruido / max(opacos, 1), 2),
        "ilhas": ilhas,
        "buracos": buracos,
        "toque_lat": toque_lat,
        "toque_base": toque_base,
        "tremor_med": round(float(np.mean(passos)), 1) if passos else 0.0,
        "tremor_max": round(float(np.max(passos)), 1) if passos else 0.0,
    }


def julgar(medido: dict, base: dict | None) -> list[str]:
    """As queixas. Lista vazia = passou.

    Quase tudo e julgado CONTRA O BASELINE, e nao contra zero. A primeira
    versao deste julgamento reprovava buraco e ilha em absoluto — e reprovava o
    proprio Kenny, que tem 5910 px de buraco e 1552 ilhas e e o sprite que saiu
    certo. Arte de recorte tem franja e vao entre pernas; o que importa e ser
    PIOR que o padrao da casa, nao ser perfeito.

    O toque lateral e a excecao: ali o numero nao depende da arte. Bicho
    encostado na lateral do quadro so acontece quando a caixa o cortou, e isso
    e defeito em qualquer sprite.
    """
    queixas = []
    if medido["toque_lat"] > 40:
        queixas.append(f"{medido['toque_lat']} px encostados na lateral "
                       f"(a caixa esta cortando o bicho)")
    if not base:
        return queixas
    for chave, rotulo in (("cores", "cores"), ("ruido_pct", "ruido"),
                          ("ilhas", "ilhas"), ("buracos", "buracos")):
        limite = base[chave] * TETO_RELATIVO
        if medido[chave] > limite:
            queixas.append(f"{rotulo}: {medido[chave]} contra {base[chave]} do "
                           f"baseline (teto {limite:.0f})")
    return queixas


def prototipar(sprite: ClwSprite, destino: Path, nome: str) -> None:
    """O que a placa vai mostrar: contato, moldura da tela e GIF no ritmo real.

    Nao e ilustracao: as cores saem do RGB565 gravado, o fundo e o `BG` do
    firmware e o bicho fica no Y de `R_RESET_Y`. E a ultima chance de ver o
    enquadramento antes de gastar uma viagem ate a placa.
    """
    destino.mkdir(parents=True, exist_ok=True)
    quadros = []
    for indice in range(sprite.frames):
        valor = _quadro_565(sprite, indice)
        rgb = _rgb(valor)
        opaco = valor != sprite.key
        imagem = Image.fromarray(np.where(opaco[..., None], rgb,
                                          np.array(FUNDO_TELA, np.uint8)))
        quadros.append((imagem, opaco))

    colunas = min(6, sprite.frames)
    linhas = (sprite.frames + colunas - 1) // colunas
    contato = Image.new("RGB", (sprite.width * colunas, sprite.height * linhas),
                        FUNDO_TELA)
    for indice, (imagem, _) in enumerate(quadros):
        contato.paste(imagem, ((indice % colunas) * sprite.width,
                               (indice // colunas) * sprite.height))
    contato.save(destino / f"{nome}-contato.png")

    telas = []
    for imagem, opaco in quadros:
        tela = Image.new("RGB", (PANEL_W, PANEL_H), FUNDO_TELA)
        recorte = Image.fromarray(np.dstack([np.asarray(imagem),
                                             (opaco * 255).astype(np.uint8)]),
                                  "RGBA")
        tela.paste(recorte, ((PANEL_W - sprite.width) // 2, R_RESET_Y), recorte)
        telas.append(tela)
    telas[0].save(destino / f"{nome}-tela.png")
    # O GIF anda no `frame_ms` do proprio arquivo: ver a animacao no ritmo
    # errado esconde justamente o tremor, que e o defeito que so o movimento
    # denuncia.
    telas[0].save(destino / f"{nome}-animado.gif", save_all=True,
                  append_images=telas[1:], duration=sprite.frame_ms, loop=0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arquivo", type=Path)
    parser.add_argument("--baseline", type=Path,
                        default=Path("sdcard/clawd/sp_kenny_reset.clw"),
                        help="sprite de referencia (padrao: %(default)s)")
    parser.add_argument("--saida", type=Path, default=None,
                        help="pasta para contato, tela e GIF")
    args = parser.parse_args()

    if not args.arquivo.is_file():
        parser.error(f"arquivo nao encontrado: {args.arquivo}")
    sprite = read_clw(args.arquivo)
    medido = medir(sprite)

    base = None
    if args.baseline and args.baseline.is_file() and \
            args.baseline.resolve() != args.arquivo.resolve():
        base = medir(read_clw(args.baseline))

    print(f"{args.arquivo.name}: {medido['caixa']}, {medido['quadros']} quadros, "
          f"{medido['kb']} KB (compressao {medido['compressao']}x)")
    for chave in ("cores", "ruido_pct", "ilhas", "buracos", "toque_lat",
                  "toque_base", "tremor_med", "tremor_max"):
        linha = f"  {chave:12s} {medido[chave]}"
        if base:
            linha += f"   (baseline {base[chave]})"
        print(linha)

    if args.saida:
        prototipar(sprite, args.saida, args.arquivo.stem)
        print(f"  prototipo em {args.saida}")

    queixas = julgar(medido, base)
    if queixas:
        print("REPROVADO:")
        for q in queixas:
            print(f"  - {q}")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
