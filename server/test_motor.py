#!/usr/bin/env python3
"""Testes da escolha de motor.

A escolha tem uma evidencia so — o herdr esta respondendo? — e uma histerese.
A histerese existe porque o herdr pisca: uma consulta que estoura o timeout num
pico de carga nao e o herdr estar fora, e trocar de motor a cada piscada faria o
indicador do painel tremular.
"""

import unittest
from unittest import mock

import motor


class TesteEscolha(unittest.TestCase):

    def setUp(self):
        motor.reset()

    def test_comeca_em_hooks(self):
        # O motor que nao depende de nada externo e o padrao seguro: ate provar
        # que o herdr esta la, quem responde e a nossa deducao.
        self.assertEqual(motor.atual(), motor.HOOKS)

    def test_tres_avaliacoes_sobem_para_herdr(self):
        self.assertEqual(motor.avaliar(True, 100.0), motor.HOOKS)
        self.assertEqual(motor.avaliar(True, 102.0), motor.HOOKS)
        self.assertEqual(motor.avaliar(True, 104.0), motor.HERDR)

    def test_uma_piscada_nao_derruba(self):
        for t in (100.0, 102.0, 104.0):
            motor.avaliar(True, t)
        self.assertEqual(motor.atual(), motor.HERDR)
        # O herdr sumiu por UMA consulta. Nao e motivo para trocar.
        self.assertEqual(motor.avaliar(False, 106.0), motor.HERDR)
        self.assertEqual(motor.avaliar(True, 108.0), motor.HERDR)

    def test_duas_discordantes_e_uma_concordante_nao_trocam(self):
        for t in (100.0, 102.0, 104.0):
            motor.avaliar(True, t)
        motor.avaliar(False, 106.0)
        motor.avaliar(False, 108.0)
        motor.avaliar(True, 110.0)          # zera a contagem
        self.assertEqual(motor.atual(), motor.HERDR)
        self.assertEqual(motor.avaliar(False, 112.0), motor.HERDR)

    def test_tres_seguidas_derrubam_para_hooks(self):
        for t in (100.0, 102.0, 104.0):
            motor.avaliar(True, t)
        motor.avaliar(False, 106.0)
        motor.avaliar(False, 108.0)
        self.assertEqual(motor.avaliar(False, 110.0), motor.HOOKS)

    def test_troca_carimba_e_conta(self):
        for t in (100.0, 102.0, 104.0):
            motor.avaliar(True, t)
        s = motor.status()
        self.assertEqual(s["atual"], motor.HERDR)
        self.assertEqual(s["desde"], 104.0)
        self.assertEqual(s["trocas"], 1)

    def test_permanecer_no_mesmo_motor_nao_conta_troca(self):
        for t in (100.0, 102.0, 104.0, 106.0, 108.0):
            motor.avaliar(True, t)
        self.assertEqual(motor.status()["trocas"], 1)

    def test_desde_do_boot_e_a_primeira_avaliacao(self):
        # Sem isto, /health mostraria "desde 1970" ate a primeira troca.
        motor.avaliar(False, 500.0)
        self.assertEqual(motor.status()["desde"], 500.0)

    def test_nunca_avaliado_desde_nao_e_none(self):
        # Herdr nunca instalado: `avaliar` nunca roda (ver `main()` em
        # claude_metrics_api.py). Sem o fallback de boot, "desde" ficaria
        # `None` para sempre, e /health espera um numero.
        s = motor.status()
        self.assertIsNotNone(s["desde"])
        self.assertIsInstance(s["desde"], float)


class TesteVivacidade(unittest.TestCase):
    """`avaliar` so e chamada por uma thread, em producao: a do sensor do
    herdr. Se essa thread travar (sem lancar excecao — `_laco` so engole
    excecao, nao travamento), `atual()` nao pode continuar devolvendo o ultimo
    motor escolhido para sempre: precisa cair para HOOKS, o motor que nao
    depende de nenhuma thread externa continuar viva.
    """

    def setUp(self):
        motor.reset()

    def _sobe_para_herdr(self, agora_monotonic):
        with mock.patch("motor.time.monotonic", return_value=agora_monotonic):
            for t in (100.0, 102.0, 104.0):
                motor.avaliar(True, t)

    def test_avaliacao_recente_devolve_motor_escolhido(self):
        self._sobe_para_herdr(1000.0)
        with mock.patch("motor.time.monotonic", return_value=1000.0 + 5):
            self.assertEqual(motor.atual(), motor.HERDR)

    def test_avaliacao_velha_devolve_hooks_mesmo_com_atual_em_herdr(self):
        self._sobe_para_herdr(1000.0)
        depois = 1000.0 + motor.AVALIACAO_MAX_IDADE_S + 1
        with mock.patch("motor.time.monotonic", return_value=depois):
            # O valor guardado continua HERDR — so a DECISAO cai para HOOKS.
            self.assertEqual(motor._estado["atual"], motor.HERDR)
            self.assertEqual(motor.atual(), motor.HOOKS)

    def test_status_reflete_o_valor_guardado_e_nao_a_decisao(self):
        # status() e diagnostico, nao decisao: continua mostrando HERDR mesmo
        # quando `atual()` ja caiu para HOOKS por vivacidade velha.
        self._sobe_para_herdr(1000.0)
        depois = 1000.0 + motor.AVALIACAO_MAX_IDADE_S + 1
        with mock.patch("motor.time.monotonic", return_value=depois):
            self.assertEqual(motor.atual(), motor.HOOKS)
            self.assertEqual(motor.status()["atual"], motor.HERDR)

    def test_nunca_avaliado_atual_e_hooks(self):
        # viva = None: nunca houve avaliacao nesta vida do processo.
        self.assertEqual(motor.atual(), motor.HOOKS)


if __name__ == "__main__":
    unittest.main(verbosity=2)
