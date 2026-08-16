#!/usr/bin/env python3
"""Testes do livro-caixa amarrado ao ciclo de vida da API.

`test_works.py` prova a unidade isolada. Aqui prova-se a AMARRAÇÃO: quem abre,
quem marca pendente, quem fecha, e o que acontece quando o hook que deveria
fechar nunca chega — que é o caso comum, não o excepcional.

Nada aqui fala HTTP. As funções chamadas são as mesmas que os handlers chamam,
sob o mesmo lock, e é isso que se quer travar.
"""

import os
import tempfile
import time
import unittest

import claude_metrics_api as api
import works


class Base(unittest.TestCase):

    def setUp(self):
        fd, self.livro = tempfile.mkstemp(suffix=".jsonl")
        os.close(fd)
        os.unlink(self.livro)
        self._real = works.CAMINHO
        works.CAMINHO = self.livro          # o livro-caixa real não é tocado
        api._sessions.clear()
        self.agora = time.time()

    def tearDown(self):
        works.CAMINHO = self._real
        api._sessions.clear()
        if os.path.exists(self.livro):
            os.unlink(self.livro)

    def sessao(self, sid="s1", **dados):
        d = {"session_id": sid, "repo": "esp32-s3", "branch": "main",
             "model": "Opus 5", "cost_usd": 1.0, "api_ms": 1000,
             "lines_added": 10, "lines_removed": 2}
        d.update(dados)
        rec = api._sessions.setdefault(sid, {})
        rec["data"] = d
        rec["ts"] = self.agora
        rec.setdefault("state_ts", 0)
        rec["api_ms"] = d.get("api_ms")
        return rec

    def registros(self):
        return works.ler(self.livro)


class TestCicloNormal(Base):

    def test_prompt_abre_e_o_heartbeat_apos_o_stop_fecha(self):
        rec = self.sessao()
        api.abrir_trabalho(rec, "s1", self.agora)
        self.assertIn("work", rec)
        self.assertEqual(self.registros(), [])      # nada gravado ainda

        # Stop apenas MARCA. Fechar aqui perderia a cauda do turno: o último
        # heartbeat pode ser anterior à última chamada de API.
        rec["work"]["pendente_ts"] = self.agora + 100
        self.assertEqual(self.registros(), [])

        # O heartbeat seguinte traz os contadores completos.
        self.sessao(cost_usd=1.5, api_ms=9000, lines_added=47, lines_removed=14)
        api.fechar_trabalho(rec, self.agora + 110)

        regs = self.registros()
        self.assertEqual(len(regs), 1)
        r = regs[0]
        self.assertAlmostEqual(r["cost_usd"], 0.5)
        self.assertAlmostEqual(r["api_seconds"], 8.0)
        self.assertEqual(r["lines_added"], 37)
        self.assertAlmostEqual(r["seconds"], 110.0)
        self.assertNotIn("work", rec)
        for marca in ("partial", "aborted", "interrupted", "stalled"):
            self.assertNotIn(marca, r)

    def test_identidade_do_trabalho_vem_da_sessao(self):
        rec = self.sessao()
        api.abrir_trabalho(rec, "s1", self.agora)
        api.fechar_trabalho(rec, self.agora + 10)
        r = self.registros()[0]
        self.assertEqual(r["repo"], "esp32-s3")
        self.assertEqual(r["branch"], "main")
        self.assertEqual(r["model"], "Opus 5")


class TestFechamentosAnormais(Base):

    def test_prompt_novo_fecha_o_turno_anterior_que_ficou_aberto(self):
        # Esc no meio do turno: o `Stop` nunca dispara. Sem isto o trabalho
        # anterior seria abandonado e o novo herdaria a duração dele.
        rec = self.sessao()
        api.abrir_trabalho(rec, "s1", self.agora)
        api.abrir_trabalho(rec, "s1", self.agora + 300)

        regs = self.registros()
        self.assertEqual(len(regs), 1)
        self.assertTrue(regs[0]["stalled"])
        self.assertAlmostEqual(regs[0]["seconds"], 300.0)
        self.assertIn("work", rec)                  # o novo continua aberto

    def test_sessao_encerrada_com_trabalho_aberto_fecha_marcada(self):
        rec = self.sessao()
        api.abrir_trabalho(rec, "s1", self.agora)
        api.fechar_trabalho(rec, self.agora + 42, "aborted")
        self.assertTrue(self.registros()[0]["aborted"])

    def test_pendente_sem_heartbeat_fecha_como_parcial_no_prune(self):
        # A janela foi fechada logo depois do turno: nenhum heartbeat vem.
        # Um número honesto e marcado vale mais do que um número perdido.
        rec = self.sessao()
        rec["hooked"] = True
        api.abrir_trabalho(rec, "s1", self.agora)
        rec["work"]["pendente_ts"] = self.agora
        api._now = lambda: self.agora + api.FECHAMENTO_MAX_S + 1
        try:
            api.prune()
        finally:
            api._now = time.time
        regs = self.registros()
        self.assertEqual(len(regs), 1)
        self.assertTrue(regs[0]["partial"])

    def test_pendente_recente_ainda_espera_o_heartbeat(self):
        # 30 s de folga: o heartbeat da statusline chega a cada ~10 s.
        rec = self.sessao()
        rec["hooked"] = True
        api.abrir_trabalho(rec, "s1", self.agora)
        rec["work"]["pendente_ts"] = self.agora
        api._now = lambda: self.agora + 5
        try:
            api.prune()
        finally:
            api._now = time.time
        self.assertEqual(self.registros(), [])
        self.assertIn("work", rec)

    def test_trabalho_sai_da_sessao_mesmo_se_a_gravacao_falhar(self):
        # Deixá-lo aberto faria o próximo prompt fechá-lo de novo, agora com a
        # duração errada. Perder o registro é ruim; contar duas vezes é pior.
        rec = self.sessao()
        api.abrir_trabalho(rec, "s1", self.agora)
        works.CAMINHO = os.path.join(self.livro, "impossivel", "x.jsonl")
        api.fechar_trabalho(rec, self.agora + 10)
        self.assertNotIn("work", rec)

    def test_fechar_sem_trabalho_aberto_nao_faz_nada(self):
        rec = self.sessao()
        self.assertIsNone(api.fechar_trabalho(rec, self.agora))
        self.assertEqual(self.registros(), [])


class TestBloqueio(Base):

    def test_bloqueio_e_desbloqueio_entram_no_registro(self):
        rec = self.sessao(api_ms=1000)
        api.abrir_trabalho(rec, "s1", self.agora)

        works.bloquear(rec["work"], rec["api_ms"], self.agora + 10)
        self.assertEqual(rec["work"]["blocks"], 1)

        # Você respondeu: `api_ms` andou. É a única prova disponível — não
        # existe hook de "desbloqueou".
        rec["api_ms"] = 5000
        works.desbloquear(rec["work"], rec["api_ms"], self.agora + 70)

        api.fechar_trabalho(rec, self.agora + 100)
        r = self.registros()[0]
        self.assertAlmostEqual(r["blocked_seconds"], 60.0)
        self.assertEqual(r["blocks"], 1)

    def test_bloqueio_nunca_respondido_ainda_aparece(self):
        # Contado no desbloqueio, este caso sumiria do livro-caixa.
        rec = self.sessao()
        api.abrir_trabalho(rec, "s1", self.agora)
        works.bloquear(rec["work"], rec["api_ms"], self.agora + 10)
        api.fechar_trabalho(rec, self.agora + 610, "aborted")
        r = self.registros()[0]
        self.assertEqual(r["blocks"], 1)
        self.assertAlmostEqual(r["blocked_seconds"], 600.0)


class TestVao(Base):

    def test_heartbeat_parado_abre_vao_e_atividade_o_fecha(self):
        rec = self.sessao(api_ms=1000)
        api.abrir_trabalho(rec, "s1", self.agora)

        rec["api_ms"] = 1000
        works.atividade(rec["work"], rec["api_ms"], self.agora + 60)   # parado
        rec["api_ms"] = 4000
        works.atividade(rec["work"], rec["api_ms"], self.agora + 120)  # andou

        api.fechar_trabalho(rec, self.agora + 130)
        self.assertAlmostEqual(self.registros()[0]["gap_seconds"], 120.0)


class TestUsoNoStatus(unittest.TestCase):

    def test_status_traz_a_chave_uso(self):
        s = api.build_status()
        self.assertIn("uso", s)
        self.assertIn("modelos", s["uso"])
        self.assertIn("dia", s["uso"])
        self.assertIn("vigente", s["uso"])

    def test_uso_sai_mesmo_sem_sessao_viva(self):
        """O dia ja teve os turnos que teve: fechar o terminal nao zera."""
        with api._lock:
            api._sessions.clear()
        s = api.build_status()
        self.assertIn("uso", s)
        self.assertIsInstance(s["uso"]["modelos"], list)


class TestRecorteDoUso(unittest.TestCase):

    def test_hoje_filtra_pelo_inicio_do_dia(self):
        agora = time.time()
        regs = [
            {"started": agora, "cost_usd": 1.0,
             "tokens": {"claude-opus-5": {"output": 1000}}},
            {"started": agora - 3 * 86400, "cost_usd": 1.0,
             "tokens": {"claude-opus-5": {"output": 9999}}},
        ]
        hoje = api.uso_do_recorte(regs, "hoje", agora)
        tudo = api.uso_do_recorte(regs, "tudo", agora)
        self.assertEqual(hoje["modelos"][0]["tokens"], 1000)
        self.assertEqual(tudo["modelos"][0]["tokens"], 10999)

    def test_dia_explicito(self):
        agora = time.time()
        dia = time.strftime("%Y-%m-%d", time.localtime(agora))
        regs = [{"started": agora, "cost_usd": 1.0,
                 "tokens": {"claude-opus-5": {"output": 42}}}]
        out = api.uso_do_recorte(regs, dia, agora)
        self.assertEqual(out["modelos"][0]["tokens"], 42)
        self.assertEqual(out["dia"], dia)

    def test_recorte_invalido_cai_em_hoje(self):
        agora = time.time()
        out = api.uso_do_recorte([], "banana", agora)
        self.assertEqual(out["dia"],
                         time.strftime("%Y-%m-%d", time.localtime(agora)))


class TestVitalicioNoStatus(unittest.TestCase):
    """O bloco que a quarta tela do painel consome."""

    def test_status_traz_o_bloco_com_as_tres_chaves(self):
        st = api.build_status()
        self.assertIn("vitalicio", st)
        for k in ("turnos", "cost_usd", "desde"):
            self.assertIn(k, st["vitalicio"])

    def test_memoizado_nao_recalcula_com_o_livro_parado(self):
        # Duas leituras seguidas sem o arquivo mudar têm que devolver o MESMO
        # objeto. O painel pede /status a cada 2 s e o livro só cresce: varrer
        # 700+ registros 43 mil vezes por dia seria trabalho puro.
        a = api.vitalicio_do_livro()
        b = api.vitalicio_do_livro()
        self.assertIs(a, b)


if __name__ == "__main__":
    unittest.main(verbosity=2)
