#!/usr/bin/env python3
"""Texto de terminal COM COR, preparado para a placa desenhar.

POR QUE ISTO EXISTE
O `texto.py` reduz o texto a ASCII e o `terminal.py` o quebra na largura da
placa — e os dois foram escritos para texto CRU, sem sequencias de escape. Com
`herdr pane read --format ansi` cada linha vem cheia de SGR, e as duas operacoes
passam a mentir:

- `so_ascii` nao toca no escape (ele ja e ASCII), mas ao substituir "─" por "-"
  ele MUDA a contagem de colunas de um jeito que o corte precisa saber.
- `linha[:cols]` conta BYTES, e uma linha de 52 colunas visiveis pode ter 400
  bytes de SGR. Cortar em 52 bytes deixaria a tela com meia sequencia de escape
  e nenhuma letra.

Este modulo faz as duas coisas contando COLUNA VISIVEL e carregando o estado de
cor atraves dos cortes: cada pedaco sai com o SGR que valia no comeco dele, para
que a placa possa desenhar qualquer linha sem ter visto as anteriores.

POR QUE AQUI E NAO NO FIRMWARE
Mesma razao do `texto.py`: e a API que da forma ao texto para o painel, e o laco
de desenho da placa e onde nao ha folga. O firmware recebe linhas ja na largura
certa e so precisa interpretar SGR — que e o `lib/termparse`, portado do
herdr-assist.

O DESENHO DE CAIXA SOBREVIVE
`so_ascii(..., caixa=True)`: a fonte embutida da placa e a CP437 e ela TEM os
glifos de caixa — quem os converte e o `lib/cp437` do firmware. Reduzi-los aqui
a `-|+` era o que fazia a tabela do Claude Code sair em tracinhos.

O QUE ELE NAO FAZ
Nao emula terminal. Cursor, OSC, DECSET e afins sao CONSUMIDOS e descartados: o
herdr ja entrega o viewport reflowado, entao o que sobra e texto mais cor.
"""

import re

ESC_CH = chr(27)

from texto import so_ascii

# CSI: ESC [ ... letra final. So o `m` (SGR) e preservado; o resto e descartado.
_CSI = re.compile(r"\x1b\[([0-9;:?]*)([A-Za-z])")
# OSC: ESC ] ... BEL ou ESC \. O Claude Code usa para titulo de janela.
_OSC = re.compile(r"\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)")
# Escapes de dois caracteres (ESC ( B, ESC = ...) e o que sobrar solto.
_ESC2 = re.compile(r"\x1b[()#][0-9A-Za-z]|\x1b[=><78]")


def limpar(bruto):
    """Tira tudo o que nao e texto nem SGR. Devolve texto + sequencias `m`."""
    if not bruto:
        return ""
    s = _OSC.sub("", bruto)
    s = _ESC2.sub("", s)

    def _keep(m):
        return m.group(0) if m.group(2) == "m" else ""

    return _CSI.sub(_keep, s)


def _normalizar(params):
    """Os parametros de um SGR, com o vazio valendo 0 (reset), como no padrao."""
    if params == "":
        return ["0"]
    # `38:2::R:G:B` (subparametro por dois-pontos) aparece em alguns emuladores.
    # Vira o formato por ponto-e-virgula, que e o unico que a placa parseia.
    return params.replace(":", ";").split(";")


def _aplicar(estado, params):
    """Atualiza a lista de parametros SGR ativos.

    O estado e a lista de codigos que a placa precisa ver para reconstruir a
    aparencia — nao um dicionario de atributos. Guardar assim mantem este modulo
    ignorante sobre o que cada codigo significa: quem interpreta e o firmware, e
    codigo novo atravessa sem precisar de mudanca aqui.
    """
    p = _normalizar(params)
    i = 0
    while i < len(p):
        c = p[i]
        if c in ("", "0"):
            estado.clear()
            i += 1
            continue
        # Cor estendida: 38/48 seguido de 5;N (256) ou 2;R;G;B (truecolor). Sao
        # varios parametros para UM atributo, e precisam entrar juntos.
        if c in ("38", "48") and i + 1 < len(p):
            n = 3 if p[i + 1] == "5" else (5 if p[i + 1] == "2" else 1)
            trecho = p[i:i + n]
            estado[:] = [e for e in estado if not e.startswith(c + ";")]
            estado.append(";".join(trecho))
            i += n
            continue
        # Um codigo por vez. O mesmo GRUPO substitui o anterior: dois `31`
        # seguidos de um `32` tem que deixar so o verde, senao o estado cresce
        # sem limite ao longo de uma tela inteira.
        grupo = _grupo(c)
        estado[:] = [e for e in estado if _grupo(e.split(";")[0]) != grupo]
        estado.append(c)
        i += 1
    return estado


def _grupo(codigo):
    """A que atributo um codigo SGR pertence. Codigo desconhecido e ele mesmo."""
    try:
        n = int(codigo)
    except ValueError:
        return codigo
    if n in (1, 2, 22):
        return "peso"
    if n in (3, 23):
        return "italico"
    if n in (4, 24):
        return "sublinhado"
    if n in (7, 27):
        return "reverso"
    if n in (9, 29):
        return "riscado"
    if 30 <= n <= 37 or 90 <= n <= 97 or n == 39 or n == 38:
        return "fg"
    if 40 <= n <= 47 or 100 <= n <= 107 or n == 49 or n == 48:
        return "bg"
    return codigo


def _sgr(estado):
    return "\x1b[0m" + ("\x1b[" + ";".join(estado) + "m" if estado else "")


def quebrar(linha, cols):
    """Uma linha ANSI em pedacos de `cols` COLUNAS VISIVEIS.

    Cada pedaco comeca com o SGR completo que valia ali, entao a placa pode
    desenhar qualquer linha isoladamente. Devolve [] para linha sem conteudo.
    """
    if cols <= 0:
        return []
    estado, saida, buf, col = [], [], [], 0
    # O SGR que valia quando o pedaco COMECOU. Capturado no primeiro caractere
    # de texto dele, e nao antes do laco: a cor de uma linha e aberta antes da
    # primeira letra, e capturar cedo demais mandaria o pedaco de cima sem cor
    # nenhuma enquanto o de baixo sairia colorido.
    inicio = None

    i, n = 0, len(linha)
    while i < n:
        if linha[i] == "\x1b":
            m = _CSI.match(linha, i)
            if m:
                if m.group(2) == "m":
                    _aplicar(estado, m.group(1))
                i = m.end()
                continue
            i += 1          # escape solto: consome o ESC e segue
            continue

        ch = linha[i]
        i += 1
        if ch in "\r\x00":
            continue
        if not buf:
            inicio = list(estado)
        buf.append(ch)
        col += 1
        if col >= cols:
            saida.append(_sgr(inicio) + "".join(buf))
            buf, col, inicio = [], 0, None

    if buf and "".join(buf).strip():
        saida.append(_sgr(inicio or []) + "".join(buf))
    elif not saida:
        return []
    return saida


def ascii_preservando(linha):
    """`so_ascii` aplicado SO ao texto, com as sequencias intactas.

    Passar a linha inteira pelo `so_ascii` destroi o proprio escape: o ESC
    (0x1B) e caractere de controle e ele o substitui por espaco. O resultado sai
    como "ESC[0m [0m [38;5;7m..." — o primeiro escape sobrevive e os demais
    viram texto, que e o pior dos dois mundos: nem cor nem alinhamento.
    """
    if "" not in linha:
        return so_ascii(linha, caixa=True)
    out, i, n = [], 0, len(linha)
    while i < n:
        if linha[i] == "":
            m = _CSI.match(linha, i)
            if m:
                out.append(m.group(0))
                i = m.end()
                continue
            i += 1
            continue
        j = linha.find("", i)
        if j < 0:
            j = n
        out.append(so_ascii(linha[i:j], caixa=True))
        i = j
    return "".join(out)


def preparar(bruto, cols):
    """Linhas ANSI prontas para a placa: ASCII, sem padding, em `cols` colunas.

    A quebra e DURA e nao por palavra, como no `terminal.preparar` original:
    conteudo de terminal e alinhado por coluna, e quebrar na palavra desalinha
    tabela, diff e barra de progresso — que sao o que se quer ler aqui.
    """
    if not bruto:
        return []

    fora = []
    for linha in limpar(bruto.replace("\t", "    ")).split("\n"):
        # A reducao a ASCII vem ANTES da contagem de colunas, porque ela muda o
        # tamanho: "…" vira "..." e ganha duas colunas. Contar antes deixaria o
        # corte errado justamente nas linhas com box-drawing, que sao as mais
        # largas do Claude Code.
        linha = ascii_preservando(linha)
        # `rstrip` do texto visivel, e nao da linha inteira: o herdr devolve a
        # linha na largura do pane, e sem isto cada linha de conteudo viraria
        # uma dezena de linhas em branco (63 caracteres dentro de 838).
        if not _visivel(linha).strip():
            fora.append("")
            continue
        pedacos = quebrar(_rstrip_visivel(linha), cols)
        fora.extend(pedacos or [""])

    while fora and not _visivel(fora[-1]).strip():
        fora.pop()
    return fora


def _visivel(linha):
    """So o texto, sem nenhum escape. Para medir e para testar."""
    return _CSI.sub("", linha)


def _rstrip_visivel(linha):
    """Tira o espaco a direita SEM tirar o SGR que vem depois dele."""
    vis = _visivel(linha)
    sobra = len(vis) - len(vis.rstrip())
    if not sobra:
        return linha
    # Anda de tras para a frente removendo `sobra` espacos visiveis.
    out, restam, i = [], sobra, len(linha)
    while i > 0 and restam:
        if linha[i - 1] == " ":
            restam -= 1
            i -= 1
            continue
        m = None
        for k in range(max(0, i - 24), i):
            mm = _CSI.match(linha, k)
            if mm and mm.end() == i:
                m = mm
                break
        if m:
            out.append(linha[m.start():m.end()])
            i = m.start()
            continue
        break
    return linha[:i] + "".join(reversed(out))
