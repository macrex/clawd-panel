"""Atualizacao de arquivo do cartao da placa, sem tirar o cartao.

O caminho inverso da captura de tela, pela mesma carona: `POST /arquivo/pedir`
arma um pedido, o `GET /status` seguinte o carrega no campo `arquivo`, a placa
baixa o binario em `GET /arquivo/baixar`, grava no cartao e encerra com
`POST /arquivo/ok`. A placa continua fazendo so requisicoes de saida — nenhuma
porta nova abre em lugar nenhum.

O arquivo a servir mora em `server/atualizacoes/`, posto la por quem pediu
(tools/atualizar_sprite.py). O `pedir` calcula tamanho e CRC-32 na hora do
pedido e CONGELA os tres (nome, bytes, crc) no proprio pedido: trocar o arquivo
da staging no meio do voo nao muda o que a placa confere, so faz o CRC falhar —
que e o comportamento honesto.
"""

import os
import threading
import time
import zlib

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), "atualizacoes")

# O download de ~600 KB leva segundos; a gravacao no cartao, mais um ou dois; o
# reboot, uns cinco. O TTL cobre o ciclo inteiro com folga — vencido, o pedido
# expira e a placa nova que aparecer nao baixa nada que ninguem espera mais.
TTL = 120

# Teto do que se serve. Um sprite de tela cheia fica na casa dos 600 KB; 4 MB
# ja e metade da PSRAM da placa, e o firmware recusa acima de 2 MB de qualquer
# jeito. Conferido no `pedir`, que e onde da para responder com erro legivel.
MAX_BYTES = 2 * 1024 * 1024

_lock = threading.Lock()
_seq = int(time.time())      # semeado no relogio, pela mesma razao da captura

_estado = {
    "pedido": None,   # {"id", "nome", "bytes", "crc", "reiniciar", "ts"}
    "ultimo": None,   # {"id", "nome", "ok", "ts"} — como terminou o anterior
}


def _vivo(pedido, now):
    if not pedido or (now - pedido["ts"]) > TTL:
        return None
    return pedido


def _nome_limpo(nome):
    """So nome de arquivo simples: e ele que a placa cola em `/clawd/`."""
    if not nome or len(nome) > 63:
        return None
    if any(c in nome for c in ("/", "\\", "..")) or nome.startswith("."):
        return None
    return nome


def receber_upload(nome, corpo):
    """Poe um arquivo na staging, vindo por POST. Devolve (codigo, corpo).

    E assim que o arquivo chega ate AQUI: o disparador roda onde o usuario
    estiver, e a staging e desta instancia da API — que pode ser outra maquina.
    Copiar por filesystem so funcionava quando os dois calhavam de ser o mesmo
    disco, e foi exatamente o primeiro uso real que provou que nao sao.
    """
    nome = _nome_limpo(nome)
    if not nome:
        return 400, {"error": "nome ausente ou invalido"}
    if not corpo or len(corpo) > MAX_BYTES:
        return 400, {"error": "corpo vazio ou acima do teto",
                     "max": MAX_BYTES}
    os.makedirs(RAIZ, exist_ok=True)
    with open(os.path.join(RAIZ, nome), "wb") as f:
        f.write(corpo)
    return 200, {"ok": True, "nome": nome, "bytes": len(corpo),
                 "crc": zlib.crc32(corpo)}


def pedir(dados, now):
    """Arma um pedido. Devolve (codigo HTTP, corpo).

    Um pedido novo SUBSTITUI o pendente, como na captura: dois `pedir` seguidos
    deixam so o segundo, e a placa — que compara id por desigualdade — atende o
    que sobreviver.
    """
    global _seq
    nome = _nome_limpo((dados or {}).get("nome"))
    if not nome:
        return 400, {"error": "nome ausente ou invalido"}

    caminho = os.path.join(RAIZ, nome)
    if not os.path.isfile(caminho):
        return 404, {"error": "arquivo nao esta na staging", "staging": RAIZ}

    tamanho = os.path.getsize(caminho)
    if not tamanho or tamanho > MAX_BYTES:
        return 400, {"error": "tamanho fora do limite", "bytes": tamanho,
                     "max": MAX_BYTES}

    crc = 0
    with open(caminho, "rb") as f:
        for bloco in iter(lambda: f.read(65536), b""):
            crc = zlib.crc32(bloco, crc)

    with _lock:
        _seq += 1
        _estado["pedido"] = {
            "id": _seq, "nome": nome, "bytes": tamanho, "crc": crc,
            "reiniciar": bool((dados or {}).get("reiniciar", True)),
            "ts": now,
        }
        return 200, {"id": _seq, "nome": nome, "bytes": tamanho, "crc": crc,
                     "ttl": TTL}


def para_status(now):
    """O campo `arquivo` do /status: o pedido vivo, ou None."""
    with _lock:
        pedido = _vivo(_estado["pedido"], now)
    if not pedido:
        return None
    return {"id": pedido["id"], "nome": pedido["nome"],
            "bytes": pedido["bytes"], "crc": pedido["crc"],
            "reiniciar": pedido["reiniciar"]}


def corpo_para_baixar(now):
    """O binario do pedido pendente. Devolve (codigo, bytes-ou-corpo-json).

    Serve APENAS o arquivo do pedido vivo, e nao um nome vindo da URL: o unico
    caminho que esta rota conhece e o que o `pedir` validou — nao ha string de
    fora virando caminho de arquivo aqui.
    """
    with _lock:
        pedido = _vivo(_estado["pedido"], now)
    if not pedido:
        return 404, {"error": "nenhum pedido pendente"}
    try:
        with open(os.path.join(RAIZ, pedido["nome"]), "rb") as f:
            return 200, f.read()
    except OSError:
        return 500, {"error": "arquivo sumiu da staging"}


def receber_ok(dados, now):
    """A placa terminou (gravou ou falhou). Devolve (codigo, corpo)."""
    ident = (dados or {}).get("id")
    if not isinstance(ident, int):
        return 400, {"error": "id ausente"}

    with _lock:
        pedido = _estado["pedido"]
        if not pedido or pedido["id"] != ident:
            return 409, {"error": "id nao e o do pedido pendente", "id": ident,
                         "pendente": pedido["id"] if pedido else None}
        _estado["pedido"] = None
        _estado["ultimo"] = {"id": ident, "nome": pedido["nome"],
                             "ok": bool((dados or {}).get("ok")), "ts": now}
        return 200, {"ok": True}


def estado(now):
    """Em que pe esta a atualizacao. E o que o disparador consulta esperando."""
    with _lock:
        pedido = _estado["pedido"]
        ultimo = _estado["ultimo"]

    if pedido:
        vivo = _vivo(pedido, now)
        return {"estado": "pendente" if vivo else "expirado",
                "id": pedido["id"], "nome": pedido["nome"],
                "idade_s": int(now - pedido["ts"])}
    if ultimo:
        return {"estado": "gravado" if ultimo["ok"] else "falhou",
                "id": ultimo["id"], "nome": ultimo["nome"],
                "idade_s": int(now - ultimo["ts"])}
    return {"estado": "vazio", "id": None, "nome": None, "idade_s": None}
