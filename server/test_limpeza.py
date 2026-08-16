#!/usr/bin/env python3
"""Testes da limpeza de contexto disparada pelo painel.

Duas coisas nascem juntas aqui e as duas sao delicadas:

  - o painel so conhece 8 caracteres do id da sessao, entao o comando chega por
    PREFIXO — e um prefixo que casa com duas sessoes nao pode escolher uma;
  - enquanto a limpeza esta acesa a turma do rodape varre, entao ela precisa
    apagar sozinha. Um sinal que so acende e uma tela mentindo para sempre.
"""

import time
import unittest

import claude_metrics_api as api


class TestResolverSessao(unittest.TestCase):

    def test_id_inteiro_casa_consigo(self):
        s = {"817d452c-2ff3-4844": {}, "91bac378-aaaa": {}}
        self.assertEqual(api.resolver_sessao(s, "817d452c-2ff3-4844"),
                         "817d452c-2ff3-4844")

    def test_prefixo_de_oito_casa(self):
        # E o que o painel manda: `labels[].session_id` sao os 8 primeiros.
        s = {"817d452c-2ff3-4844": {}, "91bac378-aaaa": {}}
        self.assertEqual(api.resolver_sessao(s, "817d452c"), "817d452c-2ff3-4844")

    def test_prefixo_ambiguo_nao_escolhe(self):
        # Duas sessoes com o mesmo prefixo: mandar para a errada e pior do que
        # nao mandar. Improvavel com 8 caracteres, catastrofico se acontecer.
        s = {"817d452c-um": {}, "817d452c-dois": {}}
        self.assertIsNone(api.resolver_sessao(s, "817d452c"))

    def test_prefixo_que_nao_casa_com_ninguem(self):
        self.assertIsNone(api.resolver_sessao({"abc": {}}, "zzz"))

    def test_id_vazio_nunca_casa(self):
        # "" e prefixo de TODA string: sem esta guarda, um pedido sem id pegaria
        # a primeira sessao da lista.
        self.assertIsNone(api.resolver_sessao({"abc": {}}, ""))
        self.assertIsNone(api.resolver_sessao({"abc": {}}, None))


class TestLimpando(unittest.TestCase):

    def rec(self, desde, ctx_ref, ctx_agora):
        return {"limpeza_ts": desde, "limpeza_ctx": ctx_ref,
                "data": {"context_pct": ctx_agora}}

    def test_sessao_sem_comando_nunca_esta_limpando(self):
        self.assertFalse(api.limpando({"data": {}}, time.time()))

    def test_acabou_de_disparar_esta_limpando(self):
        agora = time.time()
        self.assertTrue(api.limpando(self.rec(agora, 68, 68), agora))

    def test_contexto_encolheu_significa_que_acabou(self):
        # A prova direta: compactar reduz o contexto. Nao e palpite de tempo.
        agora = time.time()
        self.assertFalse(api.limpando(self.rec(agora, 68, 12), agora))

    def test_contexto_que_cresceu_nao_encerra(self):
        # O turno seguiu e o contexto voltou a subir ANTES da statusline mostrar
        # a queda. Encerrar aqui apagaria a varrida no meio dela.
        agora = time.time()
        self.assertTrue(api.limpando(self.rec(agora, 68, 70), agora))

    def test_prazo_estourado_apaga_sozinho(self):
        # Sem prova nenhuma a API para de afirmar: um toque nao pode deixar a
        # turma varrendo para sempre.
        agora = time.time()
        r = self.rec(agora - api.LIMPEZA_MAX_S - 1, 68, 68)
        self.assertFalse(api.limpando(r, agora))

    def test_sem_contexto_conhecido_vale_so_o_prazo(self):
        # `context_pct` null: nao da para provar o fim, entao segura ate o teto.
        agora = time.time()
        self.assertTrue(api.limpando(self.rec(agora, None, None), agora))
        self.assertFalse(api.limpando(
            self.rec(agora - api.LIMPEZA_MAX_S - 1, None, None), agora))

    def test_carimbo_invalido_nao_acende(self):
        for lixo in ("agora", True, None, [], {}):
            self.assertFalse(api.limpando({"limpeza_ts": lixo, "data": {}},
                                          time.time()), repr(lixo))


class TestNoStatus(unittest.TestCase):

    def setUp(self):
        self.agora = time.time()
        api._sessions.clear()

    def tearDown(self):
        api._sessions.clear()

    def sessao(self, sid, **extra):
        rec = {
            "data": {"session_id": sid, "repo": sid, "context_pct": 68},
            "ts": self.agora, "state_ts": 0, "limits_ts": self.agora,
            "state": api.WORKING,
        }
        rec.update(extra)
        api._sessions[sid] = rec
        return rec

    def test_sem_comando_nenhum_o_campo_sai_falso(self):
        self.sessao("s1")
        self.assertFalse(api.build_status()["cleaning"])

    def test_uma_sessao_limpando_acende_o_campo(self):
        # O campo e GLOBAL: quem varre e a turma do rodape, que e uma so.
        self.sessao("s1")
        self.sessao("s2", limpeza_ts=self.agora, limpeza_ctx=68)
        self.assertTrue(api.build_status()["cleaning"])

    def test_sem_sessao_nenhuma_o_campo_existe_e_e_falso(self):
        # O outro caminho do build_status monta o payload a parte; sem o campo
        # aqui, o firmware leria o default e nada quebraria — mas o payload
        # ficaria com forma diferente conforme o momento do dia.
        self.assertIn("cleaning", api.build_status())
        self.assertFalse(api.build_status()["cleaning"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
