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

# Topo do documento.
#
#   sessions_active  a placa conta pela lista de agentes (ver fusao.cpp)
#   blocked          idem: quem esta bloqueado sai do estado de cada agente
#   session_label    frase pronta ("Session 47% - reseta 2:30am"); o painel monta
#   week_label       a sua a partir dos numeros, com a fonte e a largura dele
#   herdr_online     estado do sensor, que a placa nao mostra em lugar nenhum
TOPO = frozenset({
    "sessions_active",
    "blocked",
    "session_label",
    "week_label",
    "herdr_online",
})

# Cada entrada de `labels[]`. E o corte que mais rende, porque multiplica pelo
# numero de agentes vivos.
#
#   state_age    a placa usa `age`, que e o silencio do heartbeat
#   event        nome do hook que produziu o estado; diagnostico do servidor
#   proc_alive   a placa ja recebe `stale`
#   line         a statusline pronta, em UTF-8 — a maior de todas e a que o
#                painel menos pode usar
LABEL = frozenset({
    "state_age",
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
    if "?" not in caminho:
        return False
    for par in caminho.split("?", 1)[1].split("&"):
        chave, _, valor = par.partition("=")
        if chave == "campos" and valor == "painel":
            return True
    return False
