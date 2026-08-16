#!/usr/bin/env python3
"""O livro-caixa dos trabalhos.

Um TRABALHO vai do `UserPromptSubmit` ao `Stop`: uma pergunta sua e a resposta
inteira do Claude, com tudo que ele fez no meio. A API sabia disso no instante
exato — os hooks dizem — e nao guardava nada. `_sessions` tem so o estado
corrente, e o `SessionEnd` apaga a sessao: o momento em que os totais ficariam
interessantes era exatamente o momento em que eles sumiam.

Perguntas que passam a ter resposta: quanto durou cada trabalho, quantos houve
hoje, quanto custou, e quanto do tempo dele foi gasto esperando por voce.

DISCIPLINA DESTE MODULO (a mesma do weather.py)

  - So stdlib.
  - Nao conhece HTTP e nao toca em `_sessions`.
  - NUNCA levanta excecao para fora. Uma falha na contabilidade nao pode
    derrubar o painel: o pior caso aceitavel e perder um registro.

COMO OS NUMEROS SAO MEDIDOS

Custo, tempo de API e linhas sao DELTAS entre o snapshot da abertura e o do
fechamento. Os contadores da statusline sao cumulativos por sessao, entao
subtrair da o valor daquele trabalho — sem ler transcript nenhum.

O snapshot da abertura pode ter ate 10 s de idade (a statusline publica nesse
ritmo) e ainda assim estar certo: entre o `Stop` anterior e este
`UserPromptSubmit` o Claude estava ocioso, e nenhum dos contadores anda sem
chamada de API. O snapshot velho descreve exatamente o mesmo ponto.

No fechamento o raciocinio se inverte, e por isso ele NAO acontece no `Stop`:
ali o ultimo heartbeat pode ser anterior a ultima chamada de API do turno, e
fechar naquele instante perderia a cauda do trabalho — justamente a parte mais
cara. Quem chama marca o trabalho como pendente e fecha no proximo heartbeat.
"""

import json
import os
import threading
import time

import precos
import transcript

# Nome do arquivo, ao lado do state.json. Uma linha JSON por trabalho: da para
# acompanhar com `tail -f`, sobrevive a uma linha corrompida no meio, e nao
# exige carregar tudo na memoria para acrescentar um registro.
CAMINHO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "works.jsonl")

# Os quatro contadores cumulativos que a statusline publica.
CONTADORES = ("cost_usd", "api_ms", "lines_added", "lines_removed")

# Escritas concorrentes: `/ingest` e `/event` chegam por threads diferentes do
# servidor HTTP. O lock e daqui e nao emprestado do chamador — este modulo nao
# conhece os locks dele, e um `append` de uma linha e curto demais para valer
# uma disciplina de ordem entre dois locks.
_lock = threading.Lock()


def _num(v):
    """Numero utilizavel, ou None. Recusa bool: `True` e int em Python."""
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return None
    return v


def _snapshot(dados):
    """Os quatro contadores de um payload da statusline."""
    d = dados or {}
    return {c: _num(d.get(c)) for c in CONTADORES}


def _delta(antes, depois):
    """Quanto andou entre dois snapshots. Devolve (valores, regrediu).

    Um contador que REGRIDE nao e erro de leitura: acontece quando a sessao e
    outra (mesmo id reaproveitado) ou quando os totais zeram. Nesse caso o delta
    e grampeado em zero e o registro sai marcado — um numero negativo envenenaria
    qualquer media, e um numero inventado seria pior.
    """
    fora = {}
    regrediu = False
    for c in CONTADORES:
        a, b = antes.get(c), depois.get(c)
        if a is None or b is None:
            fora[c] = None
            continue
        d = b - a
        if d < 0:
            d = 0
            regrediu = True
        fora[c] = d
    return fora, regrediu


def abrir(sid, dados, now):
    """Comeca um trabalho. Devolve o registro aberto, para quem chama guardar.

    Ele mora em `rec["work"]`, dentro de `_sessions`, e nao aqui: assim entra no
    `state.json` de graca e sobrevive a um restart da API com o turno em
    andamento.
    """
    d = dados or {}
    return {
        "session_id": sid,
        "started": now,
        "snap": _snapshot(d),
        # Identidade copiada na ABERTURA. O repo e o modelo podem mudar no meio
        # de uma sessao longa, e o que descreve este trabalho e o que valia
        # quando ele comecou.
        "repo": d.get("repo"),
        "branch": d.get("branch"),
        "model": d.get("model"),
        # O id da API, publicado pela statusline. E ele que casa com o nome que
        # o transcript usa; o `model` acima e so para exibir.
        "model_id": d.get("model_id"),
        # Modo rapido dobra o preco. Copiado na ABERTURA como o resto da
        # identidade: e o que valia quando o turno comecou.
        "fast": bool(d.get("fast_mode")),
        "effort": d.get("effort"),
        "blocks": 0,
        "blocked_total": 0.0,
        "blocked_start": None,
        "blocked_api_ms": None,
        "pendente_ts": None,
        # O maior intervalo SEM chamada de API dentro do trabalho, e o instante
        # da ultima. Medido em 98 turnos reais desta maquina: o vao mediano e de
        # 1min02 e o p90 de 2min30, mas o turno mais longo (16h17) tinha um vao
        # unico de 11h55 — 73% da duracao dele. Sem este numero, aquele turno
        # entra no livro-caixa como "demorou 16 horas", quando o que aconteceu
        # foi ficar parado a noite inteira esperando alguem.
        "ultima_api": now,
        "maior_vao": 0.0,
        # Referencia para saber se `api_ms` andou desde o ultimo heartbeat. Sai
        # do snapshot da abertura, e nao de None: sem base, o PRIMEIRO heartbeat
        # do turno seria lido como atividade e fecharia um vao que nunca houve.
        "ultimo_api_ms": _snapshot(d).get("api_ms"),
    }


def bloquear(trab, api_ms, now):
    """O trabalho travou esperando voce.

    `blocks` e incrementado AQUI, na abertura do bloqueio, e nao no
    desbloqueio. Contado no fim, um bloqueio que nunca foi resolvido sumiria do
    registro — justamente o caso mais interessante de todos.

    Bloquear duas vezes sem desbloquear nao conta duas: o segundo evento e o
    mesmo bloqueio sendo reafirmado pelo heartbeat.
    """
    if not isinstance(trab, dict) or trab.get("blocked_start") is not None:
        return
    trab["blocked_start"] = now
    trab["blocked_api_ms"] = _num(api_ms)
    trab["blocks"] = (trab.get("blocks") or 0) + 1


def desbloquear(trab, api_ms, now):
    """Voce respondeu: o Claude voltou a chamar a API.

    A prova e `api_ms` ter passado do valor do instante do bloqueio. Nao existe
    hook de "desbloqueou" — esta e a unica evidencia disponivel, e ela e um fato
    e nao um proxy: aquele contador so anda com chamada de API.
    """
    if not isinstance(trab, dict) or trab.get("blocked_start") is None:
        return
    antes = trab.get("blocked_api_ms")
    agora = _num(api_ms)
    if antes is not None and (agora is None or agora <= antes):
        return
    trab["blocked_total"] = (trab.get("blocked_total") or 0.0) + \
        max(0.0, now - trab["blocked_start"])
    trab["blocked_start"] = None
    trab["blocked_api_ms"] = None


def semear(trab, dados):
    """Adota este snapshot como base, se a abertura nao teve nenhum.

    Acontece no PRIMEIRO turno de uma sessao nova: o `UserPromptSubmit` chega
    antes do primeiro heartbeat da statusline, entao nao ha contador nenhum para
    servir de base e todos os deltas sairiam nulos — a sessao inteira perderia o
    turno de abertura, que costuma ser o maior.

    Numa sessao recem-aberta os cumulativos estao proximos de zero, entao o
    primeiro heartbeat e uma base honesta. Ela pode subestimar em ate 10 s de
    trabalho; nulo nao subestima nada porque nao diz nada.

    So age quando a base esta VAZIA. Uma base existente nunca e substituida: ela
    e o ponto certo, e troca-la por uma mais nova apagaria o comeco do turno.

    A identidade segue junto pelo mesmo motivo — sem ela o registro sai sem repo
    nem modelo, e um livro-caixa que nao diz onde o trabalho aconteceu serve
    para pouca coisa.
    """
    if not isinstance(trab, dict) or not dados:
        return
    base = trab.get("snap") or {}
    if any(base.get(c) is not None for c in CONTADORES):
        return
    trab["snap"] = _snapshot(dados)
    trab["ultimo_api_ms"] = trab["snap"].get("api_ms")
    for campo in ("repo", "branch", "model", "model_id", "effort"):
        if trab.get(campo) is None:
            trab[campo] = dados.get(campo)
    if not trab.get("fast"):
        trab["fast"] = bool(dados.get("fast_mode"))


def atividade(trab, api_ms, now):
    """A API foi chamada desde o ultimo heartbeat. Fecha o vao que estava aberto.

    Chamado de todo `/ingest`. Quem decide se houve chamada e o proprio
    `api_ms`: ele so anda com chamada de API, entao um heartbeat que chega com o
    mesmo valor prova que nada aconteceu no intervalo.
    """
    if not isinstance(trab, dict):
        return
    agora = _num(api_ms)
    antes = _num(trab.get("ultimo_api_ms"))
    trab["ultimo_api_ms"] = agora
    if agora is None or (antes is not None and agora <= antes):
        return                      # nada andou: o vao continua aberto
    ultima = trab.get("ultima_api")
    if isinstance(ultima, (int, float)):
        vao = now - ultima
        if vao > (trab.get("maior_vao") or 0.0):
            trab["maior_vao"] = vao
    trab["ultima_api"] = now


def fechar(trab, dados, now, motivo=None):
    """Encerra o trabalho e devolve o registro pronto para gravar.

    `motivo` marca o que NAO foi um fim normal: "partial", "aborted",
    "interrupted", "stalled". A ausencia e a informacao — um registro sem marca
    e um turno que comecou e terminou como devia.

    Um bloqueio ainda aberto no fechamento e somado ate aqui: ele aconteceu, e
    descartar faria o tempo de espera sumir exatamente do trabalho em que ele
    mais pesou.
    """
    if not isinstance(trab, dict) or not isinstance(trab.get("started"), (int, float)):
        return None

    if trab.get("blocked_start") is not None:
        trab["blocked_total"] = (trab.get("blocked_total") or 0.0) + \
            max(0.0, now - trab["blocked_start"])
        trab["blocked_start"] = None

    # O vao ainda aberto no fechamento conta: um turno que terminou logo depois
    # de doze horas parado tem esse vao como quase toda a sua duracao.
    ultima = trab.get("ultima_api")
    vao = trab.get("maior_vao") or 0.0
    if isinstance(ultima, (int, float)) and (now - ultima) > vao:
        vao = now - ultima

    d, regrediu = _delta(trab.get("snap") or {}, _snapshot(dados))
    api_s = None if d["api_ms"] is None else round(d["api_ms"] / 1000.0, 1)

    # Tokens por modelo, lidos do transcript. A statusline nao os entrega — ela
    # so publica o custo AGREGADO da sessao, sem quebra por modelo. Esta e a
    # unica fonte, e por isso o caminho ao vivo tambem le o arquivo.
    #
    # Uma leitura por turno FECHADO, e nao por heartbeat: ver transcript.uso.
    tokens = transcript.uso((dados or {}).get("transcript_path"),
                            trab["started"], now)

    reg = {
        "session_id": trab.get("session_id"),
        "started": round(trab["started"], 3),
        "ended": round(now, 3),
        "seconds": round(max(0.0, now - trab["started"]), 1),
        "blocked_seconds": round(trab.get("blocked_total") or 0.0, 1),
        # O maior intervalo parado. Separa "demorou porque tinha muito a fazer"
        # de "demorou porque ficou esperando".
        #
        # RESOLUCAO: a statusline publica a cada ~10 s, entao vaos menores do
        # que isso nao existem para este numero — todo trabalho comeca com um
        # piso de ate um intervalo de heartbeat. E um limite do instrumento, e
        # nao um erro de conta: abaixo dele nao ha como distinguir "parado" de
        # "acabou de comecar". O que ele mede bem sao os vaos que importam, que
        # nos dados reais desta maquina tem mediana de 45 s e chegam a 11h55.
        "gap_seconds": round(max(0.0, vao), 1),
        "api_seconds": api_s,
        "blocks": trab.get("blocks") or 0,
        "repo": trab.get("repo"),
        "branch": trab.get("branch"),
        "model": trab.get("model"),
        "model_id": trab.get("model_id"),
        "fast": bool(trab.get("fast")),
        # Vazio quando nao deu para ler o transcript. Vazio e diferente de
        # zero: zero afirmaria que o turno nao consumiu nada.
        "tokens": tokens,
        "effort": trab.get("effort"),
        "cost_usd": None if d["cost_usd"] is None else round(d["cost_usd"], 4),
        "lines_added": d["lines_added"],
        "lines_removed": d["lines_removed"],
    }
    # Campos opcionais so aparecem quando se aplicam: um registro cheio de
    # `false` esconde os poucos que importam.
    if motivo:
        reg[motivo] = True
    if regrediu:
        reg["reset"] = True
    return reg


def gravar(registro, caminho=None):
    """Acrescenta uma linha ao livro-caixa. True se gravou.

    Engole todo erro de proposito. Disco cheio, permissao negada, caminho que
    sumiu: nada disso pode subir para o `/ingest` e derrubar o heartbeat de uma
    sessao. Perder um registro e ruim; perder o painel e pior.
    """
    if not isinstance(registro, dict):
        return False
    try:
        linha = json.dumps(registro, ensure_ascii=False)
    except (TypeError, ValueError):
        return False
    try:
        with _lock:
            with open(caminho or CAMINHO, "a", encoding="utf-8") as f:
                f.write(linha + "\n")
        return True
    except OSError:
        return False


def ler(caminho=None, limite=None):
    """Le o livro-caixa. Lista vazia quando ele nao existe ou nao abre.

    Uma linha invalida e PULADA em vez de derrubar a leitura: um registro
    truncado por uma queda de energia no meio da escrita nao pode apagar os
    outros mil que estao intactos.

    `limite` devolve os N ultimos, que e como este arquivo e consultado — o
    interesse e sempre o passado recente.
    """
    try:
        with open(caminho or CAMINHO, "r", encoding="utf-8") as f:
            linhas = f.readlines()
    except OSError:
        return []
    if limite:
        linhas = linhas[-limite:]
    fora = []
    for linha in linhas:
        linha = linha.strip()
        if not linha:
            continue
        try:
            reg = json.loads(linha)
        except ValueError:
            continue
        if isinstance(reg, dict):
            fora.append(reg)
    return fora


# Cache da leitura do livro-caixa.
#
# O painel pede `/status` a cada dois segundos e o arquivo so cresce: reler 600+
# linhas a cada pedido seria trabalho puro. A chave e (mtime, tamanho) — quando
# nenhum dos dois mudou, nao ha o que reler. Um `append` sempre muda o tamanho.
_cache = {"chave": None, "regs": []}


def ler_cache(caminho=None):
    """Como `ler`, mas relendo so quando o arquivo mudou de verdade."""
    alvo = caminho or CAMINHO
    try:
        st = os.stat(alvo)
        chave = (alvo, st.st_mtime, st.st_size)
    except OSError:
        chave = (alvo, None, None)
    with _lock:
        if _cache["chave"] == chave:
            return _cache["regs"]
    regs = ler(alvo)
    with _lock:
        _cache["chave"] = chave
        _cache["regs"] = regs
    return regs


def inicio_do_dia(now):
    """Meia-noite LOCAL do dia de `now`, em epoch.

    Local e nao UTC: "hoje" e o dia de quem esta olhando o painel, e nesta
    maquina isso muda a conta por tres horas — um turno das 22h apareceria como
    sendo de amanha.
    """
    t = time.localtime(now)
    return time.mktime((t.tm_year, t.tm_mon, t.tm_mday, 0, 0, 0, 0, 0, -1))


def desde(registros, inicio):
    """Só os trabalhos que COMEÇARAM a partir de `inicio`.

    Pelo inicio e nao pelo fim: um turno que atravessa a meia-noite pertence ao
    dia em que voce o pediu.
    """
    fora = []
    for r in registros or []:
        if not isinstance(r, dict):
            continue
        t = _num(r.get("started"))
        if t is not None and t >= inicio:
            fora.append(r)
    return fora


def resumo(registros, now):
    """Agregados prontos para exibir.

    A MEDIANA e nao a media: um unico trabalho de tres horas — dos que ficam
    abertos porque o `Stop` se perdeu — desloca a media inteira e nao diz nada
    sobre como e um turno tipico. Os registros marcados continuam contando; eles
    aconteceram e custaram dinheiro.
    """
    regs = [r for r in (registros or []) if isinstance(r, dict)]
    duracoes = sorted(_num(r.get("seconds")) or 0.0 for r in regs)

    def soma(campo):
        vals = [_num(r.get(campo)) for r in regs]
        vals = [v for v in vals if v is not None]
        return sum(vals) if vals else 0

    def mediana_de(vals):
        vals = sorted(v for v in vals if v is not None)
        if not vals:
            return 0.0
        meio = len(vals) // 2
        return (vals[meio] if len(vals) % 2
                else (vals[meio - 1] + vals[meio]) / 2.0)

    mediana = mediana_de(duracoes)
    mediana_gap = mediana_de(_num(r.get("gap_seconds")) for r in regs)

    return {
        "trabalhos": len(regs),
        "seconds": round(soma("seconds"), 1),
        "blocked_seconds": round(soma("blocked_seconds"), 1),
        "api_seconds": round(soma("api_seconds"), 1),
        "cost_usd": round(soma("cost_usd"), 4),
        "lines_added": int(soma("lines_added")),
        "lines_removed": int(soma("lines_removed")),
        "blocks": int(soma("blocks")),
        "mediana_seconds": round(mediana, 1),
        # O vao tipico. Junto da mediana de duracao ele responde "um turno
        # normal daqui e assim", e um turno que foge disso foge por qual dos
        # dois lados: muito a fazer, ou muito tempo parado.
        "mediana_gap_seconds": round(mediana_gap, 1),
        # Quantos NAO terminaram normalmente. Sem isto, uma media contaminada
        # por turnos abortados pareceria apenas um dia ruim.
        "marcados": sum(1 for r in regs
                        if any(r.get(m) for m in
                               ("partial", "aborted", "interrupted", "stalled"))),
    }


def por_modelo(registros, dia=None):
    """Agrega os registros por modelo, com custo em US$.

    DUAS CAMADAS DE CUSTO, e a ordem importa:

      1. Turno COM `cost_usd` real (caminho ao vivo): o numero do Claude Code e
         a verdade. Quando o turno usou mais de um modelo — acontece, o Haiku e
         o Sonnet aparecem dentro de turnos de Opus — esse numero unico e
         rateado PROPORCIONALMENTE ao valor tabelado de cada um. Preserva o
         total autoritativo e ainda separa por modelo.
      2. Turno SEM `cost_usd` (reconstruido do transcript): custo calculado da
         tabela, marcado `estimado`.

    O `fator` devolvido e a mediana das razoes real/tabelado. Com a tabela certa
    ele fica perto de 1.00; se derivar, a tabela envelheceu. E afericao de
    graca, tirada de um numero que ja precisava ser calculado.
    """
    acc = {}
    total = 0.0
    estimados = 0
    fatores = []

    for r in registros or []:
        if not isinstance(r, dict):
            continue
        toks = r.get("tokens")
        if not isinstance(toks, dict) or not toks:
            continue

        rapido = bool(r.get("fast"))
        tabelado = {}
        for mid, t in toks.items():
            if isinstance(t, dict):
                c = precos.custo(mid, t, rapido=rapido, dia=dia)
                if c is not None:
                    tabelado[mid] = c
        soma_tab = sum(tabelado.values())

        real = _num(r.get("cost_usd"))
        if real is not None and soma_tab > 0:
            fator = real / soma_tab
            fatores.append(fator)
            estimado = False
            total += real
        else:
            fator = 1.0
            estimado = True
            estimados += 1
            total += soma_tab

        for mid, t in toks.items():
            if not isinstance(t, dict):
                continue
            a = acc.get(mid)
            if a is None:
                a = acc[mid] = {"turnos": 0, "tokens": 0,
                                "cost_usd": None, "estimado": False}
            a["turnos"] += 1
            for campo in transcript.CAMPOS:
                v = t.get(campo)
                if isinstance(v, (int, float)) and not isinstance(v, bool):
                    a["tokens"] += int(v)
            c = tabelado.get(mid)
            if c is not None:
                a["cost_usd"] = (a["cost_usd"] or 0.0) + c * fator
            if estimado:
                a["estimado"] = True

    modelos = [{"id": mid,
                "rotulo": precos.rotulo(mid),
                "cost_usd": (round(a["cost_usd"], 4)
                             if a["cost_usd"] is not None else None),
                "tokens": a["tokens"],
                "turnos": a["turnos"],
                "estimado": a["estimado"]}
               for mid, a in acc.items()]
    # Custo desconhecido vai para o fim: ele nao pode disputar o topo da lista
    # com um numero medido.
    modelos.sort(key=lambda m: (m["cost_usd"] is None, -(m["cost_usd"] or 0)))

    fatores.sort()
    if fatores:
        meio = len(fatores) // 2
        mediana = (fatores[meio] if len(fatores) % 2
                   else (fatores[meio - 1] + fatores[meio]) / 2.0)
    else:
        mediana = None

    return {
        "modelos": modelos,
        "cost_usd": round(total, 4),
        "estimados": estimados,
        "fator": round(mediana, 4) if mediana is not None else None,
        # A data em que a tabela foi conferida. Sem ela, "calculado a preco de
        # quando" nao tem resposta.
        "vigente": precos.VIGENTE,
    }


def vitalicio(registros):
    """Os totais de TODA a história do livro-caixa, para o nível do Clawd.

    O custo sai dos TOKENS, e não do campo `cost_usd`. Não é preferência: no
    livro-caixa real só 74 de 709 registros têm aquele campo — os que passaram
    ao vivo pela statusline. Os outros 635 vieram reconstruídos dos transcripts
    e têm token, não dólar. Somar `cost_usd` daria US$ 275,68 onde o total real
    é US$ 2.481,60, um erro de 9× que colocaria o Clawd dezenas de níveis abaixo
    do que a história merece.

    Difere de `por_modelo` de propósito: lá o `cost_usd` real é preferido quando
    existe, porque o número do Claude Code é autoritativo para AQUELE turno e a
    quebra por modelo precisa bater com ele. Aqui a pergunta é outra — um total
    único sobre uma base onde a maioria não tem o campo — e misturar as duas
    fontes deixaria o total dependente de QUANTOS turnos passaram ao vivo, que é
    um detalhe de operação e não um fato sobre o uso.
    """
    turnos = 0
    custo = 0.0
    inicio = None

    for r in registros or []:
        if not isinstance(r, dict):
            continue
        # Sem `started` não é turno. É a mesma régua que `desde` já usa: o
        # livro-caixa sempre grava a hora de início, então um registro sem ela
        # está corrompido, e contá-lo inflaria o total com uma linha que
        # nenhuma outra parte do módulo aceita.
        ini = r.get("started")
        if not isinstance(ini, (int, float)):
            continue

        turnos += 1
        if inicio is None or ini < inicio:
            inicio = ini

        rapido = bool(r.get("fast"))
        for mid, tk in (r.get("tokens") or {}).items():
            c = precos.custo(mid, tk, rapido)
            if c:
                custo += c

    return {
        "turnos": turnos,
        "cost_usd": round(custo, 2),
        "desde": (time.strftime("%Y-%m-%d", time.localtime(inicio))
                  if inicio is not None else None),
    }
