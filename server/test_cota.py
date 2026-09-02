#!/usr/bin/env python3
"""Testes da leitura de cota. Só stdlib — `python test_cota.py`.

O que estas provas seguram, em ordem de importância:

  1. A cota do MODELO sai de `limits[]`, e não de um campo com o nome dele. O
     payload real tem `seven_day_opus` e `seven_day_sonnet` nulos ao lado da
     lista que carrega o dado de verdade — ler pelo campo daria None para
     sempre, em silêncio.
  2. Formato inesperado NUNCA levanta exceção. Este endpoint não é contrato
     público: no dia em que ele mudar, a barra do painel tem que cair na
     reserva local, e não derrubar a API.
"""

import time
import unittest

import cota


# O payload real, reduzido ao que importa (capturado da conta em 28/08/2026).
PAYLOAD = {
    "five_hour": {"utilization": 11.0, "resets_at": "2026-08-28T15:20:00+00:00"},
    "seven_day": {"utilization": 57.0, "resets_at": "2026-08-30T02:00:00+00:00"},
    # Os campos por modelo EXISTEM e vêm nulos: é a armadilha que o teste 1 trava.
    "seven_day_opus": None,
    "seven_day_sonnet": None,
    "limits": [
        {"kind": "session", "percent": 11, "scope": None},
        {"kind": "weekly_all", "percent": 57, "scope": None},
        {"kind": "weekly_scoped", "percent": 46,
         "scope": {"model": {"id": None, "display_name": "Fable"}}},
    ],
}


class TestExtrair(unittest.TestCase):

    def test_a_cota_do_modelo_sai_da_lista_e_nao_do_campo(self):
        d = cota.extrair(PAYLOAD)
        self.assertEqual(d["modelos"], {"Fable": 46})
        self.assertEqual(d["session"], 11)
        self.assertEqual(d["week"], 57)

    def test_varios_modelos_escopados_entram_todos(self):
        """Hoje só o Fable aparece; outro modelo entra sem tocar no código."""
        p = dict(PAYLOAD)
        p["limits"] = PAYLOAD["limits"] + [
            {"kind": "weekly_scoped", "percent": 12,
             "scope": {"model": {"display_name": "Opus"}}}]
        self.assertEqual(cota.extrair(p)["modelos"], {"Fable": 46, "Opus": 12})

    def test_escopado_sem_modelo_e_ignorado(self):
        """`scope` pode trazer `surface` em vez de `model` — não é cota de modelo."""
        p = dict(PAYLOAD)
        p["limits"] = [{"kind": "weekly_scoped", "percent": 30,
                        "scope": {"model": None, "surface": "cowork"}}]
        self.assertNotIn("modelos", cota.extrair(p))

    def test_percentual_serrado_e_arredondado(self):
        p = {"five_hour": {"utilization": 56.99999999999999},
             "seven_day": {"utilization": 140}}
        d = cota.extrair(p)
        self.assertEqual(d["session"], 57)
        self.assertEqual(d["week"], 100)

    def test_formato_inesperado_nao_levanta(self):
        """O endpoint não é contrato público: mudar de forma não pode explodir."""
        for lixo in (None, [], "texto", 42, {}, {"limits": "nao e lista"},
                     {"limits": [None, 3, {"kind": "weekly_scoped"}]},
                     {"five_hour": "nao e dict"},
                     {"limits": [{"kind": "weekly_scoped", "percent": None,
                                  "scope": {"model": {"display_name": "X"}}}]}):
            self.assertIsInstance(cota.extrair(lixo), dict)

    def test_booleano_nao_vira_percentual(self):
        """`True` é int em Python, e passaria como 1% sem a guarda."""
        d = cota.extrair({"five_hour": {"utilization": True}})
        self.assertNotIn("session", d)


class TestSnapshot(unittest.TestCase):

    def setUp(self):
        cota._uso, cota._uso_ts = None, 0.0

    def tearDown(self):
        cota._uso, cota._uso_ts = None, 0.0

    def test_sem_leitura_devolve_vazio(self):
        self.assertEqual(cota.snapshot(), {})
        self.assertIsNone(cota.fable())

    def test_leitura_boa_responde_com_idade(self):
        agora = time.time()
        cota._uso, cota._uso_ts = cota.extrair(PAYLOAD), agora - 30
        self.assertEqual(cota.fable(agora), 46)
        self.assertEqual(cota.snapshot(agora)["age"], 30)

    def test_leitura_velha_e_descartada(self):
        """Passada a validade, quem chama cai na reserva local em vez de mostrar
        um número de horas atrás com cara de agora."""
        agora = time.time()
        cota._uso = cota.extrair(PAYLOAD)
        cota._uso_ts = agora - cota.VALIDADE - 1
        self.assertEqual(cota.snapshot(agora), {})
        self.assertIsNone(cota.fable(agora))

    def test_o_nome_do_modelo_nao_depende_de_caixa(self):
        agora = time.time()
        cota._uso = {"modelos": {"FABLE 5": 33}}
        cota._uso_ts = agora
        self.assertEqual(cota.fable(agora), 33)


if __name__ == "__main__":
    unittest.main(verbosity=2)
