#!/usr/bin/env python3
"""Testes da memória dos limites da conta. Só stdlib — `python test_memoria_limites.py`.

Existem por um defeito relatado: fechado o último Claude Code, o painel apagava
os dois limites (`-` no percentual e `-` no prazo) e ficava sem nada para dizer
sobre a conta. Os números não morrem junto com a sessão que os leu — a janela de
5h e a de 7 dias são da CONTA, e continuam correndo com o terminal fechado.

O que estes testes prendem no lugar:
  - o último snapshot bom sobrevive ao fim da sessão e volta a ser publicado;
  - ele vem com o carimbo de QUANDO foi visto, para que o painel não o mostre
    com cara de número fresco;
  - um snapshot cuja janela já venceu não é ressuscitado: a de 5h vira zero
    (ela reseta), a de 7 dias vira desconhecida (ela desliza).
"""

import time
import unittest

import claude_metrics_api as api


def sessao(sid, session_pct, session_at, week_pct, week_at, ts=None):
    """Uma sessão como o /ingest a teria gravado."""
    agora = ts if ts is not None else time.time()
    return sid, {
        "data": {
            "session_id": sid, "repo": sid, "model": "Opus 5 (1M context)",
            "context_pct": 30,
            "session_pct": session_pct, "session_resets_at": session_at,
            "week_pct": week_pct, "week_resets_at": week_at,
        },
        "ts": agora, "state_ts": 0, "limits_ts": agora,
    }


class TestLimitesLembrados(unittest.TestCase):
    """A função pura: o que a memória sustenta, sozinha, sem tocar em _sessions."""

    def setUp(self):
        self.agora = time.time()

    def memoria(self, sess_at, week_at, visto=None):
        quando = visto if visto is not None else self.agora - 3600
        return {
            "session": {"pct": 41, "at": sess_at, "ts": quando},
            "week": {"pct": 75, "at": week_at, "ts": quando},
        }

    def test_memoria_vazia_nao_afirma_nada(self):
        lem = api.limites_lembrados({}, self.agora)
        self.assertFalse(lem["session_known"])
        self.assertFalse(lem["week_known"])
        self.assertFalse(lem["memoria"])

    def test_snapshot_valido_volta_com_o_carimbo_de_quando_foi_visto(self):
        visto = self.agora - 1800
        mem = self.memoria(self.agora + 2 * 3600, self.agora + 3 * 86400, visto)
        lem = api.limites_lembrados(mem, self.agora)
        self.assertTrue(lem["memoria"])
        self.assertTrue(lem["session_known"])
        self.assertEqual(lem["session_pct"], 41)
        self.assertEqual(lem["session_resets_at"], int(self.agora + 2 * 3600))
        self.assertTrue(lem["week_known"])
        self.assertEqual(lem["week_pct"], 75)
        self.assertEqual(lem["visto"], visto)

    def test_janela_de_5h_vencida_vira_zero_e_nao_o_valor_velho(self):
        """A de 5h RESETA: o carimbo vencido é a prova de que o uso recomeçou."""
        mem = self.memoria(self.agora - 60, self.agora + 3 * 86400)
        lem = api.limites_lembrados(mem, self.agora)
        self.assertTrue(lem["session_known"])
        self.assertTrue(lem["session_inferred"])
        self.assertEqual(lem["session_pct"], 0)
        self.assertEqual(lem["session_resets_at"], 0)

    def test_janela_de_7d_vencida_nao_vira_zero(self):
        """A de 7 dias DESLIZA: vencida, ela não afirma nada — nem zero."""
        mem = self.memoria(self.agora + 2 * 3600, self.agora - 60)
        lem = api.limites_lembrados(mem, self.agora)
        self.assertFalse(lem["week_known"])
        self.assertEqual(lem["week_pct"], 0)

    def test_carimbo_absurdo_e_descartado(self):
        """O mesmo horizonte de sanidade da leitura vale para a memória."""
        mem = self.memoria(9999999999, 9999999999)
        lem = api.limites_lembrados(mem, self.agora)
        self.assertFalse(lem["session_known"])
        self.assertFalse(lem["week_known"])

    def test_o_visto_e_o_carimbo_mais_recente_dos_dois(self):
        mem = {
            "session": {"pct": 41, "at": self.agora + 3600, "ts": self.agora - 60},
            "week": {"pct": 75, "at": self.agora + 86400, "ts": self.agora - 9999},
        }
        self.assertEqual(api.limites_lembrados(mem, self.agora)["visto"],
                         self.agora - 60)


class TestMemoriaNoStatus(unittest.TestCase):
    """O caminho inteiro: uma sessão publica, ela some, o painel continua sabendo."""

    def setUp(self):
        self.agora = time.time()
        api._sessions.clear()
        api._limites_mem.clear()

    def tearDown(self):
        api._sessions.clear()
        api._limites_mem.clear()

    def test_sessao_viva_grava_a_memoria(self):
        sid, rec = sessao("a", 41, self.agora + 2 * 3600, 75, self.agora + 3 * 86400)
        api._sessions[sid] = rec
        api.build_status()
        self.assertEqual(api._limites_mem["session"]["pct"], 41)
        self.assertEqual(api._limites_mem["week"]["pct"], 75)

    def test_fechado_o_terminal_os_numeros_continuam_na_tela(self):
        """O defeito relatado, ponta a ponta."""
        sid, rec = sessao("a", 41, self.agora + 2 * 3600, 75, self.agora + 3 * 86400)
        api._sessions[sid] = rec
        api.build_status()

        api._sessions.clear()           # fechou o último Claude Code
        s = api.build_status()
        self.assertEqual(s["sessions"], 0)
        self.assertFalse(s["online"])
        self.assertTrue(s["session_known"])
        self.assertEqual(s["session_pct"], 41)
        self.assertNotEqual(s["session_resets_hm"], "-")
        self.assertTrue(s["week_known"])
        self.assertEqual(s["week_pct"], 75)
        # E o painel precisa poder dizer que aquilo é de antes.
        self.assertTrue(s["limites"]["memoria"])
        self.assertRegex(s["limites"]["visto_hm"], r"^\d{2}:\d{2}$")

    def test_numero_de_memoria_nao_se_diz_fresco(self):
        sid, rec = sessao("a", 41, self.agora + 2 * 3600, 75, self.agora + 3 * 86400)
        api._sessions[sid] = rec
        api.build_status()
        api._sessions.clear()
        self.assertFalse(api.build_status()["limits_fresh"])

    def test_sem_memoria_nenhuma_continua_sem_afirmar_nada(self):
        s = api.build_status()
        self.assertFalse(s["session_known"])
        self.assertFalse(s["week_known"])
        self.assertIsNone(s["limites"])

    def test_leitura_viva_ganha_da_memoria(self):
        """Memória é reserva, nunca fonte: com sessão publicando, ela não aparece."""
        api._limites_mem.update({
            "session": {"pct": 99, "at": self.agora + 3600, "ts": self.agora - 9999},
            "week": {"pct": 99, "at": self.agora + 86400, "ts": self.agora - 9999},
        })
        sid, rec = sessao("a", 41, self.agora + 2 * 3600, 75, self.agora + 3 * 86400)
        api._sessions[sid] = rec
        s = api.build_status()
        self.assertEqual(s["session_pct"], 41)
        self.assertIsNone(s["limites"])

    def test_sessao_viva_sem_janela_nenhuma_usa_a_memoria(self):
        """Terminal aberto que ainda não chamou a API: os números da conta valem."""
        api._limites_mem.update({
            "session": {"pct": 41, "at": self.agora + 3600, "ts": self.agora - 60},
            "week": {"pct": 75, "at": self.agora + 86400, "ts": self.agora - 60},
        })
        sid, rec = sessao("a", 0, 0, 0, 0)
        api._sessions[sid] = rec
        s = api.build_status()
        self.assertTrue(s["session_known"])
        self.assertEqual(s["session_pct"], 41)
        self.assertTrue(s["limites"]["memoria"])

    def test_so_a_faixa_lembrada_e_marcada_como_velha(self):
        """A semana viva não pode ficar cinza porque a de 5h é lembrança."""
        api._limites_mem.update({
            "session": {"pct": 41, "at": self.agora + 3600, "ts": self.agora - 60},
        })
        sid, rec = sessao("a", 0, 0, 75, self.agora + 3 * 86400)
        api._sessions[sid] = rec
        s = api.build_status()
        self.assertTrue(s["limites"]["session"])
        self.assertFalse(s["limites"]["week"])
        self.assertEqual(s["week_pct"], 75)


if __name__ == "__main__":
    unittest.main(verbosity=2)
