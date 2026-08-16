#!/usr/bin/env python3
"""Tabela de preco dos modelos, e a conta que transforma token em dolar.

DISCIPLINA (a mesma de weather.py e works.py)
  - So stdlib.
  - Sem estado e sem I/O: uma tabela e uma conta.
  - Nunca levanta excecao para fora.

O QUE ESTE MODULO SE RECUSA A FAZER
Adivinhar. Modelo fora da tabela devolve None, e None se propaga ate a saida
como custo desconhecido. Um custo inventado para um modelo novo e PIOR do que
um campo vazio: nao da para distinguir de um custo medido.

PRECO DE HOJE, E NAO DA EPOCA
O custo de um dia antigo e calculado com esta tabela. Se um preco mudar, o
passado muda junto. `VIGENTE` viaja com o numero ate a API justamente para que
"calculado a preco de quando" seja pergunta com resposta.
"""

# Data em que a tabela foi conferida. Sai no /uso junto do custo.
VIGENTE = "2026-08-04"

# US$ por milhao de tokens.
#   preco  : (entrada, saida) no modo normal
#   rapido : (entrada, saida) no modo rapido, para quem tem
#   promo  : (data_limite, (entrada, saida)) enquanto valer
#
# A promocao do Sonnet 5 e a UNICA excecao a regra "preco de hoje". Ela tem data
# de fim conhecida e 40 dolares medidos em jogo no acervo desta maquina; nao
# abre precedente para versionar o resto da tabela por data.
TABELA = {
    "claude-fable-5":    {"rotulo": "Fable 5",    "preco": (10.0, 50.0)},
    "claude-mythos-5":   {"rotulo": "Mythos 5",   "preco": (10.0, 50.0)},
    "claude-opus-5":     {"rotulo": "Opus 5",     "preco": (5.0, 25.0),
                          "rapido": (10.0, 50.0)},
    "claude-opus-4-8":   {"rotulo": "Opus 4.8",   "preco": (5.0, 25.0),
                          "rapido": (10.0, 50.0)},
    "claude-opus-4-7":   {"rotulo": "Opus 4.7",   "preco": (5.0, 25.0)},
    "claude-opus-4-6":   {"rotulo": "Opus 4.6",   "preco": (5.0, 25.0)},
    "claude-sonnet-5":   {"rotulo": "Sonnet 5",   "preco": (3.0, 15.0),
                          "promo": ("2026-08-31", (2.0, 10.0))},
    "claude-sonnet-4-6": {"rotulo": "Sonnet 4.6", "preco": (3.0, 15.0)},
    "claude-haiku-4-5":  {"rotulo": "Haiku 4.5",  "preco": (1.0, 5.0)},
}

# Multiplicadores sobre o preco de ENTRADA.
#
# O de uma hora nao e detalhe: 99,6% do cache escrito nesta maquina e de 1h.
# Usar 1.25 para tudo subestimaria essa linha em 60%.
CACHE_READ = 0.10
CACHE_5M = 1.25
CACHE_1H = 2.00

# As cinco categorias de token que a conta conhece.
CAMPOS = ("input", "output", "cache_5m", "cache_1h", "cache_read")


def _tok(v):
    """Contagem de token utilizavel, ou 0. Recusa bool: `True` e int em Python.

    NAO se chama `_num`: works.py tem um `_num` que devolve **None** para
    entrada invalida, e o contrato aqui e o oposto — token ausente vale zero
    porque somar zero e correto. Dois nomes iguais com contratos opostos em
    modulos vizinhos seria armadilha para quem lesse os dois no mesmo dia.
    """
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return 0
    return v


def entrada(mid):
    """A linha da tabela para este id, ou None.

    Casa exato primeiro, depois por prefixo. Dois sufixos reais obrigam o
    prefixo: o transcript grava `claude-haiku-4-5-20251001` (data) e a
    statusline grava `claude-opus-5[1m]` (variante de janela).

    Do prefixo MAIS LONGO para o mais curto. Com a tabela de hoje a ordem nao
    muda nada, mas no dia em que existir uma chave que seja prefixo de outra,
    a curta sequestraria o id da longa — e o erro seria um preco silenciosamente
    trocado, nao uma excecao.
    """
    if not mid or not isinstance(mid, str):
        return None
    if mid in TABELA:
        return TABELA[mid]
    for chave in sorted(TABELA, key=len, reverse=True):
        if mid.startswith(chave):
            return TABELA[chave]
    return None


def rotulo(mid):
    """Nome curto para a tela. O proprio id quando desconhecido.

    Mora aqui e nao no firmware pelo mesmo argumento que ui.cpp:478 ja usa para
    o nivel de esforco: quem conhece o vocabulario e quem fala com o Claude Code.
    """
    e = entrada(mid)
    return e["rotulo"] if e else (mid or "?")


def custo(mid, tokens, rapido=False, dia=None):
    """US$ deste bolo de tokens. None quando o modelo nao esta na tabela.

    `dia` no formato "AAAA-MM-DD" habilita a promocao quando ela ainda valia
    naquele dia. Sem `dia`, preco cheio — nao se aplica desconto por engano a
    um dia que nao foi informado.

    `rapido` vence a promocao: nenhum modelo tem os dois hoje, e se tiver, o
    modo rapido e o que o usuario escolheu explicitamente.
    """
    e = entrada(mid)
    if not e or not isinstance(tokens, dict):
        return None

    p = e["preco"]
    if rapido and e.get("rapido"):
        p = e["rapido"]
    elif e.get("promo"):
        ate, promocional = e["promo"]
        if dia and dia <= ate:
            p = promocional

    ein, eout = p
    return (_tok(tokens.get("input")) * ein
            + _tok(tokens.get("output")) * eout
            + _tok(tokens.get("cache_read")) * ein * CACHE_READ
            + _tok(tokens.get("cache_5m")) * ein * CACHE_5M
            + _tok(tokens.get("cache_1h")) * ein * CACHE_1H) / 1_000_000.0
