#!/usr/bin/env python3
"""Compila a folha de poses do Cartman na animacao da tela de RESET.

Mesma ideia do `build_reset_kenny`: a folha e uma grade de poses SOLTAS, e o
trabalho e virar isso em ciclo — tirar o fundo, alinhar, remontar na ordem da
cena, reduzir para a caixa da tela e gravar o .clw.

O QUE MUDA E O FUNDO. A folha do Kenny vinha com o xadrez DESENHADO, branco e
uniforme: um flood fill pela borda resolvia. Esta vem com o Cartman num palco
escuro COM BRILHO, e o brilho tem a cor da roupa — o vermelho do casaco sangra
no fundo com o mesmo RGB do casaco. Nao existe uma cor de fundo para tirar, e
nem uma borda nitida para achar: o glow e desfocado e atravessa o contorno.

O que separa o bicho do palco e a PALETA CHAPADA do South Park. O desenho tem
sete cores e so elas; o palco e degrade. Entao a mascara nasce das cores exatas
(medidas na folha, nao chutadas), e o preto do contorno e do sapato entra por
VIZINHANCA — preto e preto tanto no sapato quanto no fundo, e so a distancia
ate uma cor chapada os separa.

As poses tambem nao estao em grade: elas se ENCOSTAM, e uma divisao em 6x2
cortaria bracos. A separacao sai das CABECAS — a mancha de pele do rosto e
grande, uma por bicho, e nunca encosta na do vizinho — e o resto do corpo vai
para a cabeca mais proxima ANDANDO DENTRO da mascara, nao em linha reta: assim
o braco que passa por tras do vizinho volta para o dono.

O ALINHAMENTO E PELA CABECA, e nao pelo chao como no Kenny. A sombra aqui e
projetada de lado, muda de tamanho e de direcao entre as poses — nao marca
chao nenhum. A cabeca do Cartman e metade dele: fixa-la deixa o corpo balancar
por baixo, que e exatamente como a serie anima.

Uso:

    python tools/build_reset_cartman.py "folha.png" sdcard/clawd/sp_cartman_reset.clw
"""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_sprite import crop_frames, rgba_to_565, symmetric_crop_box
from clw_format import TRANSPARENT_KEY, parse_clw, write_clw

# As tres ultimas etapas — reduzir, paletizar e montar o contato — sao as
# mesmas do Kenny (mesma tela, mesmo formato, mesmos limites), entao vem de la
# em vez de serem copiadas. O que e proprio deste bicho e o comeco.
from build_reset_kenny import FUNDO_TELA, montar_preview, paletizar, reduzir


# A grade da folha, so para dar nome as poses: a leitura e da esquerda para a
# direita, de cima para baixo. Ela NAO recorta nada — quem recorta e a cabeca.
COLUNAS, LINHAS = 6, 2

# A ordem da cena. A tela diz que um limite acabou de liberar, e o Cartman
# entrega a noticia: emburrado, percebe, abre o sorriso, gargalha, ergue os
# bracos e desce. O braco sobe e desce pelo mesmo quadro (10) para o alto nao
# ser um teleporte, e a volta fecha voltando a cara fechada (1 -> 0).
#
# A pose 5 NAO entra: ela nasceu encostada na borda direita da folha e perdeu
# 164 px de braco e mao. Remendar aquilo por espelho inventaria pose; a 1, que
# fecha o arco de volta ao emburrado, custa nada e esta inteira.
CICLO = (0, 2, 3, 6, 7, 8, 4, 9, 10, 11, 10, 1)

# 12 quadros a 150 ms fecham 1,8 s — a mesma volta do Kenny (10 x 165) e da
# danca antiga do Cartman (24 x 70). South Park e animacao de recorte: o ritmo
# baixo e o do original, nao economia.
FRAME_MS = 150

# A caixa de destino. `R_RESET_Y` (54) e o topo e o recado comeca em 406: 300
# px deixam 52 de respiro. Igual ao Kenny — este Cartman tambem e de corpo
# inteiro, e o busto antigo (240) o deixaria miudo no mesmo lugar.
ALTURA_ALVO = 300
LARGURA_MAX = 300

# Paleta unica para o ciclo inteiro. A arte vem com grao, e grao e veneno para
# RLE; a paleta comum tambem impede a cor de tremer de um quadro para o outro.
#
# 48, e o numero foi medido na CALCA. O corte mediano gasta entradas onde ha
# area, e area aqui e o casaco: com 24 ou 32 sobrava uma entrada so para o
# marrom, longe da cor real (108,66,42) — a calca saia mosqueada de vermelho
# (139,48,41). Com 48 ela cai em (106,64,41) e fica chapada. O preco sao ~190
# KB de arquivo, que cabem: o Cartman antigo pesava 1,4 MB.
CORES = 48

# Degraus do alpha antes da mistura com o fundo da tela. Ver `_degraus_de_alpha`
# — quatro seguram a silhueta suave e cortam a cauda de cores que nasce do
# anti-aliasing continuo.
NIVEIS_DE_ALPHA = 4

# A caixa do bicho, em pixels da folha e contada a partir do CENTRO DO ROSTO.
# Ela e fixa, e nao a uniao dos quadros como no Kenny: as sombras sao projetadas
# de lado e chegam a 240 px do rosto num quadro e 140 no outro — deixar a uniao
# mandar encolheria o Cartman para caber na sombra dele.
#
# Os numeros sao a MEDIDA das 12 poses mais folga: 134 acima, 229 abaixo (a
# sombra que vem junto com o sapato — ver ALCANCE_TINTA), 155 a esquerda (o
# braco estendido da pose 10) e 137 a direita. A primeira versao usou
# 138/145/200 por estimativa e cortou quatro poses — o corte aparece como um pe
# com borda RETA, que na tela se le como sprite quebrado.
#
# Mexeu no ALCANCE_TINTA ou no ciclo? Remeça. `_conferir_caixa` recusa compilar
# e diz o numero que falta, entao errar aqui custa uma mensagem e nao uma
# viagem ate a placa.
MEIA_LARGURA = 160
ACIMA = 140
ABAIXO = 235

# Quantos passos o marrom pode dar a partir do nucleo e ainda ser calca: 80 px
# cobrem a perna do casaco ao tornozelo.
ALCANCE_CALCA = 80

# Quantos passos o preto pode dar a partir da borda do corpo. 45 cobrem o
# sapato mais comprido com folga.
#
# ELE TRAZ SOMBRA JUNTO, E TUDO BEM. Duas tentativas de separar sapato de
# sombra por medida local falharam, e o registro fica para ninguem repetir:
#
#   - por mancha, com contraste de borda: contorno, sapatos e sombra saem
#     FUNDIDOS num componente so por pose (o contorno costura tudo), entao o
#     contraste medio da peca reprova a pose inteira — sapato inclusive.
#   - por semente na borda nitida: a arte e desfocada, e a fronteira do sapato
#     contra o chao iluminado nao passa de ~40 de degrau. Com limiar que exclui
#     a sombra, a semente nao encontra nem o sapato.
#
# O que decidiu: no Kenny a sombra FICA, e o resultado e o padrao da casa. Pe
# mordido salta aos olhos; sombra escura sobre fundo escuro, nao. Entre os dois
# erros, este e o barato.
ALCANCE_TINTA = 45

# Ate onde a tinta e OPACA. Do 30 ao 45 ela sai em degrade: o sapato inteiro
# cabe nos 30 primeiros passos, e o que vier depois e sombra — que deve sumir
# aos poucos, e nao terminar na linha onde a contagem acabou.
TINTA_OPACA = 30

# Faixa maxima que o remendo por espelho aceita refazer. Acima disso a pose e
# trocada no ciclo em vez de remendada: 80 px sao um braco encostado na borda,
# 164 sao a mao inteira mais o corpo, e espelhar aquilo inventa pose.
REMENDO_MAX = 110

# Area minima de uma mancha de pele para ela ser um rosto (a folha tem 12, de
# ~10 mil px cada). Serve so para separar rosto de mao e de faixa de gorro.
AREA_ROSTO = 8000


def _mascaras(folha: Image.Image
              ) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Nucleo, pele, marrom e preto de tinta, medidos na folha.

    Os limites nao sao gosto: sao as cores do desenho (casaco 206,21,48; gorro
    47,162,175; pele 252,215,175; calca 108,66,42; sapato 42,40,42) com folga
    para o grao. O que as separa do palco e sempre um canal — o glow vermelho
    tem o mesmo R do casaco, mas o verde sobe (18 na roupa, 43 no brilho).

    So o NUCLEO (roupa, cabeca, maos, olho) e inequivoco. O marrom da calca e o
    preto do sapato tem gemeos no palco: a sombra caida sobre o chao alaranjado
    da o mesmo marrom, e a sombra sobre o preto da o mesmo preto. Esses dois
    voltam depois, e so se conseguirem ANDAR ate o nucleo.

    O gorro exige B > G alem de B > R: o halo desfocado em volta dele e ciano
    ESVERDEADO — (64,150,145), com o verde acima do azul — e passava na regra
    antiga, dando pontas ao gorro e uma bolha ao lado do pompom. No tecido de
    verdade o azul ganha do verde em todo pixel, ate nas partes sombreadas.

    A calca exige TEXTURA alem de cor: o chao iluminado tem o RGB dela —
    (110,66,35) contra (108,66,42) — e nenhum canal os separa. O que os separa
    e a superficie: a arte e recorte de feltro, e o feltro tem grao e costuras
    (desvio local que passa de 10 nas bordas internas); o glow do chao e um
    degrade sem textura nenhuma (desvio < 1.5). O marrom so vale a menos de
    5 px de um ponto texturizado.
    """
    a = np.asarray(folha, dtype=np.int16)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    alto, baixo = a.max(axis=2), a.min(axis=2)

    pele = (r > 228) & (g > 188) & (b > 142) & (r - b > 45) & (r - b < 115)
    nucleo = (
        pele
        | ((r > 150) & (g < 38) & (b < 100) & (r - g > 115))          # casaco
        | ((b > 128) & (g > 120) & (r < 105) & (b - r > 70)
           & (b - g > 5))                                             # gorro
        | ((r > 222) & (g > 165) & (b < 80))                          # pompom e maos
        | (baixo > 222)                                               # olho
    )
    marrom = ((r > 78) & (r < 158) & (g > 42) & (g < 102) & (b > 20) & (b < 72)
              & (r - b > 48) & (g - b > 14) & (r - g > 24) & (r - g < 62))
    marrom &= _perto_de_textura(np.asarray(folha, dtype=float).mean(axis=2))
    tinta = (alto < 82) & (baixo > 26) & (alto - baixo < 16)
    return nucleo, pele, marrom, tinta


def _perto_de_textura(luminancia: np.ndarray, *, janela: int = 5,
                      limiar: float = 4.0, alcance: int = 11) -> np.ndarray:
    """Pixels a menos de `alcance/2` de um ponto com desvio local alto.

    O desvio e da janela `janela` x `janela`, via imagem integral — sem scipy.
    """
    def media(quadro: np.ndarray, lado: int) -> np.ndarray:
        soma = np.cumsum(quadro, axis=0)
        soma = np.vstack([soma[lado - 1:lado], soma[lado:] - soma[:-lado]])
        soma = np.cumsum(soma, axis=1)
        soma = np.hstack([soma[:, lado - 1:lado], soma[:, lado:] - soma[:, :-lado]])
        return soma / (lado * lado)

    lado = janela
    m1 = media(luminancia, lado)
    m2 = media(luminancia ** 2, lado)
    desvio = np.sqrt(np.maximum(m2 - m1 ** 2, 0.0))
    texturado = np.zeros(luminancia.shape, bool)
    texturado[lado - 1:, lado - 1:] = desvio > limiar
    return _filtro(texturado, ImageFilter.MaxFilter(alcance))


def _filtro(mascara: np.ndarray, filtro) -> np.ndarray:
    """Morfologia via PIL: `MinFilter` erode, `MaxFilter` dilata."""
    im = Image.fromarray((mascara * 255).astype(np.uint8), "L").filter(filtro)
    return np.asarray(im) > 127


def _abrir(mascara: np.ndarray, lado: int) -> np.ndarray:
    return _filtro(_filtro(mascara, ImageFilter.MinFilter(lado)),
                   ImageFilter.MaxFilter(lado))


def _fechar(mascara: np.ndarray, lado: int) -> np.ndarray:
    return _filtro(_filtro(mascara, ImageFilter.MaxFilter(lado)),
                   ImageFilter.MinFilter(lado))


def _vizinhos(mascara: np.ndarray) -> np.ndarray:
    """A mascara mais a casca de um pixel em volta dela."""
    return _filtro(mascara, ImageFilter.MaxFilter(3))


def _alcancar(semente: np.ndarray, alvo: np.ndarray, passos: int) -> np.ndarray:
    """Cresce a semente dentro do alvo, um pixel por passo."""
    atual = semente
    for _ in range(passos):
        crescido = _vizinhos(atual) & alvo
        if not crescido.sum() > atual.sum():
            break
        atual = crescido
    return atual


def _preencher_buracos(mascara: np.ndarray) -> np.ndarray:
    """Fecha o que nao se alcanca pela borda — o branco do olho, por exemplo."""
    return ~_inundar(~mascara)


def _inundar(livre: np.ndarray) -> np.ndarray:
    """O que se alcanca a partir da borda da imagem andando pelo `livre`."""
    altura, largura = livre.shape
    visto = np.zeros_like(livre)
    fila: deque[tuple[int, int]] = deque()

    def semear(y: int, x: int) -> None:
        if livre[y, x] and not visto[y, x]:
            visto[y, x] = True
            fila.append((y, x))

    for x in range(largura):
        semear(0, x)
        semear(altura - 1, x)
    for y in range(altura):
        semear(y, 0)
        semear(y, largura - 1)
    while fila:
        y, x = fila.popleft()
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if 0 <= ny < altura and 0 <= nx < largura:
                semear(ny, nx)
    return visto


def _manchas(mascara: np.ndarray, area_minima: int) -> list[np.ndarray]:
    """Componentes conexos da mascara, do maior para o menor."""
    altura, largura = mascara.shape
    visto = np.zeros_like(mascara)
    achados: list[np.ndarray] = []
    for y0, x0 in zip(*np.where(mascara)):
        if visto[y0, x0]:
            continue
        mancha = np.zeros_like(mascara)
        fila = deque([(y0, x0)])
        visto[y0, x0] = True
        while fila:
            y, x = fila.popleft()
            mancha[y, x] = True
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if (0 <= ny < altura and 0 <= nx < largura
                        and mascara[ny, nx] and not visto[ny, nx]):
                    visto[ny, nx] = True
                    fila.append((ny, nx))
        if mancha.sum() >= area_minima:
            achados.append(mancha)
    achados.sort(key=lambda m: -int(m.sum()))
    return achados


def _dono_de_cada_pixel(bicho: np.ndarray, rostos: list[np.ndarray]) -> np.ndarray:
    """Reparte a mascara entre os rostos pela distancia ANDANDO dentro dela.

    Em linha reta (Voronoi) o corte cai no meio do vao e leva junto o braco do
    vizinho que passa por tras. Andando pela propria mascara, o braco continua
    mais perto do ombro de onde ele sai.
    """
    altura, largura = bicho.shape
    dono = np.zeros((altura, largura), np.int16)
    fila: deque[tuple[int, int]] = deque()
    for indice, rosto in enumerate(rostos, 1):
        for y, x in zip(*np.where(rosto)):
            dono[y, x] = indice
            fila.append((y, x))
    while fila:
        y, x = fila.popleft()
        marca = dono[y, x]
        for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
            if (0 <= ny < altura and 0 <= nx < largura
                    and bicho[ny, nx] and not dono[ny, nx]):
                dono[ny, nx] = marca
                fila.append((ny, nx))
    return dono


def separar_poses(folha: Image.Image) -> list[tuple[np.ndarray, float, float]]:
    """Uma mascara por pose, com o centro do rosto de cada uma."""
    nucleo, pele, marrom, tinta = _mascaras(folha)
    nucleo = _abrir(nucleo, 3)

    # A calca desce a partir da barra do casaco, contada em passos. A sombra
    # caida no chao alaranjado tem o mesmo marrom, mas nasce longe do nucleo e
    # nao chega ate aqui — e a contagem que a corta.
    cor = nucleo | _alcancar(_vizinhos(nucleo) & marrom, marrom, ALCANCE_CALCA)

    # O preto vem depois: a linha do desenho, o sapato inteiro e o pedaco de
    # sombra que encosta no pe — que fica, como no Kenny, e da chao ao bicho.
    # O alpha dele cai com a distancia (ver `_desvanecer`), entao a sombra
    # some em degrade em vez de terminar na linha reta onde a contagem acabou.
    fade = _desvanecer(cor, tinta, TINTA_OPACA, ALCANCE_TINTA)
    estrito = cor | (fade > 0)
    bicho = _preencher_buracos(_fechar(estrito, 7))

    # O aparo: o fechamento gruda na borda o que estiver a ate 7 px — perto do
    # pe isso e sombra clara demais para a regra da tinta e escura demais para
    # sumir na tela. Fica o que esta a 2 px de um pixel aprovado; os 2 px
    # preservam o anti-aliasing da borda.
    bicho &= _filtro(estrito, ImageFilter.MaxFilter(5))
    bicho = _abrir(_preencher_buracos(bicho), 3)

    rostos = _manchas(_abrir(pele, 9), AREA_ROSTO)
    if len(rostos) != COLUNAS * LINHAS:
        raise ValueError(f"achei {len(rostos)} rostos na folha; esperava "
                         f"{COLUNAS * LINHAS}")

    # A ordem da grade: linha de cima primeiro, e da esquerda para a direita
    # dentro dela. A altura do rosto sozinha nao serve para dizer a linha —
    # duas poses da mesma fileira ficam 20 px deslocadas —, entao a fileira sai
    # da metade da folha.
    meio = folha.height / 2
    def posicao(rosto: np.ndarray) -> tuple[int, int]:
        ys, xs = np.where(rosto)
        return (0 if ys.mean() < meio else 1, int(xs.min()))
    rostos.sort(key=posicao)

    # O alpha final: 255 no corpo, o degrade de `_desvanecer` na tinta longe.
    opacidade = np.where(cor | (bicho & (fade == 0)), 255, fade).astype(np.uint8)

    dono = _dono_de_cada_pixel(bicho, rostos)
    poses = []
    for indice, rosto in enumerate(rostos, 1):
        ys, xs = np.where(rosto)
        poses.append(((dono == indice) * opacidade, (xs.min() + xs.max()) / 2,
                      (ys.min() + ys.max()) / 2))
    return poses


def _remendar_borda(recorte: Image.Image, faltando: int) -> Image.Image:
    """Refaz por ESPELHO a faixa que a folha cortou na lateral.

    Duas poses nasceram encostadas na borda da imagem e perderam parte do
    braco: onde a arte acaba, a mascara termina numa reta vertical, e reta
    vertical no meio de um bicho redondo le como sprite quebrado.

    O Cartman de bracos erguidos e simetrico em torno do eixo do rosto — que e
    o centro desta caixa —, entao a faixa perdida e copiada espelhada do outro
    lado. Vale so para faixa ESTREITA: reconstruir meio bicho assim inventaria
    pose, e para isso a saida certa e trocar a pose no ciclo.

    `faltando` e positivo para faixa perdida a direita, negativo a esquerda.
    """
    if not faltando:
        return recorte
    largura = recorte.width
    espelho = recorte.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    saida = recorte.copy()
    if faltando > 0:
        caixa = (largura - faltando, 0, largura, recorte.height)
    else:
        caixa = (0, 0, -faltando, recorte.height)
    saida.paste(espelho.crop(caixa), (caixa[0], 0))
    # A pose original manda onde ela existe: o espelho so preenche o vazio.
    saida.alpha_composite(recorte)
    return saida


def _desvanecer(cor: np.ndarray, tinta: np.ndarray, dentro: int,
                fim: int) -> np.ndarray:
    """Alpha da tinta por distancia ao corpo: cheio perto, zero longe.

    O alcance por passos termina SECO — a sombra e cortada na linha onde a
    contagem acabou, e uma barra reta embaixo do pe se le como pedaco faltando.
    Aqui os ultimos passos saem em degrade: o sapato (perto) fica opaco, e a
    sombra desvanece ate sumir, que e o que ela ja faz no desenho.
    """
    alpha = np.zeros(tinta.shape, np.uint8)
    frente = _vizinhos(cor) & tinta
    alcancado = frente.copy()
    alpha[frente] = 255
    for passo in range(1, fim + 1):
        frente = _vizinhos(alcancado) & tinta & ~alcancado
        if not frente.any():
            break
        alcancado |= frente
        if passo <= dentro:
            alpha[frente] = 255
        else:
            resto = (fim - passo) / max(1, fim - dentro)
            alpha[frente] = int(255 * resto)
    return alpha


def _degraus_de_alpha(quadro: Image.Image,
                      niveis: int = NIVEIS_DE_ALPHA) -> Image.Image:
    """Prende o alpha a poucos degraus antes da mistura com o fundo.

    O .clw nao guarda alpha: cada pixel ja entra misturado com a cor da tela.
    Alpha continuo, entao, vira COR nova a cada valor — e medido neste sprite,
    os 1,7% de pixels com alpha parcial geravam 459 das 684 cores do arquivo,
    mais que a area opaca inteira. Cor demais e o que o olho le como detalhe
    estourado, e e veneno para o RLE.

    Quatro degraus mantem a silhueta suave (o degrade da borda continua
    existindo, em quatro passos) e cortam a cauda de cores pela raiz. Menos que
    isso serrilha; mais nao muda a tela e devolve o ruido.
    """
    dados = np.asarray(quadro).copy()
    alpha = dados[..., 3].astype(np.int32)
    passo = 255 / (niveis - 1)
    dados[..., 3] = (np.round(alpha / passo) * passo).astype(np.uint8)
    return Image.fromarray(dados, "RGBA")


def _conferir_caixa(poses: list[tuple[np.ndarray, float, float]]) -> None:
    """Recusa a caixa que corta qualquer pose, dizendo o numero que falta.

    Existe porque o corte e SILENCIOSO: nada falha, e o pe cortado so aparece
    na placa, como uma borda reta que se le como sprite quebrado. Errar aqui
    tem de custar uma mensagem, e nao uma viagem ate o cartao.
    """
    faltas = []
    for indice, (mascara, centro_x, centro_y) in enumerate(poses):
        ys, xs = np.where(mascara)
        for nome, medido, limite in (
            ("ACIMA", centro_y - ys.min(), ACIMA),
            ("ABAIXO", ys.max() - centro_y, ABAIXO),
            ("MEIA_LARGURA", max(centro_x - xs.min(), xs.max() - centro_x),
             MEIA_LARGURA),
        ):
            if medido > limite:
                faltas.append(f"pose {indice}: {nome} precisa de "
                              f"{int(medido) + 5}, tem {limite}")
    if faltas:
        raise ValueError("a caixa corta o bicho — " + "; ".join(faltas))


def recortar(folha: Image.Image,
             poses: list[tuple[np.ndarray, float, float]]) -> list[Image.Image]:
    """Cada pose na caixa fixa, com o rosto sempre no mesmo lugar."""
    rgb = np.asarray(folha)
    largura, altura = 2 * MEIA_LARGURA, ACIMA + ABAIXO
    quadros = []
    for mascara, centro_x, centro_y in poses:
        recorte = Image.new("RGBA", (largura, altura), (0, 0, 0, 0))
        # A mascara ja vem como ALPHA de 0 a 255: o corpo e opaco e a sombra
        # desvanece na ponta (ver `_desvanecer`).
        pose = Image.fromarray(np.dstack([rgb, mascara.astype(np.uint8)]), "RGBA")
        # A caixa pode passar da folha (a fileira encosta na borda dela), entao
        # a pose e COLADA deslocada e o que faltar fica transparente.
        recorte.paste(pose, (round(MEIA_LARGURA - centro_x),
                             round(ACIMA - centro_y)), pose)

        # A pose que encosta na borda da FOLHA perdeu braco ali. O quanto falta
        # e a distancia da borda ate onde a caixa termina — e ela e remendada
        # pelo espelho do outro lado, nao deixada com a reta vertical do corte.
        colunas = mascara.shape[1]
        falta = 0
        if xs_toca_direita(mascara, colunas):
            falta = round(MEIA_LARGURA - (colunas - 1 - centro_x))
        elif xs_toca_esquerda(mascara):
            falta = -round(MEIA_LARGURA - centro_x)
        if 0 < abs(falta) <= REMENDO_MAX:
            recorte = _remendar_borda(recorte, falta)
        quadros.append(recorte)
    return quadros


def xs_toca_direita(mascara: np.ndarray, colunas: int) -> bool:
    return bool(mascara[:, colunas - 4:].any())


def xs_toca_esquerda(mascara: np.ndarray) -> bool:
    return bool(mascara[:, :4].any())


def compilar(origem: Path, saida: Path, *, frame_ms: int, cores: int,
             altura_alvo: int, largura_max: int, preview: Path | None) -> None:
    with Image.open(origem) as aberta:
        folha = aberta.convert("RGB")

    poses = separar_poses(folha)
    for indice in CICLO:
        if indice >= len(poses):
            raise ValueError(f"o ciclo pede a pose {indice}; a folha tem "
                             f"{len(poses)}")

    _conferir_caixa(poses)
    recortes = recortar(folha, poses)
    quadros = [recortes[indice] for indice in CICLO]

    fator = altura_alvo / quadros[0].height
    destino = (round(quadros[0].width * fator), altura_alvo)
    if destino[0] > largura_max:
        fator = largura_max / quadros[0].width
        destino = (largura_max, round(quadros[0].height * fator))
    quadros = [_degraus_de_alpha(reduzir(quadro, destino)) for quadro in quadros]
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("origem", type=Path, help="folha PNG com as poses")
    parser.add_argument("saida", type=Path, help="arquivo .clw de saida")
    parser.add_argument("--altura", type=int, default=ALTURA_ALVO)
    parser.add_argument("--largura-max", type=int, default=LARGURA_MAX)
    parser.add_argument("--frame-ms", type=int, default=FRAME_MS)
    parser.add_argument("--cores", type=int, default=CORES)
    parser.add_argument("--preview", type=Path, default=None,
                        help="grava um contato dos quadros ja reduzidos")
    args = parser.parse_args()

    if not args.origem.is_file():
        parser.error(f"arquivo nao encontrado: {args.origem}")
    try:
        compilar(args.origem, args.saida, frame_ms=args.frame_ms,
                 cores=args.cores, altura_alvo=args.altura,
                 largura_max=args.largura_max, preview=args.preview)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
