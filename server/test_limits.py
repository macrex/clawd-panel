#!/usr/bin/env python3
"""Testes da escolha dos limites da conta. Só stdlib — `python test_limits.py`.

Existem por um bug real: uma sessão de teste publicou `session_resets_at:
9999999999` e o painel passou a mostrar `Session 32% 2281821h43m`. A regra de
escolha é "maior carimbo de reset", então um carimbo impossível ganha de todas
as sessões reais. Estes testes prendem o horizonte de sanidade no lugar.
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


class TestHorizonte(unittest.TestCase):

    def setUp(self):
        self.agora = time.time()
        api._sessions.clear()
        # A memoria dos limites e global como `_sessions`, e sobrevive ao
        # fim de um teste: sem limpar, o snapshot de um caso vira a
        # reserva do proximo e um "sem carimbo nenhum" sai com numero.
        api._limites_mem.clear()

    def tearDown(self):
        api._sessions.clear()
        api._limites_mem.clear()

    def povoar(self, *pares):
        for sid, rec in pares:
            api._sessions[sid] = rec

    def test_carimbo_absurdo_nao_sequestra_os_limites(self):
        """O bug relatado: uma sessão com 9999999999 apagava as reais."""
        self.povoar(
            sessao("real", 41, self.agora + 2 * 3600, 62, self.agora + 3 * 86400),
            sessao("test", 32, 9999999999, 61, 9999999999),
        )
        s = api.build_status()
        self.assertTrue(s["limits_fresh"])
        self.assertEqual(s["session_pct"], 41)          # a real, não a de teste
        # Tolerância porque `agora` foi capturado antes do build_status: sem
        # ela o teste falha por milissegundos, não por defeito.
        self.assertAlmostEqual(s["session_resets_in"], 2 * 3600, delta=5)
        self.assertAlmostEqual(s["week_resets_in"], 3 * 86400, delta=5)

    def test_carimbo_no_passado_nao_e_lido_como_atual(self):
        """O snapshot vencido nunca vira o número exibido.

        Os 90% da janela anterior não podem sobreviver à virada dela. O que
        aparece é 0% — o uso da janela NOVA — e nunca o valor velho.
        """
        self.povoar(sessao("velha", 90, self.agora - 60, 90, self.agora - 60))
        s = api.build_status()
        self.assertFalse(s["limits_fresh"])
        self.assertEqual(s["session_pct"], 0)
        self.assertEqual(s["session_label"], "Session 0%")   # sem prazo: a janela virou
        # A semana DESLIZA, entao vencida ela nao autoriza concluir nada.
        self.assertFalse(s["week_known"])
        self.assertEqual(s["week_label"], "Week —")

    def test_semanal_podre_nao_derruba_o_de_cinco_horas(self):
        """Cada carimbo é conferido por si: um são sobrevive ao outro podre."""
        self.povoar(sessao("meia", 41, self.agora + 3600, 62, 9999999999))
        s = api.build_status()
        self.assertTrue(s["session_known"])
        self.assertAlmostEqual(s["session_resets_in"], 3600, delta=5)
        self.assertFalse(s["week_known"])
        self.assertEqual(s["week_resets_in"], 0)        # sem prazo inventado
        self.assertEqual(s["week_resets_dh"], "-")

    def test_limite_do_horizonte(self):
        """Dentro da folga passa; um segundo além dela, não."""
        dentro = self.agora + api.SESSION_HORIZON - 60
        fora = self.agora + api.SESSION_HORIZON + 60
        self.assertTrue(api.valid_reset(dentro, self.agora, api.SESSION_HORIZON))
        self.assertFalse(api.valid_reset(fora, self.agora, api.SESSION_HORIZON))

    def test_tipos_invalidos_viram_zero(self):
        """Payload malformado não pode levantar exceção dentro do /status."""
        for lixo in (None, "amanha", [], {}, True):
            self.assertEqual(api.valid_reset(lixo, self.agora, api.SESSION_HORIZON), 0)

    def test_formatacao_dos_prazos(self):
        """Função pura, sem relógio no meio — o formato exato mora aqui."""
        self.assertEqual(api.fmt_hm(2 * 3600), "2h00m")
        self.assertEqual(api.fmt_hm(3600 + 35 * 60), "1h35m")
        self.assertEqual(api.fmt_hm(-10), "0h00m")      # nunca prazo negativo
        self.assertEqual(api.fmt_dh(3 * 86400), "3d00h")
        self.assertEqual(api.fmt_dh(2 * 86400 + 12 * 3600), "2d12h")

    def test_janela_de_5h_vencida_nao_derruba_a_semana(self):
        """O defeito relatado, parte 1.

        A janela de 5h vira e nenhuma sessao republicou ainda. A SEMANA continua
        valida por mais dois dias — e ia para zero junto, so porque as duas
        dependiam do mesmo `limits_fresh`.
        """
        self.povoar(sessao("a", 32, self.agora - 120, 61, self.agora + 2 * 86400),
                    sessao("b", 40, self.agora - 120, 62, self.agora + 2 * 86400))
        s = api.build_status()
        self.assertTrue(s["week_known"])
        self.assertEqual(s["week_pct"], 62)             # o maior carimbo semanal valido
        self.assertAlmostEqual(s["week_resets_in"], 2 * 86400, delta=5)
        self.assertNotEqual(s["week_label"], "Week —")

    def test_janela_de_5h_vencida_significa_uso_zerado(self):
        """O defeito relatado, parte 2.

        Janela FIXA vencida = uso recomecou. Isso nao e palpite: no instante da
        virada uma sessao publicou `session_pct: 0` sozinha. Reportar "—" fazia o
        painel esconder o fato mais acionavel que existe, que e "voce tem cota
        inteira".
        """
        self.povoar(sessao("a", 32, self.agora - 120, 61, self.agora + 2 * 86400))
        s = api.build_status()
        self.assertTrue(s["session_known"])
        self.assertTrue(s["session_inferred"])
        self.assertEqual(s["session_pct"], 0)
        # O PRAZO continua desconhecido: a proxima janela so comeca quando
        # alguem usar, e inventar um horario seria a mentira que o zero nao e.
        self.assertEqual(s["session_resets_in"], 0)
        self.assertEqual(s["session_resets_hm"], "-")

    def test_snapshot_valido_nao_e_marcado_como_inferido(self):
        self.povoar(sessao("a", 41, self.agora + 2 * 3600, 62, self.agora + 3 * 86400))
        s = api.build_status()
        self.assertTrue(s["session_known"])
        self.assertFalse(s["session_inferred"])
        self.assertEqual(s["session_pct"], 41)

    def test_semana_vencida_nao_derruba_as_5h(self):
        """O inverso: a semana venceu e as 5h estao boas. Cada uma por si."""
        self.povoar(sessao("a", 41, self.agora + 2 * 3600, 90, self.agora - 60))
        s = api.build_status()
        self.assertTrue(s["session_known"])
        self.assertEqual(s["session_pct"], 41)
        self.assertFalse(s["week_known"])
        self.assertEqual(s["week_label"], "Week —")

    def test_sessao_e_semana_saem_de_sessoes_diferentes(self):
        """Nada obriga o melhor snapshot de 5h e o melhor semanal a serem da
        mesma sessao. Antes, um so registro ditava os dois."""
        self.povoar(
            sessao("cinco_horas", 45, self.agora + 4 * 3600, 10, self.agora - 60),
            sessao("semana", 5, self.agora - 60, 62, self.agora + 3 * 86400),
        )
        s = api.build_status()
        self.assertEqual(s["session_pct"], 45)
        self.assertEqual(s["week_pct"], 62)

    def test_sem_nenhum_carimbo_continua_admitindo_que_nao_sabe(self):
        """Sessao que nunca publicou limite nenhum: nao ha janela para virar,
        entao nao da para inferir zero."""
        self.povoar(sessao("a", 0, 0, 0, 0))
        s = api.build_status()
        self.assertFalse(s["session_known"])
        self.assertFalse(s["session_inferred"])
        self.assertFalse(s["week_known"])
        self.assertEqual(s["session_label"], "Session —")

    def test_maior_carimbo_valido_ainda_vence(self):
        """A regra original: dentro do horizonte, a janela vigente é a maior."""
        self.povoar(
            sessao("antiga", 20, self.agora + 600, 50, self.agora + 86400),
            sessao("nova", 45, self.agora + 4 * 3600, 62, self.agora + 3 * 86400),
        )
        s = api.build_status()
        self.assertEqual(s["session_pct"], 45)


if __name__ == "__main__":
    unittest.main(verbosity=2)
