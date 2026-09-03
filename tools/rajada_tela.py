#!/usr/bin/env python3
"""Uma sequencia de capturas da tela, com o intervalo REAL entre cada uma.

    python tools\\rajada_tela.py                    # 10 capturas, so os PNG
    python tools\\rajada_tela.py --n 15 --gif        # e monta um GIF com elas
    python tools\\rajada_tela.py --n 8 --recorte fileira --gif

NAO E VIDEO DE VERDADE, E ISTO IMPORTA PARA LER O RESULTADO
A captura pega carona no poll que a placa ja faz a cada 2 s, e so DEPOIS disso
ela ainda manda 300 KB pela Wi-Fi — a volta inteira mede de 2 a 10 s por quadro
(ver tools/tela.py). Uma animacao de 33 ms por quadro (30 fps) e curta demais
para este canal enxergar: entre duas capturas cabem 60 a 300 quadros da
fileira, entao o que sai daqui e uma sequencia de POSES DISTANTES no tempo, boa
para conferir se a fileira esta VIVA (a pose muda de uma captura para a
outra) e qual desenho esta em cena — nao para julgar suavidade.

A pergunta "quantos quadros por segundo estao de fato saindo para a tela" tem
resposta melhor no `envFaixa` do pulso da serial (ver a mudanca que criou este
campo): ele conta o envio real, sem depender de nenhuma foto.

O QUE ESTE SCRIPT FAZ
Chama `tela.capturar()` `--n` vezes em sequencia, registra o intervalo REAL
entre cada uma (nao um numero suposto) e grava um `manifesto.json` ao lado dos
PNG. Com `--gif`, monta uma prancha animada com a duracao de cada quadro igual
ao intervalo medido — o GIF "mente devagar" na mesma proporcao que a captura
mentiu, em vez de fingir 30 fps que este canal nunca entregou.

`--recorte fileira` corta so a faixa do rodape (onde os bichos do tema padrao
ficam) antes de gravar, para nao carregar 320x480 inteiros so para olhar 40 px
de altura.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from tela import API, capturar          # noqa: E402

# A faixa do rodape DEITADO (480x320, a orientacao em que a placa sempre liga —
# ver CLAUDE.md): os bichos do tema padrao moram perto do chao (SCREEN_H-8),
# ver drawFooter em src/ui.cpp. Larga o bastante para cobrir o tema South Park,
# que e mais alto, e para nao cortar o quarto bicho.
#
# QUEM MEDIU FOI O PROPRIO SCRIPT, NAO A CONTA: a suposicao inicial (320x480,
# em pe) apontava para uma faixa toda preta — o boot padrao e deitado, e so
# olhando o PNG real (nao os comentarios do codigo) ficou claro onde a fileira
# estava. Se a placa girar para retrato, este recorte para de bater.
RECORTES = {
    "fileira": (0, 255, 245, 320),
}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("base", nargs="?", default=API)
    p.add_argument("--n", type=int, default=10, help="quantas capturas (padrao 10)")
    p.add_argument("--recorte", choices=sorted(RECORTES),
                   help="corta so esta faixa antes de gravar")
    p.add_argument("--gif", action="store_true",
                   help="monta rajada.gif com a duracao real de cada intervalo")
    p.add_argument("--saida", type=pathlib.Path,
                   default=pathlib.Path("rajada"),
                   help="pasta de saida (padrao .\\rajada)")
    args = p.parse_args()

    args.saida.mkdir(parents=True, exist_ok=True)
    quadros: list[dict] = []
    t0 = time.monotonic()

    for i in range(args.n):
        antes = time.monotonic()
        arquivo = capturar(args.base)
        if not arquivo:
            print(f"captura {i}: falhou, seguindo para a proxima", file=sys.stderr)
            continue
        depois = time.monotonic()
        quadros.append({
            "indice": i,
            "arquivo": arquivo,
            "t_desde_inicio_s": round(depois - t0, 2),
            "duracao_da_captura_s": round(depois - antes, 2),
        })
        print(f"[{i+1}/{args.n}] {arquivo}  (+{depois - antes:.1f}s)")

    if not quadros:
        print("nenhuma captura chegou", file=sys.stderr)
        return 1

    manifesto = args.saida / "manifesto.json"
    manifesto.write_text(json.dumps(quadros, indent=2, ensure_ascii=False),
                         encoding="utf-8")
    print(f"\n{len(quadros)}/{args.n} capturas — {manifesto}")

    if not args.gif:
        return 0

    try:
        from PIL import Image
    except ImportError:
        print("PIL ausente (pip install pillow); GIF nao montado", file=sys.stderr)
        return 1

    imagens = []
    duracoes_ms = []
    caixa = RECORTES[args.recorte] if args.recorte else None
    for j, q in enumerate(quadros):
        im = Image.open(q["arquivo"]).convert("RGB")
        if caixa:
            im = im.crop(caixa)
        imagens.append(im)
        # A duracao deste QUADRO no GIF e o tempo ate o PROXIMO — e nao a
        # duracao da propria requisicao, que mede so a espera de rede, nao a
        # janela que aquela pose ficou visivel. Ultimo quadro repete a media.
        if j + 1 < len(quadros):
            duracoes_ms.append(int((quadros[j + 1]["t_desde_inicio_s"]
                                    - q["t_desde_inicio_s"]) * 1000))
    if duracoes_ms:
        duracoes_ms.append(sum(duracoes_ms) // len(duracoes_ms))
    else:
        duracoes_ms = [2000]

    saida_gif = args.saida / "rajada.gif"
    imagens[0].save(saida_gif, save_all=True, append_images=imagens[1:],
                    duration=duracoes_ms, loop=0)
    print(f"{saida_gif}  ({len(imagens)} quadros, "
          f"{sum(duracoes_ms)/1000:.1f}s de duracao real)")
    print("LEMBRETE: os intervalos sao os do POLL da placa (2-10s por quadro),"
          " nao os da animacao — ver o docstring deste arquivo.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
