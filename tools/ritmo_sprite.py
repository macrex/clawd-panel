#!/usr/bin/env python3
"""Muda o ritmo (`frame_ms`) de um sprite .clw sem recompilar a arte.

    python tools\\ritmo_sprite.py sdcard\\clawd\\sp_*.clw            # so lista
    python tools\\ritmo_sprite.py sdcard\\clawd\\sp_*.clw --teto 50  # acelera quem passa

POR QUE ISTO EXISTE
O ritmo de cada animacao vem do proprio arquivo, e alguns foram compilados bem
mais lentos do que o painel consegue mostrar: `sp_cartman_trabalho` pede 120 ms
por quadro (8,3 quadros por segundo) enquanto a placa entrega ~20. Nenhum
conserto no laco passa desse teto — quem manda no ritmo e o arquivo.

O QUE ELE FAZ
Reescreve DOIS BYTES do cabecalho. O `frame_ms` mora no offset 14 (ver
`clw_format.parse_clw`), e nada mais no arquivo depende dele: as corridas, a
tabela de offsets e o tamanho ficam intactos. Nao ha recodificacao, entao a
operacao e reversivel — `--ms` devolve qualquer valor antigo.

`--teto` so ABAIXA. Um teto de 50 acelera o que pede 120 e nao encosta no que
ja pede 40: subir o ritmo de quem ja e rapido nao era o pedido, e faria uma
animacao calibrada a mao regredir sem ninguem notar.

DEPOIS DE MEXER, REGRAVE A FLASH. O firmware le os sprites da particao `assets`
e so cai para o cartao quando nao acha (ver src/assets.h) — mudar o arquivo em
sdcard\\clawd\\ nao muda o painel enquanto a particao trouxer o ritmo velho:

    python tools\\build_assets.py
    esptool --chip esp32s3 -p COMx --before usb-reset --after watchdog-reset \\
        write-flash 0x650000 assets.bin
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from clw_format import ClwError, parse_clw       # noqa: E402

# O `frame_ms` e o sexto campo de oito `uint16` que comecam no offset 4.
OFFSET_FRAME_MS = 4 + 5 * 2


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("arquivos", nargs="+", type=pathlib.Path)
    g = p.add_mutually_exclusive_group()
    g.add_argument("--teto", type=int, metavar="MS",
                   help="abaixa para MS quem pedir mais que isso")
    g.add_argument("--ms", type=int, metavar="MS",
                   help="grava MS em todos os arquivos, subindo ou descendo")
    args = p.parse_args()

    alvo = args.teto if args.teto is not None else args.ms
    if alvo is not None and not 1 <= alvo <= 0xFFFF:
        sys.exit("o ritmo tem que caber num uint16 e nao pode ser zero")

    mudados = 0
    for arq in sorted(args.arquivos):
        try:
            dados = arq.read_bytes()
            sp = parse_clw(dados)
        except (OSError, ClwError) as e:
            print(f"{arq.name:32s} IGNORADO ({e})")
            continue

        novo = sp.frame_ms
        if args.ms is not None:
            novo = args.ms
        elif args.teto is not None and sp.frame_ms > args.teto:
            novo = args.teto

        ciclo = sp.frames * novo
        marca = "" if novo == sp.frame_ms else f"  ->{novo:4d} ms"
        print(f"{arq.name:32s} {sp.frames:3d} quadros  {sp.frame_ms:4d} ms"
              f"{marca:14s}  ciclo {ciclo/1000:4.1f} s")

        if novo == sp.frame_ms:
            continue

        saida = bytearray(dados)
        struct.pack_into("<H", saida, OFFSET_FRAME_MS, novo)
        # Reconferido depois do patch: um cabecalho que nao sobrevive ao proprio
        # parser vira sprite ausente na placa, e la o sintoma e um buraco na
        # tela sem mensagem nenhuma.
        parse_clw(bytes(saida))
        arq.write_bytes(bytes(saida))
        mudados += 1

    if alvo is None:
        print("\nnada gravado (rode com --teto ou --ms)")
    else:
        print(f"\n{mudados} arquivo(s) reescrito(s)")
        if mudados:
            print("agora: python tools\\build_assets.py  e regravar a particao")
    return 0


if __name__ == "__main__":
    sys.exit(main())
