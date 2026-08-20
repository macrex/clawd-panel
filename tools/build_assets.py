#!/usr/bin/env python3
"""Compila os sprites que o firmware referencia num blob para a particao
`assets` da flash.

    python tools\\build_assets.py            # gera assets.bin e relata
    python tools\\build_assets.py --lista    # so relata, nao escreve

POR QUE ISTO EXISTE
O cartao desta placa trava em 0x107 e nao volta por software. Enquanto os
sprites moram la, cada travamento deixa o painel desenhando buracos. Na flash
eles nao dependem de nada que possa travar.

O QUE ENTRA
So o que o firmware REFERENCIA — os nomes literais em src/*.cpp mais a fileira
`sp_*`, que e montada por prefixo. Embutir a pasta inteira gravaria 26 MB de
bichos de nivel que ninguem mais desenha (a pagina do nivel perdeu o sprite).
"""

from __future__ import annotations

import argparse
import pathlib
import re
import struct
import sys
import zlib

RAIZ  = pathlib.Path(__file__).resolve().parent.parent
FONTE = RAIZ / "sdcard" / "clawd"
SAIDA = RAIZ / "assets.bin"

MAGIC     = b"CLWA"
VERSAO    = 1
NOME_MAX  = 24
ENTRADA   = NOME_MAX + 12
CABECALHO = 12

# O tamanho da particao `assets` em partitions_clawd.csv. Bater aqui e o que
# transforma "nao coube" em erro de build em vez de flash corrompida.
TETO = 0x640000      # 6,25 MB


def _deflate_raw(dados: bytes) -> bytes:
    """Deflate RAW, sem cabecalho zlib.

    O `wbits` negativo e o detalhe que decide: o `tinfl` da ROM do ESP32-S3
    assume raw, e um cabecalho zlib faria a descompressao falhar NA PLACA e
    passar batido aqui — o pior lugar para descobrir.
    """
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(dados) + c.flush()


def referenciados() -> list[str]:
    """Os nomes que o firmware abre, lidos do proprio codigo.

    Derivar do codigo e nao de uma lista a mao: uma lista a mao envelhece em
    silencio, e o sintoma seria um sprite faltando na tela semanas depois.
    """
    src = "\n".join(p.read_text(encoding="utf-8", errors="replace")
                    for p in sorted((RAIZ / "src").glob("*.cpp")))

    nomes = set(re.findall(r'"([a-z_0-9]+)\.clw"', src))
    nomes |= set(re.findall(r"/clawd/([a-z_0-9]+)\.clw", src))

    # O rodizio do cabecalho e um array de nomes sem extensao.
    rod = re.search(r"const char \*RODIZIO\[\][^;]*?\{(.*?)\};", src, re.S)
    if rod:
        nomes |= set(re.findall(r'"([a-z_0-9]+)"', rod.group(1)))

    # Nomes montados por printf ("clawd_level_%03d") nao viram arquivo.
    nomes = {n for n in nomes if "%" not in n}

    # A fileira e escolhida por TEMA, com o prefixo montado em tempo de
    # execucao, entao nenhum nome dela aparece literal no codigo.
    nomes |= {p.stem for p in FONTE.glob("sp_*.clw")}

    return sorted(nomes)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--lista", action="store_true", help="so relata")
    args = ap.parse_args()

    if not FONTE.is_dir():
        sys.exit(f"falta {FONTE} — a arte nao esta nesta maquina")

    entradas, faltando = [], []
    for nome in referenciados():
        if len(nome) >= NOME_MAX:
            sys.exit(f"nome longo demais para a tabela ({NOME_MAX-1}): {nome}")
        arq = FONTE / f"{nome}.clw"
        if not arq.exists():
            faltando.append(nome)
            continue
        cru = arq.read_bytes()
        entradas.append((nome, _deflate_raw(cru), len(cru)))

    entradas.sort(key=lambda e: e[0])   # a busca na placa e binaria

    corpo = CABECALHO + len(entradas) * ENTRADA
    tabela, dados, pos = b"", b"", corpo
    for nome, comp, cru in entradas:
        tabela += nome.encode("ascii").ljust(NOME_MAX, b"\0")
        tabela += struct.pack("<III", pos, len(comp), cru)
        dados  += comp
        pos    += len(comp)

    blob = (MAGIC + struct.pack("<HHI", VERSAO, len(entradas), 0)
            + tabela + dados)

    total_cru = sum(e[2] for e in entradas)
    print(f"{len(entradas)} sprites")
    print(f"  cru        : {total_cru/1048576:6.2f} MB")
    print(f"  no blob    : {len(blob)/1048576:6.2f} MB "
          f"({100*len(blob)/total_cru:.0f}%)")
    print(f"  particao   : {TETO/1048576:6.2f} MB "
          f"(folga {(TETO-len(blob))/1048576:.2f} MB)")
    if faltando:
        print(f"  CITADOS E AUSENTES ({len(faltando)}): {', '.join(faltando)}")
        print("  estes vao continuar sendo lidos do cartao.")

    if len(blob) > TETO:
        sys.exit(f"NAO CABE: {len(blob)} > {TETO}")

    if args.lista:
        return 0

    SAIDA.write_bytes(blob)
    print(f"\n{SAIDA}")
    print(f"gravar com: esptool --chip esp32s3 -p COMx --before usb-reset "
          f"--after watchdog-reset write-flash 0x650000 assets.bin")
    return 0


if __name__ == "__main__":
    sys.exit(main())
