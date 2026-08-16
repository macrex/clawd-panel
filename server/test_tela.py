#!/usr/bin/env python3
"""Testes do espelho da tela. Só stdlib — `python -m unittest test_tela`.

O que estas provas seguram, em ordem de importância:

  1. A FÓRMULA DA ROTAÇÃO. "Gira conforme `rot`" tem dois sentidos e os dois
     compilam: girar para o lado errado entrega a captura de cabeça para baixo,
     que é o defeito de 90° que esta ferramenta existe para encontrar nos
     outros. Um marcador num canto conhecido do framebuffer prova o sentido.
  2. O PNG escrito na mão abre de verdade — assinatura, IHDR e CRC conferidos
     byte a byte, sem Pillow, porque esta API não pode depender de `pip install`.
  3. Corpo do tamanho errado e id divergente são RECUSADOS sem gravar nada: uma
     captura errada é pior do que captura nenhuma, já que ninguém desconfia de
     uma imagem.

O leitor de PNG daqui é deliberadamente burro e independente do escritor: ele
não importa nada de `tela.py`, então um erro de deslocamento nos dois não se
cancela.
"""

import os
import shutil
import struct
import tempfile
import time
import unittest
import zlib

import claude_metrics_api as api
import tela

# A tela nativa da placa: 320×480 RGB565, sempre, mesmo deitada.
LARG, ALT = 320, 480
VERMELHO = 0xF800


def framebuffer(*acesos):
    """Um framebuffer preto de 320×480, com os pixels `(indice, cor)` acesos.

    O índice é o LINEAR na memória da placa, que é como o firmware entrega o
    quadro — sem passar por nenhuma conta de linha e coluna, que é justamente a
    conta que estes testes conferem.
    """
    fb = bytearray(LARG * ALT * 2)
    for i, cor in acesos:
        fb[i * 2] = cor & 0xFF
        fb[i * 2 + 1] = (cor >> 8) & 0xFF
    return bytes(fb)


def ler_png(dados):
    """Abre o PNG na mão. Devolve `(largura, altura, linhas)`.

    Cada linha vem já sem o byte de filtro, com três bytes por pixel. Confere a
    assinatura, o CRC de todo chunk e o tipo de imagem — um PNG que só "parece"
    válido passaria despercebido num teste que olhasse apenas as dimensões.
    """
    assert dados[:8] == b"\x89PNG\r\n\x1a\n", "assinatura de PNG"
    pos = 8
    largura = altura = None
    idat = bytearray()
    while pos < len(dados):
        (tam,) = struct.unpack(">I", dados[pos:pos + 4])
        tipo = dados[pos + 4:pos + 8]
        corpo = dados[pos + 8:pos + 8 + tam]
        (crc,) = struct.unpack(">I", dados[pos + 8 + tam:pos + 12 + tam])
        assert crc == zlib.crc32(tipo + corpo) & 0xFFFFFFFF, tipo
        if tipo == b"IHDR":
            largura, altura, prof, cor = struct.unpack(">IIBB", corpo[:10])
            assert (prof, cor) == (8, 2), "RGB de 8 bits, sem paleta"
        elif tipo == b"IDAT":
            idat += corpo
        pos += 12 + tam

    cru = zlib.decompress(bytes(idat))
    passo = 1 + largura * 3
    assert len(cru) == passo * altura, "linhas de menos ou de mais"
    linhas = []
    for y in range(altura):
        assert cru[y * passo] == 0, "filtro diferente de 0"
        linhas.append(cru[y * passo + 1:(y + 1) * passo])
    return largura, altura, linhas


def pixel(linhas, x, y):
    return tuple(linhas[y][x * 3:x * 3 + 3])


class Base(unittest.TestCase):

    def setUp(self):
        self.pasta = tempfile.mkdtemp(prefix="telas-")
        self._real = tela.PASTA
        tela.PASTA = self.pasta         # as capturas reais não são tocadas
        self.agora = time.time()
        api._captura["pedido"] = None
        api._captura["ultima"] = None

    def tearDown(self):
        tela.PASTA = self._real
        api._captura["pedido"] = None
        api._captura["ultima"] = None
        shutil.rmtree(self.pasta, ignore_errors=True)

    def cabecalho(self, ident, w=LARG, h=ALT, rot=1):
        return f"id={ident}; origem=0; w={w}; h={h}; rot={rot}; fmt=raw"

    def arquivos(self):
        return sorted(os.listdir(self.pasta))


class TestQuadro(Base):

    def test_o_png_abre_e_tem_as_dimensoes_da_tela_deitada(self):
        # O PNG sai como a tela ESTÁ SENDO OLHADA: o framebuffer é sempre
        # 320×480 nativo, então o painel deitado vira um PNG 480×320.
        caminho = tela.gravar(framebuffer(), LARG, ALT, rot=1,
                              pasta=self.pasta, agora=self.agora)
        self.assertIsNotNone(caminho)
        with open(caminho, "rb") as fh:
            largura, altura, _ = ler_png(fh.read())
        self.assertEqual((largura, altura), (ALT, LARG))

    def test_rot1_leva_o_marcador_do_framebuffer_para_o_canto_do_png(self):
        # A fórmula: PNG(dx, dy) = fb[dx * 320 + (319 - dy)]. Em (0,0) ela lê
        # fb[319] — o fim da primeira linha nativa. Girar para o outro lado
        # jogaria este pixel para o canto oposto, e a captura sairia de cabeça
        # para baixo sem que nada quebrasse.
        caminho = tela.gravar(framebuffer((319, VERMELHO)), LARG, ALT, rot=1,
                              pasta=self.pasta, agora=self.agora)
        with open(caminho, "rb") as fh:
            largura, altura, linhas = ler_png(fh.read())

        # Exatamente 255, e não 248: a conversão replica os bits altos
        # (`r8 = r5<<3 | r5>>2`). Sem isso o vermelho puro do painel chegaria
        # aqui como 248,0,0 e toda comparação de cor sairia deslocada.
        self.assertEqual(pixel(linhas, 0, 0), (255, 0, 0))
        # E só ele: um marcador que "vaza" para o vizinho denunciaria erro de
        # passo na fatia que faz a rotação.
        self.assertEqual(pixel(linhas, 1, 0), (0, 0, 0))
        self.assertEqual(pixel(linhas, 0, 1), (0, 0, 0))
        self.assertEqual((largura, altura), (ALT, LARG))

    def test_a_captura_21_apaga_a_mais_antiga(self):
        # Framebuffer minúsculo de propósito: o que está sendo provado é a
        # retenção, e converter 21 telas inteiras só para contar arquivos
        # gastaria segundos de suíte sem provar nada a mais.
        for i in range(tela.MANTER + 1):
            self.assertIsNotNone(
                tela.gravar(b"\x00" * 8, 2, 2, rot=0, pasta=self.pasta,
                            agora=self.agora + i))
        nomes = self.arquivos()
        self.assertEqual(len(nomes), tela.MANTER)
        # A que sobrou de fora é a PRIMEIRA: a ordem vem do nome, que é o
        # carimbo de tempo.
        primeira = time.strftime("tela-%Y%m%d-%H%M%S.png",
                                 time.localtime(self.agora))
        self.assertNotIn(primeira, nomes)


class TestPedido(Base):

    def test_corpo_de_tamanho_errado_e_recusado_sem_gravar_nada(self):
        pedido = api.pedir_captura(self.agora)
        codigo, corpo = api.receber_captura(self.cabecalho(pedido["id"]),
                                            b"\x00" * 1000, self.agora)
        # 400 e não 409, mesmo com um pedido armado: o tamanho é conferido
        # antes do id porque um corpo truncado é defeito do envio, e responder
        # 409 mandaria procurar o problema em sincronia de pedidos.
        self.assertEqual(codigo, 400)
        self.assertEqual(corpo["esperado"], LARG * ALT * 2)
        self.assertEqual(self.arquivos(), [])

    def test_id_divergente_do_pendente_e_recusado(self):
        # O caso real: um segundo `/pedir` chega enquanto a placa ainda estava
        # enviando o primeiro quadro.
        pedido = api.pedir_captura(self.agora)
        codigo, corpo = api.receber_captura(self.cabecalho(pedido["id"] + 1),
                                            framebuffer(), self.agora)
        self.assertEqual(codigo, 409)
        self.assertEqual(corpo["pendente"], pedido["id"])
        self.assertEqual(self.arquivos(), [])

    def test_dois_pedidos_seguidos_deixam_um_so_com_o_id_maior(self):
        primeiro = api.pedir_captura(self.agora)
        segundo = api.pedir_captura(self.agora + 1)
        self.assertGreater(segundo["id"], primeiro["id"])

        # O primeiro deixou de existir: a placa manda UMA tela, e guardar os dois
        # faria a segunda gravar por cima da primeira.
        codigo, _ = api.receber_captura(self.cabecalho(primeiro["id"]),
                                        framebuffer(), self.agora + 1)
        self.assertEqual(codigo, 409)

        codigo, corpo = api.receber_captura(self.cabecalho(segundo["id"]),
                                            framebuffer(), self.agora + 1)
        self.assertEqual(codigo, 200)
        self.assertEqual(corpo["bytes"], LARG * ALT * 2)
        self.assertEqual(api.estado_captura(self.agora + 1)["estado"], "pronto")

    def test_pedido_nao_atendido_expira_depois_do_ttl(self):
        # Sem o TTL o pedido ficaria armado para o próximo boot da placa, e a
        # imagem chegaria descrevendo uma tela que ninguém pediu.
        pedido = api.pedir_captura(self.agora)
        dentro = api.estado_captura(self.agora + api.CAPTURA_TTL - 1)
        self.assertEqual(dentro["estado"], "pendente")

        depois = self.agora + api.CAPTURA_TTL + 1
        self.assertEqual(api.estado_captura(depois)["estado"], "expirado")
        # E o quadro atrasado não é aceito: ele descreveria uma tela que já mudou.
        codigo, _ = api.receber_captura(self.cabecalho(pedido["id"]),
                                        framebuffer(), depois)
        self.assertEqual(codigo, 409)
        self.assertEqual(self.arquivos(), [])


class TestStatus(Base):
    """O campo `captura` do `/status` — o único caminho até a placa.

    Ela só faz requisições de saída, então o pedido tem que viajar de carona na
    resposta que ela já busca a cada dois segundos.
    """

    def tearDown(self):
        api._sessions.clear()
        super().tearDown()

    def test_o_pedido_sai_nos_dois_retornos_de_build_status(self):
        # `build_status` tem DOIS retornos, e o mesmo erro já aconteceu com o
        # campo `bloqueio`: implementar só o retorno normal quebra a captura
        # justamente quando não há sessão aberta — que é quando se ajusta
        # relógio, clima e cabeçalho, ou seja, quando mais se pede captura.
        pedido = api.pedir_captura(time.time())

        api._sessions.clear()
        ocioso = api.build_status()
        self.assertEqual(ocioso["sessions"], 0)
        self.assertEqual(ocioso["captura"], {"id": pedido["id"]})

        agora = time.time()
        api._sessions["s1"] = {
            "data": {"session_id": "s1", "repo": "esp32-s3", "context_pct": 30},
            "ts": agora, "state": api.WORKING, "state_ts": agora,
            "limits_ts": agora,
        }
        normal = api.build_status()
        self.assertEqual(normal["sessions"], 1)
        self.assertEqual(normal["captura"], {"id": pedido["id"]})

    def test_sem_pedido_o_campo_e_nulo(self):
        api._sessions.clear()
        self.assertIsNone(api.build_status()["captura"])


if __name__ == "__main__":
    unittest.main()
