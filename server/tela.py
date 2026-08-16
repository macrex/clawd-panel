#!/usr/bin/env python3
"""O quadro que a placa mandou, virando PNG no disco.

POR QUE ISTO EXISTE
Toda correção de layout do painel passava por uma foto: o agente desenha, você
grava, aponta o celular para a tela e cola a imagem no chat. Quem desenha não vê
o que desenhou, e a correção sai de uma descrição em texto de um defeito visual.
A placa já pergunta `/status` a cada 2 s; o quadro volta de carona nesse ciclo.

O QUE CHEGA AQUI
O framebuffer como ele está na memória da placa: 320×480 palavras RGB565
little-endian, 307.200 bytes, sem compressão. O tamanho é FIXO de propósito —
um RLE16 economizaria 80% do POST e custaria um compressor em C++, um módulo
puro só para poder testá-lo no PC, e uma validação de expansão deste lado. O
tamanho fixo elimina as quatro coisas de uma vez: o corpo tem exatamente o
tamanho que o cabeçalho declara, ou é recusado.

DISCIPLINA DESTE MÓDULO (a mesma do weather.py e do works.py)

  - Só stdlib, e o PNG é escrito na mão com `zlib` + `struct`. Esta API sobe no
    logon, e um acessório dela não pode ser o motivo de a máquina precisar de
    `pip install`. Um PNG RGB de 8 bits são três chunks e um CRC.
  - Não conhece HTTP. Quem fala com a rede é `claude_metrics_api.py`.
  - NUNCA levanta exceção para fora. Uma captura de diagnóstico que falha não
    pode derrubar o painel — o pior caso aceitável é não haver imagem.
"""

import array
import os
import struct
import sys
import time
import zlib

AQUI = os.path.dirname(os.path.abspath(__file__))


def _galeria():
    """A pasta de capturas de tela do Windows, se ela existir.

    A foto do painel é uma captura de tela como qualquer outra, e o lugar onde
    se procura por captura de tela nesta máquina é a galeria — não uma pasta
    dentro do repositório, que só quem conhece o projeto sabe abrir.

    O caminho é montado a partir do perfil do usuário e não fixo: o nome da
    pasta é localizado (`Capturas de tela` em português, `Screenshots` em
    inglês), e o OneDrive pode ou não estar no meio. Nada disso existindo, a
    pasta ao lado do script continua valendo — a captura nunca deixa de ser
    gravada por causa de onde ela ia morar.
    """
    lar = os.path.expanduser("~")
    for meio in (os.path.join(lar, "OneDrive"), lar):
        for imagens in ("Imagens", "Pictures"):
            for nome in ("Capturas de tela", "Screenshots"):
                p = os.path.join(meio, imagens, nome)
                if os.path.isdir(p):
                    return p
    return None


PASTA = _galeria() or os.path.join(AQUI, "capturas")

# Quantas capturas sobrevivem. Cada uma pesa ~100 KB e a pergunta que elas
# respondem — "como isso ficou?" — tem validade de minutos: a vigésima primeira
# captura de uma sessão de ajuste já não fala da mesma tela que a primeira.
MANTER = 20

# RGB565 -> 8 bits por canal, com REPLICAÇÃO DOS BITS ALTOS (`r8 = r5<<3|r5>>2`),
# e não com um deslocamento simples. Sem ela o branco de 5 bits (31) sairia como
# 248 e o de 6 bits (63) como 252: o PNG mostraria 248,252,248 onde o painel
# pintou branco, e toda comparação de cor sairia deslocada — numa ferramenta
# cujo uso inteiro é conferir cor e posição.
_R5 = [(v << 3) | (v >> 2) for v in range(32)]
_G6 = [(v << 2) | (v >> 4) for v in range(64)]


def confere(corpo, w, h):
    """O corpo tem exatamente os `w*h*2` bytes que o cabeçalho declara?

    É o único contrato do formato, e por isso a checagem mora aqui e não em quem
    recebe o POST: quem grava é quem sabe o que precisa para gravar. Um corpo
    curto viraria um PNG com lixo no fim, e um longo esconderia que a placa e o
    PC discordam sobre o tamanho da tela.
    """
    if not isinstance(w, int) or not isinstance(h, int) or w <= 0 or h <= 0:
        return False
    return len(corpo) == w * h * 2


def _palavras(corpo):
    """O framebuffer como palavras de 16 bits, na ordem em que a placa as tem."""
    px = array.array("H")
    px.frombytes(corpo)
    # `frombytes` lê na ordem da MÁQUINA e o corpo vem little-endian, que é como
    # a memória do ESP32-S3 está. As duas coincidem em todo PC onde isto roda; a
    # troca existe para que a regra fique escrita no código e não no acaso.
    if sys.byteorder != "little":
        px.byteswap()
    return px


def _rgb888(palavras):
    """Uma linha de RGB565 virando os três bytes por pixel que o PNG quer."""
    fora = bytearray(len(palavras) * 3)
    i = 0
    for v in palavras:
        fora[i] = _R5[v >> 11]
        fora[i + 1] = _G6[(v >> 5) & 0x3F]
        fora[i + 2] = _R5[v & 0x1F]
        i += 3
    return fora


def _linhas(px, w, h, rot):
    """As linhas do PNG, já rodadas e já em RGB888.

    O PNG sai como a tela ESTÁ SENDO OLHADA: o framebuffer é sempre 320×480
    nativo, e o modo deitado é uma rotação do espaço de desenho, não do envio.
    Com `rot=1`, o mapeamento do `Arduino_Canvas` é

        PNG(dx, dy) = fb[dx * 320 + (319 - dy)]     dx em [0,480), dy em [0,320)

    A fórmula fica escrita porque "gira conforme `rot`" tem dois sentidos e os
    dois compilam: girar para o lado errado entrega a captura de cabeça para
    baixo, que é o defeito de 90° que este campo existe para evitar.

    Em código ela é uma fatia só: a linha `dy` do PNG é a coluna `w-1-dy` do
    framebuffer, ou seja, os pixels de `w-1-dy` em diante de `w` em `w`.

    Qualquer outro `rot` sai sem rotação — o framebuffer nativo, 320×480, que é
    exatamente o que o painel em pé mostra.
    """
    if rot == 1:
        for dy in range(w):
            yield _rgb888(px[(w - 1 - dy)::w])
    else:
        for y in range(h):
            yield _rgb888(px[y * w:(y + 1) * w])


def _chunk(tipo, dados):
    """Um chunk de PNG: tamanho, tipo, dados, e o CRC do tipo com os dados."""
    return (struct.pack(">I", len(dados)) + tipo + dados +
            struct.pack(">I", zlib.crc32(tipo + dados) & 0xFFFFFFFF))


def _png(linhas, largura, altura):
    """PNG RGB de 8 bits sem paleta: assinatura e três chunks.

    Filtro 0 (nenhum) em toda linha. Os filtros do PNG existem para a linha
    comprimir melhor, e implementar os quatro preditores para encolher um
    arquivo que vive minutos no disco de quem pediu não paga a conta.
    """
    cru = bytearray()
    for linha in linhas:
        cru.append(0)
        cru += linha
    return (b"\x89PNG\r\n\x1a\n" +
            _chunk(b"IHDR", struct.pack(">IIBBBBB",
                                        largura, altura, 8, 2, 0, 0, 0)) +
            _chunk(b"IDAT", zlib.compress(bytes(cru))) +
            _chunk(b"IEND", b""))


def podar(pasta=None, manter=MANTER):
    """Apaga as capturas antigas, deixando as `manter` mais recentes.

    A ordem sai do NOME e não da data do arquivo: o nome É o carimbo de tempo,
    então a ordem alfabética já é a cronológica — e ela continua certa depois de
    copiar a pasta, o que reescreve toda data de modificação.

    O FILTRO `tela-` NÃO É DETALHE. Esta função roda dentro da galeria de
    capturas do Windows, onde moram as capturas de tela do usuário — apagar por
    idade sem olhar o nome destruiria o que não é nosso. Só o que esta função
    escreveu é candidato a ser apagado por ela.
    """
    destino = pasta or PASTA
    try:
        nomes = sorted(n for n in os.listdir(destino)
                       if n.startswith("tela-") and n.endswith(".png"))
        for velho in nomes[:-manter]:
            os.remove(os.path.join(destino, velho))
    except OSError:
        pass        # limpeza é higiene, nunca motivo para perder a captura nova


def gravar(corpo, w, h, rot=1, pasta=None, agora=None):
    """Escreve o PNG e devolve o caminho. `None` quando não deu.

    Engole todo erro de propósito — disco cheio, permissão negada, pasta que
    sumiu. Nada disso pode subir para o handler e derrubar a thread do POST:
    quem pediu a captura fica sem a imagem, e o painel continua de pé.
    """
    if not confere(corpo, w, h):
        return None
    destino = pasta or PASTA
    try:
        px = _palavras(bytes(corpo))
        # Deitado, o PNG tem as medidas trocadas: 320×480 nativo vira 480×320.
        largura, altura = (h, w) if rot == 1 else (w, h)
        dados = _png(_linhas(px, w, h, rot), largura, altura)
        os.makedirs(destino, exist_ok=True)
        quando = time.localtime(agora if agora is not None else time.time())
        caminho = os.path.join(destino,
                               time.strftime("tela-%Y%m%d-%H%M%S.png", quando))
        with open(caminho, "wb") as fh:
            fh.write(dados)
    except Exception:
        return None
    podar(destino)
    return caminho
