#!/usr/bin/env python3
"""Testes da atualizacao de arquivo do cartao. So stdlib.

O que estas provas seguram, em ordem de importancia:

  1. O CRC e o TAMANHO congelados no pedido sao os do arquivo NA HORA DO
     PEDIDO. E contra eles que a placa confere 600 KB atravessando WiFi — um
     CRC calculado errado aqui reprova gravacao boa la, e o defeito aparece na
     serial da placa, longe deste codigo.
  2. Nome com caminho e RECUSADO. O nome viaja ate o firmware e vira
     `/clawd/<nome>`: uma barra aqui seria travessia de diretorio no cartao.
  3. O ciclo de estados que o disparador consome: pendente -> gravado/falhou,
     com id conferido — um `gravado` de pedido anterior nao pode encerrar a
     espera do pedido novo.
"""

import os
import shutil
import tempfile
import time
import unittest
import zlib

import arquivo


class Base(unittest.TestCase):
    def setUp(self):
        self._raiz_antiga = arquivo.RAIZ
        self._tmp = tempfile.mkdtemp()
        arquivo.RAIZ = self._tmp
        arquivo._estado["pedido"] = None
        arquivo._estado["ultimo"] = None
        self.now = time.time()

    def tearDown(self):
        arquivo.RAIZ = self._raiz_antiga
        shutil.rmtree(self._tmp, ignore_errors=True)

    def poe(self, nome, conteudo=b"x" * 1000):
        with open(os.path.join(self._tmp, nome), "wb") as f:
            f.write(conteudo)
        return conteudo


class TestUpload(Base):
    def test_upload_grava_na_staging_e_pedir_o_acha(self):
        dados = os.urandom(3000)
        code, corpo = arquivo.receber_upload("sp_x.clw", dados)
        self.assertEqual(code, 200)
        self.assertEqual(corpo["crc"], zlib.crc32(dados))
        code, pedido = arquivo.pedir({"nome": "sp_x.clw"}, self.now)
        self.assertEqual(code, 200)
        self.assertEqual(pedido["bytes"], 3000)

    def test_upload_recusa_nome_com_caminho(self):
        for nome in ("../x.clw", "a/b.clw", None, ""):
            code, _ = arquivo.receber_upload(nome, b"x")
            self.assertEqual(code, 400, nome)

    def test_upload_recusa_corpo_vazio_ou_gordo(self):
        self.assertEqual(arquivo.receber_upload("a.clw", b"")[0], 400)
        self.assertEqual(
            arquivo.receber_upload("a.clw", b"x" * (arquivo.MAX_BYTES + 1))[0],
            400)


class TestPedir(Base):
    def test_congela_tamanho_e_crc_do_arquivo(self):
        dados = self.poe("sp_teste.clw", os.urandom(5000))
        code, corpo = arquivo.pedir({"nome": "sp_teste.clw"}, self.now)
        self.assertEqual(code, 200)
        self.assertEqual(corpo["bytes"], 5000)
        self.assertEqual(corpo["crc"], zlib.crc32(dados))

    def test_trocar_o_arquivo_depois_nao_muda_o_pedido(self):
        self.poe("a.clw", b"primeiro")
        code, corpo = arquivo.pedir({"nome": "a.clw"}, self.now)
        crc_original = corpo["crc"]
        self.poe("a.clw", b"segundo conteudo maior")
        campo = arquivo.para_status(self.now)
        self.assertEqual(campo["crc"], crc_original)
        self.assertEqual(campo["bytes"], len(b"primeiro"))

    def test_recusa_nome_com_caminho(self):
        for nome in ("../x.clw", "a/b.clw", "a\\b.clw", ".oculto", ""):
            code, _ = arquivo.pedir({"nome": nome}, self.now)
            self.assertEqual(code, 400, nome)

    def test_recusa_arquivo_fora_da_staging(self):
        code, _ = arquivo.pedir({"nome": "nao_existe.clw"}, self.now)
        self.assertEqual(code, 404)

    def test_recusa_acima_do_teto(self):
        self.poe("gordo.clw", b"x" * (arquivo.MAX_BYTES + 1))
        code, _ = arquivo.pedir({"nome": "gordo.clw"}, self.now)
        self.assertEqual(code, 400)

    def test_pedido_novo_substitui_o_pendente(self):
        self.poe("a.clw")
        self.poe("b.clw")
        _, p1 = arquivo.pedir({"nome": "a.clw"}, self.now)
        _, p2 = arquivo.pedir({"nome": "b.clw"}, self.now)
        self.assertGreater(p2["id"], p1["id"])
        campo = arquivo.para_status(self.now)
        self.assertEqual(campo["nome"], "b.clw")

    def test_reiniciar_por_padrao_e_desligavel(self):
        self.poe("a.clw")
        arquivo.pedir({"nome": "a.clw"}, self.now)
        self.assertTrue(arquivo.para_status(self.now)["reiniciar"])
        arquivo.pedir({"nome": "a.clw", "reiniciar": False}, self.now)
        self.assertFalse(arquivo.para_status(self.now)["reiniciar"])


class TestStatusEBaixar(Base):
    def test_campo_some_depois_do_ttl(self):
        self.poe("a.clw")
        arquivo.pedir({"nome": "a.clw"}, self.now)
        self.assertIsNotNone(arquivo.para_status(self.now))
        self.assertIsNone(arquivo.para_status(self.now + arquivo.TTL + 1))

    def test_baixar_serve_o_binario_do_pedido(self):
        dados = self.poe("a.clw", os.urandom(2000))
        arquivo.pedir({"nome": "a.clw"}, self.now)
        code, corpo = arquivo.corpo_para_baixar(self.now)
        self.assertEqual(code, 200)
        self.assertEqual(corpo, dados)

    def test_baixar_sem_pedido_e_404(self):
        code, _ = arquivo.corpo_para_baixar(self.now)
        self.assertEqual(code, 404)

    def test_baixar_de_pedido_vencido_e_404(self):
        self.poe("a.clw")
        arquivo.pedir({"nome": "a.clw"}, self.now)
        code, _ = arquivo.corpo_para_baixar(self.now + arquivo.TTL + 1)
        self.assertEqual(code, 404)


class TestOkEEstado(Base):
    def test_ciclo_completo_gravado(self):
        self.poe("a.clw")
        _, pedido = arquivo.pedir({"nome": "a.clw"}, self.now)
        self.assertEqual(arquivo.estado(self.now)["estado"], "pendente")

        code, _ = arquivo.receber_ok({"id": pedido["id"], "ok": True}, self.now)
        self.assertEqual(code, 200)
        fim = arquivo.estado(self.now)
        self.assertEqual(fim["estado"], "gravado")
        self.assertEqual(fim["id"], pedido["id"])
        # O pedido foi consumido: nada mais viaja no /status.
        self.assertIsNone(arquivo.para_status(self.now))

    def test_falha_da_placa_vira_falhou(self):
        self.poe("a.clw")
        _, pedido = arquivo.pedir({"nome": "a.clw"}, self.now)
        arquivo.receber_ok({"id": pedido["id"], "ok": False}, self.now)
        self.assertEqual(arquivo.estado(self.now)["estado"], "falhou")

    def test_ok_com_id_errado_nao_encerra(self):
        self.poe("a.clw")
        _, pedido = arquivo.pedir({"nome": "a.clw"}, self.now)
        code, _ = arquivo.receber_ok({"id": pedido["id"] - 1, "ok": True},
                                     self.now)
        self.assertEqual(code, 409)
        self.assertEqual(arquivo.estado(self.now)["estado"], "pendente")

    def test_ok_sem_id_e_400(self):
        code, _ = arquivo.receber_ok({"ok": True}, self.now)
        self.assertEqual(code, 400)

    def test_estado_vazio(self):
        self.assertEqual(arquivo.estado(self.now)["estado"], "vazio")


if __name__ == "__main__":
    unittest.main()
