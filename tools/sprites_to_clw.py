#!/usr/bin/env python3
"""
Converte os sprites do clawd-tank (headers C) para arquivos .clw no cartao SD.

    python tools/sprites_to_clw.py <caminho-do-clawd-tank> <pasta-de-saida>

POR QUE UM FORMATO PROPRIO
O clawd-tank compila os sprites DENTRO do firmware, como headers C. Aqui eles
moram no SD: sao 314 KB que nao precisam ocupar flash, e trocar uma animacao
passa a ser copiar um arquivo em vez de recompilar.

O QUE NAO E FEITO AQUI, DE PROPOSITO
Rotacao. O painel e retrato e nos desenhamos em paisagem, entao o sprite precisa
girar 90 graus antes de ir para a tela. Seria natural girar aqui — e seria um
erro: o RLE comprime ao longo das linhas, e girar os pixels transforma corridas
horizontais (que comprimem 15x) em verticais (que nao comprimem nada). A rotacao
acontece na decodificacao, onde e de graca: o firmware ja escreve pixel a pixel
no buffer, entao so muda o indice de destino.

Escala tambem fica para a decodificacao, pelo mesmo motivo: guardar o sprite
ampliado multiplicaria o arquivo por 4 ou 9 sem ganhar nada.

FORMATO .clw  (little-endian)
    0   char[4]  "CLWD"
    4   u16      versao = 1
    6   u16      quantidade de frames
    8   u16      largura   (nativa, orientacao original)
    10  u16      altura
    12  u16      cor-chave transparente (RGB565)
    14  u16      milissegundos por frame
    16  u16      escala inteira a aplicar na decodificacao
    18  u16      reservado
    20  u32[frames+1]  deslocamentos, em CORRIDAS (nao em bytes)
    ..  corridas: { u16 valor, u16 repeticoes }
"""

import re
import struct
import sys
from pathlib import Path

# Animacao -> (ms por frame, estado que ela representa no nosso painel).
# Os tempos sao os do proprio clawd-tank: 6 fps no que e ambiente, 8-10 fps no
# que precisa de atencao. Copiados em vez de arbitrados — quem desenhou a
# animacao sabe em que velocidade ela le bem.
# Velocidade de cada animacao, em ms por quadro. Copiadas do scene.c do
# clawd-tank em vez de arbitradas: 6 fps no que e ambiente, 8-10 fps no que
# precisa de atencao. Quem desenhou sabe em que ritmo cada uma le bem.
FRAME_MS = {
    "alert": 100, "happy": 100,
    "idle": 167, "sleeping": 167, "disconnected": 167,
    "beacon": 125, "building": 125, "conducting": 125, "confused": 125,
    "debugger": 125, "dizzy": 125, "going_away": 125, "juggling": 125,
    "sweeping": 125, "thinking": 125, "typing": 125, "wizard": 125,
    "walking": 125, "mini_crab": 125,
}
DEFAULT_MS = 125

# O que o firmware usa HOJE. O resto vai para o cartao mesmo assim: espaco
# sobra, e ter o acervo inteiro la significa que uma melhoria futura e so
# mudar o firmware, sem desmontar a placa para chegar no cartao.
EM_USO = {
    "idle":       "turno encerrado",
    "alert":      "BLOQUEADO — precisa de voce",
    "typing":     "trabalhando",
    "sleeping":   "estado desconhecido",
    # "nenhuma sessao" usa going_away (o bicho indo embora) e NAO disconnected.
    # A `disconnected` do clawd-tank desenha um logo de Bluetooth enorme: la ela
    # significa "perdi o link BLE com o computador", que nao e o que falta aqui.
    "going_away": "nenhuma sessao",
}

# Area util da pagina 3, em coordenadas de tela (paisagem): quase toda a
# largura, e a faixa entre o cabecalho e a linha de texto. A escala de cada
# animacao e a maior potencia inteira que ainda cabe aqui — inteira para o
# pixel-art continuar nitido, sem interpolacao.
BOX_W, BOX_H = 440, 200

# Custo medido no nosso painel: um flush de 320x480 leva 63-70 ms, o que da
# ~0,42 us por pixel. Serve para prever o custo de cada animacao antes de gravar.
US_PER_PIXEL = 65_000 / (320 * 480)


def parse_header(path, prefix):
    """Extrai dimensoes, deslocamentos e corridas de um sprite_*.h."""
    src = path.read_text(encoding="utf-8", errors="replace")

    def const(name):
        m = re.search(rf"#define\s+{prefix.upper()}_{name}\s+(0x[0-9A-Fa-f]+|\d+)", src)
        if not m:
            raise ValueError(f"{path.name}: nao achei {prefix.upper()}_{name}")
        return int(m.group(1), 0)

    width, height = const("WIDTH"), const("HEIGHT")
    count, key = const("FRAME_COUNT"), const("TRANSPARENT_KEY")

    def array(name):
        m = re.search(rf"{prefix}_{name}\[[^\]]*\]\s*=\s*\{{(.*?)\}};", src, re.S)
        if not m:
            raise ValueError(f"{path.name}: nao achei {prefix}_{name}")
        return [int(t, 0) for t in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(1))]

    flat = array("rle_data")
    runs = list(zip(flat[0::2], flat[1::2]))     # o array e valor,contagem,valor,...
    # Os deslocamentos do clawd-tank contam WORDS de 16 bits, e cada corrida
    # ocupa dois. Convertidos aqui para indice de CORRIDA, que e o que o
    # decodificador realmente itera — a conversao mora num lugar so.
    offsets = [o // 2 for o in array("frame_offsets")]

    if len(offsets) != count + 1:
        raise ValueError(f"{path.name}: {len(offsets)} deslocamentos para {count} frames")
    # Cada frame tem que cobrir exatamente width*height pixels. Isto pega tanto
    # um header truncado quanto um erro meu de parsing, em vez de deixar a placa
    # desenhar lixo e eu ficar procurando defeito no lugar errado.
    for i in range(count):
        total = sum(c for _, c in runs[offsets[i]:offsets[i + 1]])
        if total != width * height:
            raise ValueError(
                f"{path.name}: frame {i} soma {total} pixels, esperado {width * height}")
    return width, height, count, key, offsets, runs


def fit_scale(w, h):
    """Maior escala inteira em que o sprite ainda cabe na area util."""
    s = min(BOX_W // w, BOX_H // h)
    return max(1, min(s, 4))


def write_clw(dest, width, height, key, frame_ms, scale, offsets, runs):
    blob = bytearray()
    blob += b"CLWD"
    blob += struct.pack("<HHHHHHHH", 1, len(offsets) - 1, width, height,
                        key, frame_ms, scale, 0)
    blob += struct.pack(f"<{len(offsets)}I", *offsets)
    for value, count in runs:
        blob += struct.pack("<HH", value, count)
    dest.write_bytes(blob)
    return len(blob)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    src_repo, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    assets = src_repo / "firmware" / "main" / "assets"
    if not assets.is_dir():
        print(f"erro: {assets} nao existe — o caminho aponta para o clawd-tank?")
        return 1
    out_dir.mkdir(parents=True, exist_ok=True)

    # Converte TUDO que existir, nao uma lista fixa: um sprite novo no
    # clawd-tank passa a ser copiado sem editar este arquivo.
    nomes = sorted(p.stem[len("sprite_"):] for p in assets.glob("sprite_*.h")
                   if p.stem != "sprite_ble_icon")

    print(f"{'animacao':<14}{'frames':>7}{'nativo':>10}{'esc':>5}{'na tela':>10}"
          f"{'arquivo':>10}{'blit':>8}{'carga':>7}  uso")
    total = 0
    convertidas = 0
    for name in nomes:
        path = assets / f"sprite_{name}.h"
        try:
            w, h, count, key, offsets, runs = parse_header(path, name)
        except ValueError as e:
            print(f"{name:<14} PULADA: {e}")
            continue
        frame_ms = FRAME_MS.get(name, DEFAULT_MS)
        scale = fit_scale(w, h)
        size = write_clw(out_dir / f"{name}.clw", w, h, key, frame_ms, scale, offsets, runs)
        total += size
        convertidas += 1

        px = (w * scale) * (h * scale)
        blit_ms = px * US_PER_PIXEL / 1000
        carga = blit_ms / frame_ms * 100
        uso = "<-- em uso" if name in EM_USO else ""
        print(f"{name:<14}{count:>7}{w:>5}x{h:<4}{scale:>4}x{w*scale:>6}x{h*scale:<4}"
              f"{size/1024:>8.0f}K{blit_ms:>7.1f}ms{carga:>6.0f}%  {uso}")

    print(f"\n{convertidas} animacoes, {total/1024:.0f} KB no cartao")
    print("\nusadas pelo firmware:")
    for name, papel in EM_USO.items():
        print(f"  {name:<14} {papel}")
    faltando = [n for n in EM_USO if n not in nomes]
    if faltando:
        print(f"\nATENCAO: o firmware espera {faltando}, que nao existem na origem")
    return 0


if __name__ == "__main__":
    sys.exit(main())
