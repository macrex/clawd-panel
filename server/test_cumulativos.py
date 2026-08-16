#!/usr/bin/env python3
"""Testes dos contadores cumulativos chegando ao painel.

Custo, linhas e tempo de API ja chegavam da statusline e paravam no `/sessions`.
O painel le `/status`, entao para ele eles simplesmente nao existiam.

O que estas provas seguram: a diferenca entre "zero" e "nao sei". Uma sessao
recem-aberta gastou zero dolares; uma sessao com statusline antiga nao diz
quanto gastou. Publicar 0 nos dois casos apagaria essa diferenca, e o painel
passaria a afirmar um numero que ninguem mediu.
"""

import time
import unittest

import claude_metrics_api as api


class TestCumulativosNoStatus(unittest.TestCase):

    def setUp(self):
        self.agora = time.time()
        api._sessions.clear()

    def tearDown(self):
        api._sessions.clear()

    def sessao(self, sid, **dados):
        d = {"session_id": sid, "repo": sid, "context_pct": 30}
        d.update(dados)
        api._sessions[sid] = {
            "data": d, "ts": self.agora, "state_ts": 0,
            "limits_ts": self.agora, "state": api.WORKING,
        }

    def agente(self, sid):
        for a in api.build_status()["labels"]:
            if a["session_id"] == sid[:8]:
                return a
        self.fail(f"agente {sid} nao apareceu em labels")

    def test_os_quatro_contadores_chegam_no_agente(self):
        self.sessao("s1", cost_usd=436.04, lines_added=20963,
                    lines_removed=2208, api_ms=32985351)
        a = self.agente("s1")
        self.assertAlmostEqual(a["cost_usd"], 436.04)
        self.assertEqual(a["lines_added"], 20963)
        self.assertEqual(a["lines_removed"], 2208)
        self.assertEqual(a["api_ms"], 32985351)

    def test_sem_os_campos_sai_none_e_nao_zero(self):
        # Statusline anterior a esta versao. Zero seria uma afirmacao.
        self.sessao("s1")
        a = self.agente("s1")
        for campo in ("cost_usd", "lines_added", "lines_removed", "api_ms"):
            self.assertIsNone(a[campo], campo)

    def test_zero_publicado_continua_sendo_zero(self):
        # Sessao recem-aberta: gastou zero de verdade, e isso e diferente de
        # nao saber. O painel imprime "US$ 0.00" neste caso, e nada no anterior.
        self.sessao("s1", cost_usd=0, lines_added=0, lines_removed=0, api_ms=0)
        a = self.agente("s1")
        for campo in ("cost_usd", "lines_added", "lines_removed", "api_ms"):
            self.assertEqual(a[campo], 0, campo)

    def test_sao_por_sessao_e_nao_somados(self):
        # Cada agente carrega o SEU cumulativo. A soma nao e feita aqui de
        # proposito: a pagina do Clawd escreve o nome de um agente e os numeros
        # dele, e um total misturado nao pertenceria aquele nome.
        self.sessao("s1", cost_usd=436.04)
        self.sessao("s2", cost_usd=5.79)
        self.assertAlmostEqual(self.agente("s1")["cost_usd"], 436.04)
        self.assertAlmostEqual(self.agente("s2")["cost_usd"], 5.79)


if __name__ == "__main__":
    unittest.main(verbosity=2)
