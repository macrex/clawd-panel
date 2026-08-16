#!/usr/bin/env python3
"""Testes do momento ABSOLUTO em que cada limite reseta.

O painel ja mostrava quanto falta ("4h24m", "1d06h"). Isso responde uma
pergunta, e a outra — "entao, QUANDO?" — obrigava a fazer a conta de cabeca, no
fuso certo, com a virada do dia no meio. A conversao mora aqui porque a placa
nao tem relogio nem fuso, e esta maquina e a mesma que roda o Claude Code que
produziu o carimbo.
"""

import calendar
import time
import unittest

import claude_metrics_api as api


def local(ano, mes, dia, hora, minuto):
    """Epoch de um instante do calendario LOCAL desta maquina.

    `time.mktime` interpreta a tupla no fuso local, que e exatamente o mesmo
    fuso que `fmt_clock`/`fmt_date` usam para voltar. Assim o teste vale em
    qualquer maquina, sem carimbo magico.
    """
    return time.mktime((ano, mes, dia, hora, minuto, 0, 0, 0, -1))


class TestRelogio(unittest.TestCase):
    """A janela de 5h: hora do relogio, formato de 12h.

    O carimbo que entra e o INSTANTE DO RESET; o texto que sai e o ultimo
    minuto em que a janela ainda vale. E a convencao do `/cost`, e ela existe
    para os dois numeros baterem na mesa: janela que vira 15:10 em ponto e
    "3:09pm" no terminal, e agora tambem no painel.
    """

    def test_tarde_vira_pm(self):
        self.assertEqual(api.fmt_clock(local(2026, 8, 1, 19, 20)), "7:19pm")

    def test_manha_vira_am(self):
        self.assertEqual(api.fmt_clock(local(2026, 8, 1, 7, 20)), "7:19am")

    def test_meio_dia_e_12pm_e_nao_0pm(self):
        # `19 % 12` da 7, mas `12 % 12` da 0 — e "0:00pm" nao existe. O carimbo
        # e 12:01 para o minuto anterior cair DENTRO do meio-dia.
        self.assertEqual(api.fmt_clock(local(2026, 8, 1, 12, 1)), "12:00pm")

    def test_meia_noite_e_12am_e_nao_0am(self):
        self.assertEqual(api.fmt_clock(local(2026, 8, 1, 0, 6)), "12:05am")

    def test_minuto_tem_dois_digitos(self):
        # "7:5pm" seria lido como 7:50 num relance.
        self.assertEqual(api.fmt_clock(local(2026, 8, 1, 19, 6)), "7:05pm")

    def test_bate_com_o_cost(self):
        # Os dois carimbos da captura que abriu este caso: a sessao virando as
        # 15:10 e a semana virando as 23:00, com o `/cost` escrevendo 3:09pm e
        # 10:59pm ao lado.
        self.assertEqual(api.fmt_clock(local(2026, 8, 16, 15, 10)), "3:09pm")
        self.assertEqual(api.fmt_clock(local(2026, 8, 16, 23, 0)), "10:59pm")

    def test_carimbo_quebrado_formata_igual(self):
        # Sem segundo redondo o -1s nao muda o minuto exibido — o ajuste so
        # aparece onde a API carimba a virada em ponto, que e o caso real.
        self.assertEqual(api.fmt_clock(local(2026, 8, 16, 15, 9) + 30),
                         "3:09pm")


class TestData(unittest.TestCase):
    """A janela de 7 dias: data com o dia da semana junto."""

    def test_formato_completo(self):
        # 01/08/2026 e um sabado.
        self.assertEqual(api.fmt_date(local(2026, 8, 1, 19, 20)),
                         "01/08/2026 (Sabado)")

    def test_dia_e_mes_com_zero_a_esquerda(self):
        self.assertEqual(api.fmt_date(local(2026, 1, 5, 9, 0)),
                         "05/01/2026 (Segunda)")

    def test_dia_da_semana_bate_com_o_calendario(self):
        # Uma semana inteira, conferida contra a stdlib em vez de contra uma
        # tabela escrita a mao — que e justamente onde um deslocamento de um dia
        # passaria despercebido.
        nomes = api.DIAS_EXTENSO
        for dia in range(1, 8):
            wd = calendar.weekday(2026, 8, dia)
            self.assertIn(f"({nomes[wd]})", api.fmt_date(local(2026, 8, dia, 12, 0)))

    def test_sem_acento(self):
        # A fonte embutida do Arduino_GFX so tem ASCII: um acento sai como lixo
        # na tela, e "Terca"/"Sabado" sao os dois que tentariam.
        for nome in api.DIAS_EXTENSO:
            nome.encode("ascii")   # levanta se algum ganhar acento um dia


class TestNoStatus(unittest.TestCase):
    """Os campos chegando ao painel pelo /status."""

    def setUp(self):
        self.agora = time.time()
        api._sessions.clear()

    def tearDown(self):
        api._sessions.clear()

    def povoar(self, session_at, week_at):
        api._sessions["s1"] = {
            "data": {
                "session_id": "s1", "repo": "esp32-s3", "context_pct": 30,
                "session_pct": 41, "session_resets_at": session_at,
                "week_pct": 62, "week_resets_at": week_at,
            },
            "ts": self.agora, "state_ts": 0, "limits_ts": self.agora,
        }

    def test_os_dois_campos_saem_no_status(self):
        cinco = self.agora + 2 * 3600
        semana = self.agora + 3 * 86400
        self.povoar(cinco, semana)
        s = api.build_status()
        self.assertEqual(s["session_resets_clock"], api.fmt_clock(cinco))
        self.assertEqual(s["week_resets_date"], api.fmt_date(semana))

    def test_sem_carimbo_sai_traco_e_nao_uma_hora_inventada(self):
        # Zero nao e "reseta agora", e "nao sei quando" — a mesma regra que o
        # prazo relativo ja segue.
        self.povoar(0, 0)
        s = api.build_status()
        self.assertEqual(s["session_resets_clock"], "-")
        self.assertEqual(s["week_resets_date"], "-")
        self.assertEqual(s["session_resets_hm"], "-")

    def test_sem_sessao_nenhuma_tambem_sai_traco(self):
        # O outro caminho do build_status, que monta o payload a parte.
        s = api.build_status()
        self.assertEqual(s["session_resets_clock"], "-")
        self.assertEqual(s["week_resets_date"], "-")


if __name__ == "__main__":
    unittest.main(verbosity=2)
