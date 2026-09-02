#!/usr/bin/env python3
"""A cota da conta, lida do mesmo lugar que o `/cost` do Claude Code lê.

DISCIPLINA (a mesma de weather.py, works.py e precos.py)
  - So stdlib.
  - Nunca levanta excecao para fora.
  - Falhar devolve None, e quem chama decide o que fazer com isso.

POR QUE ISTO EXISTE
A statusline publica DUAS janelas — `five_hour` e `seven_day` — e o painel
sempre viveu delas. Mas o `/cost` mostra TRES barras, e a terceira e a que
faltava: "Current week (Fable)", a cota semanal do modelo. Conferido no payload
real da statusline: aquela terceira janela nao chega la.

O `/cost` a busca em `GET /api/oauth/usage`, com o token OAuth da propria
maquina — e o mesmo servidor oficial que o cliente ja consulta o tempo todo.
Este modulo faz a mesma leitura e a publica para a placa.

O QUE ESTE MODULO NAO FAZ

*Nao renova o token.* O `accessToken` de `~/.claude/.credentials.json` expira,
e quem o renova e o proprio Claude Code, reescrevendo o arquivo. Aqui a
credencial e RELIDA a cada consulta: quando ele renova, a proxima leitura ja
pega a nova. Renovar daqui exigiria escrever no arquivo de credenciais do
usuario e correr o risco de duas partes gravando ao mesmo tempo — para ganhar
o que a releitura ja da de graca.

*Nao e contrato publico.* Este endpoint nao esta na documentacao da API; ele e
interface interna do Claude Code, descoberta lendo o binario. Pode mudar sem
aviso, e e por isso que TUDO aqui degrada para None em vez de quebrar: quem
chama tem uma reserva local (ver `works.fatia_fable`), e a barra do painel cai
nela sem ninguem perceber.
"""

from __future__ import annotations

import json
import os
import threading
import time
import urllib.error
import urllib.request

URL = "https://api.anthropic.com/api/oauth/usage"
TIMEOUT = 8

# De dois em dois minutos. A cota anda em passos de um ponto percentual ao
# longo de uma semana — pedir mais que isso seria consultar para ver o mesmo
# numero. O painel pede /status a cada dois SEGUNDOS, e le o que estiver em
# memoria; a consulta nunca acontece dentro do pedido dele.
REFRESH = 120
RETRY = 30

CREDENCIAL = os.path.join(os.path.expanduser("~"), ".claude", ".credentials.json")

_lock = threading.Lock()
_uso = None          # o ultimo bloco bom, ou None
_uso_ts = 0.0
_erro = ""           # por que a ultima tentativa falhou, para o /diag


def _token():
    """O accessToken corrente, ou None. RELIDO a cada chamada (ver o cabecalho)."""
    try:
        with open(CREDENCIAL, encoding="utf-8") as fh:
            return (json.load(fh).get("claudeAiOauth") or {}).get("accessToken")
    except (OSError, ValueError, AttributeError):
        return None


def _pedir(token):
    req = urllib.request.Request(URL, headers={
        "Authorization": "Bearer " + token,
        "Content-Type": "application/json",
        # O mesmo par que o cliente oficial manda. Sem o `anthropic-beta` a
        # rota recusa o token de OAuth.
        "anthropic-beta": "oauth-2025-04-20",
        "User-Agent": "clawd-panel (painel local de uso)",
    })
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        return json.load(r)


def _pct(v):
    """Percentual inteiro e serrado em 0..100, ou None."""
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return None
    n = int(round(v))
    return 0 if n < 0 else (100 if n > 100 else n)


def extrair(doc):
    """O que a placa usa, tirado da resposta crua. `{}` quando nao da para ler.

    A COTA DO MODELO SAI DE `limits[]`, e nao de um campo com o nome dele.
    O payload TEM `seven_day_opus` e `seven_day_sonnet`, e os dois vem nulos —
    quem carrega o dado e a lista, onde cada item traz `kind`, `percent` e um
    `scope` opcional. Ler pelo nome do campo seria escrever "Fable" no codigo
    para um campo que nem existe; ler pela lista funciona para qualquer modelo
    que a conta venha a ter, sem tocar aqui.
    """
    if not isinstance(doc, dict):
        return {}

    fora = {}
    for chave, campo in (("five_hour", "session"), ("seven_day", "week")):
        bloco = doc.get(chave)
        if isinstance(bloco, dict):
            p = _pct(bloco.get("utilization"))
            if p is not None:
                fora[campo] = p

    for item in doc.get("limits") or []:
        if not isinstance(item, dict) or item.get("kind") != "weekly_scoped":
            continue
        escopo = item.get("scope") or {}
        modelo = (escopo.get("model") or {}).get("display_name")
        if not isinstance(modelo, str) or not modelo:
            continue
        p = _pct(item.get("percent"))
        if p is None:
            continue
        # Guarda TODOS os escopados, com o nome que o servidor deu. Hoje so o
        # Fable aparece; amanha, se a conta ganhar outro, ele entra sozinho.
        fora.setdefault("modelos", {})[modelo] = p

    return fora


def _buscar():
    global _erro
    token = _token()
    if not token:
        _erro = "sem credencial em ~/.claude/.credentials.json"
        return None
    try:
        dados = extrair(_pedir(token))
        if not dados:
            _erro = "resposta sem os campos esperados"
            return None
        _erro = ""
        return dados
    except urllib.error.HTTPError as e:
        # 401 e o caso ESPERADO: o token expirou e o Claude Code ainda nao
        # renovou. Some sozinho na proxima leitura da credencial.
        _erro = f"http {e.code}"
    except Exception as e:                     # rede, DNS, JSON quebrado
        _erro = type(e).__name__
    return None


def _ciclo():
    global _uso, _uso_ts
    while True:
        novo = _buscar()
        if novo:
            with _lock:
                _uso, _uso_ts = novo, time.time()
        time.sleep(REFRESH if novo else RETRY)


def start():
    """Sobe o ciclo. Daemon: nao segura o desligamento da API."""
    threading.Thread(target=_ciclo, name="cota", daemon=True).start()


# Quanto tempo uma leitura continua valendo. Meia hora: a cota semanal anda
# devagar, e um numero de vinte minutos atras ainda descreve a semana. Passou
# disso, `snapshot` devolve vazio e quem chama cai na reserva local.
VALIDADE = 1800


def snapshot(agora=None):
    """O ultimo bloco bom, ou `{}`. Nunca levanta excecao."""
    agora = time.time() if agora is None else agora
    with _lock:
        uso, ts = _uso, _uso_ts
    if not uso or (agora - ts) > VALIDADE:
        return {}
    fora = dict(uso)
    fora["age"] = int(agora - ts)
    return fora


def fable(agora=None):
    """A cota semanal do Fable em porcento, ou None.

    O nome do modelo vem do SERVIDOR ("Fable"), e a busca e sem diferenciar
    maiuscula: a placa nao pode depender de como o rotulo foi capitalizado do
    outro lado.
    """
    for nome, pct in (snapshot(agora).get("modelos") or {}).items():
        if nome.strip().lower().startswith("fable"):
            return pct
    return None


def diag():
    """Uma linha sobre o estado da leitura, para o /diag e o log de boot."""
    s = snapshot()
    if not s:
        return f"cota: sem leitura ({_erro or 'ainda nao consultou'})"
    return (f"cota: sessao {s.get('session')}% semana {s.get('week')}% "
            f"modelos {s.get('modelos') or {}} (ha {s.get('age')}s)")
