# server/test_fusao.py
import unittest

import claude_metrics_api as api


def nossa(sid, pane, estado):
    return {"sid": sid, "pane_id": pane, "state": estado}


def dele(pane, estado, agente="claude", repo="x"):
    return {"pane_id": pane, "state": estado, "agent": agente,
            "cwd": "/tmp/" + repo, "repo": repo, "focused": False, "name": ""}


class TesteFusao(unittest.TestCase):

    def test_herdr_ganha_quando_tem_certeza(self):
        # O caso de 31/07: a nossa API dizia working ha 644 s e o herdr ja sabia
        # que a sessao estava parada.
        estados, _, _ = api.fundir([nossa("s1", "w0:p1", api.WORKING)],
                                   [dele("w0:p1", "idle")])
        self.assertEqual(estados["s1"], api.IDLE)

    def test_unknown_do_herdr_nao_derruba_o_nosso(self):
        estados, _, _ = api.fundir([nossa("s1", "w0:p1", api.BLOCKED)],
                                   [dele("w0:p1", "unknown")])
        self.assertEqual(estados["s1"], api.BLOCKED)

    def test_sessao_sem_pane_fica_intocada(self):
        # Claude Code aberto fora do herdr.
        estados, _, _ = api.fundir([nossa("s1", None, api.WORKING)],
                                   [dele("w0:p1", "idle")])
        self.assertEqual(estados["s1"], api.WORKING)

    def test_pane_que_o_herdr_nao_conhece_fica_intocado(self):
        estados, _, _ = api.fundir([nossa("s1", "w9:p9", api.WORKING)],
                                   [dele("w0:p1", "idle")])
        self.assertEqual(estados["s1"], api.WORKING)

    def test_done_vira_idle_e_e_sinalizado(self):
        # A placa mapeia string desconhecida para Unknown (status.cpp:20), entao
        # mandar "done" faria um agente que TERMINOU aparecer como "estado
        # desconhecido". Vai como idle, com a informacao ao lado.
        estados, concluidos, _ = api.fundir([nossa("s1", "w0:p1", api.WORKING)],
                                            [dele("w0:p1", "done")])
        self.assertEqual(estados["s1"], api.IDLE)
        self.assertIn("s1", concluidos)

    def test_agente_sem_par_vira_orfao(self):
        _, _, orfaos = api.fundir([], [dele("w2:p3", "working", agente="codex",
                                            repo="proj")])
        self.assertEqual(len(orfaos), 1)
        self.assertEqual(orfaos[0]["agent"], "codex")
        self.assertEqual(orfaos[0]["repo"], "proj")

    def test_pane_pareado_nao_vira_orfao(self):
        _, _, orfaos = api.fundir([nossa("s1", "w0:p1", api.WORKING)],
                                  [dele("w0:p1", "working")])
        self.assertEqual(orfaos, [])

    def test_estado_desconhecido_do_herdr_e_ignorado(self):
        # Uma versao futura pode inventar um estado novo. Nao adivinhamos.
        estados, _, _ = api.fundir([nossa("s1", "w0:p1", api.WORKING)],
                                   [dele("w0:p1", "hibernando")])
        self.assertEqual(estados["s1"], api.WORKING)

    def test_dois_panes_com_a_mesma_sessao(self):
        # Nao deve acontecer, mas se acontecer nao pode explodir.
        estados, _, orfaos = api.fundir(
            [nossa("s1", "w0:p1", api.WORKING)],
            [dele("w0:p1", "idle"), dele("w0:p1", "blocked")])
        self.assertIn(estados["s1"], (api.IDLE, api.BLOCKED))
        self.assertEqual(orfaos, [])

    def test_retrato_vazio_nao_muda_nada(self):
        estados, concluidos, orfaos = api.fundir(
            [nossa("s1", "w0:p1", api.WORKING)], [])
        self.assertEqual(estados["s1"], api.WORKING)
        self.assertEqual(concluidos, set())
        self.assertEqual(orfaos, [])


class TesteDivergencia(unittest.TestCase):

    def setUp(self):
        self.mem = {}

    def test_divergencia_curta_nao_loga(self):
        # Os dois sistemas nao mudam de estado no mesmo instante; discordar por
        # um segundo e normal e logar isso seria ruido.
        self.assertFalse(api.divergencia("s1", "working", "idle", 100.0, self.mem))
        self.assertFalse(api.divergencia("s1", "working", "idle", 120.0, self.mem))

    def test_divergencia_sustentada_loga_uma_vez(self):
        api.divergencia("s1", "working", "idle", 100.0, self.mem)
        self.assertTrue(api.divergencia("s1", "working", "idle", 131.0, self.mem))
        # Ja avisou: nao repete a cada consulta.
        self.assertFalse(api.divergencia("s1", "working", "idle", 200.0, self.mem))

    def test_concordar_zera_a_memoria(self):
        api.divergencia("s1", "working", "idle", 100.0, self.mem)
        api.divergencia("s1", "idle", "idle", 110.0, self.mem)
        self.assertFalse(api.divergencia("s1", "working", "idle", 131.0, self.mem))

    def test_sessoes_sao_independentes(self):
        api.divergencia("s1", "working", "idle", 100.0, self.mem)
        self.assertFalse(api.divergencia("s2", "working", "idle", 131.0, self.mem))


class TesteDivergenciaLog(unittest.TestCase):
    """`print()` nao escreve nada em producao (pythonw.exe destacado, sem
    stdout) — o registro que sobrevive para /health e este deque em memoria."""

    def setUp(self):
        api._divergencias.clear()
        api._divergencia_log.clear()

    def tearDown(self):
        api._divergencias.clear()
        api._divergencia_log.clear()

    def test_divergencia_sustentada_fica_no_log_em_memoria(self):
        nossas = [nossa("s1", "w0:p1", api.WORKING)]
        agentes = [dele("w0:p1", "idle")]
        # Primeira chamada so comeca a contar o episodio; a segunda, apos
        # DIVERGENCIA_S, e que confirma e loga.
        api.fundir(nossas, agentes, now=100.0, memoria=api._divergencias)
        self.assertEqual(len(api._divergencia_log), 0)
        api.fundir(nossas, agentes, now=100.0 + api.DIVERGENCIA_S + 1,
                   memoria=api._divergencias)

        self.assertEqual(len(api._divergencia_log), 1)
        registro = api._divergencia_log[-1]
        self.assertEqual(registro["sid"], "s1")
        self.assertEqual(registro["nosso"], api.WORKING)
        self.assertEqual(registro["herdr"], api.IDLE)
        self.assertEqual(registro["pane"], "w0:p1")

    def test_log_tem_teto_maxlen(self):
        self.assertEqual(api._divergencia_log.maxlen, api.DIVERGENCIA_LOG_MAX)


if __name__ == "__main__":
    unittest.main(verbosity=2)
