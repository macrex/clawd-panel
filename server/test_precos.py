#!/usr/bin/env python3
"""Testes da tabela de preco.

Os valores conferidos aqui foram calculados a mao a partir da tabela. Se um
preco mudar, ESTE arquivo e o lugar de atualizar junto — um teste que le a
propria tabela nao provaria nada.
"""

import unittest

import precos


class TestEntrada(unittest.TestCase):
    def test_id_exato(self):
        self.assertEqual(precos.entrada("claude-opus-5")["rotulo"], "Opus 5")

    def test_sufixo_de_variante(self):
        # A statusline publica "claude-opus-5[1m]".
        self.assertEqual(precos.entrada("claude-opus-5[1m]")["rotulo"], "Opus 5")

    def test_sufixo_de_data(self):
        # O transcript grava "claude-haiku-4-5-20251001".
        self.assertEqual(
            precos.entrada("claude-haiku-4-5-20251001")["rotulo"], "Haiku 4.5")

    def test_desconhecido(self):
        self.assertIsNone(precos.entrada("<synthetic>"))
        self.assertIsNone(precos.entrada(""))
        self.assertIsNone(precos.entrada(None))

    def test_rotulo_de_desconhecido_e_o_proprio_id(self):
        self.assertEqual(precos.rotulo("modelo-novo-99"), "modelo-novo-99")


class TestCusto(unittest.TestCase):
    def test_um_milhao_de_entrada(self):
        c = precos.custo("claude-opus-5", {"input": 1_000_000})
        self.assertAlmostEqual(c, 5.00, places=6)

    def test_um_milhao_de_saida(self):
        c = precos.custo("claude-opus-5", {"output": 1_000_000})
        self.assertAlmostEqual(c, 25.00, places=6)

    def test_cache_lido_e_um_decimo_da_entrada(self):
        c = precos.custo("claude-opus-5", {"cache_read": 1_000_000})
        self.assertAlmostEqual(c, 0.50, places=6)

    def test_cache_de_uma_hora_e_o_dobro_da_entrada(self):
        c = precos.custo("claude-opus-5", {"cache_1h": 1_000_000})
        self.assertAlmostEqual(c, 10.00, places=6)

    def test_cache_de_cinco_minutos(self):
        c = precos.custo("claude-opus-5", {"cache_5m": 1_000_000})
        self.assertAlmostEqual(c, 6.25, places=6)

    def test_mensagem_real(self):
        # Bloco `usage` copiado de um transcript desta maquina.
        #   2*5 + 954*25 + 453060*5*0.10 + 4384*5*2.00 = 294230 -> /1e6
        c = precos.custo("claude-opus-5", {
            "input": 2, "output": 954, "cache_1h": 4384,
            "cache_read": 453060, "cache_5m": 0})
        self.assertAlmostEqual(c, 0.294230, places=6)

    def test_modelo_fora_da_tabela_nao_e_estimado(self):
        self.assertIsNone(precos.custo("<synthetic>", {"input": 1_000_000}))

    def test_tokens_invalidos_valem_zero(self):
        c = precos.custo("claude-opus-5",
                         {"input": None, "output": True, "cache_read": "x"})
        self.assertAlmostEqual(c, 0.0, places=6)

    def test_modo_rapido_dobra_o_opus(self):
        c = precos.custo("claude-opus-5", {"output": 1_000_000}, rapido=True)
        self.assertAlmostEqual(c, 50.00, places=6)

    def test_modo_rapido_em_modelo_sem_preco_rapido_usa_o_normal(self):
        c = precos.custo("claude-haiku-4-5", {"output": 1_000_000}, rapido=True)
        self.assertAlmostEqual(c, 5.00, places=6)

    def test_promocao_do_sonnet_dentro_da_validade(self):
        c = precos.custo("claude-sonnet-5", {"input": 1_000_000},
                         dia="2026-08-04")
        self.assertAlmostEqual(c, 2.00, places=6)

    def test_promocao_do_sonnet_depois_da_validade(self):
        c = precos.custo("claude-sonnet-5", {"input": 1_000_000},
                         dia="2026-09-01")
        self.assertAlmostEqual(c, 3.00, places=6)

    def test_sem_dia_usa_preco_cheio(self):
        c = precos.custo("claude-sonnet-5", {"input": 1_000_000})
        self.assertAlmostEqual(c, 3.00, places=6)


if __name__ == "__main__":
    unittest.main(verbosity=2)
