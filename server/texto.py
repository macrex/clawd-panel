#!/usr/bin/env python3
"""Texto de terminal virando texto que a placa consegue desenhar.

POR QUE ISTO EXISTE
A fonte embutida do Arduino_GFX so tem ASCII, e ela imprime BYTE a byte: cada
caractere acentuado do UTF-8 vira dois glifos soltos na tela. Medido na tela em
11/08/2026, "As mudanças estão" saiu como "As mudan|ºas est|Ão" — a frase certa,
ilegivel.

POR QUE AQUI E NAO NO FIRMWARE
E a API que da forma a este texto para o painel; ela ja o corta em bytes pelo
mesmo motivo (ver ROTULO_MAX_B). Do outro lado custaria um decodificador de
UTF-8 e uma tabela dentro do laco de desenho, pelo mesmo resultado — e o laco de
desenho e onde esta placa nao tem folga.

POR QUE NAO SO `encode("ascii", "ignore")`
Descartar caractere que nao decompoe destroi o desenho: uma caixa vira uma linha
de nada e o travessao some colando as palavras vizinhas. Cada familia tem um
substituto ASCII que preserva a FORMA, e nao so os bytes.
"""

import unicodedata

# Pontuacao tipografica: o NFKD nao decompoe nenhuma destas, entao sem a tabela
# elas sumiriam. "Sim — com o tunel" viraria "Sim  com o tunel".
TIPOGRAFICOS = {
    "—": "-", "–": "-", "‑": "-",
    "“": '"', "”": '"', "„": '"',
    "‘": "'", "’": "'",
    "…": "...", "•": "*", "→": "->", "←": "<-", "×": "x",
}

# Desenho de caixa e simbolos que as TUIs usam o tempo todo. Sem isto, a tela de
# terminal do painel perderia toda a moldura que o Claude Code desenha e o texto
# ficaria boiando sem estrutura nenhuma.
#
# `─` vira `-` e nao ` ` de proposito: uma regua ainda tem que parecer uma
# regua. O objetivo e a forma sobreviver a traducao, nao o byte.
CAIXA = {
    "─": "-", "━": "-", "═": "=", "╌": "-", "┄": "-", "┈": "-",
    "│": "|", "┃": "|", "║": "|", "╎": "|", "┆": "|", "┊": "|",
    "┌": "+", "┐": "+", "└": "+", "┘": "+", "├": "+", "┤": "+",
    "┬": "+", "┴": "+", "┼": "+", "╭": "+", "╮": "+", "╰": "+", "╯": "+",
    "╔": "+", "╗": "+", "╚": "+", "╝": "+", "╠": "+", "╣": "+",
    "╦": "+", "╩": "+", "╬": "+",
    "█": "#", "▉": "#", "▊": "#", "▋": "#", "▌": "#", "▍": "#", "▎": "#",
    "▏": "|", "░": ".", "▒": ":", "▓": "#", "▀": "-", "▄": "_",
}

# Simbolos que o Claude Code e o Codex desenham na propria interface. Sem eles,
# o prompt perde o `>` que diz onde voce esta digitando.
SIMBOLOS = {
    "❯": ">", "›": ">", "▶": ">", "⏵": ">", "❮": "<", "‹": "<",
    "⏺": "o", "●": "o", "◉": "o", "○": "o", "◯": "o",
    "✓": "v", "✔": "v", "☑": "v", "✅": "v",
    "✗": "x", "✘": "x", "❌": "x", "☒": "x",
    "⚠": "!", "⚡": "!", "❗": "!", "‼": "!!",
    "❖": "*", "◆": "*", "◇": "*", "✦": "*", "✳": "*", "✻": "*", "✽": "*",
    "★": "*", "☆": "*", "✢": "*",
    "⎇": "@", "⌥": "@", "⌘": "@", "⏎": "<", "⌫": "<",
    "·": ".", "∙": ".", "⋮": ":", "⏳": "~", "◔": "o", "◑": "o", "◕": "o",
}

_TABELA = {**TIPOGRAFICOS, **CAIXA, **SIMBOLOS}


def so_ascii(texto):
    """Tudo que a fonte da placa nao desenha, trocado por algo que ela desenha.

    Ordem importa: a tabela roda ANTES do NFKD porque o NFKD nao decompoe nada
    do que esta nela e o `ignore` do encode as descartaria em silencio.
    """
    if not texto:
        return ""
    for de, para in _TABELA.items():
        if de in texto:
            texto = texto.replace(de, para)
    # NFKD separa o acento da letra; o encode descarta o acento, ja solto.
    saida = unicodedata.normalize("NFKD", texto).encode("ascii", "ignore").decode()
    # Sobrou algo que nao e imprimivel (controle, escape que passou): vira
    # espaco em vez de sumir, para nao encolher a linha e desalinhar a coluna.
    return "".join(c if c == "\n" or 0x20 <= ord(c) < 0x7F else " " for c in saida)


def cortar(texto, limite):
    """Corta em `limite` BYTES sem partir caractere UTF-8.

    Por byte e nao por caractere porque o firmware guarda estes campos em buffer
    de tamanho fixo e trunca por byte. Cortar aqui, no lugar certo, evita meio
    caractere acentuado virar lixo na tela.

    A marca de corte e "..." e nao "…" de proposito: ela sai depois da
    transliteracao, entao um caractere nao-ASCII aqui reintroduziria na ultima
    linha exatamente o lixo que `so_ascii` acabou de tirar.
    """
    cru = texto.encode()
    if len(cru) <= limite:
        return texto
    return cru[:limite].decode(errors="ignore").rstrip() + "..."
