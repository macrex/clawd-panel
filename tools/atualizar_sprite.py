#!/usr/bin/env python3
"""Poe um arquivo no cartao da placa sem tirar o cartao dela.

    python tools\\atualizar_sprite.py sdcard\\clawd\\sp_cartman_reset.clw
    python tools\\atualizar_sprite.py arquivo.clw --api http://outro:8787
    python tools\\atualizar_sprite.py arquivo.clw --sem-reiniciar

O irmao do `tela.py`, no sentido contrario: SOBE o arquivo para a staging da
API (`POST /arquivo/subir` — a staging e da instancia que roda, possivelmente
em outra maquina), arma o pedido em `POST /arquivo/pedir` e espera.
O pedido viaja de carona no `/status` que a placa ja pede a cada 2 s; ela baixa
o binario, confere tamanho e CRC-32, grava em `/clawd/<nome>` no cartao
(`.tmp` + rename, entao queda no meio nao corrompe o que estava la) e reinicia
— o sprite novo vale no proximo boot, uns dez segundos depois.

O prazo cobre a volta inteira: poll (2 s) + download de ~600 KB pela pilha
WiFi (3-8 s) + gravacao no cartao (1-2 s). Estourar nao cancela: o pedido vive
ate o TTL da API (120 s), e a placa que aparecer tarde ainda o atende.

So stdlib, como o resto do servidor.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

API = "http://127.0.0.1:8787"
ESPERA = 45.0
INTERVALO = 0.5


def _json(url, corpo=None):
    req = urllib.request.Request(url)
    if corpo is not None:
        req.data = json.dumps(corpo).encode("utf-8")
        req.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read().decode("utf-8"))


def _subir(url, nome, dados):
    req = urllib.request.Request(url, data=dados)
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("X-Arquivo", f"nome={nome}")
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read().decode("utf-8"))


def atualizar(caminho: Path, base=API, reiniciar=True, espera=ESPERA):
    """Sobe, pede e espera. Devolve True quando a placa confirmou a gravacao."""
    try:
        subida = _subir(base + "/arquivo/subir", caminho.name,
                        caminho.read_bytes())
        print(f"na staging da API: {subida.get('bytes')} bytes",
              file=sys.stderr)
        pedido = _json(base + "/arquivo/pedir",
                       {"nome": caminho.name, "reiniciar": reiniciar})
    except urllib.error.HTTPError as erro:
        print(f"a API recusou o pedido: {erro.read().decode('utf-8', 'replace')}",
              file=sys.stderr)
        return False
    except (urllib.error.URLError, OSError, ValueError) as erro:
        print(f"a API nao respondeu ({base}): {erro}", file=sys.stderr)
        return False

    ident = pedido.get("id")
    print(f"pedido {ident} armado: {pedido.get('nome')} "
          f"({pedido.get('bytes')} bytes, crc {pedido.get('crc'):#010x}); "
          f"esperando a placa...", file=sys.stderr)

    limite = time.monotonic() + espera
    estado = {}
    while time.monotonic() < limite:
        time.sleep(INTERVALO)
        try:
            estado = _json(base + "/arquivo")
        except (urllib.error.URLError, OSError, ValueError):
            continue
        if estado.get("id") != ident:
            continue          # resposta de um pedido anterior
        if estado.get("estado") == "gravado":
            print("a placa gravou e "
                  + ("vai reiniciar" if reiniciar else "NAO vai reiniciar")
                  + "; o arquivo novo vale "
                  + ("no proximo boot (~10 s)" if reiniciar else "na proxima leitura"),
                  file=sys.stderr)
            return True
        if estado.get("estado") in ("falhou", "expirado"):
            break

    print(f"nao deu: {estado.get('estado') or 'sem resposta'} "
          f"(veja o `upd:` na serial da placa)", file=sys.stderr)
    return False


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("arquivo", type=Path, help="o arquivo a por no cartao")
    p.add_argument("--api", default=API, help="base da API (padrao: %(default)s)")
    p.add_argument("--sem-reiniciar", action="store_true",
                   help="grava sem reiniciar a placa (vale para arquivo que e "
                        "lido do cartao a cada uso, como os sp_*_reset)")
    p.add_argument("--espera", type=float, default=ESPERA)
    args = p.parse_args()

    if not args.arquivo.is_file():
        p.error(f"arquivo nao encontrado: {args.arquivo}")
    ok = atualizar(args.arquivo, args.api.rstrip("/"),
                   reiniciar=not args.sem_reiniciar, espera=args.espera)
    return 0 if ok else 1


# A staging NAO e resolvida aqui de proposito: ela pertence a instancia da API
# que atende (server/atualizacoes/ AO LADO DELA), e o primeiro uso real provou
# que a API em producao roda de outra pasta — e pode rodar de outra maquina.


if __name__ == "__main__":
    sys.exit(main())
