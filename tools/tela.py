#!/usr/bin/env python3
"""Pede a tela da placa e espera o PNG cair no disco. Imprime o caminho.

    python tools\\tela.py                     # a API desta maquina
    python tools\\tela.py http://outro:8787   # a de outra

POR QUE ISTO PRECISA ESPERAR
A placa so faz requisicoes de SAIDA — nao ha para onde mandar um comando. O
pedido fica armado na API e viaja de carona no `/status` que ela ja busca a cada
dois segundos; so entao ela copia o framebuffer e o devolve num POST. A volta
inteira e o poll (ate 2 s) mais o envio de 300 KB pela pilha WiFi dela (0,5 a
1,5 s), e este programa fica olhando `GET /tela` ate o quadro chegar.

O prazo acompanha o TTL do pedido na API (30 s), e nao a volta tipica. Ele ja
foi de 8 s — "o dobro folgado" de uma volta que se supunha de 4 —, e isso era
uma conta errada: MEDIDO, o quadro leva ~10 s para chegar, porque a tarefa de
rede da placa faz o poll, o clima e a tela do terminal antes de despachar os
300 KB. O script desistia com a captura a caminho e imprimia "pendente", e o
efeito era o pior possivel — a foto CHEGAVA e era gravada, so que depois de
quem a pediu ja ter concluido que a placa nao respondeu.

Estourar nao cancela nada: o pedido continua armado ate o TTL da API, entao um
quadro atrasado ainda e gravado — o que acaba e a espera, nao a captura.

So stdlib, como o resto do servidor: esta ferramenta acompanha uma API que roda
no logon e nao pode depender de `pip install`.
"""

import json
import sys
import time
import urllib.error
import urllib.request

API = "http://127.0.0.1:8787"
ESPERA = 28.0       # teto da espera, em segundos: o TTL do pedido, menos folga
INTERVALO = 0.25    # entre consultas: barato contra 127.0.0.1, e responde rapido


def _json(url, metodo="GET"):
    req = urllib.request.Request(url, method=metodo)
    if metodo == "POST":
        # Corpo vazio de proposito: o pedido e o pedido, nao ha o que mandar.
        # O Content-Length explicito evita que o urllib mande um POST sem ele.
        req.data = b""
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read().decode("utf-8"))


def capturar(base=API, espera=ESPERA):
    """Dispara e espera. Devolve o caminho do PNG, ou None."""
    try:
        pedido = _json(base + "/tela/pedir", "POST")
    except (urllib.error.URLError, OSError, ValueError) as erro:
        print(f"a API nao respondeu ({base}): {erro}", file=sys.stderr)
        return None

    ident = pedido.get("id")
    print(f"pedido {ident} armado; esperando a placa...", file=sys.stderr)

    limite = time.monotonic() + espera
    estado = {}
    while time.monotonic() < limite:
        time.sleep(INTERVALO)
        try:
            estado = _json(base + "/tela")
        except (urllib.error.URLError, OSError, ValueError):
            continue        # a API pode estar reiniciando; o pedido nao morre
        # O id tem que bater: um `pronto` com id anterior e a captura de ontem,
        # e parar nele entregaria a tela errada com cara de tela nova.
        if estado.get("estado") == "pronto" and estado.get("id") == ident:
            return estado.get("arquivo")
        if estado.get("estado") == "expirado":
            break

    print(f"a placa nao mandou o quadro: {estado.get('estado') or 'sem resposta'}",
          file=sys.stderr)
    return None


def main():
    base = (sys.argv[1] if len(sys.argv) > 1 else API).rstrip("/")
    arquivo = capturar(base)
    if not arquivo:
        return 1
    print(arquivo)
    return 0


if __name__ == "__main__":
    sys.exit(main())
