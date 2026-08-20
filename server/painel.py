#!/usr/bin/env python3
"""O /status enxuto: sai do fio o que o firmware nao le.

A placa busca o /status a cada dois segundos e reparseia o documento inteiro.
Medido contra o payload real com um agente vivo, **422 dos 2.006 bytes (21%)**
sao chaves que `lib/metrics/status.cpp` nunca indexa — elas atravessam a rede,
entram no parser, ocupam heap e morrem ali. Com mais agentes a fracao cresce:
`labels[].line` sozinho custa ~49 bytes POR AGENTE e e uma statusline em UTF-8
que a fonte embutida do painel nem consegue desenhar (ela cobre 0x20..0x7E).

Quem pede o corte e a PLACA, com `?campos=painel`. Nao e o servidor que decide:

  - `tools/tela.py`, o `curl` de depuracao e qualquer consumidor futuro
    continuam recebendo o documento inteiro, que e o contrato publico;
  - uma placa gravada ANTES desta versao nao manda o parametro e recebe tudo,
    como sempre recebeu. O firmware do projeto e versionado junto com a API, mas
    a placa nao se atualiza sozinha — e ela e que fica na parede.

A lista e de REMOCAO, e nao de permissao, e isso e a decisao que importa aqui.
Com lista de permissao, um campo novo que o firmware aprendesse a ler sumiria em
silencio ate alguem lembrar de atualizar esta lista — e o sintoma seria um dado
faltando na tela, longe daqui. Com lista de remocao o pior caso e continuar
mandando alguns bytes a toa, que e o problema que este modulo existe para
diminuir, nao para criar.

Manutencao: ao ensinar o firmware a ler um campo, tire-o da lista aqui.
`tools/campos_mortos.py` refaz a conta contra o parser e o payload vivo.
"""

from __future__ import annotations

import hashlib
import json

# Topo do documento.
#
#   sessions_active  a placa conta pela lista de agentes (ver fusao.cpp)
#   blocked          idem: quem esta bloqueado sai do estado de cada agente
#   session_label    frase pronta ("Session 47% - reseta 2:30am"); o painel monta
#   week_label       a sua a partir dos numeros, com a fonte e a largura dele
#   herdr_online     estado do sensor, que a placa nao mostra em lugar nenhum
#
# Os quatro de tempo sairam quando a placa aprendeu a contar sozinha. Eles sao
# TEXTO derivado de `session_resets_in`/`week_resets_in`, que continuam no fio:
#
#   session_resets_hm     "1h32m"                a placa reescreve a cada segundo
#   week_resets_dh        "5d22h"                (prazoTexto, em lib/metrics)
#   session_resets_clock  "1:59am"               agora derivados do relogio da
#   week_resets_date      "22/08/2026 (Sabado)"  placa (horaDaVirada/dataDaVirada)
#
# Os dois primeiros ja eram mortos desde a leva do relogio: chegavam e eram
# sobrescritos no mesmo laco. Os dois ultimos so puderam sair quando a placa
# passou a ter fuso — o comentario do `fmt_clock` dizia "a conversao acontece
# aqui porque a placa nao tem relogio nem fuso", e ela tem os dois desde o SNTP.
TOPO = frozenset({
    "sessions_active",
    "blocked",
    "session_label",
    "week_label",
    "herdr_online",
    "session_resets_hm",
    "session_resets_clock",
    "week_resets_dh",
    "week_resets_date",
})

# Cada entrada de `labels[]`. E o corte que mais rende, porque multiplica pelo
# numero de agentes vivos.
#
#   event        nome do hook que produziu o estado; diagnostico do servidor
#   proc_alive   a placa ja recebe `stale`
#   line         a statusline pronta, em UTF-8 — a maior de todas e a que o
#                painel menos pode usar
LABEL = frozenset({
    "event",
    "proc_alive",
    "line",
})

# O livro-caixa do dia. Sobrevivem os quatro que o card HOJE desenha
# (`trabalhos`, `seconds`, `blocked_seconds`, `mediana_seconds`) e o `cost_usd`.
WORKS = frozenset({
    "api_seconds",
    "blocks",
    "mediana_gap_seconds",
    "marcados",
})

# A quebra por modelo. `estimados`, `fator` e `vigente` sao a contabilidade de
# como o preco foi estimado; `dia` e o recorte que a propria requisicao pediu.
USO = frozenset({
    "estimados",
    "fator",
    "vigente",
    "dia",
})

# Dentro de `uso.modelos[]`. `id` FICA: o firmware o indexa.
USO_MODELO = frozenset({
    "tokens",
    "estimado",
})

# `code` e `night` decidiriam um icone que a placa escolhe pela temperatura.
WEATHER = frozenset({
    "code",
    "night",
})


def _sem(dado, mortas):
    """Copia um dicionario sem as chaves de `mortas`. `None` atravessa."""
    if not isinstance(dado, dict):
        return dado
    return {k: v for k, v in dado.items() if k not in mortas}


def enxugar(status: dict) -> dict:
    """O /status sem o que a placa ignora. NAO altera o original.

    O dicionario de entrada e o que `build_status()` acabou de montar, e ele
    pode ser reaproveitado por quem chamou — consumi-lo aqui faria a proxima
    leitura devolver um documento pela metade.
    """
    magro = _sem(status, TOPO)

    if isinstance(status.get("labels"), list):
        magro["labels"] = [_sem(a, LABEL) for a in status["labels"]]

    if isinstance(status.get("works"), dict):
        magro["works"] = _sem(status["works"], WORKS)

    if isinstance(status.get("uso"), dict):
        uso = _sem(status["uso"], USO)
        if isinstance(uso.get("modelos"), list):
            uso["modelos"] = [_sem(m, USO_MODELO) for m in uso["modelos"]]
        magro["uso"] = uso

    if isinstance(status.get("weather"), dict):
        magro["weather"] = _sem(status["weather"], WEATHER)

    return magro


def pedido_do_painel(caminho: str) -> bool:
    """A requisicao pediu o corte? Le `?campos=painel` da URL crua.

    Comparacao por igualdade e nao por presenca: um valor desconhecido de um
    cliente futuro nao pode ativar um corte que ele nao entende.
    """
    return _param(caminho, "campos") == "painel"


def _param(caminho: str, chave: str) -> str:
    """O valor de um parametro da URL crua, ou "" quando ele nao veio."""
    if "?" not in caminho:
        return ""
    for par in caminho.split("?", 1)[1].split("&"):
        k, _, v = par.partition("=")
        if k == chave:
            return v
    return ""


# ---- Os blocos frios ----
#
# `works`, `uso` e `vitalicio` sao o dia e a vida inteira: eles mudam quando um
# turno termina, e nao a cada dois segundos. Somados dao ~200 dos 1459 bytes do
# payload enxuto (13%) e atravessam a rede 1800 vezes por hora sem ter mudado.
#
# O acordo e o mais simples que funciona: o servidor SEMPRE publica `frio`, um
# resumo do conteudo dos tres. A placa devolve no pedido seguinte o que recebeu
# (`?frio=<resumo>`), e quando os dois batem os blocos nao viajam.
#
# Nao e um ETag de HTTP de proposito. O 304 vale para a resposta INTEIRA, e aqui
# 87% dela muda a cada poll — o que se quer e omitir uma parte e mandar o resto.
#
# O resumo cobre o CONTEUDO, entao a invalidacao e automatica: um turno novo
# muda `works`, muda o resumo, e o bloco volta a viajar sem ninguem ter que
# lembrar de expirar nada. E a virada do dia entra por essa mesma porta.

FRIOS = ("works", "uso", "vitalicio")


def resumo_frio(status: dict) -> str:
    """O resumo dos blocos frios, em hex.

    Doze digitos (48 bits): a chance de dois conteudos diferentes colidirem e
    desprezivel, e o custo sao doze bytes num payload de 1400. Truncar mais
    economizaria nada que se note e aproximaria um bug que se manifesta como
    "o painel mostra os turnos de ontem".

    `sort_keys` porque a ordem de um dicionario Python nao e contrato: sem isso
    o mesmo conteudo daria resumos diferentes entre duas versoes do servidor, e
    o bloco voltaria a viajar sempre.
    """
    material = json.dumps(
        {k: status.get(k) for k in FRIOS}, sort_keys=True, separators=(",", ":")
    )
    return hashlib.sha1(material.encode("utf-8")).hexdigest()[:12]


def aplicar_frio(status: dict, caminho: str) -> dict:
    """Publica `frio` e, quando o pedido trouxe o mesmo resumo, omite os blocos.

    NAO altera o original, pela mesma razao de `enxugar`.

    Sem `?frio=` na URL o documento sai inteiro — com o campo `frio` junto, que
    e como a placa aprende o resumo da primeira vez. Uma placa gravada antes
    desta versao simplesmente ignora um campo a mais.
    """
    resumo = resumo_frio(status)
    saida = dict(status)
    saida["frio"] = resumo

    if _param(caminho, "frio") == resumo:
        for k in FRIOS:
            saida.pop(k, None)

    return saida
