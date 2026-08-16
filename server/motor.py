#!/usr/bin/env python3
"""Qual motor decide o estado das sessoes agora.

POR QUE EXISTEM DOIS
A deducao por hooks foi a escolha certa enquanto nao havia alternativa, mas os
hooks tem furos estruturais: nao existe hook de "desbloqueou" nem de
"interrompido". Em 31/07 os dois furos prenderam sessoes no estado errado por
horas. O herdr resolve os dois lendo a TELA RENDERIZADA, sem depender de evento
nenhum — entao ele vira o motor principal.

O motor de hooks NAO e apagado. Ele assume quando o herdr nao esta la, e cair
automaticamente e o que o mantem exercitado: um caminho que so roda por
configuracao apodrece em silencio ate o dia em que se precisa dele.

POR QUE ESTE ARQUIVO E SEPARADO
A regra de escolha e a primeira coisa que alguem vai querer ler daqui a seis
meses. Diluida dentro de uma funcao de 90 linhas que monta payload, ela some.

POR QUE NAO IMPORTA `herdr`
Este modulo recebe um booleano e devolve um nome. Nao conhece o sensor, nao
conhece a API, e por isso o teste dele nao precisa de nenhum dos dois.

POR QUE IMPORTA `time`
`avaliar` precisa saber SE ELA PROPRIA esta sendo chamada com regularidade —
o unico chamador de producao e a thread de sensor do herdr, e essa thread pode
travar sem lancar excecao (ex.: um `subprocess.run` preso em `communicate()`
porque um neto herdou os pipes), o que `_laco` nao teria como engolir. `time`
e stdlib e nao ensina nada deste modulo sobre `herdr.py` nem sobre a API — so
sobre o relogio.
"""

import threading
import time

HERDR = "herdr"
HOOKS = "hooks"

# Avaliacoes consecutivas concordando antes de trocar de motor.
#
# A avaliacao acontece uma vez por consulta do sensor (2 s), entao sao ~6 s. O
# herdr pisca: uma consulta que estoura o timeout num pico de carga nao e o
# herdr estar fora. Sem histerese, uma piscada de 2 s trocaria o motor duas
# vezes e faria o indicador do painel tremular — o tipo de tremulacao que este
# projeto trata como defeito, e nao como detalhe.
ESTAVEL = 3

# Ha quanto tempo `avaliar` pode ficar sem rodar antes de `atual()` parar de
# confiar no motor escolhido e cair para HOOKS. ~7 ciclos do sensor do herdr
# (CONSULTA_S = 2 s em herdr.py — este modulo nao importa herdr.py, entao o
# numero e copiado aqui, nao referenciado). Sem esta guarda, a thread do
# sensor travar (ver o comentario de `import time` acima) e indistinguivel de
# "esta tudo bem": o motor fica grudado no ultimo valor, mesmo depois de
# `herdr.snapshot()` ja ter caido para offline pela propria guarda de frescor
# dele — e ninguem decide nada.
AVALIACAO_MAX_IDADE_S = 15

# Carimbado uma unica vez, na carga deste modulo (o "boot"), NAO em cada
# reset() — e o valor de fallback que `status()` devolve quando `avaliar()`
# nunca rodou (herdr nunca instalado: `claude_metrics_api.main()` nao chama
# `avaliar` nesse caso, de proposito). Sem isto, "desde" ficaria `None` para
# sempre, e quem le /health espera um numero.
_BOOT_TS = time.time()

_lock = threading.Lock()
_estado = {}


def reset():
    """Volta ao estado de boot. Existe para os testes; a API nunca chama."""
    with _lock:
        _estado.clear()
        _estado.update({"atual": HOOKS, "desde": None, "trocas": 0,
                        "pendente": None, "contagem": 0, "viva": None})


reset()


def avaliar(online, now):
    """Uma avaliacao. `online` e "o herdr esta respondendo e fresco?".

    Devolve o motor que vale AGORA — que pode nao ser o alvo, se a histerese
    ainda nao completou.
    """
    alvo = HERDR if online else HOOKS
    with _lock:
        # Carimba que uma avaliacao aconteceu agora — e o que `atual()` compara
        # contra o relogio para saber se o motor escolhido ainda e confiavel.
        # Relogio proprio (monotonic, imune a ajuste de hora do sistema) porque
        # isto e vivacidade do PROCESSO, nao um instante de negocio — o `now`
        # do parametro e outra coisa (epoch, injetado pelo chamador/testes) e
        # vai para `desde`, nao para cá.
        _estado["viva"] = time.monotonic()

        # Primeira avaliacao da vida do processo: carimba `desde` (epoch, para
        # /health). Sentinela e `None`, nao falsy — um `now` de 0.0 e um
        # instante valido (epoch), e `if not 0.0` engoliria essa avaliacao.
        if _estado["desde"] is None:
            _estado["desde"] = now

        if alvo == _estado["atual"]:
            # Concordou com o que ja vale: qualquer contagem em andamento morre.
            _estado["pendente"] = None
            _estado["contagem"] = 0
            return _estado["atual"]

        if _estado["pendente"] != alvo:
            _estado["pendente"] = alvo
            _estado["contagem"] = 1
        else:
            _estado["contagem"] += 1

        if _estado["contagem"] >= ESTAVEL:
            _estado["atual"] = alvo
            _estado["desde"] = now
            _estado["trocas"] += 1
            _estado["pendente"] = None
            _estado["contagem"] = 0

        return _estado["atual"]


def atual():
    """O motor que vale AGORA, para quem vai DECIDIR com isso.

    Se `avaliar` nao roda ha mais que `AVALIACAO_MAX_IDADE_S`, o motor
    escolhido deixa de ser confiavel — a thread que o alimenta pode ter
    travado — e isto devolve HOOKS, o motor que nao depende de nenhuma thread
    externa continuar viva. Sem esta guarda, silencio na avaliacao e "tudo
    bem" ficam indistinguiveis: `_estado["atual"]` fica grudado no ultimo
    valor (que pode ser HERDR) enquanto `herdr.snapshot()` ja caiu para
    offline pela sua propria guarda de frescor, e nenhum motor decide nada.
    """
    with _lock:
        viva = _estado["viva"]
        se_velha = viva is None or (time.monotonic() - viva) > AVALIACAO_MAX_IDADE_S
        return HOOKS if se_velha else _estado["atual"]


def status():
    """Retrato para o /health. Diagnostico, nao decisao — por isso devolve o
    valor GUARDADO (`_estado["atual"]`), sem passar pela guarda de vivacidade
    de `atual()`. Quem quer saber o que esta decidindo agora chama `atual()`;
    quem quer saber o que a maquina tem guardado, mesmo que a avaliacao tenha
    parado, chama isto. Copia: quem le nao mexe no que a thread escreve.
    """
    with _lock:
        return {"atual": _estado["atual"],
                "desde": _estado["desde"] if _estado["desde"] is not None else _BOOT_TS,
                "trocas": _estado["trocas"]}
